#include "terminal/TerminalPane.h"

#include "core/Toolchain.h"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <sys/wait.h>

namespace kevc {

namespace {

struct SpawnContext {
    std::weak_ptr<TerminalPane*> pane;
};

// Environment for what runs in the terminal: the folder Kev-C++ is installed in comes first on
// PATH, so `kev` works here even when the desktop session's PATH lacks ~/.local/bin (Debian-based
// desktops don't read ~/.profile at login). Only an installed Kev-C++ has `kev` next to it; a build
// folder is left out. KEVC_TERMINAL_PATH_ADDED names the folder, so the installer (run here by
// Help → Check for Updates) can tell other terminals won't have it.
std::vector<std::string> terminalEnvironment() {
    const char* inherited = std::getenv("PATH");
    const std::string path = inherited ? inherited : "";
    std::error_code ec;
    const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec || !std::filesystem::exists(exe.parent_path() / "kev", ec)) return {};
    const std::string dir = exe.parent_path().string(), added = prependToPath(path, dir);
    if (added == path) return {};
    return {"PATH=" + added, "KEVC_TERMINAL_PATH_ADDED=" + dir};
}

std::string describeStatus(int status) {
    if (WIFEXITED(status)) return "Process exited with code " + std::to_string(WEXITSTATUS(status));
    if (WIFSIGNALED(status)) {
        const int sig = WTERMSIG(status);
        const char* name = strsignal(sig);
        return "Process terminated by signal " + std::to_string(sig) + (name ? std::string(" (") + name + ")" : "");
    }
    return "Process ended";
}

} // namespace

TerminalPane::TerminalPane(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder)
    : TerminalPaneUi(object, builder), terminal_(VTE_TERMINAL(vte_->gobj())),
      self_(std::make_shared<TerminalPane*>(this)) {
    PangoFontDescription* font = pango_font_description_from_string("DejaVu Sans Mono 10");
    vte_terminal_set_font(terminal_, font);
    pango_font_description_free(font);
    // The console colours of the Slate palette: white on PowerShell navy. The 16 colours are
    // PowerShell's, with blue and magenta lightened: the shell prompt's blue path is unreadable on
    // navy otherwise.
    static const char* const kPalette[16] = {"#0c0c0c", "#e74856", "#16c60c", "#e5c100", "#5b9bff", "#c867e0",
                                             "#3a96dd", "#cccccc", "#767676", "#ff6b78", "#3ee03a", "#f9f1a5",
                                             "#8ab8ff", "#e08cf0", "#61d6d6", "#f2f2f2"};
    GdkRGBA palette[16];
    for (int i = 0; i < 16; ++i) gdk_rgba_parse(&palette[i], kPalette[i]);
    GdkRGBA fg, bg;
    gdk_rgba_parse(&fg, "#eeedf0");
    gdk_rgba_parse(&bg, "#012456");
    vte_terminal_set_colors(terminal_, &fg, &bg, palette, 16);
    g_signal_connect(terminal_, "child-exited", G_CALLBACK(&TerminalPane::onChildExited), this);
    scrollbar_->set_adjustment(Glib::wrap(gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(terminal_)), true));
}

TerminalPane::~TerminalPane() {
    self_.reset();
    g_signal_handlers_disconnect_by_data(terminal_, this);
    pending_.reset();
    signalChild();
}

void TerminalPane::startShell(const std::filesystem::path& cwd) {
    const char* shell = std::getenv("SHELL");
    request({{shell && *shell ? shell : "/bin/bash"}, cwd, true, {}});
}

void TerminalPane::runProgram(const std::vector<std::string>& argv, const std::filesystem::path& cwd,
                              ExitHandler onExit) {
    request({argv, cwd, false, std::move(onExit)});
}

void TerminalPane::stopChild() {
    pending_.reset();
    shellMode_ = false; // a deliberately stopped shell is not restarted
    signalChild();
}

void TerminalPane::request(Launch launch) {
    if (state_ == State::Idle) {
        start(std::move(launch));
        return;
    }
    pending_ = std::move(launch);
    shellMode_ = false;
    // Running: stop it now. Spawning: onSpawned stops it as soon as its pid is known.
    if (state_ == State::Running) signalChild();
}

