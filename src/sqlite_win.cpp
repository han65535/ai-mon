#include <windows.h>
#include <sqlite3.h>
#include "sqlite_win.hpp"
// sqlite_win.hpp maps calls to the dynamically loaded system library.
#undef sqlite3_bind_int
#undef sqlite3_bind_int64
#undef sqlite3_bind_text
#undef sqlite3_busy_timeout
#undef sqlite3_clear_bindings
#undef sqlite3_close
#undef sqlite3_column_int
#undef sqlite3_column_int64
#undef sqlite3_column_text
#undef sqlite3_errmsg
#undef sqlite3_exec
#undef sqlite3_finalize
#undef sqlite3_open_v2
#undef sqlite3_prepare_v2
#undef sqlite3_reset
#undef sqlite3_step
namespace aimon {
#define AIMON_DEFINE(name) decltype(&::name) name##_system = nullptr;
AIMON_SQLITE_FUNCTIONS(AIMON_DEFINE)
#undef AIMON_DEFINE
bool load_system_sqlite() {
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
    static bool loaded = false;
    InitOnceExecuteOnce(&once, [](PINIT_ONCE, PVOID, PVOID *) -> BOOL {
        HMODULE module = LoadLibraryExW(L"winsqlite3.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) return TRUE;
        bool available = true;
#define AIMON_LOAD(name) name##_system = reinterpret_cast<decltype(name##_system)>(GetProcAddress(module, #name)); available = available && name##_system;
        AIMON_SQLITE_FUNCTIONS(AIMON_LOAD)
#undef AIMON_LOAD
        if (!available) { FreeLibrary(module); return TRUE; }
        loaded = true;
        return TRUE;
    }, nullptr, nullptr);
    return loaded;
}
}
