// SPDX-License-Identifier: MIT
#include "coop_script.h"
#include <stdlib.h>
#include <string.h>
struct coop_script_t {
    cJSON *queue[8];
    unsigned head, count, beat, action;
    bool spoken;
    uint64_t next;
};
static void pop(coop_script_handle_t h)
{
    if (!h->count)
        return;
    cJSON_Delete(h->queue[h->head]);
    h->queue[h->head] = NULL;
    h->head = (h->head + 1) % 8;
    h->count--;
    h->beat = h->action = 0;
    h->spoken = false;
    h->next = 0;
}
esp_err_t coop_script_create(coop_script_handle_t *out)
{
    if (!out)
        return ESP_ERR_INVALID_ARG;
    *out = calloc(1, sizeof(**out));
    return *out ? ESP_OK : ESP_ERR_NO_MEM;
}
void coop_script_delete(coop_script_handle_t h)
{
    if (h) {
        while (h->count)
            pop(h);
        free(h);
    }
}
bool coop_script_push(coop_script_handle_t h, const cJSON *m)
{
    if (!h || !m || h->count == 8)
        return false;
    cJSON *kind = cJSON_GetObjectItemCaseSensitive(m, "t");
    if (!cJSON_IsString(kind) ||
        (strcmp(kind->valuestring, "say") && strcmp(kind->valuestring, "act")))
        return false;
    cJSON *copy = cJSON_Duplicate(m, true);
    if (!copy)
        return false;
    h->queue[(h->head + h->count) % 8] = copy;
    h->count++;
    return true;
}
void coop_script_poll(coop_script_handle_t h, coop_state_handle_t s, uint64_t now, bool audio_busy)
{
    if (!h || !s)
        return;
    const coop_snapshot_t *v = coop_state_get(s);
    if (!v->resident || v->transferring) {
        while (h->count)
            pop(h);
        return;
    }
    if (!h->count || now < h->next || v->listening || coop_state_animation_busy(s))
        return;
    cJSON *m = h->queue[h->head], *kind = cJSON_GetObjectItem(m, "t");
    bool say = !strcmp(kind->valuestring, "say");
    cJSON *beat = say ? cJSON_GetArrayItem(cJSON_GetObjectItem(m, "beats"), h->beat) : m;
    if (!beat) {
        pop(h);
        return;
    }
    if (say && !h->spoken) {
        if (audio_busy)
            return;
        cJSON *text = cJSON_GetObjectItem(beat, "text"), *face = cJSON_GetObjectItem(beat, "face");
        if (cJSON_IsString(face))
            coop_state_action(s, face->valuestring, now);
        if (cJSON_IsString(text))
            coop_state_say(s, text->valuestring, true, now);
        h->spoken = true;
        h->next = now + 250;
        return;
    }
    cJSON *a = cJSON_GetArrayItem(cJSON_GetObjectItem(beat, "actions"), h->action);
    if (a) {
        h->action++;
        if (cJSON_IsString(a))
            coop_state_action(s, a->valuestring, now);
        h->next = now + 1500;
        return;
    }
    if (say && audio_busy)
        return;
    if (say) {
        h->beat++;
        h->action = 0;
        h->spoken = false;
    } else
        pop(h);
}
