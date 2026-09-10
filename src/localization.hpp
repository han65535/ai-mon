#pragma once
#include "platform.hpp"
#include <initializer_list>
#include <map>

namespace aimon {
struct LanguagePack {
    std::string id;
    std::wstring name;
    std::map<std::string, std::wstring> strings;
};
bool valid_language_id(const std::string &id);
bool parse_language_pack(const std::string &text, LanguagePack &result);
class Localizer {
  public:
    bool initialize(HINSTANCE module, const std::wstring &data_folder);
    void discover();
    void select(const std::string &requested, const std::string &system_locale = "");
    const wchar_t *text(const char *key) const;
    std::wstring format(const char *key,
                        std::initializer_list<std::pair<std::wstring, std::wstring>> values) const;
    const std::vector<LanguagePack> &languages() const { return packs_; }
    const std::string &current() const { return current_; }
    bool warning() const { return warning_; }

  private:
    std::wstring folder_;
    std::vector<LanguagePack> builtin_, packs_;
    std::map<std::string, std::wstring> active_;
    std::string current_ = "en";
    bool warning_ = false, discovery_warning_ = false;
};
} // namespace aimon
