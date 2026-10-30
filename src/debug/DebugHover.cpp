#include "debug/DebugHover.h"

#include <memory>

struct KevcDebugHover {
    GObject parent_instance;
    kevc::DebugHoverHandler* handler;
};
struct KevcDebugHoverClass {
    GObjectClass parent_class;
};

static void kevc_debug_hover_iface_init(GtkSourceHoverProviderInterface* iface);

G_DEFINE_TYPE_WITH_CODE(KevcDebugHover, kevc_debug_hover, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(GTK_SOURCE_TYPE_HOVER_PROVIDER, kevc_debug_hover_iface_init))

static void kevc_debug_hover_finalize(GObject* object) {
    delete reinterpret_cast<KevcDebugHover*>(object)->handler;
    G_OBJECT_CLASS(kevc_debug_hover_parent_class)->finalize(object);
}

static void kevc_debug_hover_class_init(KevcDebugHoverClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = kevc_debug_hover_finalize;
}

static void kevc_debug_hover_init(KevcDebugHover* self) {
    self->handler = nullptr;
}

static void debug_hover_populate_async(GtkSourceHoverProvider* provider, GtkSourceHoverContext* context,
                                       GtkSourceHoverDisplay* display, GCancellable* cancellable,
                                       GAsyncReadyCallback callback, gpointer userData) {
    auto* self = reinterpret_cast<KevcDebugHover*>(provider);
    GTask* task = g_task_new(provider, cancellable, callback, userData);
    GtkTextIter iter;
    // Released when the callback is dropped, whether or not the handler ever calls it.
    std::shared_ptr<GtkSourceHoverDisplay> keep(GTK_SOURCE_HOVER_DISPLAY(g_object_ref(display)), g_object_unref);
    const bool handled = gtk_source_hover_context_get_iter(context, &iter) && self->handler &&
                         (*self->handler)(iter, [task, keep](GtkWidget* widget) {
                             if (widget) {
                                 gtk_source_hover_display_append(keep.get(), widget);
                                 g_task_return_boolean(task, TRUE);
                             } else {
                                 g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No value");
                             }
                             g_object_unref(task);
                         });
    if (!handled) {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Not debugging");
        g_object_unref(task);
    }
}

static gboolean debug_hover_populate_finish(GtkSourceHoverProvider*, GAsyncResult* result, GError** error) {
    return g_task_propagate_boolean(G_TASK(result), error);
}

static void kevc_debug_hover_iface_init(GtkSourceHoverProviderInterface* iface) {
    iface->populate_async = debug_hover_populate_async;
    iface->populate_finish = debug_hover_populate_finish;
}

namespace kevc {

GtkSourceHoverProvider* newDebugHoverProvider(DebugHoverHandler handler) {
    auto* self = static_cast<KevcDebugHover*>(g_object_new(kevc_debug_hover_get_type(), nullptr));
    self->handler = new DebugHoverHandler(std::move(handler));
    return GTK_SOURCE_HOVER_PROVIDER(self);
}

} // namespace kevc
