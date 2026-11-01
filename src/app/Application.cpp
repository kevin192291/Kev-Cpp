#include "app/Application.h"

#include "app/MainWindow.h"

#include <gtksourceview/gtksource.h>

namespace kevc {

namespace {

// GTK 4 draws with OpenGL 3.3 or GLES 3.0. On older GPUs (Intel Ironlake laptops have OpenGL 2.1)
// GTK 4.16+ falls back to Vulkan on lavapipe, a CPU driver, and some X servers crash copying its
// frames into a window (XLibre 25 + Mesa crocus segfault on the 520 px wide New Project dialog).
// Cairo draws on the CPU too but hands X plain images, so use it there. GSK_RENDERER is read when
// the first window is shown, so this must run before that; a renderer the user chose wins.
void useCairoWithoutGl() {
    if (!Glib::getenv("GSK_RENDERER").empty()) return;
    try {
        Gdk::Display::get_default()->prepare_gl();
    } catch (const Glib::Error&) {
        Glib::setenv("GSK_RENDERER", "cairo");
    }
}

} // namespace

Application::Application()
    : Gtk::Application("org.kevcpp.KevCpp", Gio::Application::Flags::NON_UNIQUE | Gio::Application::Flags::HANDLES_OPEN) {
    Glib::set_application_name("Kev-C++");
}

Glib::RefPtr<Application> Application::create() {
    return Glib::make_refptr_for_instance<Application>(new Application());
}

void Application::on_startup() {
    Gtk::Application::on_startup();
    useCairoWithoutGl();
    gtk_source_init();

    // The Slate palette (data/kevc-theme.css) is written against GTK's own dark theme, so use that
    // whatever theme the desktop has; the editor gets the matching styles/kevc-slate.xml.
    if (auto settings = Gtk::Settings::get_default()) {
        settings->property_gtk_theme_name() = "Default";
        settings->property_gtk_application_prefer_dark_theme() = true;
    }
    auto palette = Gtk::CssProvider::create();
    palette->load_from_resource("/org/kevcpp/KevCpp/kevc-theme.css");
    Gtk::StyleContext::add_provider_for_display(Gdk::Display::get_default(), palette,
                                                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    installStyleScheme();

    // Some desktop themes don't style GTK4's .monospace class, so set the editor font ourselves.
    auto css = Gtk::CssProvider::create();
    css->load_from_data(".kevc-editor { font-family: \"DejaVu Sans Mono\", \"Liberation Mono\", monospace; "
                        "font-size: 11pt; }\n"
                        ".kevc-log { font-family: \"DejaVu Sans Mono\", monospace; font-size: 10pt; }\n"
                        ".kevc-error { color: #ff6b6b; }\n"
                        ".kevc-warning { color: #f0c050; }\n"
                        ".kevc-tab-menu { min-height: 0; min-width: 0; padding: 0 2px; }\n"
                        // The left panel's tabs (with icons) fit its width without scrolling.
                        ".kevc-compact-tabs > header > tabs > tab { padding: 4px 7px; min-width: 0; }\n");
    Gtk::StyleContext::add_provider_for_display(Gdk::Display::get_default(), css,
                                                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    // App icon, embedded as a GResource (see data/kevcpp.gresource.xml).
    Gtk::IconTheme::get_for_display(Gdk::Display::get_default())->add_resource_path("/org/kevcpp/KevCpp/icons/hicolor");
    Gtk::Window::set_default_icon_name("org.kevcpp.KevCpp");
    settings_.load();
    // Started from a menu or a file manager, PATH lacks what a terminal has (nvm's node, pnpm): use
    // what the terminal would, for the IDE and everything it runs (Vite, pnpm, the language servers).
    {
        const std::string path = Glib::getenv("PATH");
        const auto node = findInPath("node", path);
        NodeSearch where{Glib::get_home_dir(), Glib::getenv("NVM_DIR"), Glib::getenv("PNPM_HOME")};
        if (const std::string n = Glib::getenv("N_PREFIX"); !n.empty()) where.nPrefix = std::filesystem::path(n) / "n";
        const std::string better = terminalLikePath(path, node ? nodeMajor(toolVersion(*node)) : 0, where);
        if (better != path) Glib::setenv("PATH", better, true);
    }
    toolchain_ = Toolchain::detect();
    setupAccelerators();
    add_action("quit", [this] { quit(); });
}

void Application::installStyleScheme() {
    // GtkSourceView 5.12 can list a style scheme in a GResource but not read it, so copy ours to
    // the cache directory (every start, so it follows upgrades) and look for schemes there.
    const std::string dir = Glib::build_filename(Glib::get_user_cache_dir(), "kev-cpp", "styles");
    try {
        auto bytes = Gio::Resource::lookup_data_global("/org/kevcpp/KevCpp/styles/kevc-slate.xml");
        gsize size = 0;
        const auto* data = static_cast<const char*>(bytes->get_data(size));
        g_mkdir_with_parents(dir.c_str(), 0700);
        Glib::file_set_contents(Glib::build_filename(dir, "kevc-slate.xml"), data, static_cast<gssize>(size));
        gtk_source_style_scheme_manager_append_search_path(gtk_source_style_scheme_manager_get_default(), dir.c_str());
    } catch (const Glib::Error&) {
        // The editor falls back to Adwaita-dark (see EditorTab).
    }
    // Languages GtkSourceView doesn't ship (Svelte), the same way and for the same reason.
    const std::string langs = Glib::build_filename(Glib::get_user_cache_dir(), "kev-cpp", "language-specs");
    try {
        auto bytes = Gio::Resource::lookup_data_global("/org/kevcpp/KevCpp/language-specs/svelte.lang");
        gsize size = 0;
        const auto* data = static_cast<const char*>(bytes->get_data(size));
        g_mkdir_with_parents(langs.c_str(), 0700);
        Glib::file_set_contents(Glib::build_filename(langs, "svelte.lang"), data, static_cast<gssize>(size));
        gtk_source_language_manager_append_search_path(gtk_source_language_manager_get_default(), langs.c_str());
    } catch (const Glib::Error&) {
        // .svelte files open without highlighting.
    }
}

void Application::on_activate() {
    if (!toolchain_.missingRequired().empty()) {
        showMissingToolsAndQuit();
        return;
    }
    if (!window_) {
        window_ = new MainWindow(*this);
        add_window(*window_);
    }
    window_->present();
}

void Application::on_window_removed(Gtk::Window* window) {
    // Closing the main window destroys it without emitting "hide"; GTK just drops it from the
    // application. Delete it now so ~MainWindow runs: it closes the floating panel windows, which
    // would otherwise keep the app running, and stops gdb, the language servers and the dev server.
    if (window && window == window_) {
        MainWindow* main = window_;
        window_ = nullptr;
        hold(); // GTK is still tearing the window down: delete it once that's over, before exiting
        Glib::signal_idle().connect_once([this, main] {
            delete main;
            release();
        });
    }
    Gtk::Application::on_window_removed(window);
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
        {"win.find-in-files", {"<Control><Shift>f"}},
        {"win.quick-search", {"<Control>p"}},
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
