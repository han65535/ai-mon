#include "history_window.hpp"
#include <commdlg.h>
#include <algorithm>
#include <limits>

namespace aimon {
namespace {
constexpr wchar_t WindowClass[] = L"AI.Mon.History.1";
constexpr int Period = 2001, Save = 2002;
UINT window_dpi(HWND window) {
    using Fn = UINT(WINAPI *)(HWND);
    auto fn = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    return fn ? fn(window) : 96;
}
}
bool HistoryWindow::initialize(HINSTANCE instance, HWND owner, const Localizer &locale) {
    instance_ = instance; owner_ = owner; locale_ = &locale;
    WNDCLASSW wc{}; wc.lpfnWndProc = procedure; wc.hInstance = instance; wc.lpszClassName = WindowClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    return RegisterClassW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}
void HistoryWindow::show() {
    if (!window_) {
        const UINT initial_dpi = window_dpi(owner_);
        window_ = CreateWindowExW(WS_EX_CONTROLPARENT, WindowClass, locale_->text("history.title"),
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(850, initial_dpi, 96), MulDiv(560, initial_dpi, 96), owner_, nullptr, instance_, this);
        if (!window_) return;
    }
    render(); ShowWindow(window_, SW_SHOWNORMAL); SetForegroundWindow(window_);
}
void HistoryWindow::update(const HistoryView &view) { view_ = view; if (window_) render(); }
void HistoryWindow::shutdown() { if (window_) DestroyWindow(window_); }
LRESULT CALLBACK HistoryWindow::procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto *self = reinterpret_cast<HistoryWindow *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) { auto *create = reinterpret_cast<CREATESTRUCTW *>(l); self = static_cast<HistoryWindow *>(create->lpCreateParams); self->window_ = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
    if (self && message == WM_NCDESTROY) { SetWindowLongPtrW(window, GWLP_USERDATA, 0); return DefWindowProcW(window, message, w, l); }
    return self ? self->dispatch(message, w, l) : DefWindowProcW(window, message, w, l);
}
LRESULT HistoryWindow::dispatch(UINT message, WPARAM w, LPARAM l) {
    switch (message) {
    case WM_CREATE: {
        dpi_ = window_dpi(window_);
        font_ = CreateFontW(-MulDiv(14, dpi_, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        period_ = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, 0, 0, 0, window_, reinterpret_cast<HMENU>(Period), instance_, nullptr);
        save_ = CreateWindowExW(0, L"BUTTON", locale_->text("history.export"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0, window_, reinterpret_cast<HMENU>(Save), instance_, nullptr);
        text_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
            0, 0, 0, 0, window_, nullptr, instance_, nullptr);
        if (!period_ || !save_ || !text_ || !font_) return -1;
        for (HWND control : {period_, save_, text_}) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        SendMessageW(period_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(locale_->text("history.weekly")));
        SendMessageW(period_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(locale_->text("history.monthly")));
        SendMessageW(period_, CB_SETCURSEL, 0, 0); SendMessageW(text_, EM_SETLIMITTEXT, 0x7ffffffe, 0);
        return 0;
    }
    case WM_SIZE: layout(LOWORD(l), HIWORD(l)); return 0;
    case WM_DPICHANGED: {
        dpi_ = HIWORD(w);
        HFONT replacement = CreateFontW(-MulDiv(14, dpi_, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        if (replacement) { for (HWND control : {period_, save_, text_}) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(replacement), TRUE); if (font_) DeleteObject(font_); font_ = replacement; }
        auto rect = reinterpret_cast<RECT *>(l); SetWindowPos(window_, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == Period && HIWORD(w) == CBN_SELCHANGE) { render(); return 0; }
        if (LOWORD(w) == Save) { save_csv(); return 0; }
        break;
    case WM_CLOSE: ShowWindow(window_, SW_HIDE); return 0;
    case WM_DESTROY: if (font_) { DeleteObject(font_); font_ = nullptr; } window_ = nullptr; period_ = nullptr; text_ = nullptr; save_ = nullptr; last_text_.clear(); return 0;
    }
    return DefWindowProcW(window_, message, w, l);
}
void HistoryWindow::layout(int width, int height) {
    const int pad = MulDiv(12, dpi_, 96), button_height = MulDiv(30, dpi_, 96), choice_width = MulDiv(150, dpi_, 96), save_width = MulDiv(160, dpi_, 96);
    MoveWindow(period_, pad, pad, choice_width, MulDiv(180, dpi_, 96), TRUE);
    MoveWindow(save_, pad * 2 + choice_width, pad, save_width, button_height, TRUE);
    MoveWindow(text_, pad, pad * 2 + button_height, std::max(1, width - pad * 2), std::max(1, height - pad * 3 - button_height), TRUE);
}
void HistoryWindow::render() {
    if (!window_ || !text_) return;
    const int selected = SendMessageW(period_, CB_GETCURSEL, 0, 0) == 1 ? 1 : 0;
    wchar_t first[256]{};
    LRESULT length = SendMessageW(period_, CB_GETLBTEXTLEN, 0, 0);
    if (length >= 0 && length < 255) SendMessageW(period_, CB_GETLBTEXT, 0, reinterpret_cast<LPARAM>(first));
    if (std::wstring(first) != locale_->text("history.weekly")) {
        SendMessageW(period_, CB_RESETCONTENT, 0, 0);
        SendMessageW(period_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(locale_->text("history.weekly")));
        SendMessageW(period_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(locale_->text("history.monthly")));
        SendMessageW(period_, CB_SETCURSEL, selected, 0);
        SetWindowTextW(window_, locale_->text("history.title"));
        SetWindowTextW(save_, locale_->text("history.export"));
    }
    std::wstring text = locale_->text(selected ? "history.month_header" : "history.week_header"); text += L"\r\n";
    if (view_.failed) text += std::wstring(locale_->text("history.error")) + L"\r\n";
    if (view_.pending) text += std::wstring(locale_->text("history.collecting")) + L"\r\n";
    if (view_.incomplete) text += std::wstring(locale_->text("history.incomplete")) + L"\r\n";
    if (view_.truncated[selected]) text += std::wstring(locale_->text("history.truncated")) + L"\r\n";
    text += std::wstring(locale_->text("history.scope")) + L"\r\n\r\n";
    const auto &rows = view_.rows[selected];
    for (size_t start = 0; start < rows.size();) {
        size_t end = start; uint64_t input = 0, output = 0, count = 0; bool overflow = false;
        auto add = [&](uint64_t &sum, uint64_t val) { if (val > std::numeric_limits<uint64_t>::max() - sum) overflow = true; else sum += val; };
        while (end < rows.size() && rows[end].period == rows[start].period) { add(input, rows[end].input); add(output, rows[end].output); add(count, rows[end].events); ++end; }
        text += wide(rows[start].period) + L"  " + locale_->text("history.total") + L"  ";
        if (overflow) text += locale_->text("history.overflow");
        else text += locale_->format("history.totals", {{L"input", grouped(input)}, {L"output", grouped(output)}, {L"count", grouped(count)}});
        text += L"\r\n";
        for (size_t i = start; i < end; ++i) {
            const auto &r = rows[i];
            text += L"  " + std::wstring(r.provider ? L"Codex" : L"Claude") + L"  " + (r.model.empty() ? locale_->text("history.unknown_model") : wide(r.model)) + L"  " + (r.source.empty() ? locale_->text("history.unknown_source") : wide(r.source)) + L"\r\n";
            text += L"    " + locale_->format("history.row", {{L"input", grouped(r.input)}, {L"output", grouped(r.output)}, {L"cached", grouped(r.cached)}, {L"count", grouped(r.events)}}) + L"\r\n";
        }
        text += L"\r\n"; start = end;
    }
    if (rows.empty() && !view_.pending && !view_.failed) text += locale_->text("history.empty");
    if (text != last_text_) { last_text_ = std::move(text); SetWindowTextW(text_, last_text_.c_str()); }
}
void HistoryWindow::save_csv() {
    const int selected = SendMessageW(period_, CB_GETCURSEL, 0, 0) == 1 ? 1 : 0;
    if (view_.pending || view_.failed || view_.truncated[selected]) { MessageBoxW(window_, locale_->text("history.export_unavailable"), locale_->text("history.title"), MB_OK | MB_ICONWARNING); return; }
    wchar_t path[32768] = L"ai-mon-history.csv";
    OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = window_; dialog.lpstrFile = path; dialog.nMaxFile = 32768;
    dialog.lpstrFilter = L"CSV (*.csv)\0*.csv\0\0"; dialog.lpstrDefExt = L"csv"; dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog)) return;
    const auto csv = std::string("\xef\xbb\xbf") + format_history_csv(view_.rows[selected], utf8(locale_->text("history.unknown_model")), utf8(locale_->text("history.unknown_source")));
    if (!atomic_write(path, csv)) MessageBoxW(window_, locale_->text("history.export_failed"), locale_->text("history.title"), MB_OK | MB_ICONERROR);
}
}
