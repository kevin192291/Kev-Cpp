---
order: 1
section: programs
description: A GTK 4 application in C with one window holding a label and a button with a callback
file: main.c
whole-file: yes
ask: AppName = MyApp
packages: gtk4
---
// A GTK 4 application with one window.
#include <gtk/gtk.h>

static void on_button_clicked(GtkButton* button, gpointer user_data)
{
    (void)button;
    GtkLabel* label = GTK_LABEL(user_data);
    gtk_label_set_text(label, "You clicked the button.");
    {{cursor}}
}

// Runs when the application starts (and again if a second copy is started): build the window here.
static void activate(GtkApplication* app, gpointer user_data)
{
    (void)user_data;
    GtkWidget* window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "{{AppName}}");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);

    GtkWidget* label = gtk_label_new("Hello, GTK!");
    gtk_widget_set_vexpand(label, TRUE);
    GtkWidget* button = gtk_button_new_with_label("Click Me");
    gtk_widget_set_halign(button, GTK_ALIGN_CENTER);
    g_signal_connect(button, "clicked", G_CALLBACK(on_button_clicked), label);

    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), button);
    gtk_window_set_child(GTK_WINDOW(window), box);
    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char* argv[])
{
    // The ID must be unique to this program: a second copy started with the same ID only activates the first.
    GtkApplication* app = gtk_application_new("org.example.{{AppName:pascal}}", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
