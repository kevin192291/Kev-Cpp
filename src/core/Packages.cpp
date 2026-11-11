#include "core/Packages.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace kevc {

std::vector<std::string> splitArgs(std::string_view text) {
    std::vector<std::string> args;
    std::string current;
    bool inArg = false;
    char quote = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quote) {
            if (c == quote) quote = 0;
            else if (c == '\\' && quote == '"' && i + 1 < text.size()) current += text[++i];
            else current += c;
        } else if (c == '"' || c == '\'') {
            quote = c;
            inArg = true;
        } else if (c == '\\' && i + 1 < text.size()) {
            current += text[++i];
            inArg = true;
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            if (inArg) args.push_back(std::move(current));
            current.clear();
            inArg = false;
        } else {
            current += c;
            inArg = true;
        }
    }
    if (inArg) args.push_back(std::move(current));
    return args;
}

std::string joinArgs(const std::vector<std::string>& args) {
    std::string out;
    for (const auto& arg : args) {
        if (!out.empty()) out += ' ';
        const bool plain = !arg.empty() && std::none_of(arg.begin(), arg.end(), [](unsigned char c) {
            return std::isspace(c) || c == '"' || c == '\'' || c == '\\';
        });
        if (plain) {
            out += arg;
            continue;
        }
        out += '"';
        for (char c : arg) {
            if (c == '"' || c == '\\') out += '\\';
            out += c;
        }
        out += '"';
    }
    return out;
}

Package CatalogEntry::toPackage() const {
    return Package{Package::Source::Catalog, name, gitUrl, gitTag, targets, options};
}

std::vector<CatalogEntry> parseCatalog(std::string_view text) {
    const auto j = nlohmann::json::parse(text);
    std::vector<CatalogEntry> out;
    for (const auto& e : j.at("packages")) {
        CatalogEntry c;
        c.name = e.at("name").get<std::string>();
        c.description = e.value("description", "");
        c.category = e.value("category", "");
        c.gitUrl = e.at("git").get<std::string>();
        c.gitTag = e.at("tag").get<std::string>();
        c.targets = e.at("targets").get<std::vector<std::string>>();
        c.options = e.value("options", std::map<std::string, std::string>{});
        c.include = e.value("include", "");
        c.notes = e.value("notes", "");
        out.push_back(std::move(c));
    }
    return out;
}

std::vector<PkgConfigModule> parsePkgConfigList(std::string_view output) {
    std::vector<PkgConfigModule> out;
    std::istringstream in{std::string(output)};
    for (std::string line; std::getline(in, line);) {
        const auto nameEnd = line.find_first_of(" \t");
        PkgConfigModule m;
        m.name = line.substr(0, nameEnd);
        if (m.name.empty()) continue;
        if (nameEnd != std::string::npos) {
            const auto descStart = line.find_first_not_of(" \t", nameEnd);
            if (descStart != std::string::npos) m.description = line.substr(descStart);
        }
        out.push_back(std::move(m));
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    out.erase(std::unique(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.name == b.name; }),
              out.end());
    return out;
}

std::vector<PkgConfigModule> listPkgConfigModules() {
    FILE* pipe = popen("pkg-config --list-all 2>/dev/null", "r");
    if (!pipe) return {};
    std::string output;
    std::array<char, 4096> buf{};
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), pipe)) > 0) output.append(buf.data(), n);
    pclose(pipe);
    return parsePkgConfigList(output);
}

std::string runPkgConfig(const std::string& arguments, const std::vector<std::string>& modules) {
    std::string command = "pkg-config " + arguments;
    for (const auto& m : modules) {
        // Module names are letters, digits and a few signs; anything else isn't one.
        if (m.empty() || !std::all_of(m.begin(), m.end(), [](unsigned char c) {
                return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '+';
            }))
            return {};
        command += " " + m;
    }
    FILE* pipe = popen((command + " 2>/dev/null").c_str(), "r");
    if (!pipe) return {};
    std::string output;
    std::array<char, 4096> buf{};
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), pipe)) > 0) output.append(buf.data(), n);
    if (pclose(pipe) != 0) return {};
    while (!output.empty() && std::isspace(static_cast<unsigned char>(output.back()))) output.pop_back();
    return output;
}

