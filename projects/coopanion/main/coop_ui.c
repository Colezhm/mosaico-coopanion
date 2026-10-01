// SPDX-License-Identifier: MIT
#include "coop_ui.h"
#include "coop_render.h"
#define GSP_BUNDLE_ENABLE_RAW_IDS
#include "bundle_gsp.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
struct coop_ui_t {
    esp_gsp_handle_t gsp;
    coop_ui_config_t cfg;
    coop_state_handle_t state;
    coop_render_handle_t render;
    void *timer;
    char text[384], status[96], previous_status[96], question_id[64];
    bool menu, confirmation;
    int answers;
    const char *receipt_type;
    char receipt_id[64];
    uint32_t receipt_epoch;
    uint64_t receipt_at;
};
static void state_event(void *ctx, const coop_event_t *e)
{
    coop_ui_handle_t h = ctx;
    if (!strcmp(e->type, "transfer_hidden") || !strcmp(e->type, "transfer_arrived") ||
        !strcmp(e->type, "glow_started") || !strcmp(e->type, "body_entered")) {
        h->receipt_type = e->type;
        snprintf(h->receipt_id, sizeof(h->receipt_id), "%s", e->text ? e->text : "");
        h->receipt_epoch = e->epoch;
        h->receipt_at = e->at;
    } else if (h->cfg.event)
        h->cfg.event(h->cfg.ctx, e);
}
static void draw(const esp_gsp_canvas_surface_t *s, void *ctx)
{
    coop_ui_handle_t h = ctx;
    coop_render_draw(h->render, coop_state_get(h->state), s->pixels, s->stride_bytes, s->x, s->y,
                     s->width, s->height);
}
static void tick(esp_gsp_handle_t gsp, void *ctx)
{
    coop_ui_handle_t h = ctx;
    if (h->cfg.poll)
        h->cfg.poll(h->cfg.ctx, h->state);
    coop_state_tick(h->state, h->cfg.now(h->cfg.ctx));
    const coop_snapshot_t *s = coop_state_get(h->state);
    if (strcmp(h->text, s->subtitle)) {
        snprintf(h->text, sizeof(h->text), "%s", s->subtitle);
        gsp_coop_subtitle_set_text(gsp, h->text);
    }
    snprintf(h->status, sizeof(h->status), "%s  ·  %u%%  %s",
             s->transferring           ? "传送中"
             : s->listening            ? "倾听"
             : s->motion == COOP_THINK ? "思考"
             : s->motion == COOP_SPEAK ? "播报"
             : s->connected            ? "已连接"
                                       : "离线",
             s->battery, s->muted ? "静音" : "");
    if (strcmp(h->status, h->previous_status)) {
        gsp_coop_status_set_text(gsp, h->status);
        strcpy(h->previous_status, h->status);
    }
    if (h->menu != s->menu) {
        h->menu = s->menu;
        gsp_coop_menu_set_visible(gsp, h->menu);
    }
    esp_gsp_canvas_invalidate(gsp, GSP_COOP_BIND_CANVAS);
    /* Submit the changed frame before asking the board's I/O worker to fence
     * rendering and acknowledge it. Never block the render task itself. */
    if (h->receipt_type && h->cfg.event) {
        const coop_event_t e = {.type = h->receipt_type,
                                .text = h->receipt_id,
                                .epoch = h->receipt_epoch,
                                .at = h->receipt_at};
        h->cfg.event(h->cfg.ctx, &e);
        h->receipt_type = NULL;
    }
}
static void event(esp_gsp_handle_t gsp, const esp_gsp_event_t *e, void *ctx)
{
    (void)gsp;
    coop_ui_handle_t h = ctx;
    uint64_t now = h->cfg.now(h->cfg.ctx);
    const coop_snapshot_t *s = coop_state_get(h->state);
    if (gsp_coop_event_is_poke(e))
        coop_state_touch(h->state, false, now);
    else if (gsp_coop_event_is_menu_open(e))
        coop_state_menu(h->state, true);
    else if (gsp_coop_event_is_menu_close(e))
        coop_state_menu(h->state, false);
    else if (gsp_coop_event_is_petting(e))
        coop_state_touch(h->state, true, now);
    else if (gsp_coop_event_is_mute(e))
        coop_state_mute(h->state, !s->muted);
    else if (gsp_coop_event_is_return_pc(e) && h->cfg.event) {
        const coop_event_t msg = {.type = "return", .at = now};
        h->cfg.event(h->cfg.ctx, &msg);
    } else if (gsp_coop_event_is_answer(e) && e->arg < (uint32_t)h->answers && h->cfg.event) {
        const coop_event_t msg = {.type = h->confirmation ? "confirmed" : "answer",
                                  .text = h->question_id,
                                  .epoch = e->arg,
                                  .at = now};
        h->cfg.event(h->cfg.ctx, &msg);
        h->answers = 0;
        gsp_coop_answer_0_set_visible(h->gsp, false);
        gsp_coop_answer_1_set_visible(h->gsp, false);
        gsp_coop_answer_2_set_visible(h->gsp, false);
    }
}
esp_err_t coop_ui_create(esp_gsp_handle_t gsp, const coop_ui_config_t *cfg, coop_ui_handle_t *out)
{
    if (!gsp || !cfg || !cfg->now || !out)
        return ESP_ERR_INVALID_ARG;
    *out = calloc(1, sizeof(**out));
    if (!*out)
        return ESP_ERR_NO_MEM;
    coop_ui_handle_t h = *out;
    h->gsp = gsp;
    h->cfg = *cfg;
    coop_state_config_t state_cfg = {.event = state_event, .ctx = h, .seed = 1};
    if (coop_state_create(&state_cfg, &h->state) != ESP_OK ||
        coop_render_create(&h->render) != ESP_OK ||
        !coop_render_atlas(h->render, cfg->atlas, cfg->atlas_size)) {
        coop_ui_delete(h);
        *out = NULL;
        return ESP_ERR_INVALID_ARG;
    }
    esp_gsp_err_t err = esp_gsp_canvas_set_draw_cb(gsp, GSP_COOP_BIND_CANVAS, draw, h);
    if (err == ESP_GSP_OK)
        err = esp_gsp_on_event(gsp, event, h);
    if (err == ESP_GSP_OK)
        h->timer = esp_gsp_timer_create(gsp, 33, tick, h);
    if (err != ESP_GSP_OK || !h->timer) {
        coop_ui_delete(h);
        *out = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
void coop_ui_delete(coop_ui_handle_t h)
{
    if (!h)
        return;
    if (h->timer)
        esp_gsp_timer_delete(h->gsp, h->timer);
    if (h->gsp) {
        esp_gsp_on_event(h->gsp, NULL, NULL);
        esp_gsp_canvas_stop(h->gsp, GSP_COOP_BIND_CANVAS);
        esp_gsp_flush(h->gsp, 1000);
    }
    coop_render_delete(h->render);
    coop_state_delete(h->state);
    free(h);
}
coop_state_handle_t coop_ui_state(coop_ui_handle_t h)
{
    return h ? h->state : NULL;
}
bool coop_ui_atlas(coop_ui_handle_t h, const uint8_t *bytes, size_t size)
{
    return h && coop_render_atlas(h->render, bytes, size);
}
void coop_ui_question(coop_ui_handle_t h, const char *id, const char *question,
                      const char *const options[], int count, bool confirmation)
{
    if (!h)
        return;
    h->answers = count < 0 ? 0 : count > 3 ? 3 : count;
    h->confirmation = confirmation;
    snprintf(h->question_id, sizeof(h->question_id), "%s", id ? id : "");
    coop_state_say(h->state, question ? question : "", true, h->cfg.now(h->cfg.ctx));
    gsp_coop_answer_0_set_text(h->gsp, count > 0 ? options[0] : "");
    gsp_coop_answer_1_set_text(h->gsp, count > 1 ? options[1] : "");
    gsp_coop_answer_2_set_text(h->gsp, count > 2 ? options[2] : "");
    gsp_coop_answer_0_set_visible(h->gsp, count > 0);
    gsp_coop_answer_1_set_visible(h->gsp, count > 1);
    gsp_coop_answer_2_set_visible(h->gsp, count > 2);
}
