// SPDX-License-Identifier: MIT
#include "coop_wifi_ui.h"
#include "coop_wifi_pick.h"
#define GSP_BUNDLE_ENABLE_RAW_IDS
#include "bundle_gsp.h"
#include <stdio.h>
#include <string.h>

#define PAGE_CLOSED 0
#define PAGE_NETWORKS 1
#define PAGE_PASSWORD 2
#define MIN_PASSWORD 8
#define DISPLAY_TAIL 22
#define NOTE_DEFAULT "长按顶部状态栏随时回到这里"

static void wipe(void *data, size_t size)
{
    volatile char *p = data;
    while (size--)
        *p++ = 0;
}

static gsp_err_t network_row(esp_gsp_handle_t gsp, esp_gsp_row_t row, uint32_t index, void *ctx)
{
    coop_wifi_ui_t *w = ctx;
    if (index >= w->status.ap_count)
        return ESP_GSP_OK;
    const coop_wifi_ap_t *ap = &w->status.aps[index];
    bool current = w->status.link == COOP_WIFI_CONNECTED && !strcmp(ap->ssid, w->status.ssid);
    char detail[64];
    /* One run without spaces: list rows reshape cleanly when its length changes. */
    snprintf(detail, sizeof(detail), "%s%s，%s", current ? "当前，" : "",
             ap->saved ? "已保存" : ap->secure ? "需要密码" : "开放网络", coop_wifi_strength(ap->rssi));
    esp_gsp_err_t err = esp_gsp_row_set_text(gsp, row, GSP_COOP_TEMPLATE_NETWORKS_ROW_NETWORKS_TITLE_TEXT_SLOT, ap->ssid);
    if (err == ESP_GSP_OK)
        err = esp_gsp_row_set_text(gsp, row, GSP_COOP_TEMPLATE_NETWORKS_ROW_NETWORKS_DETAIL_TEXT_SLOT, detail);
    return err;
}

void coop_wifi_ui_init(coop_wifi_ui_t *w, esp_gsp_handle_t gsp)
{
    memset(w, 0, sizeof(*w));
    w->list = esp_gsp_list_bind_component(gsp, GSP_COOP_OBJ_KEY_NETWORKS, network_row, w);
}
static void asked_to_join(coop_wifi_ui_t *w, const char *ssid)
{
    coop_wifi_status_t now;
    coop_wifi_status(&now);
    w->seen_error = now.error_revision;
    snprintf(w->joining, sizeof(w->joining), "%s", ssid);
}

static void project_password(coop_wifi_ui_t *w, esp_gsp_handle_t gsp)
{
    size_t n = strlen(w->password);
    char display[COOP_WIFI_PASSWORD_BYTES] = {0};
    if (w->show_password)
        memcpy(display, w->password, n);
    else
        memset(display, '*', n);
    gsp_coop_password_display_set_text(gsp, n ? display + (n > DISPLAY_TAIL ? n - DISPLAY_TAIL : 0) : "输入密码");
    wipe(display, sizeof(display));
}

static void show(coop_wifi_ui_t *w, esp_gsp_handle_t gsp, int page)
{
    w->page = page;
    gsp_coop_wifi_set_visible(gsp, page == PAGE_NETWORKS);
    gsp_coop_password_set_visible(gsp, page == PAGE_PASSWORD);
    if (page != PAGE_PASSWORD)
        wipe(w->password, sizeof(w->password));
    if (page == PAGE_PASSWORD) {
        w->show_password = false;
        gsp_coop_password_ssid_set_text(gsp, w->selected);
        gsp_coop_password_toggle_set_text(gsp, "显示");
        gsp_coop_password_error_set_text(gsp, "");
        project_password(w, gsp);
        /* Always start on the lower-case letters. */
        esp_gsp_component_set_visible(gsp, GSP_COOP_OBJ_KEY_PASSWORD_KEYBOARD_LOWER, true);
        esp_gsp_component_set_visible(gsp, GSP_COOP_OBJ_KEY_PASSWORD_KEYBOARD_UPPER, false);
        esp_gsp_component_set_visible(gsp, GSP_COOP_OBJ_KEY_PASSWORD_KEYBOARD_SYM, false);
    }
    w->dirty = true;
}

static void note(coop_wifi_ui_t *w, const char *text)
{
    snprintf(w->note, sizeof(w->note), "%s", text);
    w->dirty = true;
}

static void submit(coop_wifi_ui_t *w, esp_gsp_handle_t gsp)
{
    if (strlen(w->password) < MIN_PASSWORD) {
        gsp_coop_password_error_set_text(gsp, "密码至少 8 位");
        return;
    }
    asked_to_join(w, w->selected);
    if (coop_wifi_join(w->selected, w->password) != ESP_OK) {
        gsp_coop_password_error_set_text(gsp, "现在不能连接，请稍后再试");
        return;
    }
    char text[96];
    snprintf(text, sizeof(text), "正在连接 %s…", w->selected);
    note(w, text);
    show(w, gsp, PAGE_NETWORKS);
}

void coop_wifi_ui_show(coop_wifi_ui_t *w, esp_gsp_handle_t gsp)
{
    note(w, NOTE_DEFAULT);
    show(w, gsp, PAGE_NETWORKS);
    coop_wifi_scan();
}

