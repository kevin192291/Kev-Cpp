#include "lsp/LspBridges.h"

#include "core/Icons.h"
#include "core/Lsp.h"
#include "lsp/LspManager.h"

#include <algorithm>
#include <optional>
#include <vector>

namespace kevc {

namespace {

using json = lsp::json;

std::string markdownOf(const json& documentation) {
    if (documentation.is_string()) return documentation.get<std::string>();
    if (documentation.is_object()) return documentation.value("value", "");
    return {};
}

void setMarkupOrText(GtkLabel* label, const std::string& markup) {
    if (pango_parse_markup(markup.c_str(), -1, 0, nullptr, nullptr, nullptr, nullptr)) gtk_label_set_markup(label, markup.c_str());
    else gtk_label_set_text(label, markup.c_str());
}

} // namespace

} // namespace kevc

// -------------------------------------------------------------------------------------------------
// GObject types. They use C-style naming because G_DEFINE_TYPE generates C symbols from it.

using kevc::lsp::json;

struct KevcCompletionItem {
    GObject parent_instance;
    json* item;          // the raw LSP CompletionItem
    std::string* label;  // shown text
    std::string* filter; // matched against what the user typed
    std::string* sortText;
    guint index;         // position in the server's list (tie-breaker)
    guint priority;      // match bucket for the current word: 0 exact prefix, 1 any-case prefix, 2 fuzzy
    guint extraChars;    // characters beyond what was typed (closest match first within a bucket)
};
struct KevcCompletionItemClass {
    GObjectClass parent_class;
};

static void kevc_completion_item_proposal_init(GtkSourceCompletionProposalInterface*) {}

G_DEFINE_TYPE_WITH_CODE(KevcCompletionItem, kevc_completion_item, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(GTK_SOURCE_TYPE_COMPLETION_PROPOSAL, kevc_completion_item_proposal_init))

static void kevc_completion_item_finalize(GObject* object) {
    auto* self = reinterpret_cast<KevcCompletionItem*>(object);
    delete self->item;
    delete self->label;
    delete self->filter;
    delete self->sortText;
    G_OBJECT_CLASS(kevc_completion_item_parent_class)->finalize(object);
}

static void kevc_completion_item_class_init(KevcCompletionItemClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = kevc_completion_item_finalize;
}

static void kevc_completion_item_init(KevcCompletionItem* self) {
    self->item = new json();
    self->label = new std::string();
    self->filter = new std::string();
    self->sortText = new std::string();
}

static KevcCompletionItem* kevc_completion_item_new(const json& item, guint index) {
    auto* self = static_cast<KevcCompletionItem*>(g_object_new(kevc_completion_item_get_type(), nullptr));
    *self->item = item;
    std::string label = item.value("label", "");
    // clangd marks items that also insert an #include with a leading bullet or space.
    while (!label.empty() && (label.front() == ' ' || label.compare(0, 3, "•") == 0))
        label.erase(0, label.front() == ' ' ? 1 : 3);
    *self->label = label;
    *self->filter = item.value("filterText", label);
    *self->sortText = item.value("sortText", label);
    self->index = index;
    return self;
}

// --- Completion provider -------------------------------------------------------------------------

struct KevcCompletionProvider {
    GObject parent_instance;
    kevc::LspManager* manager;
    kevc::EditorTab* tab;
};
struct KevcCompletionProviderClass {
    GObjectClass parent_class;
};

static void kevc_completion_provider_iface_init(GtkSourceCompletionProviderInterface* iface);

G_DEFINE_TYPE_WITH_CODE(KevcCompletionProvider, kevc_completion_provider, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(GTK_SOURCE_TYPE_COMPLETION_PROVIDER, kevc_completion_provider_iface_init))

static void kevc_completion_provider_class_init(KevcCompletionProviderClass*) {}
static void kevc_completion_provider_init(KevcCompletionProvider*) {}

// populate() returns SortListModel(FilterListModel(all items)). refilter() only updates the word
// and re-runs filter and sorter in place, which is how GtkSourceView expects providers to filter.
struct FilterState {
    std::string word;     // as typed
    std::string casefold; // for fuzzy matching
    // The server said its list was cut short: as the word grows, ask it again rather than only
    // filtering what we have (otherwise e.g. std::cout can be missing from a long std:: list).
    bool incomplete = false;
    GListStore* all = nullptr; // owned by the filter model
    kevc::LspManager* manager = nullptr;
    kevc::EditorTab* tab = nullptr;
    unsigned generation = 0;
};
constexpr const char* kFilterState = "kevc-filter-state";

