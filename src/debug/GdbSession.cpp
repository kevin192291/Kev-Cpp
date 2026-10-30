#include "debug/GdbSession.h"

#include <csignal>

namespace kevc {

GdbSession::GdbSession(std::string gdbPath) : gdbPath_(std::move(gdbPath)), self_(std::make_shared<GdbSession*>(this)) {}

GdbSession::~GdbSession() {
    self_.reset();
    if (cancellable_) cancellable_->cancel();
    if (process_) {
        // gdb kills the inferior when it exits.
        if (input_) {
            try {
                const std::string quit = "-gdb-exit\n";
                gsize written = 0;
                input_->write_all(quit, written);
                input_->close();
            } catch (const Glib::Error&) {
            }
        }
        process_->send_signal(SIGTERM);
    }
}

void GdbSession::start(const std::filesystem::path& cwd) {
    auto launcher = Gio::SubprocessLauncher::create(Gio::Subprocess::Flags::STDIN_PIPE |
                                                    Gio::Subprocess::Flags::STDOUT_PIPE |
                                                    Gio::Subprocess::Flags::STDERR_MERGE);
    launcher->set_cwd(cwd.string());
    launcher->setenv("LC_ALL", "C.UTF-8", true);
    process_ = launcher->spawn({gdbPath_, "--interpreter=mi3", "--quiet",
                                // No interactive questions (Ubuntu's gdb asks about debuginfod) and no
                                // downloads of debug symbols for system libraries.
                                "-iex", "set debuginfod enabled off", "-iex", "set confirm off"});
    input_ = process_->get_stdin_pipe();
    output_ = Gio::DataInputStream::create(process_->get_stdout_pipe());
    cancellable_ = Gio::Cancellable::create();

    std::weak_ptr<GdbSession*> weak = self_;
    auto process = process_;
    process->wait_async(
        [weak, process](const Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                process->wait_finish(result);
            } catch (const Glib::Error&) {
            }
            if (auto self = weak.lock()) (*self)->onExited();
        },
        cancellable_);
    readNext();
}

void GdbSession::send(const std::string& command, Callback callback) {
    if (!input_) {
        if (callback) {
            mi::Record error;
            error.type = mi::Record::Type::Result;
            error.resultClass = "error";
            error.results["msg"] = "gdb is not running";
            callback(error);
        }
        return;
    }
    const int token = nextToken_++;
    if (callback) pending_[token] = std::move(callback);
    const std::string line = std::to_string(token) + command + "\n";
    try {
        gsize written = 0;
        input_->write_all(line, written);
        input_->flush();
    } catch (const Glib::Error&) {
        // The exit handler fails the pending callbacks.
    }
}

void GdbSession::readNext() {
    std::weak_ptr<GdbSession*> weak = self_;
    auto stream = output_;
    stream->read_line_async(
        [weak, stream](const Glib::RefPtr<Gio::AsyncResult>& result) {
            gsize length = 0;
            GError* error = nullptr;
            char* text = g_data_input_stream_read_line_finish_utf8(stream->gobj(), result->gobj(), &length, &error);
            auto self = weak.lock();
            if (error) g_error_free(error);
            if (!self || !text) {
                g_free(text);
                return; // EOF or shutting down; the exit handler takes over
            }
            std::string line(text, length);
            g_free(text);
            (*self)->dispatch(line);
            if (weak.lock()) (*self)->readNext();
        },
        cancellable_);
}

void GdbSession::dispatch(const std::string& line) {
    const mi::Record record = mi::parseLine(line);
    switch (record.type) {
    case mi::Record::Type::Result:
        if (record.token) {
            auto it = pending_.find(*record.token);
            if (it != pending_.end()) {
                Callback callback = std::move(it->second);
                pending_.erase(it);
                callback(record);
            }
        }
        break;
    case mi::Record::Type::Exec: exec_.emit(record); break;
    case mi::Record::Type::Notify: notify_.emit(record); break;
    case mi::Record::Type::Console:
    case mi::Record::Type::Target:
    case mi::Record::Type::Log: console_.emit(record.text); break;
    case mi::Record::Type::Unknown:
        // Non-MI text, e.g. "warning: ..." printed before the interpreter started.
        if (!record.text.empty()) console_.emit(record.text + "\n");
        break;
    default: break;
    }
}

void GdbSession::onExited() {
    if (!process_) return;
    process_.reset();
    input_.reset();
    output_.reset();
    auto pending = std::move(pending_);
    pending_.clear();
    mi::Record error;
    error.type = mi::Record::Type::Result;
    error.resultClass = "error";
    error.results["msg"] = "gdb exited";
    for (auto& [token, callback] : pending) callback(error);
    exited_.emit();
}

} // namespace kevc
