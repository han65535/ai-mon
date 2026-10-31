#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <atomic>
using HANDLE = const std::atomic_bool *;
using HINSTANCE = void *;
#endif
#include <string>
#include <vector>
#include <cstdint>

namespace aimon {
#ifdef _WIN32
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { reset(); }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    Handle(Handle &&h) noexcept : value(h.value) { h.value = INVALID_HANDLE_VALUE; }
    Handle &operator=(Handle &&h) noexcept {
        if (this != &h) {
            reset();
            value = h.value;
            h.value = INVALID_HANDLE_VALUE;
        }
        return *this;
    }
    bool valid() const { return value && value != INVALID_HANDLE_VALUE; }
    void reset(HANDLE h = INVALID_HANDLE_VALUE) {
        if (valid())
            CloseHandle(value);
        value = h;
    }
};
#endif
std::wstring wide(const std::string &s);
std::string utf8(const std::wstring &s);
std::wstring env(const wchar_t *name);
std::wstring join(const std::wstring &a, const std::wstring &b);
std::wstring canonical(const std::wstring &p);
bool directory(const std::wstring &p);
bool ensure_directory(const std::wstring &p);
bool read_text(const std::wstring &p, std::string &out, size_t limit);
bool atomic_write(const std::wstring &p, const std::string &text);
uint64_t now_seconds();
uint64_t timestamp(const std::string &iso);
int local_day(uint64_t seconds);
uint64_t hash_bytes(const char *bytes, size_t length);
std::string hex(uint64_t value);
std::wstring grouped(uint64_t value);
struct FileInfo {
    std::wstring path;
    uint64_t size = 0, modified = 0, identity = 0;
};
std::vector<FileInfo> list_logs(const std::wstring &root, bool &error, HANDLE stop = nullptr);
#ifdef _WIN32
struct Watch {
    Handle folder, event;
    OVERLAPPED overlapped{};
    alignas(DWORD) char buffer[32768]{};
    std::wstring path;
    ~Watch();
    void start(const std::wstring &root);
    bool arm();
    void close();
};
#endif
} // namespace aimon
