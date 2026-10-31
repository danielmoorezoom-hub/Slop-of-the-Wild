// Settings > Prelaunch > Check for updates: the latest release's tag from GitHub, asked only when the button is pressed. Nothing is sent but the
// request itself, and nothing is downloaded: the page is opened for the player to get it.
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>

static char g_tag[64];
static atomic_int g_state;  // 0 not asked, 1 asking, 2 done, 3 failed

static void *ask(void *arg) {
    (void)arg;
    bool ok = false;
    static char buf[65536];
    DWORD total = 0, got = 0;
    HINTERNET s = WinHttpOpen(L"SlopOfTheWild", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, NULL, NULL, 0);
    HINTERNET c = s ? WinHttpConnect(s, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0) : NULL;
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", L"/repos/devall96/Slop-of-the-Wild/releases/latest", NULL, NULL, NULL, WINHTTP_FLAG_SECURE) : NULL;
    if (r && WinHttpSendRequest(r, L"Accept: application/vnd.github+json\r\n", (DWORD)-1, NULL, 0, 0, 0) && WinHttpReceiveResponse(r, NULL)) {
        while (total < sizeof buf - 1 && WinHttpReadData(r, buf + total, sizeof buf - 1 - total, &got) && got) total += got;
        buf[total] = 0;
        const char *t = strstr(buf, "\"tag_name\"");
        t = t ? strchr(t + 10, '"') : NULL;
        const char *e = t ? strchr(t + 1, '"') : NULL;
        if (e && (size_t)(e - t - 1) < sizeof g_tag) {
            memcpy(g_tag, t + 1, (size_t)(e - t - 1));
            g_tag[e - t - 1] = 0;
            ok = true;
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    g_state = ok ? 2 : 3;
    return NULL;
}

void update_check_start(void) {
    if (g_state == 1) return;
    g_state = 1;
    pthread_t t;
    if (pthread_create(&t, NULL, ask, NULL)) g_state = 3;
    else pthread_detach(t);
}

int update_check_state(void) { return g_state; }
const char *update_check_tag(void) { return g_tag; }
#else
void update_check_start(void) {}
int update_check_state(void) { return 0; }
const char *update_check_tag(void) { return ""; }
#endif
