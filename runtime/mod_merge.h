// Mods' Actor/ActorInfo.product.sbyml merged into one (runtime/mod_merge.c, used by romfs_build.c).
#pragma once
#include "byml.h"

// game_file: the game's table; mod_files: the mods' own, in mod order (later wins). Writes the merged table
// (Yaz0) to out_path. False if it couldn't.
int mod_merge_actorinfo(const char *game_file, const char *const *mod_files, int nmods, const char *out_path);
// a BYML file, Yaz0 or not
int mod_read_byml(const char *path, Byml *out);
