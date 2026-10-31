#pragma once
#include "history.hpp"
#include "localization.hpp"
#include <array>
namespace aimon {
struct HistoryView {
    std::array<std::vector<HistoryRow>, 2> rows;
    std::array<bool, 2> truncated{{false, false}};
    bool pending = true, incomplete = false, failed = false;
};
class HistoryWindow {
public:
    bool initialize(HINSTANCE instance, HWND owner, const Localizer &locale);
    void show();
    void update(const HistoryView &view);
    void shutdown();
    bool visible() const { return window_ && IsWindowVisible(window_); }
    bool handle_message(MSG &message) const { return visible() && IsDialogMessageW(window_, &message); }
private:
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    LRESULT dispatch(UINT, WPARAM, LPARAM);
    void render();
    void save_csv();
    void layout(int width, int height);
    HWND window_ = nullptr, owner_ = nullptr, period_ = nullptr, text_ = nullptr, save_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HFONT font_ = nullptr;
    UINT dpi_ = 96;
    const Localizer *locale_ = nullptr;
    HistoryView view_;
    std::wstring last_text_;
};
}
