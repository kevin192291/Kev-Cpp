#include "ui/NewProjectDialog.h"

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

} // namespace

NewProjectDialog::NewProjectDialog(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder,
                                   Gtk::Window& parent, const std::filesystem::path& defaultLocation,
                                   CreateHandler onCreate)
    : NewProjectDialogUi(object, builder), onCreate_(std::move(onCreate)) {
    set_transient_for(parent);
    // A click picks the type; a double-click or Enter creates the project (the design's
    // row-activated handler), as Enter in either field does (Create is the default widget).
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
        templates_->append(*row);
    }
    templates_->select_row(*templates_->get_row_at_index(0));
    location_->set_text(defaultLocation.string());
    closeOnEscape(*this);
    deleteWhenHidden(this);
    name_->grab_focus();
}

void NewProjectDialog::on_templates_row_activated() {
    submit();
}

void NewProjectDialog::on_create_clicked() {
    submit();
}

void NewProjectDialog::on_cancel_clicked() {
    set_visible(false);
}

void NewProjectDialog::on_browse_clicked() {
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Project Location");
    dialog->set_initial_folder(Gio::File::create_for_path(location_->get_text()));
    dialog->select_folder(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
            if (auto folder = dialog->select_folder_finish(result)) location_->set_text(folder->get_path());
        } catch (const Glib::Error&) {
            // Cancelled.
        }
    });
}

void NewProjectDialog::submit() {
    const std::string name = name_->get_text();
    if (name.empty()) {
        error_->set_text("Enter a project name.");
        error_->set_visible(true);
        return;
    }
    auto* row = templates_->get_selected_row();
    const ProjectTemplate tmpl = kTemplates[row ? row->get_index() : 0].tmpl;
    Glib::ustring message = onCreate_(name, std::string(location_->get_text()), tmpl);
    if (message.empty()) {
        set_visible(false);
    } else {
        error_->set_text(message);
        error_->set_visible(true);
    }
}

} // namespace kevc
