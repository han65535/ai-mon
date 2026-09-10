#pragma once
#include "json.hpp"
#include "platform.hpp"
#include <array>
#include <map>

namespace aimon {
constexpr uint64_t Retention = 7 * 86400;
constexpr size_t MaxEvents = 24000;
constexpr size_t MaxLine = 2 * 1024 * 1024;
struct Tokens {
    uint64_t input = 0, output = 0, read = 0, write = 0;
    bool operator==(const Tokens &t) const {
        return input == t.input && output == t.output && read == t.read && write == t.write;
    }
};
struct UsageEvent {
    int provider = 0;
    std::string key;
    uint64_t time = 0, updated = 0;
    Tokens tokens;
};
struct ParseState {
    std::string session;
    Tokens previous;
    bool has_previous = false;
    uint64_t epoch = 0;
};
enum class ParseResult { Ignore, Event, Unsupported, Invalid };
ParseResult parse_usage(int provider, const std::string &line, const std::string &source, ParseState &state,
                        UsageEvent &event);
bool token_json(const cJSON *p, Tokens &out);
cJSON *token_json(const Tokens &t);
struct Totals {
    Tokens tokens;
    uint64_t events = 0;
    bool overflow = false;
};
struct EventStore {
    std::map<std::string, UsageEvent> events;
    bool limited = false;
    void upsert(UsageEvent event);
    void prune(uint64_t now);
    Totals today(int provider, uint64_t now) const;
};
struct Settings {
    std::array<bool, 2> enabled{{true, true}};
    std::array<std::wstring, 2> roots;
    int interval = 10;
    bool show_start = true;
    int mini_opacity = 100;
    std::string language = "auto";
};
Settings default_settings();
bool load_settings(const std::wstring &path, Settings &settings);
bool save_settings(const std::wstring &path, const Settings &settings);
} // namespace aimon
