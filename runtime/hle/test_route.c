// (tests, and the built-in benchmarks: runtime/benchmark.c) A test run that looks where it's going. SAVAGEWIND_TEST_ROUTE=<file> (with
// SAVAGEWIND_HOST_INPUT set, which keeps the player's own controller out of it): steps, one a line, read while the game
// runs, so lines added later come next (a live session: add a step, read the log, look at a shot). Each frame the sticks
// are worked out from where Link is (uking::act::PlayerInfo +0x88) and where the world camera looks (vulkan/exec.c
// route_camera), not fixed values on a timer, so a route doesn't depend on the way he happened to load in facing.
//
//   play [1]                  press A through the title screen, until Link can move (the load and any event over), and B
//                             at the DLC screen that opens over it; with 1 (a game with DLC), no A until that has opened
//   goto <x> <z> [m] [spin]   run to world x, z, within m meters (default 4), with the camera behind him, or orbiting
//                             him at spin (-1..1 of full tilt, the left stick still towards the point). A point he
//                             makes no headway to is given up after a jump and a few seconds (logged).
//   face <x> <z>              turn the camera to that point, smoothly
//   turn <degrees>            turn the camera by that much (+: to the right)
//   spin <turns> [speed]      turn the camera that many turns, measured, at speed (default 1: full tilt; -: left)
//   stick <lx> <ly> <rx> <ry> <seconds>   the sticks at these (-1..1), as they are
//   press <button> [seconds]  hold a button (names as in savagewind.ini's bind_* lines: east, south, west, north,
//                             lshoulder, ltrigger...); default a tenth of a second
//   fight <seconds>           lock on (left trigger), walk in, attack (west) twice a second
//   hold <seconds>            stand still
//   sprint <seconds>          run ahead, holding the sprint button
//   shadow <0-3>              Video > Shadow resolution, now
//   camspeed <0|1> <x %> <y %>   Controls > Controller camera speed and its sliders, now
//   lod <0|1> <distance %> <grass %> [objects %]   Video > Extended draw distance and its sliders, now
//   poke <address> <float>    a float of the game's, now (to find what a value does; the log has what it was)
//   pokep <address> <offset> <float>   the same, at an offset from the pointer stored at address
//   shot <name>               the window to build/<name>.png
//   log <text>                a mark in the log
//   bench begin|end           the frame times written down from here to there (runtime/benchmark.c)
//   quit                      end the run
//
// While an event runs (a talk, a cutscene) a step waits, pressing A now and then. The log has a "[route]" line a second:
// the step, Link's position, the camera's heading (0: +z, 90: +x) and pitch.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../benchmark.h"
#include "../framepace.h"
#include "../game_sites.h"
#include "../hle.h"
#include "../input_bind.h"
#include "../platform.h"
#include "../settings.h"
#ifdef _WIN32
#include <windows.h>
#endif

#define PI 3.14159265358979323846
#define MAX_STEPS 1024
#define SECONDS 1000000000ull

typedef struct { char op[12], name[64]; double v[5]; int n; } Step;

volatile int g_test_route_on;  // (vulkan/exec.c: the world camera is wanted)
static Step steps[MAX_STEPS];
static int nsteps, cur;
static long read_at;
static const char *path;
static float cam[12];  // the world camera's matrix: right, up, back, position in its columns
static uint64_t cam_ns;

// (the render thread, like test_route_frame: vulkan/exec.c route_camera) f: the frame's camera block
void test_route_camera(const float *f) {
    memcpy(cam, f + 44, sizeof cam);
    cam_ns = plat_now_ns();
}

const char *test_route_start(void) {
    path = getenv("SAVAGEWIND_TEST_ROUTE");
    g_test_route_on = path != NULL;
    return path;
}

static void read_steps(void) {  // (only whole lines: one being added is read when it's there)
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, read_at, SEEK_SET);
    char line[256];
    while (nsteps < MAX_STEPS && fgets(line, sizeof line, f) && strchr(line, '\n')) {
        read_at = ftell(f);
        Step s = {{0}};
        int used = 0;
        if (sscanf(line, "%11s%n", s.op, &used) != 1 || s.op[0] == '#') continue;
        if (!strcmp(s.op, "press") || !strcmp(s.op, "shot") || !strcmp(s.op, "bench")) {
            int u2 = 0;
            sscanf(line + used, "%63s%n", s.name, &u2);
            used += u2;
        } else if (!strcmp(s.op, "log")) {
            snprintf(s.name, sizeof s.name, "%.63s", line + used + (line[used] == ' '));
            s.name[strcspn(s.name, "\r\n")] = 0;
            used = (int)strlen(line);
        }
        for (const char *p = line + used; s.n < 5;) {
            char *end;
            double d = strtod(p, &end);
            if (end == p) break;
            s.v[s.n++] = d;
            p = end;
        }
        steps[nsteps++] = s;
    }
    fclose(f);
}

