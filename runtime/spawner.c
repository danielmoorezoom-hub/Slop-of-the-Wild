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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#include "game_sites.h"
#include "hle.h"
#include "rt.h"
#include "settings.h"
#include "platform.h"

#define ITEMGET_190        0x7000d921bcull
#define PMDM_INSTANCE_190  0x7001e18570ull
#define SAFESTRING_GOT_190 0x7001d66260ull
// PauseMenuDataMgr::removeItem(const SafeString &name) 0x7000d93320: takes one of the first unequipped item of that
// name (one off a stack, or the whole slot), as when an item is used up; equipped items are left alone.
#define REMOVEITEM_190     0x7000d93320ull

typedef struct { char name[64]; int32_t value; int count; int rm; uint32_t mod_flags; int32_t mod_value; } Req;
static Req g_q[2048];
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
int spawner_request_mod(const char *name, int value, int count, int stack, uint32_t mod_flags, int32_t mod_value);
int spawner_request(const char *name, int value, int count, int stack) {
    return spawner_request_mod(name, value, count, stack, 0, 0);
}

// ... with a weapon bonus: act::WeaponModifierInfo {u32 flags; s32 value} - flags as act::WeaponModifier (AddAtk 1,
// AddLife 2, AddCrit 4, AddThrow 8, AddSpreadFire 0x10, AddZoomRapid 0x20, AddRapidFire 0x40, AddSurfMaster 0x80,
// AddGuard 0x100, | IsYellow 0x80000000). itemGet copies it into the new pouch item for weapons, bows and shields
// (1.9.0 0x7000d92458: flags at [x3], value at [x3 + 4]).
int spawner_request_mod(const char *name, int value, int count, int stack, uint32_t mod_flags, int32_t mod_value) {
    if (!spawner_available() || !name || !*name || strlen(name) >= sizeof g_q[0].name) return 0;
    lock();
    int ok = g_qn < (int)(sizeof g_q / sizeof g_q[0]);
    if (ok) {
        Req *r = &g_q[g_qn++];
        snprintf(r->name, sizeof r->name, "%s", name);
        r->value = stack ? count : value;
        r->count = stack ? 1 : count;
        r->rm = 0;
        r->mod_flags = mod_flags;
        r->mod_value = mod_value;
    }
    unlock();
    return ok;
}

// (render thread) queue removing `count` of an item (one removeItem call each: one off a stack, or one slot)
int spawner_remove_request(const char *name, int count) {
    if (!spawner_available() || !name || !*name || strlen(name) >= sizeof g_q[0].name || count < 1) return 0;
    lock();
    int ok = g_qn < (int)(sizeof g_q / sizeof g_q[0]);
    if (ok) {
        Req *r = &g_q[g_qn++];
        snprintf(r->name, sizeof r->name, "%s", name);
        r->value = 0;
        r->count = count > 999 ? 999 : count;
        r->rm = 1;
        r->mod_flags = 0;
        r->mod_value = 0;
    }
    unlock();
    return ok;
}

static int guest_ptr(uint64_t p) { return p >= GUEST_LO && p < GUEST_HI - 0x100; }
static uint64_t gcall(Cpu *c, uint64_t fn, uint64_t sp, const uint64_t *args, int n);

// (game main thread, once a frame) run the queued requests
static void actor_frame(Cpu *c);
static void slots_frame(Cpu *c);
static void tweaks_frame(Cpu *c);
static void armor_frame(void);
static void inv_frame(void);
static void noclip_frame(Cpu *c);
static void move_frame(Cpu *c);
static void world_frame(Cpu *c);
static int g_aqn;
void spawner_frame(void) {
    if (spawner_available()) {
        Cpu *c = kernel_current_cpu();
        if (c && g_aqn) actor_frame(c);
        if (c && guest_ptr(LD64(PMDM_INSTANCE_190))) slots_frame(c);
        if (c) tweaks_frame(c);
        armor_frame();
        inv_frame();
        if (c) noclip_frame(c);
        if (c) move_frame(c);
        if (c) world_frame(c);
    }
    if (!g_qn) return;  // (racy read: the next frame picks up a request queued meanwhile)
    static Req todo[96];
    int n;
    lock();  // (at most 96 a frame: a long batch, "one of each", spreads over a few frames)
    n = g_qn < 96 ? g_qn : 96;
    memcpy(todo, g_q, sizeof todo[0] * n);
    memmove(g_q, g_q + n, sizeof g_q[0] * (size_t)(g_qn - n));
    g_qn -= n;
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
    int calls = 0, removes = 0;
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
            if (todo[i].rm) {
                guest_call(c, REMOVEITEM_190);
                removes++;
            } else {
                c->x[2] = (uint64_t)(uint32_t)todo[i].value;
                c->x[3] = 0;
                if (todo[i].mod_flags) {  // act::WeaponModifierInfo on the stack
                    uint64_t mod = sp + 0x100;
                    *(uint32_t *)(uintptr_t)mod = todo[i].mod_flags;
                    *(int32_t *)(uintptr_t)(mod + 4) = todo[i].mod_value;
                    c->x[3] = mod;
                }
                guest_call(c, ITEMGET_190);
                calls++;
            }
            *c = saved;
        }
        LOG("[spawner] %s %s value %d x%d, bonus %x %d\n", todo[i].rm ? "removeItem" : "itemGet", todo[i].name,
            todo[i].value, todo[i].count, todo[i].mod_flags, todo[i].mod_value);
    }
    static int batch_calls, batch_removes;  // (a batch over several frames: its totals)
    batch_calls += calls, batch_removes += removes;
    int more = g_qn;  // (racy read: only for the message)
    if (more || batch_calls + batch_removes > calls + removes) {
        if (batch_removes && !batch_calls)
            snprintf(g_status, sizeof g_status, more ? "Removing... %d so far." : "Removed up to %d items.", batch_removes);
        else
            snprintf(g_status, sizeof g_status, more ? "Adding... %d so far." : "Sent %d items to your pouch (a full pouch takes nothing more).", batch_calls);
        if (!more) batch_calls = batch_removes = 0;
    } else if (n == 1 && todo[0].rm)
        snprintf(g_status, sizeof g_status, "Removed up to %d of %s (equipped ones are kept; unequip to remove them).",
                 todo[0].count, todo[0].name);
    else if (removes && !calls)
        snprintf(g_status, sizeof g_status, "Removed up to %d items.", removes);
    else if (n == 1)
        snprintf(g_status, sizeof g_status, "Sent %s to your pouch. Open the inventory to see it (a full pouch takes nothing).",
                 todo[0].name);
    else
        snprintf(g_status, sizeof g_status, "Sent %d items to your pouch.", calls);
    if (!more) batch_calls = batch_removes = 0;
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

