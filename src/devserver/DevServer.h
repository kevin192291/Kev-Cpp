#pragma once

#include "process/ManagedProcess.h"

#include <giomm.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace kevc {

// A web dev server for the open folder: the bundled one (tools/devserver/devserver.mjs under node)
// or the project's own Vite. Either way it is a ManagedProcess whose output says when it's ready.
class DevServer {
public:
    static constexpr int kDefaultPort = 5465; // the bundled server
    static constexpr int kExitPortInUse = 2;  // the bundled server's exit status when the port is taken
    // Reads one line of output; returns the server's URL once it says it's ready, else empty.
    using ReadyParser = std::function<std::string(const std::string& line)>;

    DevServer();
    ~DevServer() = default; // ManagedProcess stops the server

    // The bundled server's command and ready marker.
    static std::vector<std::string> bundledCommand(const std::string& node, const std::filesystem::path& script,
                                                   const std::filesystem::path& root, int port = kDefaultPort);
    static ReadyParser bundledReady();

    // Throws Glib::Error if the program cannot be started. `label` names it in messages ("Vite").
    void start(const std::vector<std::string>& argv, const std::filesystem::path& root, ReadyParser ready,
               std::string label, const std::string& pathPrepend = {},
               const std::vector<std::pair<std::string, std::string>>& env = {});
    void stop();
    bool running() const { return process_.running(); }
    bool ready() const { return !url_.empty(); }
    const std::string& url() const { return url_; }
    const std::filesystem::path& root() const { return root_; }
    const std::string& label() const { return label_; }

    sigc::signal<void(const std::string& line)>& signalLog() { return log_; }
    sigc::signal<void(const std::string& url)>& signalReady() { return ready_; }
    // Exit status (kExitPortInUse when the bundled server's port was taken), or -1 if killed.
    sigc::signal<void(int status)>& signalStopped() { return stopped_; }

private:
    std::filesystem::path root_;
    std::string url_;
    std::string label_;
    ReadyParser readyParser_;
    ManagedProcess process_;
    sigc::signal<void(const std::string&)> log_;
    sigc::signal<void(const std::string&)> ready_;
    sigc::signal<void(int)> stopped_;
};

} // namespace kevc
