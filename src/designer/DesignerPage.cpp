#include "designer/DesignerPage.h"

#include "designer/DesignerTypes.h"
#include "editor/EditorTab.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace kevc {

namespace fs = std::filesystem;
using designer::classKind;
using designer::typeOf;
using ui::Node;
using ui::UiDocument;

namespace {

// How GtkBuilder reads booleans.
bool truthy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s == "true" || s == "yes" || s == "t" || s == "y" || s == "1";
}

std::string numberText(double v, bool integer) {
    if (integer) return std::to_string(std::llround(v));
    char buffer[G_ASCII_DTOSTR_BUF_SIZE];
    return g_ascii_formatd(buffer, sizeof buffer, "%.6g", v);
}

std::optional<double> parseNumber(const std::string& s) {
    if (s.empty()) return std::nullopt;
    char* end = nullptr;
    const double v = g_ascii_strtod(s.c_str(), &end);
    if (end == s.c_str()) return std::nullopt;
    return v;
}

// A GValue as a .ui file writes it.
std::string valueText(const GValue* v) {
    const GType type = G_VALUE_TYPE(v);
    switch (G_TYPE_FUNDAMENTAL(type)) {
    case G_TYPE_BOOLEAN: return g_value_get_boolean(v) ? "True" : "False";
    case G_TYPE_INT: return std::to_string(g_value_get_int(v));
    case G_TYPE_UINT: return std::to_string(g_value_get_uint(v));
    case G_TYPE_LONG: return std::to_string(g_value_get_long(v));
    case G_TYPE_ULONG: return std::to_string(g_value_get_ulong(v));
    case G_TYPE_INT64: return std::to_string(g_value_get_int64(v));
    case G_TYPE_UINT64: return std::to_string(g_value_get_uint64(v));
    case G_TYPE_FLOAT: return numberText(g_value_get_float(v), false);
    case G_TYPE_DOUBLE: return numberText(g_value_get_double(v), false);
    case G_TYPE_STRING: {
        const char* s = g_value_get_string(v);
        return s ? s : "";
    }
    case G_TYPE_ENUM: {
        auto* klass = G_ENUM_CLASS(g_type_class_ref(type));
        const GEnumValue* e = g_enum_get_value(klass, g_value_get_enum(v));
        std::string out = e ? e->value_nick : std::to_string(g_value_get_enum(v));
        g_type_class_unref(klass);
        return out;
    }
    case G_TYPE_FLAGS: {
        auto* klass = G_FLAGS_CLASS(g_type_class_ref(type));
        std::string out;
        guint rest = g_value_get_flags(v);
        while (rest) {
            const GFlagsValue* f = g_flags_get_first_value(klass, rest);
            if (!f || f->value == 0) break;
            if (!out.empty()) out += '|';
            out += f->value_nick;
            rest &= ~f->value;
        }
        g_type_class_unref(klass);
        return out;
    }
    default: return {};
    }
}

std::string defaultText(GParamSpec* spec) {
    const GValue* v = g_param_spec_get_default_value(spec);
    return v ? valueText(v) : std::string();
}

std::string withoutMnemonics(const char* text) {
    std::string out;
    for (const char* p = text; *p; ++p) {
        if (*p == '_') {
            if (p[1] == '_') out += *++p;
            continue;
        }
        out += *p;
    }
    return out;
}

// Properties that hold a child widget. They're the design's structure (the outline shows what's in
// them), not values to type in.
bool widgetSlot(GParamSpec* spec) {
    if (!g_type_is_a(G_PARAM_SPEC_VALUE_TYPE(spec), GTK_TYPE_WIDGET)) return false;
    static const char* const slots[] = {"child", "titlebar", "title-widget", "label-widget", "start-child",
                                        "end-child", "center-widget", "start-widget", "end-widget", "popover",
                                        "content", "icon-widget", "overlay-child"};
    return std::any_of(std::begin(slots), std::end(slots), [&](const char* s) { return std::strcmp(s, spec->name) == 0; });
}

Gtk::Label& sectionTitle(const Glib::ustring& text) {
    auto* label = Gtk::make_managed<Gtk::Label>();
    label->set_markup("<b>" + Glib::Markup::escape_text(text) + "</b>");
    label->set_xalign(0);
    label->set_margin_top(10);
    label->set_margin_bottom(4);
    return *label;
}

} // namespace

