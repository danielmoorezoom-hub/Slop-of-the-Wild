// RomFS image builder (romfs_build.h). Layout as the game's own image: a 0x50-byte header, file data
// from 0x200 (each file 16-byte aligned), then the directory hash table, directory entries, file hash table and file
// entries. Entries are found by hash (the game's RomFS code walks the tables itself), so the order inside them only
// has to be consistent: directories and files are sorted by name, children before siblings' children.
#include "romfs_build.h"

#include "platform.h"  // (Windows: POSIX mkdir)

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/clonefile.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif

#define EMPTY 0xFFFFFFFFu
#define DATA_START 0x200u

typedef struct Dir Dir;
typedef struct {
    char *name, *path;
    uint64_t size, data_off;
    uint32_t off;  // entry offset in the file table
    Dir *parent;
} File;
struct Dir {
    char *name, *path;
    Dir *parent;
    Dir **dirs;
    File **files;
    int ndirs, nfiles;
    uint32_t off;  // entry offset in the directory table
};

static int by_name_dir(const void *a, const void *b) { return strcmp((*(Dir *const *)a)->name, (*(Dir *const *)b)->name); }
static int by_name_file(const void *a, const void *b) { return strcmp((*(File *const *)a)->name, (*(File *const *)b)->name); }

static Dir *scan(const char *path, const char *name, Dir *parent, int depth) {
    if (depth > 32) return NULL;
    DIR *d = opendir(path);
    if (!d) return NULL;
    Dir *dir = calloc(1, sizeof *dir);
    dir->name = strdup(name), dir->path = strdup(path), dir->parent = parent;
    int cd = 0, cf = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;  // (".", "..", and hidden files such as .DS_Store aren't the game's)
        size_t n = strlen(path) + strlen(e->d_name) + 2;
        char *p = malloc(n);
        snprintf(p, n, "%s/%s", path, e->d_name);
        struct stat st;
        if (stat(p, &st) != 0) { free(p); continue; }
        if (S_ISDIR(st.st_mode)) {
            Dir *c = scan(p, e->d_name, dir, depth + 1);
            free(p);
            if (!c) continue;
            if (dir->ndirs == cd) dir->dirs = realloc(dir->dirs, sizeof *dir->dirs * (cd = cd ? cd * 2 : 8));
            dir->dirs[dir->ndirs++] = c;
        } else if (S_ISREG(st.st_mode)) {
            File *f = calloc(1, sizeof *f);
            f->name = strdup(e->d_name), f->path = p, f->size = (uint64_t)st.st_size, f->parent = dir;
            if (dir->nfiles == cf) dir->files = realloc(dir->files, sizeof *dir->files * (cf = cf ? cf * 2 : 16));
            dir->files[dir->nfiles++] = f;
        } else {
            free(p);
        }
    }
    closedir(d);
    if (dir->ndirs) qsort(dir->dirs, dir->ndirs, sizeof *dir->dirs, by_name_dir);
    if (dir->nfiles) qsort(dir->files, dir->nfiles, sizeof *dir->files, by_name_file);
    return dir;
}

static void free_tree(Dir *d) {
    if (!d) return;
    for (int i = 0; i < d->ndirs; i++) free_tree(d->dirs[i]);
    for (int i = 0; i < d->nfiles; i++) free(d->files[i]->name), free(d->files[i]->path), free(d->files[i]);
    free(d->dirs), free(d->files), free(d->name), free(d->path), free(d);
}

// flat lists, in table order
typedef struct { Dir **dirs; File **files; int nd, nf, cd, cf; } Lists;
static void flatten(Dir *d, Lists *l) {
    for (int i = 0; i < d->nfiles; i++) {
        if (l->nf == l->cf) l->files = realloc(l->files, sizeof *l->files * (l->cf = l->cf ? l->cf * 2 : 256));
        l->files[l->nf++] = d->files[i];
    }
    for (int i = 0; i < d->ndirs; i++) {
        if (l->nd == l->cd) l->dirs = realloc(l->dirs, sizeof *l->dirs * (l->cd = l->cd ? l->cd * 2 : 64));
        l->dirs[l->nd++] = d->dirs[i];
    }
    for (int i = 0; i < d->ndirs; i++) flatten(d->dirs[i], l);
}

