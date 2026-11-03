#pragma once

#include "debug/WatchPanel.ui.h"

#include <string>
#include <vector>

namespace kevc {

// The left "Debug" tab: watch expressions, re-evaluated every time the program stops.
class WatchPanel : public WatchPanelUi {
public:
    static Glib::RefPtr<WatchPanel> create() { return WatchPanelUi::create<WatchPanel>(); }
    WatchPanel(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder);

    const std::vector<std::string>& expressions() const { return expressions_; }
    void addExpression(const std::string& expression);
    // The project's saved watches (or none); doesn't signal a change.
    void setExpressions(std::vector<std::string> expressions);
    // One row per expression, in order (VarNodes or error rows).
    void setRows(const std::vector<Gtk::Widget*>& rows);
    void showPlaceholders(); // expressions without values (not debugging)

    sigc::signal<void()>& signalChanged() { return changed_; }

protected:
    void on_entry_activate() override;

private:
    void rebuild(const std::vector<Gtk::Widget*>& rows);

    std::vector<std::string> expressions_;
    sigc::signal<void()> changed_;
};

} // namespace kevc
