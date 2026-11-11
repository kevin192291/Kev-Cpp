#include "ui/ProjectOptionsDialog.h"

#include "ui/Dialogs.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace kevc {

namespace {

constexpr const char* kCatalogResource = "/org/kevcpp/KevCpp/packages.json";

// Where "Detect Targets" clones repositories: ~/.cache/kev-cpp/detect
fs::path detectCacheDir() {
    return fs::path(Glib::get_user_cache_dir()) / "kev-cpp" / "detect";
}

std::string tildePath(const fs::path& p) {
    const std::string home = Glib::get_home_dir();
    const std::string s = p.string();
    return s.starts_with(home + "/") ? "~" + s.substr(home.size()) : s;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::vector<std::string> readLines(Gtk::TextView& view) {
    std::vector<std::string> out;
    std::istringstream in(std::string(view.get_buffer()->get_text()));
    for (std::string line; std::getline(in, line);) {
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos) continue;
        out.push_back(line.substr(first, line.find_last_not_of(" \t\r") - first + 1));
    }
    return out;
}

std::string joinLines(const std::vector<std::string>& items) {
    std::string out;
    for (const auto& item : items) out += item + "\n";
    return out;
}

Gtk::Label& hint(const Glib::ustring& text) {
    auto* label = Gtk::make_managed<Gtk::Label>(text);
    label->set_xalign(0.0f);
    label->set_wrap(true);
    label->add_css_class("dim-label");
    return *label;
}

// Fills a drop-down from the design with `labels`, `selected` chosen.
void setChoices(Gtk::DropDown& dropDown, const std::vector<Glib::ustring>& labels, size_t selected) {
    dropDown.set_model(Gtk::StringList::create(labels));
    dropDown.set_selected(static_cast<guint>(selected));
}

// Index of `value` in `values`, appending it (as a custom choice) if missing.
size_t indexOrAppend(std::vector<std::string>& values, std::vector<Glib::ustring>& labels, const std::string& value) {
    auto it = std::find(values.begin(), values.end(), value);
    if (it != values.end()) return static_cast<size_t>(it - values.begin());
    values.push_back(value);
    labels.push_back(value + " (custom)");
    return values.size() - 1;
}

std::string packageSummary(const Package& p) {
    std::string targets;
    for (const auto& t : p.targets) targets += (targets.empty() ? "" : ", ") + t;
    switch (p.source) {
    case Package::Source::System: return "System library via pkg-config";
    case Package::Source::CMake:
        return "Installed library via its CMake package (find_package)" + (targets.empty() ? "" : " · links " + targets);
    case Package::Source::Catalog: return "Catalog, " + p.gitTag + " · links " + targets;
    case Package::Source::Git: return p.gitUrl + " @ " + p.gitTag + " · links " + targets;
    }
    return {};
}

} // namespace

