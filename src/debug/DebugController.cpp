#include "debug/DebugController.h"

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
    gdb_.signalConsole().connect([this](const std::string& text) { console_.emit(text); });
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

void DebugController::start(const std::vector<std::pair<fs::path, int>>& breakpoints) {
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
    for (const auto& [file, line] : breakpoints) setBreakpoint(file, line, true);
    gdb_.send("-exec-run", [this](const mi::Record& r) {
        if (!ok(r)) finish("Could not start the program: " + errorOf(r));
    });
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

void DebugController::setBreakpoint(const fs::path& file, int line, bool enabled) {
    const auto key = std::make_pair(file.string(), line);
    if (!enabled) {
        auto it = breakpointIds_.find(key);
        if (it == breakpointIds_.end()) return;
        gdb_.send("-break-delete " + it->second);
        breakpointIds_.erase(it);
        return;
    }
    if (breakpointIds_.count(key)) return;
    breakpointIds_[key] = "";
    // -f creates a pending breakpoint if the location isn't loaded yet (e.g. in a shared library).
    gdb_.send("-break-insert -f " + mi::quote(file.string() + ":" + std::to_string(line)),
              [this, key](const mi::Record& r) {
                  if (!ok(r)) {
                      breakpointIds_.erase(key);
                      console_.emit("Breakpoint at " + key.first + ":" + std::to_string(key.second) +
                                    " failed: " + errorOf(r) + "\n");
                      return;
                  }
                  auto it = breakpointIds_.find(key);
                  const std::string number = r.results.value("bkpt", json::object()).value("number", "");
                  if (it != breakpointIds_.end()) it->second = number;
                  else if (!number.empty()) gdb_.send("-break-delete " + number); // removed while inserting
              });
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

    StopInfo info;
    info.reason = reason;
    info.frame = frameFrom(record.results.value("frame", json::object()));
    const std::string where = info.frame.file.empty()
                                  ? info.frame.function + " (no source)"
                                  : info.frame.file.filename().string() + ":" + std::to_string(info.frame.line);
    if (reason == "breakpoint-hit") info.description = "Breakpoint " + record.results.value("bkptno", "") + " at " + where;
    else if (reason == "signal-received")
        info.description = record.results.value("signal-name", std::string("Signal")) + " (" +
                           record.results.value("signal-meaning", std::string("")) + ") at " + where;
    else if (reason == "function-finished") info.description = "Returned to " + where;
    else info.description = "Paused at " + where;
    selectedFrame_ = 0;
    setState(State::Stopped);
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
