#include "core/Servers.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <netdb.h>
#include <poll.h>
#include <set>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace kevc::servers {

namespace {

constexpr const char* kListen = "0A"; // TCP_LISTEN in /proc/net/tcp's st column

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

bool allDigits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
}

// The kernel prints each 32-bit word of an address as a number, in this machine's byte order.
bool parseAddress(const std::string& hex, bool ipv6, std::string& out) {
    char text[INET6_ADDRSTRLEN] = {};
    if (!ipv6) {
        if (hex.size() != 8) return false;
        in_addr a{};
        a.s_addr = static_cast<uint32_t>(std::stoul(hex, nullptr, 16));
        if (!inet_ntop(AF_INET, &a, text, sizeof text)) return false;
    } else {
        if (hex.size() != 32) return false;
        in6_addr a{};
        for (int i = 0; i < 4; ++i) {
            const auto word = static_cast<uint32_t>(std::stoul(hex.substr(size_t(i) * 8, 8), nullptr, 16));
            std::memcpy(a.s6_addr + i * 4, &word, 4);
        }
        if (!inet_ntop(AF_INET6, &a, text, sizeof text)) return false;
    }
    out = text;
    return true;
}

std::vector<std::string> cmdline(const fs::path& file) {
    std::vector<std::string> argv;
    const std::string text = readFile(file);
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\0', start);
        if (end == std::string::npos) end = text.size();
        argv.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return argv;
}

std::string baseName(const std::string& path) {
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool isRuntime(const std::string& exe) {
    static const std::set<std::string> runtimes{"node", "nodejs", "bun", "deno", "ruby", "php", "perl", "tsx", "ts-node"};
    return runtimes.count(exe) || exe.rfind("python", 0) == 0;
}

// A script's name: its package for one in node_modules (pnpm's nested ones too), else its file name.
std::string scriptName(const std::string& script) {
    if (const size_t at = script.rfind("/node_modules/"); at != std::string::npos) {
        const std::string rest = script.substr(at + 14);
        const size_t slash = rest.find('/');
        if (rest.starts_with('@')) { // @scope/package
            const size_t second = rest.find('/', slash + 1);
            return rest.substr(0, second);
        }
        return rest.substr(0, slash);
    }
    std::string name = baseName(script);
    for (const char* ext : {".js", ".mjs", ".cjs", ".ts", ".mts", ".py"})
        if (name.size() > std::strlen(ext) && name.ends_with(ext)) return name.substr(0, name.size() - std::strlen(ext));
    return name;
}

// Electron and Chromium rewrite their command line into one string: the program is its first word.
std::vector<std::string> words(const std::vector<std::string>& argv) {
    if (argv.size() != 1) return argv;
    const size_t space = argv[0].find(' ');
    if (space == std::string::npos) return argv;
    return {argv[0].substr(0, space), argv[0].substr(space + 1)};
}

std::string join(const std::vector<std::string>& parts, size_t from) {
    std::string out;
    for (size_t i = from; i < parts.size(); ++i) out += (out.empty() ? "" : " ") + parts[i];
    return out;
}

} // namespace

std::vector<Socket> parseProcNetTcp(std::string_view text, bool ipv6) {
    std::vector<Socket> out;
    std::istringstream lines{std::string(text)};
    std::string line;
    std::getline(lines, line); // the column names
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        std::string slot, local, remote, state, queues, timer, retransmits, uid, timeout, inode;
        if (!(fields >> slot >> local >> remote >> state >> queues >> timer >> retransmits >> uid >> timeout >> inode))
            continue;
        if (state != kListen) continue;
        const size_t colon = local.find(':');
        if (colon == std::string::npos) continue;
        Socket s;
        try {
            if (!parseAddress(local.substr(0, colon), ipv6, s.address)) continue;
            s.port = std::stoi(local.substr(colon + 1), nullptr, 16);
            s.uid = static_cast<unsigned>(std::stoul(uid));
            s.inode = std::stoul(inode);
        } catch (const std::exception&) {
            continue;
        }
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<Server> listening(unsigned uid, const fs::path& procRoot) {
    std::vector<Socket> sockets = parseProcNetTcp(readFile(procRoot / "net" / "tcp"), false);
    for (auto& s : parseProcNetTcp(readFile(procRoot / "net" / "tcp6"), true)) sockets.push_back(std::move(s));
    std::erase_if(sockets, [uid](const Socket& s) { return s.uid != uid; });
    if (sockets.empty()) return {};

    // Which processes hold them: their open files that are these sockets.
    std::map<unsigned long, std::vector<int>> owners;
    for (const auto& s : sockets) owners[s.inode];
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(procRoot, ec)) {
        const std::string name = entry.path().filename().string();
        if (!allDigits(name)) continue;
        std::error_code fdError;
        for (const auto& fd : fs::directory_iterator(entry.path() / "fd", fdError)) {
            std::error_code linkError;
            const std::string target = fs::read_symlink(fd.path(), linkError).string();
            if (linkError || !target.starts_with("socket:[")) continue;
            try {
                if (auto it = owners.find(std::stoul(target.substr(8))); it != owners.end())
                    it->second.push_back(std::stoi(name));
            } catch (const std::exception&) {
            }
        }
    }

    // One server per port and set of processes.
    std::map<std::pair<int, std::vector<int>>, Server> byOwner;
    for (const auto& s : sockets) {
        std::vector<int> pids = owners[s.inode];
        std::sort(pids.begin(), pids.end());
        pids.erase(std::unique(pids.begin(), pids.end()), pids.end());
        Server& server = byOwner[{s.port, pids}];
        server.port = s.port;
        server.pids = pids;
        if (std::find(server.addresses.begin(), server.addresses.end(), s.address) == server.addresses.end())
            server.addresses.push_back(s.address);
        server.inodes.push_back(s.inode);
    }
    std::vector<Server> out;
    for (auto& [key, server] : byOwner) {
        if (!server.pids.empty()) {
            const fs::path dir = procRoot / std::to_string(server.pids.front());
            server.argv = cmdline(dir / "cmdline");
            std::error_code cwdError;
            server.cwd = fs::read_symlink(dir / "cwd", cwdError);
            server.state = processState(server.pids.front(), procRoot);
        }
        out.push_back(std::move(server));
    }
    return out; // the map kept them in port order
}

