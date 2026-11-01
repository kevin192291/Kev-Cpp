#pragma once

#include "debug/DebugController.h"

#include <gtkmm.h>

#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace kevc {

// One expandable "name = value" row. Data pointers also show what they point at
// ("p = 0x7ffe… → {x = 3, y = 4}"), and children are fetched from gdb when expanded.
// Which rows are expanded, by path ("L:head/next"), so the Locals and Watch trees can be rebuilt
// after every step without collapsing what the user opened.
using ExpandedRows = std::shared_ptr<std::set<std::string>>;

class VarNode : public Gtk::Box {
public:
    // `ownsVarobj`: delete the gdb variable object when this node goes away (top-level nodes only).
    // `path`/`expanded`: remember and restore this row's open state (optional).
    VarNode(std::weak_ptr<DebugController> debugger, VarInfo info, int depth, bool ownsVarobj,
            std::string path = {}, ExpandedRows expanded = {});
    ~VarNode() override;

    // A row that shows an error instead of a value (e.g. a watch that can't be evaluated).
    static VarNode* error(const std::string& name, const std::string& message);
    VarNode(const std::string& name, const std::string& message); // error row; prefer error()

private:
    void updateHeader();
    void toggle();
    void setExpanded(bool expand);

    std::weak_ptr<DebugController> debugger_;
    VarInfo info_;
    int depth_ = 0;
    bool ownsVarobj_ = false;
    bool loaded_ = false;
    std::string path_;
    ExpandedRows expandedRows_;
    std::string pointee_;
    bool pointeeOk_ = true;
    // gdb replies can arrive after this row is gone; callbacks hold a weak reference to this.
    std::shared_ptr<char> alive_ = std::make_shared<char>();

    Gtk::Box header_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Button expander_;
    Gtk::Image icon_; // what kind of value: int, pointer, array, string, ...
    Gtk::Label text_;
    Gtk::Label type_;
    Gtk::Box children_{Gtk::Orientation::VERTICAL};
};

// A row of the Breakpoints tab.
struct BreakpointItem {
    std::filesystem::path file;
    int line = 0;
    Breakpoint breakpoint;
};
// A "break when the value changes" watch and what the debugger says about it.
struct ValueWatchItem {
    std::string expression;
    std::string state; // e.g. "watching", "waiting: …", "changed from 1 to 2"
    bool armed = false;
};

// The bottom "Debug" tab: Locals, Call Stack, Breakpoints and the GDB console.
class DebugPanel : public Gtk::Notebook {
public:
    DebugPanel();

    void showMessage(const Glib::ustring& text); // clears locals and stack
    void setLocals(const std::vector<Gtk::Widget*>& nodes);
    void setFrames(const std::vector<StackFrame>& frames, int selected);
    // The Breakpoints tab: every breakpoint (with its kind, condition or values) and value watch.
    void setBreakpoints(const std::vector<BreakpointItem>& breakpoints, const std::vector<ValueWatchItem>& watches);
    // A live breakpoint's latest values, kept across setBreakpoints until cleared.
    void setLiveValues(const std::filesystem::path& file, int line, const std::string& text);
    void clearLiveValues();
    void appendConsole(const std::string& text);
    void clearConsole();

    sigc::signal<void(int level)>& signalFrameActivated() { return frameActivated_; }
    sigc::signal<void(const std::filesystem::path&, int line)>& signalBreakpointActivated() { return bpActivated_; }
    sigc::signal<void(const std::filesystem::path&, int line)>& signalBreakpointRemoved() { return bpRemoved_; }
    // Change an IF breakpoint's condition or a live breakpoint's values.
    sigc::signal<void(const std::filesystem::path&, int line)>& signalBreakpointEdit() { return bpEdit_; }
    sigc::signal<void(const std::string& expression)>& signalValueWatchRemoved() { return watchRemoved_; }
    sigc::signal<void(const std::string&)>& signalConsoleCommand() { return consoleCommand_; }

private:
    Gtk::ScrolledWindow localsScroller_;
    Gtk::Box locals_{Gtk::Orientation::VERTICAL};
    Gtk::Label localsMessage_;
    Gtk::ScrolledWindow stackScroller_;
    Gtk::ListBox stack_;
    std::vector<int> stackLevels_;
    Gtk::Box bpPage_{Gtk::Orientation::VERTICAL};
    Gtk::FlowBox bpButtons_; // wraps when the panel is docked narrow
    Gtk::ScrolledWindow bpScroller_;
    Gtk::ListBox breakpoints_;
    std::vector<std::pair<std::filesystem::path, int>> bpItems_;
    std::map<std::pair<std::filesystem::path, int>, Gtk::Label*> liveLabels_;
    std::map<std::pair<std::filesystem::path, int>, std::string> liveTexts_;
    Gtk::Box console_{Gtk::Orientation::VERTICAL};
    Gtk::ScrolledWindow consoleScroller_;
    Gtk::TextView consoleView_;
    Gtk::Entry consoleEntry_;

    sigc::signal<void(int)> frameActivated_;
    sigc::signal<void(const std::filesystem::path&, int)> bpActivated_;
    sigc::signal<void(const std::filesystem::path&, int)> bpRemoved_;
    sigc::signal<void(const std::filesystem::path&, int)> bpEdit_;
    sigc::signal<void(const std::string&)> watchRemoved_;
    sigc::signal<void(const std::string&)> consoleCommand_;
};

// The left "Debug" tab: watch expressions, re-evaluated every time the program stops.
class WatchPanel : public Gtk::Box {
public:
    WatchPanel();

    const std::vector<std::string>& expressions() const { return expressions_; }
    void addExpression(const std::string& expression);
    // The project's saved watches (or none); doesn't signal a change.
    void setExpressions(std::vector<std::string> expressions);
    // One row per expression, in order (VarNodes or error rows).
    void setRows(const std::vector<Gtk::Widget*>& rows);
    void showPlaceholders(); // expressions without values (not debugging)

    sigc::signal<void()>& signalChanged() { return changed_; }

private:
    void rebuild(const std::vector<Gtk::Widget*>& rows);

    Gtk::Entry entry_;
    Gtk::ScrolledWindow scroller_;
    Gtk::Box list_{Gtk::Orientation::VERTICAL};
    std::vector<std::string> expressions_;
    sigc::signal<void()> changed_;
};

} // namespace kevc
