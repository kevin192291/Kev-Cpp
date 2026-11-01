#include "ui/NpmPackagesDialog.h"

#include "process/Capture.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace kevc {

namespace {

using npm::Source;
using npm::Updates;

// The update rules the drop-down offers, in its order (Locked is the Lock box).
constexpr Updates kRules[] = {Updates::Fixes, Updates::Features, Updates::Newer, Updates::Any};

Glib::ustring esc(const std::string& s) {
    return Glib::Markup::escape_text(s);
}

Gtk::Label& heading(const Glib::ustring& text) {
    auto* label = Gtk::make_managed<Gtk::Label>();
    label->set_markup("<b>" + esc(text) + "</b>");
    label->set_xalign(0.0f);
    return *label;
}

Gtk::Label& note(const Glib::ustring& text) {
    auto* label = Gtk::make_managed<Gtk::Label>(text);
    label->add_css_class("dim-label");
    label->set_xalign(0.0f);
    label->set_wrap(true);
    return *label;
}

std::string readFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::string trimmed(const Glib::ustring& text) {
    std::string s = text;
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

// npm's rules for names: lower case, URL-safe, optionally @scope/name.
bool validName(const std::string& name) {
    if (name.empty() || name.size() > 214 || name[0] == '.' || name[0] == '_') return false;
    size_t slashes = 0;
    for (size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (c == '/') ++slashes;
        else if (c == '@' && i == 0) continue;
        else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~'))
            return false;
    }
    return name[0] == '@' ? slashes == 1 && name.back() != '/' : slashes == 0;
}

// `npm view … version --json` prints "1.2.3", or a list when a range matches several.
std::string lastVersion(const std::string& output) {
    try {
        const auto j = nlohmann::json::parse(output);
        if (j.is_string()) return j.get<std::string>();
        if (j.is_array() && !j.empty() && j.back().is_string()) return j.back().get<std::string>();
    } catch (const std::exception&) {
    }
    return {};
}

} // namespace

NpmPackagesDialog::NpmPackagesDialog(Gtk::Window& parent, fs::path root, std::string packageManager)
    : root_(std::move(root)), packageManager_(std::move(packageManager)) {
    set_title("Packages — " + root_.filename().string());
    set_transient_for(parent);
    // Not modal: Log In… asks for the password in the main window's Run tab.
    set_default_size(1000, 700);
    signal_hide().connect([this] { Glib::signal_idle().connect_once([this] { delete this; }); });

    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
    box->set_margin(16);
    box->append(buildSource());
    box->append(*Gtk::make_managed<Gtk::Separator>());
    box->append(buildPackages());
    box->append(buildAdd());
    box->append(note("Lock = always install exactly this version. Otherwise package.json says how far updates may go: "
                     "~ bug fixes only, ^ new features and fixes (npm's usual choice; nothing that should break your "
                     "code), >= anything newer, * any version. What's actually installed is written to the lockfile "
                     "(package-lock.json, pnpm-lock.yaml…) until you install or update again."));
    error_.add_css_class("error");
    error_.set_xalign(0.0f);
    error_.set_wrap(true);
    error_.set_visible(false);
    box->append(error_);

    auto* buttons = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    buttons->set_halign(Gtk::Align::END);
    auto* cancel = Gtk::make_managed<Gtk::Button>("Cancel");
    cancel->signal_clicked().connect([this] { set_visible(false); });
    auto* save = Gtk::make_managed<Gtk::Button>("Save");
    save->set_tooltip_text("Write package.json and .npmrc; install later (Project → Install Dependencies)");
    save->signal_clicked().connect([this] { this->save(false); });
    auto* install = Gtk::make_managed<Gtk::Button>("Save and Install");
    install->add_css_class("suggested-action");
    install->set_tooltip_text("Write package.json and .npmrc, then run " + packageManager_ + " install");
    install->signal_clicked().connect([this] { this->save(true); });
    buttons->append(*cancel);
    buttons->append(*save);
    buttons->append(*install);
    box->append(*buttons);
    set_child(*box);
}

// ---------------------------------------------------------------------------------------------
// Where packages come from

Gtk::Widget& NpmPackagesDialog::buildSource() {
    npmrcText_ = readFile(root_ / ".npmrc");
    savedRegistries_ = npm::readRegistries(npmrcText_);

    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    box->append(heading("Where packages come from"));
    auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto* label = Gtk::make_managed<Gtk::Label>("Package source:");
    source_.set_placeholder_text("npm's public registry (https://registry.npmjs.org/)");
    source_.set_text(savedRegistries_.main);
    source_.set_hexpand(true);
    auto* login = Gtk::make_managed<Gtk::Button>("Log In…");
    login->set_tooltip_text("Sign in to this source (runs npm login in the Run tab; your token is kept in ~/.npmrc)");
    login->signal_clicked().connect([this] { login_.emit(npm::normalRegistryUrl(trimmed(source_.get_text()))); });
    row->append(*label);
    row->append(source_);
    row->append(*login);
    box->append(*row);
    box->append(note("To use your company's own npm server (for private packages, or so every download goes through it), "
                     "enter its address here. It's saved in this project's .npmrc, which npm, pnpm, yarn and bun all "
                     "read, so everyone building the project uses it. Passwords and tokens are never saved there: "
                     "Log In… keeps yours in your own ~/.npmrc."));

    auto* expander = Gtk::make_managed<Gtk::Expander>("Packages with a source of their own (@company/…)");
    auto* inner = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    inner->set_margin_start(18);
    inner->set_margin_top(6);
    inner->append(note("Packages whose names start with a scope, such as @company/ui, can come from another server "
                       "than the rest."));
    inner->append(scopeList_);
    auto* add = Gtk::make_managed<Gtk::Button>("Add a Scope");
    add->set_halign(Gtk::Align::START);
    add->signal_clicked().connect([this] { addScopeRow("", ""); });
    inner->append(*add);
    expander->set_child(*inner);
    for (const auto& [scope, url] : savedRegistries_.scoped) addScopeRow(scope, url);
    expander->set_expanded(!savedRegistries_.scoped.empty());
    box->append(*expander);
    return *box;
}

void NpmPackagesDialog::addScopeRow(const std::string& scope, const std::string& url) {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto* scopeEntry = Gtk::make_managed<Gtk::Entry>();
    scopeEntry->set_placeholder_text("@company");
    scopeEntry->set_width_chars(16);
    scopeEntry->set_text(scope);
    auto* urlEntry = Gtk::make_managed<Gtk::Entry>();
    urlEntry->set_placeholder_text("https://npm.company.com/");
    urlEntry->set_hexpand(true);
    urlEntry->set_text(url);
    auto* login = Gtk::make_managed<Gtk::Button>("Log In…");
    login->signal_clicked().connect([this, urlEntry] { login_.emit(npm::normalRegistryUrl(trimmed(urlEntry->get_text()))); });
    auto* remove = Gtk::make_managed<Gtk::Button>();
    remove->set_icon_name("user-trash-symbolic");
    remove->set_tooltip_text("Remove this scope's source");
    remove->signal_clicked().connect([this, box] {
        std::erase_if(scopes_, [box](const ScopeRow& s) { return s.box == box; });
        scopeList_.remove(*box);
    });
    box->append(*scopeEntry);
    box->append(*Gtk::make_managed<Gtk::Label>("comes from"));
    box->append(*urlEntry);
    box->append(*login);
    box->append(*remove);
    scopeList_.append(*box);
    scopes_.push_back({scopeEntry, urlEntry, box});
}

npm::Registries NpmPackagesDialog::registries() const {
    npm::Registries r;
    if (const std::string main = trimmed(source_.get_text()); !main.empty()) r.main = npm::normalRegistryUrl(main);
    for (const auto& s : scopes_) {
        std::string scope = trimmed(s.scope->get_text());
        const std::string url = trimmed(s.url->get_text());
        if (scope.empty() || url.empty()) continue;
        if (scope[0] != '@') scope = "@" + scope;
        while (scope.size() > 1 && scope.back() == '/') scope.pop_back();
        r.scoped.emplace_back(scope, npm::normalRegistryUrl(url));
    }
    return r;
}

std::vector<std::string> NpmPackagesDialog::registryArgs(const std::string& package) const {
    std::vector<std::string> args;
    const auto r = registries();
    if (!r.main.empty()) args.push_back("--registry=" + r.main);
    for (const auto& [scope, url] : r.scoped)
        if (package.rfind(scope + "/", 0) == 0) args.push_back("--" + scope + ":registry=" + url);
    return args;
}

// ---------------------------------------------------------------------------------------------
// The packages

Gtk::Widget& NpmPackagesDialog::buildPackages() {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    auto* top = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto& title = heading("Packages");
    title.set_hexpand(true);
    top->append(title);
    checkStatus_.add_css_class("dim-label");
    top->append(checkStatus_);
    checkButton_.set_tooltip_text("Ask the package source for each package's newest version");
    checkButton_.signal_clicked().connect([this] { checkNewest(); });
    top->append(checkButton_);
    box->append(*top);

    grid_.set_column_spacing(14);
    grid_.set_row_spacing(2);
    grid_.set_margin_end(8);
    int column = 0;
    for (const char* name : {"Package", "Version", "Lock", "Updates allowed", "Installed", "Newest"}) {
        auto* label = Gtk::make_managed<Gtk::Label>();
        label->set_markup(std::string("<small><b>") + name + "</b></small>");
        label->add_css_class("dim-label");
        label->set_xalign(0.0f);
        grid_.attach(*label, column++, 0);
    }
    const auto deps = npm::readDependencies(root_);
    for (const auto& dep : deps) addRow(dep, false);
    auto* scroller = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroller->set_child(grid_);
    scroller->set_vexpand(true);
    scroller->set_min_content_height(260);
    box->append(*scroller);
    if (deps.empty()) box->append(note("package.json has no dependencies yet: add one below."));
    return *box;
}

void NpmPackagesDialog::addRow(npm::Dependency dep, bool added) {
    auto owned = std::make_unique<Row>();
    Row& r = *owned;
    r.dep = std::move(dep);
    r.added = added;
    r.original = npm::parseSpec(r.dep.spec);
    r.spec = r.dep.spec;
    r.version = r.original.version;
    const bool registry = r.original.source == Source::Registry;
    r.updates = registry ? r.original.updates : Updates::Custom;
    r.lastRange = r.updates == Updates::Locked ? Updates::Features : r.updates; // unlocking an exact pin: ^
    const int y = nextGridRow_;
    nextGridRow_ += 2;

    auto* name = Gtk::make_managed<Gtk::Label>();
    std::string markup = "<b>" + esc(r.dep.name) + "</b>";
    if (r.dep.section == "devDependencies") markup += "  <span alpha=\"60%\">dev</span>";
    else if (r.dep.section != "dependencies") markup += "  <span alpha=\"60%\">" + esc(r.dep.section.substr(0, r.dep.section.find('D'))) + "</span>";
    if (added) markup += "  <span foreground=\"#7bd88f\">new</span>";
    name->set_markup(markup);
    name->set_xalign(0.0f);
    name->set_tooltip_text(r.dep.section);
    grid_.attach(*name, 0, y);
    r.widgets.push_back(name);

    if (registry) {
        r.versionEntry = Gtk::make_managed<Gtk::Entry>();
        r.versionEntry->set_width_chars(12);
        r.versionEntry->set_text(r.version);
        r.versionEntry->set_placeholder_text("1.2.3");
        r.versionEntry->set_tooltip_text("The version the rule starts from");
        grid_.attach(*r.versionEntry, 1, y);
        r.widgets.push_back(r.versionEntry);
    } else {
        auto* raw = Gtk::make_managed<Gtk::Label>(r.dep.spec);
        raw->set_xalign(0.0f);
        raw->set_ellipsize(Pango::EllipsizeMode::END);
        raw->set_max_width_chars(18);
        grid_.attach(*raw, 1, y);
        r.widgets.push_back(raw);
    }

    r.lock = Gtk::make_managed<Gtk::CheckButton>();
    r.lock->set_active(r.updates == Updates::Locked);
    const bool lockable = registry || (r.original.source == Source::Tag && !r.dep.installed.empty());
    r.lock->set_sensitive(lockable);
    r.lock->set_tooltip_text(lockable ? "Locked: always install exactly this version, never an update"
                                      : "Packages from git, a folder or the workspace can't be locked to a version here");
    r.lock->set_halign(Gtk::Align::CENTER);
    grid_.attach(*r.lock, 2, y);
    r.widgets.push_back(r.lock);

    std::vector<Glib::ustring> choices;
    for (Updates u : kRules) choices.push_back(npm::updatesLabel(u));
    if (registry && r.original.updates == Updates::Custom) choices.push_back("As written: " + r.dep.spec);
    r.rule = Gtk::make_managed<Gtk::DropDown>(choices);
    r.rule->set_visible(registry);
    const Updates shown = r.updates == Updates::Locked ? r.lastRange : r.updates;
    for (guint i = 0; i < std::size(kRules); ++i)
        if (kRules[i] == shown) r.rule->set_selected(i);
    if (shown == Updates::Custom) r.rule->set_selected(static_cast<guint>(std::size(kRules)));
    grid_.attach(*r.rule, 3, y);
    r.widgets.push_back(r.rule);

    auto* installed = Gtk::make_managed<Gtk::Label>();
    if (r.dep.installed.empty()) installed->set_markup("<span alpha=\"60%\">not installed</span>");
    else installed->set_text(r.dep.installed);
    installed->set_xalign(0.0f);
    grid_.attach(*installed, 4, y);
    r.widgets.push_back(installed);

    r.newestButton = Gtk::make_managed<Gtk::Button>();
    r.newestButton->set_has_frame(false);
    r.newestButton->set_sensitive(false);
    r.newestButton->set_halign(Gtk::Align::START);
    grid_.attach(*r.newestButton, 5, y);
    r.widgets.push_back(r.newestButton);

    auto* remove = Gtk::make_managed<Gtk::Button>();
    remove->set_icon_name("user-trash-symbolic");
    remove->set_has_frame(false);
    remove->set_tooltip_text("Remove from package.json");
    grid_.attach(*remove, 6, y);
    r.widgets.push_back(remove);

    r.words = Gtk::make_managed<Gtk::Label>();
    r.words->set_xalign(0.0f);
    r.words->set_wrap(true);
    r.words->set_margin_bottom(6);
    grid_.attach(*r.words, 1, y + 1, 5, 1);
    r.widgets.push_back(r.words);

    // Row is on the heap and lives as long as the dialog, like its widgets.
    Row* row = &r;
    r.lock->signal_toggled().connect([this, row] {
        if (row->lock->get_active()) {
            // Locking keeps what's installed now, when there is something installed.
            const std::string v = !row->dep.installed.empty() ? row->dep.installed : row->version;
            if (v.empty()) {
                row->lock->set_active(false);
                return;
            }
            row->version = v;
            if (row->versionEntry) row->versionEntry->set_text(v);
            row->updates = Updates::Locked;
        } else {
            row->updates = row->lastRange;
        }
        update(*row);
    });
    if (r.versionEntry)
        r.versionEntry->signal_changed().connect([this, row] {
            const std::string text = trimmed(row->versionEntry->get_text());
            if (npm::isVersion(text)) {
                row->versionEntry->remove_css_class("error");
                row->version = text;
                if (row->updates == Updates::Custom) row->updates = row->lastRange == Updates::Custom ? Updates::Features : row->lastRange;
            } else {
                row->versionEntry->add_css_class("error");
            }
            update(*row);
        });
    r.rule->property_selected().signal_changed().connect([this, row] {
        const guint i = row->rule->get_selected();
        const Updates chosen = i < std::size(kRules) ? kRules[i] : Updates::Custom;
        row->lastRange = chosen;
        if (row->updates != Updates::Locked) row->updates = chosen;
        update(*row);
    });
    r.newestButton->signal_clicked().connect([row] {
        if (row->versionEntry && !row->newest.empty()) row->versionEntry->set_text(row->newest);
    });
    remove->signal_clicked().connect([row] {
        row->removed = true;
        for (auto* w : row->widgets) w->set_visible(false);
    });

    rows_.push_back(std::move(owned));
    update(r);
}

void NpmPackagesDialog::update(Row& r) {
    if (r.updates == Updates::Custom) r.spec = r.original.raw;
    else r.spec = npm::makeSpec(r.updates, r.version);
    if (r.rule) r.rule->set_sensitive(r.updates != Updates::Locked);
    std::string words = "<small><tt>" + esc(r.spec) + "</tt>  " + esc(npm::describe(npm::parseSpec(r.spec)));
    if (!r.added && r.spec != r.dep.spec) words += "  <b>(was " + esc(r.dep.spec) + ")</b>";
    r.words->set_markup(words + "</small>");
    r.words->set_opacity(0.8);
}

// ---------------------------------------------------------------------------------------------
// Newest versions, from the package source

void NpmPackagesDialog::checkNewest() {
    checkQueue_.clear();
    for (auto& r : rows_)
        if (!r->removed && (r->original.source == Source::Registry || r->original.source == Source::Tag))
            checkQueue_.push_back(r.get());
    if (checkQueue_.empty()) return;
    checkButton_.set_sensitive(false);
    checkStatus_.set_text("Asking " + (registries().main.empty() ? std::string("npm's registry") : registries().main) + "…");
    // A few at a time: each is an npm process.
    while (checksRunning_ < 4 && !checkQueue_.empty()) runNextCheck();
}

void NpmPackagesDialog::runNextCheck() {
    if (checkQueue_.empty()) {
        if (checksRunning_ == 0) {
            checkButton_.set_sensitive(true);
            size_t newer = 0, unknown = 0;
            for (const auto& r : rows_) {
                if (r->removed || (r->original.source != Source::Registry && r->original.source != Source::Tag)) continue;
                if (r->newest.empty()) ++unknown;
                else if (npm::compareVersions(r->newest, r->dep.installed.empty() ? r->version : r->dep.installed) > 0) ++newer;
            }
            std::string text = newer == 0 ? "Everything is up to date" : std::to_string(newer) + " can be updated (click a version to use it)";
            if (unknown) text += "; " + std::to_string(unknown) + " not found at the source";
            checkStatus_.set_text(text);
        }
        return;
    }
    Row* row = checkQueue_.front();
    checkQueue_.pop_front();
    ++checksRunning_;
    std::vector<std::string> argv{"npm", "view", row->dep.name, "version", "--json"};
    for (auto& a : registryArgs(row->dep.name)) argv.push_back(a);
    std::weak_ptr<int> alive = alive_;
    captureOutput(argv, root_,
                  [this, row](std::string out, int status) {
                      --checksRunning_;
                      row->newest = status == 0 ? lastVersion(out) : std::string();
                      if (row->newest.empty()) {
                          row->newestButton->set_label("?");
                          row->newestButton->set_tooltip_text("The package source didn't answer for this package");
                      } else {
                          const std::string have = row->dep.installed.empty() ? row->version : row->dep.installed;
                          const bool newer = npm::compareVersions(row->newest, have) > 0;
                          auto* label = Gtk::make_managed<Gtk::Label>();
                          label->set_markup(newer ? "<b><span foreground=\"#f0c050\">↑ " + esc(row->newest) + "</span></b>"
                                                  : "<span alpha=\"60%\">" + esc(row->newest) + "</span>");
                          row->newestButton->set_child(*label);
                          row->newestButton->set_sensitive(newer && row->versionEntry);
                          row->newestButton->set_tooltip_text(newer ? "Newer than " + have + ": click to use it"
                                                                    : "Up to date");
                      }
                      runNextCheck();
                  },
                  alive);
}

// ---------------------------------------------------------------------------------------------
// Adding

Gtk::Widget& NpmPackagesDialog::buildAdd() {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    addName_.set_placeholder_text("Add a package, e.g. zod or @company/ui");
    addName_.set_hexpand(true);
    addName_.signal_activate().connect([this] { addPackage(); });
    addVersion_.set_placeholder_text("newest");
    addVersion_.set_width_chars(10);
    addVersion_.set_tooltip_text("A version (1.2.3) or tag (next); empty: the newest");
    addVersion_.signal_activate().connect([this] { addPackage(); });
    addDev_.set_tooltip_text("For building and testing only (devDependencies), not needed when the app runs");
    addButton_.signal_clicked().connect([this] { addPackage(); });
    addStatus_.add_css_class("dim-label");
    box->append(addName_);
    box->append(addVersion_);
    box->append(addDev_);
    box->append(addButton_);
    auto* outer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    outer->append(*box);
    addStatus_.set_xalign(0.0f);
    outer->append(addStatus_);
    return *outer;
}

void NpmPackagesDialog::addPackage() {
    const std::string name = trimmed(addName_.get_text()), version = trimmed(addVersion_.get_text());
    if (!validName(name)) {
        addStatus_.set_text("That isn't a package name: lower case, like zod or @company/ui.");
        return;
    }
    for (const auto& r : rows_)
        if (!r->removed && r->dep.name == name) {
            addStatus_.set_text(name + " is already in the list.");
            return;
        }
    const std::string section = addDev_.get_active() ? "devDependencies" : "dependencies";
    auto added = [this, name, section](const std::string& spec) {
        addRow({name, section, spec, {}}, true);
        addName_.set_text("");
        addVersion_.set_text("");
        addStatus_.set_text("Added " + name + " " + spec + ": Save to keep it.");
    };
    if (npm::isVersion(version)) {
        added("^" + version);
        return;
    }
    // Ask the source for the newest (or tagged) version, which also checks the package exists there.
    addStatus_.set_text("Looking up " + name + "…");
    addButton_.set_sensitive(false);
    std::vector<std::string> argv{"npm", "view", version.empty() ? name : name + "@" + version, "version", "--json"};
    for (auto& a : registryArgs(name)) argv.push_back(a);
    captureOutput(argv, root_,
                  [this, name, added](std::string out, int status) {
                      addButton_.set_sensitive(true);
                      const std::string found = status == 0 ? lastVersion(out) : std::string();
                      if (found.empty()) {
                          const auto r = registries();
                          addStatus_.set_text("Couldn't find " + name + " at " +
                                              (r.main.empty() ? std::string("npm's registry") : r.main) + ".");
                          return;
                      }
                      added("^" + found);
                  },
                  std::weak_ptr<int>(alive_));
}

// ---------------------------------------------------------------------------------------------

void NpmPackagesDialog::save(bool install) {
    auto fail = [this](const std::string& message) {
        error_.set_text(message);
        error_.set_visible(true);
    };
    const npm::Registries regs = registries();
    if (auto problem = npm::checkRegistryUrl(regs.main); !problem.empty()) return fail("Package source: " + problem);
    for (const auto& [scope, url] : regs.scoped) {
        if (scope.size() < 2 || scope.find('/') != std::string::npos) return fail(scope + " isn't a scope (like @company).");
        if (auto problem = npm::checkRegistryUrl(url); !problem.empty()) return fail(scope + ": " + problem);
    }
    for (const auto& r : rows_)
        if (!r->removed && r->versionEntry && r->versionEntry->has_css_class("error"))
            return fail("The version of " + r->dep.name + " should look like 1.2.3.");

    const fs::path packageJson = root_ / "package.json";
    const std::string before = readFile(packageJson);
    std::string text = before;
    for (const auto& r : rows_) {
        std::optional<std::string> next;
        if (r->removed) {
            if (r->added) continue;
            next = npm::removeDependency(text, r->dep.section, r->dep.name);
        } else if (r->added || r->spec != r->dep.spec) {
            next = npm::setDependency(text, r->dep.section, r->dep.name, r->spec);
        } else {
            continue;
        }
        if (!next) return fail("package.json couldn't be read as JSON, so nothing was changed.");
        text = std::move(*next);
    }
    try {
        if (!nlohmann::json::accept(text)) return fail("The edited package.json wouldn't be valid JSON, so nothing was changed.");
        if (text != before) Glib::file_set_contents(packageJson.string(), text);
        if (regs != savedRegistries_) {
            const std::string npmrc = npm::writeRegistries(npmrcText_, regs);
            std::error_code ec;
            if (npmrc.empty()) fs::remove(root_ / ".npmrc", ec);
            else Glib::file_set_contents((root_ / ".npmrc").string(), npmrc);
        }
    } catch (const Glib::Error& e) {
        return fail("Couldn't save: " + std::string(e.what()));
    } catch (const std::exception&) {
        return fail("The edited package.json wouldn't be valid JSON, so nothing was changed.");
    }
    saved_.emit(install);
    set_visible(false);
}

} // namespace kevc