DesignerPage::DesignerPage(const fs::path& file) : Gtk::Box(Gtk::Orientation::VERTICAL, 0), path_(file) {
    const std::string contents = Glib::file_get_contents(file.string());
    if (!g_utf8_validate(contents.data(), static_cast<gssize>(contents.size()), nullptr))
        throw std::runtime_error(file.filename().string() + " is not valid UTF-8 text");
    savedText_ = contents;
    add_css_class("kevc-designer");

    // The XML view.
    xmlBuffer_ = gtk_source_buffer_new(nullptr);
    gtk_source_buffer_set_language(
        xmlBuffer_, gtk_source_language_manager_get_language(gtk_source_language_manager_get_default(), "xml"));
    gtk_source_buffer_set_style_scheme(xmlBuffer_, preferredStyleScheme());
    gtk_source_buffer_set_highlight_matching_brackets(xmlBuffer_, TRUE);
    xmlView_ = GTK_SOURCE_VIEW(gtk_source_view_new_with_buffer(xmlBuffer_));
    gtk_source_view_set_show_line_numbers(xmlView_, TRUE);
    gtk_source_view_set_highlight_current_line(xmlView_, TRUE);
    gtk_source_view_set_auto_indent(xmlView_, TRUE);
    gtk_source_view_set_tab_width(xmlView_, 2);
    gtk_source_view_set_insert_spaces_instead_of_tabs(xmlView_, TRUE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(xmlView_), TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(xmlView_), 4);
    gtk_widget_add_css_class(GTK_WIDGET(xmlView_), "kevc-editor");
    xmlScroll_.set_child(*Gtk::manage(Glib::wrap(GTK_TEXT_VIEW(xmlView_))));
    setBufferText(contents);
    g_signal_connect(xmlBuffer_, "changed", G_CALLBACK(+[](GtkTextBuffer*, gpointer self) {
                         static_cast<DesignerPage*>(self)->updateTitle();
                     }),
                     this);

    // Design | XML, and a line for messages.
    designButton_.set_label("Design");
    xmlButton_.set_label("XML");
    xmlButton_.set_group(designButton_);
    designButton_.set_active(true);
    designButton_.set_tooltip_text("The design, with its outline and properties");
    xmlButton_.set_tooltip_text("The .ui file as text");
    designButton_.signal_toggled().connect([this] {
        if (!syncing_ && designButton_.get_active()) showView(View::Design);
    });
    xmlButton_.signal_toggled().connect([this] {
        if (!syncing_ && xmlButton_.get_active()) showView(View::Xml);
    });
    viewSwitch_.add_css_class("linked");
    viewSwitch_.append(designButton_);
    viewSwitch_.append(xmlButton_);
    message_.set_hexpand(true);
    message_.set_xalign(0);
    message_.set_ellipsize(Pango::EllipsizeMode::END);
    message_.set_selectable(true);
    bar_.add_css_class("kevc-designer-bar");
    bar_.set_margin(4);
    bar_.set_margin_start(6);
    auto codeMenu = Gio::Menu::create();
    auto write = Gio::Menu::create();
    for (const auto& [label, language] : {std::pair{"Write _gtkmm Code", "gtkmm"}, {"Write _C Code", "c"}}) {
        auto item = Gio::MenuItem::create(label, "");
        item->set_action_and_target("design.write-code", Glib::Variant<Glib::ustring>::create(language));
        write->append_item(item);
    }
    codeMenu->append_section(write);
    auto other = Gio::Menu::create();
    other->append("Export _Blueprint", "design.blueprint");
    codeMenu->append_section(other);
    codeButton_.set_label("Code");
    codeButton_.set_menu_model(codeMenu);
    codeButton_.set_tooltip_text("Write code for this design: a gtkmm class or C functions, rewritten each time "
                                 "you save; your own code goes in files Kev-C++ writes once");
    bar_.append(viewSwitch_);
    bar_.append(message_);
    bar_.append(codeButton_);

    // The canvas: the design in a frame, under a layer that takes the clicks. The design itself can
    // neither be clicked nor focused, so nothing in it runs (or catches keys meant for the IDE).
    frame_.set_can_target(false);
    frame_.set_can_focus(false);
    frame_.set_halign(Gtk::Align::CENTER);
    frame_.set_valign(Gtk::Align::CENTER);
    frame_.set_margin(32);
    frame_.add_css_class("kevc-design-frame");
    frame_.set_overflow(Gtk::Overflow::HIDDEN); // round window corners clip the design
    frame_.signalAllocated().connect([this] { selectionLayer_.queue_draw(); });
    fakeTitle_.add_css_class("kevc-design-titlebar");
    fakeTitle_.set_visible(false);
    frame_.append(fakeTitle_);
    canvas_.set_child(frame_);
    canvas_.add_css_class("kevc-design-canvas");
    selectionLayer_.set_draw_func([this](const Cairo::RefPtr<Cairo::Context>& cr, int, int) { drawSelection(cr); });
    auto click = Gtk::GestureClick::create();
    click->signal_pressed().connect([this](int, double x, double y) { pickAt(x, y); });
    selectionLayer_.add_controller(click);
    canvas_.add_overlay(selectionLayer_);
    canvasScroll_.set_child(canvas_);
    canvasScroll_.set_hexpand(true);
    canvasScroll_.set_vexpand(true);

    // The outline and the properties.
    outline_.set_selection_mode(Gtk::SelectionMode::SINGLE);
    outline_.add_css_class("kevc-outline");
    outline_.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
        if (syncing_ || !row) return;
        if (auto it = rowUids_.find(row); it != rowUids_.end()) select(it->second);
    });
    outlineScroll_.set_child(outline_);
    outlineScroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    props_.set_margin(8);
    propsScroll_.set_child(props_);
    // Scrolls sideways rather than ask for its full width: the paned never makes the panel narrower
    // than what it asks for.
    propsScroll_.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    signals_.set_margin(8);
    signalsScroll_.set_child(signals_);
    signalsScroll_.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    inspector_.append_page(propsScroll_, "Properties");
    inspector_.append_page(signalsScroll_, "Signals");
    side_.set_start_child(outlineScroll_);
    side_.set_end_child(inspector_);
    side_.set_position(220);
    side_.set_size_request(330, -1);

    // The palette, left of the canvas.
    buildPalette();
    paletteScroll_.set_child(palette_);
    paletteScroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    paletteScroll_.set_size_request(150, -1);
    paletteScroll_.add_css_class("kevc-palette");
    designArea_.append(paletteScroll_);
    designArea_.append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL));
    designArea_.append(canvasScroll_);
    split_.set_start_child(designArea_);
    split_.set_end_child(side_);
    split_.set_resize_end_child(false);
    split_.set_shrink_end_child(false);
    // The side panel keeps its width as the page resizes (left alone, the paned would follow the
    // panel's natural width, which changes with every selection); dragging the handle sets it.
    split_.property_max_position().signal_changed().connect([this] {
        settingSplit_ = true;
        split_.set_position(std::clamp(split_.get_width() - sideWidth_, 0, split_.property_max_position().get_value()));
        settingSplit_ = false;
    });
    split_.property_position().signal_changed().connect([this] {
        // Before the first set_position() these are the paned laying itself out, not a drag.
        if (!settingSplit_ && split_.property_position_set().get_value() && split_.get_width() > 400)
            sideWidth_ = std::clamp(split_.get_width() - split_.get_position(), 200, split_.get_width() - 100);
    });

    // In the design view Ctrl+Z is the design's, even while a property field has the focus (a
    // field would otherwise take the key for its own, empty, history).
    auto keys = Gtk::EventControllerKey::create();
    keys->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    keys->signal_key_pressed().connect(
        [this](guint key, guint, Gdk::ModifierType state) {
            if (view_ != View::Design) return false;
            const auto mods = state & (Gdk::ModifierType::CONTROL_MASK | Gdk::ModifierType::SHIFT_MASK |
                                       Gdk::ModifierType::ALT_MASK);
            const bool ctrl = mods == Gdk::ModifierType::CONTROL_MASK;
            const bool ctrlShift = mods == (Gdk::ModifierType::CONTROL_MASK | Gdk::ModifierType::SHIFT_MASK);
            const bool z = key == GDK_KEY_z || key == GDK_KEY_Z, y = key == GDK_KEY_y || key == GDK_KEY_Y;
            if (ctrl && z) {
                undo();
                return true;
            }
            if ((ctrlShift && z) || (ctrl && y)) {
                redo();
                return true;
            }
            // The clipboard and Delete act on widgets unless a text field is being typed in.
            if (textFieldFocused()) return false;
            if (mods == Gdk::ModifierType{} && (key == GDK_KEY_Delete || key == GDK_KEY_KP_Delete)) {
                deleteSelected();
                return true;
            }
            if (ctrl && (key == GDK_KEY_c || key == GDK_KEY_C)) copySelected();
            else if (ctrl && (key == GDK_KEY_x || key == GDK_KEY_X)) cutSelected();
            else if (ctrl && (key == GDK_KEY_v || key == GDK_KEY_V)) paste();
            else return false;
            return true;
        },
        false);
    add_controller(keys);
    setupEditing();

    stack_.add(split_, "design");
    stack_.add(xmlScroll_, "xml");
    stack_.set_vexpand(true);
    append(bar_);
    append(stack_);
    set_hexpand(true);
    set_vexpand(true);

    tabTitle_.set_tooltip_text(file.string());
    tabClose_.set_icon_name("window-close-symbolic");
    tabClose_.set_has_frame(false);
    tabClose_.set_focus_on_click(false);
    tabClose_.set_tooltip_text("Close (Ctrl+W)");
    tabClose_.signal_clicked().connect([this] { closeRequested_.emit(); });
    tabBox_.append(tabTitle_);
    tabBox_.append(tabClose_);

    try {
        doc_ = UiDocument::parse(contents);
        if (auto tops = doc_.topLevelObjects(); !tops.empty()) selected_ = shownRoot_ = tops.front()->uid;
        documentChanged();
    } catch (const ui::ParseError& e) {
        // Open it as text, so the mistake can be fixed here.
        view_ = View::Xml;
        syncing_ = true;
        xmlButton_.set_active(true);
        syncing_ = false;
        stack_.set_visible_child("xml");
        showMessage(Glib::ustring("This file isn't valid XML (") + e.what() + "). Fix it, then switch to Design.", true);
    }
    updateTitle();

    try {
        monitor_ = Gio::File::create_for_path(file.string())->monitor_file();
        monitor_->signal_changed().connect(
            [this](const Glib::RefPtr<Gio::File>&, const Glib::RefPtr<Gio::File>&, Gio::FileMonitor::Event event) {
                onFileChanged(event);
            });
    } catch (const Glib::Error&) {
        // Without a monitor the page just won't notice outside changes.
    }
}

