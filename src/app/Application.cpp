#include "app/Application.h"

#include "app/MainWindow.h"

#include <gtksourceview/gtksource.h>

namespace kevc {

Application::Application()
    : Gtk::Application("org.kevcpp.KevCpp", Gio::Application::Flags::NON_UNIQUE | Gio::Application::Flags::HANDLES_OPEN) {
    Glib::set_application_name("Kev-C++");
}

Glib::RefPtr<Application> Application::create() {
    return Glib::make_refptr_for_instance<Application>(new Application());
}

void Application::on_startup() {
    Gtk::Application::on_startup();
    gtk_source_init();

    // Some desktop themes don't style GTK4's .monospace class, so set the editor font ourselves.
    auto css = Gtk::CssProvider::create();
    css->load_from_data(".kevc-editor { font-family: \"DejaVu Sans Mono\", \"Liberation Mono\", monospace; "
                        "font-size: 11pt; }\n"
                        ".kevc-log { font-family: \"DejaVu Sans Mono\", monospace; font-size: 10pt; }\n"
                        ".kevc-error { color: #e01b24; }\n"
                        ".kevc-warning { color: #e5a50a; }\n"
                        ".kevc-tab-menu { min-height: 0; min-width: 0; padding: 0 2px; }\n");
    Gtk::StyleContext::add_provider_for_display(Gdk::Display::get_default(), css,
                                                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    // App icon, embedded as a GResource (see data/kevcpp.gresource.xml).
    Gtk::IconTheme::get_for_display(Gdk::Display::get_default())->add_resource_path("/org/kevcpp/KevCpp/icons/hicolor");
    Gtk::Window::set_default_icon_name("org.kevcpp.KevCpp");
    toolchain_ = Toolchain::detect();
    setupAccelerators();
    add_action("quit", [this] { quit(); });
}

void Application::on_activate() {
    if (!toolchain_.missingRequired().empty()) {
        showMissingToolsAndQuit();
        return;
    }
    if (!window_) {
        window_ = new MainWindow(*this);
        add_window(*window_);
        window_->signal_hide().connect([this] {
            delete window_;
            window_ = nullptr;
        });
    }
    window_->present();
}

void Application::on_open(const type_vec_files& files, const Glib::ustring&) {
    on_activate();
    if (window_ && !files.empty()) window_->openPath(files.front()->get_path());
}

void Application::showMissingToolsAndQuit() {
    Glib::ustring detail = "Kev-C++ needs these programs on your PATH:\n\n";
    for (const ToolInfo* t : toolchain_.missingRequired())
        detail += "  • " + t->name + " — " + t->purpose + "\n";
    detail += "\nOn Ubuntu/Mint: sudo apt install build-essential cmake gdb nodejs";

    auto dialog = Gtk::AlertDialog::create("Required tools are missing");
    dialog->set_detail(detail);
    dialog->set_modal(true);
    hold();
    dialog->choose([this, dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
            dialog->choose_finish(result);
        } catch (const Glib::Error&) {
            // Dismissed with Escape or the close button; nothing to handle.
        }
        release();
        quit();
    });
}

void Application::setupAccelerators() {
    const std::vector<std::pair<const char*, std::vector<Glib::ustring>>> accels = {
        {"app.quit", {"<Control>q"}},
        {"win.new-project", {"<Control><Shift>n"}},
        {"win.open-project", {"<Control>o"}},
        {"win.new-file", {"<Control>n"}},
        {"win.save", {"<Control>s"}},
        {"win.save-all", {"<Control><Shift>s"}},
        {"win.close-tab", {"<Control>w"}},
        {"win.find", {"<Control>f"}},
        {"win.replace", {"<Control>r"}},
        {"win.goto-line", {"<Control>g"}},
        {"win.goto-definition", {"<Control>F12"}},
        {"win.project-options", {"<Alt>p"}},
        {"win.compile", {"F9"}},
        {"win.run", {"F10"}},
        {"win.compile-run", {"F11"}},
        {"win.rebuild", {"F12"}},
        {"win.debug", {"F5"}},
        {"win.debug-stop", {"<Shift>F5"}},
        {"win.step-over", {"F6"}},
        {"win.step-into", {"F7"}},
        {"win.step-out", {"<Shift>F7"}},
        {"win.debug-continue", {"F8"}},
        {"win.toggle-breakpoint", {"<Control>F5"}},
        {"win.stop", {"<Control>F2"}},
    };
    for (const auto& [action, keys] : accels) set_accels_for_action(action, keys);
}

} // namespace kevc
