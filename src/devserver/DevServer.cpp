#include "devserver/DevServer.h"

#include "core/Diagnostics.h"

namespace kevc {

DevServer::DevServer() {
    process_.signalLine().connect([this](const std::string& raw) {
        const std::string line = stripAnsi(raw); // Vite colours its output when FORCE_COLOR is set
        if (url_.empty() && readyParser_) {
            if (std::string url = readyParser_(line); !url.empty()) {
                url_ = std::move(url);
                ready_.emit(url_);
                // The bundled server's marker is for us only; Vite's banner line is worth showing.
                if (line.starts_with("KEVC_DEVSERVER_READY ")) return;
            }
        }
        log_.emit(line);
    });
    process_.signalExited().connect([this](int status) {
        url_.clear();
        stopped_.emit(status);
    });
}

std::vector<std::string> DevServer::bundledCommand(const std::string& node, const std::filesystem::path& script,
                                                   const std::filesystem::path& root, int port) {
    return {node, script.string(), "--root", root.string(), "--port", std::to_string(port)};
}

DevServer::ReadyParser DevServer::bundledReady() {
    return [](const std::string& line) -> std::string {
        constexpr std::string_view marker = "KEVC_DEVSERVER_READY ";
        return line.starts_with(marker) ? line.substr(marker.size()) : std::string{};
    };
}

void DevServer::start(const std::vector<std::string>& argv, const std::filesystem::path& root, ReadyParser ready,
                      std::string label, const std::string& pathPrepend,
                      const std::vector<std::pair<std::string, std::string>>& env) {
    if (process_.running()) return;
    root_ = root;
    url_.clear();
    label_ = std::move(label);
    readyParser_ = std::move(ready);
    process_.start({argv, root, env, pathPrepend});
}

void DevServer::stop() {
    process_.stop();
}

} // namespace kevc
