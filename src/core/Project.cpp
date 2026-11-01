#include "core/Project.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace kevc {

using json = nlohmann::json;

NLOHMANN_JSON_SERIALIZE_ENUM(ProjectKind, {
    {ProjectKind::Cpp, "cpp-cmake"},
    {ProjectKind::C, "c-make"},
    {ProjectKind::WebTs, "web-ts"},
})

NLOHMANN_JSON_SERIALIZE_ENUM(TargetType, {
    {TargetType::Console, "console"},
    {TargetType::Static, "static"},
    {TargetType::Shared, "shared"},
})

NLOHMANN_JSON_SERIALIZE_ENUM(Package::Source, {
    {Package::Source::Catalog, "catalog"},
    {Package::Source::Git, "git"},
    {Package::Source::System, "system"},
})

void to_json(json& j, const Package& p) {
    j = json{{"source", p.source}, {"name", p.name}, {"targets", p.targets}};
    if (!p.options.empty()) j["options"] = p.options;
    if (p.source != Package::Source::System) {
        j["gitUrl"] = p.gitUrl;
        j["gitTag"] = p.gitTag;
    }
}

void from_json(const json& j, Package& p) {
    p.source = j.value("source", Package::Source::Catalog);
    p.name = j.at("name").get<std::string>();
    p.gitUrl = j.value("gitUrl", "");
    p.gitTag = j.value("gitTag", "");
    p.targets = j.value("targets", std::vector<std::string>{});
    p.options = j.value("options", std::map<std::string, std::string>{});
}

