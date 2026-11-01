// Achievements from the game's flags in its memory, while it runs (ach_live_update): no waiting for the next save.
//
// The game keeps each game data flag as an object on its heap (ksys::gdt, made once as it starts): a bool flag is 0x18
// bytes - +0 its class (vtable, in main), +0xb its value (bit 0), +0xc the crc32 of its name (the save's hash) - and an
// s32 flag 0x28 bytes - +0 its class, +0xc its initial value, +0x18 its value, +0x1c the hash. The classes are found
// from two flags every version has, IsGet_PlayerStole2 (bool) and MaxHartValue (s32); then every object of those
// classes on the heap is a flag. They are found once (again if one stops looking like a flag) and read every
// LIVE_POLL_MS into a save image (hash, value), which the achievements read like a save.
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "achievements.h"
#include "game_sites.h"
#include "kernel.h"
#include "platform.h"
#include "rt.h"
#include "settings.h"

#define LIVE_POLL_MS 5000  // (at 500 a frame took 20 ms every half a second: no one needs a notice sooner than this)
#define RUPEE_HASH 0x23149bf8u  // crc32("CurrentRupee")
#define BOOL_HASH 0xfe4d1501u  // crc32("IsGet_PlayerStole2")
#define S32_HASH 0x2906f327u   // crc32("MaxHartValue")

typedef struct { uint64_t obj; uint32_t hash; bool s32; } LiveFlag;

static pthread_t thread;
static pthread_mutex_t llock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t lcond = PTHREAD_COND_INITIALIZER;
static bool running, stop_req;
static LiveFlag *flags;
static int n_flags;
static uint64_t vt_bool, vt_s32;

static uint32_t rd32(uint64_t a) { return *(volatile uint32_t *)(uintptr_t)a; }
static uint64_t rd64(uint64_t a) { return *(volatile uint64_t *)(uintptr_t)a; }

static bool in_main(uint64_t v) { return v >= 0x7000000000ull && g_site.main_end && v < g_site.main_end && !(v & 7); }

// The class of the flag named by `hash`: its object has the hash at hash_at and its class at 0
static uint64_t class_of(uint64_t lo, uint64_t hi, uint32_t hash, uint64_t hash_at) {
    uint64_t a, e;
    for (uint64_t at = lo; plat_committed_range(at, hi, &a, &e); at = e)  // (only memory that is there)
        for (uint64_t p = a + hash_at; p + 4 <= e; p += 4) {
            if (rd32(p) != hash || (p - hash_at) & 7) continue;
            uint64_t vt = rd64(p - hash_at);
            if (in_main(vt)) return vt;
        }
    return 0;
}

// Every bool and s32 flag object on the heap
static bool find_flags(void) {
    uint64_t lo = HEAP_BASE, hi = HEAP_BASE + kernel_heap_size();
    if (hi <= lo) return false;
    if (!vt_bool) vt_bool = class_of(lo, hi, BOOL_HASH, 0xc);
    if (!vt_s32) vt_s32 = class_of(lo, hi, S32_HASH, 0x1c);
    if (!vt_bool || !vt_s32) return false;
    int cap = 1 << 15, n = 0;
    LiveFlag *f = malloc(sizeof *f * cap);
    uint64_t a, e;
    for (uint64_t at = lo; f && plat_committed_range(at, hi, &a, &e); at = e)
        for (uint64_t p = (a + 7) & ~7ull; p + 0x28 <= e && f; p += 8) {
            uint64_t v = rd64(p);
            if (v != vt_bool && v != vt_s32) continue;
            if (n == cap) f = realloc(f, sizeof *f * (cap *= 2));
            if (f) f[n++] = (LiveFlag){p, rd32(p + (v == vt_s32 ? 0x1c : 0xc)), v == vt_s32};
        }
    if (!f || n < 1000) { free(f); return false; }
    free(flags);
    flags = f, n_flags = n;
    LOG("[achievements] %d game data flags in memory, read every %d ms\n", n, LIVE_POLL_MS);
    return true;
}

static bool still_flags(void) {  // (a sample: the heap may be laid out again, a new run of the game)
    for (int i = 0; i < n_flags; i += n_flags / 16 + 1) {
        const LiveFlag *f = &flags[i];
        if (rd64(f->obj) != (f->s32 ? vt_s32 : vt_bool) || rd32(f->obj + (f->s32 ? 0x1c : 0xc)) != f->hash) return false;
    }
    return n_flags > 0;
}

