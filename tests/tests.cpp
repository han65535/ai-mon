#include "collector.hpp"
#include "localization.hpp"
#include "claude_bridge.hpp"
#include "codex_bridge.hpp"
#include "startup.hpp"
#include <cstdio>
#include <stdexcept>
#include <limits>

using namespace aimon;
namespace {
int checks = 0;
void check(bool value, const char *label) {
    ++checks;
    if (!value)
        throw std::runtime_error(label);
}
std::string iso_now() {
    SYSTEMTIME t{};
    GetSystemTime(&t);
    char b[48];
    std::snprintf(b, sizeof(b), "%04u-%02u-%02uT%02u:%02u:%02uZ", t.wYear, t.wMonth, t.wDay, t.wHour,
                  t.wMinute, t.wSecond);
    return b;
}
std::string claude(const std::string &id, uint64_t input, uint64_t output,
                   const std::string &time = iso_now()) {
    return "{\"type\":\"assistant\",\"sessionId\":\"synthetic-session\",\"requestId\":\"req-" + id +
           "\",\"timestamp\":\"" + time + "\",\"message\":{\"id\":\"" + id +
           "\",\"usage\":{\"input_tokens\":" + std::to_string(input) +
           ",\"output_tokens\":" + std::to_string(output) +
           ",\"cache_read_input_tokens\":20,\"cache_creation_input_tokens\":5}}}";
}
std::string codex(uint64_t input, uint64_t output, uint64_t read, const std::string &time = iso_now(),
                  const std::string &last = "") {
    return "{\"type\":\"event_msg\",\"timestamp\":\"" + time +
           "\",\"payload\":{\"type\":\"token_count\",\"info\":{\"total_token_usage\":{\"input_tokens\":" +
           std::to_string(input) + ",\"output_tokens\":" + std::to_string(output) +
           ",\"cached_input_tokens\":" + std::to_string(read) + "}" +
           (last.empty() ? "" : ",\"last_token_usage\":" + last) + "}}}";
}
void append(const std::wstring &path, const std::string &text) {
    Handle h(CreateFileW(path.c_str(), FILE_APPEND_DATA,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr));
    DWORD n = 0;
    check(h.valid() && WriteFile(h.value, text.data(), static_cast<DWORD>(text.size()), &n, nullptr) &&
              n == text.size(),
          "append fixture");
}
void parser_tests() {
    auto good = parse("{\"a\":\"\\uD55C\\uAE00 \\uD83D\\uDE00\",\"n\":42}");
    check(bool(good), "JSON unicode");
    check(!str(good.get(), "a").empty(), "unicode decode");
    check(!parse("{} trailing"), "reject trailing content");
    check(!parse(std::string("{\"a\":\"") + char(0xff) + "\"}"), "reject invalid UTF8");
    check(!parse(std::string("{}\0junk", 7)), "reject embedded null");
    std::string deep;
    for (int i = 0; i < 70; ++i)
        deep += '[';
    deep += '0';
    for (int i = 0; i < 70; ++i)
        deep += ']';
    check(!parse(deep), "depth bounded");
    uint64_t n = 0;
    auto values = parse("[0,-1,1.5,9007199254740992,1e999]");
    check(number(cJSON_GetArrayItem(values.get(), 0), n) && n == 0, "zero accepted");
    for (int i = 1; i < 5; ++i)
        check(!number(cJSON_GetArrayItem(values.get(), i), n), "unsafe numeric rejected");
    check(timestamp("2026-09-10T09:00:00+09:00") == timestamp("2026-09-10T00:00:00Z"),
          "UTC offset equivalence");
    check(timestamp("2026-09-10T00:00:00.123Z") == timestamp("2026-09-10T00:00:00Z"), "fractional second");
    check(!timestamp("2026-02-30T00:00:00Z"), "invalid calendar date");
    check(!timestamp("2026-09-10T25:00:00Z") && !timestamp("2026-09-10T00:00:00"),
          "invalid/missing timezone");
    ParseState state;
    UsageEvent e;
    check(parse_usage(0, claude("m1", 100, 10), "file", state, e) == ParseResult::Event, "Claude event");
    check(e.tokens.input == 125 && e.tokens.output == 10 && e.tokens.read == 20 && e.tokens.write == 5,
          "Claude cache semantics");
    EventStore store;
    store.upsert(e);
    store.upsert(e);
    check(store.today(0, now_seconds()).events == 1, "Claude duplicate id");
    UsageEvent corrected = e;
    corrected.time = e.time + 1;
    corrected.tokens.output = 15;
    store.upsert(corrected);
    store.upsert(e);
    check(store.today(0, now_seconds()).tokens.output == 15, "older duplicate cannot undo latest correction");
    corrected.time = e.time + 2;
    corrected.tokens.output = 12;
    store.upsert(corrected);
    check(store.today(0, now_seconds()).tokens.output == 12, "newer correction can lower count");
    check(parse_usage(0, "{\"type\":\"assistant\",\"message\":{\"usage\":{}}}", "file", state, e) ==
              ParseResult::Unsupported,
          "unknown usage fields");
    check(parse_usage(0, "{\"type\":\"user\"}", "file", state, e) == ParseResult::Ignore,
          "ignore conversation");
    check(parse_usage(0, "{", "file", state, e) == ParseResult::Invalid, "malformed JSON");
    state = {};
    check(parse_usage(1, "{\"type\":\"session_meta\",\"payload\":{\"id\":\"s1\"}}", "file", state, e) ==
              ParseResult::Ignore,
          "Codex metadata");
    check(parse_usage(1, "{\"type\":\"event_msg\",\"payload\":{\"type\":\"token_count\",\"info\":null}}",
                      "file", state, e) == ParseResult::Ignore,
          "null quota-only message");
    check(parse_usage(1, codex(100, 10, 40), "file", state, e) == ParseResult::Event && e.tokens.input == 100,
          "first cumulative counter");
    EventStore cs;
    cs.upsert(e);
    check(parse_usage(1, codex(100, 10, 40), "file", state, e) == ParseResult::Ignore,
          "duplicate cumulative ignored");
    check(parse_usage(1, codex(160, 15, 60), "file", state, e) == ParseResult::Event &&
              e.tokens.input == 60 && e.tokens.output == 5 && e.tokens.read == 20,
          "cumulative delta");
    cs.upsert(e);
    check(cs.today(1, now_seconds()).tokens.input == 160, "no sum of cumulative totals");
    check(parse_usage(1, codex(20, 2, 0), "file", state, e) == ParseResult::Event && e.tokens.input == 20 &&
              state.epoch == 1,
          "counter reset");
    ParseState resumed;
    check(parse_usage(1,
                      codex(1000, 100, 100, iso_now(),
                            "{\"input_tokens\":50,\"output_tokens\":5,\"cached_input_tokens\":10}"),
                      "file", resumed, e) == ParseResult::Event &&
              e.tokens.input == 50,
          "resumed first snapshot attribution");
    check(parse_usage(1, codex(10, 1, 50), "file", resumed, e) == ParseResult::Unsupported,
          "invalid cache exceeds input");
    UsageEvent old;
    old.key = "old";
    old.time = now_seconds() - Retention - 1;
    old.tokens.input = 500;
    store.upsert(old);
    store.prune(now_seconds());
    check(!store.events.count("old"), "retention prune");
    UsageEvent huge;
    huge.provider = 0;
    huge.key = "huge";
    huge.time = now_seconds();
    huge.tokens.input = std::numeric_limits<uint64_t>::max();
    store.upsert(huge);
    check(store.today(0, now_seconds()).overflow, "overflow flagged");
}
void collector_tests(const std::wstring &root) {
    check(ensure_directory(root), "test directory");
    std::wstring c = join(root, L"claude"), x = join(root, L"codex"), data = join(root, L"data");
    check(ensure_directory(c) && ensure_directory(x) && ensure_directory(data), "fixture directories");
    {
        Watch watch;
        watch.start(c);
        check(watch.folder.valid() && watch.event.valid(), "directory watcher start");
        append(join(c, L"watch.tmp"), "change");
        check(WaitForSingleObject(watch.event.value, 2000) == WAIT_OBJECT_0, "directory change notification");
        DWORD bytes = 0;
        check(GetOverlappedResult(watch.folder.value, &watch.overlapped, &bytes, FALSE) != FALSE,
              "read watch completion");
        check(watch.arm(), "directory watcher rearm");
        watch.close();
        check(!watch.folder.valid(), "cancel pending directory watch");
    }
    Settings s;
    s.roots = {{c, x}};
    std::wstring cf = join(c, L"session.jsonl"), xf = join(x, L"rollout.jsonl");
    std::string first = claude("one", 100, 10);
    check(atomic_write(cf, first + "\n" + first + "\n" + claude("two", 200, 20)),
          "create Claude partial tail");
    check(atomic_write(xf, "{\"type\":\"session_meta\",\"payload\":{\"id\":\"s\"}}\n" + codex(100, 10, 40) +
                               "\n" + codex(150, 15, 50) + "\n"),
          "create Codex fixture");
    Collector collector(data);
    collector.configure(s);
    auto snap = collector.scan();
    check(snap.providers[0].total.tokens.input == 125 && snap.providers[0].total.events == 1,
          "complete lines only and dedup");
    check(snap.providers[1].total.tokens.input == 150 && snap.providers[1].total.tokens.output == 15,
          "integrated cumulative");
    append(cf, "\n");
    snap = collector.scan();
    check(snap.providers[0].total.tokens.input == 350 && snap.providers[0].total.events == 2,
          "tail completion");
    uint64_t read = collector.bytes_read();
    snap = collector.scan();
    check(collector.bytes_read() == read, "idle does not reread body");
    check(!snap.cache_error, "cache saved");
    Collector restart(data);
    restart.configure(s);
    check(restart.load(), "load cache");
    snap = restart.scan();
    check(snap.providers[0].total.tokens.input == 350 && snap.providers[1].total.tokens.input == 150,
          "restart preserves totals");
    check(restart.bytes_read() == 0, "restart checkpoint skips unchanged body");
    append(xf, codex(200, 20, 70) + "\n");
    snap = restart.scan();
    check(snap.providers[1].total.tokens.input == 200, "restored previous cumulative counter");
    append(cf, "{broken}\n");
    snap = restart.scan();
    check(snap.providers[0].status == Status::Partial, "malformed data status");
    check(snap.providers[1].status == Status::Ready, "provider failure isolation");
    check(atomic_write(cf, claude("three", 300, 30) + "\n"), "replace source file");
    snap = restart.scan();
    check(snap.providers[0].total.tokens.input == 675, "replacement detected with prior retained history");
    append(cf, std::string(MaxLine + 100, 'x') + "\n" + claude("four", 400, 40) + "\n");
    snap = restart.scan();
    check(snap.providers[0].total.tokens.input == 1100 && snap.providers[0].status == Status::Partial,
          "oversized line recovery");
    Settings disabled = s;
    disabled.enabled[0] = false;
    restart.configure(disabled);
    snap = restart.scan();
    check(snap.providers[0].status == Status::Disabled, "disabled provider");
    auto savedSettings = join(data, L"settings.json");
    s.interval = 15;
    s.show_start = false;
    check(save_settings(savedSettings, s), "save settings");
    Settings restored;
    check(load_settings(savedSettings, restored) && restored.interval == 15 && !restored.show_start &&
              restored.roots[0] == c,
          "settings roundtrip");
    check(atomic_write(savedSettings, "{}"), "corrupt settings fixture");
    check(!load_settings(savedSettings, restored), "invalid settings rejected");
    std::wstring empty = join(root, L"empty");
    check(ensure_directory(empty), "empty root");
    Settings changed = s;
    changed.roots[0] = empty;
    restart.configure(changed);
    snap = restart.scan();
    check(snap.providers[0].total.events == 0 && snap.providers[0].status == Status::Empty,
          "path change resets selected scope");
    check(atomic_write(join(data, L"state.json"), "{partial"), "simulate interrupted state");
    Collector corrupt(data);
    corrupt.configure(s);
    check(!corrupt.load(), "corrupt state rejected atomically");
    snap = corrupt.scan();
    check(snap.providers[1].total.tokens.input == 200 && snap.cache_rebuilt, "rebuild from available source");
    Settings missing = s;
    missing.roots[0] = join(root, L"missing");
    corrupt.configure(missing);
    snap = corrupt.scan();
    check(snap.providers[0].status == Status::Missing, "missing directory");
    Handle stop(CreateEventW(nullptr, TRUE, TRUE, nullptr));
    snap = corrupt.scan(stop.value);
    check(snap.providers[1].status == Status::Collecting, "cancellation preserves collecting status");
}
void language_tests(const std::wstring &root) {
    LanguagePack pack;
    check(valid_language_id("pt-br") && !valid_language_id("../en") && !valid_language_id("en--us"),
          "language id validation");
    check(!parse_language_pack(R"({"schema":2,"id":"fr","name":"French","strings":{"action.open":"Ouvrir"}})",
                               pack),
          "reject unsupported language schema");
    check(!parse_language_pack(
              R"({"schema":1,"id":"auto","name":"Automatic","strings":{"action.open":"Open"}})", pack),
          "auto is not a pack id");
    check(!parse_language_pack(
              R"({"schema":1,"id":"fr","name":"French","strings":{"action.open":"A","action.open":"B"}})",
              pack),
          "reject duplicate translation keys");
    check(!parse_language_pack(
              R"({"schema":1,"id":"fr","name":"French","strings":{"action.open":"A\u0000B"}})", pack),
          "reject embedded NUL");
    check(!parse_language_pack(std::string(65537, ' '), pack), "language file size bound");
    Localizer locale;
    auto data = join(root, L"localization");
    check(ensure_directory(data) && ensure_directory(join(data, L"languages")),
          "create language test folder");
    check(locale.initialize(GetModuleHandleW(nullptr), data), "load embedded language resources");
    check(locale.languages().size() == 2 &&
              locale.languages()[0].strings.size() == locale.languages()[1].strings.size(),
          "embedded languages have matching key count");
    for (const auto &item : locale.languages()[0].strings)
        check(locale.languages()[1].strings.count(item.first) == 1, "Korean translation completeness");
    locale.select("auto", "ko-KR");
    check(locale.current() == "ko" && std::wstring(locale.text("action.cancel")) == L"취소",
          "automatic Korean UI language");
    locale.select("auto", "en-US");
    check(locale.current() == "en" && !locale.warning(), "automatic English UI language");
    locale.select("auto", "de-DE");
    check(locale.current() == "en" && !locale.warning(), "unsupported OS language falls back to English");
    locale.select("ko", "en-US");
    check(locale.current() == "ko", "explicit language overrides OS");
    locale.select("en");
    check(locale.format("summary.updated", {{L"seconds", L"12"}, {L"status", L"{seconds}"}}) ==
              L"{seconds}  ·  Updated 12s ago",
          "replacement text is not recursively formatted");
    auto file = join(join(data, L"languages"), L"fr.json");
    check(
        atomic_write(
            file,
            R"({"schema":1,"id":"fr","name":"Français","strings":{"action.open":"Ouvrir","summary.updated":"{seconds}s : {status}"}})"),
        "create partial external language");
    locale.discover();
    locale.select("auto", "fr-FR");
    check(locale.current() == "fr" && std::wstring(locale.text("action.open")) == L"Ouvrir",
          "discover and auto select community language");
    check(std::wstring(locale.text("action.cancel")) == L"Cancel" && !locale.warning(),
          "missing translation falls back per key");
    check(locale.format("summary.updated", {{L"seconds", L"2"}, {L"status", L"OK"}}) == L"2s : OK",
          "translation can reorder named placeholders");
    check(atomic_write(file,
                       R"({"schema":1,"id":"fr","name":"Français","strings":{"summary.updated":"{oops}"}})"),
          "write invalid placeholders");
    locale.discover();
    locale.select("fr");
    check(locale.warning() && locale.format("summary.updated", {{L"seconds", L"2"}, {L"status", L"OK"}}) ==
                                  L"OK  ·  Updated 2s ago",
          "bad placeholders fall back safely");
    check(atomic_write(file, R"({"schema":1,"id":"en","name":"Override","strings":{"action.cancel":"Bad"}})"),
          "write duplicate builtin id");
    locale.discover();
    locale.select("en");
    check(locale.warning() && std::wstring(locale.text("action.cancel")) == L"Cancel",
          "external pack cannot replace builtins");
    locale.select("fr");
    check(locale.current() == "en" && locale.warning(), "missing selected pack falls back");
    auto settings_file = join(data, L"settings.json");
    Settings settings = default_settings(), loaded;
    settings.language = "ko";
    check(save_settings(settings_file, settings) && load_settings(settings_file, loaded) &&
              loaded.language == "ko",
          "language selection survives restart");
    std::string content;
    check(read_text(settings_file, content, 65536), "read settings for migration fixture");
    auto doc = parse(content);
    cJSON_DeleteItemFromObjectCaseSensitive(doc.get(), "language");
    check(atomic_write(settings_file, dump(doc.get())) && load_settings(settings_file, loaded) &&
              loaded.language == "auto",
          "0.1 settings migrate to automatic language");
    put(doc.get(), "language", std::string("../bad"));
    check(atomic_write(settings_file, dump(doc.get())) && !load_settings(settings_file, loaded),
          "invalid stored language rejected");
}
std::string quota_response(uint64_t reset, int minutes = 300, const std::string &used = "12.5",
                           const std::string &id = "codex") {
    return "{\"rateLimits\":{\"limitId\":\"" + id + "\",\"primary\":{\"usedPercent\":" + used +
           ",\"windowDurationMins\":" + std::to_string(minutes) + ",\"resetsAt\":" + std::to_string(reset) +
           "},\"secondary\":null}}";
}
bool parse_codex_fixture(const std::string &response, QuotaSnapshot &q) {
    auto doc = parse(response);
    return parse_codex_account(doc.get(), now_seconds(), q);
}
bool save_codex_fixture(const std::wstring &data, const QuotaSnapshot &q) {
    auto doc = json(quota_json(q));
    put(doc.get(), "source", std::string("codex-account"));
    put(doc.get(), "limit_id", std::string("codex"));
    return atomic_write(join(data, L"codex-quota.json"), dump(doc.get()));
}
void quota_tests(const std::wstring &root) {
    uint64_t now = now_seconds();
    QuotaSnapshot q;
    auto usage = parse(
        R"({"rate_limits_available":true,"subscription_type":"max","rate_limits":{"five_hour":{"utilization":26,"resets_at":"2026-09-10T10:50:00.472851+00:00"},"seven_day":{"utilization":20,"resets_at":"2026-09-16T04:00:00Z"}}})");
    check(parse_claude_usage(usage.get(), now, q) && q.short_term.remaining == 7400 &&
              q.weekly.remaining == 8000 && q.short_term.minutes == 300 && q.weekly.minutes == 10080 &&
              q.short_term.resets == timestamp("2026-09-10T10:50:00Z") && q.plan == "max",
          "Claude SDK usage converts real utilization and ISO reset times");
    usage = parse(R"({"rate_limits_available":false,"rate_limits":null})");
    check(parse_claude_usage(usage.get(), now, q) && !q.short_term.available && !q.weekly.available,
          "non-subscription account clears old allowance");
    usage = parse(
        R"({"rate_limits_available":true,"rate_limits":{"five_hour":null,"seven_day":{"utilization":100,"resets_at":null}}})");
    check(parse_claude_usage(usage.get(), now, q) && !q.short_term.available && q.weekly.available &&
              q.weekly.remaining == 0 && q.weekly.resets == 0,
          "SDK partial windows preserve zero remaining and unknown reset");
    for (
        const char *invalid :
        {R"({})", R"({"rate_limits_available":true})",
         R"({"rate_limits_available":true,"rate_limits":{"five_hour":{"utilization":101}}})",
         R"({"rate_limits_available":true,"rate_limits":{"five_hour":{"utilization":20,"resets_at":"invalid"}}})",
         R"({"rate_limits_available":true,"rate_limits":{"five_hour":{"utilization":"20"}}})"}) {
        usage = parse(invalid);
        check(!parse_claude_usage(usage.get(), now, q), "malformed SDK response cannot overwrite quota");
    }
    check(parse_codex_fixture(quota_response(now + 1000), q) && q.short_term.remaining == 8750 &&
              q.short_term.minutes == 300,
          "Codex account response supplies real remaining allowance");
    check(!q.weekly.available, "absent weekly quota is not invented");
    check(parse_codex_fixture(quota_response(now + 5000, 10080, "4"), q) && q.weekly.remaining == 9600 &&
              !q.short_term.available,
          "weekly primary is classified by duration, not position");
    check(parse_codex_fixture(quota_response(now + 1000, 240), q) && q.short_term.minutes == 240,
          "server-reported four-hour window preserved");
    check(!parse_codex_fixture(quota_response(now + 1000, 300, "101"), q) &&
              !parse_codex_fixture(quota_response(now + 1000, 300, "-1"), q),
          "invalid percentages rejected");
    check(parse_codex_fixture(quota_response(now + 1000, 300, "3", "other_model"), q) &&
              !q.short_term.available && !q.weekly.available,
          "model-specific bucket cannot overwrite account quota");
    check(parse_codex_fixture(quota_response(now - 1, 300, "100"), q) && !quota_current(q.short_term, now),
          "expired limit waits for new report, never invents reset");
    check(parse_codex_fixture(quota_response(now + 1000, 300, "0"), q) && quota_current(q.short_term, now) &&
              q.short_term.remaining == 10000,
          "zero used means fully remaining");
    auto encoded = json(quota_json(q));
    QuotaSnapshot restored;
    check(quota_json(encoded.get(), restored) && restored.short_term.remaining == 10000 &&
              restored.short_term.resets == now + 1000,
          "quota cache roundtrip");
    auto claude = std::string("{\"rate_limits\":{\"five_hour\":{\"used_percentage\":75.5,\"resets_at\":") +
                  std::to_string(now + 1000) +
                  "},\"seven_day\":{\"used_percentage\":20,\"resets_at\":" + std::to_string(now + 50000) +
                  "}},\"context_window\":{\"remaining_percentage\":99}}";
    check(parse_claude_quota(claude, now, q) && q.short_term.remaining == 2450 && q.weekly.remaining == 8000,
          "Claude subscription percentages, not context occupancy");
    check(parse_claude_quota("{\"context_window\":{\"remaining_percentage\":99}}", now, q) &&
              !q.short_term.available && !q.weekly.available,
          "missing Claude subscription telemetry is unknown");
    QuotaStore history;
    check(parse_codex_fixture(quota_response(now + 2000), q) && history.insert(q, now),
          "insert quota sample");
    auto older = q;
    older.observed -= 60;
    older.short_term.remaining = 9900;
    history.insert(older, now);
    check(history.latest.short_term.remaining == 8750 && history.latest.observed == now,
          "old session cannot override newer account snapshot");
    auto future = q;
    future.observed = now + 301;
    check(!history.insert(future, now), "future quota timestamps rejected");
    auto only_week = q;
    only_week.observed = now + 1;
    only_week.weekly = q.short_term;
    only_week.weekly.minutes = 10080;
    only_week.short_term = {};
    history.insert(only_week, now);
    check(!history.latest.short_term.available, "new snapshot clears missing old windows");
    for (size_t i = 0; i < MaxQuotaHistory + 50; ++i) {
        q.observed = now - 600 + static_cast<uint64_t>(i);
        q.short_term.remaining = static_cast<int>(i);
        history.insert(q, now);
    }
    check(history.history.size() <= MaxQuotaHistory, "quota history bounded");
    auto data = join(root, L"quota-data"), logs = join(root, L"quota-logs");
    check(ensure_directory(data) && ensure_directory(logs), "quota collector fixture directories");
    auto file = join(logs, L"quota.jsonl");
    check(
        atomic_write(
            file,
            R"({"type":"event_msg","timestamp":")" + iso_now() +
                R"(","payload":{"type":"token_count","info":null,"rate_limits":{"limit_id":"codex","primary":{"used_percent":3,"window_minutes":300},"secondary":null}}})"
                "\n"),
        "conflicting session quota fixture");
    check(parse_codex_fixture(quota_response(now + 1000, 10080, "4"), q) && save_codex_fixture(data, q),
          "account quota fixture");
    Settings settings = default_settings();
    settings.enabled[0] = false;
    settings.roots[1] = logs;
    Collector collector(data);
    collector.configure(settings);
    auto snap = collector.scan();
    check(snap.providers[1].total.events == 0 && snap.providers[1].quota.latest.weekly.remaining == 9600 &&
              !snap.providers[1].quota.latest.short_term.available,
          "session quota cannot overwrite the account RPC snapshot");
    Collector restart(data);
    restart.configure(settings);
    check(restart.load(), "load persisted quota history");
    snap = restart.scan();
    check(snap.providers[1].quota.latest.weekly.remaining == 9600 && restart.bytes_read() == 0,
          "restart restores quota without rereading logs");
    std::string state_text;
    check(read_text(join(data, L"state.json"), state_text, 1024 * 1024), "read migration fixture");
    auto state = parse(state_text);
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(state.get(), "version"), 2);
    check(DeleteFileW(join(data, L"codex-quota.json").c_str()), "remove account fixture for migration");
    check(atomic_write(join(data, L"state.json"), dump(state.get())), "write polluted v2 quota cache");
    Collector v2(data);
    v2.configure(settings);
    check(v2.load(), "version two cache accepted for migration");
    snap = v2.scan();
    check(!snap.providers[1].quota.latest.observed && snap.providers[1].quota.history.empty() &&
              v2.bytes_read() == 0,
          "v2 mixed quotas discarded while log checkpoints retained");
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(state.get(), "version"), 1);
    cJSON_DeleteItemFromObjectCaseSensitive(state.get(), "quotas");
    check(atomic_write(join(data, L"state.json"), dump(state.get())), "write legacy cache fixture");
    Collector migrated(data);
    migrated.configure(settings);
    check(migrated.load(), "version one cache accepted for migration");
    snap = migrated.scan();
    check(!snap.providers[1].quota.latest.observed && migrated.bytes_read() == 0,
          "legacy checkpoints retained without importing session quota");
    auto bridge_dir = join(root, L"bridge-data"), claude_settings = join(root, L"claude-test-settings.json");
    check(ensure_directory(bridge_dir), "bridge fixture folder");
    check(
        atomic_write(
            claude_settings,
            R"({"model":"keep-model","statusLine":{"type":"command","command":"cat","padding":3},"hooks":{"keep":true}})"),
        "existing statusline fixture");
    check(connect_claude(claude_settings, bridge_dir, L"C:/Program Files/AI Mon/ai-mon.exe") ==
              BridgeResult::Done,
          "connect while preserving existing status line");
    std::string settings_text;
    check(read_text(claude_settings, settings_text, 65536), "read connected settings");
    auto configured = parse(settings_text);
    check(str(configured.get(), "model") == "keep-model" &&
              cJSON_IsObject(field(configured.get(), "hooks")) &&
              str(field(configured.get(), "statusLine"), "command").find("--claude-statusline") !=
                  std::string::npos,
          "unrelated Claude settings preserved");
    check(connect_claude(claude_settings, bridge_dir, L"C:/Program Files/AI Mon/ai-mon.exe") ==
              BridgeResult::AlreadyConnected,
          "bridge connect is idempotent");
    check(disconnect_claude(claude_settings, bridge_dir) == BridgeResult::Done,
          "disconnect restores original status line");
    check(read_text(claude_settings, settings_text, 65536), "read restored settings");
    configured = parse(settings_text);
    check(str(field(configured.get(), "statusLine"), "command") == "cat" &&
              cJSON_GetNumberValue(field(field(configured.get(), "statusLine"), "padding")) == 3,
          "original command and padding restored");
    check(connect_claude(claude_settings, bridge_dir, L"C:/app.exe") == BridgeResult::Done,
          "reconnect fixture");
    check(atomic_write(claude_settings, R"({"statusLine":{"type":"command","command":"user-new-command"}})"),
          "simulate external settings edit");
    check(disconnect_claude(claude_settings, bridge_dir) == BridgeResult::Changed,
          "disconnect does not overwrite external edits");
}
} // namespace
int fake_claude() {
    char line[2048];
    if (!std::fgets(line, sizeof(line), stdin) || !strstr(line, "initialize"))
        return 2;
    std::puts(
        R"({"type":"control_response","response":{"subtype":"success","request_id":"init","response":{}}})");
    std::fflush(stdout);
    if (!std::fgets(line, sizeof(line), stdin) || !strstr(line, "get_usage") ||
        !strstr(line, "skip_behaviors"))
        return 3;
    auto mode = env(L"AI_MON_TEST_PROBE");
    if (mode == L"hang") {
        Sleep(30000);
        return 4;
    }
    if (mode == L"invalid")
        std::puts(
            R"({"type":"control_response","response":{"subtype":"success","request_id":"quota","response":{}}})");
    else
        std::puts(
            R"({"type":"control_response","response":{"subtype":"success","request_id":"quota","response":{"rate_limits_available":true,"rate_limits":{"five_hour":{"utilization":26,"resets_at":null},"seven_day":{"utilization":20,"resets_at":null}}}}})");
    std::fflush(stdout);
    Sleep(30000); // The monitor must close its own child after receiving the response.
    return 0;
}
DWORD WINAPI cancel_probe(void *event) {
    Sleep(500);
    SetEvent(event);
    return 0;
}
void probe_tests(const std::wstring &root) {
    auto profile = env(L"USERPROFILE");
    auto home = join(root, L"probe-home"), data = join(root, L"probe-output");
    check(ensure_directory(home) && ensure_directory(join(home, L".local")) &&
              ensure_directory(join(home, L".local\\bin")),
          "probe executable fixture directory");
    wchar_t self[32768]{};
    GetModuleFileNameW(nullptr, self, 32768);
    check(CopyFileW(self, join(home, L".local\\bin\\claude.exe").c_str(), TRUE), "copy fake CLI");
    SetEnvironmentVariableW(L"USERPROFILE", home.c_str());
    SetEnvironmentVariableW(L"AI_MON_TEST_PROBE", L"success");
    check(probe_claude(data) == ClaudeProbe::Ready,
          "real process handshake retrieves quota without user prompt");
    std::string before, after;
    check(read_text(join(data, L"claude-quota.json"), before, 8192), "probe saves sanitized quota");
    auto doc = parse(before);
    QuotaSnapshot q;
    check(quota_json(doc.get(), q) && q.short_term.remaining == 7400 && q.weekly.remaining == 8000,
          "process response correctly normalized");
    SetEnvironmentVariableW(L"AI_MON_TEST_PROBE", L"invalid");
    check(probe_claude(data) == ClaudeProbe::Failed, "invalid subprocess response is a failure");
    check(read_text(join(data, L"claude-quota.json"), after, 8192) && before == after,
          "failed probe preserves last known values");
    SetEnvironmentVariableW(L"AI_MON_TEST_PROBE", L"hang");
    Handle stop(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    Handle cancel(CreateThread(nullptr, 0, cancel_probe, stop.value, 0, nullptr));
    auto started = GetTickCount64();
    check(probe_claude(data, stop.value) == ClaudeProbe::Failed && GetTickCount64() - started < 3000,
          "shutdown cancels hung CLI promptly");
    WaitForSingleObject(cancel.value, INFINITE);
    SetEnvironmentVariableW(L"USERPROFILE", profile.c_str());
    SetEnvironmentVariableW(L"AI_MON_TEST_PROBE", nullptr);
}
int fake_codex() {
    char line[2048];
    if (!std::fgets(line, sizeof(line), stdin) || !strstr(line, "initialize"))
        return 2;
    std::puts(R"({"id":1,"result":{}})");
    std::fflush(stdout);
    if (!std::fgets(line, sizeof(line), stdin) || !strstr(line, "initialized") ||
        !std::fgets(line, sizeof(line), stdin) || !strstr(line, "account/rateLimits/read"))
        return 3;
    auto mode = env(L"AI_MON_TEST_CODEX");
    if (mode == L"hang") {
        Sleep(30000);
        return 4;
    }
    if (mode == L"error")
        std::puts(R"({"id":2,"error":{"code":-32000,"message":"offline"}})");
    else if (mode == L"auth")
        std::puts(
            R"({"id":2,"error":{"code":-32600,"message":"codex account authentication required to read rate limits"}})");
    else if (mode == L"invalid")
        std::puts(R"({"id":2,"result":{"rateLimits":{}}})");
    else if (mode == L"oversize") {
        for (int i = 0; i < 2200; ++i)
            std::fputs(std::string(1024, 'x').c_str(), stdout);
    } else {
        // Both a notification and the legacy view deliberately contain a different quota.
        std::puts(
            R"({"method":"account/rateLimits/updated","params":{"rateLimits":{"limitId":"codex_bengalfox"}}})");
        std::puts(
            R"({"id":2,"result":{"accountId":"synthetic-private-account","rateLimits":{"limitId":"codex_bengalfox"},"rateLimitsByLimitId":{"codex_bengalfox":{"limitName":"GPT-5.3-Codex-Spark","primary":{"usedPercent":0,"windowDurationMins":300},"secondary":{"usedPercent":17,"windowDurationMins":10080}},"codex":{"limitId":"codex","planType":"prolite","primary":{"usedPercent":22,"windowDurationMins":10080,"resetsAt":null},"secondary":null}}}})");
    }
    std::fflush(stdout);
    Sleep(30000);
    return 0;
}
void codex_tests(const std::wstring &root) {
    uint64_t now = now_seconds();
    QuotaSnapshot q;
    for (
        const auto *text :
        {R"({"rateLimitsByLimitId":{"codex_bengalfox":{"primary":{"usedPercent":0,"windowDurationMins":300}}},"rateLimits":{"primary":{"usedPercent":0,"windowDurationMins":300},"secondary":null}})",
         R"({"rateLimits":{"primary":{"usedPercent":0,"windowDurationMins":300},"secondary":null}})",
         R"({"rateLimits":{"limitName":"GPT-5.3-Codex-Spark","primary":{"usedPercent":0,"windowDurationMins":300},"secondary":null}})",
         R"({"rateLimits":{"limitId":"codex_bengalfox","primary":{"usedPercent":0,"windowDurationMins":300},"secondary":null}})",
         R"({"rateLimitsByLimitId":{"codex":{"limitId":"codex_bengalfox","primary":null,"secondary":null}}})"}) {
        check(parse_codex_fixture(text, q) && !q.short_term.available && !q.weekly.available,
              "other bucket never becomes main quota, including unnamed legacy IDs");
    }
    for (
        const auto *text :
        {R"({})", R"({"rateLimitsByLimitId":[]})",
         R"({"rateLimitsByLimitId":{"codex":null},"rateLimits":{"primary":null,"secondary":null}})",
         R"({"rateLimits":{"limitId":"codex","primary":{"usedPercent":1,"windowDurationMins":300},"secondary":{"usedPercent":2,"windowDurationMins":240}}})",
         R"({"rateLimits":{"limitId":"codex","primary":{"usedPercent":"1","windowDurationMins":300},"secondary":null}})"})
        check(!parse_codex_fixture(text, q), "invalid full snapshot rejected without fallback");
    check(parse_codex_fixture(quota_response(now + 60), q), "account history fixture");
    q.scope = "account-a";
    QuotaStore history;
    history.insert(q, now);
    q.short_term.remaining = 5500;
    check(history.insert(q, now) && history.latest.short_term.remaining == 5500 &&
              history.history.size() == 1,
          "same-second account update replaces its history point");
    q.scope = "account-b";
    ++q.observed;
    history.insert(q, now);
    check(history.history.size() == 1 && history.latest.scope == "account-b",
          "account change clears history");
    q.scope = "account-a";
    q.observed -= 2;
    check(!history.insert(q, now) && history.latest.scope == "account-b", "older account cannot return");
    CodexPoll poll;
    check(poll.due(0), "Codex lookup at startup");
    poll.completed(0);
    check(!poll.due(119999) && poll.due(120000), "idle polling does not depend on logs");
    poll.requested = true;
    check(!poll.due(14999) && poll.due(15000), "manual refresh throttled separately");
    wchar_t self[32768]{};
    GetModuleFileNameW(nullptr, self, 32768);
    auto data = join(root, L"codex-rpc-output");
    SetEnvironmentVariableW(L"AI_MON_TEST_CODEX", L"success");
    check(probe_codex(data, nullptr, self) == CodexProbe::Ready,
          "account RPC handshake without a model turn");
    std::string before, after;
    check(read_text(join(data, L"codex-quota.json"), before, 8192), "read normalized Codex result");
    auto doc = parse(before);
    check(quota_json(doc.get(), q) && !q.short_term.available && q.weekly.remaining == 7800 &&
              before.find("synthetic-private-account") == std::string::npos &&
              before.find("Spark") == std::string::npos && !q.scope.empty(),
          "main bucket selected; private account and other buckets not saved");
    for (const auto *mode : {L"error", L"invalid", L"oversize", L"hang"}) {
        SetEnvironmentVariableW(L"AI_MON_TEST_CODEX", mode);
        auto started = GetTickCount64();
        check(probe_codex(data, nullptr, self, 500) == CodexProbe::Failed &&
                  GetTickCount64() - started < 3000,
              "RPC failure and oversized or hung child bounded");
        check(read_text(join(data, L"codex-quota.json"), after, 8192) && after == before,
              "lookup failure preserves timestamp and last known value");
    }
    Handle stop(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    Handle cancel(CreateThread(nullptr, 0, cancel_probe, stop.value, 0, nullptr));
    auto started = GetTickCount64();
    check(probe_codex(data, stop.value, self) == CodexProbe::Failed && GetTickCount64() - started < 3000,
          "Codex child cancelled during shutdown");
    WaitForSingleObject(cancel.value, INFINITE);
    SetEnvironmentVariableW(L"AI_MON_TEST_CODEX", L"auth");
    check(probe_codex(data, nullptr, self) == CodexProbe::AuthRequired,
          "authentication failure reported explicitly");
    check(read_text(join(data, L"codex-quota.json"), after, 8192), "read signed-out snapshot");
    doc = parse(after);
    check(quota_json(doc.get(), q) && !q.weekly.available && !q.short_term.available,
          "sign-out clears previous account quota");
    SetEnvironmentVariableW(L"AI_MON_TEST_CODEX", nullptr);
}
int wmain(int argc, wchar_t **argv) {
    if (argc == 2 && wcscmp(argv[1], L"app-server") == 0)
        return fake_codex();
    if (argc == 3 && wcscmp(argv[1], L"--startup-test") == 0) {
        std::wstring key = argv[2];
        if (key.find(L"Software\\AI Mon\\Tests\\") != 0)
            return 2;
        auto command =
            startup_command(L"C:\\Program Files\\AI Mon\\ai-mon.exe", L"C:\\Users\\Test User\\AI Mon");
        bool ok = startup_value(key.c_str()).empty() && set_startup_value(command, key.c_str()) &&
                  startup_value(key.c_str()) == command &&
                  !set_startup_value(std::wstring(261, L'x'), key.c_str()) &&
                  startup_value(key.c_str()) == command && set_startup_value(L"", key.c_str()) &&
                  startup_value(key.c_str()).empty();
        ok = ok && set_startup_value(command, key.c_str()) &&
             remove_startup_for(L"C:\\Other\\ai-mon.exe", key.c_str()) &&
             startup_value(key.c_str()) == command &&
             remove_startup_for(L"C:\\Program Files\\AI Mon\\ai-mon.exe", key.c_str()) &&
             startup_value(key.c_str()).empty();
        RegDeleteKeyW(HKEY_CURRENT_USER, key.c_str());
        std::printf("Startup registry test: %s\n", ok ? "PASS" : "FAIL");
        return ok ? 0 : 1;
    }
    if (argc > 2 && wcscmp(argv[1], L"-p") == 0)
        return fake_claude();
    if (argc != 2) {
        std::fprintf(stderr, "Pass a new writable fixture directory.\n");
        return 2;
    }
    try {
        parser_tests();
        collector_tests(argv[1]);
        language_tests(argv[1]);
        quota_tests(argv[1]);
        probe_tests(argv[1]);
        codex_tests(argv[1]);
        check(startup_command(L"C:\\Program Files\\AI Mon\\ai-mon.exe", L"C:\\Data\\") ==
                  L"\"C:\\Program Files\\AI Mon\\ai-mon.exe\" --startup --data-dir \"C:\\Data\\\\\"",
              "startup command safely quotes spaces and trailing slash");
        Settings appearance = default_settings(), restored;
        auto appearance_file = join(argv[1], L"appearance.json");
        appearance.mini_opacity = 65;
        check(save_settings(appearance_file, appearance) && load_settings(appearance_file, restored) &&
                  restored.mini_opacity == 65,
              "transparency setting survives restart");
        appearance.mini_opacity = 0;
        check(save_settings(appearance_file, appearance) && !load_settings(appearance_file, restored),
              "invisible widget configuration rejected");
        std::printf("PASS: %d checks\n", checks);
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FAIL after %d checks: %s\n", checks, e.what());
        return 1;
    }
}
