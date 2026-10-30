#pragma once

#include "debug/GdbSession.h"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace kevc {

// A gdb variable object: one value in the Locals / Watch / hover trees.
struct VarInfo {
    std::string varobj;     // gdb's handle, e.g. "var3.public.x"
    std::string name;       // what to show: "x", "[0]", "p"
    std::string value;      // gdb's text, e.g. "0x7ffe..." or "{...}"
    std::string type;
    int children = 0;       // numchild, or 1 if a pretty-printer says it has more
    bool isPointer = false; // a data pointer worth following
};

struct StackFrame {
    int level = 0;
    std::string function;
    std::filesystem::path file; // empty when gdb has no source for the frame
    int line = 0;
};

struct StopInfo {
    std::string reason; // gdb's reason, e.g. "breakpoint-hit", "end-stepping-range"
    StackFrame frame;
    std::string description; // for the status bar
};

// One debugging session of one program under gdb.
class DebugController {
public:
    enum class State { Starting, Running, Stopped, Finished };

    DebugController(std::string gdbPath, std::filesystem::path executable, std::filesystem::path cwd,
                    std::string inferiorTty);
    ~DebugController();

    // Starts gdb, sets `breakpoints` (file, 1-based line) and runs the program.
    void start(const std::vector<std::pair<std::filesystem::path, int>>& breakpoints);
    State state() const { return state_; }
    bool stopped() const { return state_ == State::Stopped; }

    void resume();   // continue
    void stepOver(); // next
    void stepInto(); // step
    void stepOut();  // finish
    void pause();    // interrupt a running program
    void terminate();

    void setBreakpoint(const std::filesystem::path& file, int line, bool enabled);
    void selectFrame(int level);
    int selectedFrame() const { return selectedFrame_; }

    // Values. Callbacks run only while this controller is alive.
    void createVar(const std::string& expression, std::function<void(std::optional<VarInfo>, std::string error)> done);
    void deleteVar(const std::string& varobj);
    void listChildren(const std::string& varobj, std::function<void(std::vector<VarInfo>)> done);
    // What a pointer variable points at, e.g. "{x = 3, y = 4}" or an error such as "Cannot access memory at 0x10".
    void pointee(const std::string& varobj, std::function<void(std::string text, bool ok)> done);
    void frames(std::function<void(std::vector<StackFrame>)> done);
    void locals(std::function<void(std::vector<std::string> names)> done);
    void consoleCommand(const std::string& command);

    sigc::signal<void(State)>& signalStateChanged() { return stateChanged_; }
    sigc::signal<void(const StopInfo&)>& signalStopped() { return stopped_; }
    sigc::signal<void(const std::string& outcome)>& signalFinished() { return finished_; }
    sigc::signal<void(const std::string&)>& signalConsole() { return console_; }

private:
    void setState(State s);
    void onExec(const mi::Record& record);
    void execCommand(const std::string& command);
    void finish(const std::string& outcome);
    void collectChildren(const std::string& varobj, std::vector<VarInfo>* out, std::function<void()> done);
    static VarInfo varInfoFrom(const mi::json& r, const std::string& fallbackName);

    std::filesystem::path executable_;
    std::filesystem::path cwd_;
    std::string tty_;
    GdbSession gdb_;
    State state_ = State::Starting;
    int selectedFrame_ = 0;
    std::map<std::pair<std::string, int>, std::string> breakpointIds_; // (file, line) -> gdb number
    std::shared_ptr<DebugController*> self_;

    sigc::signal<void(State)> stateChanged_;
    sigc::signal<void(const StopInfo&)> stopped_;
    sigc::signal<void(const std::string&)> finished_;
    sigc::signal<void(const std::string&)> console_;
};

} // namespace kevc
