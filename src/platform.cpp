#include "platform.hpp"
#include <algorithm>
#include <cstdio>
#include <cwchar>

namespace aimon {
std::wstring wide(const std::string &s) {
    int n =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring result(n, 0);
    if (n)
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()),
                            result.data(), n);
    return result;
}
std::string utf8(const std::wstring &s) {
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr,
                                0, nullptr, nullptr);
    std::string result(n, 0);
    if (n)
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()),
                            result.data(), n, nullptr, nullptr);
    return result;
}
std::wstring env(const wchar_t *name) {
    DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (!n)
        return {};
    std::wstring s(n, 0);
    DWORD written = GetEnvironmentVariableW(name, s.data(), n);
    if (written >= n)
        return {};
    s.resize(written);
    return s;
}
std::wstring join(const std::wstring &a, const std::wstring &b) {
    return a.empty() ? b : a + (a.back() == L'\\' ? L"" : L"\\") + b;
}
std::wstring canonical(const std::wstring &p) {
    if (p.empty())
        return {};
    DWORD n = GetFullPathNameW(p.c_str(), 0, nullptr, nullptr);
    if (!n)
        return p;
    std::wstring s(n, 0);
    DWORD written = GetFullPathNameW(p.c_str(), n, s.data(), nullptr);
    if (!written || written >= n)
        return p;
    s.resize(written);
    while (s.size() > 3 && (s.back() == L'\\' || s.back() == L'/'))
        s.pop_back();
    return s;
}
bool directory(const std::wstring &p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
bool ensure_directory(const std::wstring &p) {
    return directory(p) || CreateDirectoryW(p.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}
bool read_text(const std::wstring &p, std::string &out, size_t limit) {
    Handle h(CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    LARGE_INTEGER size{};
    if (!h.valid() || !GetFileSizeEx(h.value, &size) || size.QuadPart < 0 ||
        static_cast<uint64_t>(size.QuadPart) > limit)
        return false;
    out.resize(static_cast<size_t>(size.QuadPart));
    DWORD count = 0;
    return ReadFile(h.value, out.data(), static_cast<DWORD>(out.size()), &count, nullptr) &&
           count == out.size();
}
bool atomic_write(const std::wstring &p, const std::string &text) {
    std::wstring temp = p + L".tmp";
    Handle h(
        CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!h.valid())
        return false;
    DWORD count = 0;
    bool ok = WriteFile(h.value, text.data(), static_cast<DWORD>(text.size()), &count, nullptr) &&
              count == text.size() && FlushFileBuffers(h.value);
    h.reset();
    if (ok)
        ok =
            MoveFileExW(temp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!ok)
        DeleteFileW(temp.c_str());
    return ok;
}
uint64_t now_seconds() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER v{};
    v.LowPart = ft.dwLowDateTime;
    v.HighPart = ft.dwHighDateTime;
    return v.QuadPart / 10000000ULL - 11644473600ULL;
}
uint64_t timestamp(const std::string &iso) {
    if (iso.size() < 20)
        return 0;
    auto digits = [&](size_t i, size_t n) {
        unsigned v = 0;
        for (size_t k = 0; k < n; ++k) {
            char c = iso[i + k];
            if (c < '0' || c > '9')
                return 999999u;
            v = v * 10 + (c - '0');
        }
        return v;
    };
    if (iso[4] != '-' || iso[7] != '-' || iso[10] != 'T' || iso[13] != ':' || iso[16] != ':')
        return 0;
    SYSTEMTIME t{};
    unsigned year = digits(0, 4), month = digits(5, 2), day = digits(8, 2), hour = digits(11, 2),
             minute = digits(14, 2), second = digits(17, 2);
    if (year < 1970 || year > 9999 || month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 ||
        minute > 59 || second > 59)
        return 0;
    t.wYear = static_cast<WORD>(year);
    t.wMonth = static_cast<WORD>(month);
    t.wDay = static_cast<WORD>(day);
    t.wHour = static_cast<WORD>(hour);
    t.wMinute = static_cast<WORD>(minute);
    t.wSecond = static_cast<WORD>(second);
    size_t at = 19;
    if (iso[at] == '.') {
        ++at;
        size_t first = at;
        while (at < iso.size() && iso[at] >= '0' && iso[at] <= '9')
            ++at;
        if (at == first)
            return 0;
    }
    int offset = 0;
    if (at + 1 == iso.size() && iso[at] == 'Z') {
    } else if (at + 6 == iso.size() && (iso[at] == '+' || iso[at] == '-') && iso[at + 3] == ':') {
        unsigned oh = digits(at + 1, 2), om = digits(at + 4, 2);
        if (oh > 23 || om > 59)
            return 0;
        offset = static_cast<int>(oh * 3600 + om * 60) * (iso[at] == '+' ? 1 : -1);
    } else
        return 0;
    FILETIME ft{};
    if (!SystemTimeToFileTime(&t, &ft))
        return 0;
    SYSTEMTIME checked{};
    FileTimeToSystemTime(&ft, &checked);
    if (checked.wDay != t.wDay || checked.wMonth != t.wMonth)
        return 0;
    ULARGE_INTEGER v{};
    v.LowPart = ft.dwLowDateTime;
    v.HighPart = ft.dwHighDateTime;
    int64_t seconds = static_cast<int64_t>(v.QuadPart / 10000000ULL) - 11644473600LL - offset;
    return seconds > 0 ? static_cast<uint64_t>(seconds) : 0;
}
int local_day(uint64_t seconds) {
    if (seconds > 253402300799ULL)
        return 0;
    ULARGE_INTEGER v{};
    v.QuadPart = (seconds + 11644473600ULL) * 10000000ULL;
    FILETIME ft{v.LowPart, v.HighPart};
    SYSTEMTIME utc{}, local{};
    if (!FileTimeToSystemTime(&ft, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
        return 0;
    return local.wYear * 10000 + local.wMonth * 100 + local.wDay;
}
uint64_t hash_bytes(const char *bytes, size_t length) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < length; ++i) {
        h ^= static_cast<unsigned char>(bytes[i]);
        h *= 1099511628211ULL;
    }
    return h;
}
std::string hex(uint64_t value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(value));
    return buf;
}
std::wstring grouped(uint64_t value) {
    std::wstring s = std::to_wstring(value);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3)
        s.insert(i, L",");
    return s;
}
std::vector<FileInfo> list_logs(const std::wstring &root, bool &error, HANDLE stop) {
    std::vector<FileInfo> result;
    std::vector<std::pair<std::wstring, int>> pending{{root, 0}};
    size_t visited = 0;
    while (!pending.empty()) {
        if (stop && WaitForSingleObject(stop, 0) == WAIT_OBJECT_0)
            break;
        auto item = std::move(pending.back());
        pending.pop_back();
        if (++visited > 50000 || item.second > 48) {
            error = true;
            break;
        }
        WIN32_FIND_DATAW fd{};
        HANDLE find = FindFirstFileW(join(item.first, L"*").c_str(), &fd);
        if (find == INVALID_HANDLE_VALUE) {
            if (GetLastError() != ERROR_FILE_NOT_FOUND)
                error = true;
            continue;
        }
        do {
            if (fd.cFileName[0] == L'.' &&
                (!fd.cFileName[1] || (fd.cFileName[1] == L'.' && !fd.cFileName[2])))
                continue;
            std::wstring p = join(item.first, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                pending.emplace_back(p, item.second + 1);
            else {
                const wchar_t *ext = wcsrchr(fd.cFileName, L'.');
                if (ext && _wcsicmp(ext, L".jsonl") == 0) {
                    ULARGE_INTEGER size{}, modified{};
                    size.LowPart = fd.nFileSizeLow;
                    size.HighPart = fd.nFileSizeHigh;
                    modified.LowPart = fd.ftLastWriteTime.dwLowDateTime;
                    modified.HighPart = fd.ftLastWriteTime.dwHighDateTime;
                    result.push_back({p, size.QuadPart, modified.QuadPart});
                    if (result.size() >= 20000) {
                        error = true;
                        break;
                    }
                }
            }
        } while (FindNextFileW(find, &fd));
        DWORD last = GetLastError();
        FindClose(find);
        if (last != ERROR_NO_MORE_FILES && result.size() < 20000)
            error = true;
        if (result.size() >= 20000)
            break;
    }
    std::sort(result.begin(), result.end(),
              [](const FileInfo &a, const FileInfo &b) { return a.modified > b.modified; });
    return result;
}
Watch::~Watch() {
    close();
}
void Watch::close() {
    if (folder.valid()) {
        CancelIoEx(folder.value, &overlapped);
        if (event.valid()) {
            DWORD ignored;
            GetOverlappedResult(folder.value, &overlapped, &ignored, TRUE);
        }
    }
    folder.reset();
    event.reset();
    path.clear();
}
bool Watch::arm() {
    if (!folder.valid())
        return false;
    ResetEvent(event.value);
    overlapped = {};
    overlapped.hEvent = event.value;
    return ReadDirectoryChangesW(folder.value, buffer, sizeof(buffer), TRUE,
                                 FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                     FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
                                 nullptr, &overlapped, nullptr) != FALSE;
}
void Watch::start(const std::wstring &root) {
    close();
    path = root;
    folder.reset(CreateFileW(root.c_str(), FILE_LIST_DIRECTORY,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                             FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr));
    if (!folder.valid())
        return;
    event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event.valid() || !arm())
        close();
}
} // namespace aimon
