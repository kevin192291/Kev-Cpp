#include "core/FileOps.h"

#include <cerrno>
#include <system_error>

namespace fs = std::filesystem;

namespace kevc::fileops {

namespace {

// Without a trailing slash, so "a/b/" and "a/b" compare equal.
fs::path normal(const fs::path& p) {
    fs::path n = p.lexically_normal();
    if (!n.has_filename() && n.has_parent_path() && n != n.root_path()) n = n.parent_path();
    return n;
}

std::string quoted(const fs::path& p) {
    return "“" + p.filename().string() + "”";
}

bool taken(const fs::path& p) {
    std::error_code ec;
    return fs::exists(fs::symlink_status(p, ec)); // a broken symlink still takes the name
}

} // namespace

std::string checkName(const fs::path& dir, const std::string& name, const fs::path& current) {
    if (name.find_first_not_of(" \t") == std::string::npos) return "Enter a name.";
    if (name.find('/') != std::string::npos) return "A name can't contain /.";
    if (name.find('\0') != std::string::npos) return "A name can't contain a null character.";
    if (name == "." || name == "..") return "“" + name + "” is reserved.";
    if (name.size() > 255) return "That name is too long.";
    if (!current.empty() && name == current.filename().string()) return {}; // unchanged: nothing to do
    if (taken(dir / name)) return "“" + name + "” already exists in this folder.";
    return {};
}

bool sameFolder(const fs::path& source, const fs::path& targetDir) {
    return normal(source).parent_path() == normal(targetDir);
}

std::string checkMove(const fs::path& source, const fs::path& targetDir) {
    std::error_code ec;
    if (!taken(source)) return quoted(source) + " no longer exists.";
    if (!fs::is_directory(targetDir, ec)) return quoted(targetDir) + " isn't a folder.";
    if (isInside(targetDir, source)) return "A folder can't be moved into itself.";
    if (sameFolder(source, targetDir)) return {};
    if (taken(normal(targetDir) / normal(source).filename()))
        return quoted(source) + " already exists in " + quoted(normal(targetDir)) + ".";
    return {};
}

std::string move(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::rename(from, to, ec);
    if (!ec) return {};
    if (ec.value() != EXDEV) return ec.message();
    // Another file system (a mounted folder inside the project): copy, then remove the original.
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
    if (ec) return ec.message();
    fs::remove_all(from, ec);
    return ec ? "Copied, but the original couldn't be removed: " + ec.message() : std::string();
}

bool isInside(const fs::path& path, const fs::path& dir) {
    const fs::path p = normal(path), d = normal(dir);
    auto pi = p.begin(), di = d.begin();
    for (; di != d.end(); ++di, ++pi)
        if (pi == p.end() || *pi != *di) return false;
    return true;
}

fs::path relocated(const fs::path& path, const fs::path& from, const fs::path& to) {
    const fs::path p = normal(path), f = normal(from);
    if (p == f) return to;
    if (!isInside(p, f)) return {};
    return to / p.lexically_relative(f);
}

int stemLength(const std::string& name, bool folder) {
    size_t bytes = name.size();
    if (!folder)
        if (const size_t dot = name.rfind('.'); dot != std::string::npos && dot > 0) bytes = dot;
    int chars = 0;
    for (size_t i = 0; i < bytes; ++i)
        if ((static_cast<unsigned char>(name[i]) & 0xC0) != 0x80) ++chars;
    return chars;
}

} // namespace kevc::fileops
