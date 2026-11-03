// Merging mods' Actor/ActorInfo.product.sbyml (mod_merge.h). Every mod that adds an item, or changes one's tags, ships
// the whole table, and only one file can win: so the table the game reads is the game's own with each mod's changes
// laid over it, in mod order (later mods win where two change the same value). A mod's change is whatever differs
// from the game's table: new actors, and inside an actor every value (dicts like "tags" value by value) that isn't
// the game's. The actors stay sorted by the CRC32 of their names, "Hashes" alongside, as the game looks them up.
#include "mod_merge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "byml.h"
#include "services/archive.h"

static uint8_t *read_all(const char *path, size_t *n) {
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

int mod_read_byml(const char *path, Byml *out) {
    size_t zn = 0, n = 0;
    uint8_t *z = read_all(path, &zn), *d = z && zn >= 4 && !memcmp(z, "Yaz0", 4) ? yaz0_unpack(z, zn, &n) : NULL;
    int ok = 0;
    if (d) ok = byml_read(d, n, out);
    else if (z) ok = byml_read(z, zn, out);
    free(z), free(d);
    return ok;
}

static uint32_t crc32s(const char *s) {
    uint32_t c = 0xffffffffu;
    for (; *s; s++) {
        c ^= (uint8_t)*s;
        for (int k = 0; k < 8; k++) c = c >> 1 ^ (0xedb88320u & -(c & 1));
    }
    return ~c;
}

// nodes made while merging, freed together
typedef struct { void **p; size_t n, cap; } Allocs;
static void *keep(Allocs *a, void *p) {
    if (a->n == a->cap) a->p = realloc(a->p, sizeof *a->p * (a->cap = a->cap ? a->cap * 2 : 256));
    return a->p[a->n++] = p;
}

static int cmp_key(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

// cur: what the merge has so far; van: the game's; mod: this mod's. Dicts merge value by value, anything else is the
// mod's when it isn't the game's.
static ByNode merge(const ByNode *cur, const ByNode *van, const ByNode *mod, Allocs *al) {
    if (van && byml_equal(van, mod)) return *cur;  // (the mod left it as it was)
    if (!(cur->type == BY_DICT && mod->type == BY_DICT && (!van || van->type == BY_DICT))) return *mod;
    // keys: cur's and mod's
    const char **keys = keep(al, malloc(sizeof *keys * (size_t)(cur->n + mod->n + 1)));
    int nk = 0;
    for (int i = 0; i < cur->n; i++) keys[nk++] = cur->keys[i];
    for (int i = 0; i < mod->n; i++)
        if (!byml_get(cur, mod->keys[i])) keys[nk++] = mod->keys[i];
    qsort(keys, (size_t)nk, sizeof *keys, cmp_key);
    ByNode out = {BY_DICT};
    out.n = nk, out.keys = keys;
    out.items = keep(al, calloc((size_t)(nk ? nk : 1), sizeof *out.items));
    for (int i = 0; i < nk; i++) {
        const ByNode *c = byml_get(cur, keys[i]), *v = van ? byml_get(van, keys[i]) : NULL, *m = byml_get(mod, keys[i]);
        if (!m) out.items[i] = *c;
        else if (!c) out.items[i] = *m;
        else out.items[i] = merge(c, v, m, al);
    }
    return out;
}

typedef struct { const char *name; uint32_t hash; ByNode node; } Actor;
static int cmp_actor(const void *a, const void *b) {
    const Actor *x = a, *y = b;
    return x->hash < y->hash ? -1 : x->hash > y->hash;
}
static const char *actor_name(const ByNode *a) {
    const ByNode *n = byml_get(a, "name");
    return n && n->type == BY_STR ? n->v.s : NULL;
}
static int find(const Actor *v, int n, uint32_t h, const char *name) {
    int lo = 0, hi = n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (v[mid].hash < h) lo = mid + 1;
        else hi = mid;
    }
    for (; lo < n && v[lo].hash == h; lo++)
        if (!strcmp(v[lo].name, name)) return lo;
    return -1;
}

// Yaz0 with literal bytes only (a 0xFF flag before every 8): what the game's decoder reads, no search needed
static uint8_t *yaz0_literal(const uint8_t *d, size_t n, size_t *out_n) {
    uint8_t *o = malloc(16 + n + (n + 7) / 8), *w = o;
    memcpy(w, "Yaz0", 4);
    w[4] = (uint8_t)(n >> 24), w[5] = (uint8_t)(n >> 16), w[6] = (uint8_t)(n >> 8), w[7] = (uint8_t)n;
    memset(w + 8, 0, 8);
    w += 16;
    for (size_t i = 0; i < n; i += 8) {
        size_t k = n - i < 8 ? n - i : 8;
        *w++ = 0xff;
        memcpy(w, d + i, k);
        w += k;
    }
    *out_n = (size_t)(w - o);
    return o;
}

int mod_merge_actorinfo(const char *game_file, const char *const *mod_files, int nmods, const char *out_path) {
    Byml van;
    if (!mod_read_byml(game_file, &van)) {
        fprintf(stderr, "[mods] ActorInfo: can't read the game's %s\n", game_file);
        return 0;
    }
    const ByNode *va = byml_get(&van.root, "Actors");
    if (!va || va->type != BY_ARRAY) { byml_free(&van); return 0; }
    int n = va->n, cap = n + 1024;
    Actor *game = malloc(sizeof *game * (size_t)n), *res = malloc(sizeof *res * (size_t)cap);
    int ng = 0;
    for (int i = 0; i < va->n; i++) {
        const char *nm = actor_name(&va->items[i]);
        if (nm) game[ng++] = (Actor){nm, crc32s(nm), va->items[i]};
    }
    qsort(game, (size_t)ng, sizeof *game, cmp_actor);
    memcpy(res, game, sizeof *game * (size_t)ng);
    int nr = ng;
    Byml *mods = calloc((size_t)nmods, sizeof *mods);
    Allocs al = {0};
    int changed = 0, added = 0, used = 0;
    for (int m = 0; m < nmods; m++) {
        if (!mod_read_byml(mod_files[m], &mods[m])) {
            fprintf(stderr, "[mods] ActorInfo: can't read %s, left out\n", mod_files[m]);
            continue;
        }
        const ByNode *ma = byml_get(&mods[m].root, "Actors");
        if (!ma || ma->type != BY_ARRAY) continue;
        used++;
        int new_here = 0, sorted_n = nr;  // (res is sorted up to sorted_n; this mod's new ones after)
        for (int i = 0; i < ma->n; i++) {
            const ByNode *a = &ma->items[i];
            const char *nm = actor_name(a);
            if (!nm) continue;
            uint32_t h = crc32s(nm);
            int g = find(game, ng, h, nm);
            int r = find(res, sorted_n, h, nm);
            for (int k = sorted_n; r < 0 && k < nr; k++)
                if (res[k].hash == h && !strcmp(res[k].name, nm)) r = k;
            if (r < 0) {  // (new: kept sorted at the end, once this mod is done)
                if (nr == cap) res = realloc(res, sizeof *res * (size_t)(cap *= 2));
                res[nr++] = (Actor){nm, h, *a};
                added++, new_here = 1;
                continue;
            }
            if (g >= 0 && byml_equal(&game[g].node, a)) continue;
            res[r].node = merge(&res[r].node, g >= 0 ? &game[g].node : NULL, a, &al);
            changed++;
        }
        if (new_here) qsort(res, (size_t)nr, sizeof *res, cmp_actor);
    }
    // the table: {Actors, Hashes}, the other keys as the game's
    ByNode *items = keep(&al, calloc((size_t)nr, sizeof *items)), *hashes = keep(&al, calloc((size_t)nr, sizeof *hashes));
    for (int i = 0; i < nr; i++) {
        items[i] = res[i].node;
        hashes[i].type = res[i].hash < 0x80000000u ? BY_INT : BY_UINT;
        hashes[i].v.u32 = res[i].hash;
    }
    ByNode root = van.root;
    root.items = keep(&al, calloc((size_t)root.n, sizeof *root.items));
    memcpy(root.items, van.root.items, sizeof *root.items * (size_t)root.n);
    for (int i = 0; i < root.n; i++) {
        if (!strcmp(root.keys[i], "Actors")) root.items[i] = (ByNode){BY_ARRAY, {0}, nr, items, NULL};
        else if (!strcmp(root.keys[i], "Hashes")) root.items[i] = (ByNode){BY_ARRAY, {0}, nr, hashes, NULL};
    }
    size_t bn = 0, zn = 0;
    uint8_t *b = byml_write(&root, &bn), *z = yaz0_literal(b, bn, &zn);
    FILE *f = fopen(out_path, "wb");
    int ok = f && fwrite(z, 1, zn, f) == zn;
    if (f) fclose(f);
    free(b), free(z);
    fprintf(stderr, "[mods] ActorInfo: %d mods' tables merged: %d actors added, %d changed (%s)%s\n", used, added, changed,
            out_path, ok ? "" : ": couldn't write it");
    for (size_t i = 0; i < al.n; i++) free(al.p[i]);
    free(al.p);
    for (int m = 0; m < nmods; m++) byml_free(&mods[m]);
    free(mods), free(game), free(res);
    byml_free(&van);
    return ok;
}