ProjectOptionsDialog::ProjectOptionsDialog(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder,
                                           Gtk::Window& parent, const Project& project, const Toolchain& toolchain,
                                           Page start, ApplyHandler onApply)
    : ProjectOptionsDialogUi(object, builder), project_(project), toolchain_(toolchain), onApply_(std::move(onApply)) {
    set_title("Project Options: " + project.name);
    set_transient_for(parent);

    try {
        auto bytes = Gio::Resource::lookup_data_global(kCatalogResource);
        gsize size = 0;
        const auto* data = static_cast<const char*>(bytes->get_data(size));
        catalog_ = parseCatalog(std::string_view(data, size));
    } catch (const std::exception&) {
        // Covers Glib::Error too. An empty catalog still leaves Git and System packages usable.
    }

    fillGeneral();
    fillCompiler();
    fillLinker();
    fillPackages();
    notebook_->set_current_page(static_cast<int>(start));

    detector_.signalLine().connect([this](const std::string& line, bool) {
        if (auto t = parseDetectedTarget(line)) detected_.push_back(*t);
        else if (!line.empty()) gitStatus_->set_text(line);
    });
    detector_.signalFinished().connect([this](bool success) {
        detect_->set_sensitive(true);
        cleanCache_->set_sensitive(true);
        refreshCacheSize();
        if (!success || detected_.empty()) {
            gitStatus_->set_text(success ? "No library targets found. Enter the targets to link by hand."
                                         : "Fetching or configuring failed. Check the URL and tag. Last output: " +
                                               gitStatus_->get_text());
            return;
        }
        // Suggest <name>::<name>, else any namespaced target, else the first library.
        const std::string name = lower(gitName_->get_text());
        auto pick = std::find_if(detected_.begin(), detected_.end(), [&](const std::string& t) {
            const auto sep = t.find("::");
            return sep != std::string::npos && lower(t.substr(sep + 2)) == name;
        });
        if (pick == detected_.end())
            pick = std::find_if(detected_.begin(), detected_.end(),
                                [](const std::string& t) { return t.find("::") != std::string::npos; });
        if (pick == detected_.end()) pick = detected_.begin();
        gitTargets_->set_text(*pick);
        std::string all;
        for (const auto& t : detected_) all += (all.empty() ? "" : ", ") + t;
        gitStatus_->set_text("Found: " + all);
    });

    closeOnEscape(*this);
    deleteWhenHidden(this);
}

// ---------------------------------------------------------------------------------------------

void ProjectOptionsDialog::fillGeneral() {
    const bool isC = project_.kind == ProjectKind::C;
    projectName_->set_text(project_.name);
    projectKind_->set_text(isC ? "C, built with Make and gcc" : "C++, built with CMake and g++");

    setChoices(*targetType_, {"Console application", "Static library (.a)", "Shared library (.so)"},
               static_cast<size_t>(project_.targetType));
    if (!project_.ownsTarget) {
        targetType_->set_sensitive(false);
        targetType_->set_tooltip_text("This project's target is defined in your own CMake code.");
    }

    std::vector<Glib::ustring> labels;
    if (isC) {
        standards_ = {"99", "11", "17", "2x"};
        labels = {"C99", "C11", "C17", "C2x (C23 draft)"};
    } else {
        standards_ = {"11", "14", "17", "20", "23"};
        labels = {"C++11", "C++14", "C++17", "C++20", "C++23"};
    }
    const size_t current = indexOrAppend(standards_, labels, project_.standard);
    setChoices(*standard_, labels, current);
    if (!project_.ownsTarget && !isC) {
        standard_->set_sensitive(false);
        standard_->set_tooltip_text("Set the standard in your own CMake code for adopted projects.");
    }

    outputName_->set_text(project_.outputName);
    outputName_->set_placeholder_text(project_.targetName);
    generalHint_->set_text(isC ? "The Makefile's managed block is regenerated when you press OK."
                               : "The managed block in CMakeLists.txt is regenerated when you press OK. "
                                 "Anything you wrote outside the block is left alone.");
}

void ProjectOptionsDialog::fillCompiler() {
    warningFlags_ = {{"-Wall", wall_}, {"-Wextra", wextra_}, {"-Wpedantic", wpedantic_}, {"-Werror", werror_}};
    std::vector<std::string> rest;
    for (const auto& [flag, check] : warningFlags_)
        check->set_active(std::find(project_.compilerFlags.begin(), project_.compilerFlags.end(), flag) !=
                          project_.compilerFlags.end());
    for (const auto& f : project_.compilerFlags)
        if (std::none_of(warningFlags_.begin(), warningFlags_.end(), [&](const auto& k) { return k.first == f; }))
            rest.push_back(f);

    debugOpts_ = {"-O0", "-Og", "-O1", ""};
    std::vector<Glib::ustring> debugLabels = {"-O0 (best debugging)", "-Og", "-O1", "Compiler default"};
    const size_t debug = indexOrAppend(debugOpts_, debugLabels, project_.debugOptimization);
    setChoices(*debugOpt_, debugLabels, debug);
    releaseOpts_ = {"-O1", "-O2", "-O3", "-Os", ""};
    std::vector<Glib::ustring> releaseLabels = {"-O1", "-O2", "-O3 (fastest)", "-Os (smallest)", "Compiler default"};
    const size_t release = indexOrAppend(releaseOpts_, releaseLabels, project_.releaseOptimization);
    setChoices(*releaseOpt_, releaseLabels, release);

    extraFlags_->set_text(joinArgs(rest));
    defines_->get_buffer()->set_text(joinLines(project_.defines));
    includeDirs_->get_buffer()->set_text(joinLines(project_.includeDirs));
}

