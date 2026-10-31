// Built-in benchmarks: Video > Benchmark, and the start screen's BENCHMARK. Each is one of the test routes
// (runtime/hle/test_route.c) walked on a scratch copy of the player's save: the warp rewrites that copy's position as it is
// read, the save itself isn't opened, and the settings are a copy too with the frame rate limit and VSync off. The player's
// other settings apply, since those are what is being measured. Results go to benchmark/results.txt, the newest last.
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "benchmark.h"
#include "hle.h"
#include "platform.h"
#include "settings.h"

#define DIR_BENCH "benchmark"
#define MAX_RUN_NS (600 * 1000000000ull)  // (a stuck route ends here: the player may be away)

typedef struct { const char *name, *about, *warp, *steps; } Route;
static const Route routes[] = {
    {"Hateno Village", "A walk through the village: buildings, people and two full turns of the camera. 1 to 2 minutes.",
     "3449.7,227.7,2122.8",
     "face 3458.1 2124.1\nbench begin\ngoto 3458.1 2124.1 3\ngoto 3471.6 2132.9 3\ngoto 3482.2 2142.8 3\ngoto 3492.6 2153.0 3\n"
     "goto 3504.8 2161.4 3\ngoto 3523.0 2169.0 3\ngoto 3541.2 2175.6 3\ngoto 3553.6 2172.9 3\nspin 1 1\nface 3566.0 2170.2\n"
     "goto 3567.5 2157.1 3\ngoto 3585.3 2119.4 3\ngoto 3587.9 2102.5 3\ngoto 3590.6 2092.3 3\ngoto 3599.3 2100.1 3\n"
     "goto 3605.2 2116.8 3\ngoto 3610.4 2132.3 3\nspin 1 -1\nface 3615.6 2147.8\n"},
    {"Kakariko Village", "A walk through the village and out the far side, with two full turns of the camera. 1 to 2 minutes.",
     "1809.8,219.8,983.1",
     "face 1817.1 978.8\nbench begin\ngoto 1817.1 978.8 3\ngoto 1834.4 970.9 3\ngoto 1849.6 965.8 3\ngoto 1868.5 960.9 3\n"
     "goto 1885.9 958.2 3\ngoto 1883.3 943.2 3\ngoto 1870.1 933.4 3\ngoto 1861.1 929.4 3\nspin 1 1\nface 1852.1 925.3\n"
     "goto 1845.3 925.0 3\ngoto 1838.2 913.2 3\ngoto 1844.4 905.1 3\ngoto 1852.8 902.3 3\ngoto 1870.1 902.0 3\n"
     "goto 1883.3 902.0 3\ngoto 1894.0 903.1 3\ngoto 1905.9 904.8 3\ngoto 1923.6 911.3 3\nspin 1 -1\nface 1941.4 917.8\n"
     "goto 1940.0 911.8 3\ngoto 1946.5 926.5 3\ngoto 1957.5 942.2 3\ngoto 1965.8 950.8 3\ngoto 1975.8 954.9 3\n"
     "goto 1992.7 958.0 3\ngoto 2007.0 960.1 3\n"},
    {"Open field", "A long run down a road across open country, the camera circling for part of it: distance and grass. "
     "2 to 3 minutes.",
     "353.9,132.0,-427.7",
     "face 366.1 -428.9\nbench begin\ngoto 366.1 -428.9 3\ngoto 381.6 -434.1 3\ngoto 421.8 -452.6 3\ngoto 472.2 -475.2 3\n"
     "goto 499.2 -488.3 3\ngoto 512.8 -494.9 3\ngoto 547.0 -511.4 3\ngoto 567.5 -521.1 3\ngoto 580.9 -527.3 3\n"
     "goto 616.9 -541.5 3 0.7\ngoto 626.3 -540.1 3 0.7\ngoto 637.5 -532.1 3 0.7\ngoto 645.4 -524.7 3 0.7\n"
     "goto 667.7 -510.7 3 0.7\ngoto 695.8 -517.0 3 0.7\ngoto 713.4 -519.2 3 0.7\ngoto 725.3 -523.5 3 0.7\n"
     "goto 746.8 -539.7 3 0.7\ngoto 759.8 -541.8 3\ngoto 775.0 -537.3 3\ngoto 803.4 -519.3 3\ngoto 834.2 -500.9 3\n"
     "goto 855.1 -490.6 3\ngoto 877.6 -485.7 3\ngoto 913.8 -484.9 3\ngoto 939.0 -488.0 3\ngoto 969.5 -487.6 3\n"
     "goto 1001.0 -490.0 3\n"},
};
#define ROUTES ((int)(sizeof routes / sizeof routes[0]))

int benchmark_count(void) { return ROUTES; }
const char *benchmark_name(int r) { return r >= 0 && r < ROUTES ? routes[r].name : ""; }
const char *benchmark_about(int r) { return r >= 0 && r < ROUTES ? routes[r].about : ""; }

