// RomFS image from a folder of game files (runtime/romfs_build.c). The game reads its data through one raw RomFS
// image (romfs.bin, runtime/services/fs.c); players bring their own game files, and the start screen builds the image
// from their romfs/ folder.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Writes the RomFS image of the folder `dir` to `out`. progress (may be NULL): bytes of file data written so far and
// the total, now and then. false = failed (why: a short message).
bool romfs_build(const char *dir, const char *out, void (*progress)(uint64_t done, uint64_t total), char *why,
                 size_t why_size);

// Slop of the Wild: the same image's header and tables alone (romfs.idx, a few MB); the data is read from the folder
bool romfs_build_index(const char *dir, const char *out, void (*progress)(uint64_t done, uint64_t total), char *why,
                       size_t why_size);

// Reading an image: bin_path ("<root>/romfs.bin") itself, else <root>/romfs.idx with the data from <root>/romfs/.
// A handle (-1: neither); pread / size / close as for a file descriptor.
#include <sys/types.h>
int romfs_image_open(const char *bin_path);
ssize_t romfs_image_pread(int h, void *dst, size_t size, uint64_t off);
uint64_t romfs_image_size(int h);
void romfs_image_close(int h);

// Bytes of file data under dir (the image is about this plus its tables).
uint64_t romfs_dir_bytes(const char *dir);

// Copies the folder tree src to dst (hard links where the file system allows: no second copy of 6 GB). progress as
// above. false = failed.
bool romfs_copy_tree(const char *src, const char *dst, void (*progress)(uint64_t done, uint64_t total));

#ifdef __cplusplus
}
#endif
