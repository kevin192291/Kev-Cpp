#pragma once

#include "ui/ServersWindow.ui.h"

#include "core/Servers.h"
#include "process/BackgroundJob.h"

#include <memory>
#include <string>
#include <vector>

namespace kevc {

// Tools → Running Servers (SvelteKit projects, also on the web toolbar): every port the user's
// programs listen on, what each program is, whether it answers as a web server should, and a Kill
// button. The layout is ServersWindow.ui (edit it on Kev-C++'s Design page); this class makes a row
// per server below the design's headings. It deletes itself when closed.
class ServersWindow : public ServersWindowUi {
public:
    static ServersWindow* create(Gtk::Window& parent) { return ServersWindowUi::create<ServersWindow>(parent); }
    ServersWindow(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder, Gtk::Window& parent);
    ~ServersWindow() override;

protected:
    void on_refresh_clicked() override;
    void on_close_clicked() override;

private:
    struct Row {
        servers::Server server;
        std::string what; // the program in a few words
        Gtk::Label* answer = nullptr;
        Gtk::Button* kill = nullptr;
    };
    // Lists the ports again (and asks each server whether it answers); the message line stays.
    void refresh();
    void showAnswer(size_t row, const servers::Probe& probe);
    void kill(size_t row);
    // Rows whose programs have all gone say so (another row can be the same program on another port).
    void markStopped();
    void say(const std::string& text, bool error = false);

    std::vector<Row> rows_;
    BackgroundJob probes_;
    int listing_ = 0; // answers for an older listing are dropped
    // Kills outlive the window (so a stubborn program still gets its SIGKILL); this says it's there.
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

} // namespace kevc
