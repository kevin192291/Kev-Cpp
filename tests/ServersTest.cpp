#include "core/Servers.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace kevc::servers;

namespace {

const char* kHeader =
    "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n";

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / ("kevc-servers-" + std::to_string(::getpid()) + "-" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void write(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

// A process in a made-up /proc: its command line, folder, state and the sockets it has open.
void fakeProcess(const fs::path& proc, int pid, const std::vector<std::string>& argv, const std::string& cwd,
                 char state, const std::vector<unsigned long>& sockets) {
    const fs::path dir = proc / std::to_string(pid);
    std::string cmdline;
    for (const auto& a : argv) cmdline += a + '\0';
    write(dir / "cmdline", cmdline);
    write(dir / "stat", std::to_string(pid) + " (" + fs::path(argv[0]).filename().string() + ") " + state + " 1 1 1\n");
    fs::create_symlink(cwd, dir / "cwd");
    fs::create_directories(dir / "fd");
    int fd = 3;
    for (unsigned long inode : sockets) fs::create_symlink("socket:[" + std::to_string(inode) + "]", dir / "fd" / std::to_string(fd++));
    fs::create_symlink("/dev/null", dir / "fd" / "0");
}

// A server on 127.0.0.1 that answers each connection with `reply` (nothing: it says nothing), and
// remembers the last request. Listening only (no accept) when `accept` is false.
class TestServer {
public:
    explicit TestServer(std::string reply, bool accept = true) : reply_(std::move(reply)) {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(fd_, 4) != 0)
            ADD_FAILURE() << "no test server: " << std::strerror(errno);
        socklen_t size = sizeof a;
        ::getsockname(fd_, reinterpret_cast<sockaddr*>(&a), &size);
        port_ = ntohs(a.sin_port);
        if (accept)
            thread_ = std::thread([this] {
                const int client = ::accept(fd_, nullptr, nullptr);
                if (client < 0) return;
                char buffer[1024];
                const ssize_t n = ::recv(client, buffer, sizeof buffer, 0);
                if (n > 0) request_.assign(buffer, static_cast<size_t>(n));
                if (!reply_.empty()) ::send(client, reply_.data(), reply_.size(), MSG_NOSIGNAL);
                else std::this_thread::sleep_for(std::chrono::milliseconds(600)); // silent
                ::close(client);
            });
    }
    ~TestServer() {
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        if (thread_.joinable()) thread_.join();
    }
    int port() const { return port_; }
    const std::string& request() const { return request_; }

private:
    std::string reply_;
    int fd_ = -1;
    int port_ = 0;
    std::thread thread_;
    std::string request_;
};

} // namespace

TEST(Servers, ParsesListeningSocketsFromProcNetTcp) {
    const std::string tcp = std::string(kHeader) +
        "   0: 0100007F:1435 00000000:0000 0A 00000000:00000000 00:00000000 00000000  1000        0 111 1 0 100 0 0 10 0\n"
        "   1: 00000000:0016 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 222 1 0 100 0 0 10 0\n"
        "   2: 0100007F:1435 0100007F:C350 01 00000000:00000000 00:00000000 00000000  1000        0 333 1 0 20 4 30 10 -1\n";
    const auto sockets = parseProcNetTcp(tcp, false);
    ASSERT_EQ(sockets.size(), 2u); // the connection (st 01) isn't listening
    EXPECT_EQ(sockets[0].address, "127.0.0.1");
    EXPECT_EQ(sockets[0].port, 5173);
    EXPECT_EQ(sockets[0].uid, 1000u);
    EXPECT_EQ(sockets[0].inode, 111u);
    EXPECT_EQ(sockets[1].address, "0.0.0.0");
    EXPECT_EQ(sockets[1].port, 22);

    const std::string tcp6 = std::string(kHeader) +
        "   0: 00000000000000000000000001000000:1435 00000000000000000000000000000000:0000 0A 00000000:00000000 00:00000000 00000000  1000        0 444 1 0 100 0 0 10 0\n"
        "   1: 00000000000000000000000000000000:1F90 00000000000000000000000000000000:0000 0A 00000000:00000000 00:00000000 00000000  1000        0 555 1 0 100 0 0 10 0\n"
        "   2: 0000000000000000FFFF00000100007F:0050 00000000000000000000000000000000:0000 0A 00000000:00000000 00:00000000 00000000  1000        0 666 1 0 100 0 0 10 0\n";
    const auto six = parseProcNetTcp(tcp6, true);
    ASSERT_EQ(six.size(), 3u);
    EXPECT_EQ(six[0].address, "::1");
    EXPECT_EQ(six[0].port, 5173);
    EXPECT_EQ(six[1].address, "::");
    EXPECT_EQ(six[1].port, 8080);
    EXPECT_EQ(six[2].address, "::ffff:127.0.0.1");
    EXPECT_TRUE(parseProcNetTcp("", false).empty());
    EXPECT_TRUE(parseProcNetTcp(std::string(kHeader) + "garbage\n", false).empty());
}

TEST(Servers, ListsTheUsersServersWithTheirPrograms) {
    const fs::path proc = scratch("proc");
    write(proc / "net" / "tcp", std::string(kHeader) +
        "   0: 0100007F:1435 00000000:0000 0A 00000000:00000000 00:00000000 00000000  1000        0 111 1 0 100 0 0 10 0\n"
        "   1: 00000000:0016 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 222 1 0 100 0 0 10 0\n"
        "   2: 0100007F:10E1 00000000:0000 0A 00000000:00000000 00:00000000 00000000  1000        0 999 1 0 100 0 0 10 0\n");
    write(proc / "net" / "tcp6", std::string(kHeader) +
        "   0: 00000000000000000000000001000000:1435 00000000000000000000000000000000:0000 0A 00000000:00000000 00:00000000 00000000  1000        0 444 1 0 100 0 0 10 0\n"
        "   1: 00000000000000000000000001000000:10E1 00000000000000000000000000000000:0000 0A 00000000:00000000 00:00000000 00000000  1000        0 998 1 0 100 0 0 10 0\n");
    // A Vite on 127.0.0.1:5173, an older one left suspended on [::1]:5173, and one with both sockets on 4321.
    fakeProcess(proc, 300, {"/usr/bin/node", "/app/node_modules/vite/bin/vite.js", "dev"}, "/home/u/app", 'S', {111});
    fakeProcess(proc, 200, {"node", "/old/node_modules/vite/bin/vite.js", "dev"}, "/home/u/old", 'T', {444});
    fakeProcess(proc, 400, {"python3", "-m", "http.server", "4321"}, "/srv", 'S', {999, 998});
    write(proc / "self", ""); // not a process

    const auto servers = listening(1000, proc);
    ASSERT_EQ(servers.size(), 3u) << "port 22 is root's";
    EXPECT_EQ(servers[0].port, 4321);
    EXPECT_EQ(servers[0].pids, std::vector<int>{400});
    EXPECT_EQ(servers[0].addresses, (std::vector<std::string>{"127.0.0.1", "::1"}));
    EXPECT_EQ(summary(servers[0].argv), "http.server 4321");

    EXPECT_EQ(servers[1].port, 5173);
    EXPECT_EQ(servers[1].pids, std::vector<int>{200});
    EXPECT_EQ(servers[1].addresses, std::vector<std::string>{"::1"});
    EXPECT_EQ(servers[1].state, 'T');
    EXPECT_EQ(servers[1].cwd, "/home/u/old");
    EXPECT_EQ(servers[2].port, 5173);
    EXPECT_EQ(servers[2].pids, std::vector<int>{300});
    EXPECT_EQ(servers[2].state, 'S');
    EXPECT_EQ(summary(servers[2].argv), "vite dev");

    EXPECT_TRUE(holdsSocket(300, 111, proc));
    EXPECT_FALSE(holdsSocket(300, 444, proc));
    EXPECT_FALSE(holdsSocket(12345, 111, proc));
    EXPECT_EQ(processState(12345, proc), 0);
    fs::remove_all(proc);
}

TEST(Servers, ASocketNoProcessIsSeenHoldingHasNoProgram) {
    const fs::path proc = scratch("orphan");
    write(proc / "net" / "tcp", std::string(kHeader) +
        "   0: 00000000:1F90 00000000:0000 0A 00000000:00000000 00:00000000 00000000  1000        0 777 1 0 100 0 0 10 0\n");
    const auto servers = listening(1000, proc);
    ASSERT_EQ(servers.size(), 1u);
    EXPECT_EQ(servers[0].port, 8080);
    EXPECT_TRUE(servers[0].pids.empty());
    EXPECT_TRUE(servers[0].argv.empty());
    fs::remove_all(proc);
}

TEST(Servers, FindsThisProcesssOwnListeningSocket) {
    TestServer server("HTTP/1.1 200 OK\r\n\r\n", false);
    const auto servers = listening(::getuid());
    const auto it = std::find_if(servers.begin(), servers.end(), [&](const Server& s) { return s.port == server.port(); });
    ASSERT_NE(it, servers.end());
    EXPECT_EQ(it->pids, std::vector<int>{::getpid()});
    EXPECT_EQ(it->addresses, std::vector<std::string>{"127.0.0.1"});
    EXPECT_EQ(it->cwd, fs::current_path());
    EXPECT_TRUE(holdsSocket(::getpid(), it->inodes.front()));
    EXPECT_NE(processState(::getpid()), 0);
}

TEST(Servers, SummariesNameTheScriptARuntimeRuns) {
    EXPECT_EQ(summary({"/home/u/.nvm/versions/node/v24/bin/node", "/p/node_modules/vite/bin/vite.js", "dev", "--strictPort",
                       "--host", "127.0.0.1"}),
              "vite dev --strictPort --host 127.0.0.1");
    EXPECT_EQ(summary({"node", "/p/node_modules/.pnpm/vite@7.3.1/node_modules/vite/bin/vite.js", "preview"}), "vite preview");
    EXPECT_EQ(summary({"node", "--inspect", "/p/node_modules/@sveltejs/kit/src/cli.js", "sync"}), "@sveltejs/kit sync");
    EXPECT_EQ(summary({"node", "/home/u/.local/share/kev-cpp/tools/otel/collector.mjs", "--port", "4318"}),
              "collector --port 4318");
    EXPECT_EQ(summary({"node", "server.js"}), "server");
    EXPECT_EQ(summary({"/usr/bin/python3.12", "-m", "http.server", "8000"}), "http.server 8000");
    EXPECT_EQ(summary({"python3", "manage.py", "runserver"}), "manage runserver");
    EXPECT_EQ(summary({"deno", "run", "--allow-net", "main.ts"}), "main");
    EXPECT_EQ(summary({"bun", "run", "dev"}), "dev");
    EXPECT_EQ(summary({"/usr/sbin/nginx", "-g", "daemon off;"}), "nginx -g daemon off;");
    EXPECT_EQ(summary({"/opt/discord/Discord"}), "Discord");
    EXPECT_EQ(summary({"node"}), "node");
    // Electron writes its whole command line into one string.
    EXPECT_EQ(summary({"/app/discord/Discord --type=renderer --app-path=/app/discord/resources/app.asar"}),
              "Discord --type=renderer --app-path=/app/discord/resources/app.asar");

    EXPECT_TRUE(devRuntime({"/home/u/.nvm/versions/node/v24/bin/node", "vite.js"}));
    EXPECT_TRUE(devRuntime({"python3.12", "-m", "http.server"}));
    EXPECT_TRUE(devRuntime({"bun run dev"}));
    EXPECT_FALSE(devRuntime({"/opt/synology/cloud-drive-connect", "--log_folder", "log"}));
    EXPECT_FALSE(devRuntime({"/app/discord/Discord --type=renderer"}));
    EXPECT_FALSE(devRuntime({}));
    EXPECT_EQ(summary({}), "");

    EXPECT_EQ(kevcHelper({"node", "/x/tools/otel/collector.mjs", "--port", "4318"}), "Kev-C++'s traces collector");
    EXPECT_NE(kevcHelper({"node", "/x/tools/js-debug/src/dapDebugServer.js", "40123"}), "");
    EXPECT_EQ(kevcHelper({"node", "/p/node_modules/vite/bin/vite.js"}), "");
}

TEST(Servers, StateReadsPastACommandNameWithBrackets) {
    const fs::path proc = scratch("stat");
    write(proc / "42" / "stat", "42 (a) b (c)) T 1 42 42 0 -1\n");
    EXPECT_EQ(processState(42, proc), 'T');
    write(proc / "43" / "stat", "43 (oops");
    EXPECT_EQ(processState(43, proc), 0);
    fs::remove_all(proc);
}

TEST(Servers, ReachableAtTheLoopbackForTheWildcards) {
    EXPECT_EQ(reachableAt("0.0.0.0"), "127.0.0.1");
    EXPECT_EQ(reachableAt("::"), "::1");
    EXPECT_EQ(reachableAt("192.168.1.5"), "192.168.1.5");
}

TEST(Servers, ProbesSayWhetherAServerAnswers) {
    {
        TestServer server("HTTP/1.1 200 OK\r\nContent-Type: text/javascript\r\n\r\nexport {}");
        const Probe p = probe("127.0.0.1", server.port(), 2000);
        EXPECT_EQ(p.answer, Answer::Http);
        EXPECT_EQ(p.status, 200);
        EXPECT_EQ(p.statusLine, "HTTP/1.1 200 OK");
    }
    {
        TestServer server("HTTP/1.1 404 Not Found\r\n\r\n");
        const Probe p = probe("127.0.0.1", server.port(), 2000);
        EXPECT_EQ(p.answer, Answer::Http); // answering at all is what counts
        EXPECT_EQ(p.status, 404);
    }
    {
        TestServer server("HTTP/1.1 503 Service Unavailable\r\n\r\n");
        EXPECT_EQ(probe("127.0.0.1", server.port(), 2000).answer, Answer::HttpError);
    }
    {
        TestServer server("SSH-2.0-OpenSSH_9.6\r\n");
        EXPECT_EQ(probe("127.0.0.1", server.port(), 2000).answer, Answer::NotHttp);
    }
    {
        TestServer server(""); // takes the connection, says nothing
        const auto start = std::chrono::steady_clock::now();
        EXPECT_EQ(probe("127.0.0.1", server.port(), 300).answer, Answer::Silent);
        EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(1500));
    }
    {
        TestServer server("HTTP/1.1 200 OK\r\n\r\n", false); // listening, never accepting: suspended
        EXPECT_EQ(probe("127.0.0.1", server.port(), 300).answer, Answer::Silent);
    }
}

TEST(Servers, ProbesAskForVitesOwnClientWithLocalhostAsTheHost) {
    TestServer server("HTTP/1.1 200 OK\r\n\r\n");
    probe("127.0.0.1", server.port(), 2000);
    // Vite checks the Host header (localhost is always allowed) and serves this path without the app.
    EXPECT_TRUE(server.request().starts_with("GET /@vite/client HTTP/1.1\r\n")) << server.request();
    EXPECT_NE(server.request().find("\r\nHost: localhost:" + std::to_string(server.port()) + "\r\n"), std::string::npos);
}

TEST(Servers, ProbesOfNothingAndCancelledProbes) {
    int port = 0;
    {
        TestServer gone("", false);
        port = gone.port();
    }
    EXPECT_EQ(probe("127.0.0.1", port, 1000).answer, Answer::Refused);
    EXPECT_EQ(probe("not an address", 80, 1000).answer, Answer::Refused);

    TestServer silent("");
    std::atomic<bool> cancelled{true};
    const auto start = std::chrono::steady_clock::now();
    probe("127.0.0.1", silent.port(), 5000, &cancelled);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(500));
}
