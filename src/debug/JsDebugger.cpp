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

void JsDebugger::start(const BreakpointMap& breakpoints, const std::vector<std::string>& valueWatches) {
    for (const auto& [file, lines] : breakpoints)
        if (dap::debuggableSource(file) && !lines.empty()) breakpoints_[file] = lines;
    for (const auto& expression : valueWatches) addValueWatch(expression);
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

void JsDebugger::sendBreakpoints(Session& session, const fs::path& file) {
    if (!dap::sessionWantsBreakpoints(session.adapter, file)) return;
    const auto it = breakpoints_.find(file);
    const FileBreakpoints none;
    const FileBreakpoints& lines = it == breakpoints_.end() ? none : it->second;
    std::map<int, int> live;
    for (const auto& [line, bp] : lines)
        if (bp.kind == BreakKind::Live) {
            int& id = liveIds_[{file, line}];
            if (!id) id = nextLiveId_++;
            live[line] = id;
        }
    session.client->request("setBreakpoints", dap::setBreakpointsArguments(file, lines, live));
}

void JsDebugger::configure(Session& session) {
    for (const auto& [file, lines] : breakpoints_) sendBreakpoints(session, file);
    session.client->request("configurationDone", json::object());
    session.configured = true;
    if (state_ == State::Starting) setState(State::Running);
}

void JsDebugger::onEvent(Session& session, const std::string& event, const json& body) {
    if (event == "initialized") {
        configure(session);
    } else if (event == "output") {
        const std::string category = body.value("category", "console");
        const std::string text = body.value("output", "");
        if (auto log = parseLiveLog(text)) {
            // A live breakpoint's logpoint: shown next to its line, not in the output.
            for (const auto& [key, id] : liveIds_)
                if (id == log->id) {
                    const auto file = breakpoints_.find(key.first);
                    if (file == breakpoints_.end()) break;
                    const auto bp = file->second.find(key.second);
                    if (bp != file->second.end()) liveHit_.emit(key.first, key.second, bp->second.show, log->values);
                    break;
                }
            return;
        }
        if (category != "telemetry") onOutputText(text);
    } else if (event == "stopped") {
        stoppedSession_ = &session;
        stoppedThread_ = body.value("threadId", 0);
        onStopped(session, body.value("reason", "pause"));
    } else if (event == "continued") {
        if (&session == stoppedSession_) setState(State::Running);
    } else if (event == "terminated" || event == "exited") {
        if (session.root && session.adapter == "pwa-node") finish("Vite exited");
        else if (session.root && session.adapter == "pwa-chrome") output_.emit("Debugger: the browser was closed\n");
    }
}

void JsDebugger::onStopped(Session& session, const std::string& reason) {
    std::weak_ptr<JsDebugger*> weak = self_;
    Session* s = &session;
    const int thread = stoppedThread_;
    frames([this, weak, s, thread, reason](std::vector<dap::Frame> frames) {
        if (!weak.lock()) return;
        // The first frame in a real file: the user's code (node internals and our watch traps have none).
        dap::Frame top = frames.empty() ? dap::Frame{} : frames.front();
        for (const auto& f : frames)
            if (!f.file.empty()) {
                top = f;
                break;
            }

        // A live breakpoint that names no values: read the locals and carry on, never shown as paused.
        if (reason == "breakpoint" && !top.file.empty())
            if (auto file = breakpoints_.find(top.file); file != breakpoints_.end())
                if (auto bp = file->second.find(top.line);
                    bp != file->second.end() && bp->second.kind == BreakKind::Live && bp->second.show.empty()) {
                    const fs::path path = top.file;
                    const int line = top.line;
                    auto resume = [s, thread] { s->client->request("continue", {{"threadId", thread}}); };
                    // The innermost scope that has any: a function using its module's variables
                    // (let count = 0 at the top of the file) shows those. Never the globals.
                    scopes(top.id, [this, weak, path, line, resume](std::vector<dap::Scope> scopes) {
                        if (!weak.lock()) return;
                        std::erase_if(scopes, [](const dap::Scope& scope) { return scope.name == "Global"; });
                        auto next = std::make_shared<std::function<void(size_t)>>();
                        *next = [this, weak, path, line, resume, scopes, next](size_t index) {
                            if (index >= scopes.size()) {
                                liveHit_.emit(path, line, {}, {});
                                const auto carryOn = resume;
                                *next = nullptr; // breaks the cycle through the capture (and frees this lambda)
                                return carryOn();
                            }
                            variables(scopes[index].reference, [this, weak, path, line, resume, next, index](std::vector<dap::Variable> vars) {
                                if (!weak.lock()) return;
                                std::vector<std::string> names, values;
                                for (const auto& v : vars) {
                                    if (v.name == "this") continue;
                                    names.push_back(v.name);
                                    values.push_back(v.value);
                                }
                                if (names.empty()) return (*next)(index + 1);
                                liveHit_.emit(path, line, names, values);
                                *next = nullptr;
                                resume();
                            });
                        };
                        (*next)(0);
                    });
                    return;
                }

        auto report = [this, weak, top](const std::string& description) {
            if (!weak.lock()) return;
            setState(State::Stopped);
            armPendingWatches(top.id);
            stopped_.emit(top, description);
        };
        if (!trapsPlaced_ || frames.empty()) return report(reason);
        // Was it a watched value changing? The trap left a note.
        evaluate(jsWatchLastChange(), frames.front().id, "repl", [this, reason, report](std::optional<dap::Variable> v, std::string) {
            std::string description = reason;
            if (v)
                if (auto change = parseWatchChange(v->value)) {
                    description = "a watched value changed";
                    for (const auto& [expression, watch] : watches_)
                        if (watch.id == change->id) description = expression + " changed";
                    description += " from " + change->before + " to " + change->after;
                }
            report(description);
        });
    });
}

void JsDebugger::addValueWatch(const std::string& expression) {
    if (expression.empty() || watches_.count(expression)) return;
    watches_[expression].id = nextWatchId_++;
    if (!splitPropertyPath(expression))
        setWatchState(expression, false, "JavaScript can only watch a property, like player.score or items[0]");
    else
        setWatchState(expression, false, "placed the next time the program pauses where it can be reached");
    if (state_ != State::Stopped) return;
    // Paused now: place it in the frame the user is looking at (the first in their code).
    std::weak_ptr<JsDebugger*> weak = self_;
    frames([this, weak](std::vector<dap::Frame> frames) {
        if (!weak.lock() || state_ != State::Stopped) return;
        for (const auto& f : frames)
            if (!f.file.empty()) return armPendingWatches(f.id);
        if (!frames.empty()) armPendingWatches(frames.front().id);
    });
}

void JsDebugger::removeValueWatch(const std::string& expression) {
    auto it = watches_.find(expression);
    if (it == watches_.end()) return;
    // Put the plain property back. js-debug evaluates in the global scope while running too.
    if (Session* s = it->second.session; s && s->client->connected())
        s->client->request("evaluate", {{"expression", jsWatchRemove(it->second.id)}, {"context", "repl"}});
    watches_.erase(it);
}

void JsDebugger::setWatchState(const std::string& expression, bool armed, const std::string& note) {
    auto it = watches_.find(expression);
    if (it == watches_.end()) return;
    const auto now = std::make_pair(armed, note);
    if (it->second.reported == now) return;
    it->second.reported = now;
    watchState_.emit(expression, armed, note);
}

void JsDebugger::armPendingWatches(int frameId) {
    for (auto& [expression, watch] : watches_) {
        if (watch.session) continue;
        const auto path = splitPropertyPath(expression);
        if (!path) continue;
        std::weak_ptr<JsDebugger*> weak = self_;
        Session* s = stoppedSession_;
        evaluate(jsWatchInstall(watch.id, path->first, path->second), frameId, "repl",
                 [this, weak, s, expression, id = watch.id](std::optional<dap::Variable> v, std::string error) {
                     if (!weak.lock()) return;
                     auto it = watches_.find(expression);
                     if (it == watches_.end() || it->second.id != id || it->second.session) return;
                     std::string result = v ? v->value : error;
                     if (result.size() >= 2 && (result.front() == '\'' || result.front() == '"') && result.back() == result.front())
                         result = result.substr(1, result.size() - 2);
                     if (result == "ok") {
                         it->second.session = s;
                         trapsPlaced_ = true;
                         setWatchState(expression, true, {});
                     } else {
                         setWatchState(expression, false, result + "; tried again whenever the program pauses");
                     }
                 });
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

void JsDebugger::setBreakpoints(const fs::path& file, const FileBreakpoints& lines) {
    if (!dap::debuggableSource(file)) return;
    breakpoints_[file] = lines;
    std::erase_if(liveIds_, [&](const auto& entry) {
        if (entry.first.first != file) return false;
        auto bp = lines.find(entry.first.second);
        return bp == lines.end() || bp->second.kind != BreakKind::Live;
    });
    for (auto& s : sessions_)
        if (s->configured && s->client->connected()) sendBreakpoints(*s, file);
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
