#pragma once

#include "core/Breakpoints.h"
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

    // Starts gdb, sets the breakpoints and value watches, and runs the program.
    void start(const BreakpointMap& breakpoints, const std::vector<std::string>& valueWatches);
    State state() const { return state_; }
    bool stopped() const { return state_ == State::Stopped; }

    void resume();   // continue
    void stepOver(); // next
    void stepInto(); // step
    void stepOut();  // finish
    void pause();    // interrupt a running program
    void terminate();

    // Sets, changes or (nullopt) removes the breakpoint on a line. IF breakpoints are gdb conditions;
    // live ones are dprintf breakpoints printing through Python (see gdbLivePython), or, with a gdb
    // that has no Python, breakpoints that stop for a moment: values read, liveHit, continue.
    void setBreakpoint(const std::filesystem::path& file, int line, const std::optional<Breakpoint>& breakpoint);
    // Break when the value changes: a gdb watchpoint. One gdb can't place yet (a local of a function
    // that isn't running) is tried again each time the program pauses, and after it goes out of scope.
    void addValueWatch(const std::string& expression);
    void removeValueWatch(const std::string& expression);
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
    // A live breakpoint ran (the program has carried on): its expressions and their values (the
    // frame's locals when it names none).
    sigc::signal<void(const std::filesystem::path& file, int line, const std::vector<std::string>& names,
                      const std::vector<std::string>& values)>&
    signalLiveHit() {
        return liveHit_;
    }
    // A value watch is placed (true), or isn't and why.
    sigc::signal<void(const std::string& expression, bool armed, const std::string& note)>& signalWatchState() {
        return watchState_;
    }

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
    using Key = std::pair<std::string, int>; // (file, line)
    void place(const Key& key);
    void onConsole(const std::string& text);
    void onLiveHit(const std::filesystem::path& file, int line, const Breakpoint& breakpoint);
    void emitLive(const Key& key, const std::vector<std::string>& values);
    void armWatch(const std::string& expression);
    void armPendingWatches();
    void setWatchState(const std::string& expression, bool armed, const std::string& note);
    struct Placed {
        std::string number; // gdb's, "" until it answers
        Breakpoint breakpoint;
        int liveId = 0; // the dprintf's marker
    };
    struct Watch {
        std::string number; // gdb's, "" while not placed
        std::optional<std::pair<bool, std::string>> reported; // what watchState last said
    };
    bool ready_ = false;  // gdb has said whether it has Python; breakpoints wait for that
    bool python_ = false; // live breakpoints are dprintf
    int nextLiveId_ = 1;
    std::map<Key, Placed> breakpointIds_;
    std::map<int, Key> liveIds_;
    std::map<std::string, Watch> watches_; // expression -> its watchpoint
    std::shared_ptr<DebugController*> self_;

    sigc::signal<void(State)> stateChanged_;
    sigc::signal<void(const StopInfo&)> stopped_;
    sigc::signal<void(const std::string&)> finished_;
    sigc::signal<void(const std::string&)> console_;
    sigc::signal<void(const std::filesystem::path&, int, const std::vector<std::string>&, const std::vector<std::string>&)> liveHit_;
    sigc::signal<void(const std::string&, bool, const std::string&)> watchState_;
};

} // namespace kevc