void TerminalPane::start(Launch launch) {
    shellMode_ = launch.shell;
    childIsShell_ = launch.shell;
    if (launch.shell) shellCwd_ = launch.cwd;
    else vte_terminal_reset(terminal_, TRUE, TRUE);
    onExit_ = std::move(launch.onExit);
    state_ = State::Spawning;

    std::vector<char*> args;
    for (auto& a : launch.argv) args.push_back(a.data());
    args.push_back(nullptr);
    auto* ctx = new SpawnContext{self_};
    // VTE adds these to the environment the child inherits.
    std::vector<std::string> variables = terminalEnvironment();
    std::vector<char*> env;
    for (auto& v : variables) env.push_back(v.data());
    env.push_back(nullptr);
    // SEARCH_PATH: programs are full paths, but package.json scripts run as plain `npm run …`.
    vte_terminal_spawn_async(terminal_, VTE_PTY_DEFAULT, launch.cwd.c_str(), args.data(), env.data(), G_SPAWN_SEARCH_PATH,
                             nullptr, nullptr, nullptr, -1, nullptr, &TerminalPane::onSpawned, ctx);
}

void TerminalPane::signalChild() {
    if (state_ != State::Running || pid_ <= 0) return;
    if (childIsShell_) {
        kill(pid_, SIGHUP); // interactive shells ignore SIGTERM
        return;
    }
    kill(pid_, SIGTERM);
    // Programs that ignore SIGTERM get SIGKILL after a grace period.
    const GPid pid = pid_;
    const unsigned generation = ++killGeneration_;
    std::weak_ptr<TerminalPane*> weak = self_;
    Glib::signal_timeout().connect_once(
        [weak, pid, generation] {
            auto self = weak.lock();
            if (self && (*self)->pid_ == pid && (*self)->killGeneration_ == generation) kill(pid, SIGKILL);
        },
        2000);
}

void TerminalPane::onSpawned(VteTerminal*, GPid pid, GError* error, gpointer data) {
    std::unique_ptr<SpawnContext> ctx(static_cast<SpawnContext*>(data));
    auto self = ctx->pane.lock();
    if (!self) {
        if (!error && pid > 0) kill(pid, SIGTERM);
        return;
    }
    TerminalPane* pane = *self;
    if (error) {
        pane->state_ = State::Idle;
        pane->feedText(std::string("\nCould not start: ") + error->message + "\n");
        if (auto onExit = std::move(pane->onExit_)) onExit(error->message);
        if (pane->pending_) {
            Launch next = std::move(*pane->pending_);
            pane->pending_.reset();
            pane->start(std::move(next));
        }
        return;
    }
    pane->state_ = State::Running;
    pane->pid_ = pid;
    if (pane->pending_) pane->signalChild();
}

void TerminalPane::onChildExited(VteTerminal*, int status, gpointer data) {
    auto* pane = static_cast<TerminalPane*>(data);
    pane->state_ = State::Idle;
    pane->pid_ = -1;
    if (pane->pending_) {
        Launch next = std::move(*pane->pending_);
        pane->pending_.reset();
        pane->start(std::move(next));
        return;
    }
    if (pane->shellMode_) {
        pane->feedText("\n[shell exited, restarting]\n");
        pane->startShell(pane->shellCwd_);
        return;
    }
    const std::string outcome = describeStatus(status);
    pane->feedText("\n\x1b[2m[" + outcome + "]\x1b[0m\n");
    if (auto onExit = std::move(pane->onExit_)) onExit(outcome);
}

std::string TerminalPane::prepareForInferior() {
    if (state_ != State::Idle) return {};
    vte_terminal_reset(terminal_, TRUE, TRUE);
    GError* error = nullptr;
    VtePty* pty = vte_terminal_pty_new_sync(terminal_, VTE_PTY_DEFAULT, nullptr, &error);
    if (!pty) {
        if (error) {
            feedText(std::string("Could not create a terminal for the program: ") + error->message + "\n");
            g_error_free(error);
        }
        return {};
    }
    vte_terminal_set_pty(terminal_, pty); // the terminal keeps its own reference
    const char* name = ptsname(vte_pty_get_fd(pty));
    std::string path = name ? name : "";
    g_object_unref(pty);
    return path;
}

void TerminalPane::feedText(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '\n') out += '\r';
        out += c;
    }
    vte_terminal_feed(terminal_, out.data(), static_cast<gssize>(out.size()));
}

void TerminalPane::focusTerminal() {
    gtk_widget_grab_focus(GTK_WIDGET(terminal_));
}

} // namespace kevc