DesignerPage::~DesignerPage() {
    contextMenu_.unparent();
    if (monitor_) monitor_->cancel();
    g_signal_handlers_disconnect_by_data(xmlBuffer_, this);
    if (previewRoot_) gtk_box_remove(GTK_BOX(frame_.gobj()), previewRoot_);
    if (builder_) g_object_unref(builder_);
    g_object_unref(xmlBuffer_);
}

// ---- DocumentPage

std::string DesignerPage::bufferText() const {
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(xmlBuffer_), &start, &end);
    char* text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(xmlBuffer_), &start, &end, TRUE);
    std::string out(text);
    g_free(text);
    return out;
}

void DesignerPage::setBufferText(const std::string& text) {
    GtkTextBuffer* buffer = GTK_TEXT_BUFFER(xmlBuffer_);
    gtk_text_buffer_begin_irreversible_action(buffer);
    gtk_text_buffer_set_text(buffer, text.data(), static_cast<int>(text.size()));
    gtk_text_buffer_end_irreversible_action(buffer);
}

std::string DesignerPage::currentText() const {
    return view_ == View::Xml ? bufferText() : doc_.text();
}

bool DesignerPage::modified() const {
    return currentText() != savedText_;
}

void DesignerPage::save() {
    // Exactly the text: no newline added, so an untouched file stays byte for byte the same.
    const std::string text = currentText();
    Glib::file_set_contents(path_.string(), text);
    savedText_ = text;
    updateTitle();
    // Keep the generated code in step with the design (once there is any).
    if (const auto language = codeLanguage()) {
        if (view_ == View::Design) {
            writeCode(doc_, *language, true);
        } else {
            try {
                writeCode(UiDocument::parse(text), *language, true);
            } catch (const ui::ParseError&) {
                // Not valid XML yet: the code stays as it was.
            }
        }
    }
}

void DesignerPage::updateTitle() {
    const std::string name = path_.filename().string();
    tabTitle_.set_text(modified() ? "*" + name : name);
}

void DesignerPage::focusPage() {
    if (view_ == View::Xml) gtk_widget_grab_focus(GTK_WIDGET(xmlView_));
    else outline_.grab_focus();
}

void DesignerPage::activateTextAction(const char* action) {
    const std::string a = action;
    const bool history = a == "text.undo" || a == "text.redo";
    // A text field here (a property value) keeps its own clipboard. Undo is the design's: a field
    // commits as soon as it's left, so there's nothing of its own worth undoing.
    if (auto* root = get_root(); root && !history) {
        GtkWidget* focus = gtk_root_get_focus(root->gobj());
        if (focus && GTK_IS_TEXT(focus) && gtk_widget_is_ancestor(focus, GTK_WIDGET(gobj()))) {
            gtk_widget_activate_action(focus, action, nullptr);
            return;
        }
    }
    if (view_ == View::Xml) {
        gtk_widget_activate_action(GTK_WIDGET(xmlView_), action, nullptr);
        return;
    }
    if (a == "text.undo") undo();
    else if (a == "clipboard.copy") copySelected();
    else if (a == "clipboard.cut") cutSelected();
    else if (a == "clipboard.paste") paste();
    else if (a == "text.redo") redo();
}

// ---- views and edits

void DesignerPage::showMessage(const Glib::ustring& text, bool error) {
    message_.set_text(text);
    message_.set_tooltip_text(text);
    if (error) message_.add_css_class("error");
    else message_.remove_css_class("error");
}

void DesignerPage::showView(View view) {
    if (view == View::Design && view_ == View::Xml) {
        if (!applyXml()) {
            syncing_ = true;
            xmlButton_.set_active(true);
            syncing_ = false;
            return;
        }
        // Select the object the cursor was in.
        GtkTextIter cursor;
        gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(xmlBuffer_), &cursor,
                                         gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(xmlBuffer_)));
        GtkTextIter start;
        gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(xmlBuffer_), &start);
        char* head = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(xmlBuffer_), &start, &cursor, TRUE);
        const size_t offset = std::strlen(head); // bytes, as the document's offsets are
        g_free(head);
        view_ = View::Design;
        stack_.set_visible_child("design");
        if (Node* at = doc_.objectAt(offset)) select(at->uid);
    } else if (view == View::Xml && view_ == View::Design) {
        const std::string text = doc_.text();
        if (bufferText() != text) setBufferText(text);
        view_ = View::Xml;
        stack_.set_visible_child("xml");
        // Put the cursor on the selected object.
        const auto spans = doc_.spans();
        if (auto it = spans.find(selected_); it != spans.end()) {
            GtkTextIter at;
            gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(xmlBuffer_), &at);
            gtk_text_iter_forward_chars(&at, int(g_utf8_strlen(text.data(), gssize(it->second.first))));
            gtk_text_buffer_place_cursor(GTK_TEXT_BUFFER(xmlBuffer_), &at);
            auto scroll = [this] {
                gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(xmlView_), gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(xmlBuffer_)),
                                             0.1, TRUE, 0.0, 0.2);
            };
            Glib::signal_idle().connect_once(sigc::track_obj(scroll, *this), Glib::PRIORITY_LOW);
        }
        gtk_widget_grab_focus(GTK_WIDGET(xmlView_));
    }
    syncing_ = true;
    designButton_.set_active(view_ == View::Design);
    xmlButton_.set_active(view_ == View::Xml);
    syncing_ = false;
    updateTitle();
}

bool DesignerPage::applyXml() {
    const std::string text = bufferText();
    if (text == doc_.text() && doc_.interface()) return true;
    try {
        UiDocument::parse(text); // throws before anything changes
        if (doc_.interface()) {
            undo_.push_back(doc_.text());
            redo_.clear();
        }
        restore(text);
        showMessage("", false);
        return true;
    } catch (const ui::ParseError& e) {
        showMessage(Glib::ustring("The XML has an error (") + e.what() + "). Fix it to see the design.", true);
        GtkTextIter at;
        gtk_text_buffer_get_iter_at_line(GTK_TEXT_BUFFER(xmlBuffer_), &at, std::max(0, e.line - 1));
        gtk_text_buffer_place_cursor(GTK_TEXT_BUFFER(xmlBuffer_), &at);
        gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(xmlView_), gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(xmlBuffer_)),
                                     0.1, TRUE, 0.0, 0.3);
        gtk_widget_grab_focus(GTK_WIDGET(xmlView_));
        return false;
    }
}

void DesignerPage::edit(const std::function<void()>& change) {
    if (view_ != View::Design) return;
    const std::string before = doc_.text();
    change();
    if (doc_.text() == before) return;
    undo_.push_back(before);
    redo_.clear();
    documentChanged();
}

void DesignerPage::undo() {
    if (undo_.empty()) return;
    redo_.push_back(doc_.text());
    const std::string text = std::move(undo_.back());
    undo_.pop_back();
    restore(text);
}

