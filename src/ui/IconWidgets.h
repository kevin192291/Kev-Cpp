#pragma once

#include <gtkmm.h>

namespace kevc {

// Widgets that show the kevc-* icons (data/icons/hicolor/*/actions; see core/Icons.h).

constexpr int kToolbarIconSize = 24;

// An icon beside a label, e.g. for notebook tabs. Managed.
Gtk::Widget* makeIconLabel(const char* icon, const Glib::ustring& text);

// One button for something that runs: it starts it, and while it runs it shows a second icon
// (Stop, by default) and stops it. Run, Debug and the dev server all work this way.
class TaskButton : public Gtk::Button {
public:
    TaskButton(const char* icon, Glib::ustring tooltip, const char* runningIcon, Glib::ustring runningTooltip,
               int iconSize = kToolbarIconSize);

    // Shows text beside the icon in each state ("Start" / "Stop").
    void setLabels(Glib::ustring label, Glib::ustring runningLabel);
    void setRunning(bool running);
    bool running() const { return running_; }

    sigc::signal<void()>& signalStart() { return start_; }
    sigc::signal<void()>& signalStop() { return stop_; }

private:
    void update();

    const char* icon_;
    const char* runningIcon_;
    Glib::ustring tooltip_;
    Glib::ustring runningTooltip_;
    Glib::ustring label_;
    Glib::ustring runningLabel_;
    Gtk::Box box_{Gtk::Orientation::HORIZONTAL, 6};
    Gtk::Image image_;
    Gtk::Label text_;
    bool running_ = false;
    sigc::signal<void()> start_;
    sigc::signal<void()> stop_;
};

} // namespace kevc
