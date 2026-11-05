#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace kevc::servers {

// Tools → Running Servers' GTK-free half: which of the user's programs listen on which TCP port (from
// /proc), what they are in a few words, and whether they answer as a web server should.

// A TCP socket in the LISTEN state, from a line of /proc/net/tcp or /proc/net/tcp6.
struct Socket {
    std::string address; // "127.0.0.1", "::1", "0.0.0.0" (every address)
    int port = 0;
    unsigned uid = 0;
    unsigned long inode = 0;
};
// The listening sockets in /proc/net/tcp's (ipv6: tcp6's) text. Addresses are read in this machine's
// byte order, as the kernel wrote them.
std::vector<Socket> parseProcNetTcp(std::string_view text, bool ipv6);

// A program listening on a port. Its IPv4 and IPv6 sockets on that port are one server; two programs
// on one port (an old Vite on ::1:5173, a new one on 127.0.0.1:5173) are two.
struct Server {
    int port = 0;
    std::vector<std::string> addresses;
    std::vector<unsigned long> inodes;
    std::vector<int> pids;         // the processes holding a socket (workers can share one); none if unknown
    std::vector<std::string> argv; // the first process's command line
    std::filesystem::path cwd;     // and the folder it runs in
    char state = 0;                // its state in /proc/<pid>/stat: 'T' when suspended (Ctrl+Z)
};
// The ports processes of `uid` are listening on, lowest first. `procRoot` is for tests.
std::vector<Server> listening(unsigned uid, const std::filesystem::path& procRoot = "/proc");

// A command line in a few words: the script a runtime (node, bun, deno, python…) runs and its
// arguments ("vite dev --port 5173", the package's name for a script in node_modules), or the
// program's own name and arguments.
std::string summary(const std::vector<std::string>& argv);
// Whether a runtime (node, bun, deno, python…) runs it, as it does dev servers: one of those that
// takes a connection and says nothing is stuck, while another program may just not speak HTTP.
bool devRuntime(const std::vector<std::string>& argv);
// What Kev-C++'s own helpers are ("Kev-C++'s traces collector"); empty for anything else.
std::string kevcHelper(const std::vector<std::string>& argv);

// Where to connect to a server listening on `address`: the wildcard addresses mean this machine too.
std::string reachableAt(const std::string& address);

// The state letter of /proc/<pid>/stat ('R', 'S', 'T', 'Z'…), or 0 if there's no such process.
char processState(int pid, const std::filesystem::path& procRoot = "/proc");
// Whether a process still holds the socket with this inode (so a PID reused since isn't signalled).
bool holdsSocket(int pid, unsigned long inode, const std::filesystem::path& procRoot = "/proc");

// What a server said when asked for a page.
enum class Answer {
    Http,      // an HTTP status below 500
    HttpError, // 500 and up: it's there, but failing
    NotHttp,   // it answered with something else (a debugger's protocol, a database's)
    Silent,    // it took the connection and said nothing in time: stuck, or very busy
    Refused,   // nothing took the connection
};
struct Probe {
    Answer answer = Answer::Refused;
    int status = 0;          // the HTTP status
    std::string statusLine;  // "HTTP/1.1 200 OK"
};
// The path a probe asks for: Vite serves it itself, without running the app's routes and load
// functions; any other web server answers it too, if only with a 404.
constexpr const char* kProbePath = "/@vite/client";
// Connects to address:port and asks for kProbePath, waiting up to timeoutMs for an answer to start.
// Stops early (as Refused) once `cancelled` is set.
Probe probe(const std::string& address, int port, int timeoutMs, const std::atomic<bool>* cancelled = nullptr);

} // namespace kevc::servers
