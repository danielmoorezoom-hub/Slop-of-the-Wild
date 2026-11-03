// Items the game's table of actors has beyond the spawner's own list (runtime/ui/menu.mm tab_items): what mods add.
// Read from the Actor/ActorInfo.product.sbyml the game gets (romfs_build.c g_romfs_actorinfo): every actor with the
// CanGetPouch tag, its kind from its profile.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "byml.h"
#include "mod_merge.h"

extern char g_romfs_actorinfo[1200];

typedef struct { char id[64]; int cat, life; } ModItem;
static ModItem *g_items;
static int g_n = -1;

// kinds as the menu has them: 0 weapon, 1 bow, 2 arrows, 3 shield, 4 armour, 5 material, 6 food, 7 key item
static int kind_of(const char *profile, const char *name) {
    if (!strncmp(profile, "WeaponSmallSword", 16) || !strcmp(profile, "WeaponLargeSword") || !strcmp(profile, "WeaponSpear"))
        return 0;
    if (!strcmp(profile, "WeaponBow")) return 1;
    size_t nl = strlen(name);
    if (strstr(profile, "Arrow") || (nl >= 5 && !strcmp(name + nl - 5, "Arrow"))) return 2;
    if (!strcmp(profile, "WeaponShield")) return 3;
    if (!strncmp(profile, "Armor", 5)) return 4;
    if (!strncmp(name, "Item_Cook", 9) || !strncmp(name, "Item_Roast", 10) || !strncmp(name, "Item_Boiled", 11)) return 6;
    if (!strcmp(profile, "Item") || !strcmp(profile, "Ore")) return 5;
    return 7;
}

int spawner_mod_items(const ModItem **out) {
    if (g_n >= 0) { *out = g_items; return g_n; }
    g_n = 0;
    Byml b;
    if (!g_romfs_actorinfo[0] || !mod_read_byml(g_romfs_actorinfo, &b)) { *out = NULL; return 0; }
    const ByNode *acts = byml_get(&b.root, "Actors");
    int cap = 0;
    for (int i = 0; acts && acts->type == BY_ARRAY && i < acts->n; i++) {
        const ByNode *a = &acts->items[i], *tags = byml_get(a, "tags"), *nm = byml_get(a, "name"), *pr = byml_get(a, "profile");
        if (!tags || !nm || nm->type != BY_STR || !byml_get(tags, "tage0194f30")) continue;  // CanGetPouch
        if (strlen(nm->v.s) >= sizeof g_items[0].id) continue;
        if (g_n == cap) g_items = realloc(g_items, sizeof *g_items * (cap = cap ? cap * 2 : 512));
        ModItem *m = &g_items[g_n++];
        snprintf(m->id, sizeof m->id, "%s", nm->v.s);
        m->cat = kind_of(pr && pr->type == BY_STR ? pr->v.s : "", nm->v.s);
        const ByNode *life = byml_get(a, "generalLife");
        m->life = life && life->type == BY_INT ? (int)life->v.u32 : 0;
    }
    byml_free(&b);
    fprintf(stderr, "[spawner] %d pouch items in %s\n", g_n, g_romfs_actorinfo);
    *out = g_items;
    return g_n;
}
