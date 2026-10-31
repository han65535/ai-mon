#pragma once
#include "usage.hpp"
#include <string>
#include <vector>
struct sqlite3;
namespace aimon {
struct HistoryRow {
    std::string period, model, source;
    int provider = 0;
    uint64_t input = 0, output = 0, cached = 0, events = 0;
};
std::string format_history_csv(const std::vector<HistoryRow> &rows, const std::string &unknown_model = "알 수 없음", const std::string &unknown_source = "알 수 없음");
class History {
public:
    explicit History(const std::wstring &directory);
    ~History();
    History(const History &) = delete;
    History &operator=(const History &) = delete;
    bool open();
    bool scan(const Settings &settings, HANDLE stop = nullptr);
    bool report(bool monthly, std::vector<HistoryRow> &rows);
    const std::string &error() const { return error_; }
    bool pending() const { return pending_; }
    bool incomplete() const { return incomplete_; }
    bool truncated() const { return truncated_; }
private:
    std::wstring directory_;
    sqlite3 *db_ = nullptr;
    std::string error_;
    bool pending_ = false, incomplete_ = false, truncated_ = false;
};
}
