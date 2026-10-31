#pragma once

#include "core/Dap.h"
#include "debug/DapClient.h"
#include "process/ManagedProcess.h"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace kevc {

// Debugs a Vite project's server code and, when a Chromium-based browser is installed, its browser
// code too, through Microsoft's js-debug. js-debug runs as a DAP server; we open a root session that
// launches Vite under node, and js-debug asks (startDebugging) for a child session for every process
// or page it attaches to. Breakpoints go to every session; whichever pauses becomes the current one.
class JsDebugger {
public:
    struct Options {
        std::string node;
        std::filesystem::path server;                // js-debug/src/dapDebugServer.js
        std::filesystem::path root;                  // the project
        std::vector<std::string> viteArgs;           // node's arguments: vite.js dev --port … --strictPort
        std::optional<std::filesystem::path> chrome; // nullopt: server code only
        std::filesystem::path profileDir;            // the debug browser's own profile
        std::vector<std::pair<std::string, std::string>> env; // for Vite (e.g. where to send traces)
    };
    enum class State { Starting, Running, Stopped, Finished };

    explicit JsDebugger(Options options);
    ~JsDebugger();

    void start(const std::map<std::filesystem::path, std::set<int>>& breakpoints);
    void stop();
    State state() const { return state_; }
    bool stopped() const { return state_ == State::Stopped; }
    bool browserAttached() const { return browserStarted_; }

    void setBreakpoints(const std::filesystem::path& file, const std::set<int>& lines);
    void resume();
    void pause();
    void stepOver();
    void stepInto();
    void stepOut();

    // While paused: the stack, the scopes of a frame, a variable's children, an expression's value.
    void frames(std::function<void(std::vector<dap::Frame>)> done);
    void scopes(int frameId, std::function<void(std::vector<dap::Scope>)> done);
    void variables(int reference, std::function<void(std::vector<dap::Variable>)> done);
    void evaluate(const std::string& expression, int frameId, const std::string& context,
                  std::function<void(std::optional<dap::Variable>, std::string error)> done);

    // The top frame and js-debug's reason ("breakpoint", "step", …).
    sigc::signal<void(const dap::Frame& top, const std::string& reason)>& signalStopped() { return stopped_; }
    sigc::signal<void(State)>& signalStateChanged() { return stateChanged_; }
    // Program output (Vite's log, console.log) and the debugger's own notes.
    sigc::signal<void(const std::string& text)>& signalOutput() { return output_; }
    // Vite is serving at this URL.
    sigc::signal<void(const std::string& url)>& signalServing() { return serving_; }
    sigc::signal<void(const std::string& outcome)>& signalFinished() { return finished_; }

private:
    struct Session {
        std::unique_ptr<DapClient> client;
        std::string adapter; // pwa-node, pwa-chrome, or a child's type
        bool root = false;
        bool configured = false;
    };
    void onServerLine(const std::string& line);
    Session& openSession(const std::string& adapter, bool root, dap::json launch, const std::string& request);
    void configure(Session& session);
    void onEvent(Session& session, const std::string& event, const dap::json& body);
    void onOutputText(const std::string& text);
    void step(const char* command);
    void setState(State state);
    void finish(const std::string& outcome);
    DapClient* current(); // the session that paused

    Options options_;
    ManagedProcess server_;
    int port_ = 0;
    std::vector<std::unique_ptr<Session>> sessions_;
    std::map<std::filesystem::path, std::set<int>> breakpoints_;
    Session* stoppedSession_ = nullptr;
    int stoppedThread_ = 0;
    State state_ = State::Starting;
    bool browserStarted_ = false;
    bool done_ = false;
    std::string outputLine_; // a partial line of program output
    std::shared_ptr<JsDebugger*> self_;
    sigc::signal<void(const dap::Frame&, const std::string&)> stopped_;
    sigc::signal<void(State)> stateChanged_;
    sigc::signal<void(const std::string&)> output_;
    sigc::signal<void(const std::string&)> serving_;
    sigc::signal<void(const std::string&)> finished_;
};

} // namespace kevc