// (tests) a flag set from the next look on, as an event would
static char set_name[64];
static uint32_t set_value;
static volatile int set_pending;
void ach_live_set(const char *name, uint32_t value) {
    snprintf(set_name, sizeof set_name, "%s", name);
    set_value = value;
    set_pending = 1;
}

static uint32_t crc32_text(const char *s) {
    uint32_t c = 0xFFFFFFFFu;
    for (; *s; s++) {
        c ^= (uint8_t)*s;
        for (int k = 0; k < 8; k++) c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

static void *live_thread(void *arg) {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);  // (never ahead of the game's threads on their cores)
#endif
    (void)arg;
    uint8_t *img = NULL;
    size_t cap = 0;
    int img_n = 0;  // (the flags img holds: the previous look's, for SAVAGEWIND_LOG_FLAGS)
    pthread_mutex_lock(&llock);
    while (!stop_req) {
        pthread_mutex_unlock(&llock);
        bool ok = n_flags && still_flags();
        if (!ok) ok = find_flags();
        if (ok) {
            size_t n = 0xC + 8 * (size_t)n_flags;
            if (n > cap) img = realloc(img, cap = n);
            if (img) {
                // SAVAGEWIND_LOG_FLAGS=1: each flag that changes from one look to the next (crc32 of its name: tools'
                // flag lists name it), to find what an action in the game sets
                static int log_flags = -1;
                if (log_flags < 0) log_flags = getenv("SAVAGEWIND_LOG_FLAGS") && strcmp(getenv("SAVAGEWIND_LOG_FLAGS"), "0");
                bool same = log_flags && n == cap && img_n == n_flags;
                memset(img, 0, 0xC);
                if (set_pending) {
                    uint32_t hash = crc32_text(set_name);
                    for (int i = 0; i < n_flags; i++) {
                        const LiveFlag *f = &flags[i];
                        if (f->hash != hash) continue;
                        if (f->s32) *(volatile uint32_t *)(uintptr_t)(f->obj + 0x18) = set_value;
                        else *(volatile uint8_t *)(uintptr_t)(f->obj + 0xb) = (uint8_t)((*(volatile uint8_t *)(uintptr_t)(f->obj + 0xb) & ~1) | (set_value & 1));
                        LOG("[achievements] flag %s set to %u\n", set_name, set_value);
                    }
                    set_pending = 0;
                }
                for (int i = 0; i < n_flags; i++) {
                    const LiveFlag *f = &flags[i];
                    if (f->s32 && f->hash == RUPEE_HASH && g_settings.cheat_rupees && rd32(f->obj + 0x18) < 9999) {  // (Cheats > Infinite rupees)
                        *(volatile uint32_t *)(uintptr_t)(f->obj + 0x18) = 9999;
                        LOG("[cheats] infinite rupees: set\n");
                    }
                    uint32_t v = f->s32 ? rd32(f->obj + 0x18) : (*(volatile uint8_t *)(uintptr_t)(f->obj + 0xb) & 1), old;
                    memcpy(&old, img + 0xC + 8 * i + 4, 4);
                    if (same && old != v) LOG("[flags] %08x: %d -> %d\n", f->hash, (int)old, (int)v);
                    memcpy(img + 0xC + 8 * i, &f->hash, 4);
                    memcpy(img + 0xC + 8 * i + 4, &v, 4);
                }
                img_n = n_flags;
                AchSave s = {img, n};
                int fresh = ach_live_update(&s, time(NULL));
                if (fresh) LOG("[achievements] %d new from the game's flags\n", fresh);
            }
        }
        pthread_mutex_lock(&llock);
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        long long ns = until.tv_nsec + (ok ? LIVE_POLL_MS : 3000) * 1000000LL;  // (not there yet: the game is starting)
        until.tv_sec += (time_t)(ns / 1000000000LL), until.tv_nsec = (long)(ns % 1000000000LL);
        while (!stop_req && pthread_cond_timedwait(&lcond, &llock, &until) != ETIMEDOUT) {}
    }
    pthread_mutex_unlock(&llock);
    free(img);
    return NULL;
}

void ach_live_start(void) {
    if (running || getenv("SAVAGEWIND_NO_LIVE_ACH")) return;
    stop_req = false;
    if (pthread_create(&thread, NULL, live_thread, NULL) == 0) running = true;
}

void ach_live_stop(void) {
    if (!running) return;
    pthread_mutex_lock(&llock);
    stop_req = true;
    pthread_cond_signal(&lcond);
    pthread_mutex_unlock(&llock);
    pthread_join(thread, NULL);
    running = false;
}
