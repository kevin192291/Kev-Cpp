#include "core/BuildFiles.h"

#include "core/ManagedBlock.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>

namespace kevc {

namespace {

bool isPlainCMakeArg(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return std::isalnum(c) || std::strchr("_./:+=-<>$", c) != nullptr;
    });
}

// Generator expressions ($<...>) pass through; `${` is escaped so user text is never expanded.
std::string cmakeQuote(const std::string& s) {
    if (isPlainCMakeArg(s) && s.find("${") == std::string::npos) return s;
    std::string out = "\"";
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '"' || c == '\\' || (c == '$' && i + 1 < s.size() && s[i + 1] == '{')) out += '\\';
        out += c;
    }
    return out + "\"";
}

// Writes `command(target SCOPE a b c)`, one argument per line when there are several.
void cmakeList(std::ostringstream& out, const std::string& command, const std::string& head,
               const std::vector<std::string>& items) {
    if (items.empty()) return;
    out << command << "(" << head;
    if (items.size() == 1) {
        out << " " << cmakeQuote(items.front()) << ")\n";
        return;
    }
    out << "\n";
    for (const auto& item : items) out << "    " << cmakeQuote(item) << "\n";
    out << ")\n";
}

std::string pkgVariable(const std::string& module) {
    std::string v = "KEVC_PKG_";
    for (unsigned char c : module) v += std::isalnum(c) ? static_cast<char>(std::toupper(c)) : '_';
    return v;
}

std::string makeEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '$') out += '$';
        out += c;
    }
    return out;
}

std::string makeJoin(const std::vector<std::string>& items, const std::string& prefix = "") {
    std::string out;
    for (const auto& item : items) {
        if (!out.empty()) out += ' ';
        out += makeEscape(prefix + item);
    }
    return out;
}

bool hasWhitespace(const std::string& s) {
    return std::any_of(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c); });
}

std::string linkLibraryFlag(const std::string& lib) {
    if (lib.starts_with("-") || lib.find('/') != std::string::npos) return lib;
    return "-l" + lib;
}

std::string cStandardFlag(const std::string& standard) {
    if (standard.starts_with("gnu") || standard.starts_with("c")) return "-std=" + standard;
    return "-std=c" + (standard.empty() ? std::string("17") : standard);
}

std::string makeTargetKind(TargetType t) {
    switch (t) {
    case TargetType::Console: return "console";
    case TargetType::Static: return "static";
    case TargetType::Shared: return "shared";
    }
    return "console";
}

} // namespace

// ---------------------------------------------------------------------------------------------
// CMake

std::vector<std::filesystem::path> cmake_gen::unlistedSources(const std::filesystem::path& root,
                                                              const std::vector<std::filesystem::path>& files) {
    namespace fs = std::filesystem;
    // Every CMakeLists.txt of the project, not those of build folders, dependencies or .git.
    std::vector<std::pair<fs::path, std::string>> lists; // folder, text
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end;
         it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (it->is_directory(ec)) {
            if (name.empty() || name[0] == '.' || name.rfind("build", 0) == 0 || name == "node_modules" ||
                name == "_deps")
                it.disable_recursion_pending();
            continue;
        }
        if (name != "CMakeLists.txt") continue;
        std::ifstream in(it->path(), std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        lists.emplace_back(it->path().parent_path(), text.str());
    }
    std::vector<fs::path> out;
    for (const auto& file : files) {
        const std::string ext = file.extension().string();
        if (ext != ".c" && ext != ".cc" && ext != ".cpp" && ext != ".cxx") continue;
        const bool named = std::any_of(lists.begin(), lists.end(), [&](const auto& list) {
            const std::string rel = file.lexically_relative(list.first).generic_string();
            return list.second.find(rel) != std::string::npos ||
                   list.second.find(file.filename().string()) != std::string::npos;
        });
        if (!named) out.push_back(file);
    }
    return out;
}

