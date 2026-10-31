// X11 research prototype: deliberately uses XEmbed for parity with fltk.cpp.
#include <gtk/gtk.h>
#include <gdk/x11/gdkx.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <cstdio>
#include <string>
#include <filesystem>
#include <fstream>

static GtkWidget *detail, *mini, *menu;
static Display *xd;
static Window icon, owner;
static bool embedded;
static GMainLoop *loop;
static int drag_x, drag_y, origin_x, origin_y;
static std::string state_path() {
    const char *state = g_getenv("XDG_STATE_HOME"), *home = g_getenv("HOME");
    if (state && *state == '/') return std::string(state) + "/ai-mon-gtk-prototype/position";
    if (home && *home) return std::string(home) + "/.local/state/ai-mon-gtk-prototype/position";
    return {};
}
static Window xid(GtkWidget *w) {
    GdkSurface *s = gtk_native_get_surface(GTK_NATIVE(w));
    return s && GDK_IS_X11_SURFACE(s) ? gdk_x11_surface_get_xid(s) : 0;
}
static void show_detail(GtkButton *, gpointer) { gtk_window_present(GTK_WINDOW(detail)); }
static void quit(GtkButton *, gpointer) { g_main_loop_quit(loop); }
static void toggle_mini(GtkButton *, gpointer) {
    if (gtk_widget_get_visible(mini)) { gtk_widget_set_visible(mini, FALSE); return; }
    gtk_window_present(GTK_WINDOW(mini));
    Window w = xid(mini); int x, y; std::ifstream f(state_path());
    if (f >> x >> y) XMoveWindow(xd, w, x, y);
    unsigned long opacity = 0xe6666666UL;
    XChangeProperty(xd, w, XInternAtom(xd, "_NET_WM_WINDOW_OPACITY", False), XA_CARDINAL, 32, PropModeReplace, reinterpret_cast<unsigned char *>(&opacity), 1);
    XFlush(xd);
}
static gboolean close_detail(GtkWindow *, gpointer) {
    if (embedded || gtk_widget_get_visible(mini)) gtk_widget_set_visible(detail, FALSE);
    else g_main_loop_quit(loop);
    return TRUE;
}
static gboolean close_mini(GtkWindow *, gpointer) {
    gtk_widget_set_visible(mini, FALSE);
    if (!embedded && !gtk_widget_get_visible(detail)) gtk_window_present(GTK_WINDOW(detail));
    return TRUE;
}
static void drag_begin(GtkGestureDrag *, double, double, gpointer) {
    Window root, child; int rx, ry; unsigned int mask;
    XQueryPointer(xd, DefaultRootWindow(xd), &root, &child, &rx, &ry, &origin_x, &origin_y, &mask);
    Window ignored; XTranslateCoordinates(xd, xid(mini), DefaultRootWindow(xd), 0, 0, &drag_x, &drag_y, &ignored);
}
static void drag_update(GtkGestureDrag *, double dx, double dy, gpointer) {
    XMoveWindow(xd, xid(mini), drag_x + static_cast<int>(dx), drag_y + static_cast<int>(dy)); XFlush(xd);
}
static void drag_end(GtkGestureDrag *, double dx, double dy, gpointer) {
    auto path = state_path(); if (path.empty()) return;
    std::error_code ec; std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream f(path); if (f) f << drag_x + static_cast<int>(dx) << ' ' << drag_y + static_cast<int>(dy) << '\n';
}
static void draw_icon() {
    GC gc = XCreateGC(xd, icon, 0, nullptr);
    XSetForeground(xd, gc, 0x263445); XFillRectangle(xd, icon, gc, 0, 0, 24, 24);
    XSetForeground(xd, gc, 0x6bd79b); XFillRectangle(xd, icon, gc, 5, 7, 5, 13);
    XSetForeground(xd, gc, 0x76b6ff); XFillRectangle(xd, icon, gc, 14, 11, 5, 9);
    XFreeGC(xd, gc); XFlush(xd);
}
static gboolean events(GIOChannel *, GIOCondition, gpointer) {
    while (XPending(xd)) {
        XEvent e; XNextEvent(xd, &e);
        if (e.type == Expose) draw_icon();
        if (e.type == ButtonPress && e.xbutton.button == Button1) show_detail(nullptr, nullptr);
        if (e.type == ButtonPress && e.xbutton.button == Button3) {
            gtk_window_present(GTK_WINDOW(menu)); XMoveWindow(xd, xid(menu), e.xbutton.x_root, e.xbutton.y_root); XFlush(xd);
        }
        if (e.type == DestroyNotify && e.xdestroywindow.window == owner) { embedded = false; owner = 0; gtk_window_present(GTK_WINDOW(detail)); }
    }
    return G_SOURCE_CONTINUE;
}
static gboolean check_tray(gpointer) {
    Atom selection = XInternAtom(xd, ("_NET_SYSTEM_TRAY_S" + std::to_string(DefaultScreen(xd))).c_str(), False);
    Window current = XGetSelectionOwner(xd, selection);
    if (current != owner) {
        owner = current; embedded = false;
        if (owner) {
            XSelectInput(xd, owner, StructureNotifyMask);
            XEvent e{}; e.xclient.type = ClientMessage; e.xclient.window = owner;
            e.xclient.message_type = XInternAtom(xd, "_NET_SYSTEM_TRAY_OPCODE", False); e.xclient.format = 32;
            e.xclient.data.l[0] = CurrentTime; e.xclient.data.l[1] = 0; e.xclient.data.l[2] = icon;
            XSendEvent(xd, owner, False, NoEventMask, &e); XFlush(xd);
        } else gtk_window_present(GTK_WINDOW(detail));
    }
    Window root, parent, *children = nullptr; unsigned int count = 0;
    if (XQueryTree(xd, icon, &root, &parent, &children, &count)) { if (children) XFree(children); embedded = current && parent != root; }
    return G_SOURCE_CONTINUE;
}
static void menu_show(GtkButton *b, gpointer p) { gtk_widget_set_visible(menu, FALSE); show_detail(b, p); }
static void menu_mini(GtkButton *b, gpointer p) { gtk_widget_set_visible(menu, FALSE); toggle_mini(b, p); }
static GtkWidget *button(const char *text, GCallback cb) {
    GtkWidget *b = gtk_button_new_with_label(text); g_signal_connect(b, "clicked", cb, nullptr); return b;
}
int main() {
    g_setenv("GDK_BACKEND", "x11", TRUE); gtk_init();
    xd = XOpenDisplay(nullptr); if (!xd) { std::fprintf(stderr, "X11 display unavailable\n"); return 1; }
    loop = g_main_loop_new(nullptr, FALSE);
    detail = gtk_window_new(); gtk_window_set_title(GTK_WINDOW(detail), "AI Mon - GTK X11 prototype"); gtk_window_set_default_size(GTK_WINDOW(detail), 320, 270);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8); gtk_window_set_child(GTK_WINDOW(detail), box);
    for (auto name : {"Claude Code", "Codex"}) { gtk_box_append(GTK_BOX(box), gtk_label_new(name)); GtkWidget *p = gtk_progress_bar_new(); gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(p), .65); gtk_box_append(GTK_BOX(box), p); }
    gtk_box_append(GTK_BOX(box), gtk_entry_new()); gtk_box_append(GTK_BOX(box), button("미니 창", G_CALLBACK(toggle_mini))); gtk_box_append(GTK_BOX(box), button("종료", G_CALLBACK(quit)));
    g_signal_connect(detail, "close-request", G_CALLBACK(close_detail), nullptr);
    mini = gtk_window_new(); gtk_window_set_title(GTK_WINDOW(mini), "AI Mon GTK mini"); gtk_window_set_default_size(GTK_WINDOW(mini), 190, 58);
    GtkWidget *label = gtk_label_new("Claude 65%   Codex 40%"); gtk_window_set_child(GTK_WINDOW(mini), label);
    auto *gesture = gtk_gesture_drag_new(); gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(gesture));
    g_signal_connect(gesture, "drag-begin", G_CALLBACK(drag_begin), nullptr); g_signal_connect(gesture, "drag-update", G_CALLBACK(drag_update), nullptr); g_signal_connect(gesture, "drag-end", G_CALLBACK(drag_end), nullptr);
    g_signal_connect(mini, "close-request", G_CALLBACK(close_mini), nullptr);
    menu = gtk_window_new(); gtk_window_set_title(GTK_WINDOW(menu), "AI Mon GTK tray menu"); gtk_window_set_decorated(GTK_WINDOW(menu), FALSE);
    GtkWidget *mb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); gtk_window_set_child(GTK_WINDOW(menu), mb);
    gtk_box_append(GTK_BOX(mb), button("열기", G_CALLBACK(menu_show))); gtk_box_append(GTK_BOX(mb), button("미니 창 표시/숨기기", G_CALLBACK(menu_mini))); gtk_box_append(GTK_BOX(mb), button("종료", G_CALLBACK(quit)));
    icon = XCreateSimpleWindow(xd, DefaultRootWindow(xd), 0, 0, 24, 24, 0, 0, 0); XStoreName(xd, icon, "AI Mon GTK prototype tray");
    XSelectInput(xd, icon, ExposureMask | ButtonPressMask | StructureNotifyMask);
    Atom info = XInternAtom(xd, "_XEMBED_INFO", False); unsigned long values[] = {0, 1}; XChangeProperty(xd, icon, info, info, 32, PropModeReplace, reinterpret_cast<unsigned char *>(values), 2);
    GIOChannel *channel = g_io_channel_unix_new(ConnectionNumber(xd)); g_io_add_watch(channel, G_IO_IN, events, nullptr); g_timeout_add(2000, check_tray, nullptr);
    gtk_window_present(GTK_WINDOW(detail)); check_tray(nullptr); g_main_loop_run(loop);
    g_io_channel_unref(channel); XDestroyWindow(xd, icon); XCloseDisplay(xd); return 0;
}
