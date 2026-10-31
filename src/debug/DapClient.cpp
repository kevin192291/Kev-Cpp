#include "debug/DapClient.h"

namespace kevc {

DapClient::DapClient() : self_(std::make_shared<DapClient*>(this)) {}

DapClient::~DapClient() {
    self_.reset();
    cancellable_->cancel();
    if (connection_) {
        try {
            connection_->close();
        } catch (const Glib::Error&) {
        }
    }
}

void DapClient::connect(int port) {
    auto client = Gio::SocketClient::create();
    std::weak_ptr<DapClient*> weak = self_;
    client->connect_to_host_async(
        "127.0.0.1", static_cast<guint16>(port), cancellable_,
        [weak, client](const Glib::RefPtr<Gio::AsyncResult>& result) {
            Glib::RefPtr<Gio::SocketConnection> connection;
            std::string error;
            try {
                connection = client->connect_to_host_finish(result);
            } catch (const Glib::Error& e) {
                error = e.what();
            }
            auto self = weak.lock();
            if (!self) return;
            DapClient* c = *self;
            if (!connection) {
                c->closed_.emit("could not connect to the debug adapter: " + error);
                return;
            }
            c->connection_ = connection;
            c->readNext();
            c->connected_.emit();
        });
}

void DapClient::send(const json& message) {
    if (!connection_) return;
    const std::string bytes = lsp::frame(message);
    try {
        gsize written = 0;
        // Loopback and small messages: a blocking write doesn't stall the UI in practice.
        connection_->get_output_stream()->write_all(bytes.data(), bytes.size(), written);
    } catch (const Glib::Error& e) {
        close(std::string("write failed: ") + e.what());
    }
}

void DapClient::request(const std::string& command, json arguments, Callback callback) {
    const int seq = ++seq_;
    if (callback) pending_[seq] = std::move(callback);
    send({{"seq", seq}, {"type", "request"}, {"command", command}, {"arguments", std::move(arguments)}});
}

void DapClient::respond(int requestSeq, const std::string& command, bool success, json body) {
    send({{"seq", ++seq_},
          {"type", "response"},
          {"request_seq", requestSeq},
          {"command", command},
          {"success", success},
          {"body", std::move(body)}});
}

void DapClient::readNext() {
    if (!connection_) return;
    std::weak_ptr<DapClient*> weak = self_;
    auto input = connection_->get_input_stream();
    input->read_bytes_async(
        64 * 1024,
        [weak, input](const Glib::RefPtr<Gio::AsyncResult>& result) {
            Glib::RefPtr<Glib::Bytes> bytes;
            std::string error;
            try {
                bytes = input->read_bytes_finish(result);
            } catch (const Glib::Error& e) {
                error = e.what();
            }
            auto self = weak.lock();
            if (!self) return;
            DapClient* c = *self;
            gsize size = 0;
            const auto* data = bytes ? static_cast<const char*>(bytes->get_data(size)) : nullptr;
            if (!data || size == 0) {
                c->close(error.empty() ? "the debug adapter closed the connection" : error);
                return;
            }
            c->framer_.feed(std::string_view(data, size));
            while (true) {
                std::optional<json> message;
                try {
                    message = c->framer_.next();
                } catch (const std::exception& e) {
                    c->close(std::string("bad message from the debug adapter: ") + e.what());
                    return;
                }
                if (!message) break;
                const std::string type = message->value("type", "");
                if (type == "response") {
                    auto it = c->pending_.find(message->value("request_seq", -1));
                    if (it == c->pending_.end()) continue;
                    auto callback = std::move(it->second);
                    c->pending_.erase(it);
                    callback(message->value("success", false), message->value("body", json::object()),
                             message->value("message", ""));
                } else if (type == "event") {
                    c->event_.emit(message->value("event", ""), message->value("body", json::object()));
                } else if (type == "request") {
                    c->reverse_.emit(message->value("seq", 0), message->value("command", ""),
                                     message->value("arguments", json::object()));
                }
                if (!weak.lock()) return; // a handler destroyed the client
            }
            c->readNext();
        },
        cancellable_);
}

void DapClient::close(const std::string& reason) {
    if (!connection_) return;
    connection_.reset();
    for (auto& [seq, callback] : pending_) callback(false, json::object(), reason);
    pending_.clear();
    closed_.emit(reason);
}

} // namespace kevc
