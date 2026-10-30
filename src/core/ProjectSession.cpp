#include "core/ProjectSession.h"

#include "core/BuildFiles.h"
#include "core/ManagedBlock.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <iterator>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace kevc {

namespace {

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Writes only when the content differs, so file watchers and CMake don't see spurious changes.
bool writeFileIfChanged(const fs::path& path, const std::string& content) {
    std::error_code ec;
    if (fs::exists(path, ec)) {
        try {
            if (readFile(path) == content) return false;
        } catch (const std::exception&) {
        }
    }
    if (path.has_parent_path()) fs::create_directories(path.parent_path());
    fs::path tmp = path;
    tmp += ".kevc-tmp";
    {
        std::ofstream out(tmp, std::ios::binary);
        if (!out) throw std::runtime_error("cannot write " + tmp.string());
        out << content;
    }
    fs::rename(tmp, path);
    return true;
}

std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
}

std::vector<std::string> runPkgConfigCflags(const std::vector<std::string>& modules) {
    if (modules.empty()) return {};
    std::string cmd = "pkg-config --cflags";
    for (const auto& m : modules) cmd += " " + shellQuote(m);
    cmd += " 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {};
    std::string output;
    std::array<char, 1024> buf{};
    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe)) output += buf.data();
    pclose(pipe);
    std::istringstream ss(output);
    std::vector<std::string> args;
    for (std::string arg; ss >> arg;) args.push_back(arg);
    return args;
}

struct TemplateFile {
    std::string path;
    std::string content;
};

std::vector<TemplateFile> templateFiles(const Project& p, ProjectTemplate tmpl) {
    const std::string& t = p.targetName;
    switch (tmpl) {
    case ProjectTemplate::CppConsole:
        return {{"src/main.cpp",
                 "#include <iostream>\n\n"
                 "int main() {\n"
                 "    std::cout << \"Hello from " + p.name + "!\\n\";\n"
                 "    return 0;\n"
                 "}\n"}};
    case ProjectTemplate::CppStatic:
    case ProjectTemplate::CppShared:
        return {{"include/" + t + "/" + t + ".hpp",
                 "#pragma once\n\n"
                 "#include <string>\n\n"
                 "namespace " + t + " {\n\n"
                 "std::string greeting();\n\n"
                 "} // namespace " + t + "\n"},
                {"src/" + t + ".cpp",
                 "#include \"" + t + "/" + t + ".hpp\"\n\n"
                 "namespace " + t + " {\n\n"
                 "std::string greeting() {\n"
                 "    return \"Hello from " + p.name + "!\";\n"
                 "}\n\n"
                 "} // namespace " + t + "\n"}};
    case ProjectTemplate::CConsole:
        return {{"src/main.c",
                 "#include <stdio.h>\n\n"
                 "int main(void) {\n"
                 "    printf(\"Hello from " + p.name + "!\\n\");\n"
                 "    return 0;\n"
                 "}\n"}};
    case ProjectTemplate::WebTs:
        return {{"index.html",
                 "<!doctype html>\n"
                 "<html lang=\"en\">\n"
                 "<head>\n"
                 "  <meta charset=\"utf-8\">\n"
                 "  <title>" + p.name + "</title>\n"
                 "</head>\n"
                 "<body>\n"
                 "  <h1 id=\"title\"></h1>\n"
                 "  <script type=\"module\" src=\"/src/main.ts\"></script>\n"
                 "</body>\n"
                 "</html>\n"},
                {"src/main.ts",
                 "import { greet } from \"./greet\";\n\n"
                 "const title = document.getElementById(\"title\");\n"
                 "if (title) title.textContent = greet(\"" + p.name + "\");\n"},
                {"src/greet.ts",
                 "export function greet(name: string): string {\n"
                 "    return `Hello from ${name}!`;\n"
                 "}\n"},
                {"tsconfig.json",
                 "{\n"
                 "  \"compilerOptions\": {\n"
                 "    \"target\": \"ES2022\",\n"
                 "    \"module\": \"ESNext\",\n"
                 "    \"moduleResolution\": \"Bundler\",\n"
                 "    \"lib\": [\"ES2022\", \"DOM\"],\n"
                 "    \"strict\": true,\n"
                 "    \"noEmit\": true\n"
                 "  },\n"
                 "  \"include\": [\"src\"]\n"
                 "}\n"}};
    }
    return {};
}

} // namespace

bool isIgnoredDirName(const std::string& name) {
    return name.empty() || name.front() == '.' || name == "build" || name == "_deps" || name == "node_modules" ||
           name.starts_with("cmake-build-");
}

ProjectSession::ProjectSession(Project p, fs::path projectFile)
    : project_(std::move(p)), projectFile_(std::move(projectFile)), root_(projectFile_.parent_path()),
      pkgConfigCflags_(runPkgConfigCflags) {}

std::optional<fs::path> ProjectSession::findProjectFile(const fs::path& root) {
    std::error_code ec;
    std::optional<fs::path> found;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".kevc") {
            if (!found || entry.path() < *found) found = entry.path();
        }
    }
    return found;
}

ProjectSession ProjectSession::open(const fs::path& projectFile) {
    return ProjectSession(Project::load(projectFile), fs::absolute(projectFile));
}

