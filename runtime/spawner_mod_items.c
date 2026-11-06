// Items the game's table of actors has beyond the spawner's own list (runtime/ui/menu.mm tab_items): what mods add.
// Read from the Actor/ActorInfo.product.sbyml the game gets (romfs_build.c g_romfs_actorinfo): every actor with the
// CanGetPouch tag, its kind from its profile.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <sys/stat.h>

#include "byml.h"
#include "services/archive.h"
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

// Armour that upgrades further (a fairy fountain's next rank: ActorInfo armorNextRankName), read with the list below
static char (*g_lower)[64];
static int g_nlower;
static int cmp_name(const void *a, const void *b) { return strcmp(a, b); }
int spawner_mod_items(const ModItem **out);
int spawner_item_upgradable(const char *id) {
    const ModItem *m;
    spawner_mod_items(&m);
    return g_nlower && bsearch(id, g_lower, (size_t)g_nlower, sizeof *g_lower, cmp_name) != NULL;
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
        const ByNode *next = byml_get(a, "armorNextRankName");
        if (nm && nm->type == BY_STR && next && next->type == BY_STR && next->v.s[0] && strlen(nm->v.s) < 64) {
            static int lcap;
            if (g_nlower == lcap) g_lower = realloc(g_lower, sizeof *g_lower * (size_t)(lcap = lcap ? lcap * 2 : 256));
            snprintf(g_lower[g_nlower++], 64, "%s", nm->v.s);
        }
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
    if (g_nlower) qsort(g_lower, (size_t)g_nlower, sizeof *g_lower, cmp_name);
    fprintf(stderr, "[spawner] %d pouch items in %s (%d armour pieces upgrade further)\n", g_n, g_romfs_actorinfo, g_nlower);
    *out = g_items;
    return g_n;
}

