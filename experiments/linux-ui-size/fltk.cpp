// X11-only research prototype. The tray uses the XEmbed system tray protocol.
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Progress.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/x.H>
#include <X11/Xatom.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <filesystem>
#include <fstream>

static Fl_Window *detail;
static bool tray_ready = false;
static bool running = true;
static Display *tray_display = nullptr;
static Window tray_icon = 0;
static Window tray_owner = 0;
static Fl_Menu_Button *tray_menu;

static std::string state_path() {
    const char *state = std::getenv("XDG_STATE_HOME");
    const char *home = std::getenv("HOME");
    if (state && *state && state[0] == '/') return std::string(state) + "/ai-mon-ui-prototype/position";
    if (home && *home) return std::string(home) + "/.local/state/ai-mon-ui-prototype/position";
    return {};
}
class Mini : public Fl_Window {
    int drag_x = 0, drag_y = 0;
public:
    Mini() : Fl_Window(190, 58, "AI Mon mini") {
        begin();
        auto *label = new Fl_Box(4, 2, 182, 23, "Claude 65%   Codex 40%");
        label->labelsize(12);
        auto *bar = new Fl_Progress(8, 30, 174, 14);
        bar->minimum(0); bar->maximum(100); bar->value(65);
        end();
        int px, py; std::ifstream f(state_path()); if (f >> px >> py) position(px, py);
    }
    int handle(int event) override {
        if (event == FL_PUSH && Fl::event_button() == FL_LEFT_MOUSE) {
            drag_x = Fl::event_x_root() - x(); drag_y = Fl::event_y_root() - y(); return 1;
        }
        if (event == FL_DRAG && Fl::event_button() == FL_LEFT_MOUSE) {
            position(Fl::event_x_root() - drag_x, Fl::event_y_root() - drag_y); return 1;
        }
        if (event == FL_RELEASE && Fl::event_button() == FL_LEFT_MOUSE) {
            auto path = state_path();
            if (!path.empty()) { std::error_code ec; std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec); std::ofstream f(path); if (f) f << x() << " " << y() << "\n"; }
            return 1;
        }
        return Fl_Window::handle(event);
    }
};
static Mini *mini;
static void show_detail(Fl_Widget *, void *) { detail->show(); detail->take_focus(); }
static void toggle_mini(Fl_Widget *, void *) {
    if (mini->shown()) mini->hide();
    else {
        mini->show();
        unsigned long opacity = 0xe6666666UL;
        Atom property = XInternAtom(fl_display, "_NET_WM_WINDOW_OPACITY", False);
        XChangeProperty(fl_display, fl_xid(mini), property, XA_CARDINAL, 32, PropModeReplace, reinterpret_cast<unsigned char *>(&opacity), 1);
        XFlush(fl_display);
    }
}
static void quit(Fl_Widget *, void *) { running = false; detail->hide(); mini->hide(); }
static void close_detail(Fl_Widget *, void *) {
    if (tray_ready || mini->shown()) detail->hide();
    else quit(nullptr, nullptr);
}
static void draw_icon() {
    if (!tray_display || !tray_icon) return;
    GC gc = XCreateGC(tray_display, tray_icon, 0, nullptr);
    XSetForeground(tray_display, gc, 0x263445); XFillRectangle(tray_display, tray_icon, gc, 0, 0, 24, 24);
    XSetForeground(tray_display, gc, 0x6bd79b); XFillRectangle(tray_display, tray_icon, gc, 5, 7, 5, 13);
    XSetForeground(tray_display, gc, 0x76b6ff); XFillRectangle(tray_display, tray_icon, gc, 14, 11, 5, 9);
    XFreeGC(tray_display, gc); XFlush(tray_display);
}
static void tray_events(int, void *) {
    while (XPending(tray_display)) {
        XEvent event; XNextEvent(tray_display, &event);
        if (event.type == Expose) draw_icon();
        if (event.type == ButtonPress) {
            if (event.xbutton.button == Button1) show_detail(nullptr, nullptr);
            if (event.xbutton.button == Button3) { const Fl_Menu_Item *item = tray_menu->menu()->popup(event.xbutton.x_root, event.xbutton.y_root); if (item) item->do_callback(tray_menu); }
        }
        if (event.type == DestroyNotify && event.xdestroywindow.window == tray_owner) {
            tray_ready = false; tray_owner = 0; detail->show();
        }
    }
}
static void check_tray(void *) {
    Atom selection = XInternAtom(tray_display, ("_NET_SYSTEM_TRAY_S" + std::to_string(DefaultScreen(tray_display))).c_str(), False);
    Window owner = XGetSelectionOwner(tray_display, selection);
    if (owner != tray_owner) {
        tray_ready = false; tray_owner = owner;
        if (owner) {
            XSelectInput(tray_display, owner, StructureNotifyMask);
            XEvent message{}; message.xclient.type = ClientMessage; message.xclient.window = owner;
            message.xclient.message_type = XInternAtom(tray_display, "_NET_SYSTEM_TRAY_OPCODE", False);
            message.xclient.format = 32; message.xclient.data.l[0] = CurrentTime;
            message.xclient.data.l[1] = 0; message.xclient.data.l[2] = tray_icon;
            XSendEvent(tray_display, owner, False, NoEventMask, &message); XFlush(tray_display);
        } else detail->show();
    }
    // Presence of a tray manager alone does not prove the icon was embedded.
    Window root, parent, *children = nullptr; unsigned int count = 0;
    if (XQueryTree(tray_display, tray_icon, &root, &parent, &children, &count)) {
        if (children) XFree(children);
        tray_ready = owner && parent != root;
    }
    Fl::repeat_timeout(2.0, check_tray);
}
int main() {
    setenv("FLTK_BACKEND", "x11", 1);
    tray_display = XOpenDisplay(nullptr);
    if (!tray_display) { std::fprintf(stderr, "X11 display unavailable\n"); return 1; }
    detail = new Fl_Window(320, 270, "AI Mon - X11 prototype");
    auto *claude = new Fl_Box(12, 12, 296, 25, "Claude Code"); (void)claude;
    auto *first = new Fl_Progress(12, 40, 296, 20); first->minimum(0); first->maximum(100); first->value(65);
    auto *codex = new Fl_Box(12, 70, 296, 25, "Codex"); (void)codex;
    auto *second = new Fl_Progress(12, 98, 296, 20); second->minimum(0); second->maximum(100); second->value(40);
    auto *input = new Fl_Input(12, 132, 296, 25); input->tooltip("한글 입력 시험 / Korean input test");
    auto *refresh = new Fl_Button(12, 170, 142, 30, "미니 창"); refresh->callback(toggle_mini);
    auto *exit = new Fl_Button(166, 170, 142, 30, "종료"); exit->callback(quit);
    detail->end(); detail->callback(close_detail);
    mini = new Mini(); mini->callback([](Fl_Widget *w, void *) { w->hide(); if (!tray_ready && !detail->shown()) detail->show(); });
    tray_menu = new Fl_Menu_Button(0, 0, 0, 0); tray_menu->add("열기", 0, show_detail); tray_menu->add("미니 창 표시/숨기기", 0, toggle_mini); tray_menu->add("종료", 0, quit);
    int screen = DefaultScreen(tray_display);
    tray_icon = XCreateSimpleWindow(tray_display, RootWindow(tray_display, screen), 0, 0, 24, 24, 0, 0, 0);
    XStoreName(tray_display, tray_icon, "AI Mon prototype tray");
    XSelectInput(tray_display, tray_icon, ExposureMask | ButtonPressMask | StructureNotifyMask);
    Atom info = XInternAtom(tray_display, "_XEMBED_INFO", False); unsigned long values[] = {0, 1};
    XChangeProperty(tray_display, tray_icon, info, info, 32, PropModeReplace, reinterpret_cast<unsigned char *>(values), 2);
    Fl::add_fd(ConnectionNumber(tray_display), tray_events);
    detail->show(); check_tray(nullptr);
    while (running) Fl::wait();
    Fl::remove_fd(ConnectionNumber(tray_display)); XDestroyWindow(tray_display, tray_icon); XCloseDisplay(tray_display);
    return 0;
}