ProjectSession ProjectSession::create(const fs::path& parentDir, const std::string& name, ProjectTemplate tmpl) {
    if (name.empty() || name.find('/') != std::string::npos) throw std::runtime_error("invalid project name");
    fs::path root = fs::absolute(parentDir / name);
    std::error_code ec;
    if (fs::exists(root, ec) && !fs::is_empty(root, ec))
        throw std::runtime_error(root.string() + " already exists and is not empty");
    fs::create_directories(root);

    Project p;
    p.name = name;
    p.targetName = sanitizeTargetName(name);
    p.compilerFlags = {"-Wall", "-Wextra"};
    switch (tmpl) {
    case ProjectTemplate::CppConsole: p.kind = ProjectKind::Cpp; break;
    case ProjectTemplate::CppStatic:
        p.kind = ProjectKind::Cpp;
        p.targetType = TargetType::Static;
        p.includeDirs = {"include"};
        break;
    case ProjectTemplate::CppShared:
        p.kind = ProjectKind::Cpp;
        p.targetType = TargetType::Shared;
        p.includeDirs = {"include"};
        break;
    case ProjectTemplate::CConsole: p.kind = ProjectKind::C; break;
    case ProjectTemplate::WebTs:
        p.kind = ProjectKind::WebTs;
        p.compilerFlags.clear();
        break;
    }
    p.standard = p.kind == ProjectKind::C ? "17" : "20";

    for (const auto& file : templateFiles(p, tmpl)) writeFileIfChanged(root / file.path, file.content);
    // .cache/ holds clangd's background index; compile_commands.json is generated.
    writeFileIfChanged(root / ".gitignore",
                       p.kind == ProjectKind::WebTs ? "node_modules/\n" : "build/\n.cache/\ncompile_commands.json\n");

    ProjectSession session(std::move(p), root / (name + ".kevc"));
    session.project_.setSources(session.scanSources());
    session.saveAndRegenerate();
    return session;
}

ProjectSession ProjectSession::adoptCMake(const fs::path& rootIn) {
    fs::path root = fs::absolute(rootIn);
    std::string cmake = readFile(root / "CMakeLists.txt");
    static const std::regex targetRe(R"(add_(executable|library)\s*\(\s*([A-Za-z0-9_.+-]+))");
    std::smatch m;
    if (!std::regex_search(cmake, m, targetRe))
        throw std::runtime_error("no add_executable() or add_library() found in CMakeLists.txt");

    Project p;
    p.name = root.filename().string();
    p.kind = ProjectKind::Cpp;
    p.targetType = m[1] == "library" ? TargetType::Static : TargetType::Console;
    p.ownsTarget = false;
    p.targetName = m[2];
    p.standard = "20";
    p.debugOptimization.clear();
    p.releaseOptimization.clear();

    ProjectSession session(std::move(p), root / (root.filename().string() + ".kevc"));
    session.project_.setSources(session.scanSources());
    session.saveAndRegenerate();
    return session;
}

fs::path ProjectSession::buildFile() const {
    fs::path name = project_.buildFileName();
    return name.empty() ? fs::path{} : root_ / name;
}

std::vector<std::string> ProjectSession::scanSources() const {
    std::vector<std::string> found;
    std::error_code ec;
    fs::recursive_directory_iterator it(root_, fs::directory_options::skip_permission_denied, ec), end;
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        const auto& entry = *it;
        if (entry.is_directory(ec)) {
            if (isIgnoredDirName(entry.path().filename().string())) it.disable_recursion_pending();
            continue;
        }
        if (!entry.is_regular_file(ec)) continue;
        fs::path rel = entry.path().lexically_relative(root_);
        if (project_.tracksFile(rel)) found.push_back(rel.generic_string());
    }
    std::sort(found.begin(), found.end());
    return found;
}

ProjectSession::SyncResult ProjectSession::sync() {
    std::vector<std::string> before = project_.sources;
    if (!project_.setSources(scanSources())) return {};

    SyncResult result = saveAndRegenerate();
    std::set_difference(project_.sources.begin(), project_.sources.end(), before.begin(), before.end(),
                        std::back_inserter(result.added));
    std::set_difference(before.begin(), before.end(), project_.sources.begin(), project_.sources.end(),
                        std::back_inserter(result.removed));
    return result;
}

ProjectSession::SyncResult ProjectSession::saveAndRegenerate() {
    SyncResult result;
    project_.save(projectFile_);
    result.buildFileWritten = writeBuildFile(result.warnings);
    if (project_.kind == ProjectKind::C) writeCompileCommands();
    return result;
}

bool ProjectSession::writeBuildFile(std::vector<std::string>& warnings) {
    fs::path file = buildFile();
    if (file.empty()) return false;

    std::string body;
    std::string fresh;
    if (project_.kind == ProjectKind::Cpp) {
        body = cmake_gen::block(project_);
        fresh = cmake_gen::initialFile(project_);
    } else {
        body = make_gen::block(project_);
        fresh = make_gen::initialFile(project_);
        for (const auto& s : make_gen::unsupportedSources(project_))
            warnings.push_back("Make cannot build paths containing spaces; left out: " + s);
    }

    std::error_code ec;
    if (!fs::exists(file, ec)) return writeFileIfChanged(file, fresh);
    try {
        return writeFileIfChanged(file, managed_block::splice(readFile(file), body));
    } catch (const managed_block::MalformedBlock& e) {
        warnings.push_back(file.filename().string() + " was not updated: " + e.what());
        return false;
    }
}

void ProjectSession::writeCompileCommands() {
    std::vector<std::string> modules;
    for (const auto& pkg : project_.packages)
        if (pkg.source == Package::Source::System) modules.push_back(pkg.name);
    writeFileIfChanged(root_ / "compile_commands.json",
                       compileCommandsJson(project_, root_, "gcc", pkgConfigCflags_(modules)));
}

} // namespace kevc
