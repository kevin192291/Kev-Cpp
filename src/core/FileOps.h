#pragma once

#include <filesystem>
#include <string>

namespace kevc::fileops {

// Why `name` can't be the name of a new or renamed file in `dir` ("" when it can). `current` is the
// file being renamed, which may keep its own name with different capitals.
std::string checkName(const std::filesystem::path& dir, const std::string& name,
                      const std::filesystem::path& current = {});

// Why `source` can't be moved into the folder `targetDir` ("" when it can). Moving a file to the
// folder it is already in is not an error; see sameFolder.
std::string checkMove(const std::filesystem::path& source, const std::filesystem::path& targetDir);
bool sameFolder(const std::filesystem::path& source, const std::filesystem::path& targetDir);

// Renames or moves a file or folder; returns why it failed, or "".
std::string move(const std::filesystem::path& from, const std::filesystem::path& to);

// Where `path` is after `from` was renamed to `to`: `to` itself, or the same place inside `to` when
// `from` was a folder holding it. Empty when the rename doesn't touch `path`.
std::filesystem::path relocated(const std::filesystem::path& path, const std::filesystem::path& from,
                                const std::filesystem::path& to);

// Is `path` the folder `dir` or inside it? (Compares spellings, so pass both in the same form.)
bool isInside(const std::filesystem::path& path, const std::filesystem::path& dir);

// The name selected when a rename starts: "main" of "main.cpp", the whole name of a folder or of
// ".gitignore". Returns its length in characters (UTF-8 aware).
int stemLength(const std::string& name, bool folder);

// Open Terminal Here: what to type at a shell's prompt in `cwd` to go to the folder `dir`. A folder
// inside `cwd` is named from there as "./src" (a bare "src" could go where CDPATH says instead), any
// other by its full path, quoted when it has to be. "" when the shell is there already; an empty
// `cwd` (unknown) gives the full path.
std::string cdCommand(const std::filesystem::path& dir, const std::filesystem::path& cwd);

} // namespace kevc::fileops