void DesignerPage::redo() {
    if (redo_.empty()) return;
    undo_.push_back(doc_.text());
    const std::string text = std::move(redo_.back());
    redo_.pop_back();
    restore(text);
}

void DesignerPage::restore(const std::string& text) {
    std::vector<int> selectedPath, rootPath;
    if (Node* n = doc_.find(selected_)) selectedPath = UiDocument::pathOf(*n);
    if (Node* n = doc_.find(shownRoot_)) rootPath = UiDocument::pathOf(*n);
    const bool propertiesShown = propertiesUid_ == selected_;
    doc_ = UiDocument::parse(text);
    Node* n = selectedPath.empty() ? nullptr : doc_.atPath(selectedPath);
    selected_ = n && UiDocument::isObject(*n) ? n->uid : 0;
    if (propertiesShown) propertiesUid_ = selected_; // the same object, under its new uid
    n = rootPath.empty() ? nullptr : doc_.atPath(rootPath);
    shownRoot_ = n && UiDocument::isObject(*n) ? n->uid : 0;
    if (!selected_ && !shownRoot_)
        if (auto tops = doc_.topLevelObjects(); !tops.empty()) selected_ = shownRoot_ = tops.front()->uid;
    documentChanged();
}

void DesignerPage::documentChanged() {
    dropPlan_.reset(); // it points into the old nodes
    rebuildPreview();
    rebuildOutline();
    scheduleProperties();
    updateTitle();
}

void DesignerPage::onFileChanged(Gio::FileMonitor::Event event) {
    if (event != Gio::FileMonitor::Event::CHANGES_DONE_HINT && event != Gio::FileMonitor::Event::CREATED) return;
    std::string text;
    try {
        text = Glib::file_get_contents(path_.string());
    } catch (const Glib::Error&) {
        return;
    }
    if (text == savedText_) return; // our own save
    if (modified()) {
        showMessage("The file changed on disk; saving will overwrite that change.", true);
        return;
    }
    savedText_ = text;
    undo_.clear();
    redo_.clear();
    if (view_ == View::Xml) {
        setBufferText(text);
        updateTitle();
        return;
    }
    try {
        restore(text);
        showMessage("Reloaded: the file changed on disk.", false);
    } catch (const ui::ParseError& e) {
        setBufferText(text);
        view_ = View::Xml;
        stack_.set_visible_child("xml");
        syncing_ = true;
        xmlButton_.set_active(true);
        syncing_ = false;
        showMessage(Glib::ustring("The file changed on disk and isn't valid XML now (") + e.what() + ").", true);
    }
}

// ---- the preview

int DesignerPage::rootOf(int uid) const {
    Node* n = doc_.find(uid);
    if (!n) return 0;
    while (Node* up = UiDocument::parentObject(*n)) n = up;
    return n->uid;
}

void DesignerPage::rebuildPreview() {
    if (previewRoot_) {
        gtk_box_remove(GTK_BOX(frame_.gobj()), previewRoot_);
        previewRoot_ = nullptr;
    }
    widgets_.clear();
    previewRoots_.clear();
    for (const auto& group : actionGroups_) gtk_widget_insert_action_group(GTK_WIDGET(frame_.gobj()), group.c_str(), nullptr);
    actionGroups_.clear();
    if (builder_) {
        g_object_unref(builder_); // and with it every object of the old preview
        builder_ = nullptr;
    }
    fakeTitle_.set_visible(false);
    frame_.set_size_request(-1, -1);
    frame_.set_visible(false);
    selectionLayer_.queue_draw();

    if (!doc_.interface()) {
        showMessage("This isn't a GtkBuilder file: there's no <interface> element.", true);
        return;
    }
    const ui::Preview preview = ui::makePreview(doc_, classKind);
    if (preview.roots.empty()) {
        showMessage("Nothing to show: there's no widget or window at the top level.", false);
        return;
    }
    GtkBuilder* builder = gtk_builder_new();
    GError* error = nullptr;
    if (!gtk_builder_add_from_string(builder, preview.xml.c_str(), -1, &error)) {
        showMessage(Glib::ustring("GTK can't build this design: ") + error->message, true);
        g_error_free(error);
        g_object_unref(builder);
        return;
    }
    builder_ = builder;
    for (const auto& r : preview.roots) previewRoots_.insert(r.uid);

    // Show the top-level object holding the selection, else the one shown before, else the first.
    const ui::PreviewRoot* root = &preview.roots.front();
    const int wanted[] = {rootOf(selected_), shownRoot_};
    for (int uid : wanted) {
        auto it = std::find_if(preview.roots.begin(), preview.roots.end(), [&](const auto& r) { return r.uid == uid; });
        if (uid && it != preview.roots.end()) {
            root = &*it;
            break;
        }
    }
    shownRoot_ = root->uid;
    GObject* object = gtk_builder_get_object(builder_, root->id.c_str());
    if (!GTK_IS_WIDGET(object)) {
        showMessage("GTK built the design but its top-level object isn't a widget.", true);
        return;
    }
    for (const auto& [id, uid] : preview.ids) {
        GObject* o = gtk_builder_get_object(builder_, id.c_str());
        if (!GTK_IS_WIDGET(o)) continue;
        g_object_set_data(o, "kevc-node", GINT_TO_POINTER(uid));
        if (id.rfind("__kevc_label_", 0) != 0) widgets_[uid] = GTK_WIDGET(o);
        // An empty container would be 0 pixels tall: give it room to see and to drop into.
        if (Node* n = doc_.find(uid); n && UiDocument::parentObject(*n) && UiDocument::childObjects(*n).empty())
            if (const ui::ClassInfo* info = designer::catalogInfo(UiDocument::className(*n)); info && info->slots != ui::Slots::None)
                gtk_widget_add_css_class(GTK_WIDGET(o), "kevc-empty-container");
    }

    // Buttons with action-name="win.save" find these empty actions before the IDE's own, so they
    // look as they will in the app and do nothing here.
    std::map<std::string, Glib::RefPtr<Gio::SimpleActionGroup>> groups;
    for (const auto& a : preview.actions) {
        if (!g_action_name_is_valid(a.name.c_str())) continue;
        auto& group = groups[a.group];
        if (!group) group = Gio::SimpleActionGroup::create();
        if (group->lookup_action(a.name)) continue;
        GVariantType* parameter = nullptr;
        if (!a.target.empty())
            if (GVariant* target = g_variant_parse(nullptr, a.target.c_str(), nullptr, nullptr, nullptr)) {
                parameter = g_variant_type_copy(g_variant_get_type(target));
                g_variant_unref(target);
            }
        GSimpleAction* action = g_simple_action_new(a.name.c_str(), parameter);
        g_action_map_add_action(G_ACTION_MAP(group->gobj()), G_ACTION(action));
        g_object_unref(action);
        if (parameter) g_variant_type_free(parameter);
    }
    for (const auto& [name, group] : groups) {
        frame_.insert_action_group(name, group);
        actionGroups_.push_back(name);
    }

    previewRoot_ = GTK_WIDGET(object);
    stripMnemonics(previewRoot_);
    gtk_widget_set_vexpand(previewRoot_, TRUE);
    fakeTitle_.set_text(root->title.empty() ? " " : root->title);
    fakeTitle_.set_visible(root->window && !root->titlebar);
    gtk_box_append(GTK_BOX(frame_.gobj()), previewRoot_);
    if (root->window && (root->width > 0 || root->height > 0))
        frame_.set_size_request(root->width > 0 ? root->width : -1, root->height > 0 ? root->height : -1);
    if (root->window) frame_.add_css_class("kevc-design-window");
    else frame_.remove_css_class("kevc-design-window");
    frame_.set_visible(true);
    if (!message_.has_css_class("error") || message_.get_text().find("GTK can't build") == 0) showMessage("", false);
}