// the RomFS name hash and bucket count (as the game's RomFS code computes them)
static uint32_t name_hash(uint32_t parent, const char *name) {
    uint32_t h = parent ^ 123456789u;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) h = ((h >> 5) | (h << 27)) ^ *p;
    return h;
}
static uint32_t bucket_count(uint32_t n) {
    if (n < 3) return 3;
    if (n < 19) return n | 1;
    uint32_t c = n;
    while (c % 2 == 0 || c % 3 == 0 || c % 5 == 0 || c % 7 == 0 || c % 11 == 0 || c % 13 == 0 || c % 17 == 0) c++;
    return c;
}

static uint32_t align4(uint32_t x) { return (x + 3) & ~3u; }
static uint64_t align16(uint64_t x) { return (x + 15) & ~15ull; }
static void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static void put64(uint8_t *p, uint64_t v) { memcpy(p, &v, 8); }

uint64_t romfs_dir_bytes(const char *dir) {
    Dir *t = scan(dir, "", NULL, 0);
    if (!t) return 0;
    Lists l = {0};
    flatten(t, &l);
    uint64_t n = 0;
    for (int i = 0; i < l.nf; i++) n += l.files[i]->size;
    free(l.dirs), free(l.files);
    free_tree(t);
    return n;
}

static bool copy_data(FILE *out, const char *path, uint64_t size, uint8_t *buf, size_t bufsize, uint64_t *done,
                      uint64_t total, void (*progress)(uint64_t, uint64_t)) {
    FILE *in = fopen(path, "rb");
    if (!in) return false;
    uint64_t left = size;
    while (left) {
        size_t want = left < bufsize ? (size_t)left : bufsize;
        size_t got = fread(buf, 1, want, in);
        if (got != want || fwrite(buf, 1, got, out) != got) { fclose(in); return false; }
        left -= got;
        *done += got;
        if (progress) progress(*done, total);
    }
    fclose(in);
    return true;
}

