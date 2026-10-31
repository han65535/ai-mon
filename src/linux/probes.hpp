#pragma once
#include "platform.hpp"
#include <string>
namespace aimon {
enum class LinuxProbe { Ready, Missing, Failed, AuthRequired, Unavailable };
LinuxProbe probe_codex_linux(const std::wstring &data, HANDLE stop = nullptr, const std::string &executable = {}, unsigned timeout_ms = 20000);
LinuxProbe probe_claude_linux(const std::wstring &data, HANDLE stop = nullptr, const std::string &executable = {}, unsigned timeout_ms = 20000);
}