// Labels in the design would otherwise register their _mnemonics on the IDE's window, where Alt+key
// would trigger them. Underlines only show while Alt is held, so the design looks the same.
void DesignerPage::stripMnemonics(GtkWidget* widget) {
    if (GTK_IS_LABEL(widget) && gtk_label_get_use_underline(GTK_LABEL(widget))) {
        const std::string text = withoutMnemonics(gtk_label_get_label(GTK_LABEL(widget)));
        gtk_label_set_use_underline(GTK_LABEL(widget), FALSE);
        gtk_label_set_label(GTK_LABEL(widget), text.c_str());
    }
    for (GtkWidget* c = gtk_widget_get_first_child(widget); c; c = gtk_widget_get_next_sibling(c)) stripMnemonics(c);
}

void DesignerPage::pickAt(double x, double y) {
    if (const int uid = objectAt(x, y)) select(uid);
}

void DesignerPage::drawSelection(const Cairo::RefPtr<Cairo::Context>& cr) {
    auto it = widgets_.find(selected_);
    graphene_rect_t r;
    if (it != widgets_.end() && gtk_widget_get_mapped(it->second) &&
        gtk_widget_compute_bounds(it->second, GTK_WIDGET(selectionLayer_.gobj()), &r)) {
        // A window's selection includes the stand-in title bar.
        if (it->second == previewRoot_ && fakeTitle_.get_visible()) {
            graphene_rect_t frame;
            if (gtk_widget_compute_bounds(GTK_WIDGET(frame_.gobj()), GTK_WIDGET(selectionLayer_.gobj()), &frame)) r = frame;
        }
        const double x = std::floor(r.origin.x) + 1, y = std::floor(r.origin.y) + 1;
        const double w = std::max(2.0, std::ceil(double(r.size.width)) - 2), h = std::max(2.0, std::ceil(double(r.size.height)) - 2);
        cr->rectangle(x, y, w, h);
        cr->set_source_rgba(0.21, 0.52, 0.89, 0.12);
        cr->fill_preserve();
        cr->set_source_rgba(0.21, 0.52, 0.89, 1.0);
        cr->set_line_width(2);
        cr->stroke();
    }
    // Where a dragged widget would land: a line between children, or an area.
    if (dropPlan_) {
        const graphene_rect_t& d = dropPlan_->indicator;
        cr->set_source_rgba(0.96, 0.62, 0.15, 1.0);
        if (dropPlan_->line) {
            cr->set_line_width(3);
            cr->move_to(d.origin.x, d.origin.y);
            cr->line_to(d.origin.x + d.size.width, d.origin.y + d.size.height);
            cr->stroke();
        } else {
            cr->rectangle(d.origin.x + 2, d.origin.y + 2, std::max(2.f, d.size.width - 4), std::max(2.f, d.size.height - 4));
            cr->set_source_rgba(0.96, 0.62, 0.15, 0.18);
            cr->fill_preserve();
            cr->set_source_rgba(0.96, 0.62, 0.15, 1.0);
            cr->set_line_width(2);
            cr->set_dash(std::vector<double>{6, 4}, 0);
            cr->stroke();
        }
    }
}

void DesignerPage::select(int uid) {
    if (!doc_.find(uid)) uid = 0;
    selected_ = uid;
    syncing_ = true;
    Gtk::ListBoxRow* row = nullptr;
    for (const auto& [r, u] : rowUids_)
        if (u == uid) row = r;
    if (row) outline_.select_row(*row);
    else outline_.unselect_all();
    syncing_ = false;
    if (row) {
        // Scroll the outline to it once it's laid out (a list box doesn't on its own).
        auto reveal = [this, uid] {
            for (const auto& [r, u] : rowUids_) {
                if (u != uid) continue;
                graphene_rect_t bounds;
                if (!gtk_widget_compute_bounds(GTK_WIDGET(r->gobj()), GTK_WIDGET(outline_.gobj()), &bounds)) return;
                auto adjustment = outlineScroll_.get_vadjustment();
                const double top = bounds.origin.y, bottom = top + bounds.size.height;
                if (top < adjustment->get_value()) adjustment->set_value(top);
                else if (bottom > adjustment->get_value() + adjustment->get_page_size())
                    adjustment->set_value(bottom - adjustment->get_page_size());
                return;
            }
        };
        Glib::signal_idle().connect_once(sigc::track_obj(reveal, *this), Glib::PRIORITY_LOW);
    }
    // An object in another top-level window or widget: show that one.
    if (const int root = rootOf(uid); root && root != shownRoot_ && previewRoots_.count(root)) rebuildPreview();
    selectionLayer_.queue_draw();
    scheduleProperties();
    if (Node* n = doc_.find(uid)) {
        const std::string id = UiDocument::objectId(*n);
        status_.emit(UiDocument::className(*n) + (id.empty() ? "" : " " + id));
    }
}

// ---- the outline

void DesignerPage::rebuildOutline() {
    syncing_ = true;
    while (Gtk::ListBoxRow* row = outline_.get_row_at_index(0)) outline_.remove(*row);
    rowUids_.clear();
    std::function<void(const Node&, int)> add = [&](const Node& o, int depth) {
        const std::string cls = UiDocument::className(o), id = UiDocument::objectId(o);
        const ui::ClassKind kind = classKind(cls);
        auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        box->set_margin_start(6 + depth * 16);
        box->set_margin_top(2);
        box->set_margin_bottom(2);
        auto* icon = Gtk::make_managed<Gtk::Image>();
        icon->set_from_icon_name(kind == ui::ClassKind::Unknown ? "kevc-warning"
                                 : kind == ui::ClassKind::Widget || kind == ui::ClassKind::Window ? "kevc-class"
                                                                                                    : "kevc-struct");
        if (kind == ui::ClassKind::Unknown) icon->set_tooltip_text("Not a class Kev-C++ can show; drawn as a placeholder");
        box->append(*icon);
        // Where it sits in its parent, when that's more than "a child".
        std::string slot;
        if (const Node* holder = o.parent) {
            if (holder->isElement("child")) slot = holder->attribute("type");
            else if (holder->isElement("property")) slot = holder->attribute("name");
        }
        Glib::ustring markup = id.empty() ? Glib::Markup::escape_text(cls)
                                          : "<b>" + Glib::Markup::escape_text(id) + "</b>  <span alpha=\"65%\">" +
                                                Glib::Markup::escape_text(cls) + "</span>";
        if (!slot.empty()) markup += "  <span alpha=\"50%\">" + Glib::Markup::escape_text(slot) + "</span>";
        if (o.name == "template") markup += "  <span alpha=\"50%\">template</span>";
        auto* label = Gtk::make_managed<Gtk::Label>();
        label->set_markup(markup);
        label->set_xalign(0);
        label->set_ellipsize(Pango::EllipsizeMode::END);
        box->append(*label);
        auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_child(*box);
        outline_.append(*row);
        rowUids_[row] = o.uid;
        if (o.uid == selected_) outline_.select_row(*row);
        for (const Node* c : UiDocument::childObjects(o)) add(*c, depth + 1);
    };
    for (const Node* top : doc_.topLevelObjects()) add(*top, 0);
    syncing_ = false;
}