static bool build_impl(const char *dir, const char *out_path, void (*progress)(uint64_t, uint64_t), char *why,
                       size_t why_size, bool index_only) {
    #define FAIL(...) do { if (why) snprintf(why, why_size, __VA_ARGS__); goto fail; } while (0)
    bool ok = false;
    FILE *out = NULL;
    uint8_t *dtab = NULL, *ftab = NULL, *buf = NULL;
    uint32_t *dbuckets = NULL, *fbuckets = NULL;
    Lists l = {0};
    Dir *root = scan(dir, "", NULL, 0);
    if (!root) FAIL("can't read %s", dir);
    // table order: root, then each directory's subdirectories as flatten() lists them; files likewise
    if (l.nd == l.cd) l.dirs = realloc(l.dirs, sizeof *l.dirs * (l.cd = 64));
    l.dirs[l.nd++] = root;
    flatten(root, &l);
    if (!l.nf) FAIL("no files in %s", dir);

    // entry offsets
    uint32_t dsize = 0, fsize = 0;
    for (int i = 0; i < l.nd; i++) l.dirs[i]->off = dsize, dsize += 0x18 + align4((uint32_t)strlen(l.dirs[i]->name));
    for (int i = 0; i < l.nf; i++) l.files[i]->off = fsize, fsize += 0x20 + align4((uint32_t)strlen(l.files[i]->name));
    // file data offsets (relative to the data start), each 16-byte aligned
    uint64_t data = 0, total = 0;
    for (int i = 0; i < l.nf; i++) {
        data = align16(data);
        l.files[i]->data_off = data;
        data += l.files[i]->size;
        total += l.files[i]->size;
    }

    uint32_t nbd = bucket_count((uint32_t)l.nd), nbf = bucket_count((uint32_t)l.nf);
    dbuckets = malloc(4 * nbd), fbuckets = malloc(4 * nbf);
    for (uint32_t i = 0; i < nbd; i++) dbuckets[i] = EMPTY;
    for (uint32_t i = 0; i < nbf; i++) fbuckets[i] = EMPTY;
    dtab = calloc(1, dsize), ftab = calloc(1, fsize);
    for (int i = 0; i < l.nd; i++) {
        Dir *d = l.dirs[i];
        uint8_t *e = dtab + d->off;
        uint32_t parent = d->parent ? d->parent->off : 0, nl = (uint32_t)strlen(d->name);
        uint32_t sib = EMPTY;
        if (d->parent)
            for (int k = 0; k + 1 < d->parent->ndirs; k++)
                if (d->parent->dirs[k] == d) sib = d->parent->dirs[k + 1]->off;
        uint32_t h = name_hash(parent, d->name) % nbd;
        put32(e, parent);
        put32(e + 4, sib);
        put32(e + 8, d->ndirs ? d->dirs[0]->off : EMPTY);
        put32(e + 12, d->nfiles ? d->files[0]->off : EMPTY);
        put32(e + 16, dbuckets[h]);
        put32(e + 20, nl);
        memcpy(e + 0x18, d->name, nl);
        dbuckets[h] = d->off;
    }
    for (int i = 0; i < l.nf; i++) {
        File *f = l.files[i];
        uint8_t *e = ftab + f->off;
        uint32_t nl = (uint32_t)strlen(f->name), sib = EMPTY;
        for (int k = 0; k + 1 < f->parent->nfiles; k++)
            if (f->parent->files[k] == f) sib = f->parent->files[k + 1]->off;
        uint32_t h = name_hash(f->parent->off, f->name) % nbf;
        put32(e, f->parent->off);
        put32(e + 4, sib);
        put64(e + 8, f->data_off);
        put64(e + 16, f->size);
        put32(e + 24, fbuckets[h]);
        put32(e + 28, nl);
        memcpy(e + 0x20, f->name, nl);
        fbuckets[h] = f->off;
    }

    // header: tables after the file data (4-byte aligned)
    uint64_t dhash_off = (DATA_START + data + 3) & ~3ull;
    uint64_t dmeta_off = dhash_off + 4ull * nbd, fhash_off = dmeta_off + dsize, fmeta_off = fhash_off + 4ull * nbf;
    uint8_t hdr[DATA_START] = {0};
    uint64_t hv[10] = {0x50, dhash_off, 4ull * nbd, dmeta_off, dsize, fhash_off, 4ull * nbf, fmeta_off, fsize, DATA_START};
    memcpy(hdr, hv, sizeof hv);

    out = fopen(out_path, "wb");
    if (!out) FAIL("can't write %s", out_path);
    if (index_only) {  // Slop of the Wild: romfs.idx - the header and tables; the data stays in the romfs folder
        uint64_t image_size = fmeta_off + fsize, iv[3] = {0x49525753ull /* "SWRI" */ | (1ull << 32), image_size, dhash_off};
        uint64_t pad = dhash_off - ((DATA_START + data + 3) & ~3ull);  // (0: the tables start right there)
        (void)pad;
        if (fwrite(iv, 8, 3, out) != 3 || fwrite(hdr, 1, DATA_START, out) != DATA_START ||
            fwrite(dbuckets, 4, nbd, out) != nbd || fwrite(dtab, 1, dsize, out) != dsize ||
            fwrite(fbuckets, 4, nbf, out) != nbf || fwrite(ftab, 1, fsize, out) != fsize)
            FAIL("write failed (disk full?)");
        if (progress) progress(total, total);
        if (fclose(out) != 0) { out = NULL; FAIL("write failed (disk full?)"); }
        out = NULL;
        ok = true;
        goto fail;
    }
    if (fwrite(hdr, 1, DATA_START, out) != DATA_START) FAIL("write failed (disk full?)");
    size_t bufsize = 4u << 20;
    buf = malloc(bufsize);
    uint64_t pos = 0, done = 0;
    static const uint8_t zero[16];
    for (int i = 0; i < l.nf; i++) {
        File *f = l.files[i];
        if (f->data_off > pos && fwrite(zero, 1, (size_t)(f->data_off - pos), out) != f->data_off - pos)
            FAIL("write failed (disk full?)");
        if (!copy_data(out, f->path, f->size, buf, bufsize, &done, total, progress)) FAIL("can't copy %s", f->path);
        pos = f->data_off + f->size;
    }
    uint64_t tail = dhash_off - DATA_START - pos;
    if (tail && fwrite(zero, 1, (size_t)tail, out) != tail) FAIL("write failed (disk full?)");
    if (fwrite(dbuckets, 4, nbd, out) != nbd || fwrite(dtab, 1, dsize, out) != dsize ||
        fwrite(fbuckets, 4, nbf, out) != nbf || fwrite(ftab, 1, fsize, out) != fsize)
        FAIL("write failed (disk full?)");
    if (fclose(out) != 0) { out = NULL; FAIL("write failed (disk full?)"); }
    out = NULL;
    ok = true;
fail:
    if (out) fclose(out);
    if (!ok) remove(out_path);
    free(buf), free(dtab), free(ftab), free(dbuckets), free(fbuckets), free(l.dirs), free(l.files);
    free_tree(root);
    return ok;
    #undef FAIL
}

