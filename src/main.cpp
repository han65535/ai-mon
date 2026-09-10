#include "collector.hpp"
#include "localization.hpp"
#include "claude_bridge.hpp"
#include "mini_window.hpp"
#include "startup.hpp"
#include <shellapi.h>
#include <commctrl.h>
#include <algorithm>
#include <cstdio>
#include <exception>

using namespace aimon;
namespace {
constexpr wchar_t MainClass[] = L"AI.Mon.Window.1";
constexpr wchar_t ChartClass[] = L"AI.Mon.QuotaChart.1";
constexpr wchar_t SettingsClass[] = L"AI.Mon.Settings.1";
std::wstring main_class_name = MainClass;
constexpr UINT TrayMessage = WM_APP + 1, UpdatedMessage = WM_APP + 2;
constexpr int Open = 100, Refresh = 101, Configure = 102, About = 103, Exit = 104, Mini = 105;
HINSTANCE instance = nullptr;
Localizer locale;
MiniWindow mini;
const wchar_t *tr(const char *key) {
    return locale.text(key);
}
struct App {
    HWND window = nullptr, settings_window = nullptr;
    Handle stop, wake, worker, mutex;
    CRITICAL_SECTION lock{};
    Settings settings;
    Snapshot snapshot;
    ClaudeProbe claude_probe = ClaudeProbe::Waiting;
    bool probe_enabled = true;
    bool startup_controls = true;
    std::wstring executable;
    std::wstring data_dir;
    bool worker_failed = false, settings_warning = false, tray_active = false;
    HWND chart[2][2]{};
    HICON tray_icons[2]{};
    UINT dpi = 96, taskbar_created = 0;
    HFONT font = nullptr, title_font = nullptr, number_font = nullptr;
    std::vector<HWND> controls;
    HWND status[2]{}, cache[2]{}, note = nullptr;
    App() { InitializeCriticalSection(&lock); }
    ~App() {
        if (font)
            DeleteObject(font);
        if (title_font)
            DeleteObject(title_font);
        if (number_font)
            DeleteObject(number_font);
        DeleteCriticalSection(&lock);
    }
} app;
struct Lock {
    explicit Lock() { EnterCriticalSection(&app.lock); }
    ~Lock() { LeaveCriticalSection(&app.lock); }
};
int scaled(int v, UINT dpi) {
    return MulDiv(v, static_cast<int>(dpi), 96);
}
UINT window_dpi(HWND window) {
    using Fn = UINT(WINAPI *)(HWND);
    auto fn = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (fn)
        return fn(window);
    HDC dc = GetDC(window);
    UINT dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc)
        ReleaseDC(window, dc);
    return dpi;
}
void adjust_rect(RECT &rect, DWORD style, DWORD extended, UINT dpi) {
    using Fn = BOOL(WINAPI *)(LPRECT, DWORD, BOOL, DWORD, UINT);
    auto fn =
        reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
    if (fn)
        fn(&rect, style, FALSE, extended, dpi);
    else
        AdjustWindowRectEx(&rect, style, FALSE, extended);
}
std::wstring data_key(const std::wstring &path) {
    std::wstring normalized = canonical(path);
    CharLowerBuffW(normalized.data(), static_cast<DWORD>(normalized.size()));
    std::string encoded = utf8(normalized);
    return wide(hex(hash_bytes(encoded.data(), encoded.size())));
}
void show_window() {
    ShowWindow(app.window, SW_RESTORE);
    SetForegroundWindow(app.window);
}
HFONT make_font(int size, int weight, UINT dpi);
std::wstring percent(const QuotaWindow &value) {
    if (!quota_current(value, now_seconds()))
        return L"—";
    return std::to_wstring(value.remaining / 100) +
           (value.remaining % 100 ? L"." + std::to_wstring((value.remaining % 100) / 10) : L"") + L"%";
}
COLORREF quota_color(int remaining) {
    return remaining <= 1000 ? RGB(220, 55, 55) : remaining <= 2500 ? RGB(210, 130, 20) : RGB(30, 155, 95);
}
HICON quota_icon(int provider, const QuotaSnapshot &quota, bool enabled) {
    const int size = GetSystemMetrics(SM_CXSMICON);
    HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, size, size),
            mask = CreateBitmap(size, size, 1, 1, nullptr);
    HDC mask_dc = CreateCompatibleDC(screen);
    auto old_mask = SelectObject(mask_dc, mask);
    PatBlt(mask_dc, 0, 0, size, size, BLACKNESS);
    SelectObject(mask_dc, old_mask);
    DeleteDC(mask_dc);
    auto old = SelectObject(dc, bitmap);
    RECT r{0, 0, size, size};
    HBRUSH background = CreateSolidBrush(provider == 0 ? RGB(73, 46, 34) : RGB(25, 55, 77));
    FillRect(dc, &r, background);
    DeleteObject(background);
    int remaining = 10001;
    for (const auto &value : {quota.short_term, quota.weekly})
        if (enabled && quota_current(value, now_seconds()))
            remaining = std::min(remaining, value.remaining);
    std::wstring label = remaining <= 10000 ? std::to_wstring(remaining / 100) : L"—";
    HFONT font = CreateFontW(-MulDiv(size, remaining == 10000 ? 50 : 75, 100), 0, 0, 0, FW_BOLD, FALSE, FALSE,
                             FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    auto old_font = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    r.bottom = size - 3;
    DrawTextW(dc, label.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old_font);
    DeleteObject(font);
    int half = size / 2;
    for (int slot = 0; slot < 2; ++slot) {
        const auto &value = slot ? quota.weekly : quota.short_term;
        bool available = enabled && quota_current(value, now_seconds());
        RECT track{slot * half, size - 3, (slot + 1) * half - 1, size};
        HBRUSH brush = CreateSolidBrush(RGB(105, 105, 105));
        FillRect(dc, &track, brush);
        DeleteObject(brush);
        if (available) {
            track.right = track.left + MulDiv(half - 1, value.remaining, 10000);
            brush = CreateSolidBrush(quota_color(value.remaining));
            FillRect(dc, &track, brush);
            DeleteObject(brush);
        }
    }
    SelectObject(dc, old);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmColor = bitmap;
    info.hbmMask = mask;
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(bitmap);
    DeleteObject(mask);
    return icon;
}
void notify_icon(DWORD operation) {
    if (operation == NIM_ADD)
        app.tray_active = true;
    if (!app.tray_active && operation != NIM_DELETE)
        return;
    Snapshot snapshot;
    {
        Lock lock;
        snapshot = app.snapshot;
    }
    for (int i = 0; i < 2; ++i) {
        NOTIFYICONDATAW n{};
        n.cbSize = sizeof(n);
        n.hWnd = app.window;
        n.uID = static_cast<UINT>(i + 1);
        n.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        n.uCallbackMessage = TrayMessage;
        if (operation != NIM_DELETE) {
            HICON icon = quota_icon(i, snapshot.providers[i].quota.latest,
                                    snapshot.providers[i].status != Status::Disabled);
            if (app.tray_icons[i])
                DestroyIcon(app.tray_icons[i]);
            app.tray_icons[i] = icon;
            n.hIcon = icon ? icon : LoadIconW(instance, MAKEINTRESOURCEW(101));
            auto tip = std::wstring(i ? L"Codex" : L"Claude") + L" · " + tr("quota.remaining") + L"\n" +
                       tr("quota.short") + L" " + percent(snapshot.providers[i].quota.latest.short_term) +
                       L" | " + tr("quota.week") + L" " + percent(snapshot.providers[i].quota.latest.weekly);
            if (snapshot.providers[i].quota.latest.observed &&
                now_seconds() > snapshot.providers[i].quota.latest.observed + 900)
                tip += L"\n" + std::wstring(tr("quota.stale"));
            wcsncpy_s(n.szTip, tip.c_str(), _TRUNCATE);
        }
        if (!Shell_NotifyIconW(operation, &n) && operation == NIM_ADD && !mini.visible())
            ShowWindow(app.window, SW_SHOW);
        if (operation == NIM_ADD) {
            n.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &n);
        }
        if (operation == NIM_DELETE && app.tray_icons[i]) {
            DestroyIcon(app.tray_icons[i]);
            app.tray_icons[i] = nullptr;
        }
    }
    if (operation == NIM_DELETE)
        app.tray_active = false;
}
LRESULT CALLBACK chart_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_CREATE) {
        SetWindowLongPtrW(
            window, GWLP_USERDATA,
            reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams
                ? reinterpret_cast<INT_PTR>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams)
                : 0);
        return 0;
    }
    if (message == WM_PAINT || message == WM_PRINTCLIENT) {
        PAINTSTRUCT paint{};
        HDC dc = message == WM_PRINTCLIENT ? reinterpret_cast<HDC>(w) : BeginPaint(window, &paint);
        RECT bounds{};
        GetClientRect(window, &bounds);
        FillRect(dc, &bounds, GetSysColorBrush(COLOR_WINDOW));
        int index = static_cast<int>(GetWindowLongPtrW(window, GWLP_USERDATA)), provider = index / 2,
            slot = index % 2;
        QuotaStore store;
        {
            Lock lock;
            store = app.snapshot.providers[provider].quota;
        }
        const auto &value = slot ? store.latest.weekly : store.latest.short_term;
        uint64_t now = now_seconds();
        bool available = quota_current(value, now);
        UINT dpi = window_dpi(window);
        auto x = [&](int n) { return scaled(n, dpi); };
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        auto old_font = SelectObject(dc, app.font);
        std::wstring title = slot ? tr("quota.week") : tr("quota.short");
        if (value.available && !slot)
            title = value.minutes % 60 == 0
                        ? locale.format("quota.hours", {{L"hours", std::to_wstring(value.minutes / 60)}})
                        : locale.format("quota.minutes", {{L"minutes", std::to_wstring(value.minutes)}});
        RECT r{x(8), x(2), bounds.right - x(8), x(23)};
        DrawTextW(dc, title.c_str(), -1, &r, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, app.number_font);
        r.top = x(24);
        r.bottom = x(55);
        auto label = percent(value);
        DrawTextW(dc, label.c_str(), -1, &r, DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, app.font);
        r.left = x(90);
        r.top = x(30);
        r.bottom = x(52);
        DrawTextW(dc, tr("quota.remaining"), -1, &r, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        RECT track{x(8), x(60), bounds.right - x(8), x(67)};
        FillRect(dc, &track, GetSysColorBrush(COLOR_3DFACE));
        if (available) {
            track.right = track.left + MulDiv(track.right - track.left, value.remaining, 10000);
            HBRUSH brush = CreateSolidBrush(quota_color(value.remaining));
            FillRect(dc, &track, brush);
            DeleteObject(brush);
        }
        RECT graph{x(8), x(78), bounds.right - x(8), x(114)};
        HPEN grid = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DSHADOW));
        auto old_pen = SelectObject(dc, grid);
        MoveToEx(dc, graph.left, graph.bottom, nullptr);
        LineTo(dc, graph.right, graph.bottom);
        SelectObject(dc, old_pen);
        DeleteObject(grid);
        std::vector<POINT> points;
        uint64_t duration = value.minutes * 60;
        uint64_t end = value.resets ? value.resets : now, start = end > duration ? end - duration : 0;
        if (available && duration) {
            auto add_point = [&](const QuotaSnapshot &sample) {
                const auto &v = slot ? sample.weekly : sample.short_term;
                if (v.available && v.resets == value.resets && v.minutes == value.minutes &&
                    sample.plan == store.latest.plan && sample.observed >= start &&
                    sample.observed <= std::min(end, now))
                    points.push_back({graph.left + static_cast<LONG>((sample.observed - start) *
                                                                     (graph.right - graph.left) / duration),
                                      graph.bottom - MulDiv(graph.bottom - graph.top, v.remaining, 10000)});
            };
            for (const auto &sample : store.history)
                add_point(sample);
            if (store.history.empty() || store.history.back().observed != store.latest.observed)
                add_point(store.latest);
        }
        if (!points.empty()) {
            HPEN pen = CreatePen(PS_SOLID, x(2), quota_color(value.remaining));
            old_pen = SelectObject(dc, pen);
            if (points.size() > 1)
                Polyline(dc, points.data(), static_cast<int>(points.size()));
            auto dot = points.back();
            Ellipse(dc, dot.x - x(2), dot.y - x(2), dot.x + x(3), dot.y + x(3));
            SelectObject(dc, old_pen);
            DeleteObject(pen);
        } else {
            auto text = available ? tr("quota.history_empty") : tr("quota.no_data");
            r = graph;
            DrawTextW(dc, text, -1, &r, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        std::wstring reset;
        if (value.available && value.resets && value.resets <= now)
            reset = tr("quota.await_reset");
        else if (available && value.resets) {
            uint64_t seconds = value.resets - now, hours = seconds / 3600, minutes = (seconds % 3600) / 60;
            reset = locale.format(
                "quota.reset", {{L"hours", std::to_wstring(hours)}, {L"minutes", std::to_wstring(minutes)}});
        } else
            reset = tr("quota.reset_unknown");
        r = {x(8), x(121), bounds.right - x(8), x(144)};
        DrawTextW(dc, reset.c_str(), -1, &r, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, old_font);
        if (message == WM_PAINT)
            EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
HWND control(HWND owner, const wchar_t *cls, const wchar_t *text, DWORD style, int id, int x, int y, int w,
             int h, UINT dpi, HFONT font) {
    HWND c = CreateWindowExW(wcscmp(cls, L"EDIT") == 0 ? WS_EX_CLIENTEDGE : 0, cls, text,
                             WS_CHILD | WS_VISIBLE | style, scaled(x, dpi), scaled(y, dpi), scaled(w, dpi),
                             scaled(h, dpi), owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                             instance, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return c;
}
HFONT make_font(int size, int weight, UINT dpi) {
    return CreateFontW(-scaled(size, dpi), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                       L"Segoe UI");
}
void build_main() {
    for (HWND c : app.controls)
        DestroyWindow(c);
    app.controls.clear();
    if (app.font)
        DeleteObject(app.font);
    if (app.title_font)
        DeleteObject(app.title_font);
    if (app.number_font)
        DeleteObject(app.number_font);
    app.dpi = window_dpi(app.window);
    app.font = make_font(14, FW_NORMAL, app.dpi);
    app.title_font = make_font(26, FW_SEMIBOLD, app.dpi);
    app.number_font = make_font(23, FW_SEMIBOLD, app.dpi);
    auto add = [&](const wchar_t *cls, const wchar_t *text, DWORD style, int id, int x, int y, int w, int h,
                   HFONT font = nullptr) {
        HWND c = control(app.window, cls, text, style, id, x, y, w, h, app.dpi, font ? font : app.font);
        app.controls.push_back(c);
        return c;
    };
    add(L"STATIC", L"AI Mon", 0, 0, 24, 20, 220, 36, app.title_font);
    add(L"STATIC", tr("quota.heading"), 0, 0, 24, 60, 528, 22);
    for (int i = 0; i < 2; ++i) {
        int y = 88 + i * 230;
        add(L"BUTTON", i ? L"Codex" : L"Claude Code", BS_GROUPBOX, 0, 24, y, 528, 216);
        for (int slot = 0; slot < 2; ++slot) {
            HWND chart = CreateWindowExW(
                0, ChartClass, L"", WS_CHILD | WS_VISIBLE, scaled(42 + slot * 252, app.dpi),
                scaled(y + 24, app.dpi), scaled(240, app.dpi), scaled(146, app.dpi), app.window, nullptr,
                instance, reinterpret_cast<void *>(static_cast<INT_PTR>(i * 2 + slot)));
            app.chart[i][slot] = chart;
            app.controls.push_back(chart);
        }
        app.cache[i] = add(L"STATIC", L"", SS_ENDELLIPSIS, 0, 42, y + 175, 490, 18);
        app.status[i] = add(L"STATIC", tr("status.collecting"), SS_ENDELLIPSIS, 0, 42, y + 195, 490, 18);
    }
    app.note = add(L"STATIC", tr("quota.note"), 0, 0, 24, 550, 528, 42);
    add(L"BUTTON", tr("action.refresh"), WS_TABSTOP | BS_PUSHBUTTON, Refresh, 24, 606, 132, 34);
    add(L"BUTTON", tr("action.settings"), WS_TABSTOP | BS_PUSHBUTTON, Configure, 166, 606, 96, 34);
    add(L"BUTTON", tr("mini.action"), WS_TABSTOP | BS_PUSHBUTTON, Mini, 274, 606, 170, 34);
    add(L"BUTTON", tr("action.about"), WS_TABSTOP | BS_PUSHBUTTON, About, 456, 606, 96, 34);
}
void render() {
    Snapshot snapshot;
    ClaudeProbe probe;
    bool failed;
    {
        Lock lock;
        snapshot = app.snapshot;
        probe = app.claude_probe;
        failed = app.worker_failed;
    }
    mini.update(snapshot);
    for (int i = 0; i < 2; ++i) {
        const auto &row = snapshot.providers[i];
        auto local = locale.format("quota.local_tokens", {{L"input", grouped(row.total.tokens.input)},
                                                          {L"output", grouped(row.total.tokens.output)}});
        SetWindowTextW(app.cache[i], local.c_str());
        std::wstring status;
        if (row.status == Status::Disabled)
            status = tr("status.disabled");
        else if (row.quota.latest.observed) {
            uint64_t age =
                now_seconds() > row.quota.latest.observed ? now_seconds() - row.quota.latest.observed : 0;
            status = locale.format("quota.updated", {{L"minutes", std::to_wstring(age / 60)}});
            if (age > 900)
                status += L" · " + std::wstring(tr("quota.stale"));
            if (row.status == Status::ReadError)
                status += L" · " + std::wstring(tr("status.read_error"));
        } else
            status = tr(i ? "quota.wait_codex" : "quota.claude_waiting");
        if (!i && row.status != Status::Disabled && probe != ClaudeProbe::Ready) {
            const char *key = probe == ClaudeProbe::Missing       ? "quota.claude_missing"
                              : probe == ClaudeProbe::Failed      ? "quota.claude_failed"
                              : probe == ClaudeProbe::Unavailable ? "quota.claude_unavailable"
                                                                  : nullptr;
            if (key)
                status = tr(key);
        }
        SetWindowTextW(app.status[i], status.c_str());
        for (HWND chart : app.chart[i])
            InvalidateRect(chart, nullptr, FALSE);
    }
    const wchar_t *note = tr("quota.note");
    if (failed)
        note = tr("note.worker_error");
    else if (snapshot.cache_error)
        note = tr("note.cache_error");
    else if (app.settings_warning)
        note = tr("note.settings_error");
    else if (locale.warning())
        note = tr("note.language_error");
    SetWindowTextW(app.note, note);
    notify_icon(NIM_MODIFY);
}
DWORD WINAPI worker_main(void *) {
    try {
        Collector collector(app.data_dir);
        Settings initial;
        {
            Lock lock;
            initial = app.settings;
        }
        collector.configure(initial);
        collector.load();
        Watch watches[2];
        uint64_t last_probe = 0;
        bool requested_probe = true;
        bool immediate = true;
        while (WaitForSingleObject(app.stop.value, 0) != WAIT_OBJECT_0) {
            Settings settings;
            {
                Lock lock;
                settings = app.settings;
            }
            collector.configure(settings);
            for (int i = 0; i < 2; ++i) {
                if (!settings.enabled[i]) {
                    watches[i].close();
                    continue;
                }
                if (watches[i].path != settings.roots[i] || !watches[i].folder.valid())
                    watches[i].start(settings.roots[i]);
            }
            if (immediate) {
                auto tick = GetTickCount64();
                if (app.probe_enabled && settings.enabled[0] &&
                    (!last_probe || tick - last_probe >= (requested_probe ? 15000ULL : 120000ULL))) {
                    auto result = probe_claude(app.data_dir, app.stop.value);
                    last_probe = GetTickCount64();
                    requested_probe = false;
                    Lock lock;
                    app.claude_probe = result;
                }
                auto snapshot = collector.scan(app.stop.value);
                {
                    Lock lock;
                    app.snapshot = snapshot;
                }
                PostMessageW(app.window, UpdatedMessage, 0, 0);
            }
            DWORD delay = static_cast<DWORD>(settings.interval * 1000);
            {
                Lock lock;
                for (const auto &row : app.snapshot.providers)
                    if (row.status == Status::Collecting)
                        delay = 100;
            }
            HANDLE handles[4] = {app.stop.value, app.wake.value, nullptr, nullptr};
            int index[4] = {-1, -1, -1, -1};
            DWORD count = 2;
            for (int i = 0; i < 2; ++i)
                if (watches[i].event.valid()) {
                    handles[count] = watches[i].event.value;
                    index[count] = i;
                    ++count;
                }
            DWORD result = WaitForMultipleObjects(count, handles, FALSE, delay);
            if (result == WAIT_OBJECT_0 + 1)
                requested_probe = true;
            if (result == WAIT_OBJECT_0)
                break;
            if (result >= WAIT_OBJECT_0 + 2 && result < WAIT_OBJECT_0 + count) {
                int i = index[result - WAIT_OBJECT_0];
                DWORD ignored = 0;
                GetOverlappedResult(watches[i].folder.value, &watches[i].overlapped, &ignored, FALSE);
                if (!watches[i].arm())
                    watches[i].close();
                HANDLE wait[2] = {app.stop.value, app.wake.value};
                if (WaitForMultipleObjects(2, wait, FALSE, 1000) == WAIT_OBJECT_0)
                    break;
            }
            if (result == WAIT_FAILED)
                break;
            immediate = true;
        }
    } catch (...) {
        Lock lock;
        app.worker_failed = true;
        PostMessageW(app.window, UpdatedMessage, 0, 0);
    }
    return 0;
}
void about(HWND owner) {
    HRSRC r = FindResourceW(instance, MAKEINTRESOURCEW(102), RT_RCDATA);
    std::wstring license;
    if (r) {
        HGLOBAL h = LoadResource(instance, r);
        if (h) {
            const char *text = static_cast<const char *>(LockResource(h));
            if (text)
                license = wide(std::string(text, SizeofResource(instance, r)));
        }
    }
    std::wstring message = locale.format("about.body", {{L"version", L"0.3.4"}}) + license;
    MessageBoxW(owner, message.c_str(), tr("about.title"), MB_OK | MB_ICONINFORMATION);
}
struct SettingsUI {
    HFONT font = nullptr;
    UINT dpi = 96;
    HWND enabled[2]{}, path[2]{}, interval = nullptr, show = nullptr, language = nullptr;
    HWND startup = nullptr, transparency = nullptr, transparency_label = nullptr;
    std::wstring startup_before;
    bool startup_selected = false;
    std::vector<std::string> language_ids;
    std::vector<HWND> controls;
    Settings value;
} settings_ui;
void preview_transparency() {
    int transparency = static_cast<int>(SendMessageW(settings_ui.transparency, TBM_GETPOS, 0, 0));
    auto label = std::to_wstring(transparency) + L"%";
    SetWindowTextW(settings_ui.transparency_label, label.c_str());
    mini.opacity(100 - transparency);
}
std::wstring text_of(HWND c) {
    int n = GetWindowTextLengthW(c);
    std::wstring result(n + 1, 0);
    GetWindowTextW(c, result.data(), n + 1);
    result.resize(n);
    return result;
}
void build_settings(HWND window) {
    for (HWND c : settings_ui.controls)
        DestroyWindow(c);
    settings_ui.controls.clear();
    if (settings_ui.font)
        DeleteObject(settings_ui.font);
    settings_ui.dpi = window_dpi(window);
    settings_ui.font = make_font(14, FW_NORMAL, settings_ui.dpi);
    auto add = [&](const wchar_t *cls, const wchar_t *text, DWORD style, int id, int x, int y, int w, int h) {
        HWND c = control(window, cls, text, style, id, x, y, w, h, settings_ui.dpi, settings_ui.font);
        settings_ui.controls.push_back(c);
        return c;
    };
    for (int i = 0; i < 2; ++i) {
        int y = 20 + i * 102;
        settings_ui.enabled[i] =
            add(L"BUTTON", i == 0 ? tr("settings.enable_claude") : tr("settings.enable_codex"),
                WS_TABSTOP | BS_AUTOCHECKBOX, 210 + i, 20, y, 220, 26);
        SendMessageW(settings_ui.enabled[i], BM_SETCHECK,
                     settings_ui.value.enabled[i] ? BST_CHECKED : BST_UNCHECKED, 0);
        add(L"STATIC", tr("settings.log_folder"), 0, 0, 20, y + 32, 95, 24);
        settings_ui.path[i] = add(L"EDIT", settings_ui.value.roots[i].c_str(), WS_TABSTOP | ES_AUTOHSCROLL,
                                  220 + i, 115, y + 30, 440, 28);
        SendMessageW(settings_ui.path[i], EM_SETLIMITTEXT, 32000, 0);
    }
    add(L"STATIC", tr("settings.interval"), 0, 0, 20, 230, 290, 26);
    settings_ui.interval = add(L"EDIT", std::to_wstring(settings_ui.value.interval).c_str(),
                               WS_TABSTOP | ES_NUMBER, 230, 320, 228, 80, 28);
    SendMessageW(settings_ui.interval, EM_SETLIMITTEXT, 3, 0);
    settings_ui.show =
        add(L"BUTTON", tr("settings.show_start"), WS_TABSTOP | BS_AUTOCHECKBOX, 231, 20, 274, 350, 28);
    SendMessageW(settings_ui.show, BM_SETCHECK, settings_ui.value.show_start ? BST_CHECKED : BST_UNCHECKED,
                 0);
    add(L"STATIC", tr("settings.language"), 0, 0, 20, 318, 120, 24);
    settings_ui.language =
        add(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 232, 150, 314, 405, 220);
    settings_ui.language_ids = {"auto"};
    SendMessageW(settings_ui.language, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(tr("settings.auto_language")));
    for (const auto &pack : locale.languages()) {
        settings_ui.language_ids.push_back(pack.id);
        SendMessageW(settings_ui.language, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(pack.name.c_str()));
    }
    auto selected = std::find(settings_ui.language_ids.begin(), settings_ui.language_ids.end(),
                              settings_ui.value.language);
    if (selected == settings_ui.language_ids.end()) {
        settings_ui.language_ids.push_back(settings_ui.value.language);
        auto label =
            locale.format("settings.unavailable_language", {{L"language", wide(settings_ui.value.language)}});
        SendMessageW(settings_ui.language, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        selected = settings_ui.language_ids.end() - 1;
    }
    SendMessageW(settings_ui.language, CB_SETCURSEL, selected - settings_ui.language_ids.begin(), 0);
    add(L"STATIC", tr("settings.language_hint"), 0, 0, 20, 356, 535, 40);
    add(L"BUTTON", tr("quota.connect_button"), WS_TABSTOP | BS_PUSHBUTTON, 250, 20, 402, 260, 30);
    add(L"BUTTON", tr("quota.disconnect_button"), WS_TABSTOP | BS_PUSHBUTTON, 251, 290, 402, 265, 30);
    settings_ui.startup =
        add(L"BUTTON", tr("settings.autostart"), WS_TABSTOP | BS_AUTOCHECKBOX, 233, 20, 444, 535, 28);
    SendMessageW(settings_ui.startup, BM_SETCHECK, settings_ui.startup_selected ? BST_CHECKED : BST_UNCHECKED,
                 0);
    EnableWindow(settings_ui.startup, app.startup_controls);
    add(L"STATIC", tr("settings.transparency"), 0, 0, 20, 488, 180, 24);
    settings_ui.transparency =
        add(TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS, 234, 200, 478, 285, 38);
    SendMessageW(settings_ui.transparency, TBM_SETRANGE, TRUE, MAKELPARAM(0, 70));
    SendMessageW(settings_ui.transparency, TBM_SETTICFREQ, 10, 0);
    SendMessageW(settings_ui.transparency, TBM_SETPOS, TRUE, 100 - settings_ui.value.mini_opacity);
    settings_ui.transparency_label = add(L"STATIC", L"", 0, 235, 495, 486, 60, 24);
    preview_transparency();
    add(L"STATIC", tr("settings.transparency_hint"), 0, 0, 20, 520, 535, 30);
    add(L"BUTTON", tr("action.default_paths"), WS_TABSTOP | BS_PUSHBUTTON, 240, 20, 566, 110, 34);
    add(L"BUTTON", tr("action.save"), WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK, 345, 566, 100, 34);
    add(L"BUTTON", tr("action.cancel"), WS_TABSTOP | BS_PUSHBUTTON, IDCANCEL, 455, 566, 100, 34);
}
bool read_settings_ui(bool validate) {
    Settings s = settings_ui.value;
    for (int i = 0; i < 2; ++i) {
        s.enabled[i] = SendMessageW(settings_ui.enabled[i], BM_GETCHECK, 0, 0) == BST_CHECKED;
        s.roots[i] = text_of(settings_ui.path[i]);
        if (validate && s.roots[i].empty())
            return false;
        s.roots[i] = canonical(s.roots[i]);
    }
    std::wstring interval = text_of(settings_ui.interval);
    wchar_t *end = nullptr;
    long v = wcstol(interval.c_str(), &end, 10);
    if (validate && (interval.empty() || *end || v < 2 || v > 300))
        return false;
    s.interval = static_cast<int>(v);
    s.show_start = SendMessageW(settings_ui.show, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.mini_opacity = 100 - static_cast<int>(SendMessageW(settings_ui.transparency, TBM_GETPOS, 0, 0));
    settings_ui.startup_selected = SendMessageW(settings_ui.startup, BM_GETCHECK, 0, 0) == BST_CHECKED;
    LRESULT selected = SendMessageW(settings_ui.language, CB_GETCURSEL, 0, 0);
    if (selected < 0 || static_cast<size_t>(selected) >= settings_ui.language_ids.size())
        return false;
    s.language = settings_ui.language_ids[static_cast<size_t>(selected)];
    settings_ui.value = s;
    return true;
}
LRESULT CALLBACK settings_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
    case WM_CREATE: {
        {
            Lock lock;
            settings_ui.value = app.settings;
        }
        settings_ui.startup_before = app.startup_controls ? startup_value() : L"";
        settings_ui.startup_selected =
            settings_ui.startup_before == startup_command(app.executable, app.data_dir);
        build_settings(window);
        return 0;
    }
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(l) == settings_ui.transparency)
            preview_transparency();
        return 0;
    case WM_DPICHANGED: {
        read_settings_ui(false);
        auto rect = reinterpret_cast<RECT *>(l);
        SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left,
                     rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        build_settings(window);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == 250 || LOWORD(w) == 251) {
            wchar_t executable[32768]{};
            GetModuleFileNameW(nullptr, executable, 32768);
            auto result = LOWORD(w) == 250 ? connect_claude(claude_settings_path(), app.data_dir, executable)
                                           : disconnect_claude(claude_settings_path(), app.data_dir);
            const char *key = result == BridgeResult::Done
                                  ? (LOWORD(w) == 250 ? "quota.connected" : "quota.disconnected")
                              : result == BridgeResult::AlreadyConnected ? "quota.connected"
                              : result == BridgeResult::Changed          ? "quota.bridge_changed"
                                                                         : "quota.bridge_failed";
            MessageBoxW(window, tr(key), tr("settings.title"),
                        MB_OK | (result == BridgeResult::Failed || result == BridgeResult::Changed
                                     ? MB_ICONWARNING
                                     : MB_ICONINFORMATION));
            SetEvent(app.wake.value);
            return 0;
        }
        if (LOWORD(w) == 240) {
            auto s = default_settings();
            for (int i = 0; i < 2; ++i)
                SetWindowTextW(settings_ui.path[i], s.roots[i].c_str());
            return 0;
        }
        if (LOWORD(w) == IDOK) {
            if (!read_settings_ui(true)) {
                MessageBoxW(window, tr("settings.invalid_text"), tr("settings.invalid_title"),
                            MB_OK | MB_ICONWARNING);
                return 0;
            }
            if (!save_settings(join(app.data_dir, L"settings.json"), settings_ui.value)) {
                MessageBoxW(window, tr("settings.save_error_text"), tr("settings.save_error_title"),
                            MB_OK | MB_ICONERROR);
                return 0;
            }
            auto command = startup_command(app.executable, app.data_dir);
            bool requested = SendMessageW(settings_ui.startup, BM_GETCHECK, 0, 0) == BST_CHECKED;
            bool changed = requested != (settings_ui.startup_before == command);
            if (app.startup_controls && changed &&
                (startup_value() != settings_ui.startup_before ||
                 !set_startup_value(requested ? command : L""))) {
                save_settings(join(app.data_dir, L"settings.json"), app.settings);
                MessageBoxW(window, tr("settings.startup_error"), tr("settings.save_error_title"),
                            MB_OK | MB_ICONWARNING);
                return 0;
            }
            {
                Lock lock;
                app.settings = settings_ui.value;
            }
            locale.select(settings_ui.value.language);
            app.settings_warning = false;
            SetEvent(app.wake.value);
            DestroyWindow(window);
            build_main();
            render();
            notify_icon(NIM_MODIFY);
            return 0;
        }
        if (LOWORD(w) == IDCANCEL) {
            DestroyWindow(window);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        mini.opacity(app.settings.mini_opacity);
        if (settings_ui.font)
            DeleteObject(settings_ui.font);
        settings_ui.font = nullptr;
        settings_ui.controls.clear();
        app.settings_window = nullptr;
        EnableWindow(app.window, TRUE);
        SetForegroundWindow(app.window);
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
void open_settings() {
    if (app.settings_window) {
        SetForegroundWindow(app.settings_window);
        return;
    }
    locale.discover();
    show_window();
    UINT dpi = app.dpi;
    RECT r{0, 0, scaled(575, dpi), scaled(620, dpi)};
    adjust_rect(r, WS_CAPTION | WS_SYSMENU, WS_EX_DLGMODALFRAME, dpi);
    RECT parent{};
    GetWindowRect(app.window, &parent);
    app.settings_window =
        CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, SettingsClass, tr("settings.title"),
                        WS_CAPTION | WS_SYSMENU, parent.left + 20, parent.top + 30, r.right - r.left,
                        r.bottom - r.top, app.window, nullptr, instance, nullptr);
    if (app.settings_window) {
        EnableWindow(app.window, FALSE);
        ShowWindow(app.settings_window, SW_SHOW);
        SetFocus(settings_ui.enabled[0]);
    }
}
LRESULT CALLBACK main_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (app.taskbar_created && message == app.taskbar_created) {
        notify_icon(NIM_ADD);
        return 0;
    }
    switch (message) {
    case WM_CREATE:
        app.window = window;
        build_main();
        return 0;
    case WM_DPICHANGED: {
        auto rect = reinterpret_cast<RECT *>(l);
        SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left,
                     rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        build_main();
        render();
        return 0;
    }
    case WM_SETTINGCHANGE:
        if (app.settings.language == "auto") {
            locale.select("auto");
            build_main();
            render();
            notify_icon(NIM_MODIFY);
        }
        [[fallthrough]];
    case WM_TIMECHANGE:
        SetEvent(app.wake.value);
        return 0;
    case UpdatedMessage:
        render();
        return 0;
    case WM_TIMER:
        render();
        return 0;
    case TrayMessage: {
        UINT event = LOWORD(l);
        if (event == NIN_SELECT || event == NIN_KEYSELECT) {
            if (app.settings_window)
                SetForegroundWindow(app.settings_window);
            else
                show_window();
        } else if (event == WM_CONTEXTMENU) {
            POINT point{};
            GetCursorPos(&point);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, Open, tr("action.open"));
            AppendMenuW(menu, MF_STRING | (mini.visible() ? MF_CHECKED : 0), Mini, tr("mini.action"));
            AppendMenuW(menu, MF_STRING, Refresh, tr("action.refresh"));
            AppendMenuW(menu, MF_STRING, Configure, tr("action.settings"));
            AppendMenuW(menu, MF_STRING, About, tr("action.about"));
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, Exit, tr("action.exit"));
            SetForegroundWindow(window);
            int selected =
                TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window, nullptr);
            DestroyMenu(menu);
            PostMessageW(window, WM_NULL, 0, 0);
            if (selected)
                PostMessageW(window, WM_COMMAND, selected, 0);
        }
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case Open:
            show_window();
            break;
        case Mini:
            mini.toggle();
            break;
        case Refresh:
            SetEvent(app.wake.value);
            break;
        case Configure:
            open_settings();
            break;
        case About:
            about(window);
            break;
        case Exit:
            DestroyWindow(window);
            break;
        case IDCANCEL:
            ShowWindow(window, SW_HIDE);
            break;
        }
        return 0;
    case WM_CLOSE:
        if (app.settings_window)
            SetForegroundWindow(app.settings_window);
        else
            ShowWindow(window, SW_HIDE);
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (w)
            DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        mini.shutdown();
        notify_icon(NIM_DELETE);
        KillTimer(window, 1);
        SetEvent(app.stop.value);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
int headless(int argc, wchar_t **argv) {
    Settings s = default_settings();
    std::wstring destination, data;
    for (int i = 2; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (i + 1 < argc && arg == L"--claude")
            s.roots[0] = canonical(argv[++i]);
        else if (i + 1 < argc && arg == L"--codex")
            s.roots[1] = canonical(argv[++i]);
        else if (i + 1 < argc && arg == L"--data-dir")
            data = canonical(argv[++i]);
        else if (i + 1 < argc && arg == L"--output")
            destination = canonical(argv[++i]);
        else
            return 2;
    }
    if (data.empty() || destination.empty() || !ensure_directory(data))
        return 2;
    std::wstring lock_name = L"Local\\AI.Mon." + data_key(data);
    Handle data_lock(CreateMutexW(nullptr, FALSE, lock_name.c_str()));
    DWORD lock_error = GetLastError();
    if (!data_lock.valid() || lock_error == ERROR_ALREADY_EXISTS)
        return 4;
    Collector collector(data);
    collector.configure(s);
    collector.load();
    Snapshot snapshot;
    for (int attempt = 0; attempt < 2000; ++attempt) {
        snapshot = collector.scan();
        if (snapshot.providers[0].status != Status::Collecting &&
            snapshot.providers[1].status != Status::Collecting)
            break;
    }
    auto doc = json(cJSON_CreateObject());
    auto rows = cJSON_AddArrayToObject(doc.get(), "providers");
    for (int i = 0; i < 2; ++i) {
        auto p = token_json(snapshot.providers[i].total.tokens);
        put(p, "provider", std::string(i == 0 ? "claude" : "codex"));
        put(p, "events", snapshot.providers[i].total.events);
        put(p, "status", std::string(status_code(snapshot.providers[i].status)));
        cJSON_AddItemToObject(p, "quota", quota_json(snapshot.providers[i].quota.latest));
        cJSON_AddItemToArray(rows, p);
    }
    put(doc.get(), "bytes_read", collector.bytes_read());
    put(doc.get(), "cache_error", snapshot.cache_error);
    return atomic_write(destination, dump(doc.get())) ? 0 : 3;
}
int run(HINSTANCE module) {
    instance = module;
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    app.executable = executable;
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1 && wcscmp(argv[1], L"--scan") == 0) {
        int result = headless(argc, argv);
        LocalFree(argv);
        return result;
    }
    std::wstring custom_data;
    bool smoke = false, bridge_mode = false, probe_mode = false, startup_mode = false, remove_startup = false;
    std::string language_override;
    for (int i = 1; argv && i < argc; ++i) {
        if (wcscmp(argv[i], L"--data-dir") == 0 && i + 1 < argc)
            custom_data = canonical(argv[++i]);
        else if (wcscmp(argv[i], L"--language") == 0 && i + 1 < argc)
            language_override = utf8(argv[++i]);
        else if (wcscmp(argv[i], L"--claude-statusline") == 0)
            bridge_mode = true;
        else if (wcscmp(argv[i], L"--claude-probe") == 0)
            probe_mode = true;
        else if (wcscmp(argv[i], L"--no-claude-probe") == 0)
            app.probe_enabled = false;
        else if (wcscmp(argv[i], L"--no-startup-registration") == 0)
            app.startup_controls = false;
        else if (wcscmp(argv[i], L"--startup") == 0)
            startup_mode = true;
        else if (wcscmp(argv[i], L"--remove-startup") == 0)
            remove_startup = true;
        else if (wcscmp(argv[i], L"--smoke-test") == 0)
            smoke = true;
    }
    if (argv)
        LocalFree(argv);
    app.data_dir = custom_data.empty() ? join(env(L"LOCALAPPDATA"), L"AI Mon") : custom_data;
    if (remove_startup)
        return remove_startup_for(app.executable) ? 0 : 1;
    if (bridge_mode)
        return claude_statusline(app.data_dir);
    if (probe_mode)
        return probe_claude(app.data_dir) == ClaudeProbe::Ready ? 0 : 4;
    if (smoke) {
        app.probe_enabled = false;
        app.startup_controls = false;
    }
    if (!locale.initialize(instance, app.data_dir))
        return 1;
    if (app.data_dir.empty() || !ensure_directory(app.data_dir)) {
        MessageBoxW(nullptr, tr("error.data_folder"), L"AI Mon", MB_OK | MB_ICONERROR);
        return 1;
    }
    std::wstring identity = data_key(app.data_dir);
    std::wstring mutex_name = L"Local\\AI.Mon." + identity;
    main_class_name = std::wstring(MainClass) + L"." + identity;
    app.mutex.reset(CreateMutexW(nullptr, FALSE, mutex_name.c_str()));
    DWORD mutex_error = GetLastError();
    if (!app.mutex.valid())
        return 1;
    if (mutex_error == ERROR_ALREADY_EXISTS) {
        HWND prior = FindWindowW(main_class_name.c_str(), nullptr);
        if (prior && !startup_mode) {
            ShowWindow(prior, SW_RESTORE);
            SetForegroundWindow(prior);
        }
        return 0;
    }
    app.settings = default_settings();
    std::wstring settings_path = join(app.data_dir, L"settings.json");
    if (GetFileAttributesW(settings_path.c_str()) != INVALID_FILE_ATTRIBUTES)
        app.settings_warning = !load_settings(settings_path, app.settings);
    else
        save_settings(settings_path, app.settings);
    if (!language_override.empty()) {
        if (!valid_language_id(language_override))
            return 2;
        app.settings.language = language_override;
    }
    locale.select(app.settings.language);
    if (smoke) {
        app.settings.enabled = {{false, false}};
        app.settings.show_start = false;
    }
    app.stop.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    app.wake.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!app.stop.valid() || !app.wake.valid())
        return 1;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    wc.hIconSm = wc.hIcon;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpfnWndProc = main_proc;
    wc.lpszClassName = main_class_name.c_str();
    if (!RegisterClassExW(&wc))
        return 1;
    wc.lpfnWndProc = chart_proc;
    wc.lpszClassName = ChartClass;
    if (!RegisterClassExW(&wc))
        return 1;
    wc.lpfnWndProc = settings_proc;
    wc.lpszClassName = SettingsClass;
    if (!RegisterClassExW(&wc))
        return 1;
    app.taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    HDC dc = GetDC(nullptr);
    UINT dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc)
        ReleaseDC(nullptr, dc);
    RECT rect{0, 0, scaled(576, dpi), scaled(660, dpi)};
    adjust_rect(rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, WS_EX_CONTROLPARENT, dpi);
    HWND window = CreateWindowExW(WS_EX_CONTROLPARENT, main_class_name.c_str(), L"AI Mon",
                                  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
                                  CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr,
                                  nullptr, instance, nullptr);
    if (!window)
        return 1;
    if (!mini.initialize(instance, window, app.data_dir, locale)) {
        DestroyWindow(window);
        return 1;
    }
    mini.opacity(app.settings.mini_opacity);
    if (!smoke)
        notify_icon(NIM_ADD);
    SetTimer(window, 1, 1000, nullptr);
    app.worker.reset(CreateThread(nullptr, 0, worker_main, nullptr, 0, nullptr));
    if (!app.worker.valid()) {
        DestroyWindow(window);
        return 1;
    }
    if (app.settings.show_start && !smoke && !startup_mode)
        ShowWindow(window, SW_SHOW);
    if (smoke)
        SetTimer(window, 2, 1500, nullptr);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (smoke && msg.message == WM_TIMER && msg.wParam == 2) {
            bool valid = app.controls.size() == 17 && IsWindow(app.chart[0][0]) &&
                         text_of(app.status[0]) == tr("status.disabled");
            const std::string initial_language = app.settings.language;
            for (const std::string &language : {std::string("en"), std::string("ko"), initial_language}) {
                // Restore the initial preference before the last dialog, including
                // an unavailable pack that cannot be newly selected from the list.
                if (language == initial_language) {
                    Lock lock;
                    app.settings.language = initial_language;
                }
                HWND settings = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, SettingsClass,
                                                tr("settings.title"), WS_CAPTION | WS_SYSMENU, 0, 0, 600, 560,
                                                window, nullptr, instance, nullptr);
                valid = valid && settings && read_settings_ui(true);
                if (settings) {
                    auto chosen =
                        std::find(settings_ui.language_ids.begin(), settings_ui.language_ids.end(), language);
                    SendMessageW(settings_ui.language, CB_SETCURSEL,
                                 chosen - settings_ui.language_ids.begin(), 0);
                    SendMessageW(settings, WM_COMMAND, IDOK, 0);
                    Settings persisted;
                    valid = valid && !IsWindow(settings) && app.settings.language == language &&
                            load_settings(settings_path, persisted) && persisted.language == language &&
                            text_of(GetDlgItem(window, Refresh)) == tr("action.refresh") &&
                            text_of(app.status[0]) == tr("status.disabled");
                    if (language == "en")
                        valid = valid && text_of(app.status[0]) == L"Disabled";
                    if (language == "ko")
                        valid = valid && text_of(app.status[0]) == L"사용 안 함";
                }
            }
            if (!valid) {
                Lock lock;
                app.worker_failed = true;
            }
            DestroyWindow(window);
            continue;
        }
        if (app.settings_window && IsDialogMessageW(app.settings_window, &msg))
            continue;
        if (IsWindowVisible(window) && IsDialogMessageW(window, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    SetEvent(app.stop.value);
    WaitForSingleObject(app.worker.value, INFINITE);
    return app.worker_failed ? 1 : 0;
}
} // namespace
int WINAPI wWinMain(HINSTANCE module, HINSTANCE, PWSTR, int) {
    try {
        return run(module);
    } catch (const std::exception &) {
        MessageBoxW(nullptr, tr("error.startup"), L"AI Mon", MB_OK | MB_ICONERROR);
        return 1;
    }
}
