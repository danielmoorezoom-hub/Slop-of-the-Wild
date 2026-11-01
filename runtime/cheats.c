// Cheats (their own page in both menus; settings.h cheat_*): what the game code asks through tools/patches.py _cheat_*, _no_wear.
// Each is off by default and changes nothing in the save but the values the game then saves itself. The sites are from Savage Wind's
// pull requests #25 and #296, found again in 1.9.0.
#include <stdint.h>
#include <stdlib.h>

#include "hle.h"
#include "settings.h"

// (the log says once that each one did something: how a test run knows)
static void note(int cheat) {
    static const char *const names[] = {"infinite health", "infinite stamina", "infinite arrows", "unbreakable weapons", "one-hit kills"};
    static int said;
    if (said & (1 << cheat)) return;
    said |= 1 << cheat;
    LOG("[cheats] %s: kept a value\n", names[cheat]);
}

int rt_cheat_health(void) { return g_settings.cheat_health; }

// a lower life or a loss about to be stored, undone: 1 keeps the old one
int rt_cheat_keep_life(int32_t now, int32_t old) {
    if (!g_settings.cheat_health || now >= old) return 0;
    note(0);
    return 1;
}

// The life after a hit, with Cheats > Infinite health and Damage taken: healing as it is
int32_t rt_cheat_life_after(int32_t now, int32_t old) {
    if (now >= old) return now;
    if (rt_cheat_keep_life(now, old)) return old;
    if (g_settings.damage_taken == 100) return now;
    int64_t v = old - ((int64_t)(old - now) * g_settings.damage_taken + 50) / 100;
    if (v == old) v = old - 1;  // (a hit is always felt)
    return v < 0 ? 0 : (int32_t)v;
}

// a lower stamina about to be stored (float bits): 1 keeps the old one. (A NaN isn't lower: stored as the game has it.)
int rt_cheat_keep_stamina(uint32_t now_bits, uint32_t old_bits) {
    union { uint32_t u; float f; } now = {.u = now_bits}, old = {.u = old_bits};
    if (!g_settings.cheat_stamina || !(now.f < old.f)) return 0;
    note(1);
    return 1;
}

int rt_cheat_arrows(void) {
    if (!g_settings.cheat_arrows) return 0;
    note(2);
    return 1;
}

// The pouch's setter for an equipped item (type: 0 weapon, 1 bow, 2 arrows, 3 shield): a lower value is wear, kept. Arrows have
// their own cheat.
int rt_keep_wear(uint32_t type, int32_t now, int32_t old) {
    if (!g_settings.cheat_weapons || type == 2 || now >= old) return 0;
    note(3);
    return 1;
}

// The weapon actor's own copy of the durability: a hit costs nothing
int rt_no_wear(void) {
    if (!g_settings.cheat_weapons) return 0;
    note(3);
    return 1;
}

// Every life taken off, through one method of the game's: a weapon's wear (the call from the weapon's update, wear_ret) and hits on others.
// (test) SAVAGEWIND_HIT_PROBE=1: each caller once in a while, with the value and the cost
uint32_t rt_wear_cost(uint32_t cost);
uint32_t rt_hit_cost(uint64_t ret, uint64_t wear_ret, uint64_t enemy_ret, uint32_t cost, uint32_t value) {
    static int probe = -1;
    if (probe < 0) probe = getenv("SAVAGEWIND_HIT_PROBE") != NULL;
    if (probe && cost) {
        static struct { uint64_t ret; int n; } seen[32];
        for (int i = 0; i < 32; i++) {
            if (seen[i].ret && seen[i].ret != ret) continue;
            seen[i].ret = ret;
            if (seen[i].n++ < 12) LOG("[hit] from %llx: value %u, cost %u\n", (unsigned long long)ret, value, cost);
            break;
        }
    }
    if (ret == wear_ret) return rt_wear_cost(cost);
    if (ret == enemy_ret && cost) {  // (Cheats > One-hit kills, Damage dealt)
        if (g_settings.cheat_onehit) {
            note(4);
            return value > cost ? value : cost;
        }
        if (g_settings.damage_dealt != 100) {
            uint64_t v = (uint64_t)cost * g_settings.damage_dealt / 100;
            return v ? (uint32_t)v : 1;
        }
    }
    return cost;
}

// A hit's cost on it, in hundredths
uint32_t rt_wear_cost(uint32_t cost) {
    if (rt_no_wear()) return 0;
    return g_settings.weapon_wear == 100 ? cost : (uint32_t)((uint64_t)cost * g_settings.weapon_wear / 100);
}
