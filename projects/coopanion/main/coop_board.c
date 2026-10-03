// SPDX-License-Identifier: MIT
#include "coop_board.h"
#include "coop_link.h"
#include "coop_audio.h"
#include "coop_assets.h"
#include "coop_script.h"
#include "coop_receipt.h"
#include "bsp/esp_mosaico.h"
#include "esp_gsp_esp_lcd.h"
#include "esp_iris.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdatomic.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const uint8_t coo_start[] asm("_binary_coo_atlas_bin_start");
extern const uint8_t coo_end[] asm("_binary_coo_atlas_bin_end");
#define PAIRING_LIMIT 6144
#define PAIRING_TIMEOUT_MS 30000
#define RECEIPT_FENCE_BUDGET_MS 1500
#define RECEIPT_SEND_ATTEMPTS 5
/* Emotion changes are frequent; transfers persist immediately through their own
 * state events, so an expression only needs to be durable within a few seconds. */
#define EMOTION_PERSIST_DEBOUNCE_MS 5000
/* Keep Wi-Fi awake after local input so a summon or voice press that usually
 * follows a touch is not delayed by modem sleep, and briefly after connecting. */
#define LOW_LATENCY_AFTER_INPUT_MS 30000
#define LOW_LATENCY_AFTER_CONNECT_MS 60000
typedef struct {
    uint32_t version, epoch;
    uint8_t resident, visible, transit, muted;
    char id[64], emotion[24];
} journal_t;
typedef struct {
    float a[3], g[3];
    uint64_t at;
} imu_sample_t;
typedef struct {
    uint8_t *bytes;
    size_t size;
    char id[64], hash[65];
} asset_notice_t;
typedef struct {
    char type[32], text[384];
    uint32_t epoch, utterance;
    uint64_t at;
    journal_t journal;
} output_t;
struct coop_board_t {
    esp_gsp_handle_t gsp;
    coop_ui_handle_t ui;
    coop_link_handle_t link;
    coop_audio_handle_t audio;
    QueueHandle_t messages, imu, output, buttons, asset_notices;
    coop_assets_handle_t assets;
    coop_script_handle_t script;
    uint8_t *cached_atlas;
    button_handle_t button;
    atomic_bool active, connected;
    atomic_uint motor_until;
    atomic_bool journal_ok;
    bool motor, led, imu_ok, battery_ok, magnet_ok, nand_ok, capabilities_sent;
    char walk_id[64], last_emotion[24];
    bool emotion_dirty;
    uint64_t emotion_persisted_at, connected_at;
    bool was_connected;
    journal_t restored, durable;
    unsigned battery_tick;
    float magnetic_baseline[BSP_MAGNETOMETER_NUM];
    uint64_t last_magnetic;
    /* Owned only by the serial Iris RPC worker. Incomplete pairing is never persisted. */
    uint8_t *pairing;
    size_t pairing_size, pairing_received;
    uint32_t pairing_session;
    uint64_t pairing_deadline;
    bool pairing_restarting;
    /* Standing edge the UI wants (written by the render task) and the one the
     * panel shows (owned by the sensors task, which rotates it). */
    atomic_int desired_edge;
    int applied_edge;
    uint64_t rotate_retry_at;
};
static uint64_t now(void *ctx)
{
    (void)ctx;
    return esp_timer_get_time() / 1000;
}
static const char *str(cJSON *j, const char *key)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}
static double number(cJSON *j, const char *key, double fallback)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    return cJSON_IsNumber(v) ? v->valuedouble : fallback;
}
static void incoming(void *ctx, cJSON *m)
{
    coop_board_handle_t h = ctx;
    if (!strncmp(str(m, "t"), "asset_", 6)) {
        coop_assets_message(h->assets, m);
        return;
    }
    if (xQueueSend(h->messages, &m, 0) != pdTRUE)
        cJSON_Delete(m);
}
static void asset_complete(void *ctx, uint8_t *bytes, size_t size, const char *id, const char *hash)
{
    coop_board_handle_t h = ctx;
    asset_notice_t *n = calloc(1, sizeof(*n));
    if (!n) {
        free(bytes);
        return;
    }
    n->bytes = bytes;
    n->size = size;
    snprintf(n->id, sizeof(n->id), "%s", id);
    snprintf(n->hash, sizeof(n->hash), "%s", hash);
    if (xQueueSend(h->asset_notices, &n, 0) != pdTRUE) {
        free(bytes);
        free(n);
    }
}
static void connection(void *ctx, bool online)
{
    coop_board_handle_t h = ctx;
    atomic_store(&h->connected, online);
    if (!online)
        coop_audio_release(h->audio, true);
}
static cJSON *message(const char *type)
{
    cJSON *m = cJSON_CreateObject();
    if (m)
        cJSON_AddStringToObject(m, "t", type);
    return m;
}
static void save_snapshot(journal_t *j, const coop_snapshot_t *s)
{
    *j = (journal_t){.version = 1,
                     .epoch = s->epoch,
                     .resident = s->resident,
                     .visible = s->visible,
                     .transit = s->transferring,
                     .muted = s->muted};
    snprintf(j->id, sizeof(j->id), "%s", s->transfer_id);
    snprintf(j->emotion, sizeof(j->emotion), "%s", s->expression);
}
static void output(void *ctx, const coop_event_t *e)
{
    coop_board_handle_t h = ctx;
    if (!strcmp(e->type, "audio_stop")) {
        coop_audio_stop(h->audio);
        return;
    }
    if (!strcmp(e->type, "voice_start")) {
        coop_audio_record(h->audio, e->utterance);
        return;
    }
    if (!strcmp(e->type, "voice_end") || !strcmp(e->type, "voice_cancel")) {
        coop_audio_release(h->audio, !strcmp(e->type, "voice_cancel"));
        return;
    }
    if (!strcmp(e->type, "say")) {
        coop_audio_say(h->audio, e->text);
        return;
    }
    if (!strcmp(e->type, "vibrate")) {
        atomic_store(&h->motor_until, (unsigned)now(NULL) + 70);
        return;
    }
    output_t *item = calloc(1, sizeof(*item));
    if (!item) {
        h->journal_ok = false;
        return;
    }
    snprintf(item->type, sizeof(item->type), "%s", e->type);
    snprintf(item->text, sizeof(item->text), "%s", e->text ? e->text : "");
    if (!strcmp(e->type, "arrived"))
        snprintf(item->text, sizeof(item->text), "%s", h->walk_id);
    item->epoch = e->epoch;
    item->utterance = e->utterance;
    item->at = e->at;
    if (!strcmp(e->type, "persist") && h->ui)
        save_snapshot(&item->journal, coop_state_get(coop_ui_state(h->ui)));
    if (xQueueSend(h->output, &item, 0) != pdTRUE) {
        h->journal_ok = false;
        free(item);
    }
}
static coop_fence_result_t receipt_flush(void *ctx, uint32_t timeout_ms)
{
    coop_board_handle_t h = ctx;
    uint32_t before = 0, after = 0;
    gsp_err_t last = 0;
    esp_gsp_render_error_stats(h->gsp, &before, &last);
    esp_gsp_err_t result = esp_gsp_flush(h->gsp, timeout_ms);
    if (result == ESP_GSP_OK) {
        esp_gsp_render_error_stats(h->gsp, &after, &last);
        if (after != before) {
            ESP_LOGE("coo_receipt", "Render failed during fence: %d", (int)last);
            return COOP_FENCE_FAILED;
        }
        return COOP_FENCE_OK;
    }
    if (result == ESP_ERR_INVALID_STATE)
        return COOP_FENCE_BUSY;
    if (result == ESP_ERR_TIMEOUT)
        return COOP_FENCE_TIMEOUT;
    ESP_LOGE("coo_receipt", "Fence rejected: 0x%x", (unsigned)result);
    return COOP_FENCE_FAILED;
}
static void receipt_wait(void *ctx, uint32_t delay_ms)
{
    (void)ctx;
    TickType_t ticks = pdMS_TO_TICKS(delay_ms);
    vTaskDelay(ticks ? ticks : 1);
}
static void output_worker(void *ctx)
{
    coop_board_handle_t h = ctx;
    output_t *e;
    for (;;) {
        if (xQueueReceive(h->output, &e, portMAX_DELAY) != pdTRUE)
            continue;
        if (!strcmp(e->type, "persist")) {
            nvs_handle_t nvs;
            esp_err_t err = nvs_open("coo_state", NVS_READWRITE, &nvs);
            if (err == ESP_OK) {
                err = nvs_set_blob(nvs, "journal", &e->journal, sizeof(e->journal));
                if (err == ESP_OK)
                    err = nvs_commit(nvs);
                nvs_close(nvs);
            }
            h->journal_ok = err == ESP_OK;
            if (err == ESP_OK)
                h->durable = e->journal;
            free(e);
            continue;
        }
        if (!h->journal_ok &&
            (!strncmp(e->type, "transfer_", 9) || !strncmp(e->type, "report_", 7))) {
            ESP_LOGE("coo_receipt", "%s epoch=%lu withheld: journal unavailable",
                     e->type, (unsigned long)e->epoch);
            free(e);
            continue;
        }
        if (!strcmp(e->type, "transfer_hidden") || !strcmp(e->type, "transfer_arrived") ||
            !strcmp(e->type, "glow_started") || !strcmp(e->type, "body_entered")) {
            const coop_receipt_config_t cfg = {
                .flush = receipt_flush, .now = now, .wait = receipt_wait, .ctx = h};
            uint64_t started = now(NULL);
            unsigned attempts = 0;
            coop_fence_result_t result = coop_receipt_wait(
                &cfg, RECEIPT_FENCE_BUDGET_MS, &e->at, &attempts);
            if (result != COOP_FENCE_OK) {
                ESP_LOGE("coo_receipt", "%s epoch=%lu withheld: fence=%d attempts=%u",
                         e->type, (unsigned long)e->epoch, result, attempts);
                free(e);
                continue;
            }
            ESP_LOGI("coo_receipt", "%s epoch=%lu rendered_at=%llu wait=%llu attempts=%u",
                     e->type, (unsigned long)e->epoch, (unsigned long long)e->at,
                     (unsigned long long)(e->at - started), attempts);
        }
        if ((!strcmp(e->type, "transfer_hidden") || !strcmp(e->type, "transfer_arrived")) &&
            (h->durable.epoch != e->epoch || strcmp(h->durable.id, e->text) ||
             (!strcmp(e->type, "transfer_hidden") && h->durable.visible) ||
             (!strcmp(e->type, "transfer_arrived") &&
              (!h->durable.visible || !h->durable.resident)))) {
            ESP_LOGE("coo_receipt", "%s epoch=%lu withheld: durable state mismatch",
                     e->type, (unsigned long)e->epoch);
            free(e);
            continue;
        }
        const char *type = !strncmp(e->type, "report_", 7) ? "transfer_report" : e->type;
        cJSON *m = message(type);
        cJSON_AddNumberToObject(m, "epoch", e->epoch);
        cJSON_AddNumberToObject(m, "at", e->at);
        if (!strncmp(type, "transfer_", 9) || !strcmp(type, "glow_started") ||
            !strcmp(type, "body_entered")) {
            cJSON_AddStringToObject(m, "id", e->text);
            if (!strcmp(type, "transfer_report"))
                cJSON_AddBoolToObject(m, "hidden", !strcmp(e->type, "report_hidden"));
        } else if (!strcmp(type, "touch"))
            cJSON_AddStringToObject(m, "kind",
                                    !strcmp(e->text, "thrown")    ? "throw"
                                    : !strcmp(e->text, "petting") ? "pet"
                                                                  : e->text);
        else if (!strcmp(type, "answer") || !strcmp(type, "confirmed")) {
            cJSON_AddStringToObject(m, !strcmp(type, "answer") ? "askId" : "id", e->text);
            cJSON_AddNumberToObject(m, "index", e->epoch);
        } else if (!strcmp(type, "arrived")) {
            cJSON_AddStringToObject(m, "walkId", e->text);
        }
        bool critical = !strncmp(type, "transfer_", 9) || !strcmp(type, "glow_started") ||
                        !strcmp(type, "body_entered");
        if (critical) {
            /* send consumes its JSON even if the short queue/sequence lock is
             * busy. Retry admission without losing the proven render time. */
            bool sent = false;
            for (unsigned i = 0; i < RECEIPT_SEND_ATTEMPTS && !sent; i++) {
                sent = coop_link_send(h->link, cJSON_Duplicate(m, true));
                if (!sent && i + 1 < RECEIPT_SEND_ATTEMPTS)
                    receipt_wait(h, 10);
            }
            cJSON_Delete(m);
            if (!sent)
                ESP_LOGE("coo_receipt", "%s epoch=%lu not queued; awaiting reconciliation",
                         type, (unsigned long)e->epoch);
        } else
            coop_link_send(h->link, m);
        free(e);
    }
}
static void press(void *button, void *ctx)
{
    (void)button;
    coop_board_handle_t h = ctx;
    bool down = true;
    xQueueSend(h->buttons, &down, 0);
}
static void release(void *button, void *ctx)
{
    (void)button;
    coop_board_handle_t h = ctx;
    bool down = false;
    xQueueSend(h->buttons, &down, 0);
}
/* Edge -> panel rotation. The renderer's edge 1 maps a logical point to
 * (W-1-y, x), which is BSP_DISPLAY_ROTATE_90 (swap XY + mirror X); if a board
 * shows sideways edges mirrored, swap the 90 and 270 entries. */
