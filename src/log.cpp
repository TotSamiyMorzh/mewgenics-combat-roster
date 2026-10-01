#include "log.h"

#include "game.h"

#include <cstdarg>
#include <cstdio>
#include <share.h>

namespace cr {
namespace {
FILE* g_log = nullptr;
HMODULE self_module() {
    HMODULE m = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&self_module, &m);
    return m;
}
}  // namespace

void log_open(const char* game_dir) {
    char path[MAX_PATH];
    sprintf_s(path, "%s\\mod_logs", game_dir);
    CreateDirectoryA(path, nullptr);
    strcat_s(path, "\\combat_roster.log");
    g_log = _fsopen(path, "w", _SH_DENYNO);   // readable while the game runs
}

void log_line(const char* fmt, ...) {
    if (!g_log) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

int log_exception(const char* where, EXCEPTION_POINTERS* ep) {
    uintptr_t addr = (uintptr_t)ep->ExceptionRecord->ExceptionAddress;
    uintptr_t self = (uintptr_t)self_module();
    char loc[64];
    if (g_base && addr >= g_base && addr < g_base + 0x1574000)
        sprintf_s(loc, "Mewgenics.exe+0x%llX", (unsigned long long)(addr - g_base));
    else if (self && addr >= self && addr < self + 0x400000)
        sprintf_s(loc, "combat_roster.dll+0x%llX", (unsigned long long)(addr - self));
    else
        sprintf_s(loc, "%p", (void*)addr);
    log_line("!! %s: exception 0x%08lX at %s -- feature disabled for this session", where,
             ep->ExceptionRecord->ExceptionCode, loc);
    return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace cr
