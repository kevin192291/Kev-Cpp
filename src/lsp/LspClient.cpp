#include "lsp/LspClient.h"

#include <csignal>
#include <unistd.h>

namespace kevc {

namespace {

constexpr gsize kReadChunk = 64 * 1024;

LspClient::json clientCapabilities() {
    return {
        {"general", {{"positionEncodings", {"utf-16"}}}},
        {"workspace", {{"configuration", true}, {"workspaceFolders", true}}},
        {"textDocument",
         {{"synchronization", {{"didSave", true}, {"dynamicRegistration", false}}},
          {"completion",
           {{"contextSupport", true},
            {"completionItem",
             {{"snippetSupport", false},
              {"labelDetailsSupport", true},
              {"documentationFormat", {"markdown", "plaintext"}}}}}},
          {"hover", {{"contentFormat", {"markdown", "plaintext"}}}},
          {"definition", {{"linkSupport", true}}},
          {"documentSymbol", {{"hierarchicalDocumentSymbolSupport", true}}},
          {"publishDiagnostics", {{"relatedInformation", false}}}}},
    };
}

} // namespace

LspClient::LspClient(std::string name, std::vector<std::string> argv, std::filesystem::path root, json initOptions,
                     std::filesystem::path logFile)
    : name_(std::move(name)), argv_(std::move(argv)), root_(std::move(root)), initOptions_(std::move(initOptions)),
      logFile_(std::move(logFile)), self_(std::make_shared<LspClient*>(this)) {}

LspClient::~LspClient() {
    self_.reset();
    if (cancellable_) cancellable_->cancel();
    if (process_) {
        // Servers exit on EOF of stdin, but don't rely on it. close() refuses while a write is
        // pending (cancelled above, but its callback hasn't run yet); the pipe then closes when
        // that write releases the stream.
        if (input_) {
            try {
                input_->close();
            } catch (const Glib::Error&) {
            }
        }
        process_->send_signal(SIGTERM);
    }
}

void LspClient::start() {
    auto launcher = Gio::SubprocessLauncher::create(Gio::Subprocess::Flags::STDIN_PIPE |
                                                    Gio::Subprocess::Flags::STDOUT_PIPE);
    launcher->set_cwd(root_.string());
    if (!logFile_.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(logFile_.parent_path(), ec);
        launcher->set_stderr_file_path(logFile_.string());
    }
    process_ = launcher->spawn(argv_);
    input_ = process_->get_stdin_pipe();
    output_ = process_->get_stdout_pipe();
    cancellable_ = Gio::Cancellable::create();

    std::weak_ptr<LspClient*> weak = self_;
    auto process = process_;
    process->wait_async(
        [weak, process](const Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                process->wait_finish(result);
            } catch (const Glib::Error&) {
            }
            if (auto self = weak.lock()) {
                (*self)->onExited(process->get_if_exited()
                                      ? "exited with code " + std::to_string(process->get_exit_status())
                                      : std::string("was terminated"));
            }
        },
        cancellable_);
    readNext();

    // initialize goes out immediately; everything else waits for its response.
    const int id = nextId_++;
    pending_[id] = [this](const json& result, const json* error) {
        if (error) {
            onExited("failed to initialize: " + error->value("message", std::string("unknown error")));
            return;
        }
        capabilities_ = result.value("capabilities", json::object());
        ready_ = true;
        send({{"jsonrpc", "2.0"}, {"method", "initialized"}, {"params", json::object()}});
        for (auto& message : pendingUntilReady_) send(message);
        pendingUntilReady_.clear();
        readySignal_.emit();
    };
    send({{"jsonrpc", "2.0"},
          {"id", id},
          {"method", "initialize"},
          {"params",
           {{"processId", static_cast<int>(getpid())},
            {"clientInfo", {{"name", "Kev-C++"}}},
            {"rootUri", lsp::pathToUri(root_)},
            {"rootPath", root_.string()},
            {"workspaceFolders", {{{"uri", lsp::pathToUri(root_)}, {"name", root_.filename().string()}}}},
            {"capabilities", clientCapabilities()},
            {"initializationOptions", initOptions_}}}});
}

