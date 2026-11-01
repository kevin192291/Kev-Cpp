#include "process/ManagedProcess.h"

#include "core/ProcessTree.h"

#include <sys/prctl.h>
#include <unistd.h>

#include <csignal>
#include <cstdlib>

namespace kevc {

namespace {

// Runs in the child between fork and exec.
void childSetup(gpointer) {
    setpgid(0, 0);
    // If the IDE dies without stopping us (a crash, kill -9), take the server down with it.
    prctl(PR_SET_PDEATHSIG, SIGTERM);
}

// SIGKILL for whatever is left of the group, unless it already exited. The group id can't be
// reused while any process in it is alive, so this never hits an unrelated process group.
void killLater(pid_t group, std::shared_ptr<bool> exited, int graceMs) {
    Glib::signal_timeout().connect_once(
        [group, exited] {
            if (!*exited) kill(-group, SIGKILL);
        },
        graceMs);
}

// SIGTERM for the group and for anything below the leader that left it (js-debug's watchdog runs
// in its own session under Vite; while it lived, Vite waited for its debugger to go and never
// exited). The tree is read first: once the leader exits its children move to init.
void terminateTree(pid_t leader) {
    const auto below = proc::descendants(leader);
    kill(-leader, SIGTERM);
    for (int pid : below)
        if (getpgid(pid) != leader) kill(pid, SIGTERM);
}

} // namespace

ManagedProcess::ManagedProcess() : self_(std::make_shared<ManagedProcess*>(this)) {}

ManagedProcess::~ManagedProcess() {
    self_.reset();
    if (cancellable_) cancellable_->cancel();
    if (pid_ > 0 && process_) terminateTree(pid_);
}

void ManagedProcess::start(const Options& options) {
    if (process_) return;
    // stdin is a pipe only so tools that watch it notice when the IDE goes away.
    auto launcher = Gio::SubprocessLauncher::create(Gio::Subprocess::Flags::STDIN_PIPE |
                                                    Gio::Subprocess::Flags::STDOUT_PIPE |
                                                    Gio::Subprocess::Flags::STDERR_MERGE);
    if (!options.cwd.empty()) launcher->set_cwd(options.cwd.string());
    for (const auto& [name, value] : options.env) launcher->setenv(name, value, true);
    if (!options.pathPrepend.empty()) {
        const char* path = std::getenv("PATH");
        launcher->setenv("PATH", options.pathPrepend + (path && *path ? std::string(":") + path : ""), true);
    }
    g_subprocess_launcher_set_child_setup(launcher->gobj(), childSetup, nullptr, nullptr);
    process_ = launcher->spawn(options.argv);
    pid_ = static_cast<pid_t>(std::atoi(process_->get_identifier().c_str()));
    output_ = Gio::DataInputStream::create(process_->get_stdout_pipe());
    cancellable_ = Gio::Cancellable::create();

    std::weak_ptr<ManagedProcess*> weak = self_;
    auto process = process_;
    process->wait_async(
        [weak, process](const Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                process->wait_finish(result);
            } catch (const Glib::Error&) {
            }
            auto self = weak.lock();
            if (!self) return;
            ManagedProcess* p = *self;
            if (p->process_ != process) return;
            p->process_.reset();
            p->output_.reset();
            p->exited_.emit(process->get_if_exited() ? process->get_exit_status() : -1);
        },
        cancellable_);
    readNext();
}

void ManagedProcess::stop(int graceMs) {
    if (!process_ || pid_ <= 0) return;
    terminateTree(pid_);
    // Tracks the leader: once it is reaped, the SIGKILL below is skipped.
    auto exited = std::make_shared<bool>(false);
    auto process = process_;
    process->wait_async([exited, process](const Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
            process->wait_finish(result);
        } catch (const Glib::Error&) {
        }
        *exited = true;
    });
    killLater(pid_, exited, graceMs);
}

void ManagedProcess::readNext() {
    std::weak_ptr<ManagedProcess*> weak = self_;
    auto stream = output_;
    stream->read_line_async(
        [weak, stream](const Glib::RefPtr<Gio::AsyncResult>& result) {
            gsize length = 0;
            GError* error = nullptr;
            // Raw bytes rather than the _utf8 variant, which gives up on a line that isn't UTF-8 and
            // would leave the child blocked on a full pipe.
            char* text = g_data_input_stream_read_line_finish(stream->gobj(), result->gobj(), &length, &error);
            if (error) g_error_free(error);
            auto self = weak.lock();
            if (!self || !text) {
                g_free(text);
                return;
            }
            ManagedProcess* p = *self;
            char* valid = g_utf8_make_valid(text, static_cast<gssize>(length));
            std::string line(valid);
            g_free(valid);
            g_free(text);
            p->line_.emit(line);
            if (weak.lock() && p->output_ == stream) p->readNext();
        },
        cancellable_);
}

} // namespace kevc
