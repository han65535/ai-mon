#include "startup.hpp"

namespace aimon {
static std::wstring quote(const std::wstring &value) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        out.append(slashes * (c == L'"' ? 2 : 1), L'\\');
        slashes = 0;
        if (c == L'"')
            out += L'\\';
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L'"';
}
std::wstring startup_command(const std::wstring &exe, const std::wstring &data) {
    return quote(exe) + L" --startup --data-dir " + quote(data);
}
std::wstring startup_value(const wchar_t *path) {
    wchar_t buffer[1024]{};
    DWORD size = sizeof(buffer), type = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, path, L"AI Mon", RRF_RT_REG_SZ, &type, buffer, &size) !=
        ERROR_SUCCESS)
        return {};
    return buffer;
}
bool set_startup_value(const std::wstring &value, const wchar_t *path) {
    if (value.size() > 260)
        return false;
    HKEY key = nullptr;
    LONG result = value.empty() ? RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_SET_VALUE, &key)
                                : RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, 0, KEY_SET_VALUE,
                                                  nullptr, &key, nullptr);
    if (result == ERROR_FILE_NOT_FOUND && value.empty())
        return true;
    if (result != ERROR_SUCCESS)
        return false;
    result = value.empty()
                 ? RegDeleteValueW(key, L"AI Mon")
                 : RegSetValueExW(key, L"AI Mon", 0, REG_SZ, reinterpret_cast<const BYTE *>(value.c_str()),
                                  static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return result == ERROR_SUCCESS || (value.empty() && result == ERROR_FILE_NOT_FOUND);
}
bool remove_startup_for(const std::wstring &exe, const wchar_t *path) {
    auto value = startup_value(path);
    auto prefix = quote(exe) + L" --startup --data-dir ";
    if (value.size() < prefix.size() || _wcsnicmp(value.c_str(), prefix.c_str(), prefix.size()) != 0)
        return true;
    return set_startup_value(L"", path);
}
} // namespace aimon
