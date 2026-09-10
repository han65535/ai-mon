#pragma once
#include "platform.hpp"

namespace aimon {
constexpr wchar_t StartupKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
std::wstring startup_command(const std::wstring &exe, const std::wstring &data);
std::wstring startup_value(const wchar_t *key = StartupKey);
bool set_startup_value(const std::wstring &value, const wchar_t *key = StartupKey);
bool remove_startup_for(const std::wstring &exe, const wchar_t *key = StartupKey);
} // namespace aimon
