#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kevc::npm {

// ---- What a dependency's version in package.json allows, in plain words.

// The update rule of a version range from the registry: "5.1.0" (Locked), "~5.1.0" (Fixes),
// "^5.1.0" (Features), ">=5.1.0" (Newer), "*" (Any), and anything else as written (Custom).
enum class Updates { Locked, Fixes, Features, Newer, Any, Custom };
// Where a dependency comes from: a registry version or range, a dist-tag ("latest"), or elsewhere.
enum class Source { Registry, Tag, Git, Local, Workspace, Url, Alias };

struct VersionSpec {
    Source source = Source::Registry;
    Updates updates = Updates::Custom;
    std::string version; // the version a registry range starts from ("5.1.0"); empty if none
    std::string raw;     // as package.json has it
};

VersionSpec parseSpec(std::string_view spec);
// The spec for an update rule and a version: (Features, "5.1.0") -> "^5.1.0". Custom gives `version`.
std::string makeSpec(Updates updates, const std::string& version);
// "5.1.0 up to, not including, 6.0.0" and the like (see the tests).
std::string describe(const VersionSpec& spec);
// The choices' names, as the Packages dialog lists them.
const char* updatesLabel(Updates updates);
bool isVersion(std::string_view text); // 1.2.3, optionally -pre/+build
// Compares two plain versions (prerelease ignored): <0, 0, >0. Unparsable ones compare equal.
int compareVersions(std::string_view a, std::string_view b);

// ---- package.json

struct Dependency {
    std::string name;
    std::string section;   // "dependencies", "devDependencies", "optionalDependencies" or "peerDependencies"
    std::string spec;      // as package.json has it
    std::string installed; // version in node_modules/<name>/package.json; empty if not installed
};
// Every dependency, runtime ones first, each section in package.json's order.
std::vector<Dependency> readDependencies(const std::filesystem::path& root);

// Edits that leave the rest of package.json exactly as written (formatting, key order, other
// fields). nullopt when the text isn't a JSON object or the section isn't an object.
// A new name goes in alphabetical order, as npm itself writes them; a new section at the end.
std::optional<std::string> setDependency(std::string_view packageJson, const std::string& section,
                                         const std::string& name, const std::string& spec);
std::optional<std::string> removeDependency(std::string_view packageJson, const std::string& section,
                                            const std::string& name);

// ---- .npmrc: where packages come from

struct Registries {
    std::string main;                                        // registry=…; empty: npm's public one
    std::vector<std::pair<std::string, std::string>> scoped; // "@acme" -> URL (@acme:registry=…)
    bool operator==(const Registries&) const = default;
};
Registries readRegistries(std::string_view npmrc);
// `npmrc` with its registry lines set to `registries` (added, changed or removed); every other line
// (auth settings, comments) is kept as it was.
std::string writeRegistries(std::string_view npmrc, const Registries& registries);
// Why a registry address won't do ("" if it's fine): it has to be an http(s) URL.
std::string checkRegistryUrl(std::string_view url);
// With the trailing slash npm expects.
std::string normalRegistryUrl(std::string_view url);

} // namespace kevc::npm
