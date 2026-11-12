---
order: 30
section: dialogs
description: A GtkAlertDialog that asks Save, Discard or Cancel, and the callback that gets the answer
packages: gtk4
includes: <gtk/gtk.h>
---
// Asks whether to save before closing; the box doesn't block, on_save_question_answered gets the choice.
// Call it with the window it belongs to:  ask_save_changes(GTK_WINDOW(window));
// A plain message:  GtkAlertDialog* d = gtk_alert_dialog_new("Saved."); gtk_alert_dialog_show(d, parent);
//                   g_object_unref(d);
static void on_save_question_answered(GObject* source, GAsyncResult* result, gpointer user_data)
{
    (void)user_data;
    GError* error = NULL;
    int button = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, &error);
    if (error != NULL)
    {
        g_error_free(error); // dismissed some other way
        return;
    }
    switch (button)
    {
    case 1: // Discard
        break;
    case 2: // Save
        {{cursor}}
        break;
    default: // Cancel
        break;
    }
}

static void ask_save_changes(GtkWindow* parent)
{
    GtkAlertDialog* dialog = gtk_alert_dialog_new("Save changes before closing?");
    gtk_alert_dialog_set_detail(dialog, "If you don't save, your changes will be lost.");
    const char* buttons[] = {"Cancel", "Discard", "Save", NULL};
    gtk_alert_dialog_set_buttons(dialog, buttons);
    gtk_alert_dialog_set_cancel_button(dialog, 0);  // Escape and the window's close button choose Cancel
    gtk_alert_dialog_set_default_button(dialog, 2); // Enter chooses Save
    gtk_alert_dialog_choose(dialog, parent, NULL, on_save_question_answered, NULL);
    g_object_unref(dialog); // the running dialog keeps its own reference
}
