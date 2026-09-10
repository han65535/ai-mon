#include "collector.hpp"
#include "localization.hpp"
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
} // namespace
int wmain(int argc, wchar_t **argv) {
    if (argc != 2) {
        std::fprintf(stderr, "Pass a new writable fixture directory.\n");
        return 2;
    }
    try {
        parser_tests();
        collector_tests(argv[1]);
        language_tests(argv[1]);
        std::printf("PASS: %d checks\n", checks);
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FAIL after %d checks: %s\n", checks, e.what());
        return 1;
    }
}
