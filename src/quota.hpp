#pragma once
#include "platform.hpp"
#include "json.hpp"
#include <array>

namespace aimon {
struct QuotaWindow {
    bool available = false;
    int remaining = 0; // hundredths of a percent
    uint64_t minutes = 0, resets = 0;
};
struct QuotaSnapshot {
    uint64_t observed = 0;
    std::string plan;
    QuotaWindow short_term, weekly;
};
constexpr size_t MaxQuotaHistory = 512;
bool parse_codex_quota(const std::string &line, QuotaSnapshot &quota);
bool parse_claude_quota(const std::string &input, uint64_t observed, QuotaSnapshot &quota);
bool parse_claude_usage(const cJSON *usage, uint64_t observed, QuotaSnapshot &quota);
cJSON *quota_json(const QuotaSnapshot &quota);
bool quota_json(const cJSON *json, QuotaSnapshot &quota);
bool quota_current(const QuotaWindow &window, uint64_t now);
struct QuotaStore {
    QuotaSnapshot latest;
    std::vector<QuotaSnapshot> history;
    bool insert(const QuotaSnapshot &quota, uint64_t now);
};
} // namespace aimon
