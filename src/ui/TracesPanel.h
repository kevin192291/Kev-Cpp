#pragma once

#include "core/WebReports.h"

#include <gtkmm.h>

#include <map>
#include <string>

namespace kevc {

// The Traces panel: requests the running app traced (OpenTelemetry spans sent to Kev-C++'s local
// collector), newest first, and a waterfall of the selected one: which hook, load or fetch ran
// when, nested under what, and how long each took.
class TracesPanel : public Gtk::Paned {
public:
    TracesPanel();

    void setTraces(const TraceStore& store);
    void setStatus(const Glib::ustring& text);

    sigc::signal<void()>& signalClear() { return clear_; }

private:
    void draw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);
    void select(const std::string& id);

    Gtk::Box left_{Gtk::Orientation::VERTICAL};
    Gtk::Label status_;
    Gtk::ScrolledWindow listScroller_;
    Gtk::ListBox list_;
    Gtk::Label hint_;
    Gtk::ScrolledWindow canvasScroller_;
    Gtk::DrawingArea canvas_;
    std::map<Gtk::ListBoxRow*, std::string> rowIds_;
    std::string selected_;
    std::deque<Trace> traces_; // copies: the store drops old traces while they may still be shown
    Trace current_;
    sigc::signal<void()> clear_;
};

} // namespace kevc
