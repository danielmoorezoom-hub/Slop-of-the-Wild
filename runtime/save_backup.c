// A copy of the save folder before each game start: save-backups/<date and time>/, the newest SAVE_BACKUPS kept. Skipped for test runs
// and benchmarks, which use a scratch save folder, and when nothing has changed since the last copy (the newest file's time, in last.txt).
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "hle.h"
#include "settings.h"

#define DIR_BACKUPS "save-backups"
#define SAVE_BACKUPS 5

static SDL_EnumerationResult SDLCALL newest(void *out, const char *dir, const char *name) {
    char p[1024];
    snprintf(p, sizeof p, "%s%s", dir, name);
    SDL_PathInfo in;
    if (!SDL_GetPathInfo(p, &in)) return SDL_ENUM_CONTINUE;
    if (in.type == SDL_PATHTYPE_DIRECTORY) SDL_EnumerateDirectory(p, newest, out);
    else if (in.modify_time > *(SDL_Time *)out) *(SDL_Time *)out = in.modify_time;
    return SDL_ENUM_CONTINUE;
}

static SDL_EnumerationResult SDLCALL copy_tree(void *to_dir, const char *dir, const char *name) {
    char from[1024], to[1024];
    snprintf(from, sizeof from, "%s%s", dir, name);
    snprintf(to, sizeof to, "%s/%s", (const char *)to_dir, name);
    SDL_PathInfo in;
    if (!SDL_GetPathInfo(from, &in)) return SDL_ENUM_CONTINUE;
    if (in.type == SDL_PATHTYPE_DIRECTORY) {
        SDL_CreateDirectory(to);
        SDL_EnumerateDirectory(from, copy_tree, to);
    } else SDL_CopyFile(from, to);
    return SDL_ENUM_CONTINUE;
}

static SDL_EnumerationResult SDLCALL remove_tree(void *unused, const char *dir, const char *name) {
    char p[1024];
    snprintf(p, sizeof p, "%s%s", dir, name);
    SDL_PathInfo in;
    if (SDL_GetPathInfo(p, &in) && in.type == SDL_PATHTYPE_DIRECTORY) SDL_EnumerateDirectory(p, remove_tree, unused);
    SDL_RemovePath(p);
    return SDL_ENUM_CONTINUE;
}

typedef struct { char names[64][32]; int n; } List;

static SDL_EnumerationResult SDLCALL list_dirs(void *out, const char *dir, const char *name) {
    (void)dir;
    List *l = out;
    if (l->n < 64 && name[0] >= '0' && name[0] <= '9') snprintf(l->names[l->n++], sizeof l->names[0], "%s", name);
    return SDL_ENUM_CONTINUE;
}

static int by_name(const void *a, const void *b) { return strcmp(a, b); }

void save_backup_now(void) {
    static bool done;  // (once a run: "Title screen" boots again in the same process)
    if (done || !g_settings.save_backups || getenv("SAVAGEWIND_SAVE_DIR")) return;
    done = true;
    SDL_Time t = 0, last = 0;
    SDL_EnumerateDirectory("save", newest, &t);
    if (!t) return;  // (no saves yet)
    SDL_CreateDirectory(DIR_BACKUPS);
    FILE *f = fopen(DIR_BACKUPS "/last.txt", "r");
    if (f) {
        long long v = 0;
        if (fscanf(f, "%lld", &v) == 1) last = v;
        fclose(f);
    }
    if (t == last) return;
    char when[32] = "0", to[128];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (tm) strftime(when, sizeof when, "%Y%m%d-%H%M%S", tm);
    snprintf(to, sizeof to, DIR_BACKUPS "/%s", when);
    SDL_CreateDirectory(to);
    SDL_EnumerateDirectory("save", copy_tree, to);
    f = fopen(DIR_BACKUPS "/last.txt", "w");
    if (f) {
        fprintf(f, "%lld\n", (long long)t);
        fclose(f);
    }
    static List l;
    l.n = 0;
    SDL_EnumerateDirectory(DIR_BACKUPS, list_dirs, &l);
    qsort(l.names, l.n, sizeof l.names[0], by_name);
    for (int i = 0; i + SAVE_BACKUPS < l.n; i++) {
        snprintf(to, sizeof to, DIR_BACKUPS "/%s", l.names[i]);
        SDL_EnumerateDirectory(to, remove_tree, NULL);
        SDL_RemovePath(to);
    }
    LOG("[saves] backed up to " DIR_BACKUPS "/%s\n", when);
}
