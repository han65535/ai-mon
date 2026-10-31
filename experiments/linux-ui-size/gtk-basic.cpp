#include <initializer_list>
#include <gtk/gtk.h>
static void activate(GtkApplication *app, gpointer) {
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "AI Mon");
    gtk_window_set_default_size(GTK_WINDOW(window), 320, 240);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(box, 12); gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 12); gtk_widget_set_margin_bottom(box, 12);
    gtk_window_set_child(GTK_WINDOW(window), box);
    for (const char *name : {"Claude Code", "Codex"}) {
        gtk_box_append(GTK_BOX(box), gtk_label_new(name));
        GtkWidget *bar = gtk_progress_bar_new();
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(bar), .65);
        gtk_box_append(GTK_BOX(box), bar);
    }
    gtk_box_append(GTK_BOX(box), gtk_entry_new());
    gtk_box_append(GTK_BOX(box), gtk_button_new_with_label("새로고침 / Refresh"));
    gtk_window_present(GTK_WINDOW(window));
}
int main(int argc, char **argv) {
    auto *app = gtk_application_new("org.example.AIMonSize", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), nullptr);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app); return status;
}
