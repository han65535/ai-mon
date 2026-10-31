#include "linux/probes.hpp"
#include "quota.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace aimon;
static void check(bool b, const char *why) { if (!b) throw std::runtime_error(why); }
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    try {
        std::wstring data = wide(argv[1]); std::string mocks = argv[2];
        check(probe_codex_linux(data, nullptr, mocks + "/codex", 4000) == LinuxProbe::Ready, "codex probe");
        check(probe_claude_linux(data, nullptr, mocks + "/claude", 4000) == LinuxProbe::Ready, "claude probe");
        for (auto pair : {std::pair{L"codex-quota.json", 7500}, std::pair{L"claude-quota.json", 6000}}) {
            std::string text; check(read_text(join(data, pair.first), text, 8192), "quota cache");
            auto doc = parse(text); QuotaSnapshot q; check(quota_json(doc.get(), q) && q.short_term.remaining == pair.second, "quota value");
        }
        check(probe_claude_linux(data, nullptr, mocks + "/claude-unavailable", 4000) == LinuxProbe::Unavailable, "unauthenticated Claude response");
        std::string empty_text; check(read_text(join(data, L"claude-quota.json"), empty_text, 8192), "unavailable cache missing");
        auto empty_doc = parse(empty_text); QuotaSnapshot empty;
        check(quota_json(empty_doc.get(), empty) && !empty.short_term.available && !empty.weekly.available, "unavailable quota must clear prior values");
        auto start = std::chrono::steady_clock::now();
        check(probe_codex_linux(data, nullptr, mocks + "/hang", 150) == LinuxProbe::Failed, "timeout result");
        check(std::chrono::steady_clock::now() - start < std::chrono::seconds(3), "timeout cleanup");
        check(probe_codex_linux(data, nullptr, mocks + "/bad", 500) == LinuxProbe::Failed, "malformed response");
        std::string retained; check(read_text(join(data, L"codex-quota.json"), retained, 8192), "missing prior quota");
        auto prior = parse(retained); QuotaSnapshot q;
        check(quota_json(prior.get(), q) && q.short_term.remaining == 7500, "failure overwrote prior quota");
        std::atomic_bool cancelled{false}; std::thread cancel([&] { std::this_thread::sleep_for(std::chrono::milliseconds(100)); cancelled.store(true); });
        auto begin = std::chrono::steady_clock::now();
        auto result = probe_claude_linux(data, &cancelled, mocks + "/hang", 5000); cancel.join();
        check(result == LinuxProbe::Failed && std::chrono::steady_clock::now() - begin < std::chrono::seconds(3), "active probe cancellation");
        std::cout << "Linux probe checks passed\n"; return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
