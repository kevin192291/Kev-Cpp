#include "debug/JsDebugger.h"

#include "core/WebProject.h"

namespace fs = std::filesystem;

namespace kevc {

using json = dap::json;

JsDebugger::JsDebugger(Options options) : options_(std::move(options)), self_(std::make_shared<JsDebugger*>(this)) {
    server_.signalLine().connect([this](const std::string& line) { onServerLine(line); });
    server_.signalExited().connect([this](int) { finish("The debugger stopped"); });
}

JsDebugger::~JsDebugger() {
    self_.reset();
    sessions_.clear(); // closes the connections; server_ then stops js-debug, Vite and the browser
}

void JsDebugger::start(const std::map<fs::path, std::set<int>>& breakpoints) {
    for (const auto& [file, lines] : breakpoints)
        if (dap::debuggableSource(file) && !lines.empty()) breakpoints_[file] = lines;
    port_ = findFreePort(8123);
    if (port_ == 0) {
        finish("No free port for the debugger");
        return;
    }
    try {
        server_.start({{options_.node, options_.server.string(), std::to_string(port_), "127.0.0.1"},
                       options_.root, {}, fs::path(options_.node).parent_path().string()});
    } catch (const Glib::Error& e) {
        finish(std::string("Could not start js-debug: ") + e.what());
    }
}

void JsDebugger::onServerLine(const std::string& line) {
    if (sessions_.empty() && dap::serverPort(line) == port_) {
        openSession("pwa-node", true, dap::nodeLaunch(options_.root, options_.node, options_.viteArgs, options_.env), "launch");
        return;
    }
    if (!line.empty()) output_.emit("[js-debug] " + line + "\n");
}

JsDebugger::Session& JsDebugger::openSession(const std::string& adapter, bool root, json launch, const std::string& request) {
    auto session = std::make_unique<Session>();
    session->client = std::make_unique<DapClient>();
    session->adapter = adapter;
    session->root = root;
    Session* s = session.get();
    DapClient* client = s->client.get();
    std::weak_ptr<JsDebugger*> weak = self_;

    client->signalConnected().connect([this, s, client, adapter, launch, request, root, weak] {
        client->request("initialize", dap::initializeArguments(adapter),
                        [this, client, launch, request, root, weak](bool ok, const json&, const std::string& message) {
                            if (!weak.lock()) return;
                            if (!ok) {
                                output_.emit("Debugger: initialize failed: " + message + "\n");
                                if (root) finish("The debugger couldn't start: " + message);
                                return;
                            }
                            client->request(request, launch, [this, root, weak](bool ok, const json&, const std::string& message) {
                                if (!weak.lock() || ok) return;
                                output_.emit("Debugger: " + message + "\n");
                                if (root) finish("Couldn't start under the debugger: " + message);
                            });
                        });
    });
    client->signalEvent().connect([this, s](const std::string& event, const json& body) { onEvent(*s, event, body); });
    client->signalReverseRequest().connect([this, client](int seq, const std::string& command, const json& args) {
        if (command != "startDebugging") {
            client->respond(seq, command, false);
            return;
        }
        // js-debug attached to a process or page: it wants its own connection for it.
        client->respond(seq, command, true);
        const json configuration = args.value("configuration", json::object());
        openSession(configuration.value("type", "pwa-node"), false, configuration, args.value("request", "attach"));
    });
    client->signalClosed().connect([this, s](const std::string& reason) {
        if (s->root && s->adapter == "pwa-node") finish("Vite stopped (" + reason + ")");
    });
    client->connect(port_);
    sessions_.push_back(std::move(session));
    return *s;
}

void JsDebugger::configure(Session& session) {
    for (const auto& [file, lines] : breakpoints_)
        if (dap::sessionWantsBreakpoints(session.adapter, file))
            session.client->request("setBreakpoints", dap::setBreakpointsArguments(file, lines));
    session.client->request("configurationDone", json::object());
    session.configured = true;
    if (state_ == State::Starting) setState(State::Running);
}

void JsDebugger::onEvent(Session& session, const std::string& event, const json& body) {
    if (event == "initialized") {
        configure(session);
    } else if (event == "output") {
        const std::string category = body.value("category", "console");
        if (category != "telemetry") onOutputText(body.value("output", ""));
    } else if (event == "stopped") {
        stoppedSession_ = &session;
        stoppedThread_ = body.value("threadId", 0);
        const std::string reason = body.value("reason", "pause");
        setState(State::Stopped);
        std::weak_ptr<JsDebugger*> weak = self_;
        frames([this, weak, reason](std::vector<dap::Frame> frames) {
            if (!weak.lock()) return;
            // The first frame in a real file: the user's code (node internals have none).
            dap::Frame top = frames.empty() ? dap::Frame{} : frames.front();
            for (const auto& f : frames)
                if (!f.file.empty()) {
                    top = f;
                    break;
                }
            stopped_.emit(top, reason);
        });
    } else if (event == "continued") {
        if (&session == stoppedSession_) setState(State::Running);
    } else if (event == "terminated" || event == "exited") {
        if (session.root && session.adapter == "pwa-node") finish("Vite exited");
        else if (session.root && session.adapter == "pwa-chrome") output_.emit("Debugger: the browser was closed\n");
    }
}

void JsDebugger::onOutputText(const std::string& text) {
    output_.emit(text);
    // Watch the program's output for Vite's "Local: http://…" line.
    outputLine_ += text;
    size_t newline;
    while ((newline = outputLine_.find('\n')) != std::string::npos) {
        const std::string line = outputLine_.substr(0, newline);
        outputLine_.erase(0, newline + 1);
        const std::string url = viteReadyUrl(line);
        if (url.empty() || browserStarted_) continue;
        browserStarted_ = true;
        serving_.emit(url);
        if (options_.chrome) openSession("pwa-chrome", true, dap::chromeLaunch(url, options_.root, *options_.chrome, options_.profileDir), "launch");
    }
}

DapClient* JsDebugger::current() {
    return stoppedSession_ ? stoppedSession_->client.get() : nullptr;
}

void JsDebugger::step(const char* command) {
    DapClient* client = current();
    if (!client || state_ != State::Stopped) return;
    client->request(command, {{"threadId", stoppedThread_}});
    setState(State::Running);
}

void JsDebugger::resume() {
    step("continue");
}
void JsDebugger::stepOver() {
    step("next");
}
void JsDebugger::stepInto() {
    step("stepIn");
}
void JsDebugger::stepOut() {
    step("stepOut");
}

void JsDebugger::pause() {
    if (state_ != State::Running) return;
    // Every attached process and page: whichever has a thread pauses.
    for (auto& s : sessions_) {
        if (!s->configured || !s->client->connected()) continue;
        DapClient* client = s->client.get();
        client->request("threads", json::object(), [client](bool ok, const json& body, const std::string&) {
            if (!ok) return;
            for (const auto& t : body.value("threads", json::array())) client->request("pause", {{"threadId", t.value("id", 0)}});
        });
    }
}

void JsDebugger::setBreakpoints(const fs::path& file, const std::set<int>& lines) {
    if (!dap::debuggableSource(file)) return;
    breakpoints_[file] = lines;
    for (auto& s : sessions_)
        if (s->configured && s->client->connected() && dap::sessionWantsBreakpoints(s->adapter, file))
            s->client->request("setBreakpoints", dap::setBreakpointsArguments(file, lines));
}

void JsDebugger::frames(std::function<void(std::vector<dap::Frame>)> done) {
    DapClient* client = current();
    if (!client) return done({});
    client->request("stackTrace", {{"threadId", stoppedThread_}, {"startFrame", 0}, {"levels", 50}},
                    [done](bool ok, const json& body, const std::string&) { done(ok ? dap::framesOf(body) : std::vector<dap::Frame>{}); });
}

void JsDebugger::scopes(int frameId, std::function<void(std::vector<dap::Scope>)> done) {
    DapClient* client = current();
    if (!client) return done({});
    client->request("scopes", {{"frameId", frameId}},
                    [done](bool ok, const json& body, const std::string&) { done(ok ? dap::scopesOf(body) : std::vector<dap::Scope>{}); });
}

void JsDebugger::variables(int reference, std::function<void(std::vector<dap::Variable>)> done) {
    DapClient* client = current();
    if (!client) return done({});
    client->request("variables", {{"variablesReference", reference}},
                    [done](bool ok, const json& body, const std::string&) {
                        done(ok ? dap::variablesOf(body) : std::vector<dap::Variable>{});
                    });
}

void JsDebugger::evaluate(const std::string& expression, int frameId, const std::string& context,
                          std::function<void(std::optional<dap::Variable>, std::string)> done) {
    DapClient* client = current();
    if (!client) return done(std::nullopt, "not paused");
    json args = {{"expression", expression}, {"context", context}};
    if (frameId) args["frameId"] = frameId;
    client->request("evaluate", args, [expression, done](bool ok, const json& body, const std::string& message) {
        if (!ok) return done(std::nullopt, message);
        done(dap::Variable{expression, body.value("result", ""), body.value("type", ""), body.value("variablesReference", 0)}, {});
    });
}

void JsDebugger::stop() {
    if (done_) return;
    // Ask js-debug to end Vite and close the browser it opened, then stop the server itself.
    for (auto& s : sessions_)
        if (s->root && s->client->connected()) s->client->request("disconnect", {{"terminateDebuggee", true}});
    std::weak_ptr<JsDebugger*> weak = self_;
    Glib::signal_timeout().connect_once(
        [weak] {
            if (auto self = weak.lock()) (*self)->finish("Debugging stopped");
        },
        500);
}

void JsDebugger::setState(State state) {
    if (state_ == state || state_ == State::Finished) return;
    state_ = state;
    if (state != State::Stopped) stoppedSession_ = nullptr;
    stateChanged_.emit(state);
}

void JsDebugger::finish(const std::string& outcome) {
    if (done_) return;
    done_ = true;
    stoppedSession_ = nullptr;
    state_ = State::Finished;
    server_.stop();
    stateChanged_.emit(State::Finished);
    finished_.emit(outcome);
}

} // namespace kevc