namespace {

constexpr int kFormatVersion = 1;

constexpr std::array kCppExtensions{".cpp", ".cc", ".cxx", ".c++", ".c", ".h", ".hpp", ".hh", ".hxx", ".inl", ".tpp"};
constexpr std::array kCExtensions{".c", ".h"};
constexpr std::array kWebExtensions{".ts", ".tsx", ".js", ".mjs", ".jsx", ".html", ".css", ".svelte"};

template <typename Array>
bool hasExtension(const Array& exts, const std::string& ext) {
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

} // namespace

nlohmann::json uiToJson(const ProjectUi& ui) {
    json j = json::object();
    if (!ui.toolbars.empty()) j["toolbars"] = ui.toolbars;
    if (!ui.panels.empty()) j["panels"] = ui.panels;
    if (!ui.buildConfig.empty()) j["buildConfig"] = ui.buildConfig;
    return j;
}

ProjectUi uiFromJson(const nlohmann::json& j) {
    ProjectUi ui;
    if (!j.is_object()) return ui;
    for (const char* key : {"toolbars", "panels"}) {
        const auto it = j.find(key);
        if (it == j.end() || !it->is_object()) continue;
        auto& out = std::string(key) == "toolbars" ? ui.toolbars : ui.panels;
        for (const auto& [id, shown] : it->items())
            if (shown.is_boolean()) out[id] = shown.get<bool>();
    }
    const std::string config = j.value("buildConfig", "");
    if (config == "Debug" || config == "Release") ui.buildConfig = config;
    return ui;
}

Project Project::load(const std::filesystem::path& projectFile) {
    std::ifstream in(projectFile);
    if (!in) throw std::runtime_error("cannot open " + projectFile.string());
    json j = json::parse(in);

    Project p;
    p.name = j.at("name").get<std::string>();
    p.kind = j.at("kind").get<ProjectKind>();
    p.targetType = j.value("targetType", TargetType::Console);
    p.standard = j.value("standard", p.kind == ProjectKind::C ? "17" : "20");
    p.outputName = j.value("outputName", "");
    p.ownsTarget = j.value("ownsTarget", true);
    p.buildFileManaged = j.value("buildFileManaged", true);
    p.targetName = j.value("targetName", sanitizeTargetName(p.name));
    p.sources = j.value("sources", std::vector<std::string>{});
    p.excludedSources = j.value("excludedSources", std::vector<std::string>{});

    const json compiler = j.value("compiler", json::object());
    p.defines = compiler.value("defines", std::vector<std::string>{});
    p.includeDirs = compiler.value("includeDirs", std::vector<std::string>{});
    p.compilerFlags = compiler.value("flags", std::vector<std::string>{});
    p.debugOptimization = compiler.value("debugOptimization", "-O0");
    p.releaseOptimization = compiler.value("releaseOptimization", "-O2");

    const json linker = j.value("linker", json::object());
    p.linkLibraries = linker.value("libraries", std::vector<std::string>{});
    p.linkDirs = linker.value("dirs", std::vector<std::string>{});
    p.linkerFlags = linker.value("flags", std::vector<std::string>{});
    p.staticRuntime = linker.value("staticRuntime", false);

    p.packages = j.value("packages", std::vector<Package>{});
    p.ui = uiFromJson(j.value("ui", json::object()));

    static const std::array known{"kevcFormat", "name", "kind", "targetType", "standard", "outputName",
                                  "ownsTarget", "buildFileManaged", "targetName", "sources", "excludedSources",
                                  "compiler", "linker", "packages", "ui"};
    for (const auto& [key, value] : j.items())
        if (std::find(known.begin(), known.end(), key) == known.end()) p.extra[key] = value.dump();
    return p;
}

void Project::save(const std::filesystem::path& projectFile) const {
    json j = {
        {"kevcFormat", kFormatVersion},
        {"name", name},
        {"kind", kind},
        {"targetType", targetType},
        {"standard", standard},
        {"outputName", outputName},
        {"ownsTarget", ownsTarget},
        {"targetName", targetName},
        {"sources", sources},
        {"excludedSources", excludedSources},
        {"compiler",
         {{"defines", defines},
          {"includeDirs", includeDirs},
          {"flags", compilerFlags},
          {"debugOptimization", debugOptimization},
          {"releaseOptimization", releaseOptimization}}},
        {"linker",
         {{"libraries", linkLibraries},
          {"dirs", linkDirs},
          {"flags", linkerFlags},
          {"staticRuntime", staticRuntime}}},
        {"packages", packages},
    };
    if (!buildFileManaged) j["buildFileManaged"] = false;
    if (!ui.empty()) j["ui"] = uiToJson(ui);
    for (const auto& [key, text] : extra)
        if (!j.contains(key)) j[key] = json::parse(text, nullptr, false);
    // Write to a temp file and rename so a crash never leaves a half-written project file.
    std::filesystem::path tmp = projectFile;
    tmp += ".tmp";
    {
        std::ofstream out(tmp);
        if (!out) throw std::runtime_error("cannot write " + tmp.string());
        out << j.dump(2) << '\n';
    }
    std::filesystem::rename(tmp, projectFile);
}

bool Project::tracksFile(const std::filesystem::path& relPath) const {
    std::string ext = relPath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    switch (kind) {
    case ProjectKind::Cpp: return hasExtension(kCppExtensions, ext);
    case ProjectKind::C: return hasExtension(kCExtensions, ext);
    case ProjectKind::WebTs: return hasExtension(kWebExtensions, ext);
    }
    return false;
}

bool Project::setSources(std::vector<std::string> found) {
    std::erase_if(found, [this](const std::string& s) {
        return std::find(excludedSources.begin(), excludedSources.end(), s) != excludedSources.end();
    });
    std::sort(found.begin(), found.end());
    found.erase(std::unique(found.begin(), found.end()), found.end());
    if (found == sources) return false;
    sources = std::move(found);
    return true;
}

std::filesystem::path Project::buildFileName() const {
    switch (kind) {
    case ProjectKind::Cpp: return "CMakeLists.txt";
    case ProjectKind::C: return "Makefile";
    case ProjectKind::WebTs: return {};
    }
    return {};
}

std::string toString(ProjectKind kind) {
    return json(kind).get<std::string>();
}

std::string toString(TargetType type) {
    return json(type).get<std::string>();
}

std::string sanitizeTargetName(const std::string& name) {
    std::string out;
    for (unsigned char c : name) {
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
        else if (!out.empty() && out.back() != '_') out += '_';
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out.empty()) out = "app";
    if (std::isdigit(static_cast<unsigned char>(out.front()))) out = "app_" + out;
    return out;
}

} // namespace kevc
