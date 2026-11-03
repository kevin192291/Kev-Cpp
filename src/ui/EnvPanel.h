#pragma once

#include "core/SvelteKit.h"

#include "ui/EnvPanel.ui.h"

#include <filesystem>
#include <map>
#include <optional>

namespace kevc {

// The Environment panel for SvelteKit projects: each variable from the .env files and src/, whether
// it's public or private, where it's defined and used, and problems (a private variable in code that
// reaches the browser, a static one no .env defines, the wrong module for its prefix).
class EnvPanel : public EnvPanelUi {
public:
    static Glib::RefPtr<EnvPanel> create() { return EnvPanelUi::create<EnvPanel>(); }
    EnvPanel(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder);

    void setReport(const kit::EnvReport& report, const std::filesystem::path& root);
    void showMessage(const Glib::ustring& text);

    sigc::signal<void(const std::filesystem::path& file, int line)>& signalOpen() { return open_; }
    // "Edit .env": open (creating it if needed) the project's .env file.
    sigc::signal<void()>& signalEditEnv() { return editEnv_; }

protected:
    void on_edit_env_clicked() override;

private:
    struct Target {
        std::filesystem::path file;
        int line = 1;
    };
    void clear();
    void addRow(const char* icon, const Glib::ustring& markup, std::optional<Target> target);

    std::map<Gtk::ListBoxRow*, Target> targets_;
    sigc::signal<void(const std::filesystem::path&, int)> open_;
    sigc::signal<void()> editEnv_;
};

} // namespace kevc
