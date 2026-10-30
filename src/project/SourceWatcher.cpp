#include "project/SourceWatcher.h"

#include <filesystem>
#include <set>

namespace fs = std::filesystem;

namespace kevc {

namespace {
constexpr unsigned kDebounceMs = 250;
}

SourceWatcher::SourceWatcher(ProjectSession& session) : session_(session) {
    refreshMonitors();
}

SourceWatcher::~SourceWatcher() {
    debounce_.disconnect();
    for (auto& [dir, monitor] : monitors_) monitor->cancel();
}

void SourceWatcher::refreshMonitors() {
    std::set<std::string> dirs{session_.root().string()};
    std::error_code ec;
    fs::recursive_directory_iterator it(session_.root(), fs::directory_options::skip_permission_denied, ec), end;
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_directory(ec)) continue;
        if (isIgnoredDirName(it->path().filename().string())) {
            it.disable_recursion_pending();
            continue;
        }
        dirs.insert(it->path().string());
    }

    for (auto m = monitors_.begin(); m != monitors_.end();) {
        if (!dirs.count(m->first)) {
            m->second->cancel();
            m = monitors_.erase(m);
        } else {
            ++m;
        }
    }
    for (const auto& dir : dirs) {
        if (monitors_.count(dir)) continue;
        try {
            auto monitor = Gio::File::create_for_path(dir)->monitor_directory(Gio::FileMonitorFlags::WATCH_MOVES);
            monitor->signal_changed().connect(sigc::mem_fun(*this, &SourceWatcher::onChanged));
            monitors_.emplace(dir, monitor);
        } catch (const Glib::Error&) {
            // Directory vanished between the scan and the monitor call; the next refresh handles it.
        }
    }
}

void SourceWatcher::onChanged(const Glib::RefPtr<Gio::File>&, const Glib::RefPtr<Gio::File>&,
                              Gio::FileMonitor::Event event) {
    using E = Gio::FileMonitor::Event;
    if (event != E::CREATED && event != E::DELETED && event != E::RENAMED && event != E::MOVED_IN &&
        event != E::MOVED_OUT)
        return;
    debounce_.disconnect();
    debounce_ = Glib::signal_timeout().connect(
        [this] {
            syncNow();
            return false;
        },
        kDebounceMs);
}

void SourceWatcher::syncNow() {
    debounce_.disconnect();
    refreshMonitors();
    ProjectSession::SyncResult result;
    try {
        result = session_.sync();
    } catch (const std::exception& e) {
        result.warnings.push_back(std::string("Could not update the project: ") + e.what());
    }
    if (result.changed() || !result.warnings.empty()) synced_.emit(result);
}

} // namespace kevc
