// BYML read / write (byml.h). Version 2+ little endian: "YB", u16 version, then offsets of the hash key table, the
// string table and the root. A string table: 0xc2, u24 count, u32 offsets (count + 1) from the node. An array: 0xc0,
// u24 count, a type byte each (padded to 4), a u32 value each. A dict: 0xc1, u24 count, {u24 key index, u8 type,
// u32 value} sorted by key. Containers' values are offsets; 64-bit numbers are offsets to 8 bytes; strings are indices.
#include "byml.h"

#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint32_t rd24(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16; }

typedef struct {
    const uint8_t *d;
    size_t n;
    const char **keys, **strs;
    uint32_t nkeys, nstrs;
    ByNode *pool;
    size_t pool_n, pool_cap;
    int depth, bad;
} Rd;

static ByNode *alloc_nodes(Rd *r, size_t k) {
    if (r->pool_n + k > r->pool_cap) { r->bad = 1; return NULL; }
    ByNode *p = r->pool + r->pool_n;
    r->pool_n += k;
    return p;
}

static int is_container(uint8_t t) { return t == BY_ARRAY || t == BY_DICT; }

// nodes a container holds, all the way down (to size the pool)
static size_t count_nodes(const uint8_t *d, size_t n, uint32_t off, int depth) {
    if (depth > 64 || off + 4 > n) return 0;
    uint8_t t = d[off];
    uint32_t c = rd24(d + off + 1);
    size_t total = c;
    if (t == BY_ARRAY) {
        size_t vals = off + 4 + ((c + 3) & ~3u);
        if (vals + 4ull * c > n) return 0;
        for (uint32_t i = 0; i < c; i++)
            if (is_container(d[off + 4 + i])) total += count_nodes(d, n, rd32(d + vals + 4 * i), depth + 1);
    } else if (t == BY_DICT) {
        if (off + 4 + 8ull * c > n) return 0;
        for (uint32_t i = 0; i < c; i++) {
            const uint8_t *e = d + off + 4 + 8 * i;
            if (is_container(e[3])) total += count_nodes(d, n, rd32(e + 4), depth + 1);
        }
    }
    return total;
}

static void read_value(Rd *r, ByNode *out, uint8_t type, uint32_t v);

static void read_container(Rd *r, ByNode *out, uint32_t off) {
    if (++r->depth > 64 || off + 4 > r->n) { r->bad = 1; r->depth--; return; }
    uint8_t t = r->d[off];
    uint32_t c = rd24(r->d + off + 1);
    out->type = t, out->n = (int)c;
    out->items = c ? alloc_nodes(r, c) : NULL;
    if (c && !out->items) { r->depth--; return; }
    if (t == BY_ARRAY) {
        size_t vals = off + 4 + ((c + 3) & ~3u);
        if (vals + 4ull * c > r->n) { r->bad = 1; r->depth--; return; }
        for (uint32_t i = 0; i < c; i++) read_value(r, &out->items[i], r->d[off + 4 + i], rd32(r->d + vals + 4 * i));
    } else if (t == BY_DICT) {
        if (off + 4 + 8ull * c > r->n) { r->bad = 1; r->depth--; return; }
        out->keys = malloc(sizeof *out->keys * (c ? c : 1));
        for (uint32_t i = 0; i < c; i++) {
            const uint8_t *e = r->d + off + 4 + 8 * i;
            uint32_t k = rd24(e);
            out->keys[i] = k < r->nkeys ? r->keys[k] : "";
            read_value(r, &out->items[i], e[3], rd32(e + 4));
        }
    } else {
        r->bad = 1;
    }
    r->depth--;
}

static void read_value(Rd *r, ByNode *out, uint8_t type, uint32_t v) {
    memset(out, 0, sizeof *out);
    out->type = type;
    switch (type) {
    case BY_ARRAY: case BY_DICT: read_container(r, out, v); break;
    case BY_STR: out->v.s = v < r->nstrs ? r->strs[v] : ""; break;
    case BY_INT64: case BY_UINT64: case BY_DOUBLE:
        if (v + 8ull > r->n) { r->bad = 1; break; }
        memcpy(&out->v.u64, r->d + v, 8);
        break;
    default: out->v.u32 = v; break;
    }
}

