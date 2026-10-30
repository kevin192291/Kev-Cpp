#pragma once

#include "core/BuildPlan.h"

#include <giomm.h>

#include <sys/types.h>

#include <memory>
#include <string>
#include <vector>

namespace kevc {

// Runs build commands one after another, streaming their output (stdout and stderr merged) line by
// line. Stops at the first command that fails.
class BuildRunner {
public:
    BuildRunner();
    ~BuildRunner();
    BuildRunner(const BuildRunner&) = delete;
    BuildRunner& operator=(const BuildRunner&) = delete;

    // Ignored while a build is running. If the first command can't be started, signalFinished is
    // emitted before this returns.
    void start(std::vector<Command> commands);
    // Stops the current command and every process it started; finishes with success = false.
    void cancel();
    bool running() const { return running_; }

    // Each command's text (isCommand = true), followed by its output lines.
    sigc::signal<void(const std::string& line, bool isCommand)>& signalLine() { return line_; }
    // After the last command succeeded, or the first one failed, couldn't start or was cancelled.
    sigc::signal<void(bool success)>& signalFinished() { return finished_; }

private:
    void startNext();
    void readNext();
    void stepDone();
    void finish(bool success);
    void signalGroup(int sig);

    std::vector<Command> commands_;
    size_t next_ = 0;
    bool running_ = false;
    bool cancelled_ = false;
    // The current command. It is done only when it has exited *and* its output has been read to
    // the end, so its last lines are never reported after the next command (or the result).
    Glib::RefPtr<Gio::Subprocess> process_;
    Glib::RefPtr<Gio::DataInputStream> output_;
    Glib::RefPtr<Gio::Cancellable> cancellable_;
    pid_t pid_ = 0; // also the id of its process group
    bool exited_ = false;
    bool eof_ = false;
    std::shared_ptr<BuildRunner*> self_;
    sigc::signal<void(const std::string&, bool)> line_;
    sigc::signal<void(bool)> finished_;
};

} // namespace kevc