// ------------------------------------------------------------------ pouch slots (the Items tab)
// The weapon / bow / shield slot counts are the game data flags WeaponPorchStockNum, BowPorchStockNum and
// ShieldPorchStockNum, read and set through the game's own ksys::gdt::getS32(handle, debug) / setS32(value, handle,
// debug). The game clamps every s32 flag to its MaxValue (20, 14, 20) when it is set and when a save loads; the
// "Raised slot limits" mod (Pack/Bootup.pack with those three raised to 100) lets them go past that. 1.9.0:
#define GETS32_190 0x7000d34ae0ull  // (getFlag_WeaponPorchStockNum at 0x7000d38b30 tail-calls it)
#define SETS32_190 0x7000d34bc0ull  // (71 s32 setter stubs tail-call it)
static const uint64_t slot_handle_190[3] = {0x7001da6f80ull, 0x7001da69a4ull, 0x7001da6e84ull};  // weapon, bow, shield
static atomic_int g_slot_cur[3] = {-1, -1, -1};
static atomic_int g_slot_set[3] = {-1, -1, -1};
static atomic_int g_slot_read;

// (render thread) the current counts (-1: not read yet); asks for a fresh read
int spawner_slots_get(int which) {
    atomic_store(&g_slot_read, 1);
    return which >= 0 && which < 3 ? atomic_load(&g_slot_cur[which]) : -1;
}
void spawner_slots_set(int which, int value) {
    if (which >= 0 && which < 3 && spawner_available()) atomic_store(&g_slot_set[which], value);
}

static void slots_frame(Cpu *c) {
    static int tick;
    int pending = 0;
    for (int i = 0; i < 3; i++) pending |= atomic_load(&g_slot_set[i]) >= 0;
    if (!pending && !(atomic_load(&g_slot_read) && ++tick % 15 == 0)) return;
    atomic_store(&g_slot_read, 0);
    uint64_t sp = (c->sp - 0x400) & ~0xfull;
    for (int i = 0; i < 3; i++) {
        uint32_t h = (uint32_t)LD64(slot_handle_190[i]);  // (a u32 handle: index | property << 24)
        if (h == 0 || h == 0xffffffffu) continue;
        int v = atomic_exchange(&g_slot_set[i], -1);
        if (v >= 0) {
            uint64_t a[3] = {(uint32_t)v, h, 0};
            gcall(c, SETS32_190, sp, a, 3);
        }
        uint64_t g[2] = {h, 0};
        int cur = (int)(int32_t)gcall(c, GETS32_190, sp, g, 2);
        atomic_store(&g_slot_cur[i], cur);
        if (v >= 0) LOG("[spawner] slots %d: asked %d, now %d\n", i, v, cur);
    }
}

// ------------------------------------------------------------------ player tweaks (the Items tab's Link section)
// Link's actions read their numbers ("static params") through pointers into his AI program resource
// (Actor/AIProgram/Player_Link.baiprog, ksys::res::AIProgram: AIProgram::getSInstParam hands out a const f32 * into
// the agl::utl::Parameter), so changing the number in memory changes the game at once. Path, all 1.9.0:
//   PlayerInfo::sInstance +0x68 (Link's BaseProcLink) -> acquireActor -> the actor (the access's +8)
//   Actor +0x570 ActorParam*  ->  +0xa0 res::AIProgram* (Resources mAIProgram; 30 places read it this way)
// The AIProgram's action lists (sead::Buffer<AIActionDef> {s32 size; T *data}, 0xc8 a definition) and each
// definition's class name and static parameter list are found by looking rather than by fixed offsets: a list whose
// elements hold a pointer to a class name below, then in that element a list of Parameter pointers, each a
// Parameter carrying the CRC32 of its name and its value (checked against the game's own value). Only memory the
// game has mapped is read (kernel_guest_mapped).
bool kernel_guest_mapped(uint64_t a, uint64_t n);

