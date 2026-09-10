#include "localization.hpp"
#include "json.hpp"
#include <algorithm>
#include <set>

namespace aimon {
bool valid_language_id(const std::string &id) {
    if (id.empty() || id.size() > 35 || id.front() == '-' || id.back() == '-')
        return false;
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
            return false;
    return id.find("--") == std::string::npos;
}
static std::set<std::wstring> placeholders(const std::wstring &s) {
    std::set<std::wstring> result;
    for (size_t i = 0; i < s.size(); ++i)
        if (s[i] == L'{') {
            size_t end = s.find(L'}', i + 1);
            if (end == std::wstring::npos) {
                result.insert(L"!invalid");
                break;
            }
            result.insert(s.substr(i + 1, end - i - 1));
            i = end;
        } else if (s[i] == L'}')
            result.insert(L"!invalid");
    return result;
}
bool parse_language_pack(const std::string &text, LanguagePack &result) {
    if (text.size() > 64 * 1024)
        return false;
    // cJSON strings are NUL terminated, so forbid embedded NUL escapes in packs.
    if (text.find("\\u0000") != std::string::npos)
        return false;
    auto doc = parse(text);
    uint64_t schema = 0;
    if (!doc || !number(field(doc.get(), "schema"), schema) || schema != 1)
        return false;
    LanguagePack candidate;
    candidate.id = str(doc.get(), "id");
    candidate.name = wide(str(doc.get(), "name"));
    if (!valid_language_id(candidate.id) || candidate.id == "auto" || candidate.name.empty() ||
        candidate.name.size() > 80 || candidate.name.find_first_of(L"\r\n\t") != std::wstring::npos)
        return false;
    const auto *strings = field(doc.get(), "strings");
    if (!cJSON_IsObject(strings))
        return false;
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, strings) {
        if (!item->string || !cJSON_IsString(item))
            return false;
        std::string key = item->string;
        std::wstring value = wide(str(item));
        if (key.empty() || key.size() > 80 || value.empty() || value.size() > 4096 ||
            candidate.strings.count(key))
            return false;
        candidate.strings.emplace(std::move(key), std::move(value));
        if (candidate.strings.size() > 256)
            return false;
    }
    if (candidate.strings.empty())
        return false;
    result = std::move(candidate);
    return true;
}
bool Localizer::initialize(HINSTANCE module, const std::wstring &data_folder) {
    builtin_.clear();
    for (int id : {201, 202}) {
        HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
        if (!resource)
            return false;
        HGLOBAL loaded = LoadResource(module, resource);
        const char *bytes = loaded ? static_cast<const char *>(LockResource(loaded)) : nullptr;
        LanguagePack pack;
        if (!bytes || !parse_language_pack(std::string(bytes, SizeofResource(module, resource)), pack))
            return false;
        builtin_.push_back(std::move(pack));
    }
    if (builtin_[0].id != "en" || builtin_[1].id != "ko")
        return false;
    folder_ = join(data_folder, L"languages");
    discover();
    select("auto");
    return true;
}
void Localizer::discover() {
    packs_ = builtin_;
    discovery_warning_ = false;
    if (folder_.empty() || !directory(folder_))
        return;
    WIN32_FIND_DATAW data{};
    HANDLE search = FindFirstFileW(join(folder_, L"*.json").c_str(), &data);
    if (search == INVALID_HANDLE_VALUE)
        return;
    size_t examined = 0;
    do {
        if (++examined > 32) {
            discovery_warning_ = true;
            break;
        }
        if (data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
            continue;
        std::string content;
        LanguagePack pack;
        if (!read_text(join(folder_, data.cFileName), content, 64 * 1024) ||
            !parse_language_pack(content, pack)) {
            discovery_warning_ = true;
            continue;
        }
        auto duplicate =
            std::find_if(packs_.begin(), packs_.end(), [&](const auto &p) { return p.id == pack.id; });
        if (duplicate != packs_.end()) {
            discovery_warning_ = true;
            continue;
        }
        // A partial community translation falls back to English per key. Invalid
        // named placeholders must never turn into printf/format instructions.
        for (auto it = pack.strings.begin(); it != pack.strings.end();) {
            auto english = builtin_[0].strings.find(it->first);
            if (english == builtin_[0].strings.end() ||
                placeholders(it->second) != placeholders(english->second)) {
                it = pack.strings.erase(it);
                discovery_warning_ = true;
            } else
                ++it;
        }
        packs_.push_back(std::move(pack));
    } while (FindNextFileW(search, &data));
    FindClose(search);
    std::sort(packs_.begin() + static_cast<ptrdiff_t>(builtin_.size()), packs_.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
}
void Localizer::select(const std::string &requested, const std::string &system_locale) {
    warning_ = discovery_warning_;
    std::string target = requested;
    if (target == "auto") {
        if (system_locale.empty()) {
            wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
            if (LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), locale,
                                 LOCALE_NAME_MAX_LENGTH, 0))
                target = utf8(locale);
            else
                target = "en";
        } else
            target = system_locale;
    }
    for (char &c : target)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    auto find = [&](const std::string &id) {
        return std::find_if(packs_.begin(), packs_.end(), [&](const auto &p) { return p.id == id; });
    };
    auto chosen = find(target);
    if (chosen == packs_.end() && requested == "auto")
        chosen = find(target.substr(0, target.find('-')));
    if (chosen == packs_.end()) {
        if (requested != "auto")
            warning_ = true;
        chosen = find("en");
    }
    active_ = builtin_.empty() ? std::map<std::string, std::wstring>{} : builtin_[0].strings;
    current_ = "en";
    if (chosen != packs_.end()) {
        current_ = chosen->id;
        for (const auto &item : chosen->strings)
            active_[item.first] = item.second;
    }
}
const wchar_t *Localizer::text(const char *key) const {
    auto found = active_.find(key);
    return found == active_.end() ? L"[Missing translation]" : found->second.c_str();
}
std::wstring Localizer::format(const char *key,
                               std::initializer_list<std::pair<std::wstring, std::wstring>> values) const {
    const std::wstring source = text(key);
    std::wstring result;
    for (size_t i = 0; i < source.size();) {
        if (source[i] == L'{') {
            size_t end = source.find(L'}', i + 1);
            if (end != std::wstring::npos) {
                std::wstring name = source.substr(i + 1, end - i - 1);
                bool matched = false;
                for (const auto &value : values)
                    if (value.first == name) {
                        result += value.second;
                        matched = true;
                        break;
                    }
                if (matched) {
                    i = end + 1;
                    continue;
                }
            }
        }
        result += source[i++];
    }
    return result;
}
} // namespace aimon
