#pragma once

#include "terminal/TerminalPane.ui.h"

#include <gtkmm.h>
#include <vte/vte.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kevc {

// A VTE terminal that runs either the user's shell or a program the IDE launched (the Terminal and
// Run tabs). Only one child runs at a time: starting another stops the current one first and launches
// the new one once the old child has really exited, so exit notifications never get mixed up. The
// terminal and its scrollbar are TerminalPane.ui; the font and colours are set here, since VTE takes
// its palette through a call rather than properties.
class TerminalPane : public TerminalPaneUi {
public:
    using ExitHandler = std::function<void(const std::string& outcome)>;

    static Glib::RefPtr<TerminalPane> create() {
        g_type_ensure(VTE_TYPE_TERMINAL); // so GtkBuilder finds the class the design names
        return TerminalPaneUi::create<TerminalPane>();
    }
    TerminalPane(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder);
    ~TerminalPane() override;

    // Starts $SHELL in `cwd`, restarting it automatically whenever it exits.
    void startShell(const std::filesystem::path& cwd);
    // Clears the terminal and runs `argv`; `onExit` receives a description of how it ended.
    void runProgram(const std::vector<std::string>& argv, const std::filesystem::path& cwd, ExitHandler onExit = {});

    bool childRunning() const { return state_ != State::Idle; }
    // Clears the terminal and gives it a fresh pty for a program started by someone else (gdb).
    // Returns the pty's device path (e.g. /dev/pts/7), or empty on failure. No child may be running.
    std::string prepareForInferior();
    // Stops the current child (SIGTERM); its exit is reported as usual.
    void stopChild();
    void feedText(const std::string& text); // "\n" is translated to "\r\n"
    void focusTerminal();

private:
    enum class State { Idle, Spawning, Running };
    struct Launch {
        std::vector<std::string> argv;
        std::filesystem::path cwd;
        bool shell = false;
        ExitHandler onExit;
    };

    void request(Launch launch);
    void start(Launch launch);
    void signalChild();
    static void onSpawned(VteTerminal*, GPid pid, GError* error, gpointer data);
    static void onChildExited(VteTerminal*, int status, gpointer self);

    VteTerminal* terminal_ = nullptr;
    State state_ = State::Idle;
    GPid pid_ = -1;
    bool shellMode_ = false;    // restart the shell when it exits
    bool childIsShell_ = false; // what the current child is, for choosing how to stop it
    unsigned killGeneration_ = 0;
    std::filesystem::path shellCwd_;
    ExitHandler onExit_;
    std::optional<Launch> pending_;
    std::shared_ptr<TerminalPane*> self_;
};

} // namespace kevc
