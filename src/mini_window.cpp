#include "mini_window.hpp"
#include <algorithm>
#include <cmath>
#include <windowsx.h>
#include <commctrl.h>

namespace aimon {
namespace {
constexpr wchar_t Class[] = L"AI.Mon.MiniWindow.1";
constexpr int Width = 200, Height = 48;
constexpr UINT Pin = 1001, Dock = 1002, Details = 1003, Refresh = 1004, Hide = 1005, Configure = 1006;
constexpr DWORD Style = WS_POPUP;
UINT dpi_for(HWND window) {
    using Fn = UINT(WINAPI *)(HWND);
    auto fn = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    return fn ? fn(window) : 96;
}
bool coordinate(const cJSON *value, LONG &out) {
    if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble) || value->valuedouble < -200000 ||
        value->valuedouble > 200000 || std::floor(value->valuedouble) != value->valuedouble)
        return false;
    out = static_cast<LONG>(value->valuedouble);
    return true;
}
COLORREF color(int remaining, int slot) {
    return remaining <= 1000   ? RGB(255, 113, 125)
           : remaining <= 2500 ? RGB(255, 199, 105)
           : slot              ? RGB(181, 235, 117)
                               : RGB(91, 229, 211);
}
std::wstring percentage(const QuotaWindow &value, uint64_t now) {
    if (!quota_current(value, now))
        return L"—";
    return std::to_wstring(value.remaining / 100) +
           (value.remaining % 100 ? L"." + std::to_wstring(value.remaining % 100 / 10) : L"") + L"%";
}
} // namespace
bool MiniWindow::initialize(HINSTANCE instance, HWND main, const std::wstring &data,
                            const Localizer &locale) {
    main_ = main;
    locale_ = &locale;
    path_ = join(data, L"mini-window.json");
    bool show = false;
    std::string text;
    if (read_text(path_, text, 4096)) {
        auto doc = parse(text);
        uint64_t version = 0;
        if (number(field(doc.get(), "version"), version) && version == 1) {
            show = cJSON_IsTrue(field(doc.get(), "visible"));
            topmost_ = !cJSON_IsFalse(field(doc.get(), "topmost"));
            positioned_ =
                coordinate(field(doc.get(), "left"), left_) && coordinate(field(doc.get(), "top"), top_);
        }
    }
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.hInstance = instance;
    cls.lpfnWndProc = procedure;
    cls.lpszClassName = Class;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    cls.hIconSm = cls.hIcon;
    if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;
    // An independent tool window stays visible when the detailed window is minimized.
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED | (topmost_ ? WS_EX_TOPMOST : 0), Class,
                              locale.text("mini.title"), Style, 0, 0, Width, Height, nullptr, nullptr,
                              instance, this);
    if (!window_)
        return false;
    dpi_ = dpi_for(window_);
    fonts();
    place(!positioned_);
    opacity(100);
    tooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                               WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
                               CW_USEDEFAULT, CW_USEDEFAULT, window_, nullptr, instance, nullptr);
    if (tooltip_) {
        tip_ = locale.text("mini.title");
        TOOLINFOW tool{};
        tool.cbSize = sizeof(tool);
        tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        tool.hwnd = window_;
        tool.uId = reinterpret_cast<UINT_PTR>(window_);
        tool.lpszText = tip_.data();
        SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        SendMessageW(tooltip_, TTM_SETMAXTIPWIDTH, 0, 420);
    }
    if (show)
        ShowWindow(window_, SW_SHOWNOACTIVATE);
    return true;
}
void MiniWindow::fonts() {
    if (font_)
        DeleteObject(font_);
    if (bold_)
        DeleteObject(bold_);
    auto make = [&](int weight) {
        return CreateFontW(-MulDiv(11, dpi_, 96), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                           L"Segoe UI");
    };
    font_ = make(FW_NORMAL);
    bold_ = make(FW_SEMIBOLD);
}
void MiniWindow::place(bool near_taskbar) {
    RECT current{left_, top_, left_ + Width, top_ + Height};
    HMONITOR monitor = positioned_ ? MonitorFromRect(&current, MONITOR_DEFAULTTONEAREST)
                                   : MonitorFromWindow(main_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info))
        return;
    int logical_width = Width, logical_height = Height;
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    HWND clock = FindWindowExW(FindWindowExW(taskbar, nullptr, L"TrayNotifyWnd", nullptr), nullptr,
                               L"TrayClockWClass", nullptr);
    RECT clock_rect{};
    if (clock && GetWindowRect(clock, &clock_rect)) {
        UINT clock_dpi = dpi_for(clock);
        logical_width = std::clamp(MulDiv(clock_rect.right - clock_rect.left, 192, clock_dpi), 176, 260);
        logical_height = std::clamp(MulDiv(clock_rect.bottom - clock_rect.top, 96, clock_dpi), 40, 56);
    }
    RECT r{0, 0, MulDiv(logical_width, dpi_, 96), MulDiv(logical_height, dpi_, 96)};
    using Adjust = BOOL(WINAPI *)(LPRECT, DWORD, BOOL, DWORD, UINT);
    auto adjust =
        reinterpret_cast<Adjust>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
    if (adjust)
        adjust(&r, Style, FALSE, WS_EX_TOOLWINDOW, dpi_);
    else
        AdjustWindowRectEx(&r, Style, FALSE, WS_EX_TOOLWINDOW);
    LONG width = r.right - r.left, height = r.bottom - r.top;
    const auto &work = info.rcWork;
    if (near_taskbar) {
        LONG gap = MulDiv(8, dpi_, 96);
        left_ = work.right - width - gap;
        top_ = work.bottom - height - gap;
        if (work.top > info.rcMonitor.top)
            top_ = work.top + gap;
        if (work.left > info.rcMonitor.left)
            left_ = work.left + gap;
    }
    left_ = std::clamp(left_, work.left, std::max(work.left, work.right - width));
    top_ = std::clamp(top_, work.top, std::max(work.top, work.bottom - height));
    positioned_ = true;
    SetWindowPos(window_, topmost_ ? HWND_TOPMOST : HWND_NOTOPMOST, left_, top_, width, height,
                 SWP_NOACTIVATE);
}
void MiniWindow::opacity(int percent) {
    if (window_)
        SetLayeredWindowAttributes(
            window_, 0, static_cast<BYTE>(MulDiv(std::clamp(percent, 30, 100), 255, 100)), LWA_ALPHA);
}
bool MiniWindow::visible() const {
    return window_ && IsWindowVisible(window_);
}
void MiniWindow::toggle() {
    if (!window_)
        return;
    if (visible())
        ShowWindow(window_, SW_HIDE);
    else {
        place(false);
        ShowWindow(window_, SW_SHOWNOACTIVATE);
    }
    save();
}
void MiniWindow::save() {
    if (!window_)
        return;
    RECT r{};
    if (!GetWindowRect(window_, &r))
        return;
    left_ = r.left;
    top_ = r.top;
    auto doc = json(cJSON_CreateObject());
    put(doc.get(), "version", uint64_t(1));
    put(doc.get(), "visible", visible());
    put(doc.get(), "topmost", topmost_);
    cJSON_AddNumberToObject(doc.get(), "left", left_);
    cJSON_AddNumberToObject(doc.get(), "top", top_);
    atomic_write(path_, dump(doc.get()));
}
void MiniWindow::update(const Snapshot &snapshot) {
    snapshot_ = snapshot;
    if (!window_)
        return;
    SetWindowTextW(window_, locale_->text("mini.title"));
    tip_.clear();
    for (int i = 0; i < 2; ++i) {
        const auto &row = snapshot.providers[i];
        if (i)
            tip_ += L"\n";
        tip_ += i ? L"Codex" : L"Claude";
        if (row.status == Status::Disabled)
            tip_ += L" · " + std::wstring(locale_->text("status.disabled"));
        else {
            tip_ += L" · " + std::wstring(locale_->text("quota.short")) + L" " +
                    percentage(row.quota.latest.short_term, now_seconds()) + L" · " +
                    std::wstring(locale_->text("quota.week")) + L" " +
                    percentage(row.quota.latest.weekly, now_seconds());
            if (row.quota.latest.observed && now_seconds() > row.quota.latest.observed + 900)
                tip_ += L" · " + std::wstring(locale_->text("quota.stale"));
        }
    }
    tip_ += L"\n" + std::wstring(locale_->text("mini.help"));
    if (tooltip_) {
        TOOLINFOW tool{};
        tool.cbSize = sizeof(tool);
        tool.hwnd = window_;
        tool.uId = reinterpret_cast<UINT_PTR>(window_);
        tool.lpszText = tip_.data();
        SendMessageW(tooltip_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
    }
    if (visible())
        InvalidateRect(window_, nullptr, FALSE);
}
void MiniWindow::shutdown() {
    if (window_) {
        save();
        if (tooltip_)
            DestroyWindow(tooltip_);
        tooltip_ = nullptr;
        DestroyWindow(window_);
        window_ = nullptr;
    }
    if (font_)
        DeleteObject(font_);
    if (bold_)
        DeleteObject(bold_);
    font_ = bold_ = nullptr;
}
void MiniWindow::paint(HDC dc) {
    RECT bounds{};
    GetClientRect(window_, &bounds);
    auto x = [&](int n) { return MulDiv(n, dpi_, 96); };
    HIGHCONTRASTW contrast{};
    contrast.cbSize = sizeof(contrast);
    bool high_contrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
                         (contrast.dwFlags & HCF_HIGHCONTRASTON);
    COLORREF background = high_contrast ? GetSysColor(COLOR_WINDOW) : RGB(13, 23, 33);
    COLORREF panel = high_contrast ? background : RGB(22, 37, 49);
    COLORREF muted = high_contrast ? GetSysColor(COLOR_WINDOWTEXT) : RGB(160, 183, 197);
    COLORREF bright = high_contrast ? GetSysColor(COLOR_WINDOWTEXT) : RGB(236, 246, 249);
    COLORREF track_color = high_contrast ? GetSysColor(COLOR_3DSHADOW) : RGB(46, 65, 79);
    auto fill = [&](RECT rect, COLORREF value, int radius) {
        HBRUSH brush = CreateSolidBrush(value);
        auto old_brush = SelectObject(dc, brush);
        auto old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
        if (radius)
            RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
        else
            FillRect(dc, &rect, brush);
        SelectObject(dc, old_pen);
        SelectObject(dc, old_brush);
        DeleteObject(brush);
    };
    fill(bounds, background, 0);
    SetBkMode(dc, TRANSPARENT);
    auto old_font = SelectObject(dc, font_);
    auto now = now_seconds();
    auto text = [&](const std::wstring &s, RECT r, bool bold, UINT align = DT_LEFT) {
        SelectObject(dc, bold ? bold_ : font_);
        DrawTextW(dc, s.c_str(), -1, &r, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX | align);
    };
    for (int provider = 0; provider < 2; ++provider) {
        const auto &row = snapshot_.providers[provider];
        const auto &store = row.quota;
        int row_height = bounds.bottom / 2, y = provider * row_height;
        fill({x(1), y + x(1), bounds.right - x(1), y + row_height - x(1)}, panel, x(5));
        bool stale = store.latest.observed && now > store.latest.observed + 900;
        SetTextColor(dc, stale || row.status == Status::Disabled ? muted : bright);
        std::wstring name = provider ? L"Codex" : L"Claude";
        text(name, {x(5), y, x(51), y + row_height}, true);
        for (int slot = 0; slot < 2; ++slot) {
            const auto &value = slot ? store.latest.weekly : store.latest.short_term;
            bool available = row.status != Status::Disabled && quota_current(value, now);
            int cell_width = (bounds.right - x(58)) / 2;
            int left = x(54) + slot * cell_width, right = left + cell_width - x(5);
            std::wstring label = locale_->text(slot ? "mini.week" : "mini.short");
            if (!slot && value.available)
                label = value.minutes % 60 == 0 ? std::to_wstring(value.minutes / 60) + L"h"
                                                : std::to_wstring(value.minutes) + L"m";
            int label_bottom = y + std::min(x(14), row_height - x(8));
            SetTextColor(dc, muted);
            text(label, {left, y + x(1), right - x(27), label_bottom}, false);
            SetTextColor(dc, available && !stale ? bright : muted);
            text(available ? std::to_wstring(value.remaining / 100) + L"%" : L"—",
                 {left + x(21), y + x(1), right, label_bottom}, true, DT_RIGHT);
            // Give the remaining allowance a solid 6–8 DIP band at clock height.
            // Time-series history remains in the detailed view where it is readable.
            RECT track{left, label_bottom, right, y + row_height - x(2)};
            int radius = std::min<int>(x(4), track.bottom - track.top);
            fill(track, track_color, radius);
            if (available) {
                int width = MulDiv(track.right - track.left, value.remaining, 10000);
                if (width > 0) {
                    COLORREF accent =
                        high_contrast ? GetSysColor(COLOR_HIGHLIGHT) : color(value.remaining, slot);
                    int saved = SaveDC(dc);
                    IntersectClipRect(dc, track.left, track.top, track.left + width, track.bottom);
                    fill(track, accent, radius);
                    RestoreDC(dc, saved);
                }
            }
        }
    }
    SelectObject(dc, old_font);
}
void MiniWindow::menu(POINT point) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, Details, locale_->text("action.open"));
    AppendMenuW(menu, MF_STRING, Refresh, locale_->text("action.refresh"));
    AppendMenuW(menu, MF_STRING, Configure, locale_->text("action.settings"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (topmost_ ? MF_CHECKED : 0), Pin, locale_->text("mini.topmost"));
    AppendMenuW(menu, MF_STRING, Dock, locale_->text("mini.dock"));
    AppendMenuW(menu, MF_STRING, Hide, locale_->text("mini.hide"));
    SetForegroundWindow(window_);
    auto selected =
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window_, nullptr);
    DestroyMenu(menu);
    if (selected)
        SendMessageW(window_, WM_COMMAND, selected, 0);
    PostMessageW(window_, WM_NULL, 0, 0);
}
LRESULT CALLBACK MiniWindow::procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto self = reinterpret_cast<MiniWindow *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<MiniWindow *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->dispatch(message, w, l) : DefWindowProcW(window, message, w, l);
}
LRESULT MiniWindow::dispatch(UINT message, WPARAM w, LPARAM l) {
    switch (message) {
    case WM_LBUTTONDOWN: {
        POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        ClientToScreen(window_, &point);
        ReleaseCapture();
        SendMessageW(window_, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(point.x, point.y));
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(window_, &ps);
        RECT r{};
        GetClientRect(window_, &r);
        HDC memory = CreateCompatibleDC(dc);
        HBITMAP bitmap = CreateCompatibleBitmap(dc, std::max(1L, r.right), std::max(1L, r.bottom));
        if (memory && bitmap) {
            auto old = SelectObject(memory, bitmap);
            paint(memory);
            BitBlt(dc, 0, 0, r.right, r.bottom, memory, 0, 0, SRCCOPY);
            SelectObject(memory, old);
        } else
            paint(dc);
        if (bitmap)
            DeleteObject(bitmap);
        if (memory)
            DeleteDC(memory);
        EndPaint(window_, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:
        paint(reinterpret_cast<HDC>(w));
        return 0;
    case WM_CLOSE:
        ShowWindow(window_, SW_HIDE);
        save();
        return 0;
    case WM_EXITSIZEMOVE: {
        RECT r{};
        GetWindowRect(window_, &r);
        left_ = r.left;
        top_ = r.top;
        place(false);
        save();
        return 0;
    }
    case WM_DPICHANGED: {
        auto r = reinterpret_cast<RECT *>(l);
        dpi_ = HIWORD(w);
        left_ = r->left;
        top_ = r->top;
        fonts();
        place(false);
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    }
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        place(false);
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    case WM_CONTEXTMENU: {
        POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        if (point.x == -1 && point.y == -1) {
            point = {10, 10};
            ClientToScreen(window_, &point);
        }
        menu(point);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case Pin:
            topmost_ = !topmost_;
            SetWindowPos(window_, topmost_ ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            save();
            break;
        case Dock:
            place(true);
            save();
            break;
        case Details:
            PostMessageW(main_, WM_COMMAND, 100, 0);
            break;
        case Refresh:
            PostMessageW(main_, WM_COMMAND, 101, 0);
            break;
        case Configure:
            PostMessageW(main_, WM_COMMAND, 102, 0);
            break;
        case Hide:
            SendMessageW(window_, WM_CLOSE, 0, 0);
            break;
        }
        return 0;
    }
    return DefWindowProcW(window_, message, w, l);
}
} // namespace aimon