static gboolean matchProposal(gpointer object, gpointer data) {
    auto* item = static_cast<KevcCompletionItem*>(object);
    auto* state = static_cast<FilterState*>(data);
    if (state->word.empty()) {
        item->priority = 0;
        item->extraChars = 0;
        return TRUE;
    }
    guint score = 0;
    if (!gtk_source_completion_fuzzy_match(item->filter->c_str(), state->casefold.c_str(), &score)) return FALSE;
    // Typed-prefix matches first; within each group keep the server's own relevance order (sortText),
    // which knows far more than a fuzzy score (scope, use frequency, whether an #include is needed).
    if (g_str_has_prefix(item->filter->c_str(), state->word.c_str())) item->priority = 0;
    else if (g_ascii_strncasecmp(item->filter->c_str(), state->word.c_str(), state->word.size()) == 0) item->priority = 1;
    else item->priority = 2;
    // Prefer the closest prefix match: for "cou", cout before count before counted_iterator. clangd
    // ranks symbols that still need an #include very low, which buried std::cout in new files.
    item->extraChars = item->priority < 2 ? static_cast<guint>(item->filter->size() - state->word.size()) : 0;
    return TRUE;
}

static int compareProposals(gconstpointer a, gconstpointer b, gpointer) {
    auto* x = static_cast<const KevcCompletionItem*>(a);
    auto* y = static_cast<const KevcCompletionItem*>(b);
    if (x->priority != y->priority) return x->priority < y->priority ? GTK_ORDERING_SMALLER : GTK_ORDERING_LARGER;
    if (x->extraChars != y->extraChars) return x->extraChars < y->extraChars ? GTK_ORDERING_SMALLER : GTK_ORDERING_LARGER;
    if (const int c = x->sortText->compare(*y->sortText)) return c < 0 ? GTK_ORDERING_SMALLER : GTK_ORDERING_LARGER;
    return x->index < y->index ? GTK_ORDERING_SMALLER : (x->index > y->index ? GTK_ORDERING_LARGER : GTK_ORDERING_EQUAL);
}

static void setFilterWord(FilterState* state, const char* word) {
    state->word = word ? word : "";
    char* casefold = g_utf8_casefold(state->word.c_str(), -1);
    state->casefold = casefold;
    g_free(casefold);
}

static void fillStore(GListStore* store, const json& items) {
    std::vector<gpointer> proposals;
    guint index = 0;
    for (const auto& item : items) proposals.push_back(kevc_completion_item_new(item, index++));
    g_list_store_splice(store, 0, g_list_model_get_n_items(G_LIST_MODEL(store)), proposals.data(),
                        static_cast<guint>(proposals.size()));
    for (gpointer p : proposals) g_object_unref(p);
}

// Wraps `all` (taking ownership) in the filtered, sorted model returned to GtkSourceView.
static GListModel* proposalModel(GListStore* all, const char* word, bool incomplete, kevc::LspManager* manager,
                                 kevc::EditorTab* tab) {
    auto* state = new FilterState();
    setFilterWord(state, word);
    state->incomplete = incomplete;
    state->all = all;
    state->manager = manager;
    state->tab = tab;
    GtkCustomFilter* filter = gtk_custom_filter_new(matchProposal, state, [](gpointer p) { delete static_cast<FilterState*>(p); });
    GtkFilterListModel* filtered = gtk_filter_list_model_new(G_LIST_MODEL(all), GTK_FILTER(filter));
    GtkSortListModel* sorted = gtk_sort_list_model_new(G_LIST_MODEL(filtered), GTK_SORTER(gtk_custom_sorter_new(compareProposals, nullptr, nullptr)));
    g_object_set_data(G_OBJECT(sorted), kFilterState, state); // owned by the filter
    return G_LIST_MODEL(sorted);
}

static char* provider_get_title(GtkSourceCompletionProvider*) {
    return g_strdup("IntelliSense");
}

static int provider_get_priority(GtkSourceCompletionProvider*, GtkSourceCompletionContext*) {
    return 200;
}

static gboolean provider_is_trigger(GtkSourceCompletionProvider* provider, const GtkTextIter*, gunichar ch) {
    auto* self = reinterpret_cast<KevcCompletionProvider*>(provider);
    char utf8[8] = {};
    g_unichar_to_utf8(ch, utf8);
    for (const auto& t : self->manager->triggerCharacters(self->tab))
        if (t == utf8) return TRUE;
    return FALSE;
}

