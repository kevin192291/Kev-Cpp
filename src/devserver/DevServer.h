#pragma once

#include <giomm.h>

#include <filesystem>
#include <memory>
#include <string>

namespace kevc {

// The TypeScript dev server (tools/devserver/devserver.mjs) running under node.
class DevServer {
public:
    static constexpr int kDefaultPort = 5465;
    static constexpr int kExitPortInUse = 2;

    DevServer(std::string node, std::filesystem::path script);
    ~DevServer();

    // Throws Glib::Error if node cannot be started.
    void start(const std::filesystem::path& root, int port = kDefaultPort);
    void stop();
    bool running() const { return static_cast<bool>(process_); }
    bool ready() const { return !url_.empty(); }
    const std::string& url() const { return url_; }
    const std::filesystem::path& root() const { return root_; }

    sigc::signal<void(const std::string& line)>& signalLog() { return log_; }
    sigc::signal<void(const std::string& url)>& signalReady() { return ready_; }
    // Exit status of node (kExitPortInUse when the port was taken), or -1 if it was killed.
    sigc::signal<void(int status)>& signalStopped() { return stopped_; }

private:
    void readNext();

    std::string node_;
    std::filesystem::path script_;
    std::filesystem::path root_;
    std::string url_;
    Glib::RefPtr<Gio::Subprocess> process_;
    Glib::RefPtr<Gio::DataInputStream> output_;
    Glib::RefPtr<Gio::Cancellable> cancellable_;
    std::shared_ptr<DevServer*> self_;
    sigc::signal<void(const std::string&)> log_;
    sigc::signal<void(const std::string&)> ready_;
    sigc::signal<void(int)> stopped_;
};

} // namespace kevc