bool romfs_build(const char *dir, const char *out_path, void (*progress)(uint64_t, uint64_t), char *why, size_t why_size) {
    return build_impl(dir, out_path, progress, why, why_size, false);
}
bool romfs_build_index(const char *dir, const char *out_path, void (*progress)(uint64_t, uint64_t), char *why,
                       size_t why_size) {
    return build_impl(dir, out_path, progress, why, why_size, true);
}

// ------------------------------------------------------------------ reading an image: romfs.bin, or romfs.idx + romfs/
// A virtual image reads the header and tables from romfs.idx and each file's data from its own file in the romfs
// folder next to it (gaps read as zeros), so the game sees the same bytes romfs.bin would have.
#include <fcntl.h>
#include <pthread.h>
#ifndef O_BINARY
#define O_BINARY 0
#endif
typedef struct { uint64_t start, size; char *path; } VFile;
typedef struct {
    bool used;
    uint64_t size, tab_off;
    uint8_t hdr[DATA_START], *tabs;
    VFile *files;
    int nfiles;
    pthread_mutex_t lock;
    int cfd[64], cid[64];  // open data files, by file index % 64
} VImg;
enum { VBASE = 1 << 28, NVIMG = 4 };
static VImg g_vimg[NVIMG];
static pthread_mutex_t g_vimg_lock = PTHREAD_MUTEX_INITIALIZER;

static int by_start(const void *a, const void *b) {
    uint64_t x = ((const VFile *)a)->start, y = ((const VFile *)b)->start;
    return x < y ? -1 : x > y;
}

static int vimg_open(const char *idx_path) {
    FILE *f = fopen(idx_path, "rb");
    if (!f) return -1;
    uint64_t iv[3];
    VImg v = {0};
    if (fread(iv, 8, 3, f) != 3 || (uint32_t)iv[0] != 0x49525753u || fread(v.hdr, 1, DATA_START, f) != DATA_START ||
        iv[2] > iv[1] || iv[1] - iv[2] > (256u << 20)) {
        fclose(f);
        return -1;
    }
    v.size = iv[1], v.tab_off = iv[2];
    uint64_t tn = v.size - v.tab_off;
    v.tabs = malloc(tn ? tn : 1);
    if (fread(v.tabs, 1, tn, f) != tn) { fclose(f); free(v.tabs); return -1; }
    fclose(f);
    // the data files: <idx without .idx>/<dir>/<name>
    char base[1100];
    snprintf(base, sizeof base, "%s", idx_path);
    char *dot = strrchr(base, '.');
    if (dot) *dot = 0;
    uint64_t h[10];
    memcpy(h, v.hdr, sizeof h);
    if (h[3] < v.tab_off || h[7] < v.tab_off || h[3] + h[4] > v.size || h[7] + h[8] > v.size) { free(v.tabs); return -1; }
    const uint8_t *dm = v.tabs + (h[3] - v.tab_off), *fm = v.tabs + (h[7] - v.tab_off);
    uint64_t dsz = h[4], fsz = h[8];
    int cap = 1024;
    v.files = malloc(sizeof *v.files * cap);
    for (uint64_t o = 0; o + 0x20 <= fsz;) {
        uint32_t parent, nl;
        uint64_t data, size;
        memcpy(&parent, fm + o, 4), memcpy(&data, fm + o + 8, 8), memcpy(&size, fm + o + 0x10, 8), memcpy(&nl, fm + o + 0x1C, 4);
        if (o + 0x20 + nl > fsz) break;
        char parts[16][256], dir[1024] = "";
        int n = 0;
        for (uint32_t d = parent; d != 0 && d + 0x18 <= dsz && n < 16; n++) {
            uint32_t pp, dl;
            memcpy(&pp, dm + d, 4), memcpy(&dl, dm + d + 0x14, 4);
            if (dl > 255 || d + 0x18 + dl > dsz) break;
            memcpy(parts[n], dm + d + 0x18, dl), parts[n][dl] = 0;
            d = pp;
        }
        for (int i = n - 1; i >= 0; i--) { strncat(dir, parts[i], sizeof dir - strlen(dir) - 1); strncat(dir, "/", sizeof dir - strlen(dir) - 1); }
        char path[1500];
        snprintf(path, sizeof path, "%s/%s%.*s", base, dir, (int)nl, (const char *)fm + o + 0x20);
        if (v.nfiles == cap) v.files = realloc(v.files, sizeof *v.files * (cap *= 2));
        v.files[v.nfiles++] = (VFile){h[9] + data, size, strdup(path)};
        o += 0x20 + ((nl + 3) & ~3u);
    }
    qsort(v.files, v.nfiles, sizeof *v.files, by_start);
    for (int i = 0; i < 64; i++) v.cfd[i] = -1, v.cid[i] = -1;
    pthread_mutex_init(&v.lock, NULL);
    v.used = true;
    pthread_mutex_lock(&g_vimg_lock);
    int k = 0;
    while (k < NVIMG && g_vimg[k].used) k++;
    if (k == NVIMG) { pthread_mutex_unlock(&g_vimg_lock); return -1; }
    g_vimg[k] = v;
    pthread_mutex_unlock(&g_vimg_lock);
    fprintf(stderr, "[romfs] %s: %d files read from %s/ (no romfs.bin)\n", idx_path, v.nfiles, base);
    return VBASE + k;
}