// ------------------------------------------------------------------ items' names as the inventory shows them
// The game's text: Pack/Bootup_EUen.pack (USen if not) as the mods leave it (the last mod by name that has it, as
// romfs_build.c lays mods out, else the game's own), Message/Msg_EUen.product.ssarc in it, every MSBT's "<actor>_Name".
// MSBT: "MsgStdBn", sections from 0x20 (magic, size, 8 bytes, data, to 16 bytes); LBL1: buckets {count, offset} then
// labels {u8 length, name, u32 index}; TXT2: count, offsets, UTF-16LE strings with 0x0E tags (group, type, size, data).
typedef struct { char id[64]; char name[96]; } ItemName;
static ItemName *g_names;
static int g_nnames = -1;
static int cmp_iname(const void *a, const void *b) { return strcmp(((const ItemName *)a)->id, ((const ItemName *)b)->id); }
static uint8_t *read_file(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = len > 0 ? malloc((size_t)len) : NULL;
    if (d && fread(d, 1, (size_t)len, f) != (size_t)len) free(d), d = NULL;
    fclose(f);
    *n = d ? (size_t)len : 0;
    return d;
}
static int text_pack(const char *lang, char *out, size_t cap) {
    char best[256] = "";
    DIR *dir = opendir("mods");
    struct dirent *e;
    while (dir && (e = readdir(dir))) {
        if (e->d_name[0] == '.') continue;
        char p[1200];
        struct stat st;
        snprintf(p, sizeof p, "mods/%s/romfs/Pack/Bootup_%s.pack", e->d_name, lang);
        if (stat(p, &st) == 0 && strcmp(e->d_name, best) > 0) snprintf(best, sizeof best, "%s", e->d_name);
    }
    if (dir) closedir(dir);
    struct stat st;
    if (best[0]) snprintf(out, cap, "mods/%s/romfs/Pack/Bootup_%s.pack", best, lang);
    else snprintf(out, cap, "extracted/romfs/Pack/Bootup_%s.pack", lang);
    return stat(out, &st) == 0;
}
static void utf16_to_utf8(const uint8_t *s, size_t max, char *out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; i + 1 < max; i += 2) {
        uint32_t c = s[i] | s[i + 1] << 8;
        if (!c) break;
        if (c == 0x0E) {  // a tag: group, type, size, then that many bytes
            if (i + 8 > max) break;
            uint16_t sz = s[i + 6] | s[i + 7] << 8;
            i += 6 + sz;
            continue;
        }
        if (c == 0x0F) { i += 4; continue; }  // (a tag's end)
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < max) {
            uint32_t lo = s[i + 2] | s[i + 3] << 8;
            c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
            i += 2;
        }
        char b[4];
        int n = c < 0x80 ? (b[0] = (char)c, 1)
              : c < 0x800 ? (b[0] = (char)(0xC0 | c >> 6), b[1] = (char)(0x80 | (c & 0x3F)), 2)
              : c < 0x10000 ? (b[0] = (char)(0xE0 | c >> 12), b[1] = (char)(0x80 | (c >> 6 & 0x3F)), b[2] = (char)(0x80 | (c & 0x3F)), 3)
              : (b[0] = (char)(0xF0 | c >> 18), b[1] = (char)(0x80 | (c >> 12 & 0x3F)), b[2] = (char)(0x80 | (c >> 6 & 0x3F)), b[3] = (char)(0x80 | (c & 0x3F)), 4);
        if (o + (size_t)n >= cap) break;
        memcpy(out + o, b, (size_t)n), o += (size_t)n;
    }
    out[o] = 0;
}
static void msbt_names(const uint8_t *d, size_t n, int *cap) {
    if (n < 0x20 || memcmp(d, "MsgStdBn", 8) || d[8] != 0xFF) return;
    uint16_t nsec = d[0xE] | d[0xF] << 8;
    const uint8_t *lbl = NULL, *txt = NULL;
    size_t lbl_n = 0, txt_n = 0, p = 0x20;
    for (int k = 0; k < nsec && p + 16 <= n; k++) {
        uint32_t sz;
        memcpy(&sz, d + p + 4, 4);
        if (p + 16 + sz > n) break;
        if (!memcmp(d + p, "LBL1", 4)) lbl = d + p + 16, lbl_n = sz;
        if (!memcmp(d + p, "TXT2", 4)) txt = d + p + 16, txt_n = sz;
        p = (p + 16 + sz + 15) & ~(size_t)15;
    }
    if (!lbl || !txt || lbl_n < 4 || txt_n < 4) return;
    uint32_t buckets, count;
    memcpy(&buckets, lbl, 4), memcpy(&count, txt, 4);
    for (uint32_t b = 0; b < buckets && 4 + b * 8 + 8 <= lbl_n; b++) {
        uint32_t cnt, off;
        memcpy(&cnt, lbl + 4 + b * 8, 4), memcpy(&off, lbl + 8 + b * 8, 4);
        for (uint32_t j = 0; j < cnt && off < lbl_n; j++) {
            uint8_t len = lbl[off];
            if (off + 1 + len + 4 > lbl_n) break;
            const char *lab = (const char *)lbl + off + 1;
            uint32_t idx;
            memcpy(&idx, lbl + off + 1 + len, 4);
            off += 1 + len + 4;
            if (len < 6 || len - 5 >= 64 || memcmp(lab + len - 5, "_Name", 5) || idx >= count || 4 + idx * 4 + 4 > txt_n) continue;
            uint32_t so, se = idx + 1 < count ? 0 : (uint32_t)txt_n;
            memcpy(&so, txt + 4 + idx * 4, 4);
            if (!se) memcpy(&se, txt + 4 + (idx + 1) * 4, 4);
            if (so >= txt_n || se > txt_n || se <= so) continue;
            if (g_nnames == *cap) g_names = realloc(g_names, sizeof *g_names * (size_t)(*cap = *cap ? *cap * 2 : 1024));
            ItemName *it = &g_names[g_nnames];
            memcpy(it->id, lab, (size_t)(len - 5)), it->id[len - 5] = 0;
            utf16_to_utf8(txt + so, se - so, it->name, sizeof it->name);
            if (it->name[0]) g_nnames++;
        }
    }
}
static void load_names(void) {
    g_nnames = 0;
    static const char *langs[2] = {"EUen", "USen"};
    for (int l = 0; l < 2 && !g_nnames; l++) {
        char path[1300], inner[64];
        if (!text_pack(langs[l], path, sizeof path)) continue;
        size_t pn = 0, mn = 0;
        uint8_t *pack = read_file(path, &pn), *msg = NULL;
        uint32_t doff, s0, s1;
        snprintf(inner, sizeof inner, "Message/Msg_%s.product.ssarc", langs[l]);
        if (pack && sarc_find(pack, pn, inner, &doff, &s0, &s1) >= 0 && s1 <= pn - doff)
            msg = yaz0_unpack(pack + doff + s0, s1 - s0, &mn);
        int cap = 0, cnt = msg ? sarc_count(msg, mn) : -1;
        for (int i = 0; i < cnt; i++) {
            const char *name;
            const uint8_t *data;
            size_t size;
            if (sarc_node(msg, mn, i, &name, &data, &size) && name && !strncmp(name, "ActorType/", 10))
                msbt_names(data, size, &cap);
        }
        free(pack), free(msg);
        fprintf(stderr, "[spawner] %d item names from %s\n", g_nnames, path);
    }
    if (g_nnames > 1) qsort(g_names, (size_t)g_nnames, sizeof *g_names, cmp_iname);
}
// The inventory's name of an item (UTF-8), or NULL
const char *spawner_item_name(const char *id) {
    if (g_nnames < 0) load_names();
    ItemName key;
    snprintf(key.id, sizeof key.id, "%s", id);
    const ItemName *it = g_nnames > 0 ? bsearch(&key, g_names, (size_t)g_nnames, sizeof *g_names, cmp_iname) : NULL;
    return it ? it->name : NULL;
}