enum { TW_JUMP, TW_GLIDE, TW_SWIM, TW_GALE, TW_STAMINA, TW_CLIMB, TW_MOVE, TW_N };
typedef struct { const char *cls, *param; float def; int group; } Tweak;
static const Tweak kTweaks[] = {
    {"PlayerJump", "JumpHeight", 0.58f, TW_JUMP},
    {"PlayerWallJump", "JumpHeight", 0.58f, TW_JUMP},
    {"PlayerTwiceJump", "JumpHeight", 0.3f, TW_JUMP},
    {"PlayerLadderUpStart", "JumpHeight", 0.58f, TW_JUMP},
    {"PlayerParashawlGlide", "GlideSpeedMax", 0.02f, TW_GLIDE},
    {"PlayerParashawlGlide", "Lv2GlideSpeedMax", 0.35f, TW_GLIDE},
    {"PlayerKokkoGlide", "GlideSpeedMax", 0.02f, TW_GLIDE},
    {"PlayerKokkoGlide", "Lv2GlideSpeedMax", 0.35f, TW_GLIDE},
    {"PlayerSwimMove", "MaxSpeedF", 0.052f, TW_SWIM},
    {"PlayerSwimMove", "MaxSpeedS", 0.052f, TW_SWIM},
    {"PlayerSwimMove", "MaxSpeedB", 0.0416f, TW_SWIM},
    {"PlayerSwimMove", "MaxSpeedDash", 0.0756f, TW_SWIM},
    {"PlayerSuperJump", "JumpHeight", 40.0f, TW_GALE},
    {"PlayerActionClimb", "DashAnmSpdRate", 1.5f, TW_CLIMB},
    {"PlayerLadderMove", "MoveAnmSpeedMin", 0.4f, TW_CLIMB},
    {"PlayerLadderMove", "MoveAnmSpeedMax", 1.2f, TW_CLIMB},
    // stamina: what running, jumping, gliding, swimming and climbing take
    {"PlayerMove", "EnergyDash", 300.0f, TW_STAMINA},
    {"PlayerJump", "EnergyDashJump", 300.0f, TW_STAMINA},
    {"PlayerParashawlGlide", "EnergyGlide", 28.0f, TW_STAMINA},
    {"PlayerKokkoGlide", "EnergyGlide", 28.0f, TW_STAMINA},
    {"PlayerSwimMove", "EnergyMove", 30.0f, TW_STAMINA},
    {"PlayerSwimMove", "EnergyDash", 30.0f, TW_STAMINA},
    {"PlayerSwimDash", "EnergyDash", 125.0f, TW_STAMINA},
    {"PlayerSwimJump", "EnergyJump", 100.0f, TW_STAMINA},
    {"PlayerSwimSpinAttack", "EnergyDash", 125.0f, TW_STAMINA},
    {"PlayerLadderJump", "EnergyJump", 300.0f, TW_STAMINA},
    {"PlayerActionClimb", "StaminaDownAlways", 36.5f, TW_STAMINA},
    {"PlayerActionClimb", "StaminaDownTriggerJump", 310.0f, TW_STAMINA},
};
#define NTWEAKS ((int)(sizeof kTweaks / sizeof kTweaks[0]))

static _Atomic float g_tw_mult[TW_N] = {1, 1, 1, 1, 1, 1, 1};
static uint64_t g_tw_addr[NTWEAKS];  // where each number is (0: not found)
static float g_tw_orig[NTWEAKS], g_tw_set[NTWEAKS];
static uint64_t g_tw_prog;           // the AI program they were found in
static _Atomic int g_tw_found = -1;  // how many were found (-1: not looked yet)
static _Atomic int g_tw_active;      // a multiplier isn't 1 (or was: put the game's own numbers back)

void spawner_tweak_set(int which, float mult) {
    if (which < 0 || which >= TW_N) return;
    g_tw_mult[which] = mult;
    g_tw_active = 1;
}
float spawner_tweak_get(int which) { return which >= 0 && which < TW_N ? g_tw_mult[which] : 1; }
int spawner_tweak_found(void) { return g_tw_found; }
int spawner_tweak_total(void) { return NTWEAKS; }

static uint32_t crc32s(const char *s) {
    uint32_t c = 0xffffffffu;
    for (; *s; s++) {
        c ^= (uint8_t)*s;
        for (int k = 0; k < 8; k++) c = c >> 1 ^ (0xedb88320u & -(c & 1));
    }
    return ~c;
}
static int mapped(uint64_t a, uint64_t n) { return guest_ptr(a) && kernel_guest_mapped(a, n); }
static int str_is(uint64_t p, const char *want) {
    size_t n = strlen(want) + 1;
    return mapped(p, n) && !memcmp((const void *)(uintptr_t)p, want, n);
}

// (game thread) Link's actor's AI program, or 0
static uint64_t player_aiprog(Cpu *c) {
    uint64_t pinfo = LD64(got(PLAYERINFO_GOT_190));
    if (!mapped(pinfo, 0x80)) return 0;
    uint64_t sp = (c->sp - 0x1000) & ~0xfull, access = sp + 0x100;
    memset((void *)(uintptr_t)access, 0, 0x20);
    uint64_t a1[2] = {pinfo + 0x68, access};
    uint64_t actor = 0;
    if (gcall(c, ACQUIRE_190, sp, a1, 2) & 1) actor = LD64(access + 8);
    uint64_t a3[1] = {access};
    gcall(c, ACCESS_DTOR_190, sp, a3, 1);
    if (!mapped(actor, 0x580)) return 0;
    uint64_t param = LD64(actor + 0x570);
    if (!mapped(param, 0x100)) return 0;
    uint64_t prog = LD64(param + 0xa0);
    return mapped(prog, 0x448) ? prog : 0;
}

