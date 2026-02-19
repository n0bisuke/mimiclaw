#include "discord_server.h"
#include "atom_config.h"
#include "bus/message_bus.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"

/* Ed25519 verification via PSA Crypto API (ESP-IDF 5.x / mbedTLS 3.x).
 *
 * Required sdkconfig keys (set in sdkconfig.defaults.atomclaw):
 *   CONFIG_MBEDTLS_PSA_CRYPTO_C=y
 *   CONFIG_MBEDTLS_ECP_DP_CURVE25519_ENABLED=y
 *
 * These together enable PSA_ECC_FAMILY_TWISTED_EDWARDS (Ed25519) support.
 * No external library needed — uses ESP-IDF's built-in mbedTLS.
 * Binary overhead: ~100-150 KB (part of mbedTLS already linked for TLS).
 */
#include "psa/crypto.h"

static const char *TAG = "discord";

/* ── Configuration ─────────────────────────────────────────────────────── */

static char s_app_id[32]   = ATOM_SECRET_DISCORD_APP_ID;
static char s_pub_key[65]  = ATOM_SECRET_DISCORD_PUBLIC_KEY;  /* 64-char hex */
static httpd_handle_t s_server = NULL;
static bool s_psa_init = false;

/* ── Hex helpers ─────────────────────────────────────────────────────── */

static int hex_decode(const char *hex, uint8_t *out, size_t out_len)
{
    size_t hex_len = strlen(hex);
    if (hex_len != out_len * 2) return -1;
    for (size_t i = 0; i < out_len; i++) {
        char hi = hex[i*2], lo = hex[i*2+1];
        int vh = (hi>='0'&&hi<='9') ? hi-'0'
               : (hi>='a'&&hi<='f') ? hi-'a'+10
               : (hi>='A'&&hi<='F') ? hi-'A'+10 : -1;
        int vl = (lo>='0'&&lo<='9') ? lo-'0'
               : (lo>='a'&&lo<='f') ? lo-'a'+10
               : (lo>='A'&&lo<='F') ? lo-'A'+10 : -1;
        if (vh < 0 || vl < 0) return -1;
        out[i] = (uint8_t)((vh << 4) | vl);
    }
    return 0;
}

/* ── Ed25519 verification via PSA Crypto ─────────────────────────────── */