static gboolean provider_key_activates(GtkSourceCompletionProvider*, GtkSourceCompletionContext*,
                                       GtkSourceCompletionProposal*, guint, GdkModifierType) {
    return FALSE;
}

static void provider_populate_async(GtkSourceCompletionProvider* provider, GtkSourceCompletionContext* context,
                                    GCancellable* cancellable, GAsyncReadyCallback callback, gpointer userData) {
    auto* self = reinterpret_cast<KevcCompletionProvider*>(provider);
    GTask* task = g_task_new(provider, cancellable, callback, userData);

    GtkTextIter begin, end;
    gtk_source_completion_context_get_bounds(context, &begin, &end);

    // A trigger character right before an empty word (e.g. "v." or "p->") is sent to the server.
    std::optional<std::string> trigger;
    if (gtk_text_iter_equal(&begin, &end) && !gtk_text_iter_starts_line(&end)) {
        GtkTextIter prev = end;
        gtk_text_iter_backward_char(&prev);
        char utf8[8] = {};
        g_unichar_to_utf8(gtk_text_iter_get_char(&prev), utf8);
        for (const auto& t : self->manager->triggerCharacters(self->tab))
            if (t == utf8) trigger = t;
    }

    g_object_ref(context);
    auto* manager = self->manager;
    auto* tab = self->tab;
    const bool sent = self->manager->completion(self->tab, end, trigger, [task, context, manager, tab](const json& items, bool incomplete) {
        GListStore* all = g_list_store_new(kevc_completion_item_get_type());
        fillStore(all, items);
        // Filter by whatever has been typed while the server was answering.
        char* word = gtk_source_completion_context_get_word(context);
        g_task_return_pointer(task, proposalModel(all, word, incomplete, manager, tab), g_object_unref);
        g_free(word);
        g_object_unref(context);
        g_object_unref(task);
    });
    if (!sent) {
        g_task_return_pointer(task, g_list_store_new(kevc_completion_item_get_type()), g_object_unref);
        g_object_unref(context);
        g_object_unref(task);
    }
}

static GListModel* provider_populate_finish(GtkSourceCompletionProvider*, GAsyncResult* result, GError** error) {
    return static_cast<GListModel*>(g_task_propagate_pointer(G_TASK(result), error));
}

static void provider_refilter(GtkSourceCompletionProvider*, GtkSourceCompletionContext* context, GListModel* model) {
    if (!GTK_IS_SORT_LIST_MODEL(model)) return;
    auto* state = static_cast<FilterState*>(g_object_get_data(G_OBJECT(model), kFilterState));
    if (!state) return;
    char* word = gtk_source_completion_context_get_word(context);
    setFilterWord(state, word);
    g_free(word);
    auto* filtered = GTK_FILTER_LIST_MODEL(gtk_sort_list_model_get_model(GTK_SORT_LIST_MODEL(model)));
    gtk_filter_changed(gtk_filter_list_model_get_filter(filtered), GTK_FILTER_CHANGE_DIFFERENT);
    gtk_sorter_changed(gtk_sort_list_model_get_sorter(GTK_SORT_LIST_MODEL(model)), GTK_SORTER_CHANGE_DIFFERENT);

    if (!state->incomplete || state->word.empty() || !state->manager) return;
    // Re-ask the server with the longer word and swap the results in place when they arrive.
    GtkTextIter begin, end;
    gtk_source_completion_context_get_bounds(context, &begin, &end);
    const unsigned generation = ++state->generation;
    g_object_ref(model); // keeps the store and filter state alive until the answer arrives
    const bool sent = state->manager->completion(state->tab, end, std::nullopt, [model, state, generation](const json& items, bool incomplete) {
        if (generation == state->generation) {
            state->incomplete = incomplete;
            fillStore(state->all, items);
            auto* f = GTK_FILTER_LIST_MODEL(gtk_sort_list_model_get_model(GTK_SORT_LIST_MODEL(model)));
            gtk_filter_changed(gtk_filter_list_model_get_filter(f), GTK_FILTER_CHANGE_DIFFERENT);
            gtk_sorter_changed(gtk_sort_list_model_get_sorter(GTK_SORT_LIST_MODEL(model)), GTK_SORTER_CHANGE_DIFFERENT);
        }
        g_object_unref(model);
    });
    if (!sent) g_object_unref(model);
}

