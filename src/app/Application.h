#pragma once

#include "core/Toolchain.h"

#include <gtkmm.h>

namespace kevc {

class MainWindow;

class Application : public Gtk::Application {
public:
    static Glib::RefPtr<Application> create();

    const Toolchain& toolchain() const { return toolchain_; }

protected:
    Application();

    void on_startup() override;
    void on_activate() override;
    void on_open(const type_vec_files& files, const Glib::ustring& hint) override;

private:
    void showMissingToolsAndQuit();
    void setupAccelerators();

    Toolchain toolchain_;
    MainWindow* window_ = nullptr;
};

} // namespace kevc
