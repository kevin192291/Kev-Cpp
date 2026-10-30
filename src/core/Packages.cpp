#include "core/Packages.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
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
