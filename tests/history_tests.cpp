#include "history.hpp"
#include "history_window.hpp"
#include <stdexcept>
#include <string>
#include <ctime>
using namespace aimon;
namespace {
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void append(const std::wstring &path, const std::string &text) {
    Handle file(CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    DWORD written = 0;
    require(file.valid() && WriteFile(file.value, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size(), "history test append");
}
const HistoryRow &find(const std::vector<HistoryRow> &rows, const std::string &period, int provider, const std::string &model) {
    for (const auto &row : rows) if (row.period == period && row.provider == provider && row.model == model) return row;
    throw std::runtime_error("history row missing");
}
std::string token(int count) {
    return std::string("{\"type\":\"event_msg\",\"timestamp\":\"2025-01-10T12:00:00Z\",\"payload\":{\"type\":\"token_count\",\"info\":{\"total_token_usage\":{\"input_tokens\":") + std::to_string(count) + ",\"output_tokens\":10}}}}\n";
}
}
void history_tests(const std::wstring &root) {
    auto folder = join(root, L"history-한글"); require(ensure_directory(folder), "history root");
    auto data = join(folder, L"data"), claude = join(folder, L"claude"), codex = join(folder, L"codex"), sessions = join(codex, L"sessions"), archive = join(codex, L"archived_sessions");
    require(ensure_directory(data) && ensure_directory(claude) && ensure_directory(codex) && ensure_directory(sessions) && ensure_directory(archive), "history directories");
    auto cf = join(claude, L"session.jsonl"), xf = join(sessions, L"session.jsonl"), af = join(archive, L"copy.jsonl");
    std::string ce = "{\"type\":\"assistant\",\"sessionId\":\"s\",\"timestamp\":\"2024-11-23T12:00:00Z\",\"message\":{\"id\":\"m\",\"model\":\"claude-test\",\"usage\":{\"input_tokens\":12,\"output_tokens\":3,\"cache_read_input_tokens\":2}}}\n";
    std::string xe = "{\"type\":\"session_meta\",\"payload\":{\"id\":\"codex-s\",\"originator\":\"codex_vscode\"}}\n{\"type\":\"turn_context\",\"payload\":{\"model\":\"model-a\"}}\n" + token(100) + "{\"type\":\"turn_context\",\"payload\":{\"model\":\"model-b\"}}\n" + token(160) + token(200);
    require(atomic_write(cf, ce) && atomic_write(xf, xe) && atomic_write(af, xe), "history fixtures");
    Settings settings = default_settings(); settings.roots[0] = claude; settings.roots[1] = sessions;
    SetEnvironmentVariableW(L"TZ", L"UTC"); _tzset();
    {
        History history(data); require(history.scan(settings), "history scan"); std::vector<HistoryRow> rows;
        require(history.report(true, rows), "monthly history");
        require(find(rows,"2024-11",0,"claude-test").input == 14, "Claude model and cached input");
        require(find(rows,"2025-01",1,"model-a").input == 100, "Codex first model");
        require(find(rows,"2025-01",1,"").input == 60, "ambiguous model change");
        auto row = find(rows,"2025-01",1,"model-b"); require(row.input == 40 && row.source == "VS Code", "Codex model and IDE source");
        require(history.scan(settings) && history.report(false, rows), "repeat scan and weekly history");
        require(find(rows,"2025-01-06",1,"model-a").input == 100, "weekly Monday and deduplication");
        append(cf, "{\"type\":\"assistant\",\"sessionId\":\"s\",\"timestamp\":\"2024-11-23T12:00:01Z\",\"message\":{\"id\":\"m\",\"usage\":{\"input_tokens\":15,\"output_tokens\":4,\"cache_read_input_tokens\":2}}}\n");
        require(history.scan(settings) && history.report(true, rows), "Claude corrected scan");
        require(find(rows,"2024-11",0,"claude-test").input == 17, "corrected usage retains model");
    }
    {
        History history(data); append(xf, token(250)); std::vector<HistoryRow> rows;
        require(history.scan(settings) && history.report(true, rows), "history resume");
        require(find(rows,"2025-01",1,"model-b").input == 90, "model and cursor after restart");
    }
    DeleteFileW(cf.c_str()); DeleteFileW(xf.c_str()); DeleteFileW(af.c_str());
    History history(data); std::vector<HistoryRow> rows;
    require(history.scan(settings) && history.report(true, rows), "history after source deletion");
    require(find(rows,"2025-01",1,"model-b").input == 90, "saved history survives source deletion");
    HistoryRow unsafe; unsafe.period = "2025-01"; unsafe.model = "  =2+2";
    auto csv = format_history_csv({unsafe}); require(csv.find("\"'  =2+2\"") != std::string::npos, "CSV formula escaped");
    auto csv_path = join(folder, L"한글-report.csv"); std::string saved;
    require(atomic_write(csv_path, csv) && read_text(csv_path, saved, 8192) && saved == csv, "CSV write");
    SetEnvironmentVariableW(L"TZ", nullptr); _tzset();
}

void history_ui_tests(const std::wstring &root) {
    require(ensure_directory(root), "history UI root");
    HINSTANCE module = GetModuleHandleW(nullptr);
    Localizer locale; require(locale.initialize(module, root), "history UI locale"); locale.select("en");
    HWND owner = CreateWindowExW(0, L"STATIC", L"history test", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, module, nullptr);
    require(owner != nullptr, "history UI owner");
    HistoryWindow window; require(window.initialize(module, owner, locale), "history UI class");
    HistoryView view; view.pending = false;
    HistoryRow weekly; weekly.period="2025-01-06"; weekly.provider=1; weekly.model="model-a"; weekly.source="VS Code"; weekly.input=100; weekly.output=10; weekly.events=1;
    HistoryRow monthly=weekly; monthly.period="2025-01";
    view.rows[0].push_back(weekly); view.rows[1].push_back(monthly);
    window.update(view); window.show();
    HWND history = FindWindowW(L"AI.Mon.History.1", nullptr);
    require(window.visible() && history && GetWindow(history, GW_OWNER) == owner, "history UI window");
    HWND combo = GetDlgItem(history, 2001), edit = FindWindowExW(history, nullptr, L"EDIT", nullptr);
    require(combo && edit, "history UI controls");
    auto content = [&](HWND control) { int length=GetWindowTextLengthW(control); std::wstring s(static_cast<size_t>(length)+1, L'\0'); GetWindowTextW(control,s.data(),length+1); s.resize(length); return s; };
    require(content(edit).find(L"2025-01-06") != std::wstring::npos && content(edit).find(L"model-a") != std::wstring::npos, "weekly UI content");
    SendMessageW(combo, CB_SETCURSEL, 1, 0); SendMessageW(history, WM_COMMAND, MAKEWPARAM(2001, CBN_SELCHANGE), reinterpret_cast<LPARAM>(combo));
    require(content(edit).find(L"Monthly history") != std::wstring::npos && content(edit).find(L"2025-01-06") == std::wstring::npos, "monthly UI selection");
    locale.select("ko"); window.update(view);
    require(content(edit).find(L"월별 기록") != std::wstring::npos && content(GetDlgItem(history, 2002)).find(L"CSV") != std::wstring::npos, "history language switch");
    window.shutdown(); DestroyWindow(owner);
}