void ProjectOptionsDialog::fillLinker() {
    libraries_->get_buffer()->set_text(joinLines(project_.linkLibraries));
    libraryDirs_->get_buffer()->set_text(joinLines(project_.linkDirs));
    linkerFlags_->set_text(joinArgs(project_.linkerFlags));
    staticRuntime_->set_active(project_.staticRuntime);
}

void ProjectOptionsDialog::fillPackages() {
    refreshInstalled();
    systemList_->set_filter_func([this](Gtk::ListBoxRow* row) {
        const std::string needle = lower(systemSearch_->get_text());
        return needle.empty() || lower(row->get_child()->get_name()).find(needle) != std::string::npos;
    });
    if (project_.kind == ProjectKind::C) {
        // C projects use the system's libraries only: the System page, without the switcher or the
        // cache of downloaded repositories.
        addHeading_->set_text("Add a system library");
        packagesHint_->set_text("C projects use libraries installed on your system through pkg-config "
                                "(for example after `sudo apt install libcurl4-openssl-dev`).");
        switcher_->set_visible(false);
        cacheSeparator_->set_visible(false);
        cacheRow_->set_visible(false);
        sources_->set_visible_child("system");
        loadSystemModules();
        return;
    }
    const ToolInfo* git = toolchain_.get("git");
    packagesHint_->set_visible(!git || !git->found()); // the design's text says git is missing
    fillCatalog();
    refreshCacheSize();
}

void ProjectOptionsDialog::fillCatalog() {
    for (const CatalogEntry& entry : catalog_) {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        row->set_margin(6);
        auto* text = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        text->set_hexpand(true);
        auto* title = Gtk::make_managed<Gtk::Label>();
        title->set_markup("<b>" + Glib::Markup::escape_text(entry.name) + "</b>  <small>" +
                          Glib::Markup::escape_text(entry.gitTag + " · " + entry.category) + "</small>");
        title->set_xalign(0.0f);
        text->append(*title);
        text->append(hint(entry.description));
        if (!entry.include.empty()) {
            auto* include = Gtk::make_managed<Gtk::Label>("#include " + entry.include);
            include->set_xalign(0.0f);
            include->set_selectable(true);
            include->add_css_class("kevc-log");
            text->append(*include);
        }
        if (!entry.notes.empty()) text->append(hint(entry.notes));
        auto* add = Gtk::make_managed<Gtk::Button>("Add");
        add->set_valign(Gtk::Align::CENTER);
        add->signal_clicked().connect([this, entry] { addPackage(entry.toPackage()); });
        catalogButtons_.emplace_back(entry.name, add);
        row->append(*text);
        row->append(*add);
        catalogList_->append(*row);
    }
    refreshAddButtons();
}

void ProjectOptionsDialog::on_sources_page_changed() {
    if (sources_->get_visible_child_name() == "system") loadSystemModules();
}

void ProjectOptionsDialog::on_git_url_changed() {
    // Suggest a name from the repository URL: .../fmt.git -> fmt
    std::string url = gitUrl_->get_text();
    while (!url.empty() && url.back() == '/') url.pop_back();
    std::string base = url.substr(url.find_last_of("/:") + 1);
    if (base.ends_with(".git")) base.resize(base.size() - 4);
    gitName_->set_text(base);
}