int romfs_image_open(const char *bin_path) {
    int fd = open(bin_path, O_RDONLY | O_BINARY);
    if (fd >= 0) return fd;
    char idx[1100];
    snprintf(idx, sizeof idx, "%s", bin_path);
    size_t n = strlen(idx);
    if (n > 4 && !strcmp(idx + n - 4, ".bin")) strcpy(idx + n - 4, ".idx");
    else return -1;
    return vimg_open(idx);
}

int romfs_index_open(const char *idx_path) { return vimg_open(idx_path); }

static VImg *vimg(int h) { return h >= VBASE && h < VBASE + NVIMG && g_vimg[h - VBASE].used ? &g_vimg[h - VBASE] : NULL; }

ssize_t romfs_image_pread(int h, void *dst, size_t size, uint64_t off) {
    VImg *v = vimg(h);
    if (!v) return pread(h, dst, size, (off_t)off);
    uint8_t *out = dst;
    size_t done = 0;
    pthread_mutex_lock(&v->lock);
    while (done < size) {
        uint64_t o = off + done;
        size_t n = size - done;
        if (o >= v->size) break;
        if (n > v->size - o) n = (size_t)(v->size - o);
        if (o < DATA_START) {
            if (n > DATA_START - o) n = (size_t)(DATA_START - o);
            memcpy(out + done, v->hdr + o, n);
        } else if (o >= v->tab_off) {
            memcpy(out + done, v->tabs + (o - v->tab_off), n);
        } else {
            if (n > v->tab_off - o) n = (size_t)(v->tab_off - o);
            int lo = 0, hi = v->nfiles - 1, at = -1;  // the last file starting at or before o
            while (lo <= hi) {
                int mid = (lo + hi) / 2;
                if (v->files[mid].start <= o) at = mid, lo = mid + 1;
                else hi = mid - 1;
            }
            if (at >= 0 && o < v->files[at].start + v->files[at].size) {
                VFile *f = &v->files[at];
                uint64_t in = o - f->start;
                if (n > f->size - in) n = (size_t)(f->size - in);
                int c = at & 63;
                if (v->cid[c] != at) {
                    if (v->cfd[c] >= 0) close(v->cfd[c]);
                    v->cfd[c] = open(f->path, O_RDONLY | O_BINARY), v->cid[c] = at;
                    if (v->cfd[c] < 0) fprintf(stderr, "[romfs] cannot open %s\n", f->path);
                }
                ssize_t r = v->cfd[c] >= 0 ? pread(v->cfd[c], out + done, n, (off_t)in) : -1;
                if (r < 0) r = 0;
                if ((size_t)r < n) memset(out + done + r, 0, n - r);
            } else {  // padding between files
                uint64_t next = at + 1 < v->nfiles ? v->files[at + 1].start : v->tab_off;
                if (n > next - o) n = (size_t)(next - o);
                memset(out + done, 0, n);
            }
        }
        done += n;
    }
    pthread_mutex_unlock(&v->lock);
    return (ssize_t)done;
}

