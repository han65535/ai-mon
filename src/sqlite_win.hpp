#pragma once
#ifdef _WIN32
#include <sqlite3.h>
namespace aimon {
bool load_system_sqlite();
#define AIMON_SQLITE_FUNCTIONS(X) \
    X(sqlite3_bind_int) X(sqlite3_bind_int64) X(sqlite3_bind_text) X(sqlite3_busy_timeout) \
    X(sqlite3_clear_bindings) X(sqlite3_close) X(sqlite3_column_int) X(sqlite3_column_int64) \
    X(sqlite3_column_text) X(sqlite3_errmsg) X(sqlite3_exec) X(sqlite3_finalize) \
    X(sqlite3_open_v2) X(sqlite3_prepare_v2) X(sqlite3_reset) X(sqlite3_step)
#define AIMON_DECLARE(name) extern decltype(&::name) name##_system;
AIMON_SQLITE_FUNCTIONS(AIMON_DECLARE)
#undef AIMON_DECLARE
}
#define sqlite3_bind_int aimon::sqlite3_bind_int_system
#define sqlite3_bind_int64 aimon::sqlite3_bind_int64_system
#define sqlite3_bind_text aimon::sqlite3_bind_text_system
#define sqlite3_busy_timeout aimon::sqlite3_busy_timeout_system
#define sqlite3_clear_bindings aimon::sqlite3_clear_bindings_system
#define sqlite3_close aimon::sqlite3_close_system
#define sqlite3_column_int aimon::sqlite3_column_int_system
#define sqlite3_column_int64 aimon::sqlite3_column_int64_system
#define sqlite3_column_text aimon::sqlite3_column_text_system
#define sqlite3_errmsg aimon::sqlite3_errmsg_system
#define sqlite3_exec aimon::sqlite3_exec_system
#define sqlite3_finalize aimon::sqlite3_finalize_system
#define sqlite3_open_v2 aimon::sqlite3_open_v2_system
#define sqlite3_prepare_v2 aimon::sqlite3_prepare_v2_system
#define sqlite3_reset aimon::sqlite3_reset_system
#define sqlite3_step aimon::sqlite3_step_system
#endif
