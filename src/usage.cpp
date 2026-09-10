#include "usage.hpp"
#include "localization.hpp"
#include <algorithm>
#include <limits>

namespace aimon {
static bool optional_number(const cJSON *p, const char *name, uint64_t &out) {
    const cJSON *v = field(p, name);
    out = 0;
    return !v || cJSON_IsNull(v) || number(v, out);
}
bool token_json(const cJSON *p, Tokens &t) {
    return cJSON_IsObject(p) && number(field(p, "i"), t.input) && number(field(p, "o"), t.output) &&
           number(field(p, "r"), t.read) && number(field(p, "w"), t.write);
}
cJSON *token_json(const Tokens &t) {
    auto p = cJSON_CreateObject();
    put(p, "i", t.input);
    put(p, "o", t.output);
    put(p, "r", t.read);
    put(p, "w", t.write);
    return p;
}
static bool codex_tokens(const cJSON *p, Tokens &t) {
    return cJSON_IsObject(p) && number(field(p, "input_tokens"), t.input) &&
           number(field(p, "output_tokens"), t.output) && optional_number(p, "cached_input_tokens", t.read) &&
           optional_number(p, "cache_write_input_tokens", t.write) && t.read <= t.input && t.write <= t.input;
}
static std::string piece(const std::string &s) {
    return std::to_string(s.size()) + ":" + s;
}
ParseResult parse_usage(int provider, const std::string &line, const std::string &source, ParseState &state,
                        UsageEvent &e) {
    auto root = parse(line);
    if (!root || !cJSON_IsObject(root.get()))
        return ParseResult::Invalid;
    auto p = root.get();
    std::string type = str(p, "type");
    e = {};
    e.provider = provider;
    if (provider == 0) {
        if (type != "assistant")
            return ParseResult::Ignore;
        auto msg = field(p, "message");
        auto usage = field(msg, "usage");
        if (!usage || cJSON_IsNull(usage))
            return ParseResult::Ignore;
        std::string message = str(msg, "id"), request = str(p, "requestId"), session = str(p, "sessionId");
        if (message.empty() || message.size() > 512 || request.size() > 512 || session.size() > 512)
            return ParseResult::Unsupported;
        if (!number(field(usage, "input_tokens"), e.tokens.input) ||
            !number(field(usage, "output_tokens"), e.tokens.output) ||
            !optional_number(usage, "cache_read_input_tokens", e.tokens.read) ||
            !optional_number(usage, "cache_creation_input_tokens", e.tokens.write))
            return ParseResult::Unsupported;
        // Claude's uncached input and cache fields are disjoint; show total input.
        e.tokens.input += e.tokens.read + e.tokens.write;
        if (e.tokens.input > 9007199254740991ULL)
            return ParseResult::Invalid;
        e.key = "c:" + piece(session.empty() ? source : session) + piece(request) + piece(message);
        e.time = timestamp(str(p, "timestamp"));
        return e.time ? ParseResult::Event : ParseResult::Invalid;
    }
    auto payload = field(p, "payload");
    if (type == "session_meta") {
        std::string id = str(payload, "id");
        if (!id.empty() && id.size() <= 512) {
            if (!state.session.empty() && state.session != id) {
                state.previous = {};
                state.has_previous = false;
                state.epoch = 0;
            }
            state.session = id;
        }
        return ParseResult::Ignore;
    }
    if (type != "event_msg" || str(payload, "type") != "token_count")
        return ParseResult::Ignore;
    auto info = field(payload, "info");
    if (!info || cJSON_IsNull(info))
        return ParseResult::Ignore;
    Tokens total;
    if (!codex_tokens(field(info, "total_token_usage"), total))
        return ParseResult::Unsupported;
    e.time = timestamp(str(p, "timestamp"));
    if (!e.time)
        return ParseResult::Invalid;
    if (state.has_previous && total == state.previous)
        return ParseResult::Ignore;
    bool reset =
        state.has_previous && (total.input < state.previous.input || total.output < state.previous.output ||
                               total.read < state.previous.read || total.write < state.previous.write);
    if (reset)
        ++state.epoch;
    if (state.has_previous && !reset) {
        e.tokens = {total.input - state.previous.input, total.output - state.previous.output,
                    total.read - state.previous.read, total.write - state.previous.write};
    } else {
        // A truncated/resumed file may begin at a nonzero total. Only attribute the last reported request.
        Tokens last;
        e.tokens = codex_tokens(field(info, "last_token_usage"), last) && last.input <= total.input &&
                           last.output <= total.output
                       ? last
                       : total;
    }
    state.previous = total;
    state.has_previous = true;
    e.key = "x:" + piece(state.session.empty() ? source : state.session) + std::to_string(state.epoch) + ":" +
            std::to_string(total.input) + ":" + std::to_string(total.output) + ":" +
            std::to_string(total.read) + ":" + std::to_string(total.write);
    return ParseResult::Event;
}
void EventStore::upsert(UsageEvent e) {
    if (!e.updated)
        e.updated = e.time;
    auto found = events.find(e.key);
    if (found != events.end()) {
        // Repeated streamed Claude records keep the first timestamp; the latest usage corrects the value.
        if (e.provider == 0) {
            if (e.updated < found->second.updated)
                return;
            e.time = std::min(e.time, found->second.time);
        }
        if (e.provider == 1)
            return; // Cumulative snapshot identity is already represented.
        found->second = std::move(e);
    } else {
        if (events.size() >= MaxEvents) {
            limited = true;
            return;
        }
        events.emplace(e.key, std::move(e));
    }
}
void EventStore::prune(uint64_t now) {
    for (auto it = events.begin(); it != events.end();) {
        if (it->second.time + Retention < now || it->second.time > now + 86400)
            it = events.erase(it);
        else
            ++it;
    }
}
Totals EventStore::today(int provider, uint64_t now) const {
    Totals sum;
    int day = local_day(now);
    auto add = [&](uint64_t &a, uint64_t b) {
        if (b > std::numeric_limits<uint64_t>::max() - a)
            sum.overflow = true;
        else
            a += b;
    };
    for (const auto &item : events) {
        const auto &e = item.second;
        if (e.provider != provider || local_day(e.time) != day)
            continue;
        add(sum.tokens.input, e.tokens.input);
        add(sum.tokens.output, e.tokens.output);
        add(sum.tokens.read, e.tokens.read);
        add(sum.tokens.write, e.tokens.write);
        ++sum.events;
    }
    return sum;
}
Settings default_settings() {
    Settings s;
    auto home = env(L"USERPROFILE");
    auto claude = env(L"CLAUDE_CONFIG_DIR");
    auto codex = env(L"CODEX_HOME");
    s.roots[0] = canonical(join(claude.empty() ? join(home, L".claude") : claude, L"projects"));
    s.roots[1] = canonical(join(codex.empty() ? join(home, L".codex") : codex, L"sessions"));
    return s;
}
bool load_settings(const std::wstring &path, Settings &s) {
    std::string text;
    if (!read_text(path, text, 65536))
        return false;
    auto doc = parse(text);
    uint64_t version = 0, interval = 0;
    if (!doc || !number(field(doc.get(), "version"), version) || version != 1 ||
        !number(field(doc.get(), "interval"), interval) || interval < 2 || interval > 300)
        return false;
    auto show = field(doc.get(), "show_start");
    auto providers = field(doc.get(), "providers");
    if (!cJSON_IsBool(show) || !cJSON_IsArray(providers) || cJSON_GetArraySize(providers) != 2)
        return false;
    Settings result;
    result.interval = static_cast<int>(interval);
    result.show_start = cJSON_IsTrue(show);
    if (const auto *language = field(doc.get(), "language")) {
        if (!cJSON_IsString(language) || !valid_language_id(str(language)))
            return false;
        result.language = str(language);
    }
    for (int i = 0; i < 2; ++i) {
        auto p = cJSON_GetArrayItem(providers, i);
        auto enabled = field(p, "enabled");
        std::wstring pathValue = wide(str(p, "path"));
        if (!cJSON_IsBool(enabled) || pathValue.empty() || pathValue.size() > 32000)
            return false;
        result.enabled[i] = cJSON_IsTrue(enabled);
        result.roots[i] = canonical(pathValue);
    }
    s = std::move(result);
    return true;
}
bool save_settings(const std::wstring &path, const Settings &s) {
    auto doc = json(cJSON_CreateObject());
    put(doc.get(), "version", uint64_t(1));
    put(doc.get(), "interval", uint64_t(s.interval));
    put(doc.get(), "show_start", s.show_start);
    put(doc.get(), "language", s.language);
    auto providers = cJSON_AddArrayToObject(doc.get(), "providers");
    for (int i = 0; i < 2; ++i) {
        auto p = cJSON_CreateObject();
        put(p, "enabled", s.enabled[i]);
        put(p, "path", utf8(s.roots[i]));
        cJSON_AddItemToArray(providers, p);
    }
    return atomic_write(path, dump(doc.get()));
}
} // namespace aimon
