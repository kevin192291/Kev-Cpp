#include "ui/Dialogs.h"

#include <array>

namespace kevc {

namespace {

struct TemplateInfo {
    ProjectTemplate tmpl;
    const char* icon;
    const char* title;
    const char* subtitle;
};

constexpr std::array kTemplates{
    TemplateInfo{ProjectTemplate::CppConsole, "kevc-cpp-logo", "C++ Console Application", "CMake + g++"},
    TemplateInfo{ProjectTemplate::CppStatic, "kevc-static-library", "C++ Static Library",
                 "CMake + g++, builds lib<name>.a"},
    TemplateInfo{ProjectTemplate::CppShared, "kevc-shared-library", "C++ Shared Library",
                 "CMake + g++, builds lib<name>.so"},
    TemplateInfo{ProjectTemplate::CConsole, "kevc-c-logo", "C Console Application", "Makefile + gcc"},
    TemplateInfo{ProjectTemplate::GtkmmApp, "kevc-cpp-logo", "GTK 4 Application (gtkmm)",
                 "A window made in the GTK designer, in C++ (CMake + gtkmm)"},
    TemplateInfo{ProjectTemplate::GtkCApp, "kevc-c-logo", "GTK 4 Application (C)",
                 "A window made in the GTK designer, in C (Makefile + GTK)"},
    TemplateInfo{ProjectTemplate::WebTs, "kevc-ts-logo", "TypeScript Web App", "Served on http://localhost:5465"},
    TemplateInfo{ProjectTemplate::SvelteKit, "kevc-svelte-logo", "SvelteKit App",
                 "Svelte 5 + TypeScript + Vite, made by sv create (downloads packages)"},
};

// Destroys a transient window once GTK is done with it.
void deleteLater(Gtk::Window* window) {
    window->signal_hide().connect([window] {
        Glib::signal_idle().connect_once([window] { delete window; });
    });
}

Gtk::Box& buttonRow(Gtk::Button& cancel, Gtk::Button& ok) {
    auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    row->set_halign(Gtk::Align::END);
    row->set_margin_top(8);
    ok.add_css_class("suggested-action");
    row->append(cancel);
    row->append(ok);
    return *row;
}

} // namespace

void promptText(Gtk::Window& parent, const Glib::ustring& title, const Glib::ustring& label,
                const Glib::ustring& initial, std::function<Glib::ustring(const Glib::ustring&)> onAccept,
                int selectChars) {
    auto* window = new Gtk::Window();
    window->set_title(title);
    window->set_transient_for(parent);
    window->set_modal(true);
    window->set_default_size(420, -1);
    window->set_resizable(false);

    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    box->set_margin(16);
    auto* prompt = Gtk::make_managed<Gtk::Label>(label);
    prompt->set_xalign(0.0f);
    auto* entry = Gtk::make_managed<Gtk::Entry>();
    entry->set_text(initial);
    auto* error = Gtk::make_managed<Gtk::Label>();
    error->set_xalign(0.0f);
    error->add_css_class("error");
    error->set_visible(false);
    auto* cancel = Gtk::make_managed<Gtk::Button>("Cancel");
    auto* ok = Gtk::make_managed<Gtk::Button>("OK");

    box->append(*prompt);
    box->append(*entry);
    box->append(*error);
    box->append(buttonRow(*cancel, *ok));
    window->set_child(*box);

    auto accept = [window, entry, error, onAccept] {
        Glib::ustring message = onAccept(entry->get_text());
        if (message.empty()) {
            window->set_visible(false);
        } else {
            error->set_text(message);
            error->set_visible(true);
        }
    };
    entry->signal_activate().connect(accept);
    ok->signal_clicked().connect(accept);
    cancel->signal_clicked().connect([window] { window->set_visible(false); });

    deleteLater(window);
    window->present();
    entry->grab_focus();
    if (selectChars >= 0) {
        entry->select_region(0, selectChars);
        return;
    }
    // Select the file stem so typing replaces "untitled" but keeps the extension.
    const auto dot = initial.rfind('.');
    const auto slash = initial.rfind('/');
    const int start = slash == Glib::ustring::npos ? 0 : static_cast<int>(slash + 1);
    entry->select_region(start, dot == Glib::ustring::npos || dot < static_cast<size_t>(start) ? -1 : static_cast<int>(dot));
}

void askChoice(Gtk::Window& parent, const Glib::ustring& message, const Glib::ustring& detail,
               const std::vector<Glib::ustring>& buttons, int cancelIndex, int defaultIndex,
               std::function<void(int)> onChoice) {
    auto dialog = Gtk::AlertDialog::create(message);
    dialog->set_detail(detail);
    dialog->set_buttons(buttons);
    dialog->set_cancel_button(cancelIndex);
    dialog->set_default_button(defaultIndex);
    dialog->choose(parent, [dialog, onChoice](const Glib::RefPtr<Gio::AsyncResult>& result) {
        int choice = -1;
        try {
            choice = dialog->choose_finish(result);
        } catch (const Glib::Error&) {
        }
        onChoice(choice);
    });
}

void showError(Gtk::Window& parent, const Glib::ustring& message, const Glib::ustring& detail) {
    auto dialog = Gtk::AlertDialog::create(message);
    dialog->set_detail(detail);
    dialog->show(parent);
}

void showInfo(Gtk::Window& parent, const Glib::ustring& message, const Glib::ustring& detail) {
    showError(parent, message, detail);
}

NewProjectDialog::NewProjectDialog(Gtk::Window& parent, const std::filesystem::path& defaultLocation,
                                   CreateHandler onCreate)
    : onCreate_(std::move(onCreate)) {
    set_title("New Project");
    set_transient_for(parent);
    set_modal(true);
    set_default_size(520, -1);

    content_.set_margin(16);

    auto* heading = Gtk::make_managed<Gtk::Label>("Project type");
    heading->set_xalign(0.0f);
    heading->add_css_class("heading");
    content_.append(*heading);

    templates_.set_selection_mode(Gtk::SelectionMode::BROWSE);
    // A click picks the type; a double-click or Enter creates the project (activation calls submit).
    templates_.set_activate_on_single_click(false);
    templates_.add_css_class("boxed-list");
    for (const auto& t : kTemplates) {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        row->set_margin(8);
        auto* icon = Gtk::make_managed<Gtk::Image>();
        icon->set_from_icon_name(t.icon);
        icon->set_pixel_size(32);
        auto* text = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        text->set_valign(Gtk::Align::CENTER);
        auto* title = Gtk::make_managed<Gtk::Label>(t.title);
        title->set_xalign(0.0f);
        auto* subtitle = Gtk::make_managed<Gtk::Label>(t.subtitle);
        subtitle->set_xalign(0.0f);
        subtitle->add_css_class("dim-label");
        text->append(*title);
        text->append(*subtitle);
        row->append(*icon);
        row->append(*text);
        templates_.append(*row);
    }
    templates_.select_row(*templates_.get_row_at_index(0));
    templates_.signal_row_activated().connect([this](Gtk::ListBoxRow*) { submit(); });
    auto* frame = Gtk::make_managed<Gtk::Frame>();
    frame->set_child(templates_);
    content_.append(*frame);

    auto* grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_row_spacing(8);
    grid->set_column_spacing(8);
    grid->set_margin_top(8);
    auto* nameLabel = Gtk::make_managed<Gtk::Label>("Name");
    nameLabel->set_xalign(1.0f);
    auto* locationLabel = Gtk::make_managed<Gtk::Label>("Location");
    locationLabel->set_xalign(1.0f);
    name_.set_hexpand(true);
    name_.set_placeholder_text("MyProject");
    name_.signal_activate().connect([this] { submit(); });
    location_.set_text(defaultLocation.string());
    location_.set_hexpand(true);
    auto* browse = Gtk::make_managed<Gtk::Button>();
    browse->set_icon_name("kevc-open");
    browse->set_tooltip_text("Choose folder");
    browse->signal_clicked().connect([this] { this->browse(); });
    grid->attach(*nameLabel, 0, 0);
    grid->attach(name_, 1, 0, 2, 1);
    grid->attach(*locationLabel, 0, 1);
    grid->attach(location_, 1, 1);
    grid->attach(*browse, 2, 1);
    content_.append(*grid);

    error_.set_xalign(0.0f);
    error_.add_css_class("error");
    error_.set_wrap(true);
    error_.set_visible(false);
    content_.append(error_);

    auto* cancel = Gtk::make_managed<Gtk::Button>("Cancel");
    auto* create = Gtk::make_managed<Gtk::Button>("Create");
    cancel->signal_clicked().connect([this] { set_visible(false); });
    create->signal_clicked().connect([this] { submit(); });
    content_.append(buttonRow(*cancel, *create));

    set_child(content_);
    deleteLater(this);
    name_.grab_focus();
}

void NewProjectDialog::browse() {
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Project Location");
    dialog->set_initial_folder(Gio::File::create_for_path(location_.get_text()));
    dialog->select_folder(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
            if (auto folder = dialog->select_folder_finish(result)) location_.set_text(folder->get_path());
        } catch (const Glib::Error&) {
            // Cancelled.
        }
    });
}

void NewProjectDialog::submit() {
    const std::string name = name_.get_text();
    if (name.empty()) {
        error_.set_text("Enter a project name.");
        error_.set_visible(true);
        return;
    }
    auto* row = templates_.get_selected_row();
    const ProjectTemplate tmpl = kTemplates[row ? row->get_index() : 0].tmpl;
    Glib::ustring message = onCreate_(name, std::string(location_.get_text()), tmpl);
    if (message.empty()) {
        set_visible(false);
    } else {
        error_.set_text(message);
        error_.set_visible(true);
    }
}

} // namespace kevc
