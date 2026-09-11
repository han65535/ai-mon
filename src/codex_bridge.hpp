#pragma once
#include "quota.hpp"

namespace aimon {
enum class CodexProbe { Waiting, Ready, Missing, Failed, AuthRequired, Unavailable };
// Poll even without new logs. Refresh is bounded independently of filesystem notifications.
struct CodexPoll {
    uint64_t last = 0;
    bool started = false, requested = true;
    bool due(uint64_t tick) const { return !started || tick - last >= (requested ? 15000ULL : 120000ULL); }
    void completed(uint64_t tick) {
        last = tick;
        started = true;
        requested = false;
    }
};
CodexProbe probe_codex(const std::wstring &data, HANDLE stop = nullptr, const std::wstring &executable = {},
                       DWORD timeout_ms = 20000);
} // namespace aimon
