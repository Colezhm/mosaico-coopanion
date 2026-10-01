// SPDX-License-Identifier: MIT
#include "coop_assets.h"
#include "coop_render.h"
#include "esp_partition.h"
#include "mbedtls/base64.h"
#include "psa/crypto.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define ASSET_MAX (900 * 1024)
typedef struct {
    uint32_t magic, size;
    char hash[65];
} asset_header_t;
struct coop_assets_t {
    coop_assets_config_t cfg;
    QueueHandle_t queue;
    const esp_partition_t *partition;
    uint8_t *bytes;
    size_t size, offset;
    char id[64], hash[65], cached_hash[65];
};
static const char *str(cJSON *j, const char *key)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}
static bool digest(const uint8_t *data, size_t length, char out[65])
{
    uint8_t hash[32];
    size_t written = 0;
    if (psa_hash_compute(PSA_ALG_SHA_256, data, length, hash, sizeof(hash), &written) !=
            PSA_SUCCESS ||
        written != 32)
        return false;
    for (unsigned i = 0; i < 32; i++)
        snprintf(out + i * 2, 3, "%02x", hash[i]);
    return true;
}
static void reply(coop_assets_handle_t h, const char *type, const char *error)
{
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "t", type);
    cJSON_AddStringToObject(m, "id", h->id);
    cJSON_AddNumberToObject(m, "offset", h->offset);
    cJSON_AddStringToObject(m, "sha256", h->hash);
    if (error)
        cJSON_AddStringToObject(m, "error", error);
    coop_link_send(h->cfg.link, m);
}
static void handle(coop_assets_handle_t h, cJSON *m)
{
    const char *t = str(m, "t"), *id = str(m, "id");
    if (!strcmp(t, "asset_offer")) {
        const char *hash = str(m, "sha256");
        cJSON *size = cJSON_GetObjectItem(m, "size");
        if (strlen(id) >= sizeof(h->id) || strlen(hash) != 64 || !cJSON_IsNumber(size) ||
            size->valuedouble < 188 || size->valuedouble > ASSET_MAX)
            return;
        if (!strcmp(id, h->id) && h->bytes) {
            reply(h, "asset_next", NULL);
            return;
        }
        free(h->bytes);
        h->bytes = NULL;
        h->offset = 0;
        h->size = (size_t)size->valuedouble;
        snprintf(h->id, sizeof(h->id), "%s", id);
        snprintf(h->hash, sizeof(h->hash), "%s", hash);
        if (!strcmp(h->cached_hash, hash)) {
            reply(h, "asset_ready", NULL);
            return;
        }
        h->bytes = malloc(h->size);
        if (!h->bytes) {
            reply(h, "asset_error", "设备内存不足");
            return;
        }
        reply(h, "asset_next", NULL);
        return;
    }
    if (strcmp(id, h->id))
        return;
    if (!strcmp(t, "asset_commit") && !strcmp(h->cached_hash, h->hash)) {
        reply(h, "asset_ready", NULL);
        return;
    }
    if (!h->bytes)
        return;
    if (!strcmp(t, "asset_chunk")) {
        cJSON *offset = cJSON_GetObjectItem(m, "offset");
        const char *data = str(m, "data");
        if (!cJSON_IsNumber(offset) || offset->valuedouble != h->offset) {
            reply(h, "asset_next", NULL);
            return;
        }
        size_t decoded = 0;
        size_t length = strlen(data);
        if (length > 5464 ||
            mbedtls_base64_decode(h->bytes + h->offset, h->size - h->offset, &decoded,
                                  (const unsigned char *)data, length) != 0 ||
            decoded > 4096) {
            reply(h, "asset_error", "资源分块损坏");
            free(h->bytes);
            h->bytes = NULL;
            return;
        }
        h->offset += decoded;
        reply(h, "asset_next", NULL);
    } else if (!strcmp(t, "asset_commit")) {
        char hash[65];
        if (h->offset != h->size || !coop_atlas_validate(h->bytes, h->size) ||
            !digest(h->bytes, h->size, hash) || strcmp(hash, h->hash)) {
            reply(h, "asset_error", "资源校验失败");
            free(h->bytes);
            h->bytes = NULL;
            return;
        }
        asset_header_t header = {.magic = 0x434f4f31, .size = h->size};
        memcpy(header.hash, hash, 65);
        /* Header is written last. A interrupted write is rejected at next boot;
         * the embedded default atlas remains available without a connection. */
        size_t erase = (sizeof(header) + h->size + 4095) & ~4095u;
        if (!h->partition || esp_partition_erase_range(h->partition, 0, erase) != ESP_OK ||
            esp_partition_write(h->partition, sizeof(header), h->bytes, h->size) != ESP_OK ||
            esp_partition_write(h->partition, 0, &header, sizeof(header)) != ESP_OK) {
            reply(h, "asset_error", "角色缓存写入失败");
            free(h->bytes);
            h->bytes = NULL;
            return;
        }
        memcpy(h->cached_hash, hash, 65);
        h->cfg.complete(h->cfg.ctx, h->bytes, h->size, h->id, h->hash);
        h->bytes = NULL;
    }
}
static void worker(void *ctx)
{
    coop_assets_handle_t h = ctx;
    asset_header_t header;
    if (h->partition && esp_partition_read(h->partition, 0, &header, sizeof(header)) == ESP_OK &&
        header.magic == 0x434f4f31 && header.size >= 188 && header.size <= ASSET_MAX &&
        header.hash[64] == 0) {
        uint8_t *data = malloc(header.size);
        char hash[65];
        if (data && esp_partition_read(h->partition, sizeof(header), data, header.size) == ESP_OK &&
            coop_atlas_validate(data, header.size) && digest(data, header.size, hash) &&
            !strcmp(hash, header.hash)) {
            memcpy(h->cached_hash, hash, 65);
            h->cfg.complete(h->cfg.ctx, data, header.size, "", hash);
        } else
            free(data);
    }
    cJSON *m;
    for (;;)
        if (xQueueReceive(h->queue, &m, portMAX_DELAY) == pdTRUE) {
            handle(h, m);
            cJSON_Delete(m);
        }
}
esp_err_t coop_assets_create(const coop_assets_config_t *cfg, coop_assets_handle_t *out)
{
    if (!cfg || !cfg->complete || !out)
        return ESP_ERR_INVALID_ARG;
    *out = calloc(1, sizeof(**out));
    if (!*out)
        return ESP_ERR_NO_MEM;
    coop_assets_handle_t h = *out;
    h->cfg = *cfg;
    h->queue = xQueueCreate(4, sizeof(cJSON *));
    if (!h->queue)
        return ESP_ERR_NO_MEM;
    h->partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "coo_assets");
    return xTaskCreate(worker, "coo_assets", 4096, h, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
void coop_assets_message(coop_assets_handle_t h, cJSON *m)
{
    if (!h || xQueueSend(h->queue, &m, 0) != pdTRUE)
        cJSON_Delete(m);
}