std::string cmake_gen::block(const Project& p) {
    std::ostringstream out;
    const std::string& t = p.targetName;

    std::vector<const Package*> fetched;
    std::vector<const Package*> system;
    for (const auto& pkg : p.packages) (pkg.source == Package::Source::System ? system : fetched).push_back(&pkg);

    if (!fetched.empty()) {
        out << "# CMake 4 rejects packages whose cmake_minimum_required is below 3.5.\n"
               "set(CMAKE_POLICY_VERSION_MINIMUM 3.5)\n"
               "include(FetchContent)\n";
        std::vector<std::string> names;
        for (const Package* pkg : fetched) {
            out << "FetchContent_Declare(" << cmakeQuote(pkg->name) << "\n"
                << "    GIT_REPOSITORY " << cmakeQuote(pkg->gitUrl) << "\n"
                << "    GIT_TAG " << cmakeQuote(pkg->gitTag) << "\n"
                << "    GIT_SHALLOW TRUE\n"
                << ")\n";
            for (const auto& [key, value] : pkg->options)
                out << "set(" << key << " " << cmakeQuote(value) << " CACHE STRING \"\" FORCE)\n";
            names.push_back(pkg->name);
        }
        out << "FetchContent_MakeAvailable(";
        for (size_t i = 0; i < names.size(); ++i) out << (i ? " " : "") << cmakeQuote(names[i]);
        out << ")\n\n";
    }
    if (!system.empty()) {
        out << "find_package(PkgConfig REQUIRED)\n";
        for (const Package* pkg : system)
            out << "pkg_check_modules(" << pkgVariable(pkg->name) << " REQUIRED IMPORTED_TARGET "
                << cmakeQuote(pkg->name) << ")\n";
        out << "\n";
    }

    if (p.ownsTarget) {
        switch (p.targetType) {
        case TargetType::Console: out << "add_executable(" << t << ")\n"; break;
        case TargetType::Static: out << "add_library(" << t << " STATIC)\n"; break;
        case TargetType::Shared: out << "add_library(" << t << " SHARED)\n"; break;
        }
    }
    cmakeList(out, "target_sources", t + " PRIVATE", p.sources);

    if (p.ownsTarget) {
        out << "set_target_properties(" << t << " PROPERTIES\n"
            << "    CXX_STANDARD " << (p.standard.empty() ? "20" : p.standard) << "\n"
            << "    CXX_STANDARD_REQUIRED ON\n"
            << "    CXX_EXTENSIONS OFF\n";
        if (!p.outputName.empty()) out << "    OUTPUT_NAME " << cmakeQuote(p.outputName) << "\n";
        if (p.targetType == TargetType::Shared) out << "    POSITION_INDEPENDENT_CODE ON\n";
        out << ")\n";
    }

    const std::string includeScope = p.targetType == TargetType::Console ? " PRIVATE" : " PUBLIC";
    cmakeList(out, "target_include_directories", t + includeScope, p.includeDirs);
    cmakeList(out, "target_compile_definitions", t + " PRIVATE", p.defines);

    std::vector<std::string> options = p.compilerFlags;
    if (!p.debugOptimization.empty()) options.push_back("$<$<CONFIG:Debug>:" + p.debugOptimization + ">");
    if (!p.releaseOptimization.empty()) options.push_back("$<$<CONFIG:Release>:" + p.releaseOptimization + ">");
    cmakeList(out, "target_compile_options", t + " PRIVATE", options);

    cmakeList(out, "target_link_directories", t + " PRIVATE", p.linkDirs);

    std::vector<std::string> libs;
    for (const Package* pkg : fetched) libs.insert(libs.end(), pkg->targets.begin(), pkg->targets.end());
    for (const Package* pkg : system) libs.push_back("PkgConfig::" + pkgVariable(pkg->name));
    libs.insert(libs.end(), p.linkLibraries.begin(), p.linkLibraries.end());
    cmakeList(out, "target_link_libraries", t + " PRIVATE", libs);

    std::vector<std::string> linkOptions = p.linkerFlags;
    if (p.staticRuntime) {
        linkOptions.push_back("-static-libstdc++");
        linkOptions.push_back("-static-libgcc");
    }
    cmakeList(out, "target_link_options", t + " PRIVATE", linkOptions);
    return out.str();
}

std::string cmake_gen::initialFile(const Project& p) {
    std::ostringstream out;
    out << "cmake_minimum_required(VERSION 3.20)\n"
        << "project(" << p.targetName << " LANGUAGES C CXX)\n\n"
        << "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n\n"
        << managed_block::wrap(block(p)) << "\n"
        << "# Your own CMake code goes below. Kev-C++ never edits anything outside the managed block.\n";
    return out.str();
}

// ---------------------------------------------------------------------------------------------
// Make

std::vector<std::string> make_gen::unsupportedSources(const Project& p) {
    std::vector<std::string> out;
    for (const auto& s : p.sources)
        if (hasWhitespace(s)) out.push_back(s);
    return out;
}

