#include "platform.hpp"
#include <algorithm>
#include <codecvt>
#include <locale>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace aimon {
std::wstring wide(const std::string &s) {
    try { std::wstring_convert<std::codecvt_utf8<wchar_t>> c; auto r = c.from_bytes(s); return c.converted() == s.size() ? r : L""; }
    catch (const std::range_error &) { return {}; }
}
std::string utf8(const std::wstring &s) {
    try { std::wstring_convert<std::codecvt_utf8<wchar_t>> c; return c.to_bytes(s); }
    catch (const std::range_error &) { return {}; }
}
std::wstring env(const wchar_t *name) { auto n = utf8(name); auto *v = std::getenv(n.c_str()); return v ? wide(v) : L""; }
std::wstring join(const std::wstring &a, const std::wstring &b) { return a.empty() ? b : a + (a.back() == L'/' ? L"" : L"/") + b; }
std::wstring canonical(const std::wstring &p) {
    if (p.empty()) return {};
    std::error_code ec; auto path = std::filesystem::absolute(std::filesystem::u8path(utf8(p)), ec);
    return ec ? p : wide(path.lexically_normal().u8string());
}
bool directory(const std::wstring &p) {
    struct stat s{};
    if (stat(utf8(p).c_str(), &s) != 0) return false;
    if (!S_ISDIR(s.st_mode)) { errno = ENOTDIR; return false; }
    return true;
}
bool ensure_directory(const std::wstring &p) { std::error_code ec; return std::filesystem::create_directories(std::filesystem::u8path(utf8(p)), ec) || directory(p); }
bool read_text(const std::wstring &p, std::string &out, size_t limit) {
    std::ifstream f(std::filesystem::u8path(utf8(p)), std::ios::binary | std::ios::ate); if (!f) return false;
    auto size = f.tellg(); if (size < 0 || static_cast<uint64_t>(size) > limit) return false;
    out.resize(static_cast<size_t>(size)); f.seekg(0); return static_cast<bool>(f.read(out.data(), size));
}
bool atomic_write(const std::wstring &p, const std::string &text) {
    std::string path = utf8(p), temp = path + ".tmp.XXXXXX";
    int fd = mkstemp(temp.data()); if (fd < 0) return false;
    size_t done = 0; bool ok = true;
    while (done < text.size()) { ssize_t n = write(fd, text.data() + done, text.size() - done); if (n < 0 && errno == EINTR) continue; if (n <= 0) { ok = false; break; } done += static_cast<size_t>(n); }
    if (ok && fsync(fd) != 0) ok = false;
    if (close(fd) != 0) ok = false;
    if (ok && rename(temp.c_str(), path.c_str()) != 0) ok = false;
    if (!ok) unlink(temp.c_str());
    return ok;
}
uint64_t now_seconds() { return static_cast<uint64_t>(time(nullptr)); }
uint64_t timestamp(const std::string &s) {
    if (s.size() < 20 || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' || s[16] != ':') return 0;
    auto num = [&](size_t i, size_t n) { unsigned v = 0; for (size_t k = 0; k < n; ++k) { if (s[i+k] < '0' || s[i+k] > '9') return 999999u; v = v*10 + unsigned(s[i+k]-'0'); } return v; };
    unsigned year=num(0,4), month=num(5,2), day=num(8,2), hour=num(11,2), minute=num(14,2), second=num(17,2);
    if (year<1970 || year>9999 || month<1 || month>12 || day<1 || day>31 || hour>23 || minute>59 || second>59) return 0;
    size_t at=19; if (s[at]=='.') { size_t start=++at; while (at<s.size() && s[at]>='0' && s[at]<='9') ++at; if (at==start) return 0; }
    int offset=0;
    if (at+1==s.size() && s[at]=='Z') {} else if (at+6==s.size() && (s[at]=='+' || s[at]=='-') && s[at+3]==':') {
        unsigned h=num(at+1,2), m=num(at+4,2); if (h>23 || m>59) return 0; offset=int(h*3600+m*60)*(s[at]=='+'?1:-1);
    } else return 0;
    std::tm t{}; t.tm_year=int(year)-1900; t.tm_mon=int(month)-1; t.tm_mday=int(day); t.tm_hour=int(hour); t.tm_min=int(minute); t.tm_sec=int(second);
    time_t raw=timegm(&t); std::tm check{}; if (!gmtime_r(&raw,&check) || check.tm_year != int(year)-1900 || check.tm_mon != int(month)-1 || check.tm_mday != int(day)) return 0;
    int64_t value=static_cast<int64_t>(raw)-offset; return value>0 ? static_cast<uint64_t>(value) : 0;
}
int local_day(uint64_t seconds) { if (seconds>253402300799ULL) return 0; time_t t=static_cast<time_t>(seconds); std::tm v{}; return localtime_r(&t,&v) ? (v.tm_year+1900)*10000+(v.tm_mon+1)*100+v.tm_mday : 0; }
uint64_t hash_bytes(const char *bytes, size_t length) { uint64_t h=14695981039346656037ULL; for(size_t i=0;i<length;++i) {h^=static_cast<unsigned char>(bytes[i]); h*=1099511628211ULL;} return h; }
std::string hex(uint64_t value) { char b[32]; std::snprintf(b,sizeof b,"%016llx",static_cast<unsigned long long>(value)); return b; }
std::wstring grouped(uint64_t value) { auto s=std::to_wstring(value); for(int i=static_cast<int>(s.size())-3;i>0;i-=3) s.insert(i,L","); return s; }
std::vector<FileInfo> list_logs(const std::wstring &root, bool &error, HANDLE stop) {
    std::vector<FileInfo> out; std::error_code ec;
    auto it=std::filesystem::recursive_directory_iterator(std::filesystem::u8path(utf8(root)), std::filesystem::directory_options::none,ec);
    if(ec) { error=true; return out; }
    auto end=std::filesystem::recursive_directory_iterator{}; size_t visited=0;
    while(it!=end) {
        if(stop && stop->load()) break;
        if(++visited>50000 || it.depth()>48) {error=true; break;}
        auto p=it->path(); auto status=it->symlink_status(ec);
        if(ec) {error=true; break;}
        if(std::filesystem::is_symlink(status)) it.disable_recursion_pending();
        else if(std::filesystem::is_regular_file(status) && p.extension()==".jsonl") {
            struct stat st{}; if(stat(p.c_str(),&st)!=0) error=true;
            else {out.push_back({wide(p.u8string()), static_cast<uint64_t>(st.st_size), static_cast<uint64_t>(st.st_mtim.tv_sec)*1000000000ULL+static_cast<uint64_t>(st.st_mtim.tv_nsec)}); if(out.size()>=20000) {error=true; break;}}
        }
        it.increment(ec); if(ec) {error=true; break;}
    }
    std::sort(out.begin(),out.end(),[](const FileInfo&a,const FileInfo&b){return a.modified>b.modified;}); return out;
}
bool valid_language_id(const std::string &id) {
    if(id.empty() || id.size()>35 || id.front()=='-' || id.back()=='-') return false;
    for(char c:id) if(!((c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='-')) return false;
    return id.find("--")==std::string::npos;
}
} // namespace aimon
