// Photo mode (F6, or both sticks pressed in): the world stands nearly still, the HUD goes and the sticks fly the camera; F7 saves a picture in
// screenshots/. The camera moves only in what the draws read, the constant buffer of the world camera (vulkan/exec.c route_camera): the
// game still decides what to draw from where its own camera is, so turning far from it, or going far, shows the world cut off.
#include <SDL3/SDL.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "game_sites.h"
#include "hle.h"
#include "platform.h"

volatile int g_photo_on;

#define STICK 32767.0f
#define MOVE_MPS 8.0f       // (metres a second, full stick; held down: 5 times that)
#define TURN_RPS 1.4f       // (radians a second, full stick)
#define PITCH_MAX 1.45f

static _Atomic int32_t s_lx, s_ly, s_rx, s_ry;
static atomic_int s_up, s_down, s_fast, s_time;
static float yaw, pitch, off[3];
static uint64_t last_ns;

void photo_toggle(void) {
    g_photo_on = !g_photo_on;
    yaw = pitch = 0, off[0] = off[1] = off[2] = 0, last_ns = 0;
    LOG("[photo] %s\n", g_photo_on ? "on" : "off");
}

void photo_shot(void) {
    extern char g_window_shot_request[256];
    char when[32] = "";
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    if (tm) strftime(when, sizeof when, "%Y%m%d-%H%M%S", tm);
    SDL_CreateDirectory("screenshots");
    snprintf(g_window_shot_request, sizeof g_window_shot_request, "screenshots/slop-%s.png", when);
    LOG("[photo] %s\n", g_window_shot_request);
}

// (hle/input.c, each poll) both sticks pressed: on or off. While on the controls are the camera's, and the game gets none: true.
bool photo_take_input(int32_t lx, int32_t ly, int32_t rx, int32_t ry, bool up, bool down, bool fast, bool both_sticks, bool back, int time_dir) {
    static bool was;
    bool edge = both_sticks && !was;
    was = both_sticks;
    if (edge || (g_photo_on && back)) {
        if (edge || g_photo_on) photo_toggle();
        return true;
    }
    if (!g_photo_on) return false;
    s_lx = lx, s_ly = ly, s_rx = rx, s_ry = ry;
    s_up = up, s_down = down, s_fast = fast, s_time = time_dir;
    return true;
}

static void rot(float *a, float *b, float s, float c) {  // (a, b) turned in their plane
    float x = *a * c + *b * s;
    *b = *b * c - *a * s;
    *a = x;
}

// (vulkan/exec.c, once for each new block) f: the world camera's: the view (0-11, 3x4 rows), view-projection (12-27), projection (28-43) and
// the camera's own matrix (44-55: right, up, back, position in its columns).
void photo_camera(float *f) {
    uint64_t now = plat_now_ns();
    float dt = last_ns && now > last_ns ? (float)((now - last_ns) / 1e9) : 0;
    if (dt > 0.1f) dt = 0.1f;
    last_ns = now;
    if (s_time && g_site.world_mgr) {  // the d-pad: about 3 hours a second (ksys::world::Manager's TimeMgr, 0..360 = 24 h: test_warp.c)
        uint64_t wm = *(uint64_t *)(uintptr_t)g_site.world_mgr;
        uint64_t mgrs = wm && *(uint32_t *)(uintptr_t)(wm + 0x5c0) ? *(uint64_t *)(uintptr_t)(wm + 0x5c8) : 0, tm = mgrs ? *(uint64_t *)(uintptr_t)mgrs : 0;
        if (tm) {
            float *now_t = (float *)(uintptr_t)(tm + 0xb8);
            float v = *now_t + 45.0f * dt * s_time;
            *now_t = v >= 360 ? v - 360 : v < 0 ? v + 360 : v;
        }
    }
    float r[3] = {f[44], f[48], f[52]}, u[3] = {f[45], f[49], f[53]}, b[3] = {f[46], f[50], f[54]}, p[3] = {f[47], f[51], f[55]};
    yaw -= TURN_RPS * dt * s_rx / STICK;
    pitch += TURN_RPS * dt * s_ry / STICK;
    if (pitch > PITCH_MAX) pitch = PITCH_MAX;
    if (pitch < -PITCH_MAX) pitch = -PITCH_MAX;
    float ys = sinf(yaw), yc = cosf(yaw), ps = sinf(pitch), pc = cosf(pitch);
    // about the world's up
    rot(&r[0], &r[2], ys, yc), rot(&u[0], &u[2], ys, yc), rot(&b[0], &b[2], ys, yc);
    // about the camera's right
    for (int i = 0; i < 3; i++) {
        float ui = u[i], bi = b[i];
        u[i] = ui * pc + bi * ps;
        b[i] = bi * pc - ui * ps;
    }
    float speed = MOVE_MPS * (s_fast ? 5.0f : 1.0f) * dt;
    float side = speed * s_lx / STICK, ahead = speed * s_ly / STICK, rise = speed * ((int)s_up - (int)s_down);
    for (int i = 0; i < 3; i++) off[i] += r[i] * side - b[i] * ahead;
    off[1] += rise;
    for (int i = 0; i < 3; i++) p[i] += off[i];
    float v[12];
    for (int i = 0; i < 3; i++) {
        v[0 + i] = r[i], v[4 + i] = u[i], v[8 + i] = b[i];
        f[44 + 4 * i] = r[i], f[45 + 4 * i] = u[i], f[46 + 4 * i] = b[i], f[47 + 4 * i] = p[i];
    }
    v[3] = -(r[0] * p[0] + r[1] * p[1] + r[2] * p[2]);
    v[7] = -(u[0] * p[0] + u[1] * p[1] + u[2] * p[2]);
    v[11] = -(b[0] * p[0] + b[1] * p[1] + b[2] * p[2]);
    memcpy(f, v, sizeof v);
    for (int row = 0; row < 4; row++)
        for (int col = 0; col < 4; col++) {
            float x = 0;
            for (int k = 0; k < 3; k++) x += f[28 + row * 4 + k] * v[k * 4 + col];
            if (col == 3) x += f[28 + row * 4 + 3];
            f[12 + row * 4 + col] = x;
        }
}