// in one definition (0xc8 bytes at def): the parameter `hash` whose value is near `def_value`; its value's address
static uint64_t find_param(uint64_t def, uint32_t hash, float def_value) {
    for (int off = 0; off + 16 <= 0xc8; off += 8) {  // sead::Buffer<ParameterBase *> {s32 size; ptr}
        int32_t n = *(int32_t *)(uintptr_t)(def + off);
        uint64_t arr = LD64(def + off + 8);
        if (n < 1 || n > 128 || !mapped(arr, (uint64_t)n * 8)) continue;
        for (int i = 0; i < n; i++) {
            uint64_t prm = LD64(arr + 8 * (uint64_t)i);
            if (!mapped(prm, 0x40)) break;
            int hash_at = -1;
            for (int h = 0; h < 0x20; h += 4)
                if (*(uint32_t *)(uintptr_t)(prm + h) == hash) { hash_at = h; break; }
            if (hash_at < 0) continue;
            for (int v = hash_at + 4; v + 4 <= 0x40; v += 4) {
                float f = *(float *)(uintptr_t)(prm + v);
                if (fabsf(f - def_value) <= 1e-4f * (fabsf(def_value) + 1)) return prm + v;
            }
        }
    }
    return 0;
}

static void tweaks_find(uint64_t prog) {
    memset(g_tw_addr, 0, sizeof g_tw_addr);
    int found = 0;
    for (int off = 0; off + 16 <= 0x448; off += 8) {  // the AI / Action lists: sead::Buffer<AIActionDef>
        int32_t n = *(int32_t *)(uintptr_t)(prog + off);
        uint64_t arr = LD64(prog + off + 8);
        if (n < 1 || n > 4000 || !mapped(arr, (uint64_t)n * 0xc8)) continue;
        for (int i = 0; i < n; i++) {
            uint64_t def = arr + (uint64_t)i * 0xc8;
            uint64_t names[4];
            int nn = 0;
            for (int k = 0; k + 8 <= 0xc8 && nn < 4; k += 8) {  // (pointers to "Player..." strings: its class name)
                uint64_t q = LD64(def + k);
                if (mapped(q, 8) && !memcmp((const void *)(uintptr_t)q, "Player", 6)) names[nn++] = q;
            }
            for (int t = 0; t < NTWEAKS && nn; t++) {
                if (g_tw_addr[t]) continue;
                int named = 0;
                for (int k = 0; k < nn && !named; k++) named = str_is(names[k], kTweaks[t].cls);
                if (!named) continue;
                uint64_t a = find_param(def, crc32s(kTweaks[t].param), kTweaks[t].def);
                if (a) g_tw_addr[t] = a, g_tw_orig[t] = g_tw_set[t] = *(float *)(uintptr_t)a, found++;
            }
        }
    }
    g_tw_found = found;
    LOG("[spawner] player tweaks: AI program %llx, %d of %d numbers found\n", (unsigned long long)prog, found, NTWEAKS);
    for (int t = 0; t < NTWEAKS; t++)
        if (!g_tw_addr[t]) LOG("[spawner]   not found: %s %s\n", kTweaks[t].cls, kTweaks[t].param);
}

static void tweaks_frame(Cpu *c) {
    static int tick;
    if (!g_tw_active || ++tick % 20) return;  // (three times a second, and only once something was changed)
    uint64_t prog = player_aiprog(c);
    if (!prog) return;
    if (prog != g_tw_prog) g_tw_prog = prog, tweaks_find(prog);
    int any = 0;
    for (int t = 0; t < NTWEAKS; t++) {
        uint64_t a = g_tw_addr[t];
        if (!a || !mapped(a, 4)) continue;
        float *v = (float *)(uintptr_t)a;
        if (*v != g_tw_set[t]) g_tw_orig[t] = *v;  // (the game changed it: that's its own number now)
        float m = g_tw_mult[kTweaks[t].group];
        float want = g_tw_orig[t] * m;
        if (*v != want) *v = want;
        g_tw_set[t] = want;
        any |= m != 1;
    }
    if (!any) g_tw_active = 0;  // (all back to the game's own numbers)
}

// ------------------------------------------------------------------ armour slots (Items > Pouch slots)
// The pouch takes 100 armour pieces: a constant in the game code, not a game data flag like the weapon, bow and shield
// slots. 1.9.0, both in uking::ui::PauseMenuDataMgr:
//   cannotGetItem(name, n) 0x7000d91198: armour: countItems(ArmorHead) + n > 100 (0x7000d91408 cmp w8, #0x64)
//   isOverCategoryLimit(type) 0x7000d9f554: armour: count > 99 (0x7000d9f594 cmp w19, #0x63)
// In the recompiled code each comparison is a few register loads of the constant (and of what the flags need: 99,
// -100), as 32-bit immediates; they are found in those functions' compiled code by their exact bytes and rewritten.
// (Whatever the limit, the pouch holds 420 items of every kind together.)
int code_write32(void *at, uint32_t v);
static const uint8_t kArmorCannot[] = {0xbb, 0x63, 0, 0, 0, 0xbd, 0x64, 0, 0, 0, 0x41, 0xbe, 0x9c, 0xff, 0xff, 0xff};
static const uint8_t kArmorOver[] = {0xb9, 0x62, 0, 0, 0, 0xba, 0x63, 0, 0, 0};
static uint8_t *g_armor_cannot, *g_armor_over;  // where those are (found once, before any change)
static int g_armor_applied = 100, g_armor_state;   // state: 0 not looked, 1 found, -1 not found
int spawner_armor_state(void) { return g_armor_state; }

static uint8_t *find_bytes(uint64_t guest_fn, size_t len, const uint8_t *pat, size_t n) {
    GuestFn f = dispatch_lookup(guest_fn);
    if (!f) return NULL;
    uint8_t *p = (uint8_t *)(uintptr_t)f;
    uint8_t *hit = NULL;
    for (size_t i = 0; i + n <= len; i++)
        if (!memcmp(p + i, pat, n)) {
            if (hit) return NULL;  // (twice: not sure which)
            hit = p + i;
        }
    return hit;
}

