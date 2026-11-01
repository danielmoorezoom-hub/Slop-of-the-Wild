// Settings > Prelaunch > Check for updates: the latest release's tag from GitHub, asked only when the button is pressed. Nothing is sent but the
// request itself, and nothing is downloaded: the page is opened for the player to get it.
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <stdlib.h>
#include <windows.h>
#include <shellapi.h>
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

// Download and install: the release's Setup.exe into cache/update/, its SHA-256 checked against the digest GitHub's listing gives. Nothing runs until
// the player presses the button, and a file that doesn't match is removed.
#include <bcrypt.h>
static char g_url_path[512], g_digest[65], g_file[128];
static atomic_int g_dl;  // 0 not asked, 1 running, 2 ready, 3 failed

static bool get_json(char *buf, size_t cap) {
    bool ok = false;
    DWORD total = 0, got = 0;
    HINTERNET s = WinHttpOpen(L"SlopOfTheWild", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, NULL, NULL, 0);
    HINTERNET c = s ? WinHttpConnect(s, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0) : NULL;
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", L"/repos/devall96/Slop-of-the-Wild/releases/latest", NULL, NULL, NULL, WINHTTP_FLAG_SECURE) : NULL;
    if (r && WinHttpSendRequest(r, L"Accept: application/vnd.github+json\r\n", (DWORD)-1, NULL, 0, 0, 0) && WinHttpReceiveResponse(r, NULL)) {
        while (total < cap - 1 && WinHttpReadData(r, buf + total, (DWORD)(cap - 1 - total), &got) && got) total += got;
        buf[total] = 0;
        ok = total > 0;
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    return ok;
}

// the Setup.exe asset's URL path and digest out of the listing
static bool find_asset(const char *json) {
    for (const char *p = json; (p = strstr(p, "\"browser_download_url\"")); p += 20) {
        const char *q = strstr(p, "https://github.com"), *e = q ? strchr(q, '"') : NULL;
        if (!e || e - q < 12 || strncmp(e - 10, "-Setup.exe", 10)) continue;
        snprintf(g_url_path, sizeof g_url_path, "%.*s", (int)(e - q - 18), q + 18);
        // (each asset's object: its digest may come before or after the URL)
        const char *start = p;
        while (start > json && strncmp(start, "\"url\"", 5)) start--;
        const char *d = strstr(start, "\"digest\"");
        if (!d || !(d = strstr(d, "sha256:"))) return false;
        snprintf(g_digest, sizeof g_digest, "%.64s", d + 7);
        return strlen(g_digest) == 64;
    }
    return false;
}

static bool sha256_file(const char *path, char out[65]) {
    BCRYPT_ALG_HANDLE alg = 0;
    BCRYPT_HASH_HANDLE h = 0;
    bool ok = false;
    FILE *f = fopen(path, "rb");
    if (f && BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0 && BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0) == 0) {
        static unsigned char buf[1 << 16];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) BCryptHashData(h, buf, (ULONG)n, 0);
        unsigned char d[32];
        if (BCryptFinishHash(h, d, sizeof d, 0) == 0) {
            for (int i = 0; i < 32; i++) sprintf(out + 2 * i, "%02x", d[i]);
            ok = true;
        }
    }
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    if (f) fclose(f);
    return ok;
}

static void *dl_thread(void *arg) {
    (void)arg;
    bool ok = false;
    static char json[1 << 17];
    if (get_json(json, sizeof json) && find_asset(json)) {
        _mkdir("cache");
        _mkdir("cache\\update");
        snprintf(g_file, sizeof g_file, "cache\\update\\Setup.exe");
        wchar_t wp[512];
        MultiByteToWideChar(CP_UTF8, 0, g_url_path, -1, wp, 512);
        HINTERNET s = WinHttpOpen(L"SlopOfTheWild", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, NULL, NULL, 0);
        HINTERNET c = s ? WinHttpConnect(s, L"github.com", INTERNET_DEFAULT_HTTPS_PORT, 0) : NULL;
        HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", wp, NULL, NULL, NULL, WINHTTP_FLAG_SECURE) : NULL;
        FILE *out = fopen(g_file, "wb");
        if (r && out && WinHttpSendRequest(r, NULL, 0, NULL, 0, 0, 0) && WinHttpReceiveResponse(r, NULL)) {  // (WinHTTP follows GitHub's redirect)
            static char buf[1 << 16];
            DWORD got = 0;
            while (WinHttpReadData(r, buf, sizeof buf, &got) && got) fwrite(buf, 1, got, out);
            fclose(out);
            out = NULL;
            char have[65] = "";
            ok = sha256_file(g_file, have) && !strcmp(have, g_digest);
        }
        if (out) fclose(out);
        if (r) WinHttpCloseHandle(r);
        if (c) WinHttpCloseHandle(c);
        if (s) WinHttpCloseHandle(s);
        if (!ok) remove(g_file);
    }
    g_dl = ok ? 2 : 3;
    return NULL;
}

void update_download_start(void) {
    if (g_dl == 1) return;
    g_dl = 1;
    pthread_t t;
    if (pthread_create(&t, NULL, dl_thread, NULL)) g_dl = 3;
    else pthread_detach(t);
}

int update_download_state(void) { return g_dl; }
const char *update_download_path(void) { return g_file; }

void update_run_installer(void) {
    if (g_dl != 2) return;
    ShellExecuteA(NULL, "open", g_file, NULL, NULL, SW_SHOWNORMAL);
    exit(0);
}
#else
void update_download_start(void) {}
int update_download_state(void) { return 0; }
const char *update_download_path(void) { return ""; }
void update_run_installer(void) {}
void update_check_start(void) {}
int update_check_state(void) { return 0; }
const char *update_check_tag(void) { return ""; }
#endif
