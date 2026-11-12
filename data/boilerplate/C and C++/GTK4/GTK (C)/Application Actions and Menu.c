---
order: 2
section: programs
description: A GTK 4 application in C with a menu bar, shortcuts, app actions (Quit, About) and window actions
file: main.c
whole-file: yes
ask: AppName = MyApp
packages: gtk4
---
// A GTK 4 application with a menu bar. Menu items name actions: "app.quit" is one of the application's,
// "win.clear" one of the window's. Actions and menus are made once, in startup; windows in activate.
#include <gtk/gtk.h>

static void quit_activated(GSimpleAction* action, GVariant* parameter, gpointer app)
{
    (void)action;
    (void)parameter;
    g_application_quit(G_APPLICATION(app));
}

static void about_activated(GSimpleAction* action, GVariant* parameter, gpointer app)
{
    (void)action;
    (void)parameter;
    gtk_show_about_dialog(gtk_application_get_active_window(GTK_APPLICATION(app)),
                          "program-name", "{{AppName}}",
                          "version", "1.0",
                          "comments", "Made with GTK.",
                          NULL);
}

// The window's actions get its text view as user data (see activate).
static void clear_activated(GSimpleAction* action, GVariant* parameter, gpointer text_view)
{
    (void)action;
    (void)parameter;
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view)), "", -1);
    {{cursor}}
}

// An action with a true/false state shows as a check item; GTK passes the new state here.
static void wrap_changed(GSimpleAction* action, GVariant* state, gpointer text_view)
{
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), g_variant_get_boolean(state) ? GTK_WRAP_WORD : GTK_WRAP_NONE);
    g_simple_action_set_state(action, state);
}

static void startup(GApplication* app, gpointer user_data)
{
    (void)user_data;
    static const GActionEntry app_actions[] = {
        {.name = "quit", .activate = quit_activated},
        {.name = "about", .activate = about_activated},
    };
    g_action_map_add_action_entries(G_ACTION_MAP(app), app_actions, G_N_ELEMENTS(app_actions), app);

    const char* quit_keys[] = {"<Primary>q", NULL};
    const char* clear_keys[] = {"<Primary>l", NULL};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.quit", quit_keys);
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.clear", clear_keys);

    // Sections are separated by lines; an underscore marks the letter Alt opens it with.
    GMenu* edit_section = g_menu_new();
    g_menu_append(edit_section, "_Clear", "win.clear");
    GMenu* quit_section = g_menu_new();
    g_menu_append(quit_section, "_Quit", "app.quit");
    GMenu* file_menu = g_menu_new();
    g_menu_append_section(file_menu, NULL, G_MENU_MODEL(edit_section));
    g_menu_append_section(file_menu, NULL, G_MENU_MODEL(quit_section));

    GMenu* view_menu = g_menu_new();
    g_menu_append(view_menu, "_Wrap Lines", "win.wrap");

    GMenu* help_menu = g_menu_new();
    g_menu_append(help_menu, "_About", "app.about");

    GMenu* menu_bar = g_menu_new();
    g_menu_append_submenu(menu_bar, "_File", G_MENU_MODEL(file_menu));
    g_menu_append_submenu(menu_bar, "_View", G_MENU_MODEL(view_menu));
    g_menu_append_submenu(menu_bar, "_Help", G_MENU_MODEL(help_menu));
    gtk_application_set_menubar(GTK_APPLICATION(app), G_MENU_MODEL(menu_bar));

    // The menus hold references to what they contain.
    g_object_unref(edit_section);
    g_object_unref(quit_section);
    g_object_unref(file_menu);
    g_object_unref(view_menu);
    g_object_unref(help_menu);
    g_object_unref(menu_bar);
}

static void activate(GtkApplication* app, gpointer user_data)
{
    (void)user_data;
    // Starting the program again while it runs activates this copy: show its window.
    GtkWindow* existing = gtk_application_get_active_window(app);
    if (existing != NULL)
    {
        gtk_window_present(existing);
        return;
    }

    GtkWidget* window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "{{AppName}}");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    gtk_application_window_set_show_menubar(GTK_APPLICATION_WINDOW(window), TRUE);

    GtkWidget* text_view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD);
    GtkWidget* scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), text_view);
    gtk_window_set_child(GTK_WINDOW(window), scroller);

    // With no activate handler, a boolean action toggles its state when chosen.
    static const GActionEntry window_actions[] = {
        {.name = "clear", .activate = clear_activated},
        {.name = "wrap", .state = "true", .change_state = wrap_changed},
    };
    g_action_map_add_action_entries(G_ACTION_MAP(window), window_actions, G_N_ELEMENTS(window_actions), text_view);

    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char* argv[])
{
    GtkApplication* app = gtk_application_new("org.example.{{AppName:pascal}}", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "startup", G_CALLBACK(startup), NULL);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
