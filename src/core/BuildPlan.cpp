#include "core/BuildPlan.h"

#include "core/WebProject.h"

#include <algorithm>

namespace fs = std::filesystem;

namespace kevc {

fs::path buildDir(const fs::path& root, BuildConfig config) {
    return root / "build" / (config == BuildConfig::Debug ? "debug" : "release");
}

std::vector<Command> buildCommands(const Project& p, const fs::path& root, BuildAction action, BuildConfig config,
                                   const BuildTools& tools) {
    const std::string jobs = std::to_string(std::max(1, tools.jobs));
    std::vector<Command> out;

    if (p.kind == ProjectKind::Cpp) {
        const fs::path dir = buildDir(root, config);
        std::error_code ec;
        const bool configured = fs::exists(dir / "CMakeCache.txt", ec);
        if (action == BuildAction::Clean) {
            if (configured) out.push_back({{tools.cmake, "--build", dir.string(), "--target", "clean"}, root});
            return out;
        }
        if (!configured) {
            std::vector<std::string> configure{tools.cmake, "-S", root.string(), "-B", dir.string()};
            if (tools.ninja) configure.insert(configure.end(), {"-G", "Ninja"});
            configure.insert(configure.end(), {
                std::string("-DCMAKE_BUILD_TYPE=") + (config == BuildConfig::Debug ? "Debug" : "Release"),
                "-DCMAKE_C_COMPILER=" + tools.gcc,
                "-DCMAKE_CXX_COMPILER=" + tools.gxx,
                "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
            });
            out.push_back({configure, root});
        }
        std::vector<std::string> build{tools.cmake, "--build", dir.string(), "--parallel", jobs};
        if (action == BuildAction::Rebuild) build.push_back("--clean-first");
        out.push_back({build, root});
        return out;
    }

    if (p.kind == ProjectKind::WebTs) {
        const WebProject web = WebProject::detect(root);
        if (action == BuildAction::Clean) return out;
        // Rebuild makes the production build with the project's own build script (vite build).
        if (action == BuildAction::Rebuild && web.usesVite() && web.hasScript("build")) {
            out.push_back({web.runScriptCommand("build"), root});
            return out;
        }
        if (web.svelte()) return svelteCheckCommands(web, root, tools.node);
        std::error_code ec;
        if (tools.tsc.empty() || !fs::exists(root / "tsconfig.json", ec)) return out;
        out.push_back({{tools.node, tools.tsc, "--noEmit", "--pretty", "false", "-p", root.string()}, root});
        return out;
    }

    if (p.kind == ProjectKind::C) {
        std::vector<std::string> make{tools.make, "-C", root.string()};
        if (action == BuildAction::Clean) {
            make.push_back("clean");
            out.push_back({make, root});
            return out;
        }
        make.insert(make.end(), {"-j", jobs, "CC=" + tools.gcc});
        if (config == BuildConfig::Debug) make.push_back("DEBUG=1");
        if (action == BuildAction::Rebuild) make.push_back("-B");
        out.push_back({make, root});
    }
    return out;
}

std::optional<fs::path> runnableOutput(const Project& p, const fs::path& root, BuildConfig config) {
    if (p.kind == ProjectKind::WebTs || p.targetType != TargetType::Console) return std::nullopt;
    const fs::path dir = buildDir(root, config);
    if (p.buildFileManaged) return dir / p.effectiveOutputName();
    // A project's own CMakeLists.txt may build it in a subfolder (tests/…): look for it.
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_directory(ec)) {
            if (it->path().filename() == "CMakeFiles" || it->path().filename() == "_deps") it.disable_recursion_pending();
            continue;
        }
        if (it->path().filename() == p.targetName && it->is_regular_file(ec) &&
            (fs::status(it->path(), ec).permissions() & fs::perms::owner_exec) != fs::perms::none)
            return it->path();
    }
    return dir / p.targetName;
}

bool linkCompileCommands(const fs::path& root, BuildConfig config) {
    const fs::path link = root / "compile_commands.json";
    const fs::path target = fs::path("build") / (config == BuildConfig::Debug ? "debug" : "release") /
                            "compile_commands.json";
    std::error_code ec;
    if (!fs::exists(root / target, ec)) return fs::exists(link, ec);
    if (fs::is_symlink(fs::symlink_status(link, ec))) {
        if (fs::read_symlink(link, ec) == target) return true;
        fs::remove(link, ec);
    } else if (fs::exists(fs::symlink_status(link, ec))) {
        return true; // a real file the user (or another tool) put there; leave it alone
    }
    fs::create_symlink(target, link, ec);
    return !ec;
}

std::string displayCommand(const std::vector<std::string>& argv) {
    std::string out;
    for (const auto& arg : argv) {
        if (!out.empty()) out += ' ';
        const bool plain = !arg.empty() && std::all_of(arg.begin(), arg.end(), [](unsigned char c) {
            return std::isalnum(c) || std::string_view("-_./=:+,@%").find(c) != std::string_view::npos;
        });
        if (plain) {
            out += arg;
            continue;
        }
        out += '\'';
        for (char c : arg) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
        out += '\'';
    }
    return out;
}

} // namespace kevc