static void armor_frame(void) {
    int want = g_settings.armor_slots;
    if (want < 100) want = 100;
    if (want > 300) want = 300;
    if (want == g_armor_applied || g_armor_state < 0) return;
    if (!g_armor_state) {
        g_armor_cannot = find_bytes(0x7000d91198ull, 0x1400, kArmorCannot, sizeof kArmorCannot);
        g_armor_over = find_bytes(0x7000d9f554ull, 0xc00, kArmorOver, sizeof kArmorOver);
        g_armor_state = g_armor_cannot ? 1 : -1;
        LOG("[spawner] armour limit code: %p %p\n", (void *)g_armor_cannot, (void *)g_armor_over);
        if (g_armor_state < 0) {
            snprintf(g_status, sizeof g_status, "Couldn't find the armour limit in this game build: armour slots stay at 100.");
            return;
        }
    }
    int ok = !code_write32(g_armor_cannot + 1, (uint32_t)(want - 1)) && !code_write32(g_armor_cannot + 6, (uint32_t)want) &&
             !code_write32(g_armor_cannot + 12, (uint32_t)-want);
    if (ok && g_armor_over)
        ok = !code_write32(g_armor_over + 1, (uint32_t)(want - 2)) && !code_write32(g_armor_over + 6, (uint32_t)(want - 1));
    LOG("[spawner] armour slots %d -> %d%s\n", g_armor_applied, want, ok ? "" : " (failed)");
    if (ok) g_armor_applied = want;
    else g_armor_state = -1;
}

// ------------------------------------------------------------------ what's in the pouch (Items > Remove)
// PauseMenuDataMgr's item list, read on the game thread a few times a second: 1.9.0 (as removeItem 0x7000d93320 walks
// it) a sead::OffsetList at +0x68 (head node: prev +0x68, next +0x70; the node's offset in an item at +0x7c), an item
// (uking::ui::PouchItem): +0x18 type, +0x20 value, +0x24 equipped, +0x28 its name (SafeString: the string at +0x30).
typedef struct { char name[64]; int type, value, equipped; } InvItem;
static InvItem g_inv[600], g_inv_tmp[600];
static int g_inv_n = -1;
static atomic_flag g_inv_lock = ATOMIC_FLAG_INIT;
bool kernel_guest_mapped(uint64_t a, uint64_t n);

static void inv_frame(void) {
    static int tick;
    if (++tick % 15) return;
    uint64_t pm = LD64(PMDM_INSTANCE_190);
    if (!guest_ptr(pm) || !kernel_guest_mapped(pm, 0x80)) return;
    uint64_t head = pm + 0x68, node = LD64(pm + 0x70);
    int32_t off = *(int32_t *)(uintptr_t)(pm + 0x7c);
    int n = 0;
    for (int guard = 0; node != head && guard < 600; guard++) {
        if (!guest_ptr(node) || !kernel_guest_mapped(node, 16)) break;
        uint64_t it = node - (uint64_t)(int64_t)off;
        if (!kernel_guest_mapped(it, 0x40)) break;
        uint64_t s = LD64(it + 0x30);
        if (guest_ptr(s) && kernel_guest_mapped(s, 1) && n < 600) {
            InvItem *v = &g_inv_tmp[n];
            size_t k = 0;
            while (k < sizeof v->name - 1 && kernel_guest_mapped(s + k, 1) && ((const char *)(uintptr_t)s)[k]) {
                v->name[k] = ((const char *)(uintptr_t)s)[k];
                k++;
            }
            v->name[k] = 0;
            v->type = *(int32_t *)(uintptr_t)(it + 0x18);
            v->value = *(int32_t *)(uintptr_t)(it + 0x20);
            v->equipped = *(uint8_t *)(uintptr_t)(it + 0x24);
            if (k) n++;
        }
        node = LD64(node + 8);
    }
    while (atomic_flag_test_and_set_explicit(&g_inv_lock, memory_order_acquire)) { }
    memcpy(g_inv, g_inv_tmp, sizeof g_inv[0] * (size_t)n);
    g_inv_n = n;
    atomic_flag_clear_explicit(&g_inv_lock, memory_order_release);
}

// (render thread) a copy of the pouch as last read; -1 before the first read (no save loaded yet)
int spawner_inventory(InvItem *out, int max) {
    while (atomic_flag_test_and_set_explicit(&g_inv_lock, memory_order_acquire)) { }
    int n = g_inv_n < 0 ? -1 : g_inv_n < max ? g_inv_n : max;
    if (n > 0) memcpy(out, g_inv, sizeof out[0] * (size_t)n);
    atomic_flag_clear_explicit(&g_inv_lock, memory_order_release);
    return n;
}

// ------------------------------------------------------------------ noclip flying (R3 + d-pad down)
// While on, Link hangs where he is and the left stick flies him (along the camera's view, flattened), A rises, B sinks,
// ZR goes faster; the right stick still turns the camera. Done through Link's main rigid body (Actor +0x190, as 8 of
// the calls to RigidBody::getPosition load it) and its MotionAccessor (RigidBody +0xb8; RigidBody::getPosition
// 0x70012d3e68 and getLinearVelocity 0x70012d4c5c read it there): its virtuals setPosition (+0x18) and
// setLinearVelocity(const Vector3f &, float) (+0x48), getPosition (+0x20) - after sead's two RTTI ones. Each frame
// starts with Link put at the flown position and still, so falling never builds up. hle/input.c noclip_take_input
// takes the controls; nvn/vulkan/exec.c route_camera hands over the camera (noclip_camera).
volatile int g_noclip_on;
static _Atomic int32_t nc_lx, nc_ly;
static atomic_int nc_up, nc_down, nc_fast;
static _Atomic float nc_r[3] = {1, 0, 0}, nc_b[3] = {0, 0, 1};
static atomic_int nc_cam_seen;

