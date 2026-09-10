#pragma once
#include "collector.hpp"
#include "localization.hpp"

namespace aimon {
class MiniWindow {
  public:
    bool initialize(HINSTANCE instance, HWND main, const std::wstring &data, const Localizer &locale);
    void toggle();
    bool visible() const;
    void update(const Snapshot &snapshot);
    void shutdown();
    void opacity(int percent);

  private:
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM w, LPARAM l);
    LRESULT dispatch(UINT message, WPARAM w, LPARAM l);
    void fonts();
    void place(bool near_taskbar);
    void save();
    void paint(HDC dc);
    void menu(POINT point);
    HWND window_ = nullptr, main_ = nullptr, tooltip_ = nullptr;
    HFONT font_ = nullptr, bold_ = nullptr;
    UINT dpi_ = 96;
    const Localizer *locale_ = nullptr;
    std::wstring path_, tip_;
    Snapshot snapshot_;
    bool topmost_ = true, positioned_ = false;
    LONG left_ = 0, top_ = 0;
};
} // namespace aimon
