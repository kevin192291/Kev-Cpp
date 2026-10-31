#pragma once

#include "core/Dap.h"

#include <gtkmm.h>

#include <functional>
#include <memory>
#include <vector>

namespace kevc {

// One variable in the JavaScript debugger's Locals or Watch view: "name = value", expanding to its
// children (asked for with a DAP `variables` request), laid out like the gdb views' VarNode.
class DapVarNode : public Gtk::Box {
public:
    using Fetch = std::function<void(int reference, std::function<void(std::vector<dap::Variable>)>)>;

    DapVarNode(dap::Variable variable, int depth, Fetch fetch, bool expand = false);
    // A row for something that couldn't be evaluated.
    static DapVarNode* error(const std::string& name, const std::string& message);

private:
    void toggle();

    dap::Variable variable_;
    int depth_ = 0;
    Fetch fetch_;
    bool expanded_ = false;
    bool loaded_ = false;
    Gtk::Box header_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Button expander_;
    Gtk::Label text_;
    Gtk::Label type_;
    Gtk::Box children_{Gtk::Orientation::VERTICAL};
    std::shared_ptr<char> alive_ = std::make_shared<char>();
};

} // namespace kevc