// (input thread) t: the combo; returns true when the game should not get the movement controls
bool noclip_take_input(bool combo, int32_t lx, int32_t ly, bool a, bool b, bool zr) {
    static bool was;
    bool edge = combo && !was;
    was = combo;
    if (edge && spawner_available()) {
        g_noclip_on = !g_noclip_on;
        LOG("[noclip] %s\n", g_noclip_on ? "on" : "off");
    }
    if (!g_noclip_on) return false;
    nc_lx = lx, nc_ly = ly, nc_up = a, nc_down = b, nc_fast = zr;
    return true;
}

// (render thread) the world camera's matrix: right, up, back in its columns (as photo.c reads it)
void noclip_camera(const float *f) {
    nc_r[0] = f[44], nc_r[1] = f[48], nc_r[2] = f[52];
    nc_b[0] = f[46], nc_b[1] = f[50], nc_b[2] = f[54];
    nc_cam_seen = 1;
}

static uint64_t player_actor(Cpu *c) {
    uint64_t pinfo = LD64(got(PLAYERINFO_GOT_190));
    if (!mapped(pinfo, 0x80)) return 0;
    uint64_t sp = (c->sp - 0x1000) & ~0xfull, access = sp + 0x100;
    memset((void *)(uintptr_t)access, 0, 0x20);
    uint64_t a1[2] = {pinfo + 0x68, access};
    uint64_t actor = 0;
    if (gcall(c, ACQUIRE_190, sp, a1, 2) & 1) actor = LD64(access + 8);
    uint64_t a3[1] = {access};
    gcall(c, ACCESS_DTOR_190, sp, a3, 1);
    return mapped(actor, 0x580) ? actor : 0;
}


// Link's body. His actor has no main rigid body (Actor +0x190 is null for the player: he moves through his character
// controller), so it's looked for: phys::RigidBody objects reachable from his physics (Actor +0x578, InstanceSet) a
// few pointers deep, known by their MotionAccessor (+0xb8) pointing back at them (MotionAccessor::mBody, +8), the
// one nearest where PlayerInfo says Link is (+0x88). Found once per actor.
static uint64_t g_pbody_actor, g_pbody;
static void body_pos(Cpu *c, uint64_t sp, uint64_t body, float out[3]) {
    uint64_t acc = LD64(body + 0xb8), vt = LD64(acc), v = sp + 0x80;
    memset((void *)(uintptr_t)v, 0, 16);
    uint64_t a[2] = {acc, v};
    gcall(c, LD64(vt + 0x20), sp, a, 2);
    memcpy(out, (void *)(uintptr_t)v, 12);
}
static int is_body(uint64_t q) {
    if (!mapped(q, 0xc0)) return 0;
    uint64_t m = LD64(q + 0xb8);
    return mapped(m, 0x10) && LD64(m + 8) == q && guest_ptr(LD64(m)) && guest_ptr(LD64(LD64(m) + 0x20));
}
static uint64_t player_body(Cpu *c, uint64_t actor) {
    if (!actor) return 0;
    uint64_t mb = LD64(actor + 0x190);
    if (is_body(mb)) return mb;
    if (actor == g_pbody_actor && is_body(g_pbody)) return g_pbody;
    static int wait;
    if (actor == g_pbody_actor && !g_pbody && wait-- > 0) return 0;  // (not found: look again in 2 s)
    wait = 60;
    g_pbody_actor = actor, g_pbody = 0;
    uint64_t pinfo = g_site.player_info ? LD64(g_site.player_info) : 0;
    float lp[3] = {0, 0, 0};
    if (mapped(pinfo, 0x100)) memcpy(lp, (void *)(uintptr_t)(pinfo + 0x88), 12);
    // breadth-first from the InstanceSet, 3 levels, the first 0x300 bytes of each object
    static uint64_t q[4096];
    int qn = 0, head = 0, found = 0;
    uint64_t sp = (c->sp - 0x1000) & ~0xfull;
    float best = 1e30f;
    uint64_t root = LD64(actor + 0x578);
    if (!mapped(root, 0x200)) return 0;
    q[qn++] = root;
    int level_end = qn, level = 0;
    while (head < qn && level < 3) {
        uint64_t o = q[head++];
        for (int off = 0; off < 0x300; off += 8) {
            if (!mapped(o + off, 8)) break;
            uint64_t p = LD64(o + off);
            if (!guest_ptr(p) || (p & 7)) continue;
            if (is_body(p)) {
                float bp[3];
                body_pos(c, sp, p, bp);
                float d = (bp[0] - lp[0]) * (bp[0] - lp[0]) + (bp[1] - lp[1]) * (bp[1] - lp[1]) + (bp[2] - lp[2]) * (bp[2] - lp[2]);
                found++;
                if (d < best) best = d, g_pbody = p;
            } else if (qn < 4096 && mapped(p, 0x100)) {
                int dup = 0;
                for (int k = 0; k < qn && !dup; k++) dup = q[k] == p;
                if (!dup) q[qn++] = p;
            }
        }
        if (head == level_end) level++, level_end = qn;
    }
    LOG("[noclip] Link's body: %d bodies near his physics, nearest %llx at %.2f m\n", found, (unsigned long long)g_pbody,
        best < 1e29f ? sqrtf(best) : -1.0f);
    if (best > 9.0f) g_pbody = 0;  // (more than 3 m from him: not his)
    return g_pbody;
}

