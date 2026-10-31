#include "history.hpp"
#include "collector.hpp"
#include <sqlite3.h>
#ifdef _WIN32
#include "sqlite_win.hpp"
#endif
#include <algorithm>
#include <cerrno>
#include <filesystem>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace aimon {
namespace {
struct Statement {
    sqlite3_stmt *value = nullptr;
    ~Statement() { sqlite3_finalize(value); }
    bool prepare(sqlite3 *db, const char *sql) { return sqlite3_prepare_v2(db, sql, -1, &value, nullptr) == SQLITE_OK; }
};
static bool cancelled(HANDLE stop) {
#ifdef _WIN32
    return stop && WaitForSingleObject(stop, 0) == WAIT_OBJECT_0;
#else
    return stop && stop->load();
#endif
}
struct File {
#ifdef _WIN32
    Handle handle;
    explicit File(const std::wstring &path) : handle(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr)) {}
    bool metadata(uint64_t &size, uint64_t &identity, uint64_t &modified) {
        BY_HANDLE_FILE_INFORMATION info{};
        if (!handle.valid() || !GetFileInformationByHandle(handle.value, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
        size = (uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
        identity = ((uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow) ^ (uint64_t(info.dwVolumeSerialNumber) << 17);
        modified = (uint64_t(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime;
        if (!identity) identity = 1;
        return true;
    }
    std::ptrdiff_t read_at(char *buf, size_t count, uint64_t offset) {
        LARGE_INTEGER pos{}; pos.QuadPart = static_cast<LONGLONG>(offset); DWORD read = 0;
        if (!SetFilePointerEx(handle.value, pos, nullptr, FILE_BEGIN) || !ReadFile(handle.value, buf, static_cast<DWORD>(count), &read, nullptr)) return -1;
        return read;
    }
#else
    int fd;
    explicit File(const std::wstring &path) : fd(::open(utf8(path).c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW)) {}
    ~File() { if (fd >= 0) close(fd); }
    bool metadata(uint64_t &size, uint64_t &identity, uint64_t &modified) {
        struct stat info{}; if (fd < 0 || fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0) return false;
        size = static_cast<uint64_t>(info.st_size); identity = static_cast<uint64_t>(info.st_ino) ^ (static_cast<uint64_t>(info.st_dev) << 17);
        if (!identity) identity = 1;
        modified = static_cast<uint64_t>(info.st_mtim.tv_sec) * 1000000000ULL + static_cast<uint64_t>(info.st_mtim.tv_nsec); return true;
    }
    std::ptrdiff_t read_at(char *buf, size_t count, uint64_t offset) {
        ssize_t result; do { result = pread(fd, buf, count, static_cast<off_t>(offset)); } while (result < 0 && errno == EINTR); return result;
    }
#endif
};
struct Transaction {
    sqlite3 *db; bool committed = false;
    ~Transaction() { if (!committed) sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); }
};
void bind_text(sqlite3_stmt *s, int n, const std::string &v) { sqlite3_bind_text(s, n, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT); }
std::string column_text(sqlite3_stmt *s, int n) { const auto *v = sqlite3_column_text(s, n); return v ? reinterpret_cast<const char *>(v) : ""; }
bool unhex(const cJSON *p, const char *key, uint64_t &out) {
    auto s = str(p, key); if (s.size() != 16) return false; out = 0;
    for (char c : s) { unsigned d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 99; if (d > 15) return false; out = (out << 4) | d; }
    return true;
}
std::string encode_state(const FileState &s) {
    auto p = json(cJSON_CreateObject());
    put(p.get(), "id", hex(s.identity)); put(p.get(), "modified", hex(s.modified)); put(p.get(), "prefix", hex(s.prefix));
    put(p.get(), "size", s.size); put(p.get(), "offset", s.offset); put(p.get(), "prefix_length", s.prefix_length);
    put(p.get(), "session", s.parser.session); put(p.get(), "model", s.parser.model); put(p.get(), "previous_model", s.parser.previous_model);
    put(p.get(), "source", s.parser.source); put(p.get(), "previous", s.parser.has_previous); put(p.get(), "epoch", s.parser.epoch);
    put(p.get(), "discarding", s.discarding); put(p.get(), "malformed", s.malformed); put(p.get(), "unsupported", s.unsupported); cJSON_AddItemToObject(p.get(), "tokens", token_json(s.parser.previous));
    return dump(p.get());
}
bool decode_state(const std::string &text, FileState &s) {
    auto p = parse(text); if (!p || !unhex(p.get(), "id", s.identity) || !unhex(p.get(), "modified", s.modified) ||
        !unhex(p.get(), "prefix", s.prefix) || !number(field(p.get(), "size"), s.size) || !number(field(p.get(), "offset"), s.offset) ||
        !number(field(p.get(), "prefix_length"), s.prefix_length) || s.prefix_length > 256 || s.offset > s.size ||
        !number(field(p.get(), "epoch"), s.parser.epoch) || !token_json(field(p.get(), "tokens"), s.parser.previous)) return false;
    s.parser.session = str(p.get(), "session"); s.parser.model = str(p.get(), "model"); s.parser.previous_model = str(p.get(), "previous_model");
    s.parser.source = str(p.get(), "source"); s.parser.has_previous = cJSON_IsTrue(field(p.get(), "previous"));
    s.discarding = cJSON_IsTrue(field(p.get(), "discarding")); s.malformed = cJSON_IsTrue(field(p.get(), "malformed")); s.unsupported = cJSON_IsTrue(field(p.get(), "unsupported"));
    return s.parser.session.size() <= 512 && s.parser.model.size() <= 256 && s.parser.previous_model.size() <= 256 && s.parser.source.size() <= 32;
}
std::string safe_label(const std::string &s, size_t max) {
    if (s.size() > max) return {};
    for (unsigned char c : s) if (c < 32 || c == 127) return {};
    return s;
}
}
std::string format_history_csv(const std::vector<HistoryRow> &rows, const std::string &unknown_model, const std::string &unknown_source) {
    auto field = [](std::string value) {
        size_t first = value.find_first_not_of(" \t\r\n");
        if (first != std::string::npos && (value[first] == '=' || value[first] == '+' || value[first] == '-' || value[first] == '@')) value.insert(value.begin(), '\'');
        std::string out = "\"";
        for (char c : value) { if (c == '\"') out += '\"'; out += c; }
        return out + "\"";
    };
    std::string csv = "period,provider,model,source,input_tokens,output_tokens,cache_read_tokens,events\r\n";
    for (const auto &row : rows) csv += field(row.period) + "," + field(row.provider ? "Codex" : "Claude") + "," + field(row.model.empty() ? unknown_model : row.model) + "," + field(row.source.empty() ? unknown_source : row.source) + "," + std::to_string(row.input) + "," + std::to_string(row.output) + "," + std::to_string(row.cached) + "," + std::to_string(row.events) + "\r\n";
    return csv;
}
History::History(const std::wstring &directory) : directory_(directory) {}
History::~History() { if (db_) sqlite3_close(db_); }
bool History::open() {
    if (db_) return true;
#ifdef _WIN32
    if (!load_system_sqlite()) { error_ = "Windows SQLite unavailable"; return false; }
#endif
    if (!ensure_directory(directory_)) { error_ = "이력 폴더를 만들 수 없습니다"; return false; }
    auto path = utf8(join(directory_, L"history.sqlite3"));
#ifndef _WIN32
    int fd = ::open(path.c_str(), O_CREAT | O_CLOEXEC | O_RDWR | O_NOFOLLOW, 0600);
    if (fd < 0) { error_ = "이력 파일을 열 수 없습니다"; return false; } close(fd);
#endif
    if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) { error_ = db_ ? sqlite3_errmsg(db_) : "SQLite 오류"; if (db_) sqlite3_close(db_); db_ = nullptr; return false; }
    sqlite3_busy_timeout(db_, 1000);
    const char *schema = "PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL;"
        "CREATE TABLE IF NOT EXISTS events (key TEXT PRIMARY KEY, provider INTEGER NOT NULL, time INTEGER NOT NULL, updated INTEGER NOT NULL, model TEXT NOT NULL, source TEXT NOT NULL, input INTEGER NOT NULL, output INTEGER NOT NULL, cached INTEGER NOT NULL);"
        "CREATE INDEX IF NOT EXISTS events_time ON events(time);"
        "CREATE TABLE IF NOT EXISTS files (provider INTEGER NOT NULL, path TEXT NOT NULL, state TEXT NOT NULL, PRIMARY KEY(provider,path));";
    if (sqlite3_exec(db_, schema, nullptr, nullptr, nullptr) != SQLITE_OK) { error_ = sqlite3_errmsg(db_); sqlite3_close(db_); db_ = nullptr; return false; }
    return true;
}
bool History::scan(const Settings &settings, HANDLE stop) {
    if (!open()) return false;
    error_.clear(); pending_ = false; incomplete_ = false;
    if (sqlite3_exec(db_, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) { error_ = sqlite3_errmsg(db_); return false; }
    Transaction transaction{db_};
    Statement get, put, event, correction;
    if (!get.prepare(db_, "SELECT state FROM files WHERE provider=? AND path=?") ||
        !put.prepare(db_, "INSERT OR REPLACE INTO files(provider,path,state) VALUES(?,?,?)") ||
        !event.prepare(db_, "INSERT OR IGNORE INTO events(key,provider,time,updated,model,source,input,output,cached) VALUES(?,?,?,?,?,?,?,?,?)") ||
        !correction.prepare(db_, "UPDATE events SET time=min(time,?),updated=?,model=CASE WHEN ?='' THEN model ELSE ? END,source=CASE WHEN ?='' THEN source ELSE ? END,input=?,output=?,cached=? WHERE key=? AND provider=0 AND updated<=?")) { error_ = sqlite3_errmsg(db_); return false; }
    size_t budget = 16 * 1024 * 1024;
    for (int provider = 0; provider < 2; ++provider) {
        if (!settings.enabled[provider]) continue;
        std::vector<std::wstring> roots{settings.roots[provider]};
        if (provider == 1) {
            auto primary = std::filesystem::u8path(utf8(settings.roots[provider]));
#ifdef _WIN32
            const bool sessions_folder = _wcsicmp(primary.filename().c_str(), L"sessions") == 0;
#else
            const bool sessions_folder = primary.filename() == "sessions";
#endif
            if (sessions_folder) {
                auto archived = wide((primary.parent_path() / "archived_sessions").u8string());
                if (directory(archived)) roots.push_back(std::move(archived));
            }
        }
        std::vector<FileInfo> files;
        for (size_t i = 0; i < roots.size(); ++i) {
            if (!directory(roots[i])) { if (i == 0) incomplete_ = true; continue; }
            bool failed = false; auto found = list_logs(roots[i], failed, stop);
            if (failed) incomplete_ = true;
            files.insert(files.end(), found.begin(), found.end());
        }
        std::sort(files.begin(), files.end(), [](const FileInfo &a, const FileInfo &b) { return a.modified > b.modified; });
        for (const auto &file : files) {
            if (cancelled(stop)) { pending_ = true; break; }
            if (!budget) { pending_ = true; break; }
            auto path = utf8(file.path); if (path.empty()) { incomplete_ = true; continue; }
            sqlite3_reset(get.value); sqlite3_clear_bindings(get.value); sqlite3_bind_int(get.value, 1, provider); bind_text(get.value, 2, path);
            FileState s; int result = sqlite3_step(get.value);
            if (result == SQLITE_ROW && !decode_state(column_text(get.value, 0), s)) { error_ = "이력 읽기 위치가 손상되었습니다"; return false; }
            if (result != SQLITE_ROW && result != SQLITE_DONE) { error_ = sqlite3_errmsg(db_); return false; }
            if (s.identity && s.identity == file.identity && s.modified == file.modified && s.size == file.size && s.offset >= file.size) { incomplete_ = incomplete_ || s.malformed || s.unsupported; continue; }
            File f(file.path); uint64_t size = 0, identity = 0, modified = 0;
            if (!f.metadata(size, identity, modified)) { incomplete_ = true; continue; }
            if (s.identity && s.identity == identity && s.modified == modified && s.size == size && s.offset >= size) { incomplete_ = incomplete_ || s.malformed || s.unsupported; continue; }
            bool reset = s.identity != identity || size < s.offset || (modified != s.modified && size == s.size && s.offset == size);
            char prefix[256]{};
            if (s.prefix_length && !reset) { auto n = f.read_at(prefix, s.prefix_length, 0); if (n < 0) { incomplete_ = true; continue; } if (static_cast<uint64_t>(n) != s.prefix_length || hash_bytes(prefix, n) != s.prefix) reset = true; }
            // A rewritten/replaced source can invalidate already archived cumulative deltas.
            // Keep the history, but report that it may need review rather than claiming completeness.
            if (reset) { bool changed_source = s.identity != 0; s = {}; s.malformed = changed_source; }
            s.identity = identity;
            if (!s.prefix_length && size) { auto n = f.read_at(prefix, static_cast<size_t>(std::min<uint64_t>(size, sizeof prefix)), 0); if (n < 0) { incomplete_ = true; continue; } s.prefix_length = n; s.prefix = hash_bytes(prefix, n); }
            std::string line; char buffer[65536]; uint64_t position = s.offset; size_t file_budget = 8 * 1024 * 1024; bool read_failed = false;
            const auto source = hex(hash_bytes(path.data(), path.size()));
            while (position < size && budget && file_budget && !(cancelled(stop))) {
                size_t count = static_cast<size_t>(std::min<uint64_t>(sizeof buffer, std::min<uint64_t>(size - position, std::min(budget, file_budget))));
                std::ptrdiff_t got = f.read_at(buffer, count, position);
                if (got < 0) { read_failed = true; incomplete_ = true; break; } if (!got) break;
                budget -= got; file_budget -= got;
                for (std::ptrdiff_t i = 0; i < got; ++i) {
                    ++position; const char c = buffer[i];
                    if (c == '\n') {
                        if (!s.discarding && !line.empty()) {
                            if (line.back() == '\r') line.pop_back();
                            UsageEvent e; auto parsed = parse_usage(provider, line, source, s.parser, e);
                            if (parsed == ParseResult::Event && e.time <= now_seconds() + 86400) {
                                e.model = safe_label(e.model, 256); e.source = safe_label(e.source, 32);
                                sqlite3_reset(event.value); sqlite3_clear_bindings(event.value);
                                bind_text(event.value, 1, e.key); sqlite3_bind_int(event.value, 2, provider); sqlite3_bind_int64(event.value, 3, e.time); sqlite3_bind_int64(event.value, 4, e.updated ? e.updated : e.time);
                                bind_text(event.value, 5, e.model); bind_text(event.value, 6, e.source); sqlite3_bind_int64(event.value, 7, e.tokens.input); sqlite3_bind_int64(event.value, 8, e.tokens.output); sqlite3_bind_int64(event.value, 9, e.tokens.read);
                                if (sqlite3_step(event.value) != SQLITE_DONE) { error_ = sqlite3_errmsg(db_); return false; }
                                if (provider == 0) {
                                    sqlite3_reset(correction.value); sqlite3_clear_bindings(correction.value);
                                    sqlite3_bind_int64(correction.value, 1, e.time); sqlite3_bind_int64(correction.value, 2, e.updated ? e.updated : e.time);
                                    bind_text(correction.value, 3, e.model); bind_text(correction.value, 4, e.model); bind_text(correction.value, 5, e.source); bind_text(correction.value, 6, e.source);
                                    sqlite3_bind_int64(correction.value, 7, e.tokens.input); sqlite3_bind_int64(correction.value, 8, e.tokens.output); sqlite3_bind_int64(correction.value, 9, e.tokens.read);
                                    bind_text(correction.value, 10, e.key); sqlite3_bind_int64(correction.value, 11, e.updated ? e.updated : e.time);
                                    if (sqlite3_step(correction.value) != SQLITE_DONE) { error_ = sqlite3_errmsg(db_); return false; }
                                }
                            } else if (parsed == ParseResult::Invalid) s.malformed = true;
                            else if (parsed == ParseResult::Unsupported) s.unsupported = true;
                        }
                        line.clear(); s.discarding = false; s.offset = position;
                    } else if (!s.discarding) {
                        if (line.size() < MaxLine) line.push_back(c); else { line.clear(); s.discarding = true; s.malformed = true; }
                    }
                    if (s.discarding) s.offset = position;
                }
            }
            if (read_failed) continue;
            if (position < size || (cancelled(stop))) pending_ = true;
            incomplete_ = incomplete_ || s.malformed || s.unsupported;
            s.size = size; s.modified = modified;
            sqlite3_reset(put.value); sqlite3_clear_bindings(put.value); sqlite3_bind_int(put.value, 1, provider); bind_text(put.value, 2, path); bind_text(put.value, 3, encode_state(s));
            if (sqlite3_step(put.value) != SQLITE_DONE) { error_ = sqlite3_errmsg(db_); return false; }
        }
    }
    if (sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) { error_ = sqlite3_errmsg(db_); return false; }
    transaction.committed = true; return true;
}
bool History::report(bool monthly, std::vector<HistoryRow> &rows) {
    if (!open()) return false;
    const char *period = monthly ? "strftime('%Y-%m',time,'unixepoch','localtime')" : "date(time,'unixepoch','localtime','weekday 0','-6 days')";
    std::string sql = "SELECT "+std::string(period)+",provider,model,source,sum(input),sum(output),sum(cached),count(*) FROM events GROUP BY 1,2,3,4 ORDER BY 1 DESC,2,3,4";
    Statement s; if (!s.prepare(db_, sql.c_str())) { error_ = sqlite3_errmsg(db_); return false; }
    rows.clear(); truncated_ = false; int result;
    while ((result = sqlite3_step(s.value)) == SQLITE_ROW) {
        if (rows.size() >= 10000) { truncated_ = true; break; }
        HistoryRow row; row.period=column_text(s.value,0); row.provider=sqlite3_column_int(s.value,1); row.model=column_text(s.value,2); row.source=column_text(s.value,3);
        row.input=sqlite3_column_int64(s.value,4); row.output=sqlite3_column_int64(s.value,5); row.cached=sqlite3_column_int64(s.value,6); row.events=sqlite3_column_int64(s.value,7); rows.push_back(std::move(row));
    }
    if (result != SQLITE_DONE && !truncated_) { error_ = sqlite3_errmsg(db_); return false; }
    return true;
}
}
