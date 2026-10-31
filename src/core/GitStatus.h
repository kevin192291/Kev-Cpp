#pragma once

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// Which files in a git work tree have changes that aren't committed yet, for the Project tab's icons.
// Kev-C++ only shows this; committing and pushing are left to git in a terminal.
namespace kevc::git {

enum class FileState {
    Modified,   // changed since the last commit (staged or not)
    Added,      // new and staged
    Untracked,  // new, not in git yet
    Deleted,    // gone from the work tree or staged for removal
    Conflicted, // unmerged
};

// Paths are relative to the work tree's top folder, with "/" separators.
using Changes = std::map<std::string, FileState>;

// The command that lists them: run in the project folder.
std::vector<std::string> statusCommand();
// Its output (`git status --porcelain=v1 -z --untracked-files=all`).
Changes parseStatus(std::string_view output);
// The folders (relative, never "") that hold a changed file, at any depth.
std::set<std::string> changedFolders(const Changes& changes);

// The command that finds the work tree's top folder and its .git folder (one per line).
std::vector<std::string> topLevelCommand();

} // namespace kevc::git