static esp_err_t ed25519_verify_psa(const uint8_t *pubkey,
                                     const uint8_t *sig,
                                     const uint8_t *msg, size_t msg_len)
{
    if (!s_psa_init) {
        psa_status_t st = psa_crypto_init();
        if (st != PSA_SUCCESS) {
            ESP_LOGE(TAG, "PSA crypto init failed: %d", (int)st);
            return ESP_FAIL;
        }
        s_psa_init = true;
    }

    psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attrs,
        PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_TWISTED_EDWARDS));
    psa_set_key_bits(&attrs, 255);
    psa_set_key_usage_flags(&attrs, PSA_KEY_USAGE_VERIFY_MESSAGE);
    psa_set_key_algorithm(&attrs, PSA_ALG_PURE_EDDSA);

    psa_key_id_t key_id;
    psa_status_t st = psa_import_key(&attrs, pubkey, 32, &key_id);
    if (st != PSA_SUCCESS) {
        ESP_LOGE(TAG, "PSA import key failed: %d", (int)st);
        return ESP_FAIL;
    }

    st = psa_verify_message(key_id, PSA_ALG_PURE_EDDSA, msg, msg_len, sig, 64);
    psa_destroy_key(key_id);

    if (st != PSA_SUCCESS) {
        ESP_LOGW(TAG, "Ed25519 verify failed: %d", (int)st);
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

/* ── Discord signature verification ─────────────────────────────────── */

static esp_err_t verify_discord_signature(
    const char *sig_hex,
    const char *timestamp,
    const uint8_t *body,
    size_t body_len)
{
    uint8_t pubkey[32], sig[64];

    if (hex_decode(s_pub_key, pubkey, 32) != 0) {
        ESP_LOGE(TAG, "Invalid public key hex (need 64 chars)");
        return ESP_ERR_INVALID_ARG;
    }
    if (hex_decode(sig_hex, sig, 64) != 0) {
        ESP_LOGE(TAG, "Invalid signature hex (need 128 chars)");
        return ESP_ERR_INVALID_ARG;
    }

    /* message = timestamp || body */
    size_t ts_len  = strlen(timestamp);
    size_t msg_len = ts_len + body_len;
    uint8_t *msg   = malloc(msg_len);
    if (!msg) return ESP_ERR_NO_MEM;
    memcpy(msg,          timestamp, ts_len);
    memcpy(msg + ts_len, body,      body_len);

    esp_err_t ret = ed25519_verify_psa(pubkey, sig, msg, msg_len);
    free(msg);
    return ret;
}

/* ── HTTP utility: read full request body ────────────────────────────── */

static esp_err_t read_body(httpd_req_t *req, char **out, size_t *out_len)
{
    size_t len = req->content_len;
    if (len == 0 || len > 8192) return ESP_ERR_INVALID_SIZE;

    char *buf = malloc(len + 1);
    if (!buf) return ESP_ERR_NO_MEM;

    size_t off = 0;
    while (off < len) {
        int received = httpd_req_recv(req, buf + off, len - off);
        if (received <= 0) {
            free(buf);
            return ESP_FAIL;
        }
        off += (size_t)received;
    }
    buf[off] = '\0';
    *out     = buf;
    *out_len = off;
    return ESP_OK;
}

/* ── /interactions POST handler ──────────────────────────────────────── */

static esp_err_t interactions_handler(httpd_req_t *req)
{
    char sig_hex[130]  = {0};
    char timestamp[32] = {0};

    if (httpd_req_get_hdr_value_str(req, "X-Signature-Ed25519",
                                    sig_hex, sizeof(sig_hex)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Missing signature");
        return ESP_FAIL;
    }
    if (httpd_req_get_hdr_value_str(req, "X-Signature-Timestamp",
                                    timestamp, sizeof(timestamp)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Missing timestamp");
        return ESP_FAIL;
    }

    char *body    = NULL;
    size_t body_len = 0;
    if (read_body(req, &body, &body_len) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad body");
        return ESP_FAIL;
    }

    /* Ed25519 verify (skip only if no key configured, e.g. during local dev) */
    if (s_pub_key[0] != '\0') {
        esp_err_t verr = verify_discord_signature(sig_hex, timestamp,
                                                   (uint8_t *)body, body_len);
        if (verr != ESP_OK) {
            free(body);
            httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Invalid signature");
            return ESP_FAIL;
        }
    } else {
        ESP_LOGW(TAG, "No public key — skipping Ed25519 (dev mode only)");
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    int interaction_type = 0;
    cJSON *type_j = cJSON_GetObjectItem(root, "type");
    if (type_j) interaction_type = type_j->valueint;

    /* PING */
    if (interaction_type == 1) {
        cJSON_Delete(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"type\":1}");
        return ESP_OK;
    }

    /* APPLICATION_COMMAND (2) or MESSAGE_COMPONENT (3) */
    if (interaction_type != 2 && interaction_type != 3) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unsupported type");
        return ESP_FAIL;
    }

    cJSON *token_j             = cJSON_GetObjectItem(root, "token");
    const char *itoken         = token_j ? token_j->valuestring : NULL;
    if (!itoken) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No token");
        return ESP_FAIL;
    }

    /* User ID */
    char user_id[32] = "unknown";
    cJSON *member = cJSON_GetObjectItem(root, "member");
    {
        cJSON *src = member ? member : cJSON_GetObjectItem(root, "user");
        cJSON *user = member ? cJSON_GetObjectItem(src, "user") : src;
        cJSON *id   = user ? cJSON_GetObjectItem(user, "id") : NULL;
        if (id && id->valuestring)
            strncpy(user_id, id->valuestring, sizeof(user_id)-1);
    }

    /* Input text from slash command options */
    char input_text[512] = "";
    cJSON *data    = cJSON_GetObjectItem(root, "data");
    cJSON *options = data ? cJSON_GetObjectItem(data, "options") : NULL;
    if (options && cJSON_IsArray(options)) {
        cJSON *first = cJSON_GetArrayItem(options, 0);
        cJSON *val   = first ? cJSON_GetObjectItem(first, "value") : NULL;
        if (val && val->valuestring)
            strncpy(input_text, val->valuestring, sizeof(input_text)-1);
    }

    /* Respond immediately: DEFERRED_CHANNEL_MESSAGE_WITH_SOURCE */
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"type\":5}");

    ESP_LOGI(TAG, "Deferred: user=%s text=%.60s", user_id, input_text);

    /* Push to agent inbound bus */
    mimi_msg_t msg = {0};
    strncpy(msg.channel, ATOM_CHAN_DISCORD, sizeof(msg.channel)-1);
    strncpy(msg.chat_id, user_id,           sizeof(msg.chat_id)-1);
    strncpy(msg.meta,    itoken,            sizeof(msg.meta)-1);
    msg.content = strdup(input_text);
    if (msg.content) {
        if (message_bus_push_inbound(&msg) != ESP_OK) {
            ESP_LOGW(TAG, "Inbound queue full, dropping Discord message");
            free(msg.content);
        }
    }

    cJSON_Delete(root);
    return ESP_OK;
}

/* ── HTTP server lifecycle ───────────────────────────────────────────── */

static const httpd_uri_t s_interactions_uri = {
    .uri     = ATOM_DISCORD_INTERACTION_PATH,
    .method  = HTTP_POST,
    .handler = interactions_handler,
};

esp_err_t discord_server_init(void)
{
    nvs_handle_t nvs;
    if (nvs_open(ATOM_NVS_DISCORD, NVS_READONLY, &nvs) == ESP_OK) {
        size_t len;
        len = sizeof(s_app_id);
        nvs_get_str(nvs, ATOM_NVS_KEY_DISCORD_APP_ID,  s_app_id,  &len);
        len = sizeof(s_pub_key);
        nvs_get_str(nvs, ATOM_NVS_KEY_DISCORD_PUB_KEY, s_pub_key, &len);
        nvs_close(nvs);
    }
    ESP_LOGI(TAG, "Discord init: app_id=%s pub_key=%.16s...", s_app_id, s_pub_key);
    return ESP_OK;
}

esp_err_t discord_server_start(void)
{
    httpd_config_t cfg   = HTTPD_DEFAULT_CONFIG();
    cfg.server_port      = ATOM_DISCORD_HTTP_PORT;
    cfg.stack_size       = 8192;
    cfg.max_uri_handlers = 4;

    esp_err_t ret = httpd_start(&s_server, &cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(ret));
        return ret;
    }
    httpd_register_uri_handler(s_server, &s_interactions_uri);
    ESP_LOGI(TAG, "Discord HTTP server on port %d", ATOM_DISCORD_HTTP_PORT);
    return ESP_OK;
}