void ProjectOptionsDialog::on_system_search_changed() {
    systemList_->invalidate_filter();
}

void ProjectOptionsDialog::on_cancel_clicked() {
    set_visible(false);
}

void ProjectOptionsDialog::on_ok_clicked() {
    apply();
}

void ProjectOptionsDialog::loadSystemModules() {
    if (systemLoaded_) return;
    systemLoaded_ = true;
    for (const PkgConfigModule& m : listPkgConfigModules()) {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        row->set_name(m.name + " " + m.description); // searched by the filter
        row->set_margin(4);
        auto* text = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        text->set_hexpand(true);
        auto* title = Gtk::make_managed<Gtk::Label>();
        title->set_markup("<b>" + Glib::Markup::escape_text(m.name) + "</b>");
        title->set_xalign(0.0f);
        text->append(*title);
        if (!m.description.empty()) text->append(hint(m.description));
        auto* add = Gtk::make_managed<Gtk::Button>("Add");
        add->set_valign(Gtk::Align::CENTER);
        add->signal_clicked().connect([this, name = m.name] {
            addPackage(Package{Package::Source::System, name, {}, {}, {}, {}});
        });
        systemButtons_.emplace_back(m.name, add);
        row->append(*text);
        row->append(*add);
        systemList_->append(*row);
    }
    if (!systemList_->get_row_at_index(0)) systemList_->append(hint("pkg-config found no installed libraries."));
    refreshAddButtons();
}

// ---------------------------------------------------------------------------------------------

void ProjectOptionsDialog::addPackage(Package package) {
    const bool exists = std::any_of(project_.packages.begin(), project_.packages.end(), [&](const Package& p) {
        return lower(p.name) == lower(package.name) && p.source == package.source;
    });
    if (!exists) project_.packages.push_back(std::move(package));
    refreshInstalled();
    refreshAddButtons();
}

void ProjectOptionsDialog::refreshInstalled() {
    while (auto* row = installed_->get_row_at_index(0)) installed_->remove(*row);
    for (size_t i = 0; i < project_.packages.size(); ++i) {
        const Package& p = project_.packages[i];
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        row->set_margin(6);
        auto* text = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        text->set_hexpand(true);
        auto* title = Gtk::make_managed<Gtk::Label>();
        title->set_markup("<b>" + Glib::Markup::escape_text(p.name) + "</b>");
        title->set_xalign(0.0f);
        text->append(*title);
        text->append(hint(packageSummary(p)));
        auto* remove = Gtk::make_managed<Gtk::Button>();
        remove->set_icon_name("user-trash-symbolic");
        remove->set_tooltip_text("Remove from project");
        remove->set_valign(Gtk::Align::CENTER);
        remove->signal_clicked().connect([this, name = p.name, source = p.source] {
            std::erase_if(project_.packages, [&](const Package& q) { return q.name == name && q.source == source; });
            // Defer: this button is being destroyed by the refresh.
            Glib::signal_idle().connect_once([this] {
                refreshInstalled();
                refreshAddButtons();
            });
        });
        row->append(*text);
        row->append(*remove);
        installed_->append(*row);
    }
    const bool empty = project_.packages.empty();
    installedScroller_->set_visible(!empty);
    installedEmpty_->set_visible(empty);
}

void ProjectOptionsDialog::refreshAddButtons() {
    auto refresh = [this](auto& buttons, bool system) {
        for (auto& [name, button] : buttons) {
            const bool added = std::any_of(project_.packages.begin(), project_.packages.end(), [&](const Package& p) {
                // One found through its own CMake package (SFML) is neither list's.
                return lower(p.name) == lower(name) && p.source != Package::Source::CMake &&
                       (p.source == Package::Source::System) == system;
            });
            button->set_sensitive(!added);
            button->set_label(added ? "Added" : "Add");
        }
    };
    refresh(catalogButtons_, false);
    refresh(systemButtons_, true);
}

