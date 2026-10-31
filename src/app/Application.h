#pragma once

#include "core/Settings.h"
#include "core/Toolchain.h"

#include <gtkmm.h>

namespace kevc {

class MainWindow;

class Application : public Gtk::Application {
public:
    static Glib::RefPtr<Application> create();

    const Toolchain& toolchain() const { return toolchain_; }
    Settings& settings() { return settings_; }

protected:
    Application();

    void on_startup() override;
    void on_activate() override;
    void on_open(const type_vec_files& files, const Glib::ustring& hint) override;
    void on_window_removed(Gtk::Window* window) override;

private:
    void showMissingToolsAndQuit();
    void installStyleScheme();
    void setupAccelerators();

    Toolchain toolchain_;
    Settings settings_{Settings::defaultFile()};
    MainWindow* window_ = nullptr;
};

} // namespace kevc
