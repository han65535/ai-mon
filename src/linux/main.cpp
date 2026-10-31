// AI Mon Linux desktop application.
#include <FL/Fl.H>
#include <FL/fl_config.h>
#if !defined(FLTK_USE_X11) || !defined(FLTK_USE_WAYLAND)
#error "AI Mon requires FLTK with both X11 and Wayland backends"
#endif
#include <FL/Fl_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Progress.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/fl_ask.H>
#include <gio/gio.h>
#include "collector.hpp"
#include "linux/probes.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>

static Fl_Window *detail, *mini;
static Fl_Box *folder_label, *tray_label, *usage_label, *mini_label;
static std::array<std::string, 2> folders;
static bool refresh_requested = false;
static std::mutex data_mutex;
static std::condition_variable data_changed;
static bool backend_stop = false;
static std::atomic_bool backend_cancel{false};
static aimon::Snapshot current;
static std::array<std::string, 2> requested_folders;
static bool folders_changed = false;
static bool running = true, host_available = false, registered = false;
static GDBusConnection *bus;

static const char *watcher = "org.kde.StatusNotifierWatcher";
static const char *watcher_path = "/StatusNotifierWatcher";
static const char *item_iface = "org.kde.StatusNotifierItem";
static const char *menu_iface = "com.canonical.dbusmenu";
static const char *xml = R"XML(<node>
 <interface name='org.kde.StatusNotifierItem'>
  <method name='Activate'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>
  <method name='SecondaryActivate'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>
  <method name='ContextMenu'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>
  <method name='Scroll'><arg type='i' direction='in'/><arg type='s' direction='in'/></method>
  <signal name='NewIcon'/>
  <property name='Category' type='s' access='read'/><property name='Id' type='s' access='read'/>
  <property name='Title' type='s' access='read'/><property name='Status' type='s' access='read'/>
  <property name='WindowId' type='u' access='read'/><property name='IconName' type='s' access='read'/>
  <property name='IconPixmap' type='a(iiay)' access='read'/><property name='OverlayIconName' type='s' access='read'/>
  <property name='AttentionIconName' type='s' access='read'/><property name='Menu' type='o' access='read'/>
  <property name='ItemIsMenu' type='b' access='read'/>
 </interface>
 <interface name='com.canonical.dbusmenu'>
  <method name='GetLayout'><arg type='i' direction='in'/><arg type='i' direction='in'/><arg type='as' direction='in'/><arg type='u' direction='out'/><arg type='(ia{sv}av)' direction='out'/></method>
  <method name='GetGroupProperties'><arg type='ai' direction='in'/><arg type='as' direction='in'/><arg type='a(ia{sv})' direction='out'/></method>
  <method name='GetProperty'><arg type='i' direction='in'/><arg type='s' direction='in'/><arg type='v' direction='out'/></method>
  <method name='Event'><arg type='i' direction='in'/><arg type='s' direction='in'/><arg type='v' direction='in'/><arg type='u' direction='in'/></method>
  <method name='AboutToShow'><arg type='i' direction='in'/><arg type='b' direction='out'/></method>
  <property name='Version' type='u' access='read'/><property name='TextDirection' type='s' access='read'/><property name='Status' type='s' access='read'/><property name='IconThemePath' type='as' access='read'/>
 </interface>