uint64_t romfs_image_size(int h) {
    VImg *v = vimg(h);
    if (v) return v->size;
    struct stat st;
    return fstat(h, &st) == 0 ? (uint64_t)st.st_size : 0;
}

void romfs_image_close(int h) {
    VImg *v = vimg(h);
    if (!v) { if (h >= 0) close(h); return; }
    pthread_mutex_lock(&g_vimg_lock);
    for (int i = 0; i < 64; i++) if (v->cfd[i] >= 0) close(v->cfd[i]);
    for (int i = 0; i < v->nfiles; i++) free(v->files[i].path);
    free(v->files), free(v->tabs);
    pthread_mutex_destroy(&v->lock);
    memset(v, 0, sizeof *v);
    pthread_mutex_unlock(&g_vimg_lock);
}

// ------------------------------------------------------------------ copying the player's folder
static bool copy_file(const char *src, const char *dst, uint64_t size, uint64_t *done, uint64_t total,
                      void (*progress)(uint64_t, uint64_t)) {
    remove(dst);
#ifdef __APPLE__
    if (clonefile(src, dst, 0) == 0) goto linked;  // (APFS: a copy-on-write clone, instant)
#endif
#ifdef _WIN32
    if (CreateHardLinkA(dst, src, NULL)) goto linked;  // (same NTFS volume: a hard link)
#else
    if (link(src, dst) == 0) goto linked;  // (same volume: a hard link)
#endif
    {
        FILE *in = fopen(src, "rb"), *out = in ? fopen(dst, "wb") : NULL;
        bool ok = in && out;
        static uint8_t buf[1 << 20];
        while (ok) {
            size_t n = fread(buf, 1, sizeof buf, in);
            if (!n) break;
            if (fwrite(buf, 1, n, out) != n) ok = false;
            *done += n;
            if (progress) progress(*done, total);
        }
        if (in) fclose(in);
        if (out && fclose(out) != 0) ok = false;
        return ok;
    }
linked:
    *done += size;
    if (progress) progress(*done, total);
    return true;
}

static bool copy_dir(Dir *d, const char *dst, uint64_t *done, uint64_t total, void (*progress)(uint64_t, uint64_t)) {
    mkdir(dst, 0755);
    for (int i = 0; i < d->nfiles; i++) {
        size_t n = strlen(dst) + strlen(d->files[i]->name) + 2;
        char *p = malloc(n);
        snprintf(p, n, "%s/%s", dst, d->files[i]->name);
        bool ok = copy_file(d->files[i]->path, p, d->files[i]->size, done, total, progress);
        free(p);
        if (!ok) return false;
    }
    for (int i = 0; i < d->ndirs; i++) {
        size_t n = strlen(dst) + strlen(d->dirs[i]->name) + 2;
        char *p = malloc(n);
        snprintf(p, n, "%s/%s", dst, d->dirs[i]->name);
        bool ok = copy_dir(d->dirs[i], p, done, total, progress);
        free(p);
        if (!ok) return false;
    }
    return true;
}

bool romfs_copy_tree(const char *src, const char *dst, void (*progress)(uint64_t, uint64_t)) {
    Dir *t = scan(src, "", NULL, 0);
    if (!t) return false;
    Lists l = {0};
    flatten(t, &l);
    uint64_t total = 0, done = 0;
    for (int i = 0; i < l.nf; i++) total += l.files[i]->size;
    bool ok = copy_dir(t, dst, &done, total, progress);
    free(l.dirs), free(l.files);
    free_tree(t);
    return ok;
}