std::string make_gen::block(const Project& p) {
    std::vector<std::string> srcs;
    for (const auto& s : p.sources)
        if (!hasWhitespace(s)) srcs.push_back(s);

    std::vector<std::string> libs;
    for (const auto& lib : p.linkLibraries) libs.push_back(linkLibraryFlag(lib));
    std::vector<std::string> pkgs;
    for (const auto& pkg : p.packages)
        if (pkg.source == Package::Source::System) pkgs.push_back(pkg.name);
    std::vector<std::string> ldflags = p.linkerFlags;
    if (p.staticRuntime) ldflags.push_back("-static-libgcc");

    std::ostringstream out;
    out << "CC             := gcc\n"
        << "KIND           := " << makeTargetKind(p.targetType) << "\n"
        << "OUTPUT         := " << makeEscape(p.effectiveOutputName()) << "\n"
        << "CSTD           := " << cStandardFlag(p.standard) << "\n"
        << "SRCS           := " << makeJoin(srcs) << "\n"
        << "CPPFLAGS       := " << makeJoin(p.includeDirs, "-I");
    if (!p.includeDirs.empty() && !p.defines.empty()) out << ' ';
    out << makeJoin(p.defines, "-D") << "\n"
        << "CFLAGS         := " << makeJoin(p.compilerFlags) << "\n"
        << "DEBUG_CFLAGS   := " << makeEscape(p.debugOptimization) << " -g\n"
        << "RELEASE_CFLAGS := " << makeEscape(p.releaseOptimization) << " -DNDEBUG\n"
        << "LDFLAGS        := " << makeJoin(p.linkDirs, "-L");
    if (!p.linkDirs.empty() && !ldflags.empty()) out << ' ';
    out << makeJoin(ldflags) << "\n"
        << "LDLIBS         := " << makeJoin(libs) << "\n"
        << "PKGS           := " << makeJoin(pkgs) << "\n";
    return out.str();
}

std::string make_gen::initialFile(const Project& p) {
    std::ostringstream out;
    out << "# Built by Kev-C++. Run `make` for a release build or `make DEBUG=1` for a debug build.\n\n"
        << managed_block::wrap(block(p)) << "\n"
        << "ifeq ($(DEBUG),1)\n"
           "  BUILD_DIR := build/debug\n"
           "  CFLAGS += $(DEBUG_CFLAGS)\n"
           "else\n"
           "  BUILD_DIR := build/release\n"
           "  CFLAGS += $(RELEASE_CFLAGS)\n"
           "endif\n\n"
           "ifneq ($(strip $(PKGS)),)\n"
           "  CPPFLAGS += $(shell pkg-config --cflags $(PKGS))\n"
           "  LDLIBS += $(shell pkg-config --libs $(PKGS))\n"
           "endif\n\n"
           "OBJS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(filter %.c,$(SRCS)))\n"
           "DEPS := $(OBJS:.o=.d)\n\n"
           "ifeq ($(KIND),static)\n"
           "  OUT := $(BUILD_DIR)/lib$(OUTPUT).a\n"
           "else ifeq ($(KIND),shared)\n"
           "  OUT := $(BUILD_DIR)/lib$(OUTPUT).so\n"
           "  CFLAGS += -fPIC\n"
           "  LDFLAGS += -shared\n"
           "else\n"
           "  OUT := $(BUILD_DIR)/$(OUTPUT)\n"
           "endif\n\n"
           ".PHONY: all run clean print-out\n"
           "all: $(OUT)\n\n"
           "ifeq ($(KIND),static)\n"
           "$(OUT): $(OBJS)\n"
           "\t@mkdir -p $(dir $@)\n"
           "\t$(AR) rcs $@ $^\n"
           "else\n"
           "$(OUT): $(OBJS)\n"
           "\t@mkdir -p $(dir $@)\n"
           "\t$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@\n"
           "endif\n\n"
           "# Objects depend on the Makefile so flag changes from the IDE trigger a rebuild.\n"
           "$(BUILD_DIR)/obj/%.o: %.c Makefile\n"
           "\t@mkdir -p $(dir $@)\n"
           "\t$(CC) $(CSTD) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@\n\n"
           "run: $(OUT)\n"
           "\t./$(OUT)\n\n"
           "print-out:\n"
           "\t@echo $(OUT)\n\n"
           "clean:\n"
           "\trm -rf build\n\n"
           "-include $(DEPS)\n";
    return out.str();
}

// ---------------------------------------------------------------------------------------------
// compile_commands.json

std::string compileCommandsJson(const Project& p, const std::filesystem::path& root, const std::string& compiler,
                                const std::vector<std::string>& pkgCflags) {
    nlohmann::json entries = nlohmann::json::array();
    for (const auto& src : p.sources) {
        std::filesystem::path rel(src);
        if (rel.extension() != ".c") continue;
        std::vector<std::string> args{compiler, cStandardFlag(p.standard)};
        for (const auto& dir : p.includeDirs) args.push_back("-I" + dir);
        for (const auto& def : p.defines) args.push_back("-D" + def);
        args.insert(args.end(), p.compilerFlags.begin(), p.compilerFlags.end());
        args.push_back(p.debugOptimization);
        args.push_back("-g");
        args.insert(args.end(), pkgCflags.begin(), pkgCflags.end());
        std::filesystem::path obj = std::filesystem::path("build/debug/obj") / rel;
        obj.replace_extension(".o");
        args.insert(args.end(), {"-c", src, "-o", obj.string()});
        entries.push_back({{"directory", root.string()}, {"file", (root / rel).string()}, {"arguments", args}});
    }
    return entries.dump(2) + "\n";
}

} // namespace kevc
