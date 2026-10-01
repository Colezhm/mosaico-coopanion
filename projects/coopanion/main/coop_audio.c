// SPDX-License-Identifier: MIT
#include "coop_audio.h"
#include "bsp/esp_mosaico.h"
#include "esp_codec_dev.h"
#include "esp_partition.h"
#include "esp_tts.h"
#include "esp_tts_voice_template.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct coop_audio_t {
    coop_link_handle_t link;
    esp_codec_dev_handle_t speaker, mic;
    esp_tts_handle_t tts;
    esp_tts_voice_t *voice;
    esp_partition_mmap_handle_t mapping;
    bool mapped;
    SemaphoreHandle_t mutex;
    atomic_uint generation, recording;
    atomic_bool cancel, released, busy;
    atomic_uint peak;
    char pending[384], speaking[384];
    uint8_t packet[1292];
};
static void signal(coop_audio_handle_t h, const char *type, uint32_t utterance, bool on)
{
    cJSON *j = cJSON_CreateObject();
    if (!j)
        return;
    cJSON_AddStringToObject(j, "t", type);
    cJSON_AddNumberToObject(j, "utterance", utterance);
    cJSON_AddBoolToObject(j, "on", on);
    coop_link_send(h->link, j);
}
static void put32(uint8_t *p, uint32_t n)
{
    p[0] = n;
    p[1] = n >> 8;
    p[2] = n >> 16;
    p[3] = n >> 24;
}
static void worker(void *arg)
{
    coop_audio_handle_t h = arg;
    esp_codec_dev_sample_info_t format = {
        .sample_rate = 16000, .bits_per_sample = 16, .channel = 1};
    for (;;) {
        uint32_t id = atomic_load(&h->recording);
        if (id) {
            /* Only this task owns codecs. Close/drain playback before microphone
             * capture; discard 80 ms of old I2S input before a new utterance. */
            esp_codec_dev_close(h->speaker);
            bool failed = esp_codec_dev_open(h->mic, &format) != ESP_CODEC_DEV_OK;
            if (!failed)
                for (int i = 0; i < 2; i++)
                    if (esp_codec_dev_read(h->mic, h->packet + 12, 1280) != ESP_CODEC_DEV_OK)
                        failed = true;
            signal(h, "voice_start", id, false);
            uint32_t seq = 0;
            while (!failed && atomic_load(&h->recording) == id && !atomic_load(&h->released) &&
                   seq < 750) {
                if (esp_codec_dev_read(h->mic, h->packet + 12, 1280) != ESP_CODEC_DEV_OK) {
                    failed = true;
                    break;
                }
                memcpy(h->packet, "MPCM", 4);
                put32(h->packet + 4, id);
                put32(h->packet + 8, seq++);
                if (!coop_link_audio(h->link, h->packet, sizeof(h->packet)))
                    failed = true;
                if (seq % 5 == 0) {
                    unsigned peak = 0;
                    const int16_t *pcm = (const int16_t *)(h->packet + 12);
                    for (int i = 0; i < 640; i++) {
                        unsigned a = abs(pcm[i]);
                        if (a > peak)
                            peak = a;
                    }
                    cJSON *m = cJSON_CreateObject();
                    atomic_store(&h->peak, peak);
                    cJSON_AddStringToObject(m, "t", "voice_level");
                    cJSON_AddNumberToObject(m, "level", peak / 32768.0);
                    coop_link_send(h->link, m);
                }
            }
            esp_codec_dev_close(h->mic);
            atomic_store(&h->peak, 0);
            if (atomic_load(&h->recording) != id)
                failed = true;
            cJSON *ended = cJSON_CreateObject();
            cJSON_AddStringToObject(
                ended, "t", failed || atomic_load(&h->cancel) ? "voice_cancel" : "voice_end");
            cJSON_AddNumberToObject(ended, "utterance", id);
            cJSON_AddNumberToObject(ended, "frames", seq);
            coop_link_send(h->link, ended);
            /* Do not erase a new press that arrived while this capture ended. */
            unsigned expected = id;
            atomic_compare_exchange_strong(&h->recording, &expected, 0);
            continue;
        }
        xSemaphoreTake(h->mutex, portMAX_DELAY);
        snprintf(h->speaking, sizeof(h->speaking), "%s", h->pending);
        h->pending[0] = 0;
        unsigned generation = atomic_load(&h->generation);
        xSemaphoreGive(h->mutex);
        if (h->speaking[0] && h->tts &&
            esp_codec_dev_open(h->speaker, &format) == ESP_CODEC_DEV_OK) {
            signal(h, "speaking", 0, true);
            if (esp_tts_parse_chinese(h->tts, h->speaking)) {
                while (generation == atomic_load(&h->generation) && !atomic_load(&h->recording)) {
                    int length = 0;
                    short *pcm = esp_tts_stream_play(h->tts, &length, 3);
                    if (length <= 0 ||
                        esp_codec_dev_write(h->speaker, pcm, length * sizeof(short)) !=
                            ESP_CODEC_DEV_OK)
                        break;
                }
                esp_tts_stream_reset(h->tts);
            }
            esp_codec_dev_close(h->speaker);
            signal(h, "speaking", 0, false);
        }
        if (generation == atomic_load(&h->generation))
            atomic_store(&h->busy, false);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
esp_err_t coop_audio_create(coop_link_handle_t link, coop_audio_handle_t *out)
{
    if (!link || !out)
        return ESP_ERR_INVALID_ARG;
    *out = calloc(1, sizeof(**out));
    if (!*out)
        return ESP_ERR_NO_MEM;
    coop_audio_handle_t h = *out;
    h->link = link;
    h->mutex = xSemaphoreCreateMutex();
    h->speaker = bsp_audio_codec_speaker_init();
    h->mic = bsp_audio_codec_microphone_init();
    if (!h->mutex || !h->speaker || !h->mic)
        goto fail;
    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "voice_data");
    const void *data = NULL;
    if (part && esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA, &data,
                                   &h->mapping) == ESP_OK) {
        h->mapped = true;
        h->voice = esp_tts_voice_set_init(&esp_tts_voice_template, (void *)data);
        if (h->voice)
            h->tts = esp_tts_create(h->voice);
    }
    esp_codec_dev_set_out_vol(h->speaker, 55);
    if (xTaskCreate(worker, "coo_audio", 8192, h, 5, NULL) == pdPASS)
        return ESP_OK;
fail:
    if (h->tts)
        esp_tts_destroy(h->tts);
    if (h->voice)
        esp_tts_voice_set_free(h->voice);
    if (h->mapped)
        esp_partition_munmap(h->mapping);
    /* Codec handles belong to BSP; close them, but do not delete its singletons. */
    if (h->speaker)
        esp_codec_dev_close(h->speaker);
    if (h->mic)
        esp_codec_dev_close(h->mic);
    if (h->mutex)
        vSemaphoreDelete(h->mutex);
    free(h);
    *out = NULL;
    return ESP_FAIL;
}
void coop_audio_stop(coop_audio_handle_t h)
{
    if (!h)
        return;
    atomic_fetch_add(&h->generation, 1);
    xSemaphoreTake(h->mutex, portMAX_DELAY);
    h->pending[0] = 0;
    atomic_store(&h->busy, false);
    xSemaphoreGive(h->mutex);
}
void coop_audio_say(coop_audio_handle_t h, const char *text)
{
    if (!h || !text)
        return;
    coop_audio_stop(h);
    xSemaphoreTake(h->mutex, portMAX_DELAY);
    snprintf(h->pending, sizeof(h->pending), "%s", text);
    atomic_store(&h->busy, true);
    xSemaphoreGive(h->mutex);
}
void coop_audio_record(coop_audio_handle_t h, uint32_t id)
{
    if (!h || !id)
        return;
    coop_audio_stop(h);
    atomic_store(&h->cancel, false);
    atomic_store(&h->released, false);
    atomic_store(&h->recording, id);
}
void coop_audio_release(coop_audio_handle_t h, bool cancel)
{
    if (!h)
        return;
    atomic_store(&h->cancel, cancel);
    atomic_store(&h->released, true);
}
float coop_audio_level(coop_audio_handle_t h)
{
    return h ? atomic_load(&h->peak) / 32768.f : 0;
}
bool coop_audio_busy(coop_audio_handle_t h)
{
    return h && atomic_load(&h->busy);
}