</node>)XML";
static const char *labels[] = {"", "열기", "미니 창 표시/숨기기", "종료"};
static std::string config_path(int provider) {
    const char *config = std::getenv("XDG_CONFIG_HOME"), *home = std::getenv("HOME");
    std::string base = config && config[0] == '/' ? config : home && *home ? std::string(home) + "/.config" : "";
    return base.empty() ? "" : base + "/ai-mon/" + (provider == 0 ? "claude-root" : "codex-root");
}
static void show_detail() { detail->show(); }
static void stop() { running = false; backend_cancel.store(true); { std::lock_guard<std::mutex> lock(data_mutex); backend_stop = true; } data_changed.notify_all(); detail->hide(); mini->hide(); }
static void toggle_mini() { if (mini->shown()) mini->hide(); else mini->show(); }
static void action(int id) { if (id == 1) show_detail(); else if (id == 2) toggle_mini(); else if (id == 3) stop(); }
static void update_folder() { std::string label = "Claude: " + (folders[0].empty() ? "기본 폴더" : folders[0]) + "\nCodex: " + (folders[1].empty() ? "기본 폴더" : folders[1]); folder_label->copy_label(label.c_str()); folder_label->redraw(); }
static bool save_folder(int provider) {
    const auto path = config_path(provider); if (path.empty() || folders[provider].find('\n') != std::string::npos) return false;
    auto parent = std::filesystem::path(path).parent_path().u8string();
    return aimon::ensure_directory(aimon::wide(parent)) && aimon::atomic_write(aimon::wide(path), folders[provider]);
}
static void choose_folder(Fl_Widget *, void *data) {
    int provider = static_cast<int>(reinterpret_cast<intptr_t>(data));
    Fl_Native_File_Chooser chooser; chooser.title(provider == 0 ? "Claude 로그 폴더 선택" : "Codex 로그 폴더 선택"); chooser.type(Fl_Native_File_Chooser::BROWSE_DIRECTORY);
    if (chooser.show() == 0 && chooser.filename()) {
        auto previous = folders[provider]; folders[provider] = chooser.filename();
        if (!save_folder(provider)) { folders[provider] = previous; fl_alert("폴더 저장 실패"); return; }
        { std::lock_guard<std::mutex> lock(data_mutex); requested_folders = folders; folders_changed = true; }
        data_changed.notify_all(); update_folder();
    }
}
static void refresh(Fl_Widget *, void *) { { std::lock_guard<std::mutex> lock(data_mutex); refresh_requested = true; } data_changed.notify_all(); }
static void close_detail(Fl_Widget *, void *) { if (host_available || mini->shown()) detail->hide(); else stop(); }
static void close_mini(Fl_Widget *w, void *) { w->hide(); if (!host_available && !detail->shown()) detail->show(); }
static GVariant *menu_properties(int id) {
    GVariantBuilder b; g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    if (id >= 1 && id <= 3) { g_variant_builder_add(&b, "{sv}", "label", g_variant_new_string(labels[id])); g_variant_builder_add(&b, "{sv}", "enabled", g_variant_new_boolean(TRUE)); }
    if (id == 0) g_variant_builder_add(&b, "{sv}", "children-display", g_variant_new_string("submenu"));
    return g_variant_builder_end(&b);
}
static GVariant *property(GDBusConnection *, const gchar *, const gchar *, const gchar *iface, const gchar *name, GError **, gpointer) {
    std::string n(name);
    if (std::string(iface) == menu_iface) {
        if (n == "Version") return g_variant_new_uint32(3);
        if (n == "TextDirection") return g_variant_new_string("ltr");
        if (n == "Status") return g_variant_new_string("normal");
        if (n == "IconThemePath") return g_variant_new_strv(nullptr, 0);
        return nullptr;
    }
    if (n == "Category") return g_variant_new_string("ApplicationStatus");
    if (n == "Id") return g_variant_new_string("ai-mon-linux");
    if (n == "Title") return g_variant_new_string("AI Mon");
    if (n == "Status") return g_variant_new_string("Active");
    if (n == "WindowId") return g_variant_new_uint32(0);
    if (n == "IconName" || n == "OverlayIconName" || n == "AttentionIconName") return g_variant_new_string("");
    if (n == "Menu") return g_variant_new_object_path("/Menu");
    if (n == "ItemIsMenu") return g_variant_new_boolean(FALSE);
    if (n == "IconPixmap") {
        aimon::Snapshot snapshot;
        { std::lock_guard<std::mutex> lock(data_mutex); snapshot = current; }
        unsigned char pixels[24 * 24 * 4];
        for (int y = 0; y < 24; ++y) for (int x = 0; x < 24; ++x) {
            auto *p = &pixels[(y * 24 + x) * 4]; p[0] = 255; p[1] = 38; p[2] = 52; p[3] = 69;
            for (int index = 0; index < 4; ++index) {
                const auto &q = index % 2 ? snapshot.providers[index / 2].quota.latest.weekly : snapshot.providers[index / 2].quota.latest.short_term;
                const bool available = aimon::quota_current(q, aimon::now_seconds());
                const int height = available ? (q.remaining * 16 + 9999) / 10000 : 2;
                if (x >= 3 + index * 5 && x < 6 + index * 5 && y >= 20 - height && y < 20) {
                    p[1] = available ? (index < 2 ? 107 : 118) : 130;
                    p[2] = available ? (index < 2 ? 215 : 182) : 130;
                    p[3] = available ? (index < 2 ? 155 : 255) : 130;
                }
            }
        }
        GVariantBuilder b; g_variant_builder_init(&b, G_VARIANT_TYPE("a(iiay)"));
        g_variant_builder_add(&b, "(ii@ay)", 24, 24, g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, pixels, sizeof pixels, 1));
        return g_variant_builder_end(&b);
    }
    return nullptr;
}
static void call(GDBusConnection *, const gchar *, const gchar *, const gchar *iface, const gchar *method, GVariant *args, GDBusMethodInvocation *inv, gpointer) {
    if (std::string(iface) == item_iface) {
        if (std::string(method) == "Activate" || std::string(method) == "SecondaryActivate") show_detail();
        // Hosts supporting DBusMenu render /Menu; screen coordinates are not portable on Wayland.
        if (std::string(method) == "ContextMenu") show_detail();
        g_dbus_method_invocation_return_value(inv, nullptr); return;
    }
    std::string m(method);
    if (m == "GetLayout") {
        gint32 parent, depth; GVariant *names; g_variant_get(args, "(ii@as)", &parent, &depth, &names); g_variant_unref(names);
        GVariantBuilder children; g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
        if (parent == 0 && depth != 0) for (int id = 1; id <= 3; ++id) {
            GVariantBuilder empty; g_variant_builder_init(&empty, G_VARIANT_TYPE("av"));
            g_variant_builder_add(&children, "v", g_variant_new("(i@a{sv}@av)", id, menu_properties(id), g_variant_builder_end(&empty)));
        }
        g_dbus_method_invocation_return_value(inv, g_variant_new("(u(i@a{sv}@av))", 1u, parent, menu_properties(parent), g_variant_builder_end(&children))); return;
    }
    if (m == "GetGroupProperties") {
        GVariant *ids, *names; g_variant_get(args, "(@ai@as)", &ids, &names);
        GVariantBuilder out; g_variant_builder_init(&out, G_VARIANT_TYPE("a(ia{sv})")); GVariantIter iter; gint32 id; g_variant_iter_init(&iter, ids);
        while (g_variant_iter_next(&iter, "i", &id)) if (id >= 0 && id <= 3) g_variant_builder_add(&out, "(i@a{sv})", id, menu_properties(id));
        g_variant_unref(ids); g_variant_unref(names); g_dbus_method_invocation_return_value(inv, g_variant_new("(@a(ia{sv}))", g_variant_builder_end(&out))); return;
    }
    if (m == "GetProperty") {
        gint32 id; const char *key; g_variant_get(args, "(i&s)", &id, &key);
        GVariant *dict = menu_properties(id); GVariant *value = g_variant_lookup_value(dict, key, nullptr); g_variant_ref_sink(dict);
        if (value) { g_dbus_method_invocation_return_value(inv, g_variant_new("(v)", value)); g_variant_unref(value); }
        else g_dbus_method_invocation_return_dbus_error(inv, "com.canonical.dbusmenu.Error.InvalidProperty", "Unknown property");
        g_variant_unref(dict); return;
    }
    if (m == "Event") { gint32 id; const char *event; GVariant *data; guint32 stamp; g_variant_get(args, "(i&s@vu)", &id, &event, &data, &stamp); g_variant_unref(data); if (std::string(event) == "clicked") action(id); g_dbus_method_invocation_return_value(inv, nullptr); return; }
    if (m == "AboutToShow") { g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", FALSE)); return; }
    g_dbus_method_invocation_return_dbus_error(inv, "org.freedesktop.DBus.Error.UnknownMethod", "Unknown method");
}
static const GDBusInterfaceVTable vtable = {call, property, nullptr, {nullptr}};
static void register_done(GObject *source, GAsyncResult *result, gpointer) {
    GError *error = nullptr; GVariant *v = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    registered = v != nullptr; if (v) g_variant_unref(v); if (error) { std::fprintf(stderr, "tray registration: %s\n", error->message); g_error_free(error); }
}
static void refresh_host() {
    if (!bus) return;
    GError *error = nullptr;
    GVariant *v = g_dbus_connection_call_sync(bus, watcher, watcher_path, "org.freedesktop.DBus.Properties", "Get", g_variant_new("(ss)", watcher, "IsStatusNotifierHostRegistered"), G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, 200, nullptr, &error);
    bool available = false;
    if (v) { GVariant *boxed; g_variant_get(v, "(@v)", &boxed); GVariant *inner = g_variant_get_variant(boxed); if (g_variant_is_of_type(inner, G_VARIANT_TYPE_BOOLEAN)) available = g_variant_get_boolean(inner); g_variant_unref(inner); g_variant_unref(boxed); g_variant_unref(v); }
    if (error) g_error_free(error);
    bool next_host = registered && available;
    if (next_host != host_available) std::fprintf(stderr, "tray host %s\n", next_host ? "available" : "unavailable");
    host_available = next_host;
    tray_label->copy_label(host_available ? "트레이 사용 가능" : "트레이 없음: 창에서 종료하세요"); tray_label->redraw();
    if (!host_available && !detail->shown() && !mini->shown()) detail->show();
    if (!available) registered = false;
    if (available && !registered) {
        g_dbus_connection_call(bus, watcher, watcher_path, watcher, "RegisterStatusNotifierItem", g_variant_new("(s)", g_dbus_connection_get_unique_name(bus)), nullptr, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, register_done, nullptr);
    }
}
static std::wstring state_directory() {
    auto state = aimon::env(L"XDG_STATE_HOME");
    if (state.empty() || state[0] != L'/') {
        auto home = aimon::env(L"HOME");
        if (home.empty() || home[0] != L'/') return {};
        state = aimon::join(home, L".local/state");
    }
    return aimon::join(state, L"ai-mon");
}
static void worker() {
    auto dir = state_directory();
    if (!aimon::ensure_directory(dir)) { std::fprintf(stderr, "cannot create state directory\n"); return; }
    aimon::Collector collector(dir); auto settings = aimon::default_settings();
    { std::lock_guard<std::mutex> lock(data_mutex); for (int i = 0; i < 2; ++i) if (!requested_folders[i].empty()) settings.roots[i] = aimon::canonical(aimon::wide(requested_folders[i])); }
    collector.configure(settings);
    if (!collector.load()) std::fprintf(stderr, "cache could not be loaded; rebuilding\n");
    auto next_probe = std::chrono::steady_clock::now();
    for (;;) {
        auto snapshot = collector.scan(&backend_cancel);
        { std::lock_guard<std::mutex> lock(data_mutex); current = snapshot; }
        if (std::chrono::steady_clock::now() >= next_probe) {
            if (settings.enabled[0]) aimon::probe_claude_linux(dir, &backend_cancel);
            if (settings.enabled[1] && !backend_cancel.load()) aimon::probe_codex_linux(dir, &backend_cancel);
            next_probe = std::chrono::steady_clock::now() + std::chrono::minutes(2);
            if (!backend_cancel.load()) { snapshot = collector.scan(&backend_cancel); std::lock_guard<std::mutex> lock(data_mutex); current = snapshot; }
        }
        if (backend_cancel.load()) break;
        std::unique_lock<std::mutex> lock(data_mutex);
        data_changed.wait_for(lock, std::chrono::seconds(10), [] { return backend_stop || folders_changed || refresh_requested; });
        if (backend_stop) break;
        bool changed = folders_changed; folders_changed = false;
        if (refresh_requested) { next_probe = std::min(next_probe, std::chrono::steady_clock::now() + std::chrono::seconds(15)); refresh_requested = false; }
        auto roots = requested_folders;
        lock.unlock();
        if (changed) { for (int i = 0; i < 2; ++i) if (!roots[i].empty()) settings.roots[i] = aimon::canonical(aimon::wide(roots[i])); collector.configure(settings); }
    }
}
static void update_usage(void *) {
    aimon::Snapshot snapshot;
    { std::lock_guard<std::mutex> lock(data_mutex); snapshot = current; }
    auto format = [&](int index, const char *name) {
        const auto &row = snapshot.providers[index];
        const char *status = "확인 중";
        switch (row.status) {
        case aimon::Status::Disabled: status = "사용 안 함"; break;
        case aimon::Status::Collecting: status = "수집 중"; break;
        case aimon::Status::Ready: status = "정상"; break;
        case aimon::Status::Empty: status = "기록 없음"; break;
        case aimon::Status::Missing: status = "폴더 없음"; break;
        case aimon::Status::ReadError: status = "읽기 실패"; break;
        case aimon::Status::Unsupported: status = "형식 미지원"; break;
        case aimon::Status::Partial: status = "일부 기록"; break;
        }
        auto percent = [](const aimon::QuotaWindow &q) { return aimon::quota_current(q, aimon::now_seconds()) ? std::to_string(q.remaining / 100) + "%" : std::string("—"); };
        std::string measured;
        auto observed = row.quota.latest.observed;
        if (observed) {
            time_t when = static_cast<time_t>(observed); std::tm local{}; char formatted[32]{};
            if (localtime_r(&when, &local) && strftime(formatted, sizeof formatted, "%m-%d %H:%M", &local))
                measured = std::string("  ·  한도 ") + formatted + (aimon::now_seconds() > observed + 600 ? " (갱신 지연)" : "");
        }
        return std::string(name) + "   단기 " + percent(row.quota.latest.short_term) + "   주간 " + percent(row.quota.latest.weekly) + "\n입력 " + std::to_string(row.total.tokens.input) + "   출력 " + std::to_string(row.total.tokens.output) + "  ·  " + status + measured;
    };
    std::string full = format(0, "Claude") + "\n" + format(1, "Codex");
    usage_label->copy_label(full.c_str()); usage_label->redraw();
    static std::string previous_icon;
    std::string icon_state;
    for (const auto &provider : snapshot.providers) for (const auto *q : {&provider.quota.latest.short_term, &provider.quota.latest.weekly})
        icon_state += aimon::quota_current(*q, aimon::now_seconds()) ? std::to_string(q->remaining) + ":" : "-:";
    if (icon_state != previous_icon && bus) {
        previous_icon = icon_state;
        g_dbus_connection_emit_signal(bus, nullptr, "/StatusNotifierItem", item_iface, "NewIcon", nullptr, nullptr);
    }
    auto percent = [](const aimon::QuotaWindow &q) { return aimon::quota_current(q, aimon::now_seconds()) ? std::to_string(q.remaining / 100) + "%" : std::string("—"); };
    std::string small = "Claude " + percent(snapshot.providers[0].quota.latest.short_term) + " / " + percent(snapshot.providers[0].quota.latest.weekly) + "\nCodex  " + percent(snapshot.providers[1].quota.latest.short_term) + " / " + percent(snapshot.providers[1].quota.latest.weekly);
    mini_label->copy_label(small.c_str()); mini_label->redraw();
    Fl::repeat_timeout(1.0, update_usage);
}
static void pump(void *) { while (g_main_context_pending(nullptr)) g_main_context_iteration(nullptr, FALSE); Fl::repeat_timeout(.05, pump); }
static void poll_host(void *) { refresh_host(); Fl::repeat_timeout(2.0, poll_host); }
int main(int argc, char **argv) {
    if (argc == 3 && std::string(argv[1]) == "--path-roundtrip") { folders[0] = argv[2]; if (!save_folder(0)) return 1; std::ifstream f(config_path(0), std::ios::binary); std::string read((std::istreambuf_iterator<char>(f)), {}); return read == folders[0] ? 0 : 1; }
    for (int i = 0; i < 2; ++i) { std::ifstream f(config_path(i), std::ios::binary); folders[i].assign(std::istreambuf_iterator<char>(f), {}); }
    auto state = state_directory();
    if (state.empty() || !aimon::ensure_directory(state)) { std::fprintf(stderr, "Cannot create state directory\n"); return 1; }
    std::string lock_path = aimon::utf8(aimon::join(state, L"instance.lock"));
    int lockfd = open(lock_path.c_str(), O_CREAT | O_CLOEXEC | O_RDWR, 0600);
    if (lockfd < 0 || flock(lockfd, LOCK_EX | LOCK_NB) != 0) { std::fprintf(stderr, "AI Mon is already running or cannot create its lock\n"); if (lockfd >= 0) close(lockfd); return 1; }
    GError *error = nullptr; bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (error) { std::fprintf(stderr, "session bus: %s\n", error->message); g_error_free(error); }
    if (bus) {
        GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(xml, &error);
        if (!info) { std::fprintf(stderr, "D-Bus XML: %s\n", error->message); g_error_free(error); return 1; }
        guint a = g_dbus_connection_register_object(bus, "/StatusNotifierItem", g_dbus_node_info_lookup_interface(info, item_iface), &vtable, nullptr, nullptr, &error);
        if (!a) { std::fprintf(stderr, "item: %s\n", error->message); g_error_free(error); return 1; }
        guint b = g_dbus_connection_register_object(bus, "/Menu", g_dbus_node_info_lookup_interface(info, menu_iface), &vtable, nullptr, nullptr, &error);
        if (!b) { std::fprintf(stderr, "menu: %s\n", error->message); g_error_free(error); return 1; }
        g_dbus_node_info_unref(info);
    }
    detail = new Fl_Window(500, 310, "AI Mon");
    usage_label = new Fl_Box(10, 8, 480, 100, "사용량 수집 중"); usage_label->labelsize(14);
    folder_label = new Fl_Box(15, 110, 470, 50); folder_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP); update_folder();
    auto *choose = new Fl_Button(15, 170, 140, 30, "Claude 폴더"); choose->callback(choose_folder, reinterpret_cast<void *>(intptr_t(0)));
    auto *choose_codex = new Fl_Button(165, 170, 140, 30, "Codex 폴더"); choose_codex->callback(choose_folder, reinterpret_cast<void *>(intptr_t(1)));
    auto *toggle = new Fl_Button(315, 170, 170, 30, "미니 창"); toggle->callback([](Fl_Widget *, void *) { toggle_mini(); });
    tray_label = new Fl_Box(15, 205, 470, 30, "트레이 없음: 창에서 종료하세요");
    auto *reload = new Fl_Button(15, 250, 225, 30, "새로고침"); reload->callback(refresh);
    auto *exit = new Fl_Button(260, 250, 225, 30, "종료"); exit->callback([](Fl_Widget *, void *) { stop(); });
    detail->end(); detail->callback(close_detail);
    mini = new Fl_Window(190, 58, "AI Mon mini");
    mini_label = new Fl_Box(5, 5, 180, 45, "사용량 수집 중"); mini_label->labelsize(12);
    mini->end(); mini->callback(close_mini);
    { std::lock_guard<std::mutex> lock(data_mutex); requested_folders = folders; }
    std::thread collector_thread(worker);
    detail->show(); Fl::add_timeout(.05, pump); Fl::add_timeout(.2, poll_host); Fl::add_timeout(.1, update_usage);
    while (running) Fl::wait();
    collector_thread.join();
    if (bus) g_object_unref(bus);
    close(lockfd);
    return 0;
}
