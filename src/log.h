#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace cr {

void log_open(const char* game_dir);
void log_line(const char* fmt, ...);

// SEH filter: logs the exception code and address (as Mewgenics.exe+rva or
// combat_roster.dll+rva) and returns EXCEPTION_EXECUTE_HANDLER.
int log_exception(const char* where, EXCEPTION_POINTERS* ep);

}  // namespace cr
