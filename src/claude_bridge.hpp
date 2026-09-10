#pragma once
#include "quota.hpp"

namespace aimon {
enum class BridgeResult { Done, AlreadyConnected, Changed, Failed };
std::wstring claude_settings_path();
BridgeResult connect_claude(const std::wstring &settings, const std::wstring &data, const std::wstring &exe);
BridgeResult disconnect_claude(const std::wstring &settings, const std::wstring &data);
int claude_statusline(const std::wstring &data);
enum class ClaudeProbe { Waiting, Ready, Missing, Failed, Unavailable };
ClaudeProbe probe_claude(const std::wstring &data, HANDLE stop = nullptr);
} // namespace aimon
