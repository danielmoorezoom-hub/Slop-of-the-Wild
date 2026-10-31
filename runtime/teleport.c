// Teleport (its own page in both menus): bookmarks the player saves, and the game's main places. The game has no way to move Link from
// here that is known to be safe, so going somewhere restarts the game, and the save is read with its position rewritten
// (runtime/services/test_warp.c) when the player presses Continue: the same way a test run starts elsewhere. The save itself isn't
// changed until the game next saves. Bookmarks are kept in teleport.txt next to the settings.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game_sites.h"
#include "hle.h"
#include "rt.h"
#include "teleport.h"

#define FILE_NAME "teleport.txt"

typedef struct { const char *name; float x, y, z; } Place;
static const Place places[] = {  // (the game's own start positions there: test_warp.c)
    {"Great Plateau", -1041.6f, 250.2f, 1812.1f}, {"Kakariko Village", 1811.7f, 218.5f, 982.6f},
    {"Riverside Stable", 384.8f, 121.7f, 1035.6f}, {"Hateno Village", 3437.5f, 235.9f, 2101.5f},
    {"Hyrule Castle", -254.0f, 140.6f, -544.6f},   {"Gerudo Desert", -3196.0f, 129.4f, 2549.3f},
    {"Gerudo Town", -3784.7f, 149.1f, 2864.5f},    {"Hebra", -2165.2f, 529.2f, -2029.5f},
    {"Zora's Domain", 3315.6f, 255.6f, -501.0f},   {"Goron City", 1719.0f, 498.9f, -2456.2f},
    {"Rito Village", -3607.2f, 286.3f, -1805.5f},
};
#define PLACES ((int)(sizeof places / sizeof places[0]))

static struct { bool set; float x, y, z; } marks[TELEPORT_BOOKMARKS];
static bool loaded;

static void load(void) {
    if (loaded) return;
    loaded = true;
    FILE *f = fopen(FILE_NAME, "r");
    int i;
    float x, y, z;
    while (f && fscanf(f, "%d %f %f %f", &i, &x, &y, &z) == 4)
        if (i >= 0 && i < TELEPORT_BOOKMARKS) marks[i].set = true, marks[i].x = x, marks[i].y = y, marks[i].z = z;
    if (f) fclose(f);
}

int teleport_place_count(void) { return PLACES; }
const char *teleport_place_name(int i) { return i >= 0 && i < PLACES ? places[i].name : ""; }

static void set_env(const char *name, const char *value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

void teleport_to(float x, float y, float z) {
    char w[96];
    snprintf(w, sizeof w, "%.2f,%.2f,%.2f", x, y, z);
    LOG("[teleport] to %s: restarting\n", w);
    set_env("SAVAGEWIND_TEST_WARP", w);
    set_env("SAVAGEWIND_WARP_ONCE", "1");
    {
        extern void input_auto_continue_arm(void);  // (so one press takes you there)
        input_auto_continue_arm();
    }
    rt_request_reboot(1);
}

void teleport_go_place(int i) {
    if (i >= 0 && i < PLACES) teleport_to(places[i].x, places[i].y, places[i].z);
}

bool teleport_bookmark_set(int i) {
    load();
    return i >= 0 && i < TELEPORT_BOOKMARKS && marks[i].set;
}

void teleport_go_bookmark(int i) {
    if (teleport_bookmark_set(i)) teleport_to(marks[i].x, marks[i].y, marks[i].z);
}

const char *teleport_bookmark_text(int i) {
    static char t[TELEPORT_BOOKMARKS][40];
    if (!teleport_bookmark_set(i)) return "";
    snprintf(t[i], sizeof t[i], "%.0f, %.0f", marks[i].x, marks[i].z);
    return t[i];
}

// Link's position (uking::act::PlayerInfo +0x88, as test_route.c reads it). false: no game loaded.
bool teleport_save_here(int i) {
    load();
    uint64_t pinfo = g_site.player_info ? *(uint64_t *)(uintptr_t)g_site.player_info : 0;
    if (!pinfo || i < 0 || i >= TELEPORT_BOOKMARKS) return false;
    float p[3];
    memcpy(p, (const void *)(uintptr_t)(pinfo + 0x88), sizeof p);
    if (p[0] == 0 && p[1] == 0 && p[2] == 0) return false;
    marks[i].set = true, marks[i].x = p[0], marks[i].y = p[1], marks[i].z = p[2];
    FILE *f = fopen(FILE_NAME, "w");
    for (int k = 0; f && k < TELEPORT_BOOKMARKS; k++)
        if (marks[k].set) fprintf(f, "%d %.2f %.2f %.2f\n", k, marks[k].x, marks[k].y, marks[k].z);
    if (f) fclose(f);
    LOG("[teleport] bookmark %d: %.1f %.1f %.1f\n", i + 1, p[0], p[1], p[2]);
    return true;
}

// (test_warp.c, once it has rewritten a save for a teleport) later loads, a death's say, read the save as it is
void teleport_done(void) {
    set_env("SAVAGEWIND_TEST_WARP", NULL);
    set_env("SAVAGEWIND_WARP_ONCE", NULL);
}