static void provider_display(GtkSourceCompletionProvider*, GtkSourceCompletionContext* context,
                             GtkSourceCompletionProposal* proposal, GtkSourceCompletionCell* cell) {
    auto* item = reinterpret_cast<KevcCompletionItem*>(proposal);
    const json& raw = *item->item;
    const json details = raw.value("labelDetails", json::object());
    switch (gtk_source_completion_cell_get_column(cell)) {
    case GTK_SOURCE_COMPLETION_COLUMN_ICON:
        gtk_source_completion_cell_set_icon_name(cell, kevc::icons::forCompletionKind(raw.value("kind", 1)).c_str());
        break;
    case GTK_SOURCE_COMPLETION_COLUMN_TYPED_TEXT: {
        char* word = gtk_source_completion_context_get_word(context);
        char* casefold = g_utf8_casefold(word ? word : "", -1);
        PangoAttrList* attrs = gtk_source_completion_fuzzy_highlight(item->label->c_str(), casefold);
        gtk_source_completion_cell_set_text_with_attributes(cell, item->label->c_str(), attrs);
        if (attrs) pango_attr_list_unref(attrs);
        g_free(casefold);
        g_free(word);
        break;
    }
    case GTK_SOURCE_COMPLETION_COLUMN_AFTER:
        gtk_source_completion_cell_set_text(cell, details.value("detail", "").c_str());
        break;
    case GTK_SOURCE_COMPLETION_COLUMN_COMMENT:
        gtk_source_completion_cell_set_text(cell, details.value("description", raw.value("detail", "")).c_str());
        break;
    case GTK_SOURCE_COMPLETION_COLUMN_DETAILS: {
        std::string md = kevc::markdownOf(raw.value("documentation", json()));
        if (md.empty()) md = raw.value("detail", "");
        gtk_source_completion_cell_set_markup(cell, kevc::lsp::markdownToPango(md).c_str());
        break;
    }
    default:
        gtk_source_completion_cell_set_text(cell, nullptr);
        break;
    }
}

static void provider_activate(GtkSourceCompletionProvider*, GtkSourceCompletionContext* context,
                              GtkSourceCompletionProposal* proposal) {
    auto* item = reinterpret_cast<KevcCompletionItem*>(proposal);
    const json& raw = *item->item;
    GtkTextBuffer* buffer = GTK_TEXT_BUFFER(gtk_source_completion_context_get_buffer(context));
    GtkTextIter begin, end;
    gtk_source_completion_context_get_bounds(context, &begin, &end);

    std::string newText = raw.value("insertText", *item->label);
    if (raw.contains("textEdit")) {
        const json& edit = raw["textEdit"];
        const json& range = edit.contains("range") ? edit["range"] : edit["replace"];
        newText = edit.value("newText", newText);
        begin = kevc::LspManager::iterAt(buffer, range["start"].value("line", 0), range["start"].value("character", 0));
        // The user may have typed more since the request: replace up to the cursor at least.
        GtkTextIter editEnd = kevc::LspManager::iterAt(buffer, range["end"].value("line", 0), range["end"].value("character", 0));
        if (gtk_text_iter_compare(&editEnd, &end) > 0) end = editEnd;
    }

    // Remember additional edits (e.g. clangd's #include insertion) as marks so they survive the main edit.
    struct Pending {
        GtkTextMark* start;
        GtkTextMark* end;
        std::string text;
    };
    std::vector<Pending> extra;
    for (const auto& e : raw.value("additionalTextEdits", json::array())) {
        GtkTextIter s = kevc::LspManager::iterAt(buffer, e["range"]["start"].value("line", 0), e["range"]["start"].value("character", 0));
        GtkTextIter t = kevc::LspManager::iterAt(buffer, e["range"]["end"].value("line", 0), e["range"]["end"].value("character", 0));
        extra.push_back({gtk_text_buffer_create_mark(buffer, nullptr, &s, TRUE),
                         gtk_text_buffer_create_mark(buffer, nullptr, &t, FALSE), e.value("newText", "")});
    }

    gtk_text_buffer_begin_user_action(buffer);
    GtkTextMark* insertAt = gtk_text_buffer_create_mark(buffer, nullptr, &begin, TRUE);
    gtk_text_buffer_delete(buffer, &begin, &end);
    gtk_text_buffer_get_iter_at_mark(buffer, &begin, insertAt);
    gtk_text_buffer_insert(buffer, &begin, newText.c_str(), -1);
    gtk_text_buffer_delete_mark(buffer, insertAt);
    for (auto& p : extra) {
        GtkTextIter s, t;
        gtk_text_buffer_get_iter_at_mark(buffer, &s, p.start);
        gtk_text_buffer_get_iter_at_mark(buffer, &t, p.end);
        gtk_text_buffer_delete(buffer, &s, &t);
        gtk_text_buffer_get_iter_at_mark(buffer, &s, p.start);
        gtk_text_buffer_insert(buffer, &s, p.text.c_str(), -1);
        gtk_text_buffer_delete_mark(buffer, p.start);
        gtk_text_buffer_delete_mark(buffer, p.end);
    }
    gtk_text_buffer_end_user_action(buffer);
}

