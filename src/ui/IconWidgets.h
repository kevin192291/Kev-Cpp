#pragma once

#include <gtkmm.h>

namespace kevc {

// Widgets that show the kevc-* icons (data/icons/hicolor/*/actions; see core/Icons.h).

constexpr int kToolbarIconSize = 24;

// An icon beside a label, e.g. for notebook tabs. Managed.
Gtk::Widget* makeIconLabel(const char* icon, const Glib::ustring& text);

// What makes a designed button one for something that runs: it starts it, and while it runs the
// button shows a second icon (Stop), label and tooltip, and stops it. Run, Debug and the dev server
// all work this way. The button's icon (its icon-name, or a GtkImage child, alone or in a box beside
// a GtkLabel), label and tooltip as designed are what it shows while nothing runs. The button stays
// the design's, so it must outlive this.
class TaskButton : public sigc::trackable {
public:
    TaskButton(Gtk::Button& button, const char* runningIcon, Glib::ustring runningTooltip,
               Glib::ustring runningLabel = {});
    TaskButton(const TaskButton&) = delete;
    TaskButton& operator=(const TaskButton&) = delete;

    // What it says when hovered, in each state (Run means something else in a web project), and back
    // to the design's tooltip and the running one it was made with.
    void setTooltips(Glib::ustring tooltip, Glib::ustring runningTooltip);
    void resetTooltips() { setTooltips(designTooltip_, designRunningTooltip_); }
    void setRunning(bool running);
    bool running() const { return running_; }
    Gtk::Button& button() { return button_; }

    sigc::signal<void()>& signalStart() { return start_; }
    sigc::signal<void()>& signalStop() { return stop_; }

private:
    void update();

    Gtk::Button& button_;
    bool iconName_ = false; // the design set the button's icon-name (else its icon is a GtkImage child)
    Gtk::Image* image_ = nullptr;
    Gtk::Label* text_ = nullptr;
    Glib::ustring icon_, runningIcon_;
    Glib::ustring tooltip_, runningTooltip_, designTooltip_, designRunningTooltip_;
    Glib::ustring label_, runningLabel_;
    bool running_ = false;
    sigc::signal<void()> start_;
    sigc::signal<void()> stop_;
};

} // namespace kevc
