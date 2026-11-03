#pragma once

#include "debug/DebugPanel.ui.h"

#include "debug/DebugViews.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace kevc {

// The bottom "Debug" tab: Locals, Call Stack, Breakpoints and the GDB console. Its layout is
// DebugPanel.ui (edit it on Kev-C++'s Design page); this class fills the lists while debugging.
class DebugPanel : public DebugPanelUi {
public:
    static Glib::RefPtr<DebugPanel> create() { return DebugPanelUi::create<DebugPanel>(); }
    DebugPanel(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder);

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

protected:
    void on_console_entry_activate() override;

private:
    std::vector<int> stackLevels_;
    std::vector<std::pair<std::filesystem::path, int>> bpItems_;
    std::map<std::pair<std::filesystem::path, int>, Gtk::Label*> liveLabels_;
    std::map<std::pair<std::filesystem::path, int>, std::string> liveTexts_;

    sigc::signal<void(int)> frameActivated_;
    sigc::signal<void(const std::filesystem::path&, int)> bpActivated_;
    sigc::signal<void(const std::filesystem::path&, int)> bpRemoved_;
    sigc::signal<void(const std::filesystem::path&, int)> bpEdit_;
    sigc::signal<void(const std::string&)> watchRemoved_;
    sigc::signal<void(const std::string&)> consoleCommand_;
};

} // namespace kevc
