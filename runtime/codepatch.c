// Small writes into the game library's compiled code (runtime/spawner.c: the pouch's armour limit, a constant the
// recompiled game code carries as an instruction's immediate).
#include <stdint.h>
#include <string.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

int code_write32(void *at, uint32_t v) {
#ifdef _WIN32
    DWORD old;
    if (!VirtualProtect(at, 4, PAGE_EXECUTE_READWRITE, &old)) return -1;
    memcpy(at, &v, 4);
    VirtualProtect(at, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, 4);
    return 0;
#else
    long pg = sysconf(_SC_PAGESIZE);
    uintptr_t a = (uintptr_t)at & ~(uintptr_t)(pg - 1), e = ((uintptr_t)at + 4 + pg - 1) & ~(uintptr_t)(pg - 1);
    if (mprotect((void *)a, e - a, PROT_READ | PROT_WRITE | PROT_EXEC)) return -1;
    memcpy(at, &v, 4);
    mprotect((void *)a, e - a, PROT_READ | PROT_EXEC);
    __builtin___clear_cache((char *)at, (char *)at + 4);
    return 0;
#endif
}