static void noclip_frame(Cpu *c) {
    static int on;
    static float pos[3];
    static uint64_t last;
    if (!g_noclip_on) { on = 0; return; }
    uint64_t actor = player_actor(c);
    uint64_t body = player_body(c, actor), acc = body ? LD64(body + 0xb8) : 0;
    uint64_t vt = mapped(acc, 0x10) ? LD64(acc) : 0;  // (the vtable is in the game's module: guest_ptr, not mapped)
    if (!guest_ptr(vt) || !guest_ptr(LD64(vt + 0x18))) {
        static int told;
        if (on || told++ < 3)
            LOG("[noclip] no body: actor %llx body %llx accessor %llx vtable %llx\n", (unsigned long long)actor,
                (unsigned long long)body, (unsigned long long)acc, (unsigned long long)vt);
        on = 0;
        return;
    }
    uint64_t sp = (c->sp - 0x1000) & ~0xfull, v = sp + 0x80;
    uint64_t now = plat_now_ns();
    if (!on) {  // where he is now
        memset((void *)(uintptr_t)v, 0, 16);
        uint64_t a[2] = {acc, v};
        gcall(c, LD64(vt + 0x20), sp, a, 2);
        memcpy(pos, (void *)(uintptr_t)v, sizeof pos);
        on = 1, last = now;
        LOG("[noclip] from %.1f %.1f %.1f\n", pos[0], pos[1], pos[2]);
    }
    float dt = (float)((now - last) / 1e9);
    if (dt > 0.1f) dt = 0.1f;
    last = now;
    // flat forward and right from the camera (or Link's own facing until a camera has been seen)
    float fx, fz, rx, rz;
    if (nc_cam_seen) {
        fx = -nc_b[0], fz = -nc_b[2], rx = nc_r[0], rz = nc_r[2];
    } else {
        const float *m = (const float *)(uintptr_t)(actor + 0x398);
        fx = m[2], fz = m[10], rx = -m[10], rz = m[2];
    }
    float fl = sqrtf(fx * fx + fz * fz), rl = sqrtf(rx * rx + rz * rz);
    if (fl > 1e-4f) fx /= fl, fz /= fl;
    if (rl > 1e-4f) rx /= rl, rz /= rl;
    float speed = (nc_fast ? 45.0f : 12.0f) * dt, sx = nc_lx / 32767.0f, sy = nc_ly / 32767.0f;
    pos[0] += (rx * sx + fx * sy) * speed;
    pos[2] += (rz * sx + fz * sy) * speed;
    pos[1] += ((int)nc_up - (int)nc_down) * speed;
    memcpy((void *)(uintptr_t)v, pos, sizeof pos);
    *(float *)(uintptr_t)(v + 12) = 0;
    {   // setPosition(pos, propagate to linked motions)
        uint64_t a[3] = {acc, v, 1};
        gcall(c, LD64(vt + 0x18), sp, a, 3);
    }
    {   // setLinearVelocity(0, epsilon 0)
        uint64_t z = v + 0x20;
        memset((void *)(uintptr_t)z, 0, 16);
        Cpu saved = *c;
        c->sp = sp, c->x[0] = acc, c->x[1] = z, c->v[0] = 0;
        guest_call(c, LD64(vt + 0x48));
        *c = saved;
    }
}

// ------------------------------------------------------------------ movement speed (Link > Movement speed)
// Running, walking, climbing and gliding speeds come from Link's animations (root motion), not numbers the game reads,
// so this goes by where he got to: each frame, how far he moved across the ground since the last one, and that move
// again (times the multiplier less one) through the same setPosition noclip uses. Up and down are left alone. A jump of
// more than 5 m in a frame (a warp, a load) isn't a step and is skipped.
static void move_frame(Cpu *c) {
    static int have;
    static float prev[3];
    float m = g_tw_mult[TW_MOVE];
    if (g_noclip_on || m == 1.0f) { have = 0; return; }
    uint64_t actor = player_actor(c);
    uint64_t body = player_body(c, actor), acc = body ? LD64(body + 0xb8) : 0;
    uint64_t vt = mapped(acc, 0x10) ? LD64(acc) : 0;
    if (!guest_ptr(vt) || !guest_ptr(LD64(vt + 0x20))) { have = 0; return; }
    uint64_t sp = (c->sp - 0x1000) & ~0xfull, v = sp + 0x80;
    memset((void *)(uintptr_t)v, 0, 16);
    uint64_t a[2] = {acc, v};
    gcall(c, LD64(vt + 0x20), sp, a, 2);
    float pos[3];
    memcpy(pos, (void *)(uintptr_t)v, sizeof pos);
    if (have) {
        float dx = pos[0] - prev[0], dz = pos[2] - prev[2], d2 = dx * dx + dz * dz;
        if (d2 > 1e-6f && d2 < 25.0f) {
            pos[0] += dx * (m - 1), pos[2] += dz * (m - 1);
            memcpy((void *)(uintptr_t)v, pos, sizeof pos);
            uint64_t b[3] = {acc, v, 1};
            gcall(c, LD64(vt + 0x18), sp, b, 3);
        }
    }
    memcpy(prev, pos, sizeof prev);
    have = 1;
}

