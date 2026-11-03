#pragma once

#include "ui/DevServerPanel.ui.h"

#include "ui/IconWidgets.h"

#include <string>

namespace kevc {

// The bottom "Dev Server" tab: what the server (the bundled one or the project's Vite) is doing,
// Open in Browser, Start/Stop, and its output. Its layout is DevServerPanel.ui (edit it on Kev-C++'s
// Design page); the Start/Stop button (toggle_) works as a TaskButton.
class DevServerPanel : public DevServerPanelUi {
public:
    static Glib::RefPtr<DevServerPanel> create() { return DevServerPanelUi::create<DevServerPanel>(); }
    DevServerPanel(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder);

    // Running: Stop rather than Start; ready: serving, so it can be opened in a browser.
    void setState(bool running, bool ready);
    void setStatusText(const Glib::ustring& text) { status_->set_text(text); }
    void setStatusMarkup(const Glib::ustring& markup) { status_->set_markup(markup); }
    // Adds output at the end and scrolls to it.
    void appendLog(const std::string& text);
    void clearLog();

    sigc::signal<void()>& signalStart() { return task_.signalStart(); }
    sigc::signal<void()>& signalStop() { return task_.signalStop(); }
    sigc::signal<void()>& signalOpen() { return openClicked_; }

protected:
    void on_open_clicked() override;

private:
    TaskButton task_{*toggle_, "kevc-stop", "Stop the dev server", "Stop"};
    sigc::signal<void()> openClicked_;
};

} // namespace kevc
