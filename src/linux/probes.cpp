#include "linux/probes.hpp"
#include "quota.hpp"
#include <chrono>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <vector>
extern char **environ;
namespace aimon {
namespace {
struct Process {
    pid_t pid = -1; int socket = -1;
    ~Process() { if (socket >= 0) close(socket); if (pid > 0) { kill(-pid, SIGKILL); while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {} } }
};
std::string executable_on_path(const std::string &name) {
    auto *path = std::getenv("PATH");
    std::string paths(path ? path : ""); size_t start = 0;
    while (start <= paths.size()) {
        size_t end = paths.find(':', start); if (end == std::string::npos) end = paths.size();
        auto dir = paths.substr(start, end - start);
        if (!dir.empty() && dir[0] == '/') { auto candidate = dir + "/" + name; if (access(candidate.c_str(), X_OK) == 0) return candidate; }
        if (end == paths.size()) break;
        start = end + 1;
    }
    auto home = env(L"HOME");
    if (!home.empty()) {
        auto candidate = utf8(join(join(home, L".local/bin"), wide(name)));
        if (access(candidate.c_str(), X_OK) == 0) return candidate;
    }
    return {};
}
bool launch(Process &p, const std::string &exe, const std::wstring &directory, std::vector<std::string> args) {
    int fds[2]; if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) != 0) return false;
    posix_spawn_file_actions_t actions; posix_spawnattr_t attr;
    if (posix_spawn_file_actions_init(&actions) != 0) { close(fds[0]); close(fds[1]); return false; }
    if (posix_spawnattr_init(&attr) != 0) { posix_spawn_file_actions_destroy(&actions); close(fds[0]); close(fds[1]); return false; }
    bool ok = posix_spawn_file_actions_addchdir_np(&actions, utf8(directory).c_str()) == 0 &&
              posix_spawn_file_actions_adddup2(&actions, fds[1], STDIN_FILENO) == 0 &&
              posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO) == 0 &&
              posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0) == 0 &&
              posix_spawn_file_actions_addclose(&actions, fds[0]) == 0 &&
              posix_spawn_file_actions_addclose(&actions, fds[1]) == 0 &&
              posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP) == 0 && posix_spawnattr_setpgroup(&attr, 0) == 0;
    std::vector<char *> argv; for (auto &arg : args) argv.push_back(arg.data()); argv.push_back(nullptr);
    int result = ok ? posix_spawn(&p.pid, exe.c_str(), &actions, &attr, argv.data(), environ) : EINVAL;
    posix_spawn_file_actions_destroy(&actions); posix_spawnattr_destroy(&attr); close(fds[1]);
    if (result != 0) { close(fds[0]); p.pid = -1; return false; }
    p.socket = fds[0]; int flags = fcntl(p.socket, F_GETFL); if (flags < 0 || fcntl(p.socket, F_SETFL, flags | O_NONBLOCK) < 0) return false;
    return true;
}
bool save(const std::wstring &data, bool codex, const QuotaSnapshot &quota) {
    auto doc = json(quota_json(quota));
    if (codex) { put(doc.get(), "source", std::string("codex-account")); put(doc.get(), "limit_id", std::string("codex")); }
    return atomic_write(join(data, codex ? L"codex-quota.json" : L"claude-quota.json"), dump(doc.get()));
}
LinuxProbe probe(bool codex, const std::wstring &data, HANDLE stop, const std::string &specified, unsigned timeout_ms) {
    if (stop && stop->load()) return LinuxProbe::Failed;
    auto exe = specified.empty() ? executable_on_path(codex ? "codex" : "claude") : specified;
    if (exe.empty()) return LinuxProbe::Missing;
    if (!ensure_directory(data)) return LinuxProbe::Failed;
    std::vector<std::string> args;
    if (codex) args = {exe, "app-server"};
    else args = {exe, "-p", "--output-format", "stream-json", "--input-format", "stream-json", "--verbose", "--no-session-persistence", "--strict-mcp-config", "--mcp-config", "{\"mcpServers\":{}}", "--tools", "", "--setting-sources", "user", "--settings", "{\"disableAllHooks\":true}", "--no-chrome", "--disable-slash-commands"};
    Process child; if (!launch(child, exe, data, std::move(args))) return LinuxProbe::Failed;
    std::string outgoing = codex ? "{\"id\":1,\"method\":\"initialize\",\"params\":{\"clientInfo\":{\"name\":\"ai_mon\",\"title\":\"AI Mon\",\"version\":\"0.3.5\"}}}\n" : "{\"type\":\"control_request\",\"request_id\":\"init\",\"request\":{\"subtype\":\"initialize\",\"hooks\":{},\"sdkMcpServers\":[]}}\n";
    std::string input; size_t total = 0; bool requested = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline && !(stop && stop->load())) {
        pollfd fd{child.socket, static_cast<short>(POLLIN | (outgoing.empty() ? 0 : POLLOUT)), 0};
        int rc = poll(&fd, 1, 50); if (rc < 0 && errno == EINTR) continue; if (rc < 0 || (fd.revents & (POLLERR | POLLNVAL))) break;
        if ((fd.revents & POLLOUT) && !outgoing.empty()) {
            ssize_t n = send(child.socket, outgoing.data(), outgoing.size(), MSG_NOSIGNAL);
            if (n > 0) outgoing.erase(0, static_cast<size_t>(n)); else if (n < 0 && errno != EAGAIN && errno != EINTR) break;
        }
        if (fd.revents & (POLLIN | POLLHUP)) {
            char buffer[8192]; ssize_t n = recv(child.socket, buffer, sizeof buffer, 0);
            if (n == 0) break;
            if (n < 0) { if (errno == EAGAIN || errno == EINTR) continue; break; }
            total += static_cast<size_t>(n); if (total > 2 * 1024 * 1024) break; input.append(buffer, static_cast<size_t>(n));
            size_t newline; while ((newline = input.find('\n')) != std::string::npos) {
                auto doc = parse(input.substr(0, newline)); input.erase(0, newline + 1); if (!doc) continue;
                if (codex) {
                    uint64_t id = 0; if (!number(field(doc.get(), "id"), id) || id != (requested ? 2ULL : 1ULL)) continue;
                    auto err = field(doc.get(), "error");
                    if (err && !cJSON_IsNull(err)) {
                        if (str(err, "message").find("authentication required") != std::string::npos) { QuotaSnapshot empty; empty.observed = now_seconds(); return save(data, true, empty) ? LinuxProbe::AuthRequired : LinuxProbe::Failed; }
                        return LinuxProbe::Failed;
                    }
                    if (!requested) { if (!cJSON_IsObject(field(doc.get(), "result"))) return LinuxProbe::Failed; requested = true; outgoing += "{\"method\":\"initialized\"}\n{\"id\":2,\"method\":\"account/rateLimits/read\"}\n"; }
                    else { QuotaSnapshot quota; if (!parse_codex_account(field(doc.get(), "result"), now_seconds(), quota) || !save(data, true, quota)) return LinuxProbe::Failed; return quota.short_term.available || quota.weekly.available ? LinuxProbe::Ready : LinuxProbe::Unavailable; }
                } else {
                    if (str(doc.get(), "type") != "control_response") continue;
                    auto response = field(doc.get(), "response"); auto id = str(response, "request_id"); if (id != "init" && id != "quota") continue;
                    if (str(response, "subtype") != "success") return LinuxProbe::Failed;
                    if (id == "init" && !requested) { requested = true; outgoing += "{\"type\":\"control_request\",\"request_id\":\"quota\",\"request\":{\"subtype\":\"get_usage\",\"skip_behaviors\":true}}\n"; }
                    else if (id == "quota" && requested) { QuotaSnapshot quota; if (!parse_claude_usage(field(response, "response"), now_seconds(), quota) || !save(data, false, quota)) return LinuxProbe::Failed; return quota.short_term.available || quota.weekly.available ? LinuxProbe::Ready : LinuxProbe::Unavailable; }
                }
            }
        }
    }
    return LinuxProbe::Failed;
}
}
LinuxProbe probe_codex_linux(const std::wstring &data, HANDLE stop, const std::string &exe, unsigned timeout) { return probe(true, data, stop, exe, timeout); }
LinuxProbe probe_claude_linux(const std::wstring &data, HANDLE stop, const std::string &exe, unsigned timeout) { return probe(false, data, stop, exe, timeout); }
}
