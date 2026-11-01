#include "editor/CppIndenter.h"

#include "core/CppStyle.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace {

std::string textBetween(const GtkTextIter* from, const GtkTextIter* to) {
    char* raw = gtk_text_iter_get_text(from, to);
    std::string out = raw ? raw : "";
    g_free(raw);
    return out;
}

// The text before `iter`, from at most 4000 lines up: enough to find the brackets still open there.
std::string textBefore(const GtkTextIter* iter) {
    GtkTextIter start = *iter;
    gtk_text_iter_set_line(&start, std::max(0, gtk_text_iter_get_line(iter) - 4000));
    return textBetween(&start, iter);
}

std::string indentUnit(GtkSourceView* view) {
    int width = static_cast<int>(gtk_source_view_get_indent_width(view));
    if (width <= 0) width = static_cast<int>(gtk_source_view_get_tab_width(view));
    if (!gtk_source_view_get_insert_spaces_instead_of_tabs(view)) return "\t";
    return std::string(static_cast<size_t>(std::max(1, width)), ' ');
}

// Replaces the whitespace at the start of `line` with `indent`.
void setIndent(GtkTextBuffer* buffer, int line, const std::string& indent) {
    GtkTextIter start, end;
    gtk_text_buffer_get_iter_at_line(buffer, &start, line);
    end = start;
    while (!gtk_text_iter_ends_line(&end) && (gtk_text_iter_get_char(&end) == ' ' || gtk_text_iter_get_char(&end) == '\t'))
        gtk_text_iter_forward_char(&end);
    if (textBetween(&start, &end) == indent) return;
    gtk_text_buffer_delete(buffer, &start, &end);
    gtk_text_buffer_insert(buffer, &start, indent.data(), static_cast<int>(indent.size()));
}

} // namespace

struct KevcCppIndenter {
    GObject parent_instance;
};
struct KevcCppIndenterClass {
    GObjectClass parent_class;
};

static void kevc_cpp_indenter_iface_init(GtkSourceIndenterInterface* iface);

G_DEFINE_TYPE_WITH_CODE(KevcCppIndenter, kevc_cpp_indenter, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(GTK_SOURCE_TYPE_INDENTER, kevc_cpp_indenter_iface_init))

static void kevc_cpp_indenter_class_init(KevcCppIndenterClass*) {}
static void kevc_cpp_indenter_init(KevcCppIndenter*) {}

static gboolean kevc_cpp_indenter_is_trigger(GtkSourceIndenter*, GtkSourceView*, const GtkTextIter*,
                                             GdkModifierType state, guint keyval) {
    if (state & (GDK_CONTROL_MASK | GDK_ALT_MASK)) return FALSE;
    return keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter || keyval == GDK_KEY_braceleft ||
           keyval == GDK_KEY_braceright || keyval == GDK_KEY_colon;
}

// Called with the key's text just inserted before `iter` (GtkSourceView inserts it, then asks).
static void kevc_cpp_indenter_indent(GtkSourceIndenter*, GtkSourceView* view, GtkTextIter* iter) {
    namespace cs = kevc::cppstyle;
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    GtkTextIter typedAt = *iter;
    if (!gtk_text_iter_backward_char(&typedAt)) return;
    const gunichar typed = gtk_text_iter_get_char(&typedAt);
    // The cursor's place, kept through the edits by a mark that moves with inserted text.
    GtkTextMark* cursor = gtk_text_buffer_create_mark(buffer, nullptr, iter, FALSE);
    const int line = gtk_text_iter_get_line(iter);
    GtkTextIter lineStart = *iter;
    gtk_text_iter_set_line_offset(&lineStart, 0);

    if (typed == '\n') {
        GtkTextIter lineEnd = *iter;
        if (!gtk_text_iter_ends_line(&lineEnd)) gtk_text_iter_forward_to_line_end(&lineEnd);
        const std::string indent = cs::newLineIndent(textBefore(&lineStart), textBetween(iter, &lineEnd), indentUnit(view));
        setIndent(buffer, line, indent);
        // The cursor goes after the indentation (whitespace split off the old line went with it).
        GtkTextIter at;
        gtk_text_buffer_get_iter_at_line_offset(buffer, &at, line, static_cast<int>(g_utf8_strlen(indent.c_str(), -1)));
        gtk_text_buffer_move_mark(buffer, cursor, &at);
    } else if (typed == ':') {
        if (auto indent = cs::accessSpecifierIndent(textBefore(&lineStart), textBetween(&lineStart, iter)))
            setIndent(buffer, line, *indent);
    } else if (typed == '{') {
        if (auto replaced = cs::braceOnOwnLine(textBetween(&lineStart, iter))) {
            GtkTextIter from = lineStart, to = *iter;
            gtk_text_buffer_delete(buffer, &from, &to);
            gtk_text_buffer_insert(buffer, &from, replaced->data(), static_cast<int>(replaced->size()));
            gtk_text_buffer_move_mark(buffer, cursor, &from); // after the brace
        }
    } else if (typed == '}') {
        const std::string before = textBefore(&typedAt);
        const cs::Closing closing = cs::closingBrace(before, textBetween(&lineStart, &typedAt));
        if (closing.indent) setIndent(buffer, line, *closing.indent);
        if (closing.semicolon) {
            GtkTextIter at;
            gtk_text_buffer_get_iter_at_mark(buffer, &at, cursor);
            if (gtk_text_iter_get_char(&at) != ';') {
                gtk_text_buffer_insert(buffer, &at, ";", 1); // the mark moves past it
            }
        }
    }
    gtk_text_buffer_get_iter_at_mark(buffer, iter, cursor);
    gtk_text_buffer_place_cursor(buffer, iter);
    gtk_text_buffer_delete_mark(buffer, cursor);
}

static void kevc_cpp_indenter_iface_init(GtkSourceIndenterInterface* iface) {
    iface->is_trigger = kevc_cpp_indenter_is_trigger;
    iface->indent = kevc_cpp_indenter_indent;
}

namespace kevc {

GtkSourceIndenter* newCppIndenter() {
    return GTK_SOURCE_INDENTER(g_object_new(kevc_cpp_indenter_get_type(), nullptr));
}

bool isCppLanguage(GtkSourceLanguage* language) {
    if (!language) return false;
    const char* id = gtk_source_language_get_id(language);
    for (const char* c : {"c", "cpp", "chdr", "cpphdr", "objc"})
        if (std::strcmp(id, c) == 0) return true;
    return false;
}

} // namespace kevc
