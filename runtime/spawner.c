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
#include <math.h>
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
static void actor_frame(Cpu *c);
static int g_aqn;
void spawner_frame(void) {
    if (g_aqn && spawner_available()) {
        Cpu *c = kernel_current_cpu();
        if (c) actor_frame(c);
    }
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

// ------------------------------------------------------------------ actors (the Spawn tab)
// Creates an actor in front of Link, facing him, through ksys::act::ActorCreator::requestCreateActor(name, heap, handle,
// InstParamPack *params, map::Object *, int lane) - asynchronous: the create task copies the name and the parameter
// buffer when it is queued, so both can live on the guest stack here; only the parameter keys ("@M") are kept as
// pointers, and those are the game's own string literals. 1.9.0 addresses, from CreatePlayerEquipActorMgr::
// requestCreateArmor (0x7000b25054..), which does the same with Link's position:
#define BUF_ADD_190        0x7001126168ull  // InstParamPack::Buffer::add(buf, const void *data, const SafeString &key, s32 size, EntryType)
#define BUF_CLEAR_190      0x700112615cull  // InstParamPack::Buffer::clear (its constructor)
#define PARAMPACK_GOT_190  0x7001d664b0ull  // InstParamPack's vtable (+0x10)
#define REQ_CREATE_190     0x7001535b10ull  // ActorCreator::requestCreateActor
#define CREATOR_GOT_190    0x7001d66550ull  // &ActorCreator::sInstance
#define HEAPUTIL_GOT_190   0x7001d66558ull  // &ActorHeapUtil::sInstance (getBaseProcHeap: +0x28)
#define PLAYERINFO_GOT_190 0x7001d66a78ull  // &PlayerInfo::sInstance (getPlayerLink: +0x68)
#define ACQUIRE_190        0x700056d8d8ull  // ksys::act::acquireActor(BaseProcLink *, ActorLinkConstDataAccess *)
#define GETMTX_190         0x70010726a4ull  // ActorLinkConstDataAccess::getActorMtx -> const Matrix34f *
#define ACCESS_DTOR_190    0x7001126464ull  // ~ActorLinkConstDataAccess (releases the link)
#define KEY_M_190          0x700160f9e5ull  // "@M"

typedef struct { char name[64]; int count; float dist; } AReq;
static AReq g_aq[16];

int spawner_actor_request(const char *name, int count, float dist) {
    if (!spawner_available() || !name || !*name || strlen(name) >= sizeof g_aq[0].name) return 0;
    lock();
    int ok = g_aqn < (int)(sizeof g_aq / sizeof g_aq[0]);
    if (ok) {
        AReq *r = &g_aq[g_aqn++];
        snprintf(r->name, sizeof r->name, "%s", name);
        r->count = count < 1 ? 1 : count > 10 ? 10 : count;
        r->dist = dist;
    }
    unlock();
    return ok;
}

static uint64_t got(uint64_t entry) {  // a GOT entry's pointer (relocated at load; module-relative if not)
    uint64_t v = LD64(entry);
    return v && v < GUEST_LO ? v + GUEST_LO : v;
}

static uint64_t gcall(Cpu *c, uint64_t fn, uint64_t sp, const uint64_t *args, int n) {
    Cpu saved = *c;
    c->sp = sp;
    for (int i = 0; i < n; i++) c->x[i] = args[i];
    guest_call(c, fn);
    uint64_t r = c->x[0];
    *c = saved;
    return r;
}

static void actor_frame(Cpu *c) {
    AReq todo[16];
    int n;
    lock();
    n = g_aqn;
    memcpy(todo, g_aq, sizeof todo[0] * n);
    g_aqn = 0;
    unlock();
    if (!n) return;
    uint64_t pinfo = LD64(got(PLAYERINFO_GOT_190)), creator = LD64(got(CREATOR_GOT_190));
    uint64_t heaputil = LD64(got(HEAPUTIL_GOT_190)), pack_vt = got(PARAMPACK_GOT_190), str_vt = got(SAFESTRING_GOT_190);
    uint64_t heap = guest_ptr(heaputil) ? LD64(heaputil + 0x28) : 0;
    if (!guest_ptr(pinfo) || !guest_ptr(creator) || !guest_ptr(heap) || !guest_ptr(pack_vt) || !guest_ptr(str_vt)) {
        snprintf(g_status, sizeof g_status, "The game isn't ready to spawn yet. Load your save first.");
        LOG("[spawner] actors not ready: player info %llx, creator %llx, heap %llx\n", (unsigned long long)pinfo,
            (unsigned long long)creator, (unsigned long long)heap);
        return;
    }
    // scratch on the guest stack, well below the frame
    uint64_t sp = (c->sp - 0x1000) & ~0xfull;
    uint64_t access = sp + 0x100, mtxbuf = sp + 0x140, key = sp + 0x180, pack = sp + 0x200, name = sp + 0x300;
    memset((void *)(uintptr_t)access, 0, 0x20);
    uint64_t a1[2] = {pinfo + 0x68, access};
    int have = (int)(gcall(c, ACQUIRE_190, sp, a1, 2) & 1);
    float m[12] = {0};
    if (have) {
        uint64_t a2[1] = {access};
        uint64_t mp = gcall(c, GETMTX_190, sp, a2, 1);
        if (guest_ptr(mp)) memcpy(m, (void *)(uintptr_t)mp, sizeof m); else have = 0;
    }
    {
        uint64_t a3[1] = {access};
        gcall(c, ACCESS_DTOR_190, sp, a3, 1);
    }
    if (!have) {
        snprintf(g_status, sizeof g_status, "Couldn't find Link. Try again once you're in control of him.");
        LOG("[spawner] no player actor\n");
        return;
    }
    // Matrix34f rows: m[0..3] = (r00 r01 r02 tx), ... Link faces his +Z column.
    float fx = m[2], fy = m[6], fz = m[10], fl = sqrtf(fx * fx + fz * fz);
    if (fl < 1e-4f) fx = 0, fz = 1, fl = 1;
    fx /= fl, fz /= fl;
    (void)fy;
    int made = 0;
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < todo[i].count; k++) {
            float side = (k - (todo[i].count - 1) * 0.5f) * 2.0f;  // a row across Link's view, 2 m apart
            // facing Link: his rotation turned 180 degrees about Y (X and Z columns negated)
            float t[12] = {-m[0], m[1], -m[2], 0, -m[4], m[5], -m[6], 0, -m[8], m[9], -m[10], 0};
            t[3] = m[3] + fx * todo[i].dist + fz * side;
            t[7] = m[7] + 0.5f;
            t[11] = m[11] + fz * todo[i].dist - fx * side;
            memcpy((void *)(uintptr_t)mtxbuf, t, sizeof t);
            ST64(pack, pack_vt + 0x10);
            ST64(pack + 8, 0);
            uint64_t a4[1] = {pack + 0x10};
            gcall(c, BUF_CLEAR_190, sp, a4, 1);
            ST64(key, str_vt + 0x10);
            ST64(key + 8, KEY_M_190);
            uint64_t a5[5] = {pack + 0x10, mtxbuf, key, 0x30, 7};  // EntryType::Matrix34
            gcall(c, BUF_ADD_190, sp, a5, 5);
            memcpy((void *)(uintptr_t)name, todo[i].name, strlen(todo[i].name) + 1);
            uint64_t a6[7] = {creator, name, heap, 0, pack, 0, 0};
            int ok = (int)(gcall(c, REQ_CREATE_190, sp, a6, 7) & 1);
            made += ok;
            LOG("[spawner] requestCreateActor %s at %.1f %.1f %.1f: %s\n", todo[i].name, t[3], t[7], t[11], ok ? "queued" : "refused");
        }
    }
    if (made)
        snprintf(g_status, sizeof g_status, "Spawning %s%s in front of Link.", todo[n - 1].name, made > 1 ? " and more" : "");
    else
        snprintf(g_status, sizeof g_status, "The game refused to create %s here.", todo[n - 1].name);
}
