// Item spawner (the menu's Items tab, runtime/ui/menu.mm tab_items): adds items to Link's pouch through the game's own
// uking::ui::PauseMenuDataMgr::itemGet(const sead::SafeString &name, int value, const WeaponModifierInfo *modifier) -
// the call the game makes when Link picks something up, so the pouch's own rules apply (stacking, the 999 cap, free
// slots, IsGet flags, the gamedata copy for the next save).
//
// The menu runs on the render thread; itemGet has to run on the game's main thread between frames. Requests are queued
// and run from rt_vfr_update (runtime/hle/framepace.c), which the game calls once a frame at the start of its frame
// (ksys::VFR::updateInterval), on its main thread.
//
// 1.9.0 only (main build id cd57b23fa4bbad65), found by hand in that build:
//   itemGet                       0x7000d921bc ("Default" / "Extra" checks, then the "PouchGet" play report)
//   PauseMenuDataMgr::sInstance   0x7001e18570 (its GOT entry is 0x7001d67af0; see the helper at 0x7000be0f10,
//                                 which gives one of an item: itemGet(*sInstance, name, 1, nullptr))
//   sead::SafeStringBase<char> vtable: its GOT entry 0x7001d66260 (itemGet's own SafeString temporaries use it + 0x10)
// A SafeString is {vtable, const char *top}: built on the guest stack below the frame's sp for the call.
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "game_sites.h"
#include "hle.h"
#include "rt.h"

#define ITEMGET_190        0x7000d921bcull
#define PMDM_INSTANCE_190  0x7001e18570ull
#define SAFESTRING_GOT_190 0x7001d66260ull

typedef struct { char name[64]; int32_t value; int count; } Req;
static Req g_q[64];
static int g_qn;
static atomic_flag g_lock = ATOMIC_FLAG_INIT;
static char g_status[200];
static atomic_int g_status_seq;

static void lock(void) { while (atomic_flag_test_and_set_explicit(&g_lock, memory_order_acquire)) { } }
static void unlock(void) { atomic_flag_clear_explicit(&g_lock, memory_order_release); }

int spawner_available(void) { return g_game_version && !strcmp(g_game_version, "1.9.0"); }

// (render thread) the last result, for the menu
const char *spawner_status(void) { return g_status; }

// (render thread) queue `count` of an item: value is what itemGet takes - an amount for things that stack, the
// durability x 100 for weapons, bows and shields, the dye colour (0) for armour. stack: one call with value = count;
// otherwise one call (one slot) per item.
int spawner_request(const char *name, int value, int count, int stack) {
    if (!spawner_available() || !name || !*name || strlen(name) >= sizeof g_q[0].name) return 0;
    lock();
    int ok = g_qn < (int)(sizeof g_q / sizeof g_q[0]);
    if (ok) {
        Req *r = &g_q[g_qn++];
        snprintf(r->name, sizeof r->name, "%s", name);
        r->value = stack ? count : value;
        r->count = stack ? 1 : count;
    }
    unlock();
    return ok;
}

static int guest_ptr(uint64_t p) { return p >= GUEST_LO && p < GUEST_HI - 0x100; }

// (game main thread, once a frame) run the queued requests
void spawner_frame(void) {
    if (!g_qn) return;  // (racy read: the next frame picks up a request queued meanwhile)
    Req todo[64];
    int n;
    lock();
    n = g_qn;
    memcpy(todo, g_q, sizeof todo[0] * n);
    g_qn = 0;
    unlock();
    if (!spawner_available()) return;
    Cpu *c = kernel_current_cpu();
    uint64_t inst = LD64(PMDM_INSTANCE_190);
    uint64_t vt = LD64(SAFESTRING_GOT_190);
    if (vt && vt < GUEST_LO) vt += GUEST_LO;  // (an unrelocated entry: module-relative)
    if (!c || !guest_ptr(inst) || !guest_ptr(vt)) {
        snprintf(g_status, sizeof g_status, "The game isn't ready for items yet. Load your save first.");
        LOG("[spawner] not ready: cpu %p, pouch %llx, SafeString vtable %llx\n", (void *)c, (unsigned long long)inst,
            (unsigned long long)vt);
        atomic_fetch_add(&g_status_seq, 1);
        return;
    }
    vt += 0x10;
    int calls = 0;
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < todo[i].count; k++) {
            Cpu saved = *c;
            uint64_t sp = (c->sp - 0x400) & ~0xfull;  // (well below the frame: its red zone and the callee's spills)
            uint64_t str = sp + 0x40;
            size_t len = strlen(todo[i].name);
            memcpy((void *)(uintptr_t)str, todo[i].name, len + 1);
            ST64(sp, vt);
            ST64(sp + 8, str);
            c->sp = sp;
            c->x[0] = inst;
            c->x[1] = sp;
            c->x[2] = (uint64_t)(uint32_t)todo[i].value;
            c->x[3] = 0;
            guest_call(c, ITEMGET_190);
            *c = saved;
            calls++;
        }
        LOG("[spawner] itemGet %s value %d x%d\n", todo[i].name, todo[i].value, todo[i].count);
    }
    if (n == 1)
        snprintf(g_status, sizeof g_status, "Sent %s to your pouch. Open the inventory to see it (a full pouch takes nothing).",
                 todo[0].name);
    else
        snprintf(g_status, sizeof g_status, "Sent %d items to your pouch.", calls);
    atomic_fetch_add(&g_status_seq, 1);
}