std::string summary(const std::vector<std::string>& commandLine) {
    const std::vector<std::string> argv = words(commandLine);
    if (argv.empty()) return {};
    const std::string exe = baseName(argv[0]);
    if (!isRuntime(exe)) return argv.size() > 1 ? exe + " " + join(argv, 1) : exe;
    for (size_t i = 1; i < argv.size(); ++i) {
        const std::string& arg = argv[i];
        if (arg == "-m" && i + 1 < argv.size()) return join(argv, i + 1); // python -m http.server 8000
        if (arg == "run" && (exe == "deno" || exe == "bun")) continue;
        if (arg.starts_with('-')) continue; // the runtime's own options
        const std::string rest = join(argv, i + 1);
        return scriptName(arg) + (rest.empty() ? "" : " " + rest);
    }
    return join(argv, 0);
}

bool devRuntime(const std::vector<std::string>& argv) {
    return !argv.empty() && isRuntime(baseName(words(argv)[0]));
}

std::string kevcHelper(const std::vector<std::string>& argv) {
    for (const auto& arg : argv) {
        if (arg.ends_with("/dapDebugServer.js")) return "Kev-C++'s JavaScript debugger (js-debug)";
        if (arg.ends_with("/otel/collector.mjs")) return "Kev-C++'s traces collector";
        if (arg.ends_with("/devserver/devserver.mjs")) return "Kev-C++'s bundled dev server";
    }
    return {};
}

std::string reachableAt(const std::string& address) {
    if (address == "0.0.0.0") return "127.0.0.1";
    if (address == "::") return "::1";
    return address;
}

char processState(int pid, const fs::path& procRoot) {
    // "pid (comm) S …": the command may hold spaces and brackets, so the state follows the last ')'.
    const std::string stat = readFile(procRoot / std::to_string(pid) / "stat");
    const size_t close = stat.rfind(')');
    if (close == std::string::npos || close + 2 >= stat.size()) return 0;
    return stat[close + 2];
}

bool holdsSocket(int pid, unsigned long inode, const fs::path& procRoot) {
    const std::string wanted = "socket:[" + std::to_string(inode) + "]";
    std::error_code ec;
    for (const auto& fd : fs::directory_iterator(procRoot / std::to_string(pid) / "fd", ec)) {
        std::error_code linkError;
        if (fs::read_symlink(fd.path(), linkError).string() == wanted && !linkError) return true;
    }
    return false;
}

Probe probe(const std::string& address, int port, int timeoutMs, const std::atomic<bool>* cancelled) {
    Probe result;
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    addrinfo* found = nullptr;
    if (getaddrinfo(address.c_str(), std::to_string(port).c_str(), &hints, &found) != 0 || !found) return result;
    const int fd = ::socket(found->ai_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        freeaddrinfo(found);
        return result;
    }
    const int connected = ::connect(fd, found->ai_addr, found->ai_addrlen);
    freeaddrinfo(found);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    // Waits for `events` in slices, so a cancelled probe stops within a tenth of a second.
    auto wait = [&](short events) {
        for (;;) {
            if (cancelled && *cancelled) return false;
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (left.count() <= 0) return false;
            pollfd p{fd, events, 0};
            const int n = ::poll(&p, 1, static_cast<int>(std::min<long long>(left.count(), 100)));
            if (n > 0) return true;
            if (n < 0 && errno != EINTR) return false;
        }
    };
    if (connected != 0) {
        if (errno != EINPROGRESS || !wait(POLLOUT)) {
            ::close(fd);
            return result; // refused, or nothing took it in time
        }
        int error = 0;
        socklen_t size = sizeof error;
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) != 0 || error != 0) {
            ::close(fd);
            return result;
        }
    }
    const std::string request = std::string("GET ") + kProbePath + " HTTP/1.1\r\nHost: localhost:" +
                                std::to_string(port) + "\r\nUser-Agent: Kev-C++\r\nConnection: close\r\n\r\n";
    if (::send(fd, request.data(), request.size(), MSG_NOSIGNAL) < 0) {
        ::close(fd);
        result.answer = Answer::Silent;
        return result;
    }
    std::string reply;
    char buffer[512];
    result.answer = Answer::Silent;
    while (reply.find('\n') == std::string::npos && reply.size() < 4096 && wait(POLLIN)) {
        const ssize_t n = ::recv(fd, buffer, sizeof buffer, 0);
        if (n <= 0) break; // closed (or failed): what came before is the answer
        reply.append(buffer, static_cast<size_t>(n));
    }
    ::close(fd);
    if (reply.empty()) return result;
    std::string line = reply.substr(0, reply.find('\n'));
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.starts_with("HTTP/")) {
        result.answer = Answer::NotHttp;
        return result;
    }
    result.statusLine = line;
    const size_t space = line.find(' ');
    try {
        result.status = space == std::string::npos ? 0 : std::stoi(line.substr(space + 1));
    } catch (const std::exception&) {
        result.status = 0;
    }
    result.answer = result.status >= 500 ? Answer::HttpError : Answer::Http;
    return result;
}

} // namespace kevc::servers