static bool link_pos(float p[3]) {
    uint64_t pinfo = g_site.player_info ? *(uint64_t *)(uintptr_t)g_site.player_info : 0;
    if (!pinfo) return false;
    memcpy(p, (const void *)(uintptr_t)(pinfo + 0x88), 12);
    return true;
}

// The camera on the ground: forward and right, as unit vectors (x, z). false: no world drawn lately (a menu, a load).
static bool camera_ground(double f[2], double r[2]) {
    if (!cam_ns || plat_now_ns() - cam_ns > SECONDS / 2) return false;
    double a = -cam[2], b = -cam[10], c = cam[0], d = cam[8], n = hypot(a, b), m = hypot(c, d);
    if (n < 1e-4 || m < 1e-4) return false;
    f[0] = a / n, f[1] = b / n, r[0] = c / m, r[1] = d / m;
    return true;
}
static double camera_pitch(void) {
    double y = -cam[6];
    return asin(y > 1 ? 1 : y < -1 ? -1 : y) * 180 / PI;
}

static int32_t axis(double v) { return (int32_t)((v > 1 ? 1 : v < -1 ? -1 : v) * 32767); }

// The right stick that brings the camera round by err radians (+: to the right), slowing down as it gets there
static int32_t yaw_stick(double err) {
    double deg = err * 180 / PI;
    if (fabs(deg) < 2) return 0;
    double v = deg / 40;
    if (fabs(v) < 0.25) v = copysign(0.25, v);  // (the game's dead zone)
    return axis(v);
}

static uint32_t button(const char *name) {
    int h = bind_pad_from_name(name);
    return h >= 0 ? 1u << h : 0;
}

