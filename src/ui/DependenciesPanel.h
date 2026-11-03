#pragma once

#include "core/WebReports.h"

#include "ui/DependenciesPanel.ui.h"

#include <map>
#include <string>
#include <vector>

namespace kevc {

// The Dependencies panel for web projects: packages with newer versions (npm outdated) and known
// vulnerabilities (npm audit), on demand since both ask the npm registry.
class DependenciesPanel : public DependenciesPanelUi {
public:
    static Glib::RefPtr<DependenciesPanel> create() { return DependenciesPanelUi::create<DependenciesPanel>(); }
    DependenciesPanel(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder);

    void setReports(const std::vector<OutdatedPackage>& outdated, const AuditReport& audit);
    void showMessage(const Glib::ustring& text);
    void setBusy(bool busy);

    sigc::signal<void()>& signalRefresh() { return refresh_; }
    // A package name: open package.json at its line.
    sigc::signal<void(const std::string&)>& signalOpenPackage() { return openPackage_; }

protected:
    void on_refresh_clicked() override;

private:
    void clear();
    void addRow(const Glib::ustring& markup, const std::string& package);

    std::map<Gtk::ListBoxRow*, std::string> packages_;
    sigc::signal<void()> refresh_;
    sigc::signal<void(const std::string&)> openPackage_;
};

} // namespace kevc
