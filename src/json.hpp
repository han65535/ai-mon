#pragma once
#include "cJSON.h"
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

namespace aimon {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
inline Json json(cJSON *p = nullptr) {
    return Json(p, cJSON_Delete);
}
inline const cJSON *field(const cJSON *p, const char *name) {
    return cJSON_GetObjectItemCaseSensitive(p, name);
}
inline std::string str(const cJSON *p) {
    return cJSON_IsString(p) && p->valuestring ? p->valuestring : "";
}
inline std::string str(const cJSON *p, const char *name) {
    return str(field(p, name));
}
inline bool number(const cJSON *p, uint64_t &value) {
    // cJSON uses doubles: do not silently round unsafe integers.
    if (!cJSON_IsNumber(p) || !std::isfinite(p->valuedouble) || p->valuedouble < 0 ||
        p->valuedouble > 9007199254740991.0 || std::floor(p->valuedouble) != p->valuedouble)
        return false;
    value = static_cast<uint64_t>(p->valuedouble);
    return true;
}
inline bool valid_utf8(const std::string &s) {
#ifdef _WIN32
    return !s.empty() && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
#else
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i++]);
        if (c < 0x80) continue;
        unsigned length = c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
        if (!length || i + length - 1 > s.size()) return false;
        unsigned char first = static_cast<unsigned char>(s[i]);
        if ((c == 0xe0 && first < 0xa0) || (c == 0xed && first >= 0xa0) ||
            (c == 0xf0 && first < 0x90) || (c == 0xf4 && first >= 0x90)) return false;
        for (unsigned j = 1; j < length; ++j)
            if ((static_cast<unsigned char>(s[i++]) & 0xc0) != 0x80) return false;
    }
    return true;
#endif
}
inline Json parse(const std::string &s) {
    if (s.empty() || s.size() > 12 * 1024 * 1024 || s.find('\0') != std::string::npos ||
        !valid_utf8(s))
        return json();
    const char *end = nullptr;
    auto p = json(cJSON_ParseWithLengthOpts(s.c_str(), s.size() + 1, &end, 1));
    return p;
}
inline std::string dump(const cJSON *p) {
    char *text = cJSON_PrintUnformatted(p);
    if (!text)
        return {};
    std::string result(text);
    cJSON_free(text);
    return result;
}
inline void put(cJSON *p, const char *name, const std::string &v) {
    cJSON_AddStringToObject(p, name, v.c_str());
}
inline void put(cJSON *p, const char *name, uint64_t v) {
    cJSON_AddNumberToObject(p, name, static_cast<double>(v));
}
inline void put(cJSON *p, const char *name, bool v) {
    cJSON_AddBoolToObject(p, name, v);
}
} // namespace aimon
