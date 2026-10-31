#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Progress.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Button.H>
int main() {
    Fl_Window window(320, 240, "AI Mon");
    Fl_Box claude(12, 12, 296, 25, "Claude Code");
    Fl_Progress first(12, 40, 296, 20); first.minimum(0); first.maximum(100); first.value(65);
    Fl_Box codex(12, 70, 296, 25, "Codex");
    Fl_Progress second(12, 98, 296, 20); second.minimum(0); second.maximum(100); second.value(65);
    Fl_Input input(12, 132, 296, 25);
    Fl_Button refresh(12, 170, 296, 30, "새로고침 / Refresh");
    window.end(); window.show(); return Fl::run();
}