void ProjectOptionsDialog::on_detect_clicked() {
    const std::string url = gitUrl_->get_text();
    const std::string tag = gitTag_->get_text();
    const std::string name = gitName_->get_text();
    if (url.empty() || tag.empty() || name.empty()) {
        gitStatus_->set_text("Enter the repository URL, a tag or branch, and a name first.");
        return;
    }
    const fs::path dir = detectCacheDir() / sanitizeTargetName(name);
    std::error_code ec;
    fs::remove_all(dir / "build", ec);
    fs::create_directories(dir, ec);
    std::ofstream(dir / "CMakeLists.txt") << targetDetectionProject(name, url, tag);

    detected_.clear();
    detect_->set_sensitive(false);
    cleanCache_->set_sensitive(false); // don't delete the clone while it's being made
    gitStatus_->set_text("Fetching " + url + " (this can take a minute)…");
    detector_.start({{{toolchain_.pathOf("cmake").string(), "-S", dir.string(), "-B", (dir / "build").string(),
                       "-DCMAKE_C_COMPILER=" + toolchain_.pathOf("gcc").string(),
                       "-DCMAKE_CXX_COMPILER=" + toolchain_.pathOf("g++").string()},
                      dir}});
}

void ProjectOptionsDialog::refreshCacheSize() {
    const fs::path dir = detectCacheDir();
    const auto bytes = directorySize(dir);
    cacheLabel_->set_text("Package cache: " + humanSize(bytes) + " in " + tildePath(dir) + (bytes ? "" : " (empty)"));
    if (!detector_.running()) cleanCache_->set_sensitive(bytes > 0);
}

void ProjectOptionsDialog::on_clean_cache_clicked() {
    if (detector_.running()) return;
    const fs::path dir = detectCacheDir();
    const auto before = directorySize(dir);
    std::error_code ec;
    fs::remove_all(dir, ec);
    refreshCacheSize();
    cacheLabel_->set_text(ec ? "Could not clean the package cache: " + ec.message()
                             : "Freed " + humanSize(before) + ". " + std::string(cacheLabel_->get_text()));
}

void ProjectOptionsDialog::on_add_git_clicked() {
    Package p;
    p.source = Package::Source::Git;
    p.gitUrl = gitUrl_->get_text();
    p.gitTag = gitTag_->get_text();
    p.name = gitName_->get_text();
    p.targets = splitArgs(std::string(gitTargets_->get_text()));
    for (auto& t : p.targets)
        if (!t.empty() && t.back() == ',') t.pop_back();
    if (p.gitUrl.empty() || p.gitTag.empty() || p.name.empty() || p.targets.empty()) {
        gitStatus_->set_text("Fill in the URL, tag, name and at least one target to link.");
        return;
    }
    addPackage(std::move(p));
    gitStatus_->set_text("Added " + std::string(gitName_->get_text()) + ".");
}

void ProjectOptionsDialog::apply() {
    project_.targetType = static_cast<TargetType>(targetType_->get_selected());
    project_.standard = standards_[standard_->get_selected()];
    project_.outputName = outputName_->get_text();

    std::vector<std::string> flags;
    for (const auto& [flag, check] : warningFlags_)
        if (check->get_active()) flags.push_back(flag);
    for (auto& f : splitArgs(std::string(extraFlags_->get_text()))) flags.push_back(std::move(f));
    project_.compilerFlags = std::move(flags);
    project_.debugOptimization = debugOpts_[debugOpt_->get_selected()];
    project_.releaseOptimization = releaseOpts_[releaseOpt_->get_selected()];
    project_.defines = readLines(*defines_);
    project_.includeDirs = readLines(*includeDirs_);

    project_.linkLibraries = readLines(*libraries_);
    project_.linkDirs = readLines(*libraryDirs_);
    project_.linkerFlags = splitArgs(std::string(linkerFlags_->get_text()));
    project_.staticRuntime = staticRuntime_->get_active();

    onApply_(project_);
    set_visible(false);
}

} // namespace kevc