void test_route_frame(int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry, uint32_t *pad, const char **shot) {
    static uint64_t next_read, next_log, step_t0, paused_from, best_t;
    static int phase, started = -1;
    static double best, acc, pf[2], pr[2], target[2];
    static bool in_event;
    uint64_t now = plat_now_ns();
    if (now >= next_read) read_steps(), next_read = now + SECONDS / 10;
    float p[3] = {0};
    bool have_pos = link_pos(p);
    double f[2], r[2];
    bool have_cam = camera_ground(f, r);
    const Step *s = cur < nsteps ? &steps[cur] : NULL;
    if (now >= next_log) {
        next_log = now + SECONDS;
        LOG("[route] step %d/%d %s pos %.1f %.1f %.1f cam %s heading %.0f pitch %.0f%s\n", cur + 1, nsteps, s ? s->op : "-", p[0], p[1],
            p[2], have_cam ? "on" : "off", have_cam ? atan2(f[0], f[1]) * 180 / PI : 0.0, have_cam ? camera_pitch() : 0.0,
            framepace_in_event() ? " (event)" : "");
    }
    {   // the game's screens, when they change
        extern int framepace_screens(uint32_t *ids, int max);
        static uint32_t last[64];
        static int nlast;
        uint32_t ids[64];
        int n = framepace_screens(ids, 64);
        if (n != nlast || memcmp(ids, last, n * sizeof ids[0])) {
            char line[512];
            int o = 0;
            for (int i = 0; i < n && o < 480; i++) o += snprintf(line + o, sizeof line - o, " %x", ids[i]);
            line[o] = 0;
            LOG("[route] screens:%s\n", line);
            memcpy(last, ids, n * sizeof ids[0]);
            nlast = n;
        }
    }
    if (!s) return;  // (the end for now: standing still until more lines come)
    if (started != cur) {
        started = cur, step_t0 = now, phase = 0, best = 1e30, best_t = now, acc = 0, pf[0] = pf[1] = 0, target[0] = target[1] = 0;
        if (have_cam) memcpy(pf, f, sizeof pf), memcpy(pr, r, sizeof pr);
        if (!strcmp(s->op, "turn") && have_cam) {
            double a = s->v[0] * PI / 180;
            target[0] = f[0] * cos(a) + r[0] * sin(a), target[1] = f[1] * cos(a) + r[1] * sin(a);
        }
    }
    bool is_play = !strcmp(s->op, "play");
    static bool dlc_seen;
    if (!is_play) {  // an event: everything waits, its time doesn't count
        bool ev = framepace_in_event();
        if (ev != in_event) {
            in_event = ev;
            LOG("[route] event %s at %.1f %.1f %.1f\n", ev ? "starts" : "over", p[0], p[1], p[2]);
            if (ev) paused_from = now;
            else step_t0 += now - paused_from, best_t = now;
        }
        if (ev) {
            if (now % SECONDS < SECONDS / 10) *pad |= button("east");
            return;
        }
    }
#define NEXT() do { cur++; return; } while (0)
    double t = (double)(now - step_t0) / 1e9;
    if (is_play) {
        if (phase == 0 && framepace_title_ui()) phase = 1, LOG("[route] the title screen is up\n");
        if (phase == 1) {
            if (!framepace_title_ui()) phase = 2, best_t = now;
            else if (framepace_dlc_ui()) {  // (it opens by itself; only B closes it)
                dlc_seen = true;
                if (now % (SECONDS * 3 / 2) < SECONDS / 8) *pad |= button("south");
            }
            else if (s->n > 0 && !dlc_seen && t < 60) {}  // (A before it opens would be lost under it)
            else if (now % (SECONDS * 6 / 5) < SECONDS / 8) *pad |= button("east");
        }
        if (phase == 2) {
            bool ok = !framepace_title_ui() && !framepace_loading_ui() && !framepace_in_event() && have_pos && have_cam;
            if (!ok) best_t = now;
            if (now - best_t > SECONDS * 3 / 2) {
                LOG("[route] Link can move, %.1f s after the step began, at %.1f %.1f %.1f, heading %.0f\n", t, p[0], p[1], p[2], atan2(f[0], f[1]) * 180 / PI);
                NEXT();
            }
            if (framepace_in_event() && now % SECONDS < SECONDS / 10) *pad |= button("east");
        }
        if (t > 240) { LOG("[route] play: given up after %.0f s (phase %d)\n", t, phase); NEXT(); }
        return;
    }
    if (!strcmp(s->op, "goto")) {
        if (!have_pos) return;
        double dx = s->v[0] - p[0], dz = s->v[1] - p[2], d = hypot(dx, dz), tol = s->n > 2 && s->v[2] > 0 ? s->v[2] : 4;
        if (d < tol) {
            LOG("[route] reached %.0f %.0f in %.1f s (%.1f %.1f %.1f)\n", s->v[0], s->v[1], t, p[0], p[1], p[2]);
            NEXT();
        }
        if (d < best - 0.5) best = d, best_t = now;
        double stuck = (double)(now - best_t) / 1e9;
        if (stuck > 6) {
            LOG("[route] STUCK short of %.0f %.0f by %.1f m after %.1f s, at %.1f %.1f %.1f: on to the next step\n", s->v[0], s->v[1], d, t, p[0], p[1], p[2]);
            NEXT();
        }
        if (stuck > 2.5 && stuck < 2.7) *pad |= button("north");  // (a jump: over a low edge)
        if (!have_cam) return;
        dx /= d, dz /= d;
        double df = dx * f[0] + dz * f[1], dr = dx * r[0] + dz * r[1];
        *lx = axis(dr * 0.92), *ly = axis(df * 0.92);
        *rx = s->n > 3 ? axis(s->v[3]) : yaw_stick(atan2(dr, df));
        return;
    }
    if (!strcmp(s->op, "face") || !strcmp(s->op, "turn")) {
        if (!have_cam) { if (t > 10) NEXT(); return; }
        double dx = target[0], dz = target[1];
        if (s->op[0] == 'f') dx = s->v[0] - p[0], dz = s->v[1] - p[2];
        double err = atan2(dx * r[0] + dz * r[1], dx * f[0] + dz * f[1]);
        *rx = yaw_stick(err);
        if ((fabs(err) < 3 * PI / 180 && t > 0.2) || t > 8) {
            LOG("[route] %s done in %.1f s, %.0f degrees off\n", s->op, t, fabs(err) * 180 / PI);
            NEXT();
        }
        return;
    }
    if (!strcmp(s->op, "spin")) {
        double speed = s->n > 1 ? s->v[1] : 1, turns = s->n > 0 ? s->v[0] : 1;
        if (have_cam && (pf[0] || pf[1])) acc += atan2(f[0] * pr[0] + f[1] * pr[1], f[0] * pf[0] + f[1] * pf[1]);
        if (have_cam) memcpy(pf, f, sizeof pf), memcpy(pr, r, sizeof pr);
        *rx = axis(speed);
        if (fabs(acc) >= fabs(turns) * 2 * PI || t > 60) {
            LOG("[route] spin done: %.0f degrees in %.1f s\n", acc * 180 / PI, t);
            *rx = 0;
            NEXT();
        }
        return;
    }
    if (!strcmp(s->op, "stick")) {
        *lx = axis(s->v[0]), *ly = axis(s->v[1]), *rx = axis(s->v[2]), *ry = axis(s->v[3]);
        if (t >= s->v[4]) NEXT();
        return;
    }
    if (!strcmp(s->op, "press")) {
        *pad |= button(s->name);
        if (t >= (s->n > 0 ? s->v[0] : 0.1)) NEXT();
        return;
    }
    if (!strcmp(s->op, "sprint")) {
        *ly = axis(1);
        *pad |= button("south");
        if (t >= s->v[0]) NEXT();
        return;
    }
    if (!strcmp(s->op, "fight")) {
        *pad |= button("ltrigger");
        *ly = axis(0.6);
        if ((now - step_t0) % (SECONDS / 2) < SECONDS / 10) *pad |= button("west");
        if (t >= s->v[0]) NEXT();
        return;
    }
    if (!strcmp(s->op, "shadow")) {
        g_settings.shadow_res = (int)s->v[0];
        LOG("[route] shadow resolution %d\n", g_settings.shadow_res);
        NEXT();
    }
    if (!strcmp(s->op, "camspeed")) {
        g_settings.stick_cam_on = s->v[0] != 0, g_settings.stick_cam_x = (int)s->v[1], g_settings.stick_cam_y = (int)s->v[2];
        LOG("[route] controller camera speed %d: %d%%, %d%%\n", g_settings.stick_cam_on, g_settings.stick_cam_x, g_settings.stick_cam_y);
        NEXT();
    }
    if (!strcmp(s->op, "lod")) {
        g_settings.lod_extended = s->v[0] != 0, g_settings.lod_distance = (int)s->v[1], g_settings.lod_grass = (int)s->v[2];
        if (s->n > 3) g_settings.lod_objects = (int)s->v[3];
        LOG("[route] extended draw distance %d: distance %d%%, grass %d%%\n", g_settings.lod_extended, g_settings.lod_distance, g_settings.lod_grass);
        NEXT();
    }
    if (!strcmp(s->op, "pokep")) {
        uint64_t base = *(uint64_t *)(uintptr_t)(uint64_t)s->v[0];
        float *a = (float *)(uintptr_t)(base + (uint64_t)s->v[1]);
        LOG("[route] pokep %llx -> %llx: %g -> %g\n", (unsigned long long)(uint64_t)s->v[0], (unsigned long long)base, base ? *a : 0.0f, s->v[2]);
        if (base) *a = (float)s->v[2];
        NEXT();
    }
    if (!strcmp(s->op, "poke")) {
        float *a = (float *)(uintptr_t)(uint64_t)s->v[0], v = (float)s->v[1];
#ifdef _WIN32
        DWORD old;
        VirtualProtect(a, sizeof v, PAGE_READWRITE, &old);
#endif
        LOG("[route] poke %llx: %g -> %g\n", (unsigned long long)(uintptr_t)a, *a, v);
        *a = v;
#ifdef _WIN32
        VirtualProtect(a, sizeof v, old, &old);
#endif
        NEXT();
    }
    if (!strcmp(s->op, "hold")) {
        if (t >= s->v[0]) NEXT();
        return;
    }
    if (!strcmp(s->op, "shot")) {
        *shot = s->name;
        NEXT();
    }
    if (!strcmp(s->op, "log")) {
        LOG("[route] %s (heading %.1f pitch %.1f)\n", s->name, have_cam ? atan2(f[0], f[1]) * 180 / PI : 0.0, have_cam ? camera_pitch() : 0.0);
        NEXT();
    }
    if (!strcmp(s->op, "bench")) {
        if (!strcmp(s->name, "begin")) benchmark_begin();
        else benchmark_end();
        NEXT();
    }
    if (!strcmp(s->op, "quit")) {
        LOG("[route] the end\n");
        fflush(stderr);
        benchmark_finish();
        _exit(0);
    }
    LOG("[route] unknown step \"%s\"\n", s->op);
    NEXT();
}