// ------------------------------------------------------------------ time of day and fast travel (Items > World)
// Time: ksys::world::Manager [world_mgr] +0x5c8 its managers, [0] = TimeMgr: +0xb8 the time (0..360 = 24 h), +0xc0
// mNewTime (what TimeMgr::setTime writes and calc takes over: the inn's way of changing the hour), +0xc4 the clock's
// step a frame (0 stops it).
// Fast travel: game data bools by name. A name's handle as TimeMgr::loadFlags makes them (1.9.0 0x7001425664):
// GameDataMgr (its pointer's GOT entry 0x7001d66478) +0xbd8 -> the flag tables, crc32(name) looked up in the bool
// table (0x7001143950), | GameDataMgr +0xc28 << 24. getBool(handle, debug) 0x7000d349d0, setBool(value, handle,
// debug) 0x7000d34a20.
#define GDTMGR_GOT_190  0x7001d66478ull
#define BOOLFIND_190    0x7001143950ull
#define GETBOOL_190     0x7000d349d0ull
#define SETBOOL_190     0x7000d34a20ull
static _Atomic float g_time_req = -1;
static atomic_int g_time_freeze, g_warp_req = -1, g_warp_on;
static _Atomic float g_time_now = -1;
void spawner_time_set(float hour) { g_time_req = hour; }
float spawner_time_get(void) { return g_time_now; }  // (hours, -1 before a world)
void spawner_time_freeze(int on) { g_time_freeze = on; }
int spawner_time_frozen(void) { return g_time_freeze; }
void spawner_warp_all(int on) { g_warp_req = on; }
int spawner_warp_all_on(void) { return g_warp_on; }

static uint64_t time_mgr(void) {
    if (!g_site.world_mgr) return 0;
    uint64_t wm = LD64(g_site.world_mgr);
    if (!guest_ptr(wm) || !*(uint32_t *)(uintptr_t)(wm + 0x5c0)) return 0;
    uint64_t mgrs = LD64(wm + 0x5c8), tm = guest_ptr(mgrs) ? LD64(mgrs) : 0;
    return guest_ptr(tm) ? tm : 0;
}

static uint32_t bool_handle(Cpu *c, uint64_t sp, const char *name) {
    uint64_t mgr = LD64(got(GDTMGR_GOT_190));
    if (!guest_ptr(mgr)) return 0xffffffffu;
    uint64_t tabp = LD64(mgr + 0xbd8), tab = guest_ptr(tabp) ? LD64(tabp) : 0;
    if (!guest_ptr(tab)) return 0xffffffffu;
    uint32_t h = 0xffffffffu;
    for (const char *p = name; *p; p++) {
        h ^= (uint8_t)*p;
        for (int k = 0; k < 8; k++) h = h >> 1 ^ (0xedb88320u & -(h & 1));
    }
    uint64_t a[2] = {tab, ~h};
    uint32_t idx = (uint32_t)gcall(c, BOOLFIND_190, sp, a, 2);
    if (idx == 0xffffffffu || (idx & 0xff000000u)) return 0xffffffffu;
    return idx | *(uint32_t *)(uintptr_t)(mgr + 0xc28) << 24;
}

// the flags: every shrine (Dungeon000-136: found, opened, entered = its travel gate on) and tower (MapTower_01-15)
static char g_warp_names[512][40];
static int g_warp_n;
static uint8_t g_warp_before[512];
static int g_warp_saved;
static void warp_list(void) {
    if (g_warp_n) return;
    static const char *const pre[] = {"Location_Dungeon", "Open_Dungeon", "Enter_Dungeon"};
    for (int p = 0; p < 3; p++)
        for (int i = 0; i <= 136; i++) snprintf(g_warp_names[g_warp_n++], 40, "%s%03d", pre[p], i);
    for (int i = 1; i <= 15; i++) snprintf(g_warp_names[g_warp_n++], 40, "MapTower_%02d", i);
}

static void world_frame(Cpu *c) {
    uint64_t tm = time_mgr();
    if (tm) {
        g_time_now = *(float *)(uintptr_t)(tm + 0xb8) / 15.0f;
        float req = g_time_req;
        if (req >= 0) {
            g_time_req = -1;
            float v = req >= 24 ? 0 : req * 15.0f;
            *(float *)(uintptr_t)(tm + 0xc0) = v;
            snprintf(g_status, sizeof g_status, "Time set to %02d:%02d.", (int)req % 24, (int)((req - (int)req) * 60 + 0.5f) % 60);
            LOG("[spawner] time of day -> %.2f h\n", req);
        }
        static float step_saved = -1;
        float *step = (float *)(uintptr_t)(tm + 0xc4);
        if (g_time_freeze) {
            if (step_saved < 0 && *step > 0) step_saved = *step;
            *step = 0;
        } else if (step_saved >= 0) {
            *step = step_saved;
            step_saved = -1;
        }
    } else {
        g_time_now = -1;
    }
    int w = g_warp_req;
    if (w < 0) return;
    g_warp_req = -1;
    warp_list();
    uint64_t sp = (c->sp - 0x1000) & ~0xfull;
    int changed = 0, missing = 0;
    for (int i = 0; i < g_warp_n; i++) {
        uint32_t h = bool_handle(c, sp, g_warp_names[i]);
        if (h == 0xffffffffu) { missing++; continue; }
        if (w) {
            if (!g_warp_saved) {
                uint64_t a[2] = {h, 0};
                g_warp_before[i] = (uint8_t)(gcall(c, GETBOOL_190, sp, a, 2) & 1);
            }
            uint64_t b[3] = {1, h, 0};
            gcall(c, SETBOOL_190, sp, b, 3);
            changed++;
        } else if (g_warp_saved && !g_warp_before[i]) {
            uint64_t b[3] = {0, h, 0};
            gcall(c, SETBOOL_190, sp, b, 3);
            changed++;
        }
    }
    if (w) g_warp_saved = 1;
    else g_warp_saved = 0;
    g_warp_on = w;
    snprintf(g_status, sizeof g_status, w ? "Every shrine and tower is a fast travel point now (%d flags)." :
             "Fast travel points put back as they were (%d flags).", changed);
    LOG("[spawner] fast travel all %s: %d flags set, %d not found\n", w ? "on" : "off", changed, missing);
}