// ---- properties

void DesignerPage::scheduleProperties() {
    if (propertiesPending_) return;
    propertiesPending_ = true;
    // Later, not now: this may run from a property field's own signal, and rebuilding destroys it.
    Glib::signal_idle().connect_once(sigc::track_obj(
        [this] {
            propertiesPending_ = false;
            rebuildProperties();
        },
        *this));
}

void DesignerPage::setProperty(int uid, const std::string& name, const std::string& value) {
    if (syncing_) return;
    edit([&] {
        if (Node* o = doc_.find(uid)) doc_.setProperty(*o, name, value);
    });
}

void DesignerPage::resetProperty(int uid, const std::string& name) {
    edit([&] {
        if (Node* o = doc_.find(uid)) doc_.removeProperty(*o, name);
    });
}

Gtk::Widget& DesignerPage::rawEditor(int uid, const std::string& name, const std::string& value) {
    auto* entry = Gtk::make_managed<Gtk::Entry>();
    entry->set_text(value);
    entry->set_width_chars(6);
    entry->set_hexpand(true);
    auto commit = [this, entry, uid, name] {
        const std::string text = entry->get_text();
        Node* o = doc_.find(uid);
        if (!o) return;
        const auto current = UiDocument::property(*o, name);
        if (current ? *current != text : !text.empty()) setProperty(uid, name, text);
    };
    entry->signal_activate().connect(commit);
    auto focus = Gtk::EventControllerFocus::create();
    focus->signal_leave().connect(commit);
    entry->add_controller(focus);
    return *entry;
}

Gtk::Widget& DesignerPage::propertyEditor(int uid, GParamSpec* spec, const std::optional<std::string>& value) {
    GParamSpec* real = g_param_spec_get_redirect_target(spec);
    if (!real) real = spec;
    const std::string name = spec->name;
    const GType type = G_PARAM_SPEC_VALUE_TYPE(real);
    const std::string fallback = defaultText(real);

    switch (G_TYPE_FUNDAMENTAL(type)) {
    case G_TYPE_BOOLEAN: {
        auto* check = Gtk::make_managed<Gtk::CheckButton>();
        check->set_active(truthy(value.value_or(fallback)));
        check->signal_toggled().connect(
            [this, check, uid, name] { setProperty(uid, name, check->get_active() ? "True" : "False"); });
        return *check;
    }
    case G_TYPE_INT:
    case G_TYPE_UINT:
    case G_TYPE_LONG:
    case G_TYPE_ULONG:
    case G_TYPE_INT64:
    case G_TYPE_UINT64:
    case G_TYPE_FLOAT:
    case G_TYPE_DOUBLE: {
        double low = -1e9, high = 1e9;
        bool integer = true;
        if (G_IS_PARAM_SPEC_INT(real)) low = G_PARAM_SPEC_INT(real)->minimum, high = G_PARAM_SPEC_INT(real)->maximum;
        else if (G_IS_PARAM_SPEC_UINT(real)) low = G_PARAM_SPEC_UINT(real)->minimum, high = G_PARAM_SPEC_UINT(real)->maximum;
        else if (G_IS_PARAM_SPEC_LONG(real)) low = double(G_PARAM_SPEC_LONG(real)->minimum), high = double(G_PARAM_SPEC_LONG(real)->maximum);
        else if (G_IS_PARAM_SPEC_ULONG(real)) low = double(G_PARAM_SPEC_ULONG(real)->minimum), high = double(G_PARAM_SPEC_ULONG(real)->maximum);
        else if (G_IS_PARAM_SPEC_INT64(real)) low = double(G_PARAM_SPEC_INT64(real)->minimum), high = double(G_PARAM_SPEC_INT64(real)->maximum);
        else if (G_IS_PARAM_SPEC_UINT64(real)) low = double(G_PARAM_SPEC_UINT64(real)->minimum), high = double(G_PARAM_SPEC_UINT64(real)->maximum);
        else if (G_IS_PARAM_SPEC_FLOAT(real)) low = G_PARAM_SPEC_FLOAT(real)->minimum, high = G_PARAM_SPEC_FLOAT(real)->maximum, integer = false;
        else if (G_IS_PARAM_SPEC_DOUBLE(real)) low = G_PARAM_SPEC_DOUBLE(real)->minimum, high = G_PARAM_SPEC_DOUBLE(real)->maximum, integer = false;
        // A spin button can't hold the full range of a 64-bit or floating-point property.
        low = std::max(low, -1e12);
        high = std::min(high, 1e12);
        auto* spin = Gtk::make_managed<Gtk::SpinButton>();
        spin->set_digits(integer ? 0 : 2);
        spin->set_width_chars(4);
        spin->set_range(low, high);
        spin->set_increments(integer ? 1 : 0.1, integer ? 10 : 1);
        spin->set_value(parseNumber(value.value_or(fallback)).value_or(0));
        spin->set_hexpand(true);
        spin->signal_value_changed().connect(
            [this, spin, uid, name, integer] { setProperty(uid, name, numberText(spin->get_value(), integer)); });
        return *spin;
    }
    case G_TYPE_ENUM: {
        GEnumClass* klass = G_PARAM_SPEC_ENUM(real)->enum_class;
        std::vector<Glib::ustring> nicks;
        for (guint i = 0; i < klass->n_values; ++i) nicks.push_back(klass->values[i].value_nick);
        const std::string current = value.value_or(fallback);
        guint selected = 0;
        for (guint i = 0; i < klass->n_values; ++i) {
            const GEnumValue& e = klass->values[i];
            if (current == e.value_nick || current == e.value_name || current == std::to_string(e.value)) selected = i;
        }
        auto* drop = Gtk::make_managed<Gtk::DropDown>(nicks);
        drop->set_selected(selected);
        drop->set_hexpand(true);
        drop->property_selected().signal_changed().connect([this, drop, uid, name, nicks] {
            if (drop->get_selected() < nicks.size()) setProperty(uid, name, nicks[drop->get_selected()]);
        });
        return *drop;
    }
    default: {
        auto& editor = rawEditor(uid, name, value.value_or(""));
        if (!fallback.empty()) static_cast<Gtk::Entry&>(editor).set_placeholder_text(fallback);
        return editor;
    }
    }
}