std::vector<std::string> missingPkgConfigLibs(std::string_view libsLine) {
    std::vector<std::filesystem::path> dirs;
    std::vector<std::string> libs;
    for (const std::string& arg : splitArgs(libsLine)) {
        if (arg.starts_with("-L") && arg.size() > 2) dirs.emplace_back(arg.substr(2));
        else if (arg.starts_with("-l") && arg.size() > 2 && arg[2] != ':') libs.push_back(arg.substr(2));
    }
    std::vector<std::string> missing;
    if (dirs.empty()) return missing;
    std::error_code ec;
    for (const auto& lib : libs) {
        const bool there = std::any_of(dirs.begin(), dirs.end(), [&](const auto& dir) {
            return std::filesystem::exists(dir / ("lib" + lib + ".so"), ec) ||
                   std::filesystem::exists(dir / ("lib" + lib + ".a"), ec);
        });
        if (!there && std::find(missing.begin(), missing.end(), lib) == missing.end()) missing.push_back(lib);
    }
    return missing;
}

int CMakeConfig::major() const {
    int value = 0;
    for (char c : version) {
        if (!std::isdigit(static_cast<unsigned char>(c))) break;
        value = value * 10 + (c - '0');
    }
    return value;
}

std::vector<std::filesystem::path> cmakePrefixes() {
    std::vector<std::filesystem::path> out;
    if (const char* env = std::getenv("CMAKE_PREFIX_PATH")) {
        std::istringstream in{std::string(env)};
        for (std::string dir; std::getline(in, dir, ':');)
            if (!dir.empty()) out.emplace_back(dir);
    }
    out.emplace_back("/usr/local");
    out.emplace_back("/usr");
    return out;
}

std::optional<CMakeConfig> findCMakeConfig(const std::string& name, const std::vector<std::filesystem::path>& prefixes) {
    namespace fs = std::filesystem;
    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char c) { return std::tolower(c); });
    std::error_code ec;
    for (const fs::path& prefix : prefixes) {
        std::vector<fs::path> libDirs{prefix / "lib"};
        // Debian's multiarch folders: lib/x86_64-linux-gnu.
        std::vector<fs::path> arch;
        for (fs::directory_iterator it(prefix / "lib", ec), end; !ec && it != end; it.increment(ec))
            if (it->is_directory(ec) && fs::exists(it->path() / "cmake", ec)) arch.push_back(it->path());
        std::sort(arch.begin(), arch.end());
        libDirs.insert(libDirs.end(), arch.begin(), arch.end());
        libDirs.push_back(prefix / "lib64");
        libDirs.push_back(prefix / "share");
        for (const fs::path& lib : libDirs) {
            const fs::path dir = lib / "cmake" / name;
            const bool camel = fs::exists(dir / (name + "Config.cmake"), ec);
            if (!camel && !fs::exists(dir / (lowerName + "-config.cmake"), ec)) continue;
            CMakeConfig config{name, {}, dir};
            std::ifstream in(dir / (camel ? name + "ConfigVersion.cmake" : lowerName + "-config-version.cmake"));
            for (std::string line; std::getline(in, line);) {
                // set(PACKAGE_VERSION "3.0.0")
                const auto at = line.find("set(PACKAGE_VERSION ");
                if (at == std::string::npos) continue;
                for (char c : line.substr(at + 20)) {
                    if (c == '"' && config.version.empty()) continue;
                    if (!std::isdigit(static_cast<unsigned char>(c)) && c != '.') break;
                    config.version += c;
                }
                break;
            }
            return config;
        }
    }
    return std::nullopt;
}

