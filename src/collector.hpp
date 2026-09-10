#pragma once
#include "usage.hpp"
#include <functional>

namespace aimon {
enum class Status { Disabled, Collecting, Ready, Empty, Missing, ReadError, Unsupported, Partial };
struct ProviderSnapshot {
    Totals total;
    Status status = Status::Collecting;
    uint64_t success = 0;
    size_t files = 0;
};
struct Snapshot {
    std::array<ProviderSnapshot, 2> providers;
    bool cache_error = false;
    bool cache_rebuilt = false;
    bool limited = false;
};
struct FileState {
    int provider = 0;
    uint64_t identity = 0, modified = 0, size = 0, offset = 0, prefix = 0, prefix_length = 0;
    ParseState parser;
    bool malformed = false, unsupported = false, discarding = false;
};
class Collector {
  public:
    explicit Collector(std::wstring data_dir);
    void configure(const Settings &settings);
    Snapshot scan(HANDLE stop = nullptr);
    bool load();
    bool save();
    const EventStore &store() const { return store_; }
    uint64_t bytes_read() const { return bytes_read_; }
    const Settings &settings() const { return settings_; }

  private:
    bool read_file(const FileInfo &file, int provider, FileState &state, HANDLE stop, bool &pending,
                   size_t &budget);
    Settings settings_;
    std::wstring data_dir_;
    EventStore store_;
    std::map<std::wstring, FileState> files_;
    std::array<uint64_t, 2> success_{{0, 0}};
    uint64_t bytes_read_ = 0;
    bool rebuilt_ = false, cache_error_ = false, dirty_ = false;
};
const char *status_code(Status status);
} // namespace aimon