static int read_strtab(const uint8_t *d, size_t n, uint32_t off, const char ***out, uint32_t *count, char **buf, size_t *bufn) {
    *out = NULL, *count = 0;
    if (!off) return 1;
    if (off + 4 > n || d[off] != 0xc2) return 0;
    uint32_t c = rd24(d + off + 1);
    if (off + 8ull + 4ull * c > n) return 0;
    const char **v = malloc(sizeof *v * (c ? c : 1));
    for (uint32_t i = 0; i < c; i++) {
        uint32_t s = off + rd32(d + off + 4 + 4 * i), e = off + rd32(d + off + 8 + 4 * i);
        if (e > n || s > e) { free(v); return 0; }
        size_t len = strnlen((const char *)d + s, e - s);
        *buf = realloc(*buf, *bufn + len + 1);  // (offsets, fixed up below)
        memcpy(*buf + *bufn, d + s, len);
        (*buf)[*bufn + len] = 0;
        v[i] = (const char *)(uintptr_t)*bufn;
        *bufn += len + 1;
    }
    *out = v, *count = c;
    return 1;
}

int byml_read(const uint8_t *d, size_t n, Byml *out) {
    memset(out, 0, sizeof *out);
    if (n < 16 || memcmp(d, "YB", 2)) return 0;
    uint16_t ver = (uint16_t)(d[2] | d[3] << 8);
    if (ver < 2 || ver > 7) return 0;
    uint32_t koff = rd32(d + 4), soff = rd32(d + 8), roff = rd32(d + 12);
    char *buf = NULL;
    size_t bufn = 0;
    Rd r = {d, n};
    if (!read_strtab(d, n, koff, &r.keys, &r.nkeys, &buf, &bufn) || !read_strtab(d, n, soff, &r.strs, &r.nstrs, &buf, &bufn)) {
        free(r.keys), free(r.strs), free(buf);
        return 0;
    }
    for (uint32_t i = 0; i < r.nkeys; i++) r.keys[i] = buf + (uintptr_t)r.keys[i];
    for (uint32_t i = 0; i < r.nstrs; i++) r.strs[i] = buf + (uintptr_t)r.strs[i];
    if (!roff || roff + 4 > n || !is_container(d[roff])) { free(r.keys), free(r.strs), free(buf); return 0; }
    r.pool_cap = count_nodes(d, n, roff, 0) + 1;
    r.pool = calloc(r.pool_cap, sizeof *r.pool);
    read_container(&r, &out->root, roff);
    free(r.keys), free(r.strs);
    out->strbuf = buf, out->arena = r.pool;
    if (r.bad) { byml_free(out); return 0; }
    return 1;
}

static void free_keys(ByNode *n) {
    if (!is_container(n->type)) return;
    for (int i = 0; i < n->n; i++) free_keys(&n->items[i]);
    free(n->keys);
    n->keys = NULL;
}

void byml_free(Byml *b) {
    if (b->arena) free_keys(&b->root);
    free(b->strbuf), free(b->arena);
    memset(b, 0, sizeof *b);
}

