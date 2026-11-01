#pragma once

#include "core/Breakpoints.h"
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

    void start(const BreakpointMap& breakpoints, const std::vector<std::string>& valueWatches);
    void stop();
    State state() const { return state_; }
    bool stopped() const { return state_ == State::Stopped; }
    bool browserAttached() const { return browserStarted_; }

    // A file's breakpoints. IF breakpoints are js-debug conditions; live ones naming values are
    // logpoints (their output is caught, not shown); live ones naming nothing stop, have their locals
    // read, and continue.
    void setBreakpoints(const std::filesystem::path& file, const FileBreakpoints& breakpoints);
    // Break when a property's value changes (see jsWatchInstall). Placed the next time the program
    // pauses, since that needs the object; a plain variable can't be watched.
    void addValueWatch(const std::string& expression);
    void removeValueWatch(const std::string& expression);
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

    // The top frame and js-debug's reason ("breakpoint", "step", …), or what changed for a value watch.
    sigc::signal<void(const dap::Frame& top, const std::string& reason)>& signalStopped() { return stopped_; }
    sigc::signal<void(State)>& signalStateChanged() { return stateChanged_; }
    // Program output (Vite's log, console.log) and the debugger's own notes.
    sigc::signal<void(const std::string& text)>& signalOutput() { return output_; }
    // Vite is serving at this URL.
    sigc::signal<void(const std::string& url)>& signalServing() { return serving_; }
    // As DebugController's: a live breakpoint ran; a value watch is placed or isn't, and why.
    sigc::signal<void(const std::filesystem::path& file, int line, const std::vector<std::string>& names,
                      const std::vector<std::string>& values)>&
    signalLiveHit() {
        return liveHit_;
    }
    sigc::signal<void(const std::string& expression, bool armed, const std::string& note)>& signalWatchState() {
        return watchState_;
    }
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
    void onStopped(Session& session, const std::string& reason);
    void sendBreakpoints(Session& session, const std::filesystem::path& file);
    void armPendingWatches(int frameId);
    void setWatchState(const std::string& expression, bool armed, const std::string& note);
    void step(const char* command);
    void setState(State state);
    void finish(const std::string& outcome);
    DapClient* current(); // the session that paused

    Options options_;
    ManagedProcess server_;
    int port_ = 0;
    std::vector<std::unique_ptr<Session>> sessions_;
    BreakpointMap breakpoints_;
    std::map<std::pair<std::filesystem::path, int>, int> liveIds_; // logpoints' markers
    int nextLiveId_ = 1;
    struct Watch {
        int id = 0;
        Session* session = nullptr; // where its trap is; nullptr while not placed
        std::optional<std::pair<bool, std::string>> reported;
    };
    std::map<std::string, Watch> watches_;
    int nextWatchId_ = 1;
    bool trapsPlaced_ = false; // a trap was ever placed: look for its change after each stop
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
    sigc::signal<void(const std::filesystem::path&, int, const std::vector<std::string>&, const std::vector<std::string>&)> liveHit_;
    sigc::signal<void(const std::string&, bool, const std::string&)> watchState_;
};

} // namespace kevc
