#pragma once

#include <filesystem>
#include <vector>

namespace kevc::proc {

// Every process below `pid`: its children, theirs, and so on, from /proc/<n>/stat. A process group
// doesn't hold them all: js-debug's watchdog runs in a session of its own under the program it
// debugs, and kept Vite alive (waiting for its debugger to disconnect) after the IDE had gone.
// Read this before signalling anything: once a parent exits its children move to init, and the
// link is lost. `procRoot` is for tests.
std::vector<int> descendants(int pid, const std::filesystem::path& procRoot = "/proc");

} // namespace kevc::proc
