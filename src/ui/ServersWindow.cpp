#include "ui/ServersWindow.h"

#include "core/ProcessTree.h"
#include "ui/Dialogs.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <set>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

namespace kevc {

namespace {

constexpr int kProbeMs = 3000;
constexpr auto kGrace = std::chrono::seconds(3); // after SIGTERM, before SIGKILL
constexpr auto kGiveUp = std::chrono::seconds(6);

bool gone(int pid) {
    const char state = servers::processState(pid);
    return state == 0 || state == 'Z' || state == 'X'; // a zombie has closed its sockets
}

std::string homeShort(const fs::path& path) {
    const std::string home = Glib::get_home_dir();
    const std::string p = path.string();
    if (!home.empty() && (p == home || p.starts_with(home + "/"))) return "~" + p.substr(home.size());
    return p;
}

std::string addressText(const std::vector<std::string>& addresses) {
    std::string out;
    for (const auto& a : addresses) out += (out.empty() ? "" : ", ") + (a == "0.0.0.0" || a == "::" ? "any (" + a + ")" : a);
    return out;
}

std::string pidsText(const std::vector<int>& pids) {
    std::string out = pids.size() == 1 ? "PID " : "PIDs ";
    for (size_t i = 0; i < pids.size(); ++i) out += (i ? ", " : "") + std::to_string(pids[i]);
    return out;
}

std::string commandLine(const std::vector<std::string>& argv) {
    std::string out;
    for (const auto& a : argv) out += (out.empty() ? "" : " ") + a;
    return out;
}

Gtk::Label* makeLabel(const std::string& text, std::initializer_list<const char*> classes = {}) {
    auto* label = Gtk::make_managed<Gtk::Label>(text);
    label->set_xalign(0.0f);
    label->set_valign(Gtk::Align::START);
    for (const char* c : classes) label->add_css_class(c);
    return label;
}

void setLook(Gtk::Label& label, const char* look) {
    for (const char* c : {"dim-label", "success", "warning", "error"}) label.remove_css_class(c);
    if (look) label.add_css_class(look);
}

} // namespace

ServersWindow::ServersWindow(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder, Gtk::Window& parent)
    : ServersWindowUi(object, builder) {
    set_transient_for(parent);
    say(""); // the design shows a sample
    closeOnEscape(*this);
    deleteWhenHidden(this);
    refresh();
    close_->grab_focus();
}

ServersWindow::~ServersWindow() {
    probes_.cancel();
}

void ServersWindow::on_refresh_clicked() {
    say("");
    refresh();
}

void ServersWindow::on_close_clicked() {
    set_visible(false);
}

void ServersWindow::say(const std::string& text, bool error) {
    // Always shown, if empty: its line is kept, so a message doesn't take the list's last row away.
    message_->set_text(text);
    setLook(*message_, error ? "error" : nullptr);
}

void ServersWindow::refresh() {
    probes_.cancel();
    ++listing_;
    while (servers_->get_child_at(0, 1)) servers_->remove_row(1); // row 0 holds the design's headings
    rows_.clear();

    const auto list = servers::listening(::getuid());
    const auto descendants = proc::descendants(::getpid());
    const std::set<int> ours(descendants.begin(), descendants.end());
    servers_->set_visible(!list.empty());
    empty_->set_visible(list.empty());

    struct Ask {
        size_t row;
        std::string address;
        int port;
    };
    std::vector<Ask> asks;
    for (const auto& s : list) {
        const size_t index = rows_.size();
        const int gridRow = static_cast<int>(index) + 1;
        Row row{s, s.argv.empty() ? std::string("A program Kev-C++ can't see") : servers::summary(s.argv)};

        auto* portBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
        auto* port = makeLabel("");
        port->set_markup("<b>" + std::to_string(s.port) + "</b>");
        portBox->append(*port);
        auto* where = makeLabel(addressText(s.addresses), {"dim-label", "caption"});
        const bool open = std::any_of(s.addresses.begin(), s.addresses.end(),
                                      [](const std::string& a) { return a == "0.0.0.0" || a == "::"; });
        where->set_tooltip_text(open ? "Listening on every address: other computers on the network can reach it too"
                                     : "Only programs on this computer can reach it");
        portBox->append(*where);

        auto* programBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
        auto* program = makeLabel(row.what);
        program->set_ellipsize(Pango::EllipsizeMode::END);
        if (!s.argv.empty()) program->set_tooltip_text(commandLine(s.argv));
        programBox->append(*program);
        std::string details;
        auto add = [&details](const std::string& part) { details += (details.empty() ? "" : "  ·  ") + part; };
        if (!s.cwd.empty()) add(homeShort(s.cwd));
        if (!s.pids.empty()) add(pidsText(s.pids));
        const std::string helper = servers::kevcHelper(s.argv);
        if (!helper.empty()) add(helper);
        else if (std::any_of(s.pids.begin(), s.pids.end(), [&ours](int pid) { return ours.count(pid) > 0; }))
            add("started by this Kev-C++");
        if (s.pids.empty()) add("another user's, or in a container");
        auto* detail = makeLabel(details, {"dim-label", "caption"});
        detail->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
        detail->set_tooltip_text(details);
        programBox->append(*detail);

        auto* answer = makeLabel("Checking…", {"dim-label"});
        auto* kill = Gtk::make_managed<Gtk::Button>("Kill");
        kill->add_css_class("destructive-action");
        kill->set_valign(Gtk::Align::START);
        kill->set_sensitive(!s.pids.empty());
        kill->set_tooltip_text(s.pids.empty() ? "Kev-C++ can't see which program this is, so it can't end it"
                                              : "End " + row.what + " (" + pidsText(s.pids) +
                                                    "): asks it to stop, and forces it after 3 seconds");
        kill->signal_clicked().connect([this, index] { this->kill(index); });

        if (s.pids.empty()) {
            answer->set_text("—");
        } else if (s.state == 'T') {
            answer->set_text("No: suspended");
            setLook(*answer, "error");
            answer->set_tooltip_text("It's stopped (Ctrl+Z in a terminal, or held by a debugger): it takes connections "
                                     "and never answers them. Kill ends it.");
        } else if (helper.find("js-debug") != std::string::npos) {
            answer->set_text("Not a web server"); // the debug adapter's protocol, not HTTP: left alone
        } else {
            asks.push_back({index, servers::reachableAt(s.addresses.front()), s.port});
        }

        servers_->attach(*portBox, 0, gridRow);
        servers_->attach(*programBox, 1, gridRow);
        servers_->attach(*answer, 2, gridRow);
        servers_->attach(*kill, 3, gridRow);
        row.answer = answer;
        row.kill = kill;
        rows_.push_back(std::move(row));
    }
    if (asks.empty()) return;

    // All at once, each on a thread of its own: a stuck server takes the whole timeout.
    const int listing = listing_;
    probes_.start([this, asks, listing](const std::atomic<bool>& cancelled, const BackgroundJob::Post& post) {
        std::vector<std::thread> threads;
        for (const Ask& ask : asks)
            threads.emplace_back([this, ask, listing, &cancelled, &post] {
                const servers::Probe probe = servers::probe(ask.address, ask.port, kProbeMs, &cancelled);
                if (cancelled) return;
                post([this, listing, row = ask.row, probe] {
                    if (listing == listing_) showAnswer(row, probe);
                });
            });
        for (auto& t : threads) t.join();
    });
}

void ServersWindow::showAnswer(size_t row, const servers::Probe& probe) {
    if (row >= rows_.size()) return;
    Gtk::Label& label = *rows_[row].answer;
    using servers::Answer;
    switch (probe.answer) {
    case Answer::Http:
        label.set_text("Yes");
        setLook(label, "success");
        label.set_tooltip_text(std::string("It answered GET ") + servers::kProbePath + " with " + probe.statusLine);
        break;
    case Answer::HttpError:
        label.set_text("Yes, with errors (" + std::to_string(probe.status) + ")");
        setLook(label, "warning");
        label.set_tooltip_text(probe.statusLine + ": it's running, but failing. Its own output says why.");
        break;
    case Answer::NotHttp:
        label.set_text("Not a web server");
        setLook(label, "dim-label");
        label.set_tooltip_text("It answered, but not as a web server does (another protocol)");
        break;
    case Answer::Silent:
        // A dev server (node, python…) that says nothing is stuck; another program may simply wait for
        // its own protocol.
        if (servers::devRuntime(rows_[row].server.argv)) {
            label.set_text("No: stuck?");
            setLook(label, "error");
            label.set_tooltip_text("It took the connection but sent nothing back in " + std::to_string(kProbeMs / 1000) +
                                   " seconds: it's stuck, or very busy. A browser asking it waits forever.");
        } else {
            label.set_text("No answer");
            setLook(label, "dim-label");
            label.set_tooltip_text("It took the connection and said nothing in " + std::to_string(kProbeMs / 1000) +
                                   " seconds: it isn't a web server (it waits for its own protocol), or it's stuck.");
        }
        break;
    case Answer::Refused:
        label.set_text("No");
        setLook(label, "error");
        label.set_tooltip_text("Nothing took the connection: it may have just stopped");
        break;
    }
}

void ServersWindow::kill(size_t index) {
    if (index >= rows_.size()) return;
    const Row& row = rows_[index];
    const std::string what = row.what + " (" + pidsText(row.server.pids) + ")";
    const int port = row.server.port;
    // Only processes that still hold the socket: a PID can belong to another program by now.
    std::vector<int> pids;
    for (int pid : row.server.pids)
        if (std::any_of(row.server.inodes.begin(), row.server.inodes.end(),
                        [pid](unsigned long inode) { return servers::holdsSocket(pid, inode); }))
            pids.push_back(pid);
    if (pids.empty()) {
        markStopped();
        say(what + " had already stopped.");
        return;
    }
    for (int pid : pids) {
        ::kill(pid, SIGTERM);
        ::kill(pid, SIGCONT); // a suspended program only gets the SIGTERM once it runs again
    }
    row.kill->set_sensitive(false);
    row.answer->set_text("Stopping…");
    row.answer->set_tooltip_text("");
    setLook(*row.answer, "dim-label");

    // Watched on a timer the window doesn't own: closing it doesn't spare a program its SIGKILL.
    std::weak_ptr<int> alive = alive_;
    auto finish = [this, alive, what, port, index, listing = listing_](bool stopped, bool forced) {
        if (alive.expired()) return;
        if (stopped) {
            markStopped();
        } else if (listing == listing_ && index < rows_.size()) { // the same rows: it can be tried again
            rows_[index].answer->set_text("Still running");
            setLook(*rows_[index].answer, "error");
            rows_[index].kill->set_sensitive(true);
        }
        const auto now = servers::listening(::getuid());
        const bool held = std::any_of(now.begin(), now.end(), [port](const servers::Server& s) { return s.port == port; });
        const std::string free = held ? " Something else still listens on port " + std::to_string(port) + "."
                                      : " Port " + std::to_string(port) + " is free.";
        if (!stopped) say(what + " didn't stop, even when forced (SIGKILL).", true);
        else if (forced) say(what + " ignored the request to stop and was forced to (SIGKILL)." + free);
        else say("Killed " + what + "." + free);
    };
    const auto start = std::chrono::steady_clock::now();
    Glib::signal_timeout().connect(
        [pids, start, finish, forced = false]() mutable {
            if (std::all_of(pids.begin(), pids.end(), gone)) {
                finish(true, forced);
                return false;
            }
            const auto elapsed = std::chrono::steady_clock::now() - start;
            if (!forced && elapsed >= kGrace) {
                for (int pid : pids)
                    if (!gone(pid)) ::kill(pid, SIGKILL);
                forced = true;
            }
            if (elapsed >= kGiveUp) {
                finish(false, forced);
                return false;
            }
            return true;
        },
        100);
}

void ServersWindow::markStopped() {
    // The rows stay where they are until Refresh: a list that closed up under the pointer would put
    // the next program's Kill button where this one was.
    for (auto& r : rows_) {
        if (r.server.pids.empty() || !std::all_of(r.server.pids.begin(), r.server.pids.end(), gone)) continue;
        r.answer->set_text("Stopped");
        r.answer->set_tooltip_text("");
        setLook(*r.answer, "dim-label");
        r.kill->set_label("Killed");
        r.kill->set_sensitive(false);
        r.kill->set_tooltip_text("");
    }
}

} // namespace kevc
