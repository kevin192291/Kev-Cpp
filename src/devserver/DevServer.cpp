#include "devserver/DevServer.h"

#include <csignal>

namespace kevc {

namespace {
constexpr std::string_view kReadyMarker = "KEVC_DEVSERVER_READY ";
}

DevServer::DevServer(std::string node, std::filesystem::path script)
    : node_(std::move(node)), script_(std::move(script)), self_(std::make_shared<DevServer*>(this)) {}

DevServer::~DevServer() {
    self_.reset();
    if (cancellable_) cancellable_->cancel();
    if (process_) process_->send_signal(SIGTERM);
}

void DevServer::start(const std::filesystem::path& root, int port) {
    if (process_) return;
    root_ = root;
    url_.clear();
    // stdin is a pipe only so the server notices if this process dies and exits with it.
    auto launcher = Gio::SubprocessLauncher::create(Gio::Subprocess::Flags::STDIN_PIPE |
                                                    Gio::Subprocess::Flags::STDOUT_PIPE |
                                                    Gio::Subprocess::Flags::STDERR_MERGE);
    launcher->set_cwd(root.string());
    process_ = launcher->spawn({node_, script_.string(), "--root", root.string(), "--port", std::to_string(port)});
    output_ = Gio::DataInputStream::create(process_->get_stdout_pipe());
    cancellable_ = Gio::Cancellable::create();

    std::weak_ptr<DevServer*> weak = self_;
    auto process = process_;
    process->wait_async(
        [weak, process](const Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                process->wait_finish(result);
            } catch (const Glib::Error&) {
            }
            auto self = weak.lock();
            if (!self) return;
            DevServer* server = *self;
            server->process_.reset();
            server->output_.reset();
            server->url_.clear();
            server->stopped_.emit(process->get_if_exited() ? process->get_exit_status() : -1);
        },
        cancellable_);
    readNext();
}

void DevServer::stop() {
    if (process_) process_->send_signal(SIGTERM);
}

void DevServer::readNext() {
    std::weak_ptr<DevServer*> weak = self_;
    auto stream = output_;
    stream->read_line_async(
        [weak, stream](const Glib::RefPtr<Gio::AsyncResult>& result) {
            gsize length = 0;
            GError* error = nullptr;
            char* text = g_data_input_stream_read_line_finish_utf8(stream->gobj(), result->gobj(), &length, &error);
            if (error) g_error_free(error);
            auto self = weak.lock();
            if (!self || !text) {
                g_free(text);
                return;
            }
            DevServer* server = *self;
            std::string line(text, length);
            g_free(text);
            if (line.starts_with(kReadyMarker)) {
                server->url_ = line.substr(kReadyMarker.size());
                server->ready_.emit(server->url_);
            } else {
                server->log_.emit(line);
            }
            if (weak.lock() && server->output_ == stream) server->readNext();
        },
        cancellable_);
}

} // namespace kevc
