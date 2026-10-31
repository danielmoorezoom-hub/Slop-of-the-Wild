// Settings > Prelaunch > Make a problem report: a folder to attach to an issue, with the log, the settings and the benchmark results.
// No game files and no saves.
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// The folder made, for showing it ("" if it couldn't be)
const char *report_make(void) {
    static char dir[96];
    char when[32] = "0", to[160];
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    if (tm) strftime(when, sizeof when, "%Y%m%d-%H%M%S", tm);
    snprintf(dir, sizeof dir, "problem-report-%s", when);
    if (!SDL_CreateDirectory(dir)) return dir[0] = 0, dir;
    static const char *const files[] = {"savagewind.log", "savagewind.ini", "benchmark/results.txt"};
    static const char *const names[] = {"savagewind.log", "savagewind.ini", "benchmark-results.txt"};
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
        snprintf(to, sizeof to, "%s/%s", dir, names[i]);
        SDL_CopyFile(files[i], to);
    }
    return dir;
}
