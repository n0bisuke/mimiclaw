#include "cf_history.h"
#include "atom_config.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "cf_history";

static char s_worker_url[128]  = ATOM_SECRET_CF_WORKER_URL;
static char s_auth_token[64]   = ATOM_SECRET_CF_AUTH_TOKEN;

/* ── HTTP response accumulator ──────────────────────────────────────── */

typedef struct {
    char  *buf;
    size_t size;
    size_t pos;
} http_buf_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_buf_t *rb = (http_buf_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && rb) {
        size_t remaining = rb->size - rb->pos - 1;
        size_t copy = (size_t)evt->data_len < remaining ? (size_t)evt->data_len : remaining;
        memcpy(rb->buf + rb->pos, evt->data, copy);
        rb->pos += copy;
        rb->buf[rb->pos] = '\0';
    }
    return ESP_OK;
}

/* ── Summary fetch ───────────────────────────────────────────────────── */

esp_err_t cf_get_summary(const char *user_id, char *buf, size_t buf_size,
                         cf_summary_result_t *result)
{
    buf[0] = '\0';
    if (result) { result->needs_summarize = false; result->history_count = 0; }

    if (s_worker_url[0] == '\0') {
        ESP_LOGD(TAG, "No CF Worker URL configured, skipping summary fetch");
        return ESP_OK;
    }

    char url[192];
    snprintf(url, sizeof(url), "%s%s?user_id=%s",
             s_worker_url, ATOM_CF_SUMMARY_PATH, user_id);

    /* Use a local JSON buffer for the raw response */
    char *raw = calloc(1, buf_size);
    if (!raw) return ESP_ERR_NO_MEM;

    http_buf_t rb = { .buf = raw, .size = buf_size, .pos = 0 };

    esp_http_client_config_t cfg = {
        .url               = url,
        .method            = HTTP_METHOD_GET,
        .timeout_ms        = ATOM_CF_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler     = http_event_handler,
        .user_data         = &rb,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { free(raw); return ESP_FAIL; }

    if (s_auth_token[0]) {
        char bearer[80];
        snprintf(bearer, sizeof(bearer), "Bearer %s", s_auth_token);
        esp_http_client_set_header(client, "Authorization", bearer);
    }

    esp_err_t ret = esp_http_client_perform(client);
    int status    = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (ret != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "CF summary fetch: err=%s HTTP=%d", esp_err_to_name(ret), status);
        free(raw);
        return ESP_FAIL;
    }

    /* Parse { summary, needs_summarize, history_count } */
    cJSON *root = cJSON_Parse(raw);
    free(raw);

    if (root) {
        cJSON *js = cJSON_GetObjectItem(root, "summary");
        if (js && js->valuestring) {
            strncpy(buf, js->valuestring, buf_size - 1);
            buf[buf_size - 1] = '\0';
        }
        if (result) {
            cJSON *jns = cJSON_GetObjectItem(root, "needs_summarize");
            cJSON *jhc = cJSON_GetObjectItem(root, "history_count");
            if (jns) result->needs_summarize = cJSON_IsTrue(jns);
            if (jhc) result->history_count   = jhc->valueint;
        }
        cJSON_Delete(root);
    }

    ESP_LOGI(TAG, "CF summary: %d bytes, needs_summarize=%d",
             (int)strlen(buf), result ? result->needs_summarize : 0);
    return ESP_OK;
}

/* ── Async save task ─────────────────────────────────────────────────── */

typedef struct {
    char user_id[32];
    char role[16];
    char *content;      /* heap-allocated, task frees it */
    uint32_t timestamp;
} save_task_args_t;