const ByNode *byml_get(const ByNode *dict, const char *key) {
    if (!dict || dict->type != BY_DICT) return NULL;
    int lo = 0, hi = dict->n;
    while (lo < hi) {
        int mid = (lo + hi) / 2, c = strcmp(dict->keys[mid], key);
        if (!c) return &dict->items[mid];
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    for (int i = 0; i < dict->n; i++)  // (not sorted after all)
        if (!strcmp(dict->keys[i], key)) return &dict->items[i];
    return NULL;
}

int byml_equal(const ByNode *a, const ByNode *b) {
    if (a->type != b->type) return 0;
    switch (a->type) {
    case BY_STR: return !strcmp(a->v.s, b->v.s);
    case BY_INT64: case BY_UINT64: case BY_DOUBLE: return a->v.u64 == b->v.u64;
    case BY_ARRAY: case BY_DICT:
        if (a->n != b->n) return 0;
        for (int i = 0; i < a->n; i++) {
            if (a->type == BY_DICT && strcmp(a->keys[i], b->keys[i])) return 0;
            if (!byml_equal(&a->items[i], &b->items[i])) return 0;
        }
        return 1;
    default: return a->v.u32 == b->v.u32;
    }
}

// ---- writing
typedef struct { uint8_t *d; size_t n, cap; } Buf;
static void need(Buf *b, size_t k) {
    if (b->n + k <= b->cap) return;
    while (b->n + k > b->cap) b->cap = b->cap ? b->cap * 2 : 1 << 16;
    b->d = realloc(b->d, b->cap);
}
static void put(Buf *b, const void *p, size_t k) { need(b, k); memcpy(b->d + b->n, p, k); b->n += k; }
static void put32(Buf *b, uint32_t v) { put(b, &v, 4); }
static void align4(Buf *b) { while (b->n & 3) { uint8_t z = 0; put(b, &z, 1); } }
static void set32(Buf *b, size_t at, uint32_t v) { memcpy(b->d + at, &v, 4); }

typedef struct { const char **v; size_t n, cap; } Names;
static void name_add(Names *s, const char *x) {
    if (s->n == s->cap) s->v = realloc(s->v, sizeof *s->v * (s->cap = s->cap ? s->cap * 2 : 1024));
    s->v[s->n++] = x;
}
static int cmp_str(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }
static void uniq(Names *s) {
    if (!s->n) return;
    qsort(s->v, s->n, sizeof *s->v, cmp_str);
    size_t k = 1;
    for (size_t i = 1; i < s->n; i++)
        if (strcmp(s->v[i], s->v[k - 1])) s->v[k++] = s->v[i];
    s->n = k;
}
static uint32_t name_index(const Names *s, const char *x) {
    size_t lo = 0, hi = s->n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = strcmp(s->v[mid], x);
        if (!c) return (uint32_t)mid;
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    return 0;
}
static void collect(const ByNode *n, Names *keys, Names *strs) {
    if (n->type == BY_STR) name_add(strs, n->v.s);
    if (!is_container(n->type)) return;
    for (int i = 0; i < n->n; i++) {
        if (n->type == BY_DICT) name_add(keys, n->keys[i]);
        collect(&n->items[i], keys, strs);
    }
}
static void write_strtab(Buf *b, const Names *s) {
    size_t at = b->n;
    put32(b, 0xc2u | (uint32_t)s->n << 8);
    size_t offs = b->n;
    need(b, 4 * (s->n + 1));
    b->n += 4 * (s->n + 1);
    for (size_t i = 0; i < s->n; i++) {
        set32(b, offs + 4 * i, (uint32_t)(b->n - at));
        put(b, s->v[i], strlen(s->v[i]) + 1);
    }
    set32(b, offs + 4 * s->n, (uint32_t)(b->n - at));
    align4(b);
}

static uint32_t write_node(Buf *b, const ByNode *n, const Names *keys, const Names *strs);
static uint32_t value_of(Buf *b, const ByNode *v, const Names *keys, const Names *strs) {
    switch (v->type) {
    case BY_ARRAY: case BY_DICT: return write_node(b, v, keys, strs);
    case BY_STR: return name_index(strs, v->v.s);
    case BY_INT64: case BY_UINT64: case BY_DOUBLE: {
        align4(b);
        uint32_t at = (uint32_t)b->n;
        put(b, &v->v.u64, 8);
        return at;
    }
    default: return v->v.u32;
    }
}
static uint32_t write_node(Buf *b, const ByNode *n, const Names *keys, const Names *strs) {
    align4(b);
    size_t at = b->n;
    put32(b, n->type | (uint32_t)n->n << 8);
    size_t vals;
    if (n->type == BY_ARRAY) {
        for (int i = 0; i < n->n; i++) put(b, &n->items[i].type, 1);
        align4(b);
        vals = b->n;
        need(b, 4ull * n->n);
        memset(b->d + b->n, 0, 4ull * n->n);
        b->n += 4ull * n->n;
        for (int i = 0; i < n->n; i++) {
            uint32_t v = value_of(b, &n->items[i], keys, strs);
            set32(b, vals + 4 * i, v);
        }
    } else {
        vals = b->n;
        need(b, 8ull * n->n);
        memset(b->d + b->n, 0, 8ull * n->n);
        b->n += 8ull * n->n;
        for (int i = 0; i < n->n; i++) {
            set32(b, vals + 8 * i, name_index(keys, n->keys[i]) | (uint32_t)n->items[i].type << 24);
            uint32_t v = value_of(b, &n->items[i], keys, strs);
            set32(b, vals + 8 * i + 4, v);
        }
    }
    return (uint32_t)at;
}

uint8_t *byml_write(const ByNode *root, size_t *out_n) {
    Names keys = {0}, strs = {0};
    collect(root, &keys, &strs);
    uniq(&keys), uniq(&strs);
    Buf b = {0};
    put(&b, "YB", 2);
    uint16_t ver = 2;
    put(&b, &ver, 2);
    put32(&b, 0), put32(&b, 0), put32(&b, 0);
    if (keys.n) { set32(&b, 4, (uint32_t)b.n); write_strtab(&b, &keys); }
    if (strs.n) { set32(&b, 8, (uint32_t)b.n); write_strtab(&b, &strs); }
    uint32_t r = write_node(&b, root, &keys, &strs);
    set32(&b, 12, r);
    free(keys.v), free(strs.v);
    *out_n = b.n;
    return b.d;
}
