#pragma once

#include "core/Mi.h"

#include <giomm.h>

#include <filesystem>
#include <functional>
#include <fstream>
#include <map>
#include <memory>
#include <string>

namespace kevc {

// A gdb process driven over GDB/MI (`gdb --interpreter=mi3`).
class GdbSession {
public:
    // Receives the ^done / ^running / ^error record answering one command.
    using Callback = std::function<void(const mi::Record& result)>;

    explicit GdbSession(std::string gdbPath);
    ~GdbSession();

    // Throws Glib::Error if gdb cannot be started.
    void start(const std::filesystem::path& cwd);
    bool running() const { return static_cast<bool>(process_); }

    // `command` is an MI command without a token, e.g. "-exec-next".
    void send(const std::string& command, Callback callback = {});

    sigc::signal<void(const mi::Record&)>& signalExec() { return exec_; }
    sigc::signal<void(const mi::Record&)>& signalNotify() { return notify_; }
    sigc::signal<void(const std::string&)>& signalConsole() { return console_; }
    sigc::signal<void()>& signalExited() { return exited_; }

private:
    void readNext();
    void dispatch(const std::string& line);
    void onExited();

    std::string gdbPath_;
    Glib::RefPtr<Gio::Subprocess> process_;
    Glib::RefPtr<Gio::OutputStream> input_;
    Glib::RefPtr<Gio::DataInputStream> output_;
    Glib::RefPtr<Gio::Cancellable> cancellable_;
    int nextToken_ = 1;
    std::map<int, Callback> pending_;
    std::shared_ptr<GdbSession*> self_;
    std::unique_ptr<std::ofstream> log_;

    sigc::signal<void(const mi::Record&)> exec_;
    sigc::signal<void(const mi::Record&)> notify_;
    sigc::signal<void(const std::string&)> console_;
    sigc::signal<void()> exited_;
};

} // namespace kevc
