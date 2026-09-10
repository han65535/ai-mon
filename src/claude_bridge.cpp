#include "claude_bridge.hpp"
#include <algorithm>

namespace aimon {
std::wstring claude_settings_path() {
    auto folder = env(L"CLAUDE_CONFIG_DIR");
    if (folder.empty())
        folder = join(env(L"USERPROFILE"), L".claude");
    return join(folder, L"settings.json");
}
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
static std::wstring bash_path() {
    auto configured = env(L"CLAUDE_CODE_GIT_BASH_PATH");
    if (!configured.empty() && GetFileAttributesW(configured.c_str()) != INVALID_FILE_ATTRIBUTES)
        return configured;
    wchar_t found[32768]{};
    if (SearchPathW(nullptr, L"git.exe", nullptr, 32768, found, nullptr)) {
        std::wstring root = found;
        auto end = root.find_last_of(L"\\/");
        if (end != std::wstring::npos) {
            root.resize(end);
            end = root.find_last_of(L"\\/");
            if (end != std::wstring::npos) {
                root.resize(end);
                auto candidate = join(root, L"bin\\bash.exe");
                if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
                    return candidate;
            }
        }
    }
    return {};
}
static std::wstring shell_literal(std::wstring value, bool bash) {
    std::replace(value.begin(), value.end(), L'\\', L'/');
    std::wstring result = L"'";
    for (wchar_t c : value)
        result += c == L'\'' ? (bash ? L"'\\''" : L"''") : std::wstring(1, c);
    return result + L"'";
}
static Json read_doc(const std::wstring &file) {
    std::string text;
    return read_text(file, text, 1024 * 1024) ? parse(text) : json();
}
BridgeResult connect_claude(const std::wstring &settings, const std::wstring &data, const std::wstring &exe) {
    auto doc = read_doc(settings);
    if (!doc) {
        if (GetFileAttributesW(settings.c_str()) != INVALID_FILE_ATTRIBUTES)
            return BridgeResult::Failed;
        doc = json(cJSON_CreateObject());
    }
    if (!cJSON_IsObject(doc.get()) || !ensure_directory(data))
        return BridgeResult::Failed;
    auto backup_path = join(data, L"claude-statusline-backup.json");
    auto backup = read_doc(backup_path);
    auto original = field(doc.get(), "statusLine");
    if (backup)
        return str(original, "command") == str(backup.get(), "installed_command")
                   ? BridgeResult::AlreadyConnected
                   : BridgeResult::Changed;
    if (GetFileAttributesW(backup_path.c_str()) != INVALID_FILE_ATTRIBUTES)
        return BridgeResult::Failed;
    if (original &&
        (!cJSON_IsObject(original) || str(original, "type") != "command" || str(original, "command").empty()))
        return BridgeResult::Failed;
    if (str(original, "command").find("--claude-statusline") != std::string::npos)
        return BridgeResult::Changed;
    auto shell = bash_path();
    bool bash = !shell.empty();
    if (!bash)
        shell = join(env(L"SystemRoot"), L"System32\\WindowsPowerShell\\v1.0\\powershell.exe");
    std::wstring command = (bash ? L"" : L"& ") + shell_literal(exe, bash) +
                           L" --claude-statusline --data-dir " + shell_literal(data, bash);
    backup = json(cJSON_CreateObject());
    put(backup.get(), "settings", utf8(canonical(settings)));
    put(backup.get(), "shell", utf8(shell));
    put(backup.get(), "bash", bash);
    put(backup.get(), "installed_command", utf8(command));
    cJSON_AddItemToObject(backup.get(), "original",
                          original ? cJSON_Duplicate(original, 1) : cJSON_CreateNull());
    auto replacement = original ? cJSON_Duplicate(original, 1) : cJSON_CreateObject();
    cJSON_DeleteItemFromObjectCaseSensitive(replacement, "type");
    cJSON_DeleteItemFromObjectCaseSensitive(replacement, "command");
    put(replacement, "type", std::string("command"));
    put(replacement, "command", utf8(command));
    cJSON_DeleteItemFromObjectCaseSensitive(doc.get(), "statusLine");
    cJSON_AddItemToObject(doc.get(), "statusLine", replacement);
    if (!atomic_write(backup_path, dump(backup.get())))
        return BridgeResult::Failed;
    if (!atomic_write(settings, dump(doc.get()))) {
        DeleteFileW(backup_path.c_str());
        return BridgeResult::Failed;
    }
    return BridgeResult::Done;
}
BridgeResult disconnect_claude(const std::wstring &settings, const std::wstring &data) {
    auto backup_path = join(data, L"claude-statusline-backup.json");
    auto backup = read_doc(backup_path), doc = read_doc(settings);
    if (!backup || !doc)
        return BridgeResult::Failed;
    if (_wcsicmp(canonical(settings).c_str(), wide(str(backup.get(), "settings")).c_str()) != 0 ||
        str(field(doc.get(), "statusLine"), "command") != str(backup.get(), "installed_command"))
        return BridgeResult::Changed;
    auto original = field(backup.get(), "original");
    if (!original || (!cJSON_IsNull(original) && !cJSON_IsObject(original)))
        return BridgeResult::Failed;
    cJSON_DeleteItemFromObjectCaseSensitive(doc.get(), "statusLine");
    if (!cJSON_IsNull(original))
        cJSON_AddItemToObject(doc.get(), "statusLine", cJSON_Duplicate(original, 1));
    if (!atomic_write(settings, dump(doc.get())))
        return BridgeResult::Failed;
    DeleteFileW(backup_path.c_str());
    DeleteFileW(join(data, L"claude-quota.json").c_str());
    return BridgeResult::Done;
}
static bool output(const std::string &text) {
    DWORD written = 0;
    return WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text.data(), static_cast<DWORD>(text.size()), &written,
                     nullptr) &&
           written == text.size();
}
struct PipeInput {
    HANDLE pipe;
    const std::string *input;
};
static DWORD WINAPI write_input(void *context) {
    auto &input = *static_cast<PipeInput *>(context);
    DWORD written = 0;
    WriteFile(input.pipe, input.input->data(), static_cast<DWORD>(input.input->size()), &written, nullptr);
    CloseHandle(input.pipe);
    return 0;
}
static bool forward(const cJSON *backup, const std::string &input) {
    auto command = wide(str(field(backup, "original"), "command"));
    if (command.empty())
        return false;
    auto shell = wide(str(backup, "shell"));
    std::wstring args = quote_argument(shell) +
                        (cJSON_IsTrue(field(backup, "bash")) ? L" -c " : L" -NoProfile -Command ") +
                        quote_argument(command);
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE read = nullptr, write = nullptr;
    if (!CreatePipe(&read, &write, &security, 0))
        return false;
    Handle reader(read), writer(write);
    SetHandleInformation(writer.value, HANDLE_FLAG_INHERIT, 0);
    Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.valid() ||
        !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        return false;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = reader.value;
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(shell.c_str(), args.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &process))
        return false;
    Handle child(process.hProcess), thread(process.hThread);
    if (!AssignProcessToJobObject(job.value, child.value)) {
        TerminateProcess(child.value, 1);
        return false;
    }
    reader.reset();
    PipeInput context{writer.value, &input};
    Handle feeder(CreateThread(nullptr, 0, write_input, &context, 0, nullptr));
    if (!feeder.valid()) {
        TerminateProcess(child.value, 1);
        return false;
    }
    writer.value = INVALID_HANDLE_VALUE;
    ResumeThread(thread.value);
    DWORD waited = WaitForSingleObject(child.value, 3000);
    job.reset(); // Bounds hung status-line commands and their own descendants.
    WaitForSingleObject(feeder.value, INFINITE);
    return waited == WAIT_OBJECT_0;
}
static bool save_quota(const std::wstring &data, const QuotaSnapshot &quota) {
    if (!ensure_directory(data))
        return false;
    auto identity = utf8(canonical(data));
    auto name = L"Local\\AI.Mon.ClaudeQuota." + wide(hex(hash_bytes(identity.data(), identity.size())));
    Handle mutex(CreateMutexW(nullptr, FALSE, name.c_str()));
    if (!mutex.valid())
        return false;
    DWORD waited = WaitForSingleObject(mutex.value, 1000);
    if (waited != WAIT_OBJECT_0 && waited != WAIT_ABANDONED)
        return false;
    auto doc = json(quota_json(quota));
    bool ok = atomic_write(join(data, L"claude-quota.json"), dump(doc.get()));
    ReleaseMutex(mutex.value);
    return ok;
}
static std::wstring claude_executable() {
    auto local = join(env(L"USERPROFILE"), L".local\\bin\\claude.exe");
    if (GetFileAttributesW(local.c_str()) != INVALID_FILE_ATTRIBUTES)
        return local;
    wchar_t path[32768]{};
    // Search PATH explicitly: never execute an untrusted claude.exe in the current directory.
    if (SearchPathW(env(L"PATH").c_str(), L"claude.exe", nullptr, 32768, path, nullptr))
        return path;
    auto extensions = join(env(L"USERPROFILE"), L".vscode\\extensions");
    WIN32_FIND_DATAW entry{};
    HANDLE find = FindFirstFileW(join(extensions, L"anthropic.claude-code-*-win32-x64").c_str(), &entry);
    std::wstring best;
    uint64_t newest = 0;
    if (find != INVALID_HANDLE_VALUE) {
        do {
            auto candidate = join(join(extensions, entry.cFileName), L"resources\\native-binary\\claude.exe");
            WIN32_FILE_ATTRIBUTE_DATA attrs{};
            if (GetFileAttributesExW(candidate.c_str(), GetFileExInfoStandard, &attrs)) {
                uint64_t modified = (uint64_t(attrs.ftLastWriteTime.dwHighDateTime) << 32) |
                                    attrs.ftLastWriteTime.dwLowDateTime;
                if (modified > newest) {
                    newest = modified;
                    best = candidate;
                }
            }
        } while (FindNextFileW(find, &entry));
        FindClose(find);
    }
    return best;
}
ClaudeProbe probe_claude(const std::wstring &data, HANDLE stop) {
    auto exe = claude_executable();
    if (exe.empty())
        return ClaudeProbe::Missing;
    if (!ensure_directory(data))
        return ClaudeProbe::Failed;
    std::wstring args =
        quote_argument(exe) +
        L" -p --output-format stream-json --input-format stream-json --verbose"
        L" --no-session-persistence --strict-mcp-config --mcp-config " +
        quote_argument(L"{\"mcpServers\":{}}") + L" --tools \"\" --setting-sources user --settings " +
        quote_argument(L"{\"disableAllHooks\":true}") + L" --no-chrome --disable-slash-commands";
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE in_read = nullptr, in_write = nullptr, out_read = nullptr, out_write = nullptr;
    if (!CreatePipe(&in_read, &in_write, &security, 0))
        return ClaudeProbe::Failed;
    Handle input(in_read), writer(in_write);
    if (!CreatePipe(&out_read, &out_write, &security, 0))
        return ClaudeProbe::Failed;
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
        return ClaudeProbe::Failed;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input.value;
    startup.hStdOutput = output_pipe.value;
    startup.hStdError = errors.value;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), args.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                        nullptr, data.c_str(), &startup, &process))
        return ClaudeProbe::Failed;
    Handle child(process.hProcess), thread(process.hThread);
    if (!AssignProcessToJobObject(job.value, child.value)) {
        TerminateProcess(child.value, 1);
        return ClaudeProbe::Failed;
    }
    input.reset();
    output_pipe.reset();
    ResumeThread(thread.value);
    auto send = [&](const char *request) {
        DWORD written = 0, size = static_cast<DWORD>(strlen(request));
        return WriteFile(writer.value, request, size, &written, nullptr) && written == size;
    };
    if (!send("{\"type\":\"control_request\",\"request_id\":\"init\",\"request\":{\"subtype\":\"initialize\","
              "\"hooks\":{},\"sdkMcpServers\":[]}}\n"))
        return ClaudeProbe::Failed;
    std::string pending;
    size_t total = 0;
    bool requested = false;
    auto deadline = GetTickCount64() + 20000;
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
            if (str(doc.get(), "type") != "control_response")
                continue;
            auto response = field(doc.get(), "response");
            auto id = str(response, "request_id");
            if (id != "init" && id != "quota")
                continue;
            if (str(response, "subtype") != "success")
                return ClaudeProbe::Failed;
            if (id == "init" && !requested) {
                requested = true;
                if (!send("{\"type\":\"control_request\",\"request_id\":\"quota\",\"request\":{\"subtype\":"
                          "\"get_usage\",\"skip_behaviors\":true}}\n"))
                    return ClaudeProbe::Failed;
            } else if (id == "quota" && requested) {
                QuotaSnapshot quota;
                if (!parse_claude_usage(field(response, "response"), now_seconds(), quota) ||
                    !save_quota(data, quota))
                    return ClaudeProbe::Failed;
                return quota.short_term.available || quota.weekly.available ? ClaudeProbe::Ready
                                                                            : ClaudeProbe::Unavailable;
            }
        }
    }
    return ClaudeProbe::Failed;
}
int claude_statusline(const std::wstring &data) {
    std::string input;
    char buffer[8192];
    DWORD got = 0;
    while (ReadFile(GetStdHandle(STD_INPUT_HANDLE), buffer, sizeof(buffer), &got, nullptr) && got) {
        if (input.size() + got > 1024 * 1024)
            return 2;
        input.append(buffer, got);
    }
    QuotaSnapshot quota;
    bool valid = parse_claude_quota(input, now_seconds(), quota);
    if (valid)
        save_quota(data, quota);
    auto backup = read_doc(join(data, L"claude-statusline-backup.json"));
    if (backup && !str(field(backup.get(), "original"), "command").empty())
        return forward(backup.get(), input) ? 0 : 3;
    std::string text = "AI Mon";
    for (const auto &value : {quota.short_term, quota.weekly})
        if (valid && quota_current(value, now_seconds()))
            text += std::string(value.minutes == 10080 ? " | Week " : " | 5h ") +
                    std::to_string(value.remaining / 100) + "% left";
    output(text + "\n");
    return valid ? 0 : 2;
}
} // namespace aimon
