// SPDX-License-Identifier: MIT
/* Local test bridge only. Device networking remains in coop_link.c. */
#include "link_pc.h"
#include "coop_script.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
static coop_script_handle_t script;
static int listener = -1, peer = -1;
static char *buffer;
static size_t used;
static bool capabilities_sent;
static uint8_t *cached;
/* The PC backend is single-instance and UI-thread owned. Hold injected sensors
 * between frames, exactly like the board's latest physical samples. */
static float imu[6]={0,0,1,0,0,0};
static const char *str(cJSON *j, const char *k)
{
    cJSON *v = cJSON_GetObjectItem(j, k);
    return cJSON_IsString(v) ? v->valuestring : "";
}
static double num(cJSON *j, const char *k, double d)
{
    cJSON *v = cJSON_GetObjectItem(j, k);
    return cJSON_IsNumber(v) ? v->valuedouble : d;
}
static void send_json(cJSON *m)
{
    char *s = cJSON_PrintUnformatted(m);
    cJSON_Delete(m);
    if (!s)
        return;
    if (peer >= 0) {
        size_t n = strlen(s);
        char *line = malloc(n + 1);
        if (line) {
            memcpy(line, s, n);
            line[n] = '\n';
            if (send(peer, line, n + 1, 0) != (ssize_t)(n + 1)) {
                close(peer);
                peer = -1;
            }
            free(line);
        }
    }
    free(s);
}
bool coop_pc_link_start(void)
{
    const char *port = getenv("COOP_SIM_PORT");
    if (!port)
        return false;
    signal(SIGPIPE, SIG_IGN);
    if (coop_script_create(&script) != ESP_OK)
        return false;
    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0)
        return false;
    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in a = {.sin_family = AF_INET,
                            .sin_port = htons(atoi(port)),
                            .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    if (bind(listener, (struct sockaddr *)&a, sizeof(a)) || listen(listener, 1)) {
        close(listener);
        listener = -1;
        return false;
    }
    fcntl(listener, F_SETFL, O_NONBLOCK);
    buffer = malloc(32768);
    return buffer != NULL;
}
void coop_pc_link_event(const coop_event_t *e)
{
    if (peer < 0 || !strcmp(e->type, "persist") || !strcmp(e->type, "say") ||
        !strcmp(e->type, "audio_stop") || !strcmp(e->type, "vibrate"))
        return;
    cJSON *m = cJSON_CreateObject();
    const char *type = !strncmp(e->type, "report_", 7) ? "transfer_report" : e->type;
    cJSON_AddStringToObject(m, "t", type);
    cJSON_AddNumberToObject(m, "at", e->at);
    cJSON_AddNumberToObject(m, "epoch", e->epoch);
    cJSON_AddNumberToObject(m, "utterance", e->utterance);
    if (!strncmp(type, "transfer_", 9) || !strcmp(type, "glow_started"))
        cJSON_AddStringToObject(m, "id", e->text ? e->text : "");
    if (!strcmp(type, "transfer_report"))
        cJSON_AddBoolToObject(m, "hidden", !strcmp(e->type, "report_hidden"));
    if (!strcmp(type, "touch"))
        cJSON_AddStringToObject(m, "kind", e->text ? e->text : "");
    if (!strcmp(type, "answer") || !strcmp(type, "confirmed")) {
        cJSON_AddStringToObject(m, !strcmp(type, "answer") ? "askId" : "id", e->text);
        cJSON_AddNumberToObject(m, "index", e->epoch);
    }
    send_json(m);
}
static void command(coop_ui_handle_t ui, cJSON *m, uint64_t now)
{
    coop_state_handle_t s = coop_ui_state(ui);
    const char *t = str(m, "t");
    if (!strcmp(t, "clock_ping")) {
        cJSON *r = cJSON_CreateObject();
        cJSON_AddStringToObject(r, "t", "clock_pong");
        cJSON_AddNumberToObject(r, "echo", num(m, "at", 0));
        cJSON_AddNumberToObject(r, "at", now);
        send_json(r);
    } else if (!strcmp(t, "clock_quality")) {
        bool ready = cJSON_IsTrue(cJSON_GetObjectItem(m, "ready"));
        coop_state_connection(s, ready);
        if (ready && !capabilities_sent) {
            cJSON *caps = cJSON_CreateObject();
            cJSON_AddStringToObject(caps, "t", "capabilities");
            cJSON_AddStringToObject(caps, "app", "mosaico-coopanion/1.1.0-simulator");
            cJSON_AddNumberToObject(caps, "width", 480);
            cJSON_AddNumberToObject(caps, "height", 480);
            cJSON_AddItemToObject(caps, "assetFormats", cJSON_Parse("[\"COO1\",\"COO2\"]"));
            cJSON_AddNumberToObject(caps, "assetMaxBytes", 1000 * 1024);
            send_json(caps);
            capabilities_sent = true;
        }
    }
    else if (!strcmp(t, "presence"))
        coop_state_presence(s, !strcmp(str(m, "owner"), "device"), num(m, "epoch", 0),
                            cJSON_IsObject(cJSON_GetObjectItem(m, "transfer")));
    else if (!strncmp(t, "transfer_", 9)) {
        cJSON *tr = !strcmp(t, "transfer_status") ? cJSON_GetObjectItem(m, "transfer") : m;
        if (!strcmp(t, "transfer_arrive"))
            coop_state_emotion(s, str(m, "emotion"));
        coop_state_transfer(s, t, str(tr, "id"), num(tr, "epoch", 0), num(m, "glowAt", now), now);
    } else if (!strcmp(t, "sim_button"))
        coop_state_button(s, cJSON_IsTrue(cJSON_GetObjectItem(m, "down")), now);
    else if (!strcmp(t, "sim_imu")) {
        imu[0]=num(m,"ax",0);imu[1]=num(m,"ay",0);imu[2]=num(m,"az",1);
        imu[3]=num(m,"gx",0);imu[4]=num(m,"gy",0);imu[5]=num(m,"gz",0);
    } else if (!strcmp(t,"sim_state")) {
        const coop_snapshot_t *v=coop_state_get(s);cJSON *r=cJSON_CreateObject();
        cJSON_AddStringToObject(r,"t","sim_state");cJSON_AddStringToObject(r,"subtitle",v->subtitle);
        cJSON_AddNumberToObject(r,"motion",v->motion);cJSON_AddNumberToObject(r,"edge",v->edge);
        cJSON_AddNumberToObject(r,"orientation",v->orientation);cJSON_AddNumberToObject(r,"at",now);
        cJSON_AddBoolToObject(r,"muted",v->muted);cJSON_AddBoolToObject(r,"visible",v->visible);
        cJSON_AddBoolToObject(r,"connected",v->connected);cJSON_AddBoolToObject(r,"listening",v->listening);
        cJSON_AddNumberToObject(r,"x",v->x);cJSON_AddNumberToObject(r,"y",v->y);
        cJSON_AddNumberToObject(r,"scale_x",v->scale_x);cJSON_AddNumberToObject(r,"scale_y",v->scale_y);
        cJSON_AddStringToObject(r,"expression",v->expression);cJSON_AddNumberToObject(r,"phase",v->phase);
        cJSON_AddNumberToObject(r,"glow",v->glow);
        cJSON_AddBoolToObject(r,"busy",coop_state_animation_busy(s));send_json(r);
    } else if (!strcmp(t,"sim_battery"))
        coop_state_battery(s,num(m,"percent",100),false);
    else if (!strcmp(t, "sim_action"))
        coop_state_action(s, str(m, "action"), now);
    else if (!strcmp(t, "sim_touch"))
        coop_state_touch(s, true, now);
    else if (!strcmp(t, "sim_offline")) {
        coop_state_connection(s, false);
        capabilities_sent = false;
    }
    else if (!strcmp(t, "asset_apply")) {
        const char *path = cJSON_IsTrue(cJSON_GetObjectItem(m,"builtin")) ? COOP_ATLAS_PATH : getenv("COOP_SIM_ASSET_PATH");
        bool ok = false;
        FILE *f = path ? fopen(path, "rb") : NULL;
        if (f) {
            fseek(f, 0, SEEK_END);
            long n = ftell(f);
            rewind(f);
            uint8_t *data = n >= 188 && n <= 1000 * 1024 ? malloc(n) : NULL;
            if (data && fread(data, 1, n, f) == (size_t)n && coop_ui_atlas(ui, data, n)) {
                free(cached);
                cached = data;
                ok = true;
            } else
                free(data);
            fclose(f);
        }
        cJSON *r = cJSON_CreateObject();
        cJSON_AddStringToObject(r, "t", ok ? "asset_ready" : "asset_error");
        cJSON_AddStringToObject(r, "id", str(m, "id"));
        cJSON_AddStringToObject(r, "sha256", str(m, "sha256"));
        send_json(r);
    } else if (!strcmp(t, "pet_command")) {
        cJSON *c = cJSON_GetObjectItem(m, "command");
        const char *kind = str(c, "t");
        if (!strcmp(kind, "act") || !strcmp(kind, "say")) {
            coop_script_push(script, c);
        } else if (!strcmp(kind, "walk"))
            coop_state_walk(s, num(c, "to", .5), cJSON_IsTrue(cJSON_GetObjectItem(c, "run")), now);
        else if (!strcmp(kind, "listen"))
            coop_state_say(s, str(c, "text"), false, now);
    }
}
void coop_pc_link_poll(coop_ui_handle_t ui, uint64_t now)
{
    coop_state_imu(coop_ui_state(ui),imu[0],imu[1],imu[2],imu[3],imu[4],imu[5],now);
    coop_script_poll(script, coop_ui_state(ui), now, false);
    if (listener < 0)
        return;
    if (peer < 0) {
        peer = accept(listener, NULL, NULL);
        if (peer >= 0) {
            fcntl(peer, F_SETFL, O_NONBLOCK);
            used = 0;
            capabilities_sent = false;
        }
        return;
    }
    ssize_t n = recv(peer, buffer + used, 32767 - used, 0);
    if (n == 0) {
        close(peer);
        peer = -1;
        coop_state_connection(coop_ui_state(ui), false);
        return;
    }
    if (n < 0)
        return;
    used += n;
    buffer[used] = 0;
    size_t consumed = 0;
    for (char *end; (end = memchr(buffer + consumed, '\n', used - consumed));) {
        size_t length = end - (buffer + consumed);
        cJSON *m = cJSON_ParseWithLength(buffer + consumed, length);
        if (m) {
            command(ui, m, now);
            cJSON_Delete(m);
        }
        consumed += length + 1;
    }
    memmove(buffer, buffer + consumed, used - consumed);
    used -= consumed;
    if (used == 32767) {
        close(peer);
        peer = -1;
        used = 0;
    }
}
void coop_pc_link_stop(void)
{
    coop_script_delete(script);
    script = NULL;
    if (peer >= 0)
        close(peer);
    if (listener >= 0)
        close(listener);
    peer = listener = -1;
    free(buffer);
    buffer = NULL;
    free(cached);
    cached = NULL;
}
