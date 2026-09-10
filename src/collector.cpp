#include "collector.hpp"
#include <algorithm>
#include <set>

namespace aimon {
Collector::Collector(std::wstring data_dir) : settings_(default_settings()), data_dir_(std::move(data_dir)) {}
void Collector::configure(const Settings &s) {
    for (int i = 0; i < 2; ++i)
        if (_wcsicmp(s.roots[i].c_str(), settings_.roots[i].c_str()) != 0) {
            for (auto it = store_.events.begin(); it != store_.events.end();) {
                if (it->second.provider == i)
                    it = store_.events.erase(it);
                else
                    ++it;
            }
            for (auto it = files_.begin(); it != files_.end();) {
                if (it->second.provider == i)
                    it = files_.erase(it);
                else
                    ++it;
            }
            success_[i] = 0;
            dirty_ = true;
        }
    settings_ = s;
}
static uint64_t file_identity(const BY_HANDLE_FILE_INFORMATION &info) {
    return (uint64_t(info.nFileIndexHigh) << 32 | info.nFileIndexLow) ^
           (uint64_t(info.dwVolumeSerialNumber) << 17);
}
bool Collector::read_file(const FileInfo &file, int provider, FileState &s, HANDLE stop, bool &pending,
                          size_t &budget) {
    Handle h(CreateFileW(file.path.c_str(), GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                         FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if (!h.valid() || !GetFileInformationByHandle(h.value, &info))
        return false;
    uint64_t size = uint64_t(info.nFileSizeHigh) << 32 | info.nFileSizeLow;
    uint64_t identity = file_identity(info);
    uint64_t modified =
        uint64_t(info.ftLastWriteTime.dwHighDateTime) << 32 | info.ftLastWriteTime.dwLowDateTime;
    bool reset = s.identity != identity || size < s.offset ||
                 (modified != s.modified && size == s.size && s.offset == size);
    // Validate the committed prefix when a file changes or after restoring state.
    char prefix[256]{};
    DWORD got = 0;
    if (s.prefix_length && !reset) {
        if (!ReadFile(h.value, prefix, static_cast<DWORD>(s.prefix_length), &got, nullptr))
            return false;
        if (got != s.prefix_length || hash_bytes(prefix, got) != s.prefix)
            reset = true;
    }
    if (reset) {
        s = {};
        s.provider = provider;
        dirty_ = true;
    }
    s.identity = identity;
    if (!s.prefix_length && size) {
        LARGE_INTEGER zero{};
        if (!SetFilePointerEx(h.value, zero, nullptr, FILE_BEGIN))
            return false;
        DWORD count = static_cast<DWORD>(std::min<uint64_t>(size, sizeof(prefix)));
        if (!ReadFile(h.value, prefix, count, &got, nullptr))
            return false;
        s.prefix_length = got;
        s.prefix = hash_bytes(prefix, got);
    }
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(s.offset);
    if (!SetFilePointerEx(h.value, position, nullptr, FILE_BEGIN))
        return false;
    std::string line;
    char buffer[65536];
    uint64_t position_now = s.offset;
    size_t file_budget = 8 * 1024 * 1024;
    std::string source =
        hex(hash_bytes(reinterpret_cast<const char *>(file.path.data()), file.path.size() * sizeof(wchar_t)));
    while (position_now < size && budget && file_budget) {
        if (stop && WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) {
            pending = true;
            break;
        }
        DWORD count = static_cast<DWORD>(std::min<uint64_t>(
            sizeof(buffer), std::min<uint64_t>(size - position_now, std::min(budget, file_budget))));
        if (!ReadFile(h.value, buffer, count, &got, nullptr))
            return false;
        if (!got)
            break;
        bytes_read_ += got;
        budget -= got;
        file_budget -= got;
        for (DWORD i = 0; i < got; ++i) {
            ++position_now;
            char c = buffer[i];
            if (c == '\n') {
                if (!s.discarding && !line.empty()) {
                    if (line.back() == '\r')
                        line.pop_back();
                    UsageEvent e;
                    auto result = parse_usage(provider, line, source, s.parser, e);
                    if (result == ParseResult::Event && e.time + Retention >= now_seconds() &&
                        e.time <= now_seconds() + 86400)
                        store_.upsert(std::move(e));
                    else if (result == ParseResult::Invalid)
                        s.malformed = true;
                    else if (result == ParseResult::Unsupported)
                        s.unsupported = true;
                }
                line.clear();
                s.discarding = false;
                s.offset = position_now;
                dirty_ = true;
            } else if (!s.discarding) {
                if (line.size() < MaxLine)
                    line.push_back(c);
                else {
                    line.clear();
                    s.discarding = true;
                    s.malformed = true;
                }
            }
            if (s.discarding) {
                s.offset = position_now;
                dirty_ = true;
            }
        }
    }
    s.modified = modified;
    s.size = size;
    // Only complete lines advance the offset. A producer's unfinished tail is retried next scan.
    if (position_now < size)
        pending = true;
    return true;
}
Snapshot Collector::scan(HANDLE stop) {
    Snapshot snapshot;
    size_t before = store_.events.size();
    store_.prune(now_seconds());
    if (before != store_.events.size())
        dirty_ = true;
    for (int provider = 0; provider < 2; ++provider) {
        auto &row = snapshot.providers[provider];
        row.success = success_[provider];
        if (!settings_.enabled[provider]) {
            row.status = Status::Disabled;
            continue;
        }
        if (!directory(settings_.roots[provider])) {
            DWORD error = GetLastError();
            row.status = (error == ERROR_ACCESS_DENIED ? Status::ReadError : Status::Missing);
            row.total = store_.today(provider, now_seconds());
            continue;
        }
        bool error = false, pending = false, malformed = false, unsupported = false;
        auto logs = list_logs(settings_.roots[provider], error, stop);
        row.files = logs.size();
        if (stop && WaitForSingleObject(stop, 0) == WAIT_OBJECT_0)
            pending = true;
        std::set<std::wstring> present;
        size_t budget = 32 * 1024 * 1024;
        for (const auto &file : logs) {
            present.insert(file.path);
            if (stop && WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) {
                pending = true;
                break;
            }
            auto found = files_.find(file.path);
            if (found == files_.end() && files_.size() >= 20000) {
                store_.limited = true;
                continue;
            }
            auto &state = files_[file.path];
            state.provider = provider;
            bool changed = !state.identity || state.modified != file.modified || state.size != file.size ||
                           state.offset < file.size;
            if (changed) {
                if (budget) {
                    if (!read_file(file, provider, state, stop, pending, budget))
                        error = true;
                } else
                    pending = true;
            }
            malformed = malformed || state.malformed;
            unsupported = unsupported || state.unsupported;
        }
        if (!error && !pending) {
            for (auto it = files_.begin(); it != files_.end();) {
                if (it->second.provider == provider && !present.count(it->first)) {
                    it = files_.erase(it);
                    dirty_ = true;
                } else
                    ++it;
            }
        }
        row.total = store_.today(provider, now_seconds());
        if (error)
            row.status = Status::ReadError;
        else if (pending)
            row.status = Status::Collecting;
        else if (unsupported)
            row.status = Status::Unsupported;
        else if (malformed || store_.limited || row.total.overflow)
            row.status = Status::Partial;
        else
            row.status = row.total.events ? Status::Ready : Status::Empty;
        if (!error && !pending && !unsupported && !malformed) {
            success_[provider] = now_seconds();
            row.success = success_[provider];
        }
    }
    if (dirty_ && !(stop && WaitForSingleObject(stop, 0) == WAIT_OBJECT_0)) {
        cache_error_ = !save();
    }
    snapshot.cache_error = cache_error_;
    snapshot.cache_rebuilt = rebuilt_;
    snapshot.limited = store_.limited;
    return snapshot;
}
bool Collector::save() {
    auto doc = json(cJSON_CreateObject());
    put(doc.get(), "version", uint64_t(1));
    put(doc.get(), "limited", store_.limited);
    auto roots = cJSON_AddArrayToObject(doc.get(), "roots");
    for (const auto &root : settings_.roots)
        cJSON_AddItemToArray(roots, cJSON_CreateString(utf8(root).c_str()));
    auto events = cJSON_AddArrayToObject(doc.get(), "events");
    for (const auto &item : store_.events) {
        auto p = token_json(item.second.tokens);
        put(p, "p", uint64_t(item.second.provider));
        put(p, "k", item.first);
        put(p, "t", item.second.time);
        put(p, "u", item.second.updated);
        cJSON_AddItemToArray(events, p);
    }
    auto files = cJSON_AddArrayToObject(doc.get(), "files");
    for (const auto &item : files_) {
        const auto &s = item.second;
        auto p = cJSON_CreateObject();
        put(p, "path", utf8(item.first));
        put(p, "p", uint64_t(s.provider));
        put(p, "id", hex(s.identity));
        put(p, "modified", hex(s.modified));
        put(p, "size", s.size);
        put(p, "offset", s.offset);
        put(p, "prefix", hex(s.prefix));
        put(p, "prefix_length", s.prefix_length);
        put(p, "session", s.parser.session);
        put(p, "previous", s.parser.has_previous);
        put(p, "epoch", s.parser.epoch);
        put(p, "malformed", s.malformed);
        put(p, "unsupported", s.unsupported);
        put(p, "discarding", s.discarding);
        cJSON_AddItemToObject(p, "tokens", token_json(s.parser.previous));
        cJSON_AddItemToArray(files, p);
    }
    std::string text = dump(doc.get());
    if (text.empty() || text.size() > 10 * 1024 * 1024)
        return false;
    if (!atomic_write(join(data_dir_, L"state.json"), text))
        return false;
    dirty_ = false;
    return true;
}
static bool unhex(const cJSON *p, const char *key, uint64_t &out) {
    std::string s = str(p, key);
    if (s.size() != 16)
        return false;
    out = 0;
    for (char c : s) {
        unsigned d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 99;
        if (d > 15)
            return false;
        out = (out << 4) | d;
    }
    return true;
}
bool Collector::load() {
    std::wstring path = join(data_dir_, L"state.json");
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
        return true;
    rebuilt_ = true;
    std::string text;
    if (!read_text(path, text, 10 * 1024 * 1024))
        return false;
    auto doc = parse(text);
    uint64_t version = 0;
    if (!doc || !number(field(doc.get(), "version"), version) || version != 1)
        return false;
    auto roots = field(doc.get(), "roots");
    auto events = field(doc.get(), "events");
    auto files = field(doc.get(), "files");
    if (!cJSON_IsArray(roots) || cJSON_GetArraySize(roots) != 2 || !cJSON_IsArray(events) ||
        cJSON_GetArraySize(events) > static_cast<int>(MaxEvents) || !cJSON_IsArray(files) ||
        cJSON_GetArraySize(files) > 20000)
        return false;
    bool matching[2];
    for (int i = 0; i < 2; ++i)
        matching[i] =
            _wcsicmp(wide(str(cJSON_GetArrayItem(roots, i))).c_str(), settings_.roots[i].c_str()) == 0;
    EventStore store;
    std::map<std::wstring, FileState> restored;
    const cJSON *p = nullptr;
    cJSON_ArrayForEach(p, events) {
        UsageEvent e;
        uint64_t provider = 0;
        if (!number(field(p, "p"), provider) || provider > 1 || !number(field(p, "t"), e.time) || !e.time ||
            !number(field(p, "u"), e.updated) || e.updated < e.time || !token_json(p, e.tokens))
            return false;
        e.provider = static_cast<int>(provider);
        e.key = str(p, "k");
        if (e.key.empty() || e.key.size() > 4096)
            return false;
        if (matching[e.provider])
            store.upsert(std::move(e));
    }
    cJSON_ArrayForEach(p, files) {
        FileState s;
        uint64_t provider = 0;
        std::wstring filePath = wide(str(p, "path"));
        if (filePath.empty() || filePath.size() > 32000 || !number(field(p, "p"), provider) || provider > 1 ||
            !unhex(p, "id", s.identity) || !unhex(p, "modified", s.modified) ||
            !unhex(p, "prefix", s.prefix) || !number(field(p, "prefix_length"), s.prefix_length) ||
            s.prefix_length > 256 || !number(field(p, "size"), s.size) ||
            !number(field(p, "offset"), s.offset) || s.offset > s.size ||
            !number(field(p, "epoch"), s.parser.epoch) || !token_json(field(p, "tokens"), s.parser.previous))
            return false;
        s.provider = static_cast<int>(provider);
        s.parser.session = str(p, "session");
        if (s.parser.session.size() > 512)
            return false;
        s.parser.has_previous = cJSON_IsTrue(field(p, "previous"));
        s.malformed = cJSON_IsTrue(field(p, "malformed"));
        s.unsupported = cJSON_IsTrue(field(p, "unsupported"));
        s.discarding = cJSON_IsTrue(field(p, "discarding"));
        if (matching[s.provider])
            restored.emplace(filePath, std::move(s));
    }
    store.limited = cJSON_IsTrue(field(doc.get(), "limited"));
    store.prune(now_seconds());
    store_ = std::move(store);
    files_ = std::move(restored);
    rebuilt_ = false;
    return true;
}
const char *status_code(Status s) {
    switch (s) {
    case Status::Disabled:
        return "disabled";
    case Status::Collecting:
        return "collecting";
    case Status::Ready:
        return "ready";
    case Status::Empty:
        return "empty";
    case Status::Missing:
        return "missing";
    case Status::ReadError:
        return "read_error";
    case Status::Unsupported:
        return "unsupported";
    case Status::Partial:
        return "partial";
    }
    return "unknown";
}
} // namespace aimon
