#include "core/GitStatus.h"

namespace kevc::git {

std::vector<std::string> statusCommand() {
    // -z: paths exactly as they are (no quoting); all untracked files, not just their folders.
    return {"git", "status", "--porcelain=v1", "-z", "--untracked-files=all"};
}

std::vector<std::string> topLevelCommand() {
    return {"git", "rev-parse", "--show-toplevel", "--absolute-git-dir"};
}

Changes parseStatus(std::string_view output) {
    Changes changes;
    size_t pos = 0;
    auto next = [&]() -> std::string_view {
        const size_t end = output.find('\0', pos);
        const std::string_view field = output.substr(pos, (end == std::string_view::npos ? output.size() : end) - pos);
        pos = end == std::string_view::npos ? output.size() : end + 1;
        return field;
    };
    while (pos < output.size()) {
        const std::string_view entry = next();
        if (entry.size() < 4) continue;
        const char x = entry[0], y = entry[1];
        const std::string path(entry.substr(3));
        // A rename or copy is followed by the old path, which is no longer there.
        if (x == 'R' || x == 'C' || y == 'R' || y == 'C') next();
        FileState state = FileState::Modified;
        if (x == '?' && y == '?') state = FileState::Untracked;
        else if (x == '!' && y == '!') continue; // ignored (only with --ignored)
        else if (x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D')) state = FileState::Conflicted;
        else if (x == 'D' || y == 'D') state = FileState::Deleted;
        else if (x == 'A') state = FileState::Added;
        changes[path] = state;
    }
    return changes;
}

std::set<std::string> changedFolders(const Changes& changes) {
    std::set<std::string> folders;
    for (const auto& [path, state] : changes)
        for (size_t slash = path.find('/'); slash != std::string::npos; slash = path.find('/', slash + 1))
            folders.insert(path.substr(0, slash));
    return folders;
}

} // namespace kevc::git
