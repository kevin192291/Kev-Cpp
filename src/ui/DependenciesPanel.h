#pragma once

#include "core/WebReports.h"

#include <gtkmm.h>

#include <map>
#include <string>
#include <vector>

namespace kevc {

// The Dependencies panel for web projects: packages with newer versions (npm outdated) and known
// vulnerabilities (npm audit), on demand since both ask the npm registry.
class DependenciesPanel : public Gtk::Box {
public:
    DependenciesPanel();

    void setReports(const std::vector<OutdatedPackage>& outdated, const AuditReport& audit);
    void showMessage(const Glib::ustring& text);
    void setBusy(bool busy);

    sigc::signal<void()>& signalRefresh() { return refresh_; }
    // A package name: open package.json at its line.
    sigc::signal<void(const std::string&)>& signalOpenPackage() { return openPackage_; }

private:
    void clear();
    void addRow(const Glib::ustring& markup, const std::string& package);

    Gtk::Label summary_;
    Gtk::Button refreshButton_{"Check for updates"};
    Gtk::ScrolledWindow scroller_;
    Gtk::ListBox list_;
    Gtk::Label message_;
    std::map<Gtk::ListBoxRow*, std::string> packages_;
    sigc::signal<void()> refresh_;
    sigc::signal<void(const std::string&)> openPackage_;
};

} // namespace kevc