bool coop_wifi_ui_event(coop_wifi_ui_t *w, esp_gsp_handle_t gsp, const esp_gsp_event_t *e)
{
    if (gsp_coop_event_is_wifi_open(e)) {
        if (!w->page)
            coop_wifi_ui_show(w, gsp);
        return true;
    }
    if (!w->page)
        return false;
    if (gsp_coop_event_is_wifi_back(e))
        show(w, gsp, PAGE_CLOSED);
    else if (gsp_coop_event_is_wifi_rescan(e))
        coop_wifi_scan();
    else if (gsp_coop_event_is_wifi_forget(e)) {
        if (w->status.current_saved && w->status.ssid[0]) {
            char text[96];
            snprintf(text, sizeof(text), "已忘记 %s", w->status.ssid);
            coop_wifi_forget(w->status.ssid);
            note(w, text);
        }
    } else if (gsp_coop_event_is_select_network(e)) {
        if (w->page != PAGE_NETWORKS || e->list != w->list || e->item >= w->status.ap_count)
            return true;
        const coop_wifi_ap_t *ap = &w->status.aps[e->item];
        snprintf(w->selected, sizeof(w->selected), "%s", ap->ssid);
        if (ap->saved || !ap->secure) {
            /* Known or open: no password to ask for. */
            char text[96];
            snprintf(text, sizeof(text), "正在连接 %s…", ap->ssid);
            asked_to_join(w, ap->ssid);
            if (coop_wifi_join(ap->ssid, ap->saved ? NULL : "") == ESP_OK)
                note(w, text);
        } else
            show(w, gsp, PAGE_PASSWORD);
    } else if (gsp_coop_event_is_password_back(e))
        show(w, gsp, PAGE_NETWORKS);
    else if (gsp_coop_event_is_toggle_password(e)) {
        w->show_password = !w->show_password;
        gsp_coop_password_toggle_set_text(gsp, w->show_password ? "隐藏" : "显示");
        project_password(w, gsp);
    } else if (gsp_coop_event_is_password_keyboard_key(e)) {
        if (w->page != PAGE_PASSWORD)
            return true;
        /* Native keys emit code points: Backspace 8, Enter 13. The buffer is
         * ours so masking works the same on the device and in the simulator. */
        size_t len = strlen(w->password);
        if (e->arg == 13)
            submit(w, gsp);
        else if (e->arg == 8 && len)
            w->password[len - 1] = 0;
        else if (e->arg >= 32 && e->arg <= 126 && len + 1 < sizeof(w->password)) {
            w->password[len] = (char)e->arg;
            w->password[len + 1] = 0;
        }
        if (w->page == PAGE_PASSWORD) {
            gsp_coop_password_error_set_text(gsp, "");
            project_password(w, gsp);
        }
    } else
        return false;
    return true;
}

bool coop_wifi_ui_tick(coop_wifi_ui_t *w, esp_gsp_handle_t gsp)
{
    if (!w->page && !w->dirty)
        return false;
    coop_wifi_status(&w->status);
    if (w->status.revision == w->seen_revision && !w->dirty)
        return false;
    w->seen_revision = w->status.revision;
    w->dirty = false;
    const coop_wifi_status_t *st = &w->status;
    /* The outcome of the join the person asked for replaces "connecting":
     * a failure (the board goes back to the previous network), or success. */
    if (w->joining[0] && st->error_revision != w->seen_error) {
        snprintf(w->note, sizeof(w->note), "连接失败：%s", st->error);
        w->joining[0] = 0;
    } else if (w->joining[0] && st->link == COOP_WIFI_CONNECTED && !strcmp(st->ssid, w->joining)) {
        snprintf(w->note, sizeof(w->note), "已连上 %s", st->ssid);
        w->joining[0] = 0;
    }
    char current[64], detail[64];
    switch (st->link) {
    case COOP_WIFI_CONNECTED:
        snprintf(current, sizeof(current), "已连接：%s", st->ssid);
        snprintf(detail, sizeof(detail), "IP %s，%s", st->ip, coop_wifi_strength(st->rssi));
        break;
    case COOP_WIFI_CONNECTING:
        snprintf(current, sizeof(current), "正在连接：%s", st->ssid);
        detail[0] = 0;
        break;
    case COOP_WIFI_FAILED:
        snprintf(current, sizeof(current), "%s", st->ssid[0] ? st->ssid : "未连接");
        snprintf(detail, sizeof(detail), "%s", st->error);
        break;
    default:
        snprintf(current, sizeof(current), "未连接");
        detail[0] = 0;
        break;
    }
    gsp_coop_wifi_current_set_text(gsp, current);
    gsp_coop_wifi_detail_set_text(gsp, detail);
    gsp_coop_wifi_forget_box_set_visible(gsp, st->current_saved && st->link == COOP_WIFI_CONNECTED);
    char scan[48];
    if (st->scanning)
        snprintf(scan, sizeof(scan), "正在扫描…");
    else
        snprintf(scan, sizeof(scan), "附近的网络（%u）", st->ap_count);
    gsp_coop_wifi_scan_set_text(gsp, scan);
    gsp_coop_wifi_note_set_text(gsp, w->note[0] ? w->note : NOTE_DEFAULT);
    if (w->list != ESP_GSP_LIST_NONE) {
        esp_gsp_list_set_total(gsp, w->list, st->ap_count);
        esp_gsp_list_refresh(gsp, w->list);
    }
    return true;
}
