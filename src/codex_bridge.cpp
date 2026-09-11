#include "codex_bridge.hpp"
#include <algorithm>

namespace aimon {
static std::wstring quote_argument(const std::wstring &s) {
    std::wstring result = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : s) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        result.append(backslashes * (c == L'"' ? 2 : 1), L'\\');
        backslashes = 0;
        if (c == L'"')
            result += L'\\';
        result += c;
    }
    result.append(backslashes * 2, L'\\');
    return result + L'"';
}
static std::wstring codex_executable() {
    std::wstring best;
    uint64_t newest = 0;
    auto consider = [&](const std::wstring &candidate) {
        WIN32_FILE_ATTRIBUTE_DATA a{};
        if (GetFileAttributesExW(candidate.c_str(), GetFileExInfoStandard, &a) &&
            !(a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            auto modified =
                (uint64_t(a.ftLastWriteTime.dwHighDateTime) << 32) | a.ftLastWriteTime.dwLowDateTime;
            if (best.empty() || modified > newest) {
                best = candidate;
                newest = modified;
            }
        }
    };
    consider(join(env(L"LOCALAPPDATA"), L"Programs\\OpenAI\\Codex\\bin\\codex.exe"));
    // Search explicit PATH, never the working directory or a shell command wrapper.
    wchar_t path[32768]{};
    if (SearchPathW(env(L"PATH").c_str(), L"codex.exe", nullptr, 32768, path, nullptr))
        consider(path);
    // npm's shim is a .cmd; execute the installed native binary directly.
    consider(join(env(L"APPDATA"), L"npm\\node_modules\\@openai\\codex\\node_modules\\@openai\\codex-win32-"
                                   L"x64\\vendor\\x86_64-pc-windows-msvc\\codex\\codex.exe"));
    consider(join(env(L"APPDATA"),
                  L"npm\\node_modules\\@openai\\codex\\vendor\\x86_64-pc-windows-msvc\\codex\\codex.exe"));
    for (const auto &folder : {L".vscode\\extensions", L".vscode-insiders\\extensions"}) {
        auto extensions = join(env(L"USERPROFILE"), folder);
        WIN32_FIND_DATAW entry{};
        HANDLE find = FindFirstFileW(join(extensions, L"openai.chatgpt-*").c_str(), &entry);
        if (find == INVALID_HANDLE_VALUE)
            continue;
        do {
            consider(join(join(extensions, entry.cFileName), L"bin\\windows-x86_64\\codex.exe"));
        } while (FindNextFileW(find, &entry));
        FindClose(find);
    }
    return best;
}
static bool save_codex(const std::wstring &data, const QuotaSnapshot &quota) {
    auto doc = json(quota_json(quota));
    put(doc.get(), "source", std::string("codex-account"));
    put(doc.get(), "limit_id", std::string("codex"));
    return atomic_write(join(data, L"codex-quota.json"), dump(doc.get()));
}
CodexProbe probe_codex(const std::wstring &data, HANDLE stop, const std::wstring &executable,
                       DWORD timeout_ms) {
    if (stop && WaitForSingleObject(stop, 0) == WAIT_OBJECT_0)
        return CodexProbe::Failed;
    auto exe = executable.empty() ? codex_executable() : executable;
    if (exe.empty())
        return CodexProbe::Missing;
    if (!ensure_directory(data))
        return CodexProbe::Failed;
    std::wstring args = quote_argument(exe) + L" app-server";
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE in_read = nullptr, in_write = nullptr, out_read = nullptr, out_write = nullptr;
    if (!CreatePipe(&in_read, &in_write, &security, 0))
        return CodexProbe::Failed;
    Handle input(in_read), writer(in_write);
    if (!CreatePipe(&out_read, &out_write, &security, 0))
        return CodexProbe::Failed;
    Handle reader(out_read), output_pipe(out_write);
    SetHandleInformation(writer.value, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(reader.value, HANDLE_FLAG_INHERIT, 0);
    Handle errors(CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!errors.valid() || !job.valid() ||
        !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        return CodexProbe::Failed;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input.value;
    startup.hStdOutput = output_pipe.value;
    startup.hStdError = errors.value;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), args.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                        nullptr, data.c_str(), &startup, &process))
        return CodexProbe::Failed;
    Handle child(process.hProcess), thread(process.hThread);
    if (!AssignProcessToJobObject(job.value, child.value)) {
        TerminateProcess(child.value, 1);
        return CodexProbe::Failed;
    }
    input.reset();
    output_pipe.reset();
    ResumeThread(thread.value);
    auto send = [&](const char *request) {
        DWORD written = 0, size = static_cast<DWORD>(strlen(request));
        return WriteFile(writer.value, request, size, &written, nullptr) && written == size;
    };
    if (!send(
            R"({"id":1,"method":"initialize","params":{"clientInfo":{"name":"ai_mon","title":"AI Mon","version":"0.3.5"}}})"
            "\n"))
        return CodexProbe::Failed;
    std::string pending;
    size_t total = 0;
    bool requested = false;
    auto deadline = GetTickCount64() + timeout_ms;
    while (GetTickCount64() < deadline && !(stop && WaitForSingleObject(stop, 0) == WAIT_OBJECT_0)) {
        DWORD available = 0;
        if (!PeekNamedPipe(reader.value, nullptr, 0, nullptr, &available, nullptr))
            break;
        if (!available) {
            if (WaitForSingleObject(child.value, 0) == WAIT_OBJECT_0)
                break;
            if (stop)
                WaitForSingleObject(stop, 25);
            else
                Sleep(25);
            continue;
        }
        char buffer[8192];
        DWORD got = 0;
        if (!ReadFile(reader.value, buffer, std::min<DWORD>(sizeof(buffer), available), &got, nullptr) ||
            !got)
            break;
        total += got;
        if (total > 2 * 1024 * 1024)
            break;
        pending.append(buffer, got);
        size_t newline;
        while ((newline = pending.find('\n')) != std::string::npos) {
            auto doc = parse(pending.substr(0, newline));
            pending.erase(0, newline + 1);
            uint64_t id = 0;
            if (!number(field(doc.get(), "id"), id) || id != (requested ? 2ULL : 1ULL))
                continue; // Notifications (including model quota updates) are not full snapshots.
            auto error = field(doc.get(), "error");
            if (error && !cJSON_IsNull(error)) {
                if (str(error, "message").find("authentication required") != std::string::npos) {
                    QuotaSnapshot empty;
                    empty.observed = now_seconds();
                    if (!save_codex(data, empty))
                        return CodexProbe::Failed;
                    return CodexProbe::AuthRequired;
                }
                return CodexProbe::Failed;
            }
            if (!requested) {
                if (!cJSON_IsObject(field(doc.get(), "result")))
                    return CodexProbe::Failed;
                requested = true;
                if (!send(R"({"method":"initialized"})"
                          "\n"
                          R"({"id":2,"method":"account/rateLimits/read"})"
                          "\n"))
                    return CodexProbe::Failed;
            } else {
                QuotaSnapshot quota;
                if (!parse_codex_account(field(doc.get(), "result"), now_seconds(), quota) ||
                    !save_codex(data, quota))
                    return CodexProbe::Failed;
                return quota.short_term.available || quota.weekly.available ? CodexProbe::Ready
                                                                            : CodexProbe::Unavailable;
            }
        }
    }
    return CodexProbe::Failed;
}
} // namespace aimon
