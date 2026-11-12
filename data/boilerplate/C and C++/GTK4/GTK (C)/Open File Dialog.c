---
order: 31
section: dialogs
description: A GtkFileDialog that lets the user pick a text file, and the callback that reads it
packages: gtk4
includes: <gtk/gtk.h>
---
// Lets the user pick a file, then reads it; the dialog doesn't block, on_file_chosen runs afterwards.
// Call it with the window it belongs to:  open_file(GTK_WINDOW(window));
static void on_file_chosen(GObject* source, GAsyncResult* result, gpointer user_data)
{
    GtkWindow* parent = user_data;
    GError* error = NULL;
    GFile* file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result, &error);
    if (file == NULL)
    {
        g_error_free(error); // cancelled
        g_object_unref(parent);
        return;
    }

    char* contents = NULL;
    gsize length = 0;
    if (g_file_load_contents(file, NULL, &contents, &length, NULL, &error))
    {
        {{cursor}}
        g_free(contents);
    }
    else
    {
        GtkAlertDialog* alert = gtk_alert_dialog_new("The file can't be opened");
        gtk_alert_dialog_set_detail(alert, error->message);
        gtk_alert_dialog_show(alert, parent);
        g_object_unref(alert);
        g_error_free(error);
    }
    g_object_unref(file);
    g_object_unref(parent);
}

static void open_file(GtkWindow* parent)
{
    GtkFileFilter* text_files = gtk_file_filter_new();
    gtk_file_filter_set_name(text_files, "Text files");
    gtk_file_filter_add_mime_type(text_files, "text/plain");
    GtkFileFilter* all_files = gtk_file_filter_new();
    gtk_file_filter_set_name(all_files, "All files");
    gtk_file_filter_add_pattern(all_files, "*");

    GListStore* filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, text_files);
    g_list_store_append(filters, all_files);

    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Open File");
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    // The reference keeps the window valid for on_file_chosen's error message, whatever happens meanwhile.
    gtk_file_dialog_open(dialog, parent, NULL, on_file_chosen, g_object_ref(parent));

    g_object_unref(filters);
    g_object_unref(text_files);
    g_object_unref(all_files);
    g_object_unref(dialog);
}
