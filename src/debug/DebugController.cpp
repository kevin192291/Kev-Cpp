#include "debug/DebugController.h"

#include <cstdlib>

namespace fs = std::filesystem;

namespace kevc {

namespace {

using json = mi::json;

std::string errorOf(const mi::Record& r) {
    return r.results.value("msg", std::string("unknown error"));
}

bool ok(const mi::Record& r) {
    return r.resultClass == "done" || r.resultClass == "running";
}

int toInt(const json& v, int fallback = 0) {
    try {
        return v.is_string() ? std::stoi(v.get<std::string>()) : fallback;
    } catch (const std::exception&) {
        return fallback;
    }
}

StackFrame frameFrom(const json& f) {
    StackFrame frame;
    frame.level = toInt(f.value("level", json("0")));
    frame.function = f.value("func", "??");
    frame.line = toInt(f.value("line", json("0")));
    const std::string full = f.value("fullname", "");
    std::error_code ec;
    if (!full.empty() && fs::exists(full, ec)) frame.file = full;
    return frame;
}

} // namespace

DebugController::DebugController(std::string gdbPath, fs::path executable, fs::path cwd, std::string inferiorTty)
    : executable_(std::move(executable)), cwd_(std::move(cwd)), tty_(std::move(inferiorTty)), gdb_(std::move(gdbPath)),
      self_(std::make_shared<DebugController*>(this)) {
    gdb_.signalExec().connect(sigc::mem_fun(*this, &DebugController::onExec));
    gdb_.signalConsole().connect(sigc::mem_fun(*this, &DebugController::onConsole));
    gdb_.signalExited().connect([this] {
        if (state_ != State::Finished) finish("The debugger stopped unexpectedly");
    });
}

DebugController::~DebugController() {
    self_.reset();
}

void DebugController::setState(State s) {
    if (state_ == s) return;
    state_ = s;
    stateChanged_.emit(s);
}

void DebugController::finish(const std::string& outcome) {
    if (state_ == State::Finished) return;
    setState(State::Finished);
    finished_.emit(outcome);
}

void DebugController::start(const BreakpointMap& breakpoints, const std::vector<std::string>& valueWatches) {
    try {
        gdb_.start(cwd_);
    } catch (const Glib::Error& e) {
        finish(std::string("Could not start gdb: ") + e.what());
        return;
    }
    // Asynchronous mode lets us interrupt a running program with -exec-interrupt.
    gdb_.send("-gdb-set mi-async on");
    gdb_.send("-enable-pretty-printing");
    gdb_.send("-file-exec-and-symbols " + mi::quote(executable_.string()), [this](const mi::Record& r) {
        if (!ok(r)) finish("gdb could not load " + executable_.filename().string() + ": " + errorOf(r));
    });
    gdb_.send("-environment-cd " + mi::quote(cwd_.string()));
    if (!tty_.empty()) gdb_.send("-inferior-tty-set " + tty_);
    for (const auto& [file, lines] : breakpoints)
        for (const auto& [line, bp] : lines) setBreakpoint(file, line, bp);
    for (const auto& expression : valueWatches) addValueWatch(expression);
    // Live breakpoints need to know whether this gdb runs Python; the rest waits for its answer.
    std::weak_ptr<DebugController*> weak = self_;
    gdb_.send("-interpreter-exec console " + mi::quote(gdbLivePython()), [weak, this](const mi::Record& r) {
        if (!weak.lock() || state_ == State::Finished) return;
        // KEVC_GDB_NO_PYTHON=1 tries the fallback a gdb without Python gets (live breakpoints that
        // stop for a moment), for testing.
        const char* noPython = std::getenv("KEVC_GDB_NO_PYTHON");
        python_ = ok(r) && !(noPython && *noPython == '1');
        ready_ = true;
        for (const auto& [key, placed] : breakpointIds_) place(key);
        armPendingWatches();
        gdb_.send("-exec-run", [this](const mi::Record& r) {
            if (!ok(r)) finish("Could not start the program: " + errorOf(r));
        });
    });
}

void DebugController::onConsole(const std::string& text) {
    if (auto log = parseLiveLog(text)) {
        if (auto it = liveIds_.find(log->id); it != liveIds_.end()) emitLive(it->second, log->values);
        return; // not the user's business, even when stale
    }
    console_.emit(text);
}

void DebugController::emitLive(const Key& key, const std::vector<std::string>& values) {
    auto it = breakpointIds_.find(key);
    if (it == breakpointIds_.end()) return;
    if (it->second.breakpoint.show.empty()) {
        auto [names, vals] = splitNamedValues(values);
        liveHit_.emit(key.first, key.second, names, vals);
    } else {
        liveHit_.emit(key.first, key.second, it->second.breakpoint.show, values);
    }
}

void DebugController::execCommand(const std::string& command) {
    if (state_ != State::Stopped) return;
    gdb_.send(command, [this](const mi::Record& r) {
        if (!ok(r)) console_.emit("error: " + errorOf(r) + "\n");
    });
}

void DebugController::resume() { execCommand("-exec-continue"); }
void DebugController::stepOver() { execCommand("-exec-next"); }
void DebugController::stepInto() { execCommand("-exec-step"); }
void DebugController::stepOut() { execCommand("-exec-finish"); }

void DebugController::pause() {
    if (state_ == State::Running) gdb_.send("-exec-interrupt");
}

void DebugController::terminate() {
    if (state_ == State::Finished) return;
    // Killing gdb kills the program too; the destructor does that, the owner drops us on finished.
    finish("Debugging stopped");
}

void DebugController::setBreakpoint(const fs::path& file, int line, const std::optional<Breakpoint>& breakpoint) {
    const Key key{file.string(), line};
    if (auto it = breakpointIds_.find(key); it != breakpointIds_.end()) {
        if (breakpoint && it->second.breakpoint == *breakpoint) return; // already placed like that
        if (!it->second.number.empty()) gdb_.send("-break-delete " + it->second.number);
        if (it->second.liveId) liveIds_.erase(it->second.liveId);
        breakpointIds_.erase(it);
    }
    if (!breakpoint) return;
    breakpointIds_[key] = {"", *breakpoint, 0};
    if (ready_) place(key);
}

void DebugController::place(const Key& key) {
    auto& placed = breakpointIds_.at(key);
    const Breakpoint& bp = placed.breakpoint;
    const std::string location = mi::quote(key.first + ":" + std::to_string(key.second));
    // -f creates a pending breakpoint if the location isn't loaded yet (e.g. in a shared library).
    std::string command;
    if (bp.kind == BreakKind::Live && python_) {
        placed.liveId = nextLiveId_++;
        liveIds_[placed.liveId] = key;
        command = "-dprintf-insert -f " + location;
        for (const auto& arg : gdbLiveArguments(placed.liveId, bp.show)) command += " " + mi::quote(arg);
    } else {
        command = "-break-insert -f ";
        if (bp.kind == BreakKind::Conditional && !bp.condition.empty()) command += "-c " + mi::quote(bp.condition) + " ";
        command += location;
    }
    const Breakpoint wanted = bp;
    std::weak_ptr<DebugController*> weak = self_;
    gdb_.send(command, [weak, this, key, wanted](const mi::Record& r) {
        if (!weak.lock()) return;
        auto it = breakpointIds_.find(key);
        const bool current = it != breakpointIds_.end() && it->second.breakpoint == wanted && it->second.number.empty();
        if (!ok(r)) {
            console_.emit("Breakpoint at " + key.first + ":" + std::to_string(key.second) + " failed: " + errorOf(r) + "\n");
            return;
        }
        const std::string number = r.results.value("bkpt", json::object()).value("number", "");
        if (current) it->second.number = number;
        else if (!number.empty()) gdb_.send("-break-delete " + number); // changed or removed while inserting
    });
}

void DebugController::onLiveHit(const fs::path& file, int line, const Breakpoint& breakpoint) {
    // Read what it shows, then carry on: the program is never reported as paused.
    std::weak_ptr<DebugController*> weak = self_;
    if (breakpoint.show.empty()) {
        gdb_.send("-stack-list-locals --simple-values", [weak, this, file, line](const mi::Record& r) {
            if (!weak.lock()) return;
            std::vector<std::string> names, values;
            for (const auto& local : r.results.value("locals", json::array())) {
                names.push_back(local.value("name", ""));
                values.push_back(local.value("value", std::string("{\u2026}")));
            }
            liveHit_.emit(file, line, names, values);
        });
    } else {
        auto values = std::make_shared<std::vector<std::string>>();
        for (size_t i = 0; i < breakpoint.show.size(); ++i) {
            const bool last = i + 1 == breakpoint.show.size();
            gdb_.send("-data-evaluate-expression " + mi::quote(breakpoint.show[i]),
                      [weak, this, values, file, line, names = breakpoint.show, last](const mi::Record& r) {
                          if (!weak.lock()) return;
                          values->push_back(ok(r) ? r.results.value("value", "") : "<" + errorOf(r) + ">");
                          if (last) liveHit_.emit(file, line, names, *values);
                      });
        }
    }
    gdb_.send("-exec-continue");
}

void DebugController::addValueWatch(const std::string& expression) {
    if (expression.empty() || watches_.count(expression)) return;
    watches_[expression] = {};
    if (ready_) armWatch(expression);
}

void DebugController::removeValueWatch(const std::string& expression) {
    auto it = watches_.find(expression);
    if (it == watches_.end()) return;
    if (!it->second.number.empty()) gdb_.send("-break-delete " + it->second.number);
    watches_.erase(it);
}

void DebugController::setWatchState(const std::string& expression, bool armed, const std::string& note) {
    auto it = watches_.find(expression);
    if (it == watches_.end()) return;
    // Pending watches are retried at every pause: only say so when something changed.
    const auto now = std::make_pair(armed, note);
    if (it->second.reported == now) return;
    it->second.reported = now;
    watchState_.emit(expression, armed, note);
}

void DebugController::armWatch(const std::string& expression) {
    std::weak_ptr<DebugController*> weak = self_;
    gdb_.send("-break-watch " + mi::quote(expression), [weak, this, expression](const mi::Record& r) {
        if (!weak.lock()) return;
        std::string number;
        for (const char* key : {"wpt", "hw-wpt", "hw-awpt", "hw-rwpt"})
            if (r.results.contains(key)) number = r.results[key].value("number", "");
        auto it = watches_.find(expression);
        if (it == watches_.end() || !it->second.number.empty()) { // removed, or placed twice
            if (!number.empty()) gdb_.send("-break-delete " + number);
            return;
        }
        if (!ok(r) || number.empty()) {
            const std::string why = ok(r) ? std::string("gdb didn't place it") : errorOf(r);
            setWatchState(expression, false, why + "; tried again whenever the program pauses");
            return;
        }
        it->second.number = number;
        setWatchState(expression, true, {});
    });
}

void DebugController::armPendingWatches() {
    for (const auto& [expression, watch] : watches_)
        if (watch.number.empty()) armWatch(expression);
}

void DebugController::onExec(const mi::Record& record) {
    if (record.resultClass == "running") {
        setState(State::Running);
        return;
    }
    if (record.resultClass != "stopped") return;

    const std::string reason = record.results.value("reason", "");
    if (reason == "exited-normally") {
        finish("Program exited with code 0");
        return;
    }
    if (reason == "exited") {
        finish("Program exited with code " + std::to_string(std::stoi(record.results.value("exit-code", "0"), nullptr, 8)));
        return;
    }
    if (reason == "exited-signalled") {
        finish("Program terminated by " + record.results.value("signal-name", std::string("a signal")));
        return;
    }

    // A live breakpoint: read its values and carry on (see onLiveHit).
    if (reason == "breakpoint-hit") {
        const std::string number = record.results.value("bkptno", "");
        for (const auto& [key, placed] : breakpointIds_)
            if (placed.number == number && placed.breakpoint.kind == BreakKind::Live && !placed.liveId) {
                onLiveHit(key.first, key.second, placed.breakpoint);
                return;
            }
    }
    // A watched local's function returned: gdb dropped the watchpoint. Wait to place it again.
    if (reason == "watchpoint-scope") {
        const std::string number = record.results.value("wpnum", "");
        for (auto& [expression, watch] : watches_)
            if (watch.number == number) {
                watch.number.clear();
                setWatchState(expression, false, "its function returned; placed again when the program pauses where it exists");
            }
        gdb_.send("-exec-continue");
        return;
    }

    StopInfo info;
    info.reason = reason;
    info.frame = frameFrom(record.results.value("frame", json::object()));
    const std::string where = info.frame.file.empty()
                                  ? info.frame.function + " (no source)"
                                  : info.frame.file.filename().string() + ":" + std::to_string(info.frame.line);
    if (reason == "breakpoint-hit") {
        // gdb's numbers mean nothing to the user (live breakpoints take some): say which kind.
        info.description = "Stopped at the breakpoint at " + where;
        const std::string number = record.results.value("bkptno", "");
        for (const auto& [key, placed] : breakpointIds_)
            if (placed.number == number && placed.breakpoint.kind == BreakKind::Conditional)
                info.description = "Stopped at " + where + ": " + placed.breakpoint.condition;
    }
    else if (reason == "signal-received")
        info.description = record.results.value("signal-name", std::string("Signal")) + " (" +
                           record.results.value("signal-meaning", std::string("")) + ") at " + where;
    else if (reason == "function-finished") info.description = "Returned to " + where;
    else if (reason.find("watchpoint-trigger") != std::string::npos) {
        json wpt = json::object();
        for (const char* key : {"wpt", "hw-wpt", "hw-awpt", "hw-rwpt"})
            if (record.results.contains(key)) wpt = record.results[key];
        const json value = record.results.value("value", json::object());
        info.description = wpt.value("exp", std::string("A watched value")) + " changed from " +
                           value.value("old", std::string("?")) + " to " + value.value("new", std::string("?")) +
                           " at " + where;
    } else info.description = "Paused at " + where;
    selectedFrame_ = 0;
    setState(State::Stopped);
    armPendingWatches(); // a local that wasn't in reach may be now
    stopped_.emit(info);
}

void DebugController::selectFrame(int level) {
    if (state_ != State::Stopped) return;
    gdb_.send("-stack-select-frame " + std::to_string(level));
    selectedFrame_ = level;
}

VarInfo DebugController::varInfoFrom(const json& r, const std::string& fallbackName) {
    VarInfo v;
    v.varobj = r.value("name", "");
    v.name = r.value("exp", fallbackName);
    v.value = r.value("value", "");
    v.type = r.value("type", "");
    v.children = toInt(r.value("numchild", json("0")));
    if (v.children == 0 && r.value("has_more", "0") == "1") v.children = 1;
    if (v.children == 0 && r.value("dynamic", "0") == "1" && r.value("displayhint", "") != "string") v.children = 1;
    v.isPointer = mi::isDataPointer(v.type);
    return v;
}

void DebugController::createVar(const std::string& expression,
                                std::function<void(std::optional<VarInfo>, std::string)> done) {
    std::weak_ptr<DebugController*> weak = self_;
    gdb_.send("-var-create - * " + mi::quote(expression), [weak, expression, done](const mi::Record& r) {
        if (!weak.lock()) return;
        if (!ok(r)) {
            std::string error = errorOf(r);
            // gdb's generic failure for names that don't exist in the current frame.
            if (error.find("unable to create variable object") != std::string::npos) error = "not available in this frame";
            done(std::nullopt, error);
            return;
        }
        VarInfo v = varInfoFrom(r.results, expression);
        v.name = expression;
        done(v, {});
    });
}

void DebugController::deleteVar(const std::string& varobj) {
    if (!varobj.empty() && state_ != State::Finished) gdb_.send("-var-delete " + varobj);
}

void DebugController::collectChildren(const std::string& varobj, std::vector<VarInfo>* out, std::function<void()> done) {
    std::weak_ptr<DebugController*> weak = self_;
    gdb_.send("-var-list-children --all-values " + varobj, [this, weak, out, done](const mi::Record& r) {
        if (!weak.lock()) return;
        if (!ok(r)) {
            done();
            return;
        }
        // C++ structs list their fields under "public"/"private"/"protected" pseudo-children;
        // splice those in so the tree shows fields directly. Handled one after another to keep order.
        auto children = std::make_shared<json>(r.results.value("children", json::array()));
        auto step = std::make_shared<std::function<void(size_t)>>();
        *step = [this, weak, out, done, children, step](size_t i) {
            if (!weak.lock()) return;
            for (; i < children->size(); ++i) {
                const json& c = (*children)[i];
                const std::string exp = c.value("exp", "");
                if ((exp == "public" || exp == "private" || exp == "protected") && c.value("value", "").empty()) {
                    collectChildren(c.value("name", ""), out, [step, i] { (*step)(i + 1); });
                    return;
                }
                out->push_back(varInfoFrom(c, exp));
            }
            done();
            *step = nullptr; // break the self-reference
        };
        (*step)(0);
    });
}

void DebugController::listChildren(const std::string& varobj, std::function<void(std::vector<VarInfo>)> done) {
    auto out = std::make_shared<std::vector<VarInfo>>();
    collectChildren(varobj, out.get(), [out, done] { done(std::move(*out)); });
}

void DebugController::pointee(const std::string& varobj, std::function<void(std::string, bool)> done) {
    std::weak_ptr<DebugController*> weak = self_;
    gdb_.send("-var-info-path-expression " + varobj, [this, weak, done](const mi::Record& r) {
        if (!weak.lock()) return;
        if (!ok(r)) {
            done(errorOf(r), false);
            return;
        }
        const std::string path = r.results.value("path_expr", "");
        gdb_.send("-data-evaluate-expression " + mi::quote("*(" + path + ")"), [weak, done](const mi::Record& e) {
            if (!weak.lock()) return;
            if (ok(e)) done(e.results.value("value", ""), true);
            else done(errorOf(e), false);
        });
    });
}

void DebugController::frames(std::function<void(std::vector<StackFrame>)> done) {
    std::weak_ptr<DebugController*> weak = self_;
    gdb_.send("-stack-list-frames 0 63", [weak, done](const mi::Record& r) {
        if (!weak.lock()) return;
        std::vector<StackFrame> out;
        for (const auto& f : r.results.value("stack", json::array())) out.push_back(frameFrom(f));
        done(std::move(out));
    });
}

void DebugController::locals(std::function<void(std::vector<std::string>)> done) {
    std::weak_ptr<DebugController*> weak = self_;
    gdb_.send("-stack-list-variables --no-values", [weak, done](const mi::Record& r) {
        if (!weak.lock()) return;
        std::vector<std::string> names;
        for (const auto& v : r.results.value("variables", json::array())) names.push_back(v.value("name", ""));
        done(std::move(names));
    });
}

void DebugController::consoleCommand(const std::string& command) {
    gdb_.send("-interpreter-exec console " + mi::quote(command), [this](const mi::Record& r) {
        if (!ok(r)) console_.emit("error: " + errorOf(r) + "\n");
    });
}

} // namespace kevc
