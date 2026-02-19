#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"
#include "cJSON.h"

#include "atom_config.h"
#include "bus/message_bus.h"
#include "wifi/wifi_manager.h"
#include "llm/llm_proxy.h"
#include "memory/memory_store.h"
#include "memory/atom_session.h"
#include "agent/atom_context.h"
#include "cloudflare/cf_history.h"
#include "discord/discord_server.h"
#include "cli/serial_cli.h"
#include "proxy/http_proxy.h"
#include "tools/tool_registry.h"
#include "rgb/rgb.h"

static const char *TAG = "atomclaw";

/* ── NVS init ────────────────────────────────────────────────────────── */

static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

/* ── SPIFFS init ─────────────────────────────────────────────────────── */

static esp_err_t init_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path             = ATOM_SPIFFS_BASE,
        .partition_label       = NULL,
        .max_files             = 8,
        .format_if_mount_failed = true,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        return ret;
    }
    size_t total = 0, used = 0;
    esp_spiffs_info(NULL, &total, &used);
    ESP_LOGI(TAG, "SPIFFS: total=%dKB used=%dKB", (int)(total/1024), (int)(used/1024));
    return ESP_OK;
}

/* ── AtomClaw Agent Loop ─────────────────────────────────────────────── */

static void atom_agent_task(void *arg)
{
    ESP_LOGI(TAG, "AtomClaw agent started on core %d", xPortGetCoreID());

    /* Allocate PSRAM buffers */
    char *system_prompt = heap_caps_calloc(1, ATOM_CONTEXT_BUF_SIZE,    MALLOC_CAP_SPIRAM);
    char *history_json  = heap_caps_calloc(1, ATOM_LLM_STREAM_BUF_SIZE, MALLOC_CAP_SPIRAM);
    char *tool_output   = heap_caps_calloc(1, 8 * 1024,                  MALLOC_CAP_SPIRAM);
    char *cf_summary    = heap_caps_calloc(1, ATOM_CF_SUMMARY_MAX_LEN,   MALLOC_CAP_SPIRAM);

    if (!system_prompt || !history_json || !tool_output || !cf_summary) {
        ESP_LOGE(TAG, "PSRAM allocation failed");
        vTaskDelete(NULL);
        return;
    }

    const char *tools_json = tool_registry_get_tools_json();

    while (1) {
        mimi_msg_t msg;
        esp_err_t err = message_bus_pop_inbound(&msg, portMAX_DELAY);
        if (err != ESP_OK) continue;

        ESP_LOGI(TAG, "AtomClaw processing from %s (user=%s)", msg.channel, msg.chat_id);

        /* 1. Check CF availability for this request */
        bool cf_ok = cf_history_is_configured()
                     && strcmp(msg.channel, ATOM_CHAN_DISCORD) == 0;

        /* 2. Fetch Cloudflare summary (CF mode only) */
        cf_summary[0] = '\0';
        cf_summary_result_t cf_res = {0};
        if (cf_ok) {
            cf_get_summary(msg.chat_id, cf_summary, ATOM_CF_SUMMARY_MAX_LEN, &cf_res);
        }

        /* 3. Build system prompt */
        atom_context_build_system(system_prompt, ATOM_CONTEXT_BUF_SIZE, cf_summary);

        /* 4. Load local ring buffer history.
         *    CF mode: all stored messages (up to ATOM_SESSION_MAX_MSGS).
         *    Local-only mode: last 2 exchanges (4 messages) to keep context short. */
        int max_msgs = cf_ok ? 0 : 4;
        atom_session_get_history_json(msg.chat_id, history_json,
                                      ATOM_LLM_STREAM_BUF_SIZE, max_msgs);

        /* 5. Build messages array */
        cJSON *messages = cJSON_Parse(history_json);
        if (!messages) messages = cJSON_CreateArray();

        cJSON *user_msg_j = cJSON_CreateObject();
        cJSON_AddStringToObject(user_msg_j, "role", "user");
        cJSON_AddStringToObject(user_msg_j, "content", msg.content);
        cJSON_AddItemToArray(messages, user_msg_j);

        /* 6. ReAct loop (max ATOM_AGENT_MAX_TOOL_ITER iterations) */
        char *final_text = NULL;
        int iteration = 0;

        while (iteration < ATOM_AGENT_MAX_TOOL_ITER) {
            llm_response_t resp;
            err = llm_chat_tools(system_prompt, messages, tools_json, &resp);

            if (err != ESP_OK) {
                ESP_LOGE(TAG, "LLM error: %s", esp_err_to_name(err));
                break;
            }

            if (!resp.tool_use) {
                if (resp.text && resp.text_len > 0) {
                    final_text = strdup(resp.text);
                }
                llm_response_free(&resp);
                break;
            }

            /* Execute tools */
            cJSON *asst_msg = cJSON_CreateObject();
            cJSON_AddStringToObject(asst_msg, "role", "assistant");
            cJSON *asst_content = cJSON_CreateArray();
            if (resp.text && resp.text_len > 0) {
                cJSON *tb = cJSON_CreateObject();
                cJSON_AddStringToObject(tb, "type", "text");
                cJSON_AddStringToObject(tb, "text", resp.text);
                cJSON_AddItemToArray(asst_content, tb);
            }
            for (int i = 0; i < resp.call_count; i++) {
                const llm_tool_call_t *call = &resp.calls[i];
                cJSON *ub = cJSON_CreateObject();
                cJSON_AddStringToObject(ub, "type", "tool_use");
                cJSON_AddStringToObject(ub, "id",   call->id);
                cJSON_AddStringToObject(ub, "name", call->name);
                cJSON *inp = cJSON_Parse(call->input);
                cJSON_AddItemToObject(ub, "input", inp ? inp : cJSON_CreateObject());
                cJSON_AddItemToArray(asst_content, ub);
            }
            cJSON_AddItemToObject(asst_msg, "content", asst_content);
            cJSON_AddItemToArray(messages, asst_msg);

            cJSON *results_content = cJSON_CreateArray();
            for (int i = 0; i < resp.call_count; i++) {
                const llm_tool_call_t *call = &resp.calls[i];
                tool_output[0] = '\0';
                tool_registry_execute(call->name, call->input, tool_output, 8*1024);
                cJSON *rb = cJSON_CreateObject();
                cJSON_AddStringToObject(rb, "type",        "tool_result");
                cJSON_AddStringToObject(rb, "tool_use_id", call->id);
                cJSON_AddStringToObject(rb, "content",     tool_output);
                cJSON_AddItemToArray(results_content, rb);
            }
            cJSON *result_msg = cJSON_CreateObject();
            cJSON_AddStringToObject(result_msg, "role", "user");
            cJSON_AddItemToObject(result_msg, "content", results_content);
            cJSON_AddItemToArray(messages, result_msg);

            llm_response_free(&resp);
            iteration++;
        }

        cJSON_Delete(messages);

        /* 7. Prepare response text */
        const char *response_text = (final_text && final_text[0])
            ? final_text
            : "Sorry, I couldn't process your request.";

        /* 8. Save to local ring buffer */
        atom_session_append(msg.chat_id, "user",      msg.content);
        atom_session_append(msg.chat_id, "assistant", response_text);

        /* 9. Push to outbound bus */
        mimi_msg_t out = {0};
        strncpy(out.channel, msg.channel, sizeof(out.channel) - 1);
        strncpy(out.chat_id, msg.chat_id, sizeof(out.chat_id) - 1);
        strncpy(out.meta,    msg.meta,    sizeof(out.meta) - 1);
        out.content = strdup(response_text);
        if (out.content) {
            if (message_bus_push_outbound(&out) != ESP_OK) {
                ESP_LOGW(TAG, "Outbound queue full, dropping response");
                free(out.content);
            }
        }

        /* 10. Async CF save (both turns) — CF mode only */
        if (cf_ok) {
            uint32_t now = (uint32_t)time(NULL);
            cf_save_async(msg.chat_id, "user",      msg.content,   now);
            cf_save_async(msg.chat_id, "assistant", response_text, now + 1);
        }

        /* 11. ESP32側で要約生成 (CF mode + needs_summarize フラグが立っている場合)
         *     Cloudflare Worker は AI を呼ばない。要約生成は ESP32 の LLM が担う。 */
        if (cf_ok && cf_res.needs_summarize) {
            ESP_LOGI(TAG, "Generating summary for user %s (history_count=%d)",
                     msg.chat_id, cf_res.history_count);

            /* 直近履歴から要約プロンプトを組み立てる */
            char *sum_history = heap_caps_calloc(1, 4096, MALLOC_CAP_SPIRAM);
            if (sum_history) {
                atom_session_get_history_json(msg.chat_id, sum_history, 4096, 0);

                const char *sum_system =
                    "You are a concise summarizer. Summarize the conversation "
                    "in 3-5 sentences focusing on key facts and user preferences. "
                    "Write in third person about 'the user'. "
                    "Reply with only the summary text, no extra commentary.";

                cJSON *sum_msgs = cJSON_Parse(sum_history);
                if (!sum_msgs) sum_msgs = cJSON_CreateArray();

                llm_response_t sum_resp = {0};
                if (llm_chat_tools(sum_system, sum_msgs, NULL, &sum_resp) == ESP_OK
                    && sum_resp.text && sum_resp.text_len > 0) {
                    /* 要約を Cloudflare KV に保存 */
                    cf_update_summary(msg.chat_id, sum_resp.text);
                }
                llm_response_free(&sum_resp);
                cJSON_Delete(sum_msgs);
                heap_caps_free(sum_history);
            }
        }

        free(final_text);
        free(msg.content);

        ESP_LOGI(TAG, "Free PSRAM: %d bytes",
                 (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
}

/* ── Outbound dispatch: routes response back to Discord ──────────────── */

static void outbound_dispatch_task(void *arg)
{
    ESP_LOGI(TAG, "Outbound dispatch started");

    while (1) {
        mimi_msg_t msg;
        if (message_bus_pop_outbound(&msg, portMAX_DELAY) != ESP_OK) continue;

        ESP_LOGI(TAG, "Dispatch → %s:%s", msg.channel, msg.chat_id);

        if (strcmp(msg.channel, ATOM_CHAN_DISCORD) == 0) {
            /* meta holds the Discord interaction token */
            discord_follow_up(msg.meta, msg.content);
        } else {
            /* CLI or other future channels */
            ESP_LOGI(TAG, "[%s] %s", msg.channel, msg.content);
        }

        free(msg.content);
    }
}

/* ── app_main ────────────────────────────────────────────────────────── */

void app_main(void)
{
    esp_log_level_set("esp-x509-crt-bundle", ESP_LOG_WARN);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  AtomClaw - ESP32-S3 8MB AI Agent");
    ESP_LOGI(TAG, "  Discord + Cloudflare Hybrid");
    ESP_LOGI(TAG, "========================================");

    ESP_LOGI(TAG, "Internal heap: %d bytes",
             (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "PSRAM:         %d bytes",
             (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    /* RGB: red during boot */
    ESP_ERROR_CHECK(rgb_init());
    rgb_set(255, 0, 0);

    /* Core init */
    ESP_ERROR_CHECK(init_nvs());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(init_spiffs());

    /* Subsystems */
    ESP_ERROR_CHECK(message_bus_init());
    ESP_ERROR_CHECK(memory_store_init());
    ESP_ERROR_CHECK(atom_session_init());
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(http_proxy_init());
    ESP_ERROR_CHECK(llm_proxy_init());
    ESP_ERROR_CHECK(tool_registry_init());
    ESP_ERROR_CHECK(cf_history_init());
    ESP_LOGI(TAG, "CF history: %s",
             cf_history_is_configured()
             ? "enabled (cloud history + summary)"
             : "disabled (local-only, last 2 exchanges)");
    ESP_ERROR_CHECK(discord_server_init());
    ESP_ERROR_CHECK(serial_cli_init());

    /* WiFi */
    esp_err_t wifi_err = wifi_manager_start();
    if (wifi_err == ESP_OK) {
        ESP_LOGI(TAG, "Waiting for WiFi...");
        if (wifi_manager_wait_connected(30000) == ESP_OK) {
            ESP_LOGI(TAG, "WiFi connected: %s", wifi_manager_get_ip());

            /* RGB: green when ready */
            rgb_set(0, 255, 0);

            /* Start Discord HTTP server */
            ESP_ERROR_CHECK(discord_server_start());

            /* Start agent loop */
            BaseType_t agent_ok = xTaskCreatePinnedToCore(atom_agent_task, "atom_agent",
                                                           ATOM_AGENT_STACK, NULL,
                                                           ATOM_AGENT_PRIO, NULL, ATOM_AGENT_CORE);

            /* Start outbound dispatcher */
            BaseType_t out_ok = xTaskCreatePinnedToCore(outbound_dispatch_task, "outbound",
                                                         ATOM_OUTBOUND_STACK, NULL,
                                                         ATOM_OUTBOUND_PRIO, NULL, ATOM_OUTBOUND_CORE);
            if (agent_ok != pdPASS || out_ok != pdPASS) {
                ESP_LOGE(TAG, "Failed to create tasks: agent=%d outbound=%d",
                         (int)agent_ok, (int)out_ok);
                rgb_set(255, 0, 0);
                return;
            }

            ESP_LOGI(TAG, "AtomClaw ready! Discord interaction endpoint: "
                          "http://%s%s",
                     wifi_manager_get_ip(), ATOM_DISCORD_INTERACTION_PATH);
        } else {
            rgb_set(255, 128, 0);  /* orange: WiFi timeout */
            ESP_LOGW(TAG, "WiFi timeout. Check credentials in atom_secrets.h");
        }
    } else {
        rgb_set(255, 0, 0);
        ESP_LOGW(TAG, "No WiFi credentials. Set ATOM_SECRET_WIFI_SSID in atom_secrets.h");
    }

    ESP_LOGI(TAG, "CLI ready. Type 'help' for commands.");
}
