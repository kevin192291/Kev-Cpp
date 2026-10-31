#pragma once

#include "core/Lsp.h"

#include <giomm.h>

#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace kevc {

// One language server process speaking JSON-RPC over stdio.
// Requests and notifications sent before the server has initialized are queued.
class LspClient {
public:
    using json = lsp::json;
    // `error` is null on success.
    using Callback = std::function<void(const json& result, const json* error)>;

    LspClient(std::string name, std::vector<std::string> argv, std::filesystem::path root, json initOptions,
              std::filesystem::path logFile);
    ~LspClient();

    // Throws Glib::Error if the process cannot be started.
    void start();

    const std::string& name() const { return name_; }
    bool ready() const { return ready_; }
    bool running() const { return static_cast<bool>(process_); }
    const json& capabilities() const { return capabilities_; }

    // Answers the server's workspace/configuration requests, section by section (default: null).
    void setConfigurationHandler(std::function<json(const std::string& section)> handler) {
        configuration_ = std::move(handler);
    }

    void request(const std::string& method, json params, Callback callback);
    void notify(const std::string& method, json params);

    sigc::signal<void(const std::string& uri, const json& diagnostics)>& signalDiagnostics() { return diagnostics_; }
    sigc::signal<void()>& signalReady() { return readySignal_; }
    sigc::signal<void(const std::string& reason)>& signalExited() { return exited_; }

private:
    void send(const json& message);
    void writeNext();
    void readNext();
    void dispatch(const json& message);
    void answerServerRequest(const json& message);
    void onExited(const std::string& reason);

    std::string name_;
    std::vector<std::string> argv_;
    std::filesystem::path root_;
    json initOptions_;
    std::function<json(const std::string&)> configuration_;
    std::filesystem::path logFile_;

    Glib::RefPtr<Gio::Subprocess> process_;
    Glib::RefPtr<Gio::OutputStream> input_;
    Glib::RefPtr<Gio::InputStream> output_;
    Glib::RefPtr<Gio::Cancellable> cancellable_;
    lsp::MessageFramer framer_;
    std::deque<std::string> writeQueue_;
    bool writing_ = false;

    bool ready_ = false;
    json capabilities_ = json::object();
    std::vector<json> pendingUntilReady_;
    int nextId_ = 1;
    std::map<int, Callback> pending_;

    std::shared_ptr<LspClient*> self_;
    sigc::signal<void(const std::string&, const json&)> diagnostics_;
    sigc::signal<void()> readySignal_;
    sigc::signal<void(const std::string&)> exited_;
};

} // namespace kevc
