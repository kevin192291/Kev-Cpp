#pragma once

#include <giomm.h>

#include <sys/types.h>

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace kevc {

// A long-running child process: a dev server, Vite, a gdb server. It runs in its own process group,
// so stopping it also stops everything it started (npm → sh → vite → esbuild). It is told to exit if
// the IDE dies (a parent-death signal, and a stdin pipe that closes for tools that watch it). Output
// (stdout and stderr together) arrives line by line.
class ManagedProcess {
public:
    struct Options {
        std::vector<std::string> argv;
        std::filesystem::path cwd;
        std::vector<std::pair<std::string, std::string>> env; // variables to set
        std::string pathPrepend; // directories put in front of $PATH, e.g. node's
    };

    ManagedProcess();
    ~ManagedProcess(); // stops the process group
    ManagedProcess(const ManagedProcess&) = delete;
    ManagedProcess& operator=(const ManagedProcess&) = delete;

    // Throws Glib::Error if the program can't be started. Ignored while running.
    void start(const Options& options);
    // SIGTERM to the whole group, then SIGKILL if it's still there after `graceMs`.
    void stop(int graceMs = 2000);
    bool running() const { return static_cast<bool>(process_); }
    pid_t pid() const { return pid_; }

    sigc::signal<void(const std::string& line)>& signalLine() { return line_; }
    // Exit status, or -1 if it was killed by a signal.
    sigc::signal<void(int status)>& signalExited() { return exited_; }

private:
    void readNext();

    Glib::RefPtr<Gio::Subprocess> process_;
    Glib::RefPtr<Gio::DataInputStream> output_;
    Glib::RefPtr<Gio::Cancellable> cancellable_;
    pid_t pid_ = 0; // also its process group id
    std::shared_ptr<ManagedProcess*> self_;
    sigc::signal<void(const std::string&)> line_;
    sigc::signal<void(int)> exited_;
};

} // namespace kevc