static void set_env(const char *name, const char *value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

// ------------------------------------------------------------------ results
// A line: <date> <time>|<route>|<fps>|<1% low>|<0.1% low>|<worst ms>|<frames>|<finished 0/1>|<the settings>
const char *benchmark_last(int r) {
    static char last[ROUTES][96];
    static bool read;
    if (!read) {
        read = true;
        FILE *f = fopen(DIR_BENCH "/results.txt", "r");
        char line[512];
        while (f && fgets(line, sizeof line, f)) {
            char *route = strchr(line, '|');
            if (!route) continue;
            route++;
            char *fields = strchr(route, '|');
            if (!fields) continue;
            *fields++ = 0;
            int fps, low1, low01, frames, done;
            float worst;
            if (sscanf(fields, "%d|%d|%d|%f|%d|%d", &fps, &low1, &low01, &worst, &frames, &done) != 6) continue;
            for (int i = 0; i < ROUTES; i++)
                if (!strcmp(route, routes[i].name)) {
                    if (done) snprintf(last[i], sizeof last[i], "%d fps, 1%% %d, 0.1%% %d", fps, low1, low01);
                    else snprintf(last[i], sizeof last[i], "Didn't finish");
                }
        }
        if (f) fclose(f);
    }
    return r >= 0 && r < ROUTES ? last[r] : "";
}

// ------------------------------------------------------------------ starting one
// Any save will do: the warp puts Link where the route starts.
static SDL_EnumerationResult SDLCALL find_save(void *found, const char *dir, const char *name) {
    char p[1024];
    snprintf(p, sizeof p, "%s%s", dir, name);
    SDL_PathInfo in;
    if (!SDL_GetPathInfo(p, &in)) return SDL_ENUM_CONTINUE;
    if (in.type == SDL_PATHTYPE_DIRECTORY) SDL_EnumerateDirectory(p, find_save, found);
    else if (!strcmp(name, "game_data.sav")) *(bool *)found = true;
    return *(bool *)found ? SDL_ENUM_SUCCESS : SDL_ENUM_CONTINUE;
}

bool benchmark_can_run(void) {
    bool found = false;
    SDL_EnumerateDirectory("save", find_save, &found);
    return found;
}

static void start_self(const char *arg) {
    static char exe[1024];
    const char *base = SDL_GetBasePath();
#ifdef _WIN32
    snprintf(exe, sizeof exe, "%ssavagewind.exe", base ? base : "");
#else
    snprintf(exe, sizeof exe, "%ssavagewind", base ? base : "");
#endif
    char *argv[] = {exe, (char *)arg, NULL};
    plat_exec(argv);
    LOG("[benchmark] couldn't start %s\n", exe);
}

void benchmark_run(int route) {
    if (route < 0 || route >= ROUTES || !benchmark_can_run()) return;
    settings_save();
    char n[16];
    snprintf(n, sizeof n, "%d", route);
    set_env("SAVAGEWIND_BENCHMARK", n);
    start_self(NULL);
}

// ------------------------------------------------------------------ the benchmark's own process
static int g_route = -1;
static float *g_ms;
static size_t g_n, g_cap;
static bool g_recording, g_written;
static uint64_t g_last_ns, g_start_ns;

bool benchmark_on(void) { return g_route >= 0; }

static SDL_EnumerationResult SDLCALL copy_tree(void *to_dir, const char *dir, const char *name) {
    char from[1024], to[1024];
    snprintf(from, sizeof from, "%s%s", dir, name);
    snprintf(to, sizeof to, "%s/%s", (const char *)to_dir, name);
    SDL_PathInfo in;
    if (!SDL_GetPathInfo(from, &in)) return SDL_ENUM_CONTINUE;
    if (in.type == SDL_PATHTYPE_DIRECTORY) {
        SDL_CreateDirectory(to);
        SDL_EnumerateDirectory(from, copy_tree, to);
    } else SDL_CopyFile(from, to);
    return SDL_ENUM_CONTINUE;
}

static SDL_EnumerationResult SDLCALL remove_tree(void *unused, const char *dir, const char *name) {
    char p[1024];
    snprintf(p, sizeof p, "%s%s", dir, name);
    SDL_PathInfo in;
    if (SDL_GetPathInfo(p, &in) && in.type == SDL_PATHTYPE_DIRECTORY) SDL_EnumerateDirectory(p, remove_tree, unused);
    SDL_RemovePath(p);
    return SDL_ENUM_CONTINUE;
}

void benchmark_start_up(void) {
    const char *e = getenv("SAVAGEWIND_BENCHMARK");
    if (!e) return;
    int r = atoi(e);
    if (r < 0 || r >= ROUTES) return;
    g_route = r;
    g_start_ns = SDL_GetTicksNS();
    SDL_CreateDirectory(DIR_BENCH);
    SDL_EnumerateDirectory(DIR_BENCH "/save", remove_tree, NULL);
    SDL_CreateDirectory(DIR_BENCH "/save");
    SDL_EnumerateDirectory("save", copy_tree, (void *)DIR_BENCH "/save");
    SDL_CopyFile("savagewind.ini", DIR_BENCH "/settings.ini");
    FILE *f = fopen(DIR_BENCH "/route.txt", "w");
    if (f) {
        fprintf(f, "play\nhold 3\n%sbench end\nquit\n", routes[r].steps);
        fclose(f);
    }
    set_env("SAVAGEWIND_SAVE_DIR", DIR_BENCH "/save");
    set_env("SAVAGEWIND_SETTINGS", DIR_BENCH "/settings.ini");
    set_env("SAVAGEWIND_TEST_ROUTE", DIR_BENCH "/route.txt");
    set_env("SAVAGEWIND_HOST_INPUT", "0:key:F24:1");
    set_env("SAVAGEWIND_TEST_WARP", routes[r].warp);
    set_env("SAVAGEWIND_TEST_TIME", "12");
    set_env("SAVAGEWIND_TEST_WEATHER", "0");
    set_env("SAVAGEWIND_FPS", "0");
    set_env("SAVAGEWIND_LAUNCHER_AUTOPLAY", "1");
}

void benchmark_begin(void) {
    g_n = 0;
    g_last_ns = 0;
    g_recording = true;
}

void benchmark_frame(uint64_t ns) {
    if (g_route < 0) return;
    if (ns - g_start_ns > MAX_RUN_NS && !g_written) {
        LOG("[benchmark] ran too long: ending it\n");
        benchmark_finish();
    }
    if (!g_recording) return;
    if (g_last_ns) {
        if (g_n == g_cap) {
            size_t cap = g_cap ? g_cap * 2 : 1 << 16;
            float *m = realloc(g_ms, cap * sizeof *m);
            if (!m) { g_recording = false; return; }
            g_ms = m, g_cap = cap;
        }
        g_ms[g_n++] = (float)((ns - g_last_ns) / 1e6);
    }
    g_last_ns = ns;
}

static int cmp_float(const void *a, const void *b) {
    float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}

static void write_result(bool finished) {
    if (g_written) return;
    g_written = true;
    g_recording = false;
    int fps = 0, low1 = 0, low01 = 0;
    float worst = 0;
    if (g_n) {
        double sum = 0;
        for (size_t i = 0; i < g_n; i++) sum += g_ms[i];
        qsort(g_ms, g_n, sizeof *g_ms, cmp_float);
        fps = (int)(1000.0 * g_n / sum + 0.5);
        low1 = (int)(1000.0 / g_ms[(size_t)(g_n * 0.99)] + 0.5);
        low01 = (int)(1000.0 / g_ms[(size_t)(g_n * 0.999)] + 0.5);
        worst = g_ms[g_n - 1];
    }
    char when[32] = "";
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    if (tm) strftime(when, sizeof when, "%Y-%m-%d %H:%M", tm);
    FILE *f = fopen(DIR_BENCH "/results.txt", "a");
    if (f) {
        fprintf(f, "%s|%s|%d|%d|%d|%.1f|%zu|%d|preset %d aa %d scale %d aniso %d shadow %d lod %d/%d/%d/%d\n", when,
                routes[g_route].name, fps, low1, low01, worst, g_n, finished, settings_preset_current(), g_settings.antialias,
                g_settings.render_scale, g_settings.anisotropy, g_settings.shadow_res, g_settings.lod_extended,
                g_settings.lod_distance, g_settings.lod_grass, g_settings.lod_objects);
        fclose(f);
    }
    LOG("[benchmark] %s: %d fps, 1%% %d, 0.1%% %d, worst %.1f ms, %zu frames%s\n", routes[g_route].name, fps, low1, low01,
        worst, g_n, finished ? "" : " (didn't finish)");
}

void benchmark_end(void) { write_result(true); }

void benchmark_finish(void) {
    if (g_route < 0) return;
    write_result(false);
    fflush(stderr);
    const char *back_shot = getenv("SAVAGEWIND_BENCHMARK_RETURN_SHOT");  // (test: the way back, and a picture of the page it lands on)
    if (getenv("SAVAGEWIND_HIDDEN") && !back_shot) _exit(0);  // (a test run, no one to show it to)
    static const char *const vars[] = {"SAVAGEWIND_BENCHMARK", "SAVAGEWIND_SAVE_DIR", "SAVAGEWIND_SETTINGS", "SAVAGEWIND_TEST_ROUTE",
                                       "SAVAGEWIND_HOST_INPUT", "SAVAGEWIND_TEST_WARP", "SAVAGEWIND_TEST_TIME",
                                       "SAVAGEWIND_TEST_WEATHER", "SAVAGEWIND_FPS", "SAVAGEWIND_LAUNCHER_AUTOPLAY"};
    for (size_t i = 0; i < sizeof vars / sizeof vars[0]; i++) set_env(vars[i], NULL);
    set_env("SAVAGEWIND_LAUNCHER_PAGE", "benchmark");
    if (back_shot) {
        set_env("SAVAGEWIND_SHOT_PATH", back_shot);
        set_env("SAVAGEWIND_LAUNCHER_SHOT", "20");
    }
    start_self("--config");
    _exit(0);
}
