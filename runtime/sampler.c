// (profiling) Sampling profiler for Windows. SAVAGEWIND_SAMPLE=<start s>:<seconds>[:<interval us>[:<depth>]] suspends
// every guest thread each interval (default 1000 us) from <start> seconds after boot, for <seconds>, and records its host
// call stack (x64 unwind data: the game library and the runtime both have it), <depth> frames of it (default 48; 1: where
// it is only, no unwinding, which can read past a stack and end the run). The samples go to
// build/samples.txt; tools/profile_samples.py turns them into self / inclusive time per function, recompiled game
// functions as f_<guest address>. A thread waiting in the kernel shows its wait; filter with the tool.
// Each round of samples starts with "R <ns> <presented frame>", so a stretch of frames can be looked at alone.
#include <stdlib.h>
#include <string.h>

#include "kernel.h"
#include "rt.h"

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>

#define MAX_THREADS 128
#define MAX_DEPTH 48

// Unwinding a thread stopped anywhere can read past its stack (a guest stack has no limits the unwinder knows, and a
// frame caught half built gives it nonsense): such a stack ends there instead of ending the run.
static DWORD unwind_tid;
static volatile LONG unwinding;
static void *unwind_jb[5];
static void unwind_recover(void) { __builtin_longjmp(unwind_jb, 1); }
static LONG WINAPI unwind_fault(EXCEPTION_POINTERS *e) {
    if (GetCurrentThreadId() != unwind_tid || !unwinding || e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;
    e->ContextRecord->Rsp = (e->ContextRecord->Rsp & ~(DWORD64)15) - 8;  // (as if called)
    e->ContextRecord->Rip = (DWORD64)(uintptr_t)unwind_recover;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static volatile int unwind_depth;
__attribute__((noinline)) static void unwind_stack(CONTEXT *ctx, uint64_t *out, int depth) {
    for (; unwind_depth < depth && ctx->Rip; unwind_depth++) {
        out[unwind_depth] = ctx->Rip;
        if (unwind_depth + 1 == depth) { unwind_depth++; break; }
        DWORD64 base = 0;
        PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(ctx->Rip, &base, NULL);
        if (!f) {  // a leaf without unwind data: the return address is on top of the stack
            if (!ctx->Rsp || IsBadReadPtr((void *)ctx->Rsp, 8)) { unwind_depth++; break; }
            ctx->Rip = *(DWORD64 *)ctx->Rsp;
            ctx->Rsp += 8;
            continue;
        }
        void *hd = NULL;
        DWORD64 fr = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, ctx->Rip, f, ctx, &hd, &fr, NULL);
    }
}

static void *sampler_main(void *arg) {
    double start = 0, secs = 0, us = 1000;
    int depth = MAX_DEPTH;
    sscanf((const char *)arg, "%lf:%lf:%lf:%d", &start, &secs, &us, &depth);
    if (secs <= 0) return NULL;
    if (depth < 1 || depth > MAX_DEPTH) depth = MAX_DEPTH;
    plat_sleep_until_ns(plat_now_ns() + (uint64_t)(start * 1e9));
    unwind_tid = GetCurrentThreadId();
    AddVectoredExceptionHandler(1, unwind_fault);
    size_t cap = 48u << 20, used = 0;  // words (384 MB), allocated up front: nothing may allocate while a thread is suspended
    uint64_t *buf = malloc(cap * sizeof *buf);
    if (!buf) return NULL;
    pthread_t host[MAX_THREADS];
    uint32_t handle[MAX_THREADS];
    uint64_t entry[MAX_THREADS];
    uint64_t end = plat_now_ns() + (uint64_t)(secs * 1e9), nsamples = 0;
    LOG("[sample] sampling every %.0f us for %.0f s\n", us, secs);
    for (uint64_t next = plat_now_ns(); plat_now_ns() < end; next += (uint64_t)(us * 1000)) {
        int n = kernel_running_threads(host, handle, entry, MAX_THREADS);
        if (used + 4 < cap) {  // a round marker: "R <ns> <presented frame>" in the file
            extern uint64_t renderer_present_count(void);
            buf[used++] = ~0ull;
            buf[used++] = 2;
            buf[used++] = plat_now_ns();
            buf[used++] = renderer_present_count();
        }
        for (int i = 0; i < n && used + MAX_DEPTH + 4 < cap; i++) {
            HANDLE h = (HANDLE)pthread_gethandle(host[i]);
            if (!h || SuspendThread(h) == (DWORD)-1) continue;
            CONTEXT ctx;
            memset(&ctx, 0, sizeof ctx);
            ctx.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(h, &ctx)) {
                size_t at = used;
                buf[used++] = handle[i];
                buf[used++] = 0;  // depth, filled below
                unwind_depth = 0;
                if (!__builtin_setjmp(unwind_jb)) {
                    unwinding = 1;
                    unwind_stack(&ctx, buf + used, depth);
                }
                unwinding = 0;
                used += (size_t)unwind_depth;
                buf[at + 1] = (uint64_t)unwind_depth;
                nsamples++;
            }
            ResumeThread(h);
        }
        uint64_t now = plat_now_ns();
        if (next > now) plat_sleep_until_ns(next);
        else next = now;
    }
    FILE *f = fopen("build/samples.txt", "w");
    if (f) {
        fprintf(f, "M exe %llx\nM swgame %llx\n", (unsigned long long)(uintptr_t)GetModuleHandleA(NULL),
                (unsigned long long)(uintptr_t)GetModuleHandleA("swgame.dll"));
        HMODULE mods[512];  // (every module, for addresses outside those two: "L <base> <size> <name>")
        DWORD got = 0;
        if (K32EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &got))
            for (DWORD i = 0; i < got / sizeof mods[0] && i < 512; i++) {
                MODULEINFO mi;
                char nm[128] = "";
                if (K32GetModuleInformation(GetCurrentProcess(), mods[i], &mi, sizeof mi) && K32GetModuleBaseNameA(GetCurrentProcess(), mods[i], nm, sizeof nm))
                    fprintf(f, "L %llx %lx %s\n", (unsigned long long)(uintptr_t)mi.lpBaseOfDll, (unsigned long)mi.SizeOfImage, nm);
            }
        int n = kernel_running_threads(host, handle, entry, MAX_THREADS);
        for (int i = 0; i < n; i++) fprintf(f, "T %u %llx\n", handle[i], (unsigned long long)entry[i]);
        for (size_t p = 0; p < used;) {
            uint64_t th = buf[p], d = buf[p + 1];
            if (th == ~0ull) {
                fprintf(f, "R %llu %llu\n", (unsigned long long)buf[p + 2], (unsigned long long)buf[p + 3]);
                p += 2 + d;
                continue;
            }
            fprintf(f, "S %llu", (unsigned long long)th);
            for (uint64_t k = 0; k < d; k++) fprintf(f, " %llx", (unsigned long long)buf[p + 2 + k]);
            fputc('\n', f);
            p += 2 + d;
        }
        fclose(f);
    }
    LOG("[sample] %llu samples written to build/samples.txt\n", (unsigned long long)nsamples);
    free(buf);
    return NULL;
}

__attribute__((constructor)) static void sampler_start(void) {
    const char *e = getenv("SAVAGEWIND_SAMPLE");
    if (!e) return;
    pthread_t th;
    pthread_create(&th, NULL, sampler_main, (void *)e);
    pthread_detach(th);
}
#endif
