// Teleport bookmarks and places (runtime/teleport.c)
#pragma once
#include <stdbool.h>

#define TELEPORT_BOOKMARKS 4

#ifdef __cplusplus
extern "C" {
#endif
int teleport_place_count(void);
const char *teleport_place_name(int i);
void teleport_go_place(int i);                // these restart the game: Continue lands there
void teleport_go_bookmark(int i);
void teleport_to(float x, float y, float z);
bool teleport_bookmark_set(int i);
const char *teleport_bookmark_text(int i);    // for showing: "x, z"
bool teleport_save_here(int i);               // false: no game loaded
void teleport_done(void);                     // (test_warp.c)
#ifdef __cplusplus
}
#endif