static const bsp_display_rotation_t EDGE_ROTATION[4] = {
    BSP_DISPLAY_ROTATE_0, BSP_DISPLAY_ROTATE_90, BSP_DISPLAY_ROTATE_180, BSP_DISPLAY_ROTATE_270};
#define ROTATE_PAUSE_MS 300
#define ROTATE_RETRY_MS 1000
/* Turns the whole UI (captions, status, menu, touch) to the standing edge.
 * The panel scan (MADCTL) and touch transform change while GSP is paused;
 * GSP redraws the full frame on resume. Never called from the render task. */
static void rotate_screen(coop_board_handle_t h)
{
    int edge = atomic_load(&h->desired_edge);
    if (!h->gsp || edge == h->applied_edge || edge < 0 || edge > 3 || now(NULL) < h->rotate_retry_at)
        return;
    esp_gsp_esp_lcd_pause_t *pause = NULL;
    if (esp_gsp_esp_lcd_pause(h->gsp, ROTATE_PAUSE_MS, &pause) != ESP_OK) {
        h->rotate_retry_at = now(NULL) + ROTATE_RETRY_MS;
        return;
    }
    esp_err_t err = bsp_display_set_rotation(EDGE_ROTATION[edge]);
    if (err == ESP_OK) {
        coop_ui_set_screen_rotation(h->ui, edge * 90);
        h->applied_edge = edge;
    } else {
        ESP_LOGW("coopanion", "screen rotation to edge %d failed (%s)", edge, esp_err_to_name(err));
        h->rotate_retry_at = now(NULL) + ROTATE_RETRY_MS;
    }
    esp_gsp_handle_t resumed = NULL;
    if (esp_gsp_esp_lcd_resume_paused(pause, &resumed) != ESP_OK)
        ESP_LOGE("coopanion", "GSP did not resume after rotating the screen");
    else if (resumed != h->gsp)
        ESP_LOGW("coopanion", "GSP resumed a different UI handle after rotation");
}
static void sensors(void *ctx)
{
    coop_board_handle_t h = ctx;
    TickType_t tick = xTaskGetTickCount();
    bool motor_on = false, imu_running = false;
    const bsp_imu_config_t config = BSP_IMU_CONFIG_DEFAULT();
    for (;;) {
        bool active = atomic_load(&h->active);
        if (h->imu_ok && active != imu_running) {
            if (active)
                imu_running = bsp_imu_start(&config) == ESP_OK;
            else {
                bsp_imu_stop();
                imu_running = false;
            }
        }
        rotate_screen(h);
        if (imu_running) {
            imu_sample_t s = {.at = now(NULL)};
            if (bsp_imu_get_accel(&s.a[0], &s.a[1], &s.a[2]) == ESP_OK &&
                bsp_imu_get_gyro(&s.g[0], &s.g[1], &s.g[2]) == ESP_OK)
                xQueueSend(h->imu, &s, 0);
        }
        bool want = (int32_t)(atomic_load(&h->motor_until) - (unsigned)now(NULL)) > 0;
        if (h->motor && want != motor_on) {
            bsp_motor_set(want);
            motor_on = want;
        }
        if (++h->battery_tick % 500 == 0 && h->battery_ok) {
            bsp_battery_status_t b;
            if (bsp_battery_read(&b) == ESP_OK) {
                cJSON *m = message("local_battery");
                cJSON_AddNumberToObject(m, "percent", b.state_of_charge);
                cJSON_AddBoolToObject(m, "charging", b.current_ma > 0);
                incoming(h, m);
            }
        }
        if (active && h->magnet_ok && h->battery_tick % 10 == 0) {
            struct bmm150_mag_data mag;
            for (int sensor = 0; sensor < BSP_MAGNETOMETER_NUM; sensor++)
                if (bsp_magnetometer_read(sensor, &mag) == ESP_OK) {
                    float magnitude = sqrtf(mag.x * mag.x + mag.y * mag.y + mag.z * mag.z);
                    if (!h->magnetic_baseline[sensor])
                        h->magnetic_baseline[sensor] = magnitude;
                    if (fabsf(magnitude - h->magnetic_baseline[sensor]) > 150 &&
                        now(NULL) - h->last_magnetic > 5000) {
                        h->last_magnetic = now(NULL);
                        incoming(h, message("local_magnet"));
                    } else
                        h->magnetic_baseline[sensor] +=
                            .01f * (magnitude - h->magnetic_baseline[sensor]);
                }
        }
        vTaskDelayUntil(&tick, pdMS_TO_TICKS(10));
    }
}
static void command(coop_board_handle_t h, cJSON *m, uint64_t time)
{
    coop_state_handle_t s = coop_ui_state(h->ui);
    const char *type = str(m, "t");
    if (!strcmp(type, "say") || !strcmp(type, "act")) {
        if (!coop_script_push(h->script, m))
            coop_state_say(s, "动作队列已满，请稍等", false, time);
    } else if (!strcmp(type, "walk")) {
        snprintf(h->walk_id, sizeof(h->walk_id), "%s", str(m, "id"));
        coop_state_walk(s, number(m, "to", .5), cJSON_IsTrue(cJSON_GetObjectItem(m, "run")), time);
    } else if (!strcmp(type, "ask") || !strcmp(type, "confirm")) {
        cJSON *a = cJSON_GetObjectItem(m, "options");
        const char *options[3] = {"", "", ""};
        int count = cJSON_GetArraySize(a);
        if (count > 3)
            count = 3;
        for (int i = 0; i < count; i++) {
            cJSON *o = cJSON_GetArrayItem(a, i);
            options[i] = cJSON_IsString(o) ? o->valuestring : "";
        }
        coop_ui_question(h->ui, str(m, "id"), str(m, "question"), options, count,
                         !strcmp(type, "confirm"));
    } else if (!strcmp(type, "listen")) {
        const char *phase = str(m, "phase");
        coop_state_voice_result(s, !strcmp(phase, "transcribing") ? "正在识别…" : str(m, "text"),
                                !strcmp(phase, "transcribing"), time);
    } else if (!strcmp(type, "thinking") && cJSON_IsTrue(cJSON_GetObjectItem(m, "on"))) {
        coop_state_action(s, "thinking", time);
        coop_state_say(s, "让我想一想…", false, time);
    }
}
static void poll(void *ctx, coop_state_handle_t s)
{
    coop_board_handle_t h = ctx;
    uint64_t time = now(NULL);
    atomic_store(&h->desired_edge, coop_state_get(s)->edge);
    coop_state_voice_level(s, coop_audio_level(h->audio));
    asset_notice_t *asset;
    if (xQueueReceive(h->asset_notices, &asset, 0) == pdTRUE) {
        bool ok = coop_ui_atlas(h->ui, asset->bytes, asset->size);
        if (ok) {
            free(h->cached_atlas);
            h->cached_atlas = asset->bytes;
        } else
            free(asset->bytes);
        if (asset->id[0]) {
            cJSON *r = message(ok ? "asset_ready" : "asset_error");
            cJSON_AddStringToObject(r, "id", asset->id);
            cJSON_AddStringToObject(r, "sha256", asset->hash);
            coop_link_send(h->link, r);
        }
        free(asset);
    }
    if (!atomic_load(&h->connected))
        h->capabilities_sent = false;
    if (!atomic_load(&h->connected) && coop_state_get(s)->connected)
        coop_state_connection(s, false);
    bool down;
    while (xQueueReceive(h->buttons, &down, 0) == pdTRUE)
        coop_state_button(s, down, time);
    imu_sample_t sample;
    for (int i = 0; i < 16 && xQueueReceive(h->imu, &sample, 0) == pdTRUE; i++)
        coop_state_imu(s, sample.a[0], sample.a[1], sample.a[2], sample.g[0], sample.g[1],
                       sample.g[2], sample.at);
    cJSON *m;
    for (int i = 0; i < 8 && xQueueReceive(h->messages, &m, 0) == pdTRUE; i++) {
        const char *t = str(m, "t");
        if (!strcmp(t, "presence"))
            coop_state_presence(s, !strcmp(str(m, "owner"), "device"),
                                (uint32_t)number(m, "epoch", 0),
                                cJSON_IsObject(cJSON_GetObjectItem(m, "transfer")));
        else if (!strncmp(t, "transfer_", 9)) {
            cJSON *tr = !strcmp(t, "transfer_status") ? cJSON_GetObjectItem(m, "transfer") : m;
            if (!strcmp(t, "transfer_arrive"))
                coop_state_emotion(s, str(m, "emotion"));
            coop_state_transfer(s, t, str(tr, "id"), (uint32_t)number(tr, "epoch", 0),
                                (uint64_t)number(m, "glowAt", time), time);
        } else if (!strcmp(t, "clock_quality")) {
            bool ready = cJSON_IsTrue(cJSON_GetObjectItem(m, "ready"));
            coop_state_connection(s, ready);
            if (ready && !h->capabilities_sent) {
                cJSON *caps = message("capabilities");
                char app[sizeof("mosaico-coopanion/") + sizeof(esp_app_get_description()->version)];
                snprintf(app, sizeof(app), "mosaico-coopanion/%s", esp_app_get_description()->version);
                cJSON_AddStringToObject(caps, "app", app);
                cJSON_AddNumberToObject(caps, "width", 480);
                cJSON_AddNumberToObject(caps, "height", 480);
                cJSON_AddBoolToObject(caps, "imu", h->imu_ok);
                cJSON_AddBoolToObject(caps, "battery", h->battery_ok);
                cJSON_AddBoolToObject(caps, "magnetic", h->magnet_ok);
                cJSON_AddBoolToObject(caps, "motor", h->motor);
                cJSON_AddBoolToObject(caps, "led", h->led);
                cJSON_AddBoolToObject(caps, "audio", h->audio != NULL);
                cJSON_AddBoolToObject(caps, "nand", h->nand_ok);
                cJSON_AddStringToObject(caps, "resourceStore", "NOR/coo_assets");
                cJSON_AddItemToObject(caps, "assetFormats", cJSON_Parse("[\"COO1\",\"COO2\"]"));
                cJSON_AddNumberToObject(caps, "assetMaxBytes", 1000 * 1024);
                h->capabilities_sent = coop_link_send(h->link, caps);
            }
        } else if (!strcmp(t, "pet_command"))
            command(h, cJSON_GetObjectItem(m, "command"), time);
        else if (!strcmp(t, "voice_error")) {
            coop_audio_release(h->audio, true);
            coop_state_voice_result(s, str(m, "text"), false, time);
        } else if (!strcmp(t, "local_battery")) {
            coop_state_battery(s, (uint8_t)number(m, "percent", 100),
                               cJSON_IsTrue(cJSON_GetObjectItem(m, "charging")));
            cJSON *b = message("battery");
            cJSON_AddNumberToObject(b, "percent", number(m, "percent", 100));
            coop_link_send(h->link, b);
        } else if (!strcmp(t, "local_magnet")) {
            coop_state_touch(s, true, time);
        }
        cJSON_Delete(m);
    }
    coop_script_poll(h->script, s, time, coop_audio_busy(h->audio));
    const coop_snapshot_t *view = coop_state_get(s);
    if (view->resident && !view->transferring && strcmp(h->last_emotion, view->expression)) {
        snprintf(h->last_emotion, sizeof(h->last_emotion), "%s", view->expression);
        cJSON *mood = message("pet_emotion");
        cJSON_AddStringToObject(mood, "face", view->expression);
        coop_link_send(h->link, mood);
        h->emotion_dirty = true;
    }
    if (h->emotion_dirty && time - h->emotion_persisted_at >= EMOTION_PERSIST_DEBOUNCE_MS) {
        h->emotion_dirty = false;
        h->emotion_persisted_at = time;
        const coop_event_t save = {.type = "persist", .at = time};
        output(h, &save);
    }
    bool online = atomic_load(&h->connected);
    if (online && !h->was_connected)
        h->connected_at = time;
    h->was_connected = online;
    coop_link_set_low_latency(
        h->link, view->transferring || view->listening || view->menu ||
                     coop_assets_busy(h->assets) || coop_audio_busy(h->audio) ||
                     (online && time - h->connected_at < LOW_LATENCY_AFTER_CONNECT_MS) ||
                     (view->interacted_at && time - view->interacted_at < LOW_LATENCY_AFTER_INPUT_MS));
    atomic_store(&h->active, view->resident || view->transferring);
    if (h->led)
        bsp_led_set(view->listening);
}
static void restart_after_pairing(void *ctx)
{
    (void)ctx;
    vTaskDelay(pdMS_TO_TICKS(750));
    if (esp_iris_mark_planned_restart() == ESP_OK)
        esp_restart();
    vTaskDelete(NULL);
}
static esp_err_t save_pairing(coop_board_handle_t h, const uint8_t *data, size_t length,
                             uint8_t *out, size_t capacity, size_t *size)
{
    if (capacity < 32)
        return ESP_ERR_INVALID_SIZE;
    if (h->pairing_restarting)
        return ESP_ERR_INVALID_STATE;
    esp_err_t err = coop_link_provision(h->link, data, length);
    if (err == ESP_OK) {
        if (xTaskCreate(restart_after_pairing, "coo_pair_restart", 2048, NULL, 5, NULL) != pdPASS)
            return ESP_ERR_NO_MEM;
        h->pairing_restarting = true;
        static const char reply[] = "{\"saved\":true,\"reboot\":true}";
        memcpy(out, reply, sizeof(reply) - 1);
        *size = sizeof(reply) - 1;
    }
    return err;
}
static void clear_pairing(coop_board_handle_t h)
{
    volatile uint8_t *data = h->pairing;
    for (size_t i = 0; data && i < h->pairing_size; ++i)
        data[i] = 0;
    free(h->pairing);
    h->pairing = NULL;
    h->pairing_size = h->pairing_received = 0;
}
static esp_err_t provision(const esp_iris_rpc_request_t *r, uint8_t *out, size_t capacity,
                           size_t *size, void *ctx)
{
    coop_board_handle_t h = ctx;
    esp_iris_status_t status = {0};
    if (esp_iris_get_status(&status) != ESP_OK || !status.session_ready ||
        status.transport != ESP_IRIS_TRANSPORT_KIND_USB)
        return ESP_ERR_NOT_ALLOWED;
    return save_pairing(h, r->payload, r->payload_size, out, capacity, size);
}
static esp_err_t provision_chunk(const esp_iris_rpc_request_t *r, uint8_t *out, size_t capacity,
                                 size_t *size, void *ctx)
{
    coop_board_handle_t h = ctx;
    esp_iris_status_t status = {0};
    if (esp_iris_get_status(&status) != ESP_OK || !status.session_ready ||
        status.transport != ESP_IRIS_TRANSPORT_KIND_USB)
        return ESP_ERR_NOT_ALLOWED;
    if (r->payload_size <= 4 || r->payload_size > 1024 || capacity < 32 || h->pairing_restarting)
        return ESP_ERR_INVALID_SIZE;
    const uint8_t *p = r->payload;
    size_t total = p[0] | ((size_t)p[1] << 8), offset = p[2] | ((size_t)p[3] << 8);
    size_t length = r->payload_size - 4;
    if (!total || total > PAIRING_LIMIT || offset > total || length > total - offset)
        return ESP_ERR_INVALID_SIZE;
    if (offset == 0) {
        clear_pairing(h);
        h->pairing = malloc(total);
        if (!h->pairing)
            return ESP_ERR_NO_MEM;
        h->pairing_size = total;
        h->pairing_session = status.session_id;
        h->pairing_deadline = now(h) + PAIRING_TIMEOUT_MS;
    }
    if (!h->pairing || total != h->pairing_size || offset != h->pairing_received ||
        status.session_id != h->pairing_session || now(h) > h->pairing_deadline) {
        clear_pairing(h);
        return ESP_ERR_INVALID_STATE;
    }
    memcpy(h->pairing + offset, p + 4, length);
    h->pairing_received += length;
    if (h->pairing_received == total) {
        esp_err_t err = save_pairing(h, h->pairing, total, out, capacity, size);
        clear_pairing(h);
        return err;
    }
    *size = snprintf((char *)out, capacity, "{\"next\":%u}", (unsigned)h->pairing_received);
    return ESP_OK;
}
esp_err_t coop_board_create(coop_board_handle_t *out)
{
    if (!out)
        return ESP_ERR_INVALID_ARG;
    *out = calloc(1, sizeof(**out));
    if (!*out)
        return ESP_ERR_NO_MEM;
    coop_board_handle_t h = *out;
    h->messages = xQueueCreate(24, sizeof(cJSON *));
    h->imu = xQueueCreate(32, sizeof(imu_sample_t));
    h->output = xQueueCreate(24, sizeof(output_t *));
    h->buttons = xQueueCreate(8, sizeof(bool));
    h->asset_notices = xQueueCreate(2, sizeof(asset_notice_t *));
    if (!h->messages || !h->imu || !h->output || !h->buttons || !h->asset_notices)
        return ESP_ERR_NO_MEM;
    const coop_link_config_t config = {.message = incoming, .connection = connection, .ctx = h};
    ESP_ERROR_CHECK(coop_link_create(&config, &h->link));
    const coop_assets_config_t assets = {.complete = asset_complete, .ctx = h, .link = h->link};
    ESP_ERROR_CHECK(coop_assets_create(&assets, &h->assets));
    ESP_ERROR_CHECK(coop_script_create(&h->script));
    h->journal_ok = true;
    nvs_handle_t nvs;
    if (nvs_open("coo_state", NVS_READONLY, &nvs) == ESP_OK) {
        size_t size = sizeof(h->restored);
        if (nvs_get_blob(nvs, "journal", &h->restored, &size) != ESP_OK ||
            size != sizeof(h->restored) || h->restored.version != 1)
            memset(&h->restored, 0, sizeof(h->restored));
        nvs_close(nvs);
    }
    ESP_ERROR_CHECK(esp_iris_rpc_register(0x434f, 1, provision, h));
    ESP_ERROR_CHECK(esp_iris_rpc_register(0x434f, 2, provision_chunk, h));
    if (xTaskCreate(output_worker, "coo_out", 4096, h, 4, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    ESP_ERROR_CHECK_WITHOUT_ABORT(coop_audio_create(h->link, &h->audio));
    return ESP_OK;
}
esp_err_t coop_board_attach(coop_board_handle_t h, esp_gsp_handle_t gsp)
{
    if (!h || !gsp)
        return ESP_ERR_INVALID_ARG;
    h->gsp = gsp;
    const coop_ui_config_t cfg = {.now = now,
                                  .poll = poll,
                                  .event = output,
                                  .ctx = h,
                                  .atlas = coo_start,
                                  .atlas_size = coo_end - coo_start};
    ESP_ERROR_CHECK(coop_ui_create(gsp, &cfg, &h->ui));
    journal_t *j = &h->restored;
    if (j->version == 1) {
        j->id[63] = 0;
        j->emotion[23] = 0;
        coop_state_emotion(coop_ui_state(h->ui), j->emotion);
        coop_state_restore(coop_ui_state(h->ui), j->resident, j->visible, j->transit, j->epoch,
                           j->id, j->muted);
    }
    h->motor = bsp_motor_init() == ESP_OK;
    h->led = bsp_led_init() == ESP_OK;
    h->imu_ok = bsp_imu_init() == ESP_OK;
    h->battery_ok = bsp_battery_init() == ESP_OK;
    h->magnet_ok = bsp_magnetometer_init_all() == ESP_OK;
    /* Probe/resume existing NAND mapping only; never format or claim user sectors.
     * Coo's bounded resource cache lives in its dedicated NOR partition. */
    h->nand_ok = bsp_nand_flash_init(NULL, NULL) == ESP_OK;
    int count = 0;
    if (bsp_iot_button_create(&h->button, &count, 1) == ESP_OK && count) {
        iot_button_register_cb(h->button, BUTTON_PRESS_DOWN, NULL, press, h);
        iot_button_register_cb(h->button, BUTTON_PRESS_UP, NULL, release, h);
    }
    if (xTaskCreate(sensors, "coo_sensors", 4096, h, 6, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    esp_err_t err = coop_link_start(h->link);
    if (err != ESP_OK)
        ESP_LOGW("coopanion", "Pairing required; Iris service 0x434f method 1 (%s)",
                 esp_err_to_name(err));
    return ESP_OK;
}
