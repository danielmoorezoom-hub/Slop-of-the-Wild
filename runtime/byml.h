// BYML (version 2-4, little endian, as on Switch): read into a tree and write back (runtime/byml.c). For merging mods'
// Actor/ActorInfo.product.sbyml (romfs_build.c) and listing the items mods add (spawner_mod_items.c).
#pragma once
#include <stddef.h>
#include <stdint.h>

enum {
    BY_STR = 0xa0, BY_ARRAY = 0xc0, BY_DICT = 0xc1, BY_BOOL = 0xd0, BY_INT = 0xd1, BY_FLOAT = 0xd2, BY_UINT = 0xd3,
    BY_INT64 = 0xd4, BY_UINT64 = 0xd5, BY_DOUBLE = 0xd6, BY_NULL = 0xff
};

typedef struct ByNode ByNode;
struct ByNode {
    uint8_t type;
    union { const char *s; uint32_t u32; uint64_t u64; } v;  // strings point into the tree's own copy
    int n;               // array / dict: count
    ByNode *items;       // array / dict: values
    const char **keys;   // dict: keys, sorted
};

typedef struct {
    ByNode root;
    char *strbuf;        // every string and key (one block)
    void *arena;         // nodes
} Byml;

// d: the uncompressed file. False if it isn't BYML as expected.
int byml_read(const uint8_t *d, size_t n, Byml *out);
void byml_free(Byml *b);
// a dict's value by key, or NULL
const ByNode *byml_get(const ByNode *dict, const char *key);
// the tree as a file (version 2); malloc'd
uint8_t *byml_write(const ByNode *root, size_t *out_n);
// two values the same (deep)
int byml_equal(const ByNode *a, const ByNode *b);