esp_err_t discord_server_stop(void)
{
    if (!s_server) return ESP_OK;
    esp_err_t ret = httpd_stop(s_server);
    s_server = NULL;
    return ret;
}

/* ── Follow-up: PATCH /webhooks/{app_id}/{token}/messages/@original ──── */

esp_err_t discord_follow_up(const char *interaction_token, const char *text)
{
    if (!interaction_token || !text) return ESP_ERR_INVALID_ARG;

    char url[256];
    snprintf(url, sizeof(url),
             ATOM_DISCORD_API_BASE "/webhooks/%s/%s/messages/@original",
             s_app_id, interaction_token);

    cJSON *body = cJSON_CreateObject();
    char truncated[ATOM_DISCORD_MAX_RESP_LEN + 1];
    strncpy(truncated, text, ATOM_DISCORD_MAX_RESP_LEN);
    truncated[ATOM_DISCORD_MAX_RESP_LEN] = '\0';
    cJSON_AddStringToObject(body, "content", truncated);
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!body_str) return ESP_ERR_NO_MEM;

    esp_http_client_config_t cfg = {
        .url               = url,
        .method            = HTTP_METHOD_PATCH,
        .timeout_ms        = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { free(body_str); return ESP_FAIL; }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body_str, strlen(body_str));

    esp_err_t ret = esp_http_client_perform(client);
    if (ret == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status < 200 || status >= 300) {
            ESP_LOGW(TAG, "Discord follow-up HTTP %d", status);
            ret = ESP_FAIL;
        } else {
            ESP_LOGI(TAG, "Discord follow-up OK");
        }
    } else {
        ESP_LOGE(TAG, "Discord follow-up failed: %s", esp_err_to_name(ret));
    }

    esp_http_client_cleanup(client);
    free(body_str);
    return ret;
}