void DesignerPage::rebuildProperties() {
    // After an edit the same object's list is rebuilt; keep the field being edited focused (GTK
    // would otherwise move the focus, and scroll, to the first field).
    std::optional<std::string> focused;
    if (auto* root = get_root())
        if (GtkWidget* focus = gtk_root_get_focus(root->gobj()))
            for (const auto& [name, editor] : propertyEditors_)
                if (focus == editor->gobj() || gtk_widget_is_ancestor(focus, editor->gobj())) focused = name;
    const bool sameObject = propertiesUid_ == selected_;
    syncing_ = true;
    propertyEditors_.clear();
    while (Gtk::Widget* c = props_.get_first_child()) props_.remove(*c);
    propertiesUid_ = selected_;
    Node* object = doc_.find(selected_);
    rebuildSignals(object);
    if (!object) {
        auto* hint = Gtk::make_managed<Gtk::Label>("Select a widget in the design or the outline to see its properties.");
        hint->set_wrap(true);
        hint->add_css_class("dim-label");
        props_.append(*hint);
        syncing_ = false;
        return;
    }
    const int uid = object->uid;
    const std::string cls = UiDocument::className(*object);
    const bool isTemplate = object->name == "template";

    auto* title = Gtk::make_managed<Gtk::Label>();
    title->set_markup("<big><b>" + Glib::Markup::escape_text(cls.empty() ? "(no class)" : cls) + "</b></big>" +
                      (isTemplate ? "  <span alpha=\"60%\">template</span>" : ""));
    title->set_xalign(0);
    props_.append(*title);

    auto* grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_column_spacing(8);
    grid->set_row_spacing(4);
    int row = 0;
    auto addRow = [&](const Glib::ustring& name, Gtk::Widget& editor, bool set, const Glib::ustring& tip,
                      std::function<void()> reset) {
        auto* label = Gtk::make_managed<Gtk::Label>();
        label->set_markup(set ? "<b>" + Glib::Markup::escape_text(name) + "</b>" : Glib::Markup::escape_text(name));
        label->set_xalign(0);
        label->set_tooltip_text(tip);
        grid->attach(*label, 0, row);
        editor.set_hexpand(true);
        grid->attach(editor, 1, row);
        propertyEditors_[name] = &editor;
        if (set && reset) {
            auto* clear = Gtk::make_managed<Gtk::Button>();
            clear->set_icon_name("edit-clear-symbolic");
            clear->set_has_frame(false);
            clear->set_tooltip_text("Remove from the file (back to the default)");
            clear->signal_clicked().connect(reset);
            grid->attach(*clear, 2, row);
        }
        ++row;
    };

    // The id (a template's class name can't be changed here: code refers to it).
    auto* idEntry = Gtk::make_managed<Gtk::Entry>();
    idEntry->set_text(UiDocument::objectId(*object));
    idEntry->set_width_chars(6);
    idEntry->set_editable(!isTemplate);
    idEntry->set_placeholder_text("none (code can't look it up)");
    auto commitId = [this, idEntry, uid] {
        if (syncing_) return;
        Node* o = doc_.find(uid);
        if (!o || o->name == "template") return;
        const std::string id = idEntry->get_text();
        if (id == UiDocument::objectId(*o)) return;
        for (Node* other : doc_.objects())
            if (other != o && UiDocument::objectId(*other) == id && !id.empty()) {
                showMessage("Another object already has the id \"" + id + "\".", true);
                return;
            }
        edit([&] { doc_.setAttribute(*o, "id", id); });
    };
    idEntry->signal_activate().connect(commitId);
    auto idFocus = Gtk::EventControllerFocus::create();
    idFocus->signal_leave().connect(commitId);
    idEntry->add_controller(idFocus);
    addRow(isTemplate ? "class" : "id", *idEntry, false, "The name GtkBuilder and your code find the object by", {});
    propertyEditors_.erase(isTemplate ? "class" : "id");
    propertyEditors_[""] = idEntry;
    props_.append(*grid);

    // Its cell, when it's in a grid.
    if (Node* parent = UiDocument::parentObject(*object)) {
        const ui::ClassInfo* holder = designer::catalogInfo(UiDocument::className(*parent));
        if (holder && holder->slots == ui::Slots::Grid && ui::holderOf(*object) == "child") {
            props_.append(sectionTitle("Cell in the grid"));
            grid = Gtk::make_managed<Gtk::Grid>();
            grid->set_column_spacing(8);
            grid->set_row_spacing(4);
            row = 0;
            for (const auto& [name, low] : {std::pair{"column", 0}, {"row", 0}, {"column-span", 1}, {"row-span", 1}}) {
                const std::string key = name;
                const auto value = ui::layoutProperty(*object, key);
                auto* spin = Gtk::make_managed<Gtk::SpinButton>();
                spin->set_range(low, 999);
                spin->set_digits(0);
                spin->set_increments(1, 5);
                spin->set_width_chars(4);
                spin->set_value(parseNumber(value.value_or("")).value_or(low));
                spin->signal_value_changed().connect([this, spin, uid, key] {
                    if (syncing_) return;
                    edit([&] {
                        if (Node* o = doc_.find(uid)) ui::setLayoutProperty(doc_, *o, key, std::to_string(spin->get_value_as_int()));
                    });
                });
                addRow(key, *spin, value.has_value(), "Where it sits in its GtkGrid", {});
                propertyEditors_.erase(key);
                propertyEditors_["layout:" + key] = spin;
            }
            props_.append(*grid);
        }
    }

    const GType type = typeOf(cls);
    if (!type || !g_type_is_a(type, G_TYPE_OBJECT)) {
        auto* note = Gtk::make_managed<Gtk::Label>(
            cls + " isn't a class Kev-C++ has (a custom widget, or from another library), so only the properties "
                  "written in the file are listed.");
        note->set_wrap(true);
        note->set_xalign(0);
        note->add_css_class("dim-label");
        note->set_margin_top(8);
        props_.append(*note);
        grid = Gtk::make_managed<Gtk::Grid>();
        grid->set_column_spacing(8);
        grid->set_row_spacing(4);
        row = 0;
        for (const auto& [name, value] : UiDocument::properties(*object)) {
            const std::string n = name;
            addRow(name, rawEditor(uid, name, value), true, "", [this, uid, n] { resetProperty(uid, n); });
        }
        props_.append(*grid);
        syncing_ = false;
        restoreFocus(sameObject, focused);
        return;
    }

    auto* klass = G_OBJECT_CLASS(g_type_class_ref(type));
    guint count = 0;
    GParamSpec** specs = g_object_class_list_properties(klass, &count);
    // Own properties first, then each parent class's.
    for (GType owner = type; owner && owner != G_TYPE_OBJECT && owner != G_TYPE_INITIALLY_UNOWNED;
         owner = g_type_parent(owner)) {
        std::vector<GParamSpec*> mine;
        for (guint i = 0; i < count; ++i) {
            GParamSpec* spec = specs[i];
            if (spec->owner_type != owner || !(spec->flags & G_PARAM_WRITABLE) || (spec->flags & G_PARAM_DEPRECATED)) continue;
            const Node* node = UiDocument::propertyNode(*object, spec->name);
            if (widgetSlot(spec) && (!node || !node->childElements().empty())) continue;
            mine.push_back(spec);
        }
        if (mine.empty()) continue;
        std::sort(mine.begin(), mine.end(), [](GParamSpec* a, GParamSpec* b) { return std::strcmp(a->name, b->name) < 0; });
        props_.append(sectionTitle(g_type_name(owner)));
        grid = Gtk::make_managed<Gtk::Grid>();
        grid->set_column_spacing(8);
        grid->set_row_spacing(4);
        row = 0;
        for (GParamSpec* spec : mine) {
            const std::string name = spec->name;
            const Node* node = UiDocument::propertyNode(*object, name);
            const bool holdsObject = node && !node->childElements().empty();
            const char* blurb = g_param_spec_get_blurb(spec);
            auto reset = [this, uid, name] { resetProperty(uid, name); };
            if (holdsObject) {
                auto* note = Gtk::make_managed<Gtk::Label>("an object (in the outline)");
                note->set_xalign(0);
                note->add_css_class("dim-label");
                addRow(name, *note, true, blurb ? blurb : "", reset);
            } else {
                const auto value = node ? std::optional<std::string>(node->innerText()) : std::nullopt;
                addRow(name, propertyEditor(uid, spec, value), node != nullptr, blurb ? blurb : "", reset);
            }
        }
        props_.append(*grid);
    }
    // Written in the file but not a property of the class (a typo, or a newer GTK's).
    std::vector<std::pair<std::string, std::string>> strays;
    for (const auto& [name, value] : UiDocument::properties(*object)) {
        std::string canonical = name;
        std::replace(canonical.begin(), canonical.end(), '_', '-');
        if (!g_object_class_find_property(klass, canonical.c_str())) strays.emplace_back(name, value);
    }
    if (!strays.empty()) {
        props_.append(sectionTitle("Not properties of " + cls));
        grid = Gtk::make_managed<Gtk::Grid>();
        grid->set_column_spacing(8);
        grid->set_row_spacing(4);
        row = 0;
        for (const auto& [name, value] : strays) {
            const std::string n = name;
            addRow(name, rawEditor(uid, name, value), true, "GTK will refuse this file until it's removed",
                   [this, uid, n] { resetProperty(uid, n); });
        }
        props_.append(*grid);
    }
    g_free(specs);
    g_type_class_unref(klass);
    syncing_ = false;
    restoreFocus(sameObject, focused);
}