namespace sfml {

namespace {

// In the order SFML's own libraries depend on each other, most dependent first.
constexpr std::array<const char*, 5> kComponents{"Graphics", "Audio", "Network", "Window", "System"};

std::string lowered(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// "Graphics" for "graphics", "sfml-graphics" or "SFML::Graphics"; empty for anything else.
std::string componentNamed(const std::string& text) {
    std::string name = lowered(text);
    for (const char* prefix : {"sfml-", "sfml::"})
        if (name.starts_with(prefix)) name = name.substr(std::string_view(prefix).size());
    for (const char* c : kComponents)
        if (name == lowered(c)) return c;
    return {};
}

bool isSfml(const Package& p) {
    const std::string name = lowered(p.name);
    return p.source == Package::Source::System ? name.starts_with("sfml-") : name == "sfml";
}

void sortComponents(std::vector<std::string>& components) {
    std::vector<std::string> sorted;
    for (const char* c : kComponents)
        if (std::find(components.begin(), components.end(), c) != components.end()) sorted.emplace_back(c);
    components = std::move(sorted);
}

} // namespace

int Install::major() const {
    if (config && config->major() > 0) return config->major();
    if (!pkgConfigVersion.empty()) return pkgConfigVersion[0] == '2' ? 2 : 3;
    return 3;
}

Install detect() {
    Install install;
    install.config = findCMakeConfig("SFML", cmakePrefixes());
    install.pkgConfigVersion = runPkgConfig("--modversion", {"sfml-graphics"});
    return install;
}

std::vector<std::string> components(const std::vector<Package>& packages) {
    std::vector<std::string> out;
    auto add = [&](const std::string& text) {
        const std::string c = componentNamed(text);
        if (!c.empty() && std::find(out.begin(), out.end(), c) == out.end()) out.push_back(c);
    };
    for (const Package& p : packages) {
        if (!isSfml(p)) continue;
        if (p.source == Package::Source::System) {
            if (lowered(p.name) == "sfml-all")
                for (const char* c : kComponents) add(c);
            else add(p.name);
        } else {
            for (const auto& c : p.components) add(c);
            for (const auto& t : p.targets) add(t);
        }
    }
    sortComponents(out);
    return out;
}

std::vector<Package> packagesFor(const Install& install, std::vector<std::string> wanted) {
    for (auto& c : wanted) c = componentNamed(c);
    sortComponents(wanted);
    std::vector<std::string> targets;
    for (const auto& c : wanted) targets.push_back("SFML::" + c);

    if (install.config && install.config->major() >= 3) {
        Package p;
        p.source = Package::Source::CMake;
        p.name = "SFML";
        p.version = std::to_string(install.config->major());
        p.components = wanted;
        p.targets = targets;
        // An install with static libraries only: SFML's package looks for shared ones unless told.
        std::error_code ec;
        const auto& dir = install.config->dir;
        if (std::filesystem::exists(dir / "SFMLSystemStaticTargets.cmake", ec) &&
            !std::filesystem::exists(dir / "SFMLSystemSharedTargets.cmake", ec))
            p.options["SFML_STATIC_LIBRARIES"] = "ON";
        return {p};
    }
    if (!install.pkgConfigVersion.empty()) {
        std::vector<Package> out;
        for (const auto& c : wanted) out.push_back(Package{Package::Source::System, "sfml-" + lowered(c), {}, {}, {}, {}});
        return out;
    }
    Package p;
    p.source = Package::Source::Catalog;
    p.name = "SFML";
    p.gitUrl = "https://github.com/SFML/SFML.git";
    p.gitTag = "3.0.2";
    p.targets = targets;
    // Each part SFML isn't asked for needs development packages of its own to build.
    auto has = [&](const char* c) { return std::find(wanted.begin(), wanted.end(), c) != wanted.end(); };
    if (!has("Audio")) p.options["SFML_BUILD_AUDIO"] = "OFF";
    if (!has("Network")) p.options["SFML_BUILD_NETWORK"] = "OFF";
    return {p};
}

bool setPackages(std::vector<Package>& packages, const std::vector<Package>& wanted) {
    std::vector<Package> current;
    for (const Package& p : packages)
        if (isSfml(p)) current.push_back(p);
    if (current == wanted) return false;
    // The new ones go where the first old one was, so the build file changes as little as it can.
    const auto first = std::find_if(packages.begin(), packages.end(), isSfml);
    const auto at = first - packages.begin();
    std::erase_if(packages, isSfml);
    packages.insert(packages.begin() + std::min<std::ptrdiff_t>(at, packages.size()), wanted.begin(), wanted.end());
    return true;
}

bool needsCMakePackage(const std::vector<Package>& packages, const Install& install, std::string_view libsLine) {
    const bool viaPkgConfig = std::any_of(packages.begin(), packages.end(), [](const Package& p) {
        return p.source == Package::Source::System && isSfml(p);
    });
    return viaPkgConfig && install.config && install.config->major() >= 3 && !missingPkgConfigLibs(libsLine).empty();
}

} // namespace sfml

std::string targetDetectionProject(const std::string& name, const std::string& gitUrl, const std::string& gitTag) {
    auto quote = [](const std::string& s) {
        std::string out = "\"";
        for (char c : s) {
            if (c == '"' || c == '\\' || c == '$') out += '\\';
            out += c;
        }
        return out + "\"";
    };
    std::ostringstream out;
    out << "cmake_minimum_required(VERSION 3.20)\n"
           "project(kevc_detect LANGUAGES C CXX)\n"
           "set(CMAKE_POLICY_VERSION_MINIMUM 3.5)\n"
           "include(FetchContent)\n"
        << "FetchContent_Declare(" << quote(name) << " GIT_REPOSITORY " << quote(gitUrl) << " GIT_TAG "
        << quote(gitTag) << " GIT_SHALLOW TRUE)\n"
        << "FetchContent_MakeAvailable(" << quote(name) << ")\n"
        << R"cmake(
function(kevc_collect dir out)
  get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
  get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
  foreach(sub IN LISTS subdirs)
    kevc_collect("${sub}" more)
    list(APPEND targets ${more})
  endforeach()
  set(${out} ${targets} PARENT_SCOPE)
endfunction()

kevc_collect("${CMAKE_SOURCE_DIR}" all)
set(found "")
foreach(t IN LISTS all)
  get_target_property(type ${t} TYPE)
  if(type MATCHES "^(STATIC|SHARED|MODULE|INTERFACE|OBJECT)_LIBRARY$")
    list(APPEND found ${t})
    # Namespaced aliases are what projects are meant to link against, e.g. fmt::fmt.
    foreach(ns IN ITEMS )cmake" << quote(name) << R"cmake( ${t} ${PROJECT_NAME})
      if(TARGET ${ns}::${t})
        list(APPEND found ${ns}::${t})
      endif()
    endforeach()
  endif()
endforeach()
# Aliases whose namespace differs from the target name (GTest::gtest, SDL3::SDL3) can't be
# guessed, so read them from the package's CMake files and keep the ones that really exist.
string(TOLOWER )cmake" << quote(name) << R"cmake( lower)
file(GLOB_RECURSE cmake_files "${${lower}_SOURCE_DIR}/*CMakeLists.txt" "${${lower}_SOURCE_DIR}/*.cmake")
# Names may be built from variables, e.g. ${cmake_package_name}::${name}: the namespace is expanded
# from the current variables and a variable suffix is tried against every real library target.
set(libs ${found})
foreach(f IN LISTS cmake_files)
  file(STRINGS "${f}" lines REGEX "add_library *\\( *[A-Za-z0-9_.+\${}-]+::[A-Za-z0-9_.+\${}-]+ +ALIAS")
  foreach(l IN LISTS lines)
    string(REGEX MATCH "\\( *([A-Za-z0-9_.+\${}-]+)::([A-Za-z0-9_.+\${}-]+)" m "${l}")
    set(ns "${CMAKE_MATCH_1}")
    set(base "${CMAKE_MATCH_2}")
    string(CONFIGURE "${ns}" ns)
    if(ns STREQUAL "" OR ns MATCHES "[{}$]")
      continue()
    endif()
    if(base MATCHES "[{}$]")
      set(candidates ${libs})
    else()
      set(candidates ${base})
    endif()
    foreach(b IN LISTS candidates)
      if(TARGET ${ns}::${b})
        list(APPEND found ${ns}::${b})
      endif()
    endforeach()
  endforeach()
endforeach()
list(REMOVE_DUPLICATES found)
foreach(t IN LISTS found)
  message(STATUS "KEVC_TARGET ${t}")
endforeach()
)cmake";
    return out.str();
}

std::uintmax_t directorySize(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    std::uintmax_t total = 0;
    std::error_code ec;
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code statEc;
        if (it->is_regular_file(statEc) && !it->is_symlink(statEc)) {
            const auto size = it->file_size(statEc);
            if (!statEc) total += size;
        }
    }
    return total;
}

std::string humanSize(std::uintmax_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    static const char* units[] = {"KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes) / 1024.0;
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f %s", value, units[unit]);
    return buf;
}

std::optional<std::string> parseDetectedTarget(std::string_view line) {
    constexpr std::string_view marker = "KEVC_TARGET ";
    const auto pos = line.find(marker);
    if (pos == std::string_view::npos) return std::nullopt;
    std::string target(line.substr(pos + marker.size()));
    while (!target.empty() && std::isspace(static_cast<unsigned char>(target.back()))) target.pop_back();
    if (target.empty()) return std::nullopt;
    return target;
}

} // namespace kevc