void LspClient::request(const std::string& method, json params, Callback callback) {
    const int id = nextId_++;
    pending_[id] = std::move(callback);
    json message = {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", std::move(params)}};
    if (ready_) send(message);
    else pendingUntilReady_.push_back(std::move(message));
}

void LspClient::notify(const std::string& method, json params) {
    json message = {{"jsonrpc", "2.0"}, {"method", method}, {"params", std::move(params)}};
    if (ready_) send(message);
    else pendingUntilReady_.push_back(std::move(message));
}

void LspClient::send(const json& message) {
    if (!input_) return;
    writeQueue_.push_back(lsp::frame(message));
    if (!writing_) writeNext();
}

void LspClient::writeNext() {
    if (writeQueue_.empty() || !input_) {
        writing_ = false;
        return;
    }
    writing_ = true;
    auto bytes = Glib::Bytes::create(writeQueue_.front().data(), writeQueue_.front().size());
    std::weak_ptr<LspClient*> weak = self_;
    auto stream = input_;
    stream->write_bytes_async(
        bytes,
        [weak, stream](const Glib::RefPtr<Gio::AsyncResult>& result) {
            gssize written = -1;
            try {
                written = stream->write_bytes_finish(result);
            } catch (const Glib::Error&) {
            }
            auto self = weak.lock();
            if (!self) return;
            LspClient* client = *self;
            if (written < 0) {
                client->writing_ = false;
                client->writeQueue_.clear();
                return; // the exit handler reports the failure
            }
            std::string& front = client->writeQueue_.front();
            if (static_cast<size_t>(written) < front.size()) front.erase(0, static_cast<size_t>(written));
            else client->writeQueue_.pop_front();
            client->writeNext();
        },
        cancellable_);
}

void LspClient::readNext() {
    std::weak_ptr<LspClient*> weak = self_;
    auto stream = output_;
    stream->read_bytes_async(
        kReadChunk,
        [weak, stream](const Glib::RefPtr<Gio::AsyncResult>& result) {
            Glib::RefPtr<Glib::Bytes> bytes;
            try {
                bytes = stream->read_bytes_finish(result);
            } catch (const Glib::Error&) {
            }
            auto self = weak.lock();
            if (!self) return;
            LspClient* client = *self;
            gsize size = 0;
            const auto* data = bytes ? static_cast<const char*>(bytes->get_data(size)) : nullptr;
            if (!data || size == 0) return; // EOF; the exit handler reports it
            client->framer_.feed(std::string_view(data, size));
            try {
                while (auto message = client->framer_.next()) {
                    client->dispatch(*message);
                    if (!weak.lock()) return; // a callback may have destroyed the client
                }
            } catch (const std::exception& e) {
                client->onExited(std::string("sent invalid data: ") + e.what());
                return;
            }
            client->readNext();
        },
        cancellable_);
}

void LspClient::dispatch(const json& message) {
    if (message.contains("id") && !message.contains("method")) {
        if (!message["id"].is_number_integer()) return;
        auto it = pending_.find(message["id"].get<int>());
        if (it == pending_.end()) return;
        Callback callback = std::move(it->second);
        pending_.erase(it);
        if (message.contains("error")) callback(json(), &message["error"]);
        else callback(message.value("result", json()), nullptr);
        return;
    }
    if (message.contains("id")) {
        answerServerRequest(message);
        return;
    }
    const std::string method = message.value("method", "");
    if (method == "textDocument/publishDiagnostics") {
        const json& params = message["params"];
        diagnostics_.emit(params.value("uri", ""), params.value("diagnostics", json::array()));
    }
}

void LspClient::answerServerRequest(const json& message) {
    const std::string method = message.value("method", "");
    json reply = {{"jsonrpc", "2.0"}, {"id", message["id"]}};
    if (method == "workspace/configuration") {
        const auto items = message["params"].value("items", json::array());
        reply["result"] = json::array();
        for (const auto& item : items)
            reply["result"].push_back(configuration_ ? configuration_(item.value("section", "")) : json(nullptr));
    } else if (method == "window/workDoneProgress/create" || method == "client/registerCapability" ||
               method == "client/unregisterCapability" || method == "window/showMessageRequest" ||
               method == "workspace/workspaceFolders") {
        reply["result"] = nullptr;
    } else {
        reply["error"] = {{"code", -32601}, {"message", "Method not supported by Kev-C++: " + method}};
    }
    send(reply);
}

void LspClient::onExited(const std::string& reason) {
    if (!process_) return;
    ready_ = false;
    process_.reset();
    input_.reset();
    output_.reset();
    writeQueue_.clear();
    writing_ = false;
    // Fail outstanding requests so callers aren't left waiting.
    auto pending = std::move(pending_);
    pending_.clear();
    const json error = {{"code", -32099}, {"message", name_ + " " + reason}};
    for (auto& [id, callback] : pending) callback(json(), &error);
    exited_.emit(reason);
}

} // namespace kevc
