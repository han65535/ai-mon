#include "history.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace aimon;
static void check(bool v, const char *message) { if (!v) throw std::runtime_error(message); }
static void add(const std::filesystem::path &p, const std::string &s) { std::ofstream f(p, std::ios::app); f << s; }
static const HistoryRow &find(const std::vector<HistoryRow> &rows, const std::string &period, int provider, const std::string &model) {
    for (const auto &r : rows) if (r.period == period && r.provider == provider && r.model == model) return r;
    throw std::runtime_error("expected history row missing");
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    try {
        auto base=std::filesystem::u8path(argv[1]), claude=base/"claude", codex=base/"codex"/"sessions", archived=base/"codex"/"archived_sessions", state=base/"state";
        std::filesystem::create_directories(claude); std::filesystem::create_directories(codex); std::filesystem::create_directories(archived); std::filesystem::create_directories(state);
        auto c=claude/"session.jsonl", x=codex/"session.jsonl";
        add(c,"{\"type\":\"assistant\",\"sessionId\":\"s\",\"timestamp\":\"2024-11-23T12:00:00Z\",\"message\":{\"id\":\"m\",\"model\":\"claude-test\",\"usage\":{\"input_tokens\":12,\"output_tokens\":3,\"cache_read_input_tokens\":2}}}\n");
        add(x,"{\"type\":\"session_meta\",\"payload\":{\"id\":\"codex-s\",\"originator\":\"codex_vscode\"}}\n{\"type\":\"turn_context\",\"payload\":{\"model\":\"model-a\"}}\n");
        auto token=[](int n){return std::string("{\"type\":\"event_msg\",\"timestamp\":\"2025-01-10T12:00:00Z\",\"payload\":{\"type\":\"token_count\",\"info\":{\"total_token_usage\":{\"input_tokens\":")+std::to_string(n)+",\"output_tokens\":10}}}}\n";};
        add(x,token(100)+"{\"type\":\"turn_context\",\"payload\":{\"model\":\"model-b\"}}\n"+token(160)+token(200));
        std::filesystem::copy_file(x, archived/"duplicate.jsonl");
        auto settings=default_settings(); settings.roots[0]=wide(claude.u8string()); settings.roots[1]=wide(codex.u8string());
        {
            History h(wide(state.u8string())); check(h.scan(settings),h.error().c_str()); std::vector<HistoryRow> rows;
            check(h.report(true,rows),"monthly report");
            check(find(rows,"2024-11",0,"claude-test").input==14,"Claude cached input");
            check(find(rows,"2025-01",1,"model-a").input==100,"first model");
            check(find(rows,"2025-01",1,"").input==60,"ambiguous model transition");
            const auto b=find(rows,"2025-01",1,"model-b"); check(b.input==40 && b.source=="VS Code","VS Code model");
            check(h.scan(settings),"repeat scan"); rows.clear(); check(h.report(false,rows),"weekly report");
            check(find(rows,"2025-01-06",1,"model-a").input==100,"weekly Monday");
            add(c,"{\"type\":\"assistant\",\"sessionId\":\"s\",\"timestamp\":\"2024-11-23T12:00:01Z\",\"message\":{\"id\":\"m\",\"usage\":{\"input_tokens\":15,\"output_tokens\":4,\"cache_read_input_tokens\":2}}}\n");
            check(h.scan(settings),"corrected scan"); rows.clear(); check(h.report(true,rows),"corrected report");
            check(find(rows,"2024-11",0,"claude-test").input==17,"correction retains model and adjusts totals");
            auto partial = claude/"partial.jsonl";
            add(partial,"{\"type\":\"assistant\",\"sessionId\":\"s\",\"timestamp\":\"2024-11-23T12:00:02Z\",\"message\":{\"id\":\"p\",\"model\":\"claude-test\",\"usage\":{\"input_tokens\":6,\"output_tokens\":1}}}");
            check(h.scan(settings), "partial scan"); rows.clear(); check(h.report(true,rows), "partial report");
            check(find(rows,"2024-11",0,"claude-test").input==17,"partial line counted early");
            add(partial,"\n"); check(h.scan(settings), "completed line scan"); rows.clear(); check(h.report(true,rows), "completed line report");
            check(find(rows,"2024-11",0,"claude-test").input==23,"completed line omitted");
            auto old_time = std::filesystem::last_write_time(c);
            std::ifstream old(c, std::ios::binary); std::string contents((std::istreambuf_iterator<char>(old)), {});
            size_t pos = 0; while ((pos = contents.find("\"id\":\"m\"", pos)) != std::string::npos) { contents[pos + 6] = 'n'; pos += 8; }
            auto replacement = claude/"replacement.jsonl"; { std::ofstream f(replacement, std::ios::binary); f << contents; }
            std::filesystem::last_write_time(replacement, old_time); std::filesystem::rename(replacement, c);
            check(h.scan(settings), "replacement scan"); check(h.incomplete(), "replaced source must warn");
        }
        {
            History resumed(wide(state.u8string()));
            add(x,token(250)); check(resumed.scan(settings),"resume scan"); std::vector<HistoryRow> rows;
            check(resumed.report(true,rows),"resume report"); check(find(rows,"2025-01",1,"model-b").input==90,"model and cursor after restart");
        }
        auto missing_state = base/"missing-state"; std::filesystem::create_directories(missing_state);
        auto partial_settings = settings; partial_settings.roots[0] = wide((base/"missing").u8string());
        History partial_provider(wide(missing_state.u8string())); check(partial_provider.scan(partial_settings), "missing provider stopped collection");
        std::vector<HistoryRow> partial_rows; check(partial_provider.incomplete() && partial_provider.report(true,partial_rows) && find(partial_rows,"2025-01",1,"model-b").input==90,"healthy provider missing after other root fails");
        std::filesystem::remove(c); std::filesystem::remove(claude/"partial.jsonl"); std::filesystem::remove(x); std::filesystem::remove(archived/"duplicate.jsonl");
        History restored(wide(state.u8string())); check(restored.scan(settings),"rescan removed files"); std::vector<HistoryRow> rows;
        check(restored.report(true,rows),"restore report"); check(find(rows,"2025-01",1,"model-b").input==90,"history did not persist");
        HistoryRow unsafe; unsafe.period="2025-01"; unsafe.model="  =2+2"; unsafe.source="VS Code"; unsafe.input=1;
        auto csv = format_history_csv({unsafe});
        check(csv.find("\"'  =2+2\"") != std::string::npos, "CSV formula not escaped");
        auto csv_path = wide((base / "report.csv").u8string());
        check(atomic_write(csv_path, csv), "CSV write failed");
        std::string saved; check(read_text(csv_path, saved, 8192) && saved == csv, "CSV file contents");
        std::cout << "Linux history checks passed\n"; return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