static void save_task(void *arg)
{
    save_task_args_t *a = (save_task_args_t *)arg;

    if (s_worker_url[0] == '\0') {
        ESP_LOGD(TAG, "No CF Worker URL, skipping save");
        goto done;
    }

    /* Build URL */
    char url[160];
    snprintf(url, sizeof(url), "%s%s", s_worker_url, ATOM_CF_SAVE_PATH);

    /* Build JSON body */
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "user_id",   a->user_id);
    cJSON_AddStringToObject(body, "role",      a->role);
    cJSON_AddStringToObject(body, "content",   a->content);
    cJSON_AddNumberToObject(body, "timestamp", (double)a->timestamp);
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    if (!body_str) goto done;

    esp_http_client_config_t cfg = {
        .url               = url,
        .method            = HTTP_METHOD_POST,
        .timeout_ms        = ATOM_CF_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { free(body_str); goto done; }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (s_auth_token[0]) {
        char bearer[80];
        snprintf(bearer, sizeof(bearer), "Bearer %s", s_auth_token);
        esp_http_client_set_header(client, "Authorization", bearer);
    }
    esp_http_client_set_post_field(client, body_str, strlen(body_str));

    esp_err_t ret = esp_http_client_perform(client);
    if (ret == ESP_OK) {
        int st = esp_http_client_get_status_code(client);
        ESP_LOGD(TAG, "CF save HTTP %d", st);
    } else {
        ESP_LOGW(TAG, "CF save failed: %s", esp_err_to_name(ret));
    }

    esp_http_client_cleanup(client);
    free(body_str);

done:
    free(a->content);
    free(a);
    vTaskDelete(NULL);
}

void cf_save_async(const char *user_id, const char *role,
                   const char *content, uint32_t timestamp)
{
    save_task_args_t *a = calloc(1, sizeof(save_task_args_t));
    if (!a) return;

    strncpy(a->user_id, user_id, sizeof(a->user_id) - 1);
    strncpy(a->role, role, sizeof(a->role) - 1);
    a->content   = strdup(content ? content : "");
    a->timestamp = timestamp ? timestamp : (uint32_t)time(NULL);

    if (!a->content) { free(a); return; }

    BaseType_t ret = xTaskCreate(save_task, "cf_save", 4096, a, 3, NULL);
    if (ret != pdPASS) {
        ESP_LOGW(TAG, "Failed to create cf_save task");
        free(a->content);
        free(a);
    }
}

/* ── Update summary (ESP32-generated) ───────────────────────────────── */

esp_err_t cf_update_summary(const char *user_id, const char *summary)
{
    if (!user_id || !summary || s_worker_url[0] == '\0') return ESP_OK;

    char url[192];
    snprintf(url, sizeof(url), "%s/update_summary", s_worker_url);

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "user_id", user_id);
    cJSON_AddStringToObject(body, "summary", summary);
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!body_str) return ESP_ERR_NO_MEM;

    esp_http_client_config_t cfg = {
        .url               = url,
        .method            = HTTP_METHOD_POST,
        .timeout_ms        = ATOM_CF_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { free(body_str); return ESP_FAIL; }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (s_auth_token[0]) {
        char bearer[80];
        snprintf(bearer, sizeof(bearer), "Bearer %s", s_auth_token);
        esp_http_client_set_header(client, "Authorization", bearer);
    }
    esp_http_client_set_post_field(client, body_str, strlen(body_str));

    esp_err_t ret = esp_http_client_perform(client);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "CF summary updated (%d bytes)", (int)strlen(summary));
    } else {
        ESP_LOGW(TAG, "CF update_summary failed: %s", esp_err_to_name(ret));
    }
    esp_http_client_cleanup(client);
    free(body_str);
    return ret;
}

/* ── Configured check ────────────────────────────────────────────────── */

bool cf_history_is_configured(void)
{
    return s_worker_url[0] != '\0';
}

/* ── Init ────────────────────────────────────────────────────────────── */

esp_err_t cf_history_init(void)
{
    /* Override from NVS if available */
    nvs_handle_t nvs;
    if (nvs_open(ATOM_NVS_CF, NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = sizeof(s_worker_url);
        nvs_get_str(nvs, ATOM_NVS_KEY_CF_URL, s_worker_url, &len);
        len = sizeof(s_auth_token);
        nvs_get_str(nvs, ATOM_NVS_KEY_CF_TOKEN, s_auth_token, &len);
        nvs_close(nvs);
    }

    ESP_LOGI(TAG, "CF history init. Worker URL: %s",
             s_worker_url[0] ? s_worker_url : "(not configured)");
    return ESP_OK;
}
