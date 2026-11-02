#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace kevc {

// Runs a short command in the background and hands its stdout and exit status to `done` (on the main
// loop), unless `alive` has expired by then. stderr is discarded. -1: it couldn't start.
void captureOutput(const std::vector<std::string>& argv, const std::filesystem::path& cwd,
                   std::function<void(std::string output, int status)> done, std::weak_ptr<int> alive);

// The same with `input` written to its stdin, and its stderr kept too (where tools like prettier say why
// they refused).
void captureOutput(const std::vector<std::string>& argv, const std::filesystem::path& cwd, const std::string& input,
                   std::function<void(std::string output, std::string errors, int status)> done,
                   std::weak_ptr<int> alive);

} // namespace kevc
