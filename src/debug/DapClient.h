#pragma once

#include "core/Dap.h"
#include "core/Lsp.h"

#include <giomm.h>

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace kevc {

// One Debug Adapter Protocol session over TCP (js-debug's server takes one connection per session:
// the root one, and one for each child session it asks for with startDebugging).
class DapClient {
public:
    using json = dap::json;
    // `success` false: `message` says why.
    using Callback = std::function<void(bool success, const json& body, const std::string& message)>;

    DapClient();
    ~DapClient();
    DapClient(const DapClient&) = delete;
    DapClient& operator=(const DapClient&) = delete;

    void connect(int port);
    void request(const std::string& command, json arguments, Callback callback = {});
    // Answers one of the adapter's reverse requests (startDebugging, runInTerminal).
    void respond(int requestSeq, const std::string& command, bool success, json body = json::object());
    bool connected() const { return static_cast<bool>(connection_); }

    sigc::signal<void()>& signalConnected() { return connected_; }
    sigc::signal<void(const std::string& event, const json& body)>& signalEvent() { return event_; }
    sigc::signal<void(int seq, const std::string& command, const json& arguments)>& signalReverseRequest() { return reverse_; }
    sigc::signal<void(const std::string& reason)>& signalClosed() { return closed_; }

private:
    void readNext();
    void send(const json& message);
    void close(const std::string& reason);

    Glib::RefPtr<Gio::SocketConnection> connection_;
    Glib::RefPtr<Gio::Cancellable> cancellable_ = Gio::Cancellable::create();
    lsp::MessageFramer framer_;
    int seq_ = 0;
    std::map<int, Callback> pending_;
    std::shared_ptr<DapClient*> self_;
    sigc::signal<void()> connected_;
    sigc::signal<void(const std::string&, const json&)> event_;
    sigc::signal<void(int, const std::string&, const json&)> reverse_;
    sigc::signal<void(const std::string&)> closed_;
};

} // namespace kevc
