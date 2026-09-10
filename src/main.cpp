#include "collector.hpp"
#include "localization.hpp"
#include <shellapi.h>
#include <commctrl.h>
#include <algorithm>
#include <cstdio>
#include <exception>

using namespace aimon;
namespace {
constexpr wchar_t MainClass[] = L"AI.Mon.Window.1";
constexpr wchar_t SettingsClass[] = L"AI.Mon.Settings.1";
std::wstring main_class_name = MainClass;
constexpr UINT TrayMessage = WM_APP + 1, UpdatedMessage = WM_APP + 2;
constexpr int Open = 100, Refresh = 101, Configure = 102, About = 103, Exit = 104;
HINSTANCE instance = nullptr;
Localizer locale;
const wchar_t *tr(const char *key) {
    return locale.text(key);
}
struct App {
    HWND window = nullptr, settings_window = nullptr;
    Handle stop, wake, worker, mutex;
    CRITICAL_SECTION lock{};
    Settings settings;
    Snapshot snapshot;
    std::wstring data_dir;
    bool worker_failed = false, settings_warning = false;
    UINT dpi = 96, taskbar_created = 0;
    HFONT font = nullptr, title_font = nullptr, number_font = nullptr;
    std::vector<HWND> controls;
    HWND status[2]{}, input[2]{}, output[2]{}, cache[2]{}, note = nullptr;
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
void notify_icon(DWORD operation) {
    NOTIFYICONDATAW n{};
    n.cbSize = sizeof(n);
    n.hWnd = app.window;
    n.uID = 1;
    n.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    n.uCallbackMessage = TrayMessage;
    n.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    wcsncpy_s(n.szTip, tr("tray.tip"), _TRUNCATE);
    if (!Shell_NotifyIconW(operation, &n) && operation == NIM_ADD)
        ShowWindow(app.window, SW_SHOW);
    if (operation == NIM_ADD) {
        n.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &n);
    }
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
    add(L"STATIC", tr("summary.today"), 0, 0, 24, 62, 490, 22);
    for (int i = 0; i < 2; ++i) {
        int y = 100 + i * 162;
        add(L"BUTTON", i == 0 ? L"Claude Code" : L"Codex", BS_GROUPBOX, 0, 24, y, 528, 148);
        add(L"STATIC", tr("summary.input"), 0, 0, 42, y + 28, 170, 20);
        add(L"STATIC", tr("summary.output"), 0, 0, 294, y + 28, 170, 20);
        app.input[i] = add(L"STATIC", L"—", SS_ENDELLIPSIS, 0, 42, y + 51, 240, 30, app.number_font);
        app.output[i] = add(L"STATIC", L"—", SS_ENDELLIPSIS, 0, 294, y + 51, 240, 30, app.number_font);
        app.cache[i] =
            add(L"STATIC", locale.format("summary.cache", {{L"read", L"—"}, {L"write", L"—"}}).c_str(),
                SS_ENDELLIPSIS, 0, 42, y + 86, 490, 20);
        app.status[i] = add(L"STATIC", tr("status.collecting"), SS_ENDELLIPSIS, 0, 42, y + 113, 490, 20);
    }
    app.note = add(L"STATIC", tr("note.scope"), 0, 0, 24, 430, 528, 42);
    add(L"BUTTON", tr("action.refresh"), WS_TABSTOP | BS_PUSHBUTTON, Refresh, 24, 486, 132, 34);
    add(L"BUTTON", tr("action.settings"), WS_TABSTOP | BS_PUSHBUTTON, Configure, 166, 486, 96, 34);
    add(L"BUTTON", tr("action.about"), WS_TABSTOP | BS_PUSHBUTTON, About, 456, 486, 96, 34);
}
void render() {
    Snapshot snapshot;
    bool failed;
    {
        Lock lock;
        snapshot = app.snapshot;
        failed = app.worker_failed;
    }
    for (int i = 0; i < 2; ++i) {
        const auto &row = snapshot.providers[i];
        bool available = row.total.events > 0 && row.status != Status::Disabled && !row.total.overflow;
        SetWindowTextW(app.input[i], available ? grouped(row.total.tokens.input).c_str() : L"—");
        SetWindowTextW(app.output[i], available ? grouped(row.total.tokens.output).c_str() : L"—");
        std::wstring cache =
            locale.format("summary.cache", {{L"read", available ? grouped(row.total.tokens.read) : L"—"},
                                            {L"write", available ? grouped(row.total.tokens.write) : L"—"}});
        SetWindowTextW(app.cache[i], cache.c_str());
        std::wstring status = tr((std::string("status.") + status_code(row.status)).c_str());
        if (row.success) {
            uint64_t age = now_seconds() > row.success ? now_seconds() - row.success : 0;
            status =
                locale.format("summary.updated", {{L"status", status}, {L"seconds", std::to_wstring(age)}});
        }
        SetWindowTextW(app.status[i], status.c_str());
    }
    const wchar_t *note = tr("note.scope");
    if (failed)
        note = tr("note.worker_error");
    else if (snapshot.cache_error)
        note = tr("note.cache_error");
    else if (app.settings_warning)
        note = tr("note.settings_error");
    else if (snapshot.limited)
        note = tr("note.limited");
    else if (snapshot.cache_rebuilt)
        note = tr("note.rebuilt");
    if (locale.warning() && !failed && !snapshot.cache_error && !app.settings_warning && !snapshot.limited)
        note = tr("note.language_error");
    SetWindowTextW(app.note, note);
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
    std::wstring message = locale.format("about.body", {{L"version", L"0.2.0"}}) + license;
    MessageBoxW(owner, message.c_str(), tr("about.title"), MB_OK | MB_ICONINFORMATION);
}
struct SettingsUI {
    HFONT font = nullptr;
    UINT dpi = 96;
    HWND enabled[2]{}, path[2]{}, interval = nullptr, show = nullptr, language = nullptr;
    std::vector<std::string> language_ids;
    std::vector<HWND> controls;
    Settings value;
} settings_ui;
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
    add(L"STATIC", tr("settings.folder_hint"), 0, 0, 20, 400, 535, 40);
    add(L"BUTTON", tr("action.default_paths"), WS_TABSTOP | BS_PUSHBUTTON, 240, 20, 458, 110, 34);
    add(L"BUTTON", tr("action.save"), WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK, 345, 458, 100, 34);
    add(L"BUTTON", tr("action.cancel"), WS_TABSTOP | BS_PUSHBUTTON, IDCANCEL, 455, 458, 100, 34);
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
        build_settings(window);
        return 0;
    }
    case WM_DPICHANGED: {
        read_settings_ui(false);
        auto rect = reinterpret_cast<RECT *>(l);
        SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left,
                     rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        build_settings(window);
        return 0;
    }
    case WM_COMMAND:
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
    RECT r{0, 0, scaled(575, dpi), scaled(516, dpi)};
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
        if (IsWindowVisible(window))
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
        cJSON_AddItemToArray(rows, p);
    }
    put(doc.get(), "bytes_read", collector.bytes_read());
    put(doc.get(), "cache_error", snapshot.cache_error);
    return atomic_write(destination, dump(doc.get())) ? 0 : 3;
}
int run(HINSTANCE module) {
    instance = module;
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1 && wcscmp(argv[1], L"--scan") == 0) {
        int result = headless(argc, argv);
        LocalFree(argv);
        return result;
    }
    std::wstring custom_data;
    bool smoke = false;
    std::string language_override;
    for (int i = 1; argv && i < argc; ++i) {
        if (wcscmp(argv[i], L"--data-dir") == 0 && i + 1 < argc)
            custom_data = canonical(argv[++i]);
        else if (wcscmp(argv[i], L"--language") == 0 && i + 1 < argc)
            language_override = utf8(argv[++i]);
        else if (wcscmp(argv[i], L"--smoke-test") == 0)
            smoke = true;
    }
    if (argv)
        LocalFree(argv);
    app.data_dir = custom_data.empty() ? join(env(L"LOCALAPPDATA"), L"AI Mon") : custom_data;
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
        if (prior) {
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
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
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
    wc.lpfnWndProc = settings_proc;
    wc.lpszClassName = SettingsClass;
    if (!RegisterClassExW(&wc))
        return 1;
    app.taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    HDC dc = GetDC(nullptr);
    UINT dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc)
        ReleaseDC(nullptr, dc);
    RECT rect{0, 0, scaled(576, dpi), scaled(544, dpi)};
    adjust_rect(rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, WS_EX_CONTROLPARENT, dpi);
    HWND window = CreateWindowExW(WS_EX_CONTROLPARENT, main_class_name.c_str(), L"AI Mon",
                                  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
                                  CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr,
                                  nullptr, instance, nullptr);
    if (!window)
        return 1;
    if (!smoke)
        notify_icon(NIM_ADD);
    SetTimer(window, 1, 1000, nullptr);
    app.worker.reset(CreateThread(nullptr, 0, worker_main, nullptr, 0, nullptr));
    if (!app.worker.valid()) {
        DestroyWindow(window);
        return 1;
    }
    if (app.settings.show_start && !smoke)
        ShowWindow(window, SW_SHOW);
    if (smoke)
        SetTimer(window, 2, 1500, nullptr);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (smoke && msg.message == WM_TIMER && msg.wParam == 2) {
            bool valid = app.controls.size() == 20 && text_of(app.input[0]) == L"—" &&
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
