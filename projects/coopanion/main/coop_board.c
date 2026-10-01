// SPDX-License-Identifier: MIT
#include "coop_board.h"
#include "coop_link.h"
#include "coop_audio.h"
#include "coop_assets.h"
#include "coop_script.h"
#include "bsp/esp_mosaico.h"
#include "esp_iris.h"
#include "esp_timer.h"
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
    journal_t restored, durable;
    unsigned battery_tick;
    float magnetic_baseline[BSP_MAGNETOMETER_NUM];
    uint64_t last_magnetic;
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
            free(e);
            continue;
        }
        if (!strcmp(e->type, "transfer_hidden") || !strcmp(e->type, "transfer_arrived") ||
            !strcmp(e->type, "glow_started") || !strcmp(e->type, "body_entered")) {
            if (esp_gsp_flush(h->gsp, 1000) != ESP_GSP_OK) {
                free(e);
                continue;
            }
            e->at = now(NULL);
        }
        if ((!strcmp(e->type, "transfer_hidden") || !strcmp(e->type, "transfer_arrived")) &&
            (h->durable.epoch != e->epoch || strcmp(h->durable.id, e->text) ||
             (!strcmp(e->type, "transfer_hidden") && h->durable.visible) ||
             (!strcmp(e->type, "transfer_arrived") &&
              (!h->durable.visible || !h->durable.resident)))) {
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
                cJSON_AddStringToObject(caps, "app", "mosaico-coopanion/0.1.0-mosaico.1");
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
        const coop_event_t save = {.type = "persist", .at = time};
        output(h, &save);
    }
    atomic_store(&h->active, view->resident || view->transferring);
    if (h->led)
        bsp_led_set(view->listening);
}
static esp_err_t provision(const esp_iris_rpc_request_t *r, uint8_t *out, size_t capacity,
                           size_t *size, void *ctx)
{
    coop_board_handle_t h = ctx;
    if (capacity < 32)
        return ESP_ERR_INVALID_SIZE;
    esp_err_t err = coop_link_provision(h->link, r->payload, r->payload_size);
    if (err == ESP_OK) {
        static const char reply[] = "{\"saved\":true,\"reboot\":true}";
        memcpy(out, reply, sizeof(reply) - 1);
        *size = sizeof(reply) - 1;
    }
    return err;
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
