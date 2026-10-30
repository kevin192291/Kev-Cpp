#include "build/BuildRunner.h"

#include "core/Diagnostics.h"

#include <csignal>
#include <unistd.h>

namespace kevc {

namespace {

std::string cleanLine(const char* text, gsize length) {
    // Compilers echo source lines, which aren't always UTF-8; GTK only accepts valid UTF-8.
    char* valid = g_utf8_make_valid(text, static_cast<gssize>(length));
    std::string_view line(valid);
    // Progress output (git during FetchContent) redraws itself with \r: keep what was shown last.
    // (Before stripAnsi, which drops every \r.)
    while (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (const auto cr = line.rfind('\r'); cr != std::string_view::npos) line.remove_prefix(cr + 1);
    std::string clean = stripAnsi(line);
    g_free(valid);
    return clean;
}

} // namespace

BuildRunner::BuildRunner() : self_(std::make_shared<BuildRunner*>(this)) {}

BuildRunner::~BuildRunner() {
    self_.reset();
    if (cancellable_) cancellable_->cancel();
    signalGroup(SIGTERM); // don't leave compilers running after the window is gone
}

void BuildRunner::start(std::vector<Command> commands) {
    if (running_) return;
    commands_ = std::move(commands);
    next_ = 0;
    running_ = true;
    cancelled_ = false;
    startNext();
}

void BuildRunner::cancel() {
    if (!running_ || cancelled_) return;
    cancelled_ = true;
    signalGroup(SIGTERM);
}

void BuildRunner::signalGroup(int sig) {
    if (pid_ > 0) kill(-pid_, sig);
}

void BuildRunner::startNext() {
    if (next_ >= commands_.size()) {
        finish(true);
        return;
    }
    const Command command = commands_[next_++];
    line_.emit(displayCommand(command.argv), true);

    auto launcher = Gio::SubprocessLauncher::create(Gio::Subprocess::Flags::STDOUT_PIPE |
                                                    Gio::Subprocess::Flags::STDERR_MERGE);
    if (!command.cwd.empty()) launcher->set_cwd(command.cwd.string());
    // DiagnosticParser matches the English messages ("error:", "CMake Warning"); keep UTF-8 output.
    launcher->setenv("LC_ALL", "C.UTF-8", true);
    launcher->unsetenv("LANGUAGE");
    // A process group of its own, so cancel() also stops the compilers cmake/make/ninja started.
    // (The C call: giomm only wraps it from 2.78, newer than some distributions ship.)
    g_subprocess_launcher_set_child_setup(launcher->gobj(), [](gpointer) { setpgid(0, 0); }, nullptr, nullptr);
    try {
        process_ = launcher->spawn(command.argv);
    } catch (const Glib::Error& e) {
        line_.emit(e.what(), false);
        finish(false);
        return;
    }
    pid_ = static_cast<pid_t>(std::stol(process_->get_identifier().raw()));
    output_ = Gio::DataInputStream::create(process_->get_stdout_pipe());
    cancellable_ = Gio::Cancellable::create();
    exited_ = false;
    eof_ = false;

    std::weak_ptr<BuildRunner*> weak = self_;
    auto process = process_;
    process->wait_async(
        [weak, process](const Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                process->wait_finish(result);
            } catch (const Glib::Error&) {
            }
            auto self = weak.lock();
            if (!self || (*self)->process_ != process) return;
            (*self)->exited_ = true;
            (*self)->stepDone();
        },
        cancellable_);
    readNext();
}

void BuildRunner::readNext() {
    std::weak_ptr<BuildRunner*> weak = self_;
    auto stream = output_;
    stream->read_line_async(
        [weak, stream](const Glib::RefPtr<Gio::AsyncResult>& result) {
            gsize length = 0;
            GError* error = nullptr;
            char* text = g_data_input_stream_read_line_finish(stream->gobj(), result->gobj(), &length, &error);
            if (error) g_error_free(error);
            auto self = weak.lock();
            if (!self || (*self)->output_ != stream) {
                g_free(text);
                return;
            }
            BuildRunner* runner = *self;
            if (!text) { // end of output (or a read error, which ends it too)
                runner->eof_ = true;
                runner->stepDone();
                return;
            }
            const std::string line = cleanLine(text, length);
            g_free(text);
            runner->line_.emit(line, false);
            if (weak.lock() && runner->output_ == stream) runner->readNext();
        },
        cancellable_);
}

void BuildRunner::stepDone() {
    if (!exited_ || !eof_) return;
    auto process = std::move(process_);
    output_.reset();
    cancellable_.reset();
    pid_ = 0;
    if (cancelled_) {
        line_.emit("Stopped.", false);
        finish(false);
    } else if (!process->get_successful()) {
        // No "exited with status N" line: the tool's own last line says more (Project Options shows
        // the last line when target detection fails).
        finish(false);
    } else {
        startNext();
    }
}

void BuildRunner::finish(bool success) {
    running_ = false;
    commands_.clear();
    finished_.emit(success); // last: a handler may start the next build
}

} // namespace kevc
