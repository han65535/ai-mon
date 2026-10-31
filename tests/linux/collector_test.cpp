#include "collector.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <ctime>
using namespace aimon;
static void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
static std::string timestamp_now() { time_t t=time(nullptr); tm utc{}; gmtime_r(&t,&utc); char out[32]; strftime(out,sizeof out,"%Y-%m-%dT%H:%M:%SZ",&utc); return out; }
static std::string event(const std::string &id, int n) {
    return "{\"type\":\"assistant\",\"sessionId\":\"test\",\"timestamp\":\""+timestamp_now()+"\",\"message\":{\"id\":\""+id+"\",\"usage\":{\"input_tokens\":"+std::to_string(n)+",\"output_tokens\":2}}}";
}
int main(int argc, char **argv) {
    if(argc!=2) return 2;
    try {
        auto root=std::filesystem::u8path(argv[1]); auto logs=root / std::filesystem::u8path("한글 logs"); auto data=root / "data";
        std::filesystem::create_directories(logs); std::filesystem::create_directories(data);
        auto file=logs / "session.jsonl";
        { std::ofstream f(file); f << event("a",10) << '\n' << event("b",20); }
        auto settings=default_settings(); settings.roots[0]=wide(logs.u8string()); settings.enabled[1]=false;
        Collector first(wide(data.u8string())); first.configure(settings); auto snap=first.scan();
        check(snap.providers[0].total.tokens.input==10, "partial line counted early");
        { std::ofstream f(file,std::ios::app); f << '\n'; }
        snap=first.scan(); check(snap.providers[0].total.tokens.input==30, "append missed");
        check(first.save(), "save failed");
        Collector restored(wide(data.u8string())); restored.configure(settings); check(restored.load(), "load failed");
        snap=restored.scan(); check(snap.providers[0].total.tokens.input==30, "restart duplicated data");
        auto replaced=logs / "new.jsonl"; {std::ofstream f(replaced); f << event("c",5) << '\n';} std::filesystem::rename(replaced,file);
        snap=restored.scan(); check(snap.providers[0].total.tokens.input==35, "replacement missed");
        auto different=root / std::filesystem::u8path("한글 LOGS"); std::filesystem::create_directories(different);
        settings.roots[0]=wide(different.u8string()); restored.configure(settings); snap=restored.scan();
        check(snap.providers[0].total.tokens.input==0, "case-sensitive root did not reset");
        check(timestamp("2026-02-30T00:00:00Z")==0, "invalid date accepted");
        check(timestamp("2026-09-10T09:00:00+09:00")==timestamp("2026-09-10T00:00:00Z"), "time zone conversion");
        check(!parse(std::string("{\"x\":\"")+char(0xff)+"\"}"), "invalid UTF-8 accepted");
        for (auto invalid : {std::string("\xc0\x80",2), std::string("\xed\xa0\x80",3), std::string("\xf4\x90\x80\x80",4), std::string("\xe2\x82",2)})
            check(!parse("{\"x\":\"" + invalid + "\"}"), "invalid Unicode accepted");
        std::cout << "Linux collector checks passed\n"; return 0;
    } catch(const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
