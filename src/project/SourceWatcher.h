#pragma once

#include "core/ProjectSession.h"

#include <giomm.h>

#include <map>
#include <string>

namespace kevc {

// Watches every directory of a project and, a moment after files are created, deleted or
// renamed, resyncs the source list into the .kevc and the CMakeLists.txt / Makefile.
class SourceWatcher {
public:
    explicit SourceWatcher(ProjectSession& session);
    ~SourceWatcher();

    // Runs a sync immediately (also cancels any pending debounced sync).
    void syncNow();

    sigc::signal<void(const ProjectSession::SyncResult&)>& signalSynced() { return synced_; }

private:
    void refreshMonitors();
    void onChanged(const Glib::RefPtr<Gio::File>& file, const Glib::RefPtr<Gio::File>& other,
                   Gio::FileMonitor::Event event);

    ProjectSession& session_;
    std::map<std::string, Glib::RefPtr<Gio::FileMonitor>> monitors_;
    sigc::connection debounce_;
    sigc::signal<void(const ProjectSession::SyncResult&)> synced_;
};

} // namespace kevc