static void kevc_completion_provider_iface_init(GtkSourceCompletionProviderInterface* iface) {
    iface->get_title = provider_get_title;
    iface->get_priority = provider_get_priority;
    iface->is_trigger = provider_is_trigger;
    iface->key_activates = provider_key_activates;
    iface->populate_async = provider_populate_async;
    iface->populate_finish = provider_populate_finish;
    iface->refilter = provider_refilter;
    iface->display = provider_display;
    iface->activate = provider_activate;
}

// --- Hover provider ------------------------------------------------------------------------------

struct KevcHoverProvider {
    GObject parent_instance;
    kevc::LspManager* manager;
    kevc::EditorTab* tab;
};
struct KevcHoverProviderClass {
    GObjectClass parent_class;
};

static void kevc_hover_provider_iface_init(GtkSourceHoverProviderInterface* iface);

G_DEFINE_TYPE_WITH_CODE(KevcHoverProvider, kevc_hover_provider, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(GTK_SOURCE_TYPE_HOVER_PROVIDER, kevc_hover_provider_iface_init))

static void kevc_hover_provider_class_init(KevcHoverProviderClass*) {}
static void kevc_hover_provider_init(KevcHoverProvider*) {}

static void hover_populate_async(GtkSourceHoverProvider* provider, GtkSourceHoverContext* context,
                                 GtkSourceHoverDisplay* display, GCancellable* cancellable,
                                 GAsyncReadyCallback callback, gpointer userData) {
    auto* self = reinterpret_cast<KevcHoverProvider*>(provider);
    GTask* task = g_task_new(provider, cancellable, callback, userData);
    GtkTextIter iter;
    if (!gtk_source_hover_context_get_iter(context, &iter)) {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No position");
        g_object_unref(task);
        return;
    }
    g_object_ref(display);
    self->manager->hover(self->tab, iter, [task, display](const std::string& markup) {
        if (markup.empty()) {
            g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Nothing to show");
        } else {
            GtkWidget* label = gtk_label_new(nullptr);
            kevc::setMarkupOrText(GTK_LABEL(label), markup);
            gtk_label_set_wrap(GTK_LABEL(label), TRUE);
            gtk_label_set_max_width_chars(GTK_LABEL(label), 90);
            gtk_label_set_selectable(GTK_LABEL(label), TRUE);
            gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
            gtk_widget_set_margin_start(label, 8);
            gtk_widget_set_margin_end(label, 8);
            gtk_widget_set_margin_top(label, 6);
            gtk_widget_set_margin_bottom(label, 6);
            gtk_source_hover_display_append(display, label);
            g_task_return_boolean(task, TRUE);
        }
        g_object_unref(display);
        g_object_unref(task);
    });
}

static gboolean hover_populate_finish(GtkSourceHoverProvider*, GAsyncResult* result, GError** error) {
    return g_task_propagate_boolean(G_TASK(result), error);
}

static void kevc_hover_provider_iface_init(GtkSourceHoverProviderInterface* iface) {
    iface->populate_async = hover_populate_async;
    iface->populate_finish = hover_populate_finish;
}

namespace kevc {

GtkSourceCompletionProvider* newLspCompletionProvider(LspManager* manager, EditorTab* tab) {
    auto* self = static_cast<KevcCompletionProvider*>(g_object_new(kevc_completion_provider_get_type(), nullptr));
    self->manager = manager;
    self->tab = tab;
    return GTK_SOURCE_COMPLETION_PROVIDER(self);
}

GtkSourceHoverProvider* newLspHoverProvider(LspManager* manager, EditorTab* tab) {
    auto* self = static_cast<KevcHoverProvider*>(g_object_new(kevc_hover_provider_get_type(), nullptr));
    self->manager = manager;
    self->tab = tab;
    return GTK_SOURCE_HOVER_PROVIDER(self);
}

} // namespace kevc