void DesignerPage::rebuildSignals(Node* object) {
    while (Gtk::Widget* c = signals_.get_first_child()) signals_.remove(*c);
    if (!object) {
        auto* hint = Gtk::make_managed<Gtk::Label>("Select a widget to see its signals.");
        hint->set_wrap(true);
        hint->add_css_class("dim-label");
        signals_.append(*hint);
        return;
    }
    const int uid = object->uid;
    const std::string cls = UiDocument::className(*object);
    std::string owner = UiDocument::objectId(*object);
    if (owner.empty()) owner = ui::freshId(ui::UiDocument(), cls), owner.pop_back(); // "button1" → "button"
    const auto handlers = ui::signalHandlers(*object);
    auto handlerOf = [&](const std::string& signal) {
        for (const auto& h : handlers)
            if (h.signal == signal) return h.handler;
        return std::string();
    };

    auto* note = Gtk::make_managed<Gtk::Label>("Type a handler's name and press Enter; Enter in an empty field "
                                              "uses the suggested name.");
    note->set_wrap(true);
    note->set_xalign(0);
    note->add_css_class("dim-label");
    signals_.append(*note);

    std::set<std::string> listed;
    auto section = [&](const std::string& title, const std::vector<std::string>& names) {
        if (names.empty()) return;
        signals_.append(sectionTitle(title));
        auto* grid = Gtk::make_managed<Gtk::Grid>();
        grid->set_column_spacing(8);
        grid->set_row_spacing(4);
        int row = 0;
        for (const std::string& signal : names) {
            listed.insert(signal);
            const std::string current = handlerOf(signal);
            std::string suggested = "on_" + owner + "_" + signal;
            std::replace(suggested.begin(), suggested.end(), '-', '_');
            std::replace(suggested.begin(), suggested.end(), ':', '_');
            auto* label = Gtk::make_managed<Gtk::Label>();
            label->set_markup(current.empty() ? Glib::Markup::escape_text(signal) : "<b>" + Glib::Markup::escape_text(signal) + "</b>");
            label->set_xalign(0);
            auto* entry = Gtk::make_managed<Gtk::Entry>();
            entry->set_text(current);
            entry->set_placeholder_text(suggested);
            entry->set_width_chars(6);
            entry->set_hexpand(true);
            auto commit = [this, entry, uid, signal, suggested](bool enter) {
                if (syncing_) return; // the list being rebuilt: this field is on its way out
                std::string handler = entry->get_text();
                Node* o = doc_.find(uid);
                if (!o) return;
                std::string current;
                for (const auto& h : ui::signalHandlers(*o))
                    if (h.signal == signal) current = h.handler;
                if (handler.empty() && current.empty() && enter) {
                    handler = suggested;
                    entry->set_text(handler);
                }
                if (handler == current) return;
                const bool valid = handler.empty() || (!std::isdigit(static_cast<unsigned char>(handler[0])) &&
                                                       std::all_of(handler.begin(), handler.end(), [](unsigned char c) {
                                                           return std::isalnum(c) || c == '_';
                                                       }));
                if (!valid) {
                    showMessage("\"" + handler + "\" can't be a function name (letters, digits and _ only).", true);
                    return;
                }
                edit([&] {
                    if (Node* target = doc_.find(uid)) ui::setSignalHandler(doc_, *target, signal, handler);
                });
            };
            entry->signal_activate().connect([commit] { commit(true); });
            auto focus = Gtk::EventControllerFocus::create();
            focus->signal_leave().connect([commit] { commit(false); });
            entry->add_controller(focus);
            grid->attach(*label, 0, row);
            grid->attach(*entry, 1, row);
            if (!current.empty()) {
                auto* jump = Gtk::make_managed<Gtk::Button>();
                jump->set_icon_name("go-jump-symbolic");
                jump->set_has_frame(false);
                jump->set_tooltip_text("Go to the handler's code (Kev-C++ adds it if it isn't there)");
                jump->signal_clicked().connect([this, uid, signal] { goToHandler(uid, signal); });
                grid->attach(*jump, 2, row);
            }
            propertyEditors_["signal:" + signal] = entry;
            ++row;
        }
        signals_.append(*grid);
    };
    auto namesOf = [](GType type) {
        std::vector<std::string> names;
        guint count = 0;
        guint* ids = g_signal_list_ids(type, &count);
        for (guint i = 0; i < count; ++i) {
            GSignalQuery query;
            g_signal_query(ids[i], &query);
            if (!(query.signal_flags & G_SIGNAL_DEPRECATED)) names.emplace_back(query.signal_name);
        }
        g_free(ids);
        std::sort(names.begin(), names.end());
        return names;
    };
    if (const GType type = typeOf(cls); type && g_type_is_a(type, G_TYPE_OBJECT)) {
        for (GType t = type; t && t != G_TYPE_OBJECT && t != G_TYPE_INITIALLY_UNOWNED; t = g_type_parent(t))
            section(g_type_name(t), namesOf(t));
        // Signals of the interfaces it implements (GtkEditable's "changed" for an entry).
        guint count = 0;
        GType* interfaces = g_type_interfaces(type, &count);
        for (guint i = 0; i < count; ++i) section(g_type_name(interfaces[i]), namesOf(interfaces[i]));
        g_free(interfaces);
    }
    // Handlers in the file for signals not listed above (notify::…, or a class Kev-C++ doesn't have).
    std::vector<std::string> others;
    for (const auto& h : handlers)
        if (!listed.count(h.signal)) others.push_back(h.signal);
    section("Other", others);
}

void DesignerPage::restoreFocus(bool sameObject, const std::optional<std::string>& focused) {
    if (!focused) return; // the focus wasn't in the list
    if (sameObject && propertyEditors_.count(*focused)) propertyEditors_[*focused]->grab_focus();
    else outline_.grab_focus(); // not into the first field, where typing would rename the object
}

} // namespace kevc
