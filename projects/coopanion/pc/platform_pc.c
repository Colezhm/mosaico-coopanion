// SPDX-License-Identifier: MIT
/* The simulator executes the same controller and renderer as the device. */
#include "gsp_sim_bridge.h"
#include "coop_ui.h"
#include "link_pc.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

static coop_ui_handle_t app;
static uint8_t *atlas;
static uint64_t started;
static unsigned step;
static uint64_t now(void *ctx)
{
    (void)ctx;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static void event(void *ctx, const coop_event_t *e)
{
    coop_pc_link_event(e);
    (void)ctx;
    fprintf(stderr, "COOP_EVENT %llu %s %s\n", (unsigned long long)(e->at - started), e->type,
            e->text ? e->text : "");
}
static void poll(void *ctx, coop_state_handle_t state)
{
    (void)ctx;
    uint64_t time = now(NULL), t = time - started;
    coop_state_imu(state, 0, 0, 1, 0, 0, 0, time);
    coop_pc_link_poll(app, time);
    if (!getenv("COOP_SIM_DEMO"))
        return;
    static const uint32_t at[] = {200,   4000,  5200,  7000,  10400, 11500,
                                  12400, 14000, 15600, 16400, 19000, 22000};
    if (step >= sizeof(at) / sizeof(at[0]) || t < at[step])
        return;
    switch (step++) {
    case 0:
        coop_state_transfer(state, "transfer_arrive", "sim-in", 1, time + 500, time);
        break;
    case 1:
        coop_state_action(state, "sway", time);
        break;
    case 2:
        coop_state_action(state, "stumble", time);
        break;
    case 3:
        coop_state_imu(state, 2.8f, 0, 1, 400, 0, 0, time);
        break;
    case 4:
        coop_state_touch(state, true, time);
        break;
    case 5:
        coop_state_action(state, "sulk", time);
        break;
    case 6:
        coop_state_touch(state, true, time);
        break;
    case 7:
        coop_state_button(state, true, time);
        break;
    case 8:
        coop_state_button(state, false, time);
        break;
    case 9:
        coop_state_say(state, "听清啦！我们一起去电脑上看看。", true, time);
        break;
    case 10:
        coop_state_transfer(state, "transfer_depart", "sim-out", 2, 0, time);
        break;
    case 11:
        coop_state_connection(state, false);
        coop_state_presence(state, true, 3, false);
        coop_state_say(state, "电脑未连接，陪你玩一会儿", false, time);
        break;
    }
}
esp_gsp_err_t gsp_bridge_app_init(esp_gsp_handle_t ui)
{
    FILE *file = fopen(COOP_ATLAS_PATH, "rb");
    if (!file)
        return ESP_GSP_ERR_INVALID_ARG;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    atlas = malloc(size);
    if (!atlas || fread(atlas, 1, size, file) != (size_t)size) {
        fclose(file);
        free(atlas);
        return ESP_GSP_ERR_NO_MEM;
    }
    fclose(file);
    started = now(NULL);
    step = 0;
    const coop_ui_config_t cfg = {
        .now = now, .poll = poll, .event = event, .atlas = atlas, .atlas_size = size};
    if (coop_ui_create(ui, &cfg, &app) != ESP_OK) {
        free(atlas);
        return ESP_GSP_ERR_INVALID_ARG;
    }
    coop_state_connection(coop_ui_state(app), true);
    if (!getenv("COOP_SIM_DEMO") && !getenv("COOP_SIM_PORT"))
        coop_state_presence(coop_ui_state(app), true, 1, false);
    coop_pc_link_start();
    return ESP_GSP_OK;
}
void gsp_bridge_app_deinit(esp_gsp_handle_t ui)
{
    (void)ui;
    coop_ui_delete(app);
    coop_pc_link_stop();
    app = NULL;
    free(atlas);
    atlas = NULL;
}
