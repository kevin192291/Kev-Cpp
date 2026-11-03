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

} // namespace kevc
