#include "app/MainWindow.h"

#include "app/Application.h"
#include "app/BuildInfo.h"
#include "designer/DesignerPage.h"
#include "editor/EditorTab.h"
#include "core/FileOps.h"
#include "core/Mi.h"
#include "core/ProjectKinds.h"
#include "debug/DapViews.h"
#include "debug/DebugHover.h"
#include "lsp/LspBridges.h"
#include "process/Capture.h"
#include "project/SourceWatcher.h"
#include "ui/Dialogs.h"
#include "ui/NpmPackagesDialog.h"
#include "ui/ProjectOptionsDialog.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

namespace kevc {

namespace {

// The bundled Node tools (typescript-language-server): $KEVC_TOOLS_DIR, the install location next to
// the binary, or the source tree for development builds.
fs::path findToolsDir() {
    if (const char* env = std::getenv("KEVC_TOOLS_DIR")) return env;
    std::error_code ec;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        const fs::path installed = exe.parent_path().parent_path() / "share/kev-cpp/tools";
        if (fs::exists(installed / "node_modules", ec)) return installed;
    }
    return fs::path(KEVC_SOURCE_DIR) / "tools";
}

// Toolbar groups (View → Toolbars), in toolbar order.
constexpr std::array<std::pair<const char*, const char*>, 6> kToolbarGroups{{
    {"file", "_File"},
    {"edit", "_Edit"},
    {"build", "_Build"},
    {"debug", "_Debug"},
    {"project", "_Project"},
    {"web", "_Web"},
}};

// Tool panels (View → Panels): DockManager ids and menu labels.
constexpr std::array<std::pair<const char*, const char*>, 10> kPanels{{
    {"compiler", "_Compiler"},
    {"compile-log", "Compile _Log"},
    {"run", "_Run"},
    {"debug", "_Debug"},
    {"terminal", "_Terminal"},
    {"dev-server", "Dev _Server"},
    {"env", "_Environment (SvelteKit)"},
    {"tests", "Te_sts"},
    {"traces", "Tr_aces"},
    {"deps", "De_pendencies"},
}};

fs::path defaultProjectLocation() {
    const fs::path home = Glib::get_home_dir();
    std::error_code ec;
    for (const char* candidate : {"source/repos", "Projects", "projects", "src"})
        if (fs::is_directory(home / candidate, ec)) return home / candidate;
    return home;
}

std::string joinList(const std::vector<std::string>& items) {
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) out += ", ";
        if (i == 3) return out + "and " + std::to_string(items.size() - 3) + " more";
        out += items[i];
    }
    return out;
}

// Rejects absolute paths and anything that escapes the project folder.
std::optional<fs::path> resolveInside(const fs::path& root, const std::string& relative) {
    fs::path rel = fs::path(relative).lexically_normal();
    if (relative.empty() || rel.is_absolute() || rel.empty() || *rel.begin() == "..") return std::nullopt;
    return root / rel;
}

} // namespace

MainWindow::MainWindow(Application& app)
    : app_(app), recent_(app.settings()), toolsDir_(findToolsDir()),
      lsp_(std::make_unique<LspManager>(app.toolchain(), toolsDir_)) {
    set_default_size(1280, 800);
    updateTitle();

    buildActions();

    auto* menubar = Gtk::make_managed<Gtk::PopoverMenuBar>(buildMenuModel());
    // The search field sits in the middle of the menu bar's row, where people look for one first.
    auto* menuRow = Gtk::make_managed<Gtk::CenterBox>();
    menuRow->add_css_class("kevc-menubar-row");
    menuRow->set_start_widget(*menubar);
    menuRow->set_center_widget(quickSearch_);
    root_.append(*menuRow);
    root_.append(buildToolbar());

    vpaned_.set_start_child(buildEditorArea());
    vpaned_.set_end_child(buildBottomDock());
    vpaned_.set_resize_start_child(true);
    vpaned_.set_resize_end_child(false);
    vpaned_.set_position(560);

    rightDock_.set_size_request(140, -1); // can be dragged narrow; panels there scroll their tabs
    rightPaned_.set_start_child(vpaned_);
    rightPaned_.set_end_child(rightDock_);
    rightPaned_.set_resize_start_child(true);
    rightPaned_.set_resize_end_child(false);
    rightPaned_.set_shrink_end_child(false);
    // The editor and bottom panels never go below what they need: GTK would draw them at that
    // width anyway, cut off on the left (the right dock's first width did that). The docks give way.
    rightPaned_.set_shrink_start_child(false);

    hpaned_.set_start_child(buildLeftDock());
    hpaned_.set_end_child(rightPaned_);
    hpaned_.set_resize_start_child(false);
    hpaned_.set_shrink_end_child(false);
    hpaned_.set_position(290);
    hpaned_.set_vexpand(true);

    root_.append(hpaned_);
    root_.append(buildStatusBar());
    set_child(root_);

    projectTree_.signalFileActivated().connect([this](const fs::path& file) { openDocument(file); });
    projectTree_.signalNewFile().connect([this](const fs::path& dir) { newFileIn(dir); });
    projectTree_.signalNewFolder().connect([this](const fs::path& dir) { newFolderIn(dir); });
    projectTree_.signalRename().connect([this](const fs::path& path) { renamePath(path); });
    projectTree_.signalDelete().connect([this](const fs::path& path) { deletePath(path); });
    projectTree_.signalMove().connect([this](const fs::path& source, const fs::path& dir) { movePath(source, dir); });
    projectTree_.signalSearchDone().connect([this] {
        if (auto* page = currentDocument()) page->focusPage();
    });
    searchPanel_.signalOpen().connect([this](const fs::path& file, int line, size_t column, size_t length) {
        if (EditorTab* tab = openFile(file)) tab->selectRange(line, column, length);
    });
    searchPanel_.signalReplaceInEditor().connect([this](const fs::path& file, const std::string& text) {
        if (auto* tab = dynamic_cast<EditorTab*>(findDocument(file))) tab->replaceText(text);
    });
    searchPanel_.signalDone().connect([this] {
        if (auto* page = currentDocument()) page->focusPage();
    });
    searchPanel_.setUnsavedText([this] { return unsavedEditorText(); });
    quickSearch_.setUnsavedText([this] { return unsavedEditorText(); });
    quickSearch_.signalOpenFile().connect([this](const fs::path& file) { openDocument(file); });
    quickSearch_.signalOpenMatch().connect([this](const fs::path& file, int line, size_t column, size_t length) {
        if (EditorTab* tab = openFile(file)) tab->selectRange(line, column, length);
    });
    quickSearch_.signalShowAll().connect([this](const Glib::ustring& text) { findInFiles(text); });
    quickSearch_.signalDone().connect([this] {
        if (auto* page = currentDocument()) page->focusPage();
    });
    routesPanel_.signalOpen().connect([this](const fs::path& file, int line) { openFile(file, line); });
    routesPanel_.signalOpenInBrowser().connect([this](const std::string& url) { openRoute(url); });
    routesPanel_.signalNewRoute().connect([this] { newRoute(); });
    envPanel_.signalOpen().connect([this](const fs::path& file, int line) { openFile(file, line); });
    testsPanel_.signalRunAll().connect([this](TestRunner runner) { runTests(runner); });
    testsPanel_.signalRunFailed().connect([this] {
        std::vector<fs::path> files;
        for (const auto& c : lastFailed_)
            if (std::find(files.begin(), files.end(), c.file) == files.end()) files.push_back(c.file);
        runTests(lastRunner_, {}, {}, 0, files);
    });
    testsPanel_.signalRunOne().connect([this](const TestCase& c) {
        runTests(lastRunner_, c.file, lastRunner_ == TestRunner::Vitest ? c.fullName : std::string(), c.line);
    });
    testsPanel_.signalOpen().connect([this](const fs::path& file, int line) { openFile(file, line); });
    tracesPanel_.signalClear().connect([this] {
        traces_.clear();
        tracesPanel_.setTraces(traces_);
    });
    depsPanel_.signalRefresh().connect([this] { refreshDependencies(); });
    depsPanel_.signalOpenPackage().connect([this](const std::string& name) {
        if (folder_.empty()) return;
        std::ifstream in(folder_ / "package.json");
        std::string text;
        int number = 0, found = 1;
        while (std::getline(in, text)) {
            ++number;
            if (text.find("\"" + name + "\"") != std::string::npos) {
                found = number;
                break;
            }
        }
        openFile(folder_ / "package.json", found);
    });
    envPanel_.signalEditEnv().connect([this] {
        if (folder_.empty()) return;
        const fs::path env = folder_ / ".env";
        std::error_code ec;
        if (!fs::exists(env, ec)) std::ofstream(env) << "# Environment variables for SvelteKit's $env modules.\n"
                                                         "# Names starting with PUBLIC_ can reach the browser; the rest stay on the server.\n";
        openFile(env);
    });
    lsp_->signalStatus().connect([this](const std::string& message) { setStatus(message); });
    lsp_->signalDiagnosticsChanged().connect([this](EditorTab* tab) {
        // Library headers opened via Go to Definition aren't the user's code: no squiggles there.
        const fs::path root = !folder_.empty() ? folder_ : lspRootForLooseFiles_;
        const auto rel = tab->path().lexically_relative(root);
        if (!root.empty() && (rel.empty() || *rel.begin() == "..")) return;
        tab->showDiagnostics(lsp_->diagnosticsFor(tab));
        if (tab == currentEditor()) scheduleSymbolsRefresh();
    });
    symbols_.signalActivated().connect([this](int line, int utf16) {
        if (auto* tab = currentEditor()) tab->placeCursor(LspManager::iterAt(GTK_TEXT_BUFFER(tab->buffer()), line, utf16));
    });
    editors_.signal_switch_page().connect([this](Gtk::Widget*, guint) { scheduleSymbolsRefresh(); });
    debugPanel_.signalFrameActivated().connect([this](int level) {
        if (jsDebugger_ && jsDebugger_->stopped()) {
            for (const auto& f : frames_)
                if (f.level == level) showFrame(f);
            refreshJsViews(level);
            return;
        }
        if (!debugger_ || !debugger_->stopped()) return;
        debugger_->selectFrame(level);
        for (const auto& f : frames_)
            if (f.level == level) showFrame(f);
        refreshDebugViews();
    });
    debugPanel_.signalBreakpointActivated().connect([this](const fs::path& file, int line) { openFile(file, line); });
    debugPanel_.signalBreakpointRemoved().connect([this](const fs::path& file, int line) { setBreakpointAt(file, line, std::nullopt); });
    debugPanel_.signalBreakpointEdit().connect([this](const fs::path& file, int line) {
        auto f = breakpoints_.find(file);
        if (f == breakpoints_.end() || !f->second.count(line)) return;
        if (f->second[line].kind == BreakKind::Live) promptLive(file, line);
        else promptConditional(file, line);
    });
    debugPanel_.signalValueWatchRemoved().connect([this](const std::string& expression) { removeValueWatch(expression); });
    debugPanel_.signalConsoleCommand().connect([this](const std::string& command) {
        if (jsDebugger_) {
            // Evaluated in the paused frame, like the browser's console.
            const int frameId = jsFrames_.empty() ? 0 : jsFrames_.front().id;
            debugPanel_.appendConsole("> " + command + "\n");
            jsDebugger_->evaluate(command, frameId, "repl", [this](std::optional<dap::Variable> v, std::string error) {
                debugPanel_.appendConsole((v ? v->value : "Error: " + error) + "\n");
            });
            return;
        }
        if (debugger_) debugger_->consoleCommand(command);
        else debugPanel_.appendConsole("Not debugging. Press F5 to start.\n");
    });
    watches_.signalChanged().connect([this] {
        saveProjectState();
        if (jsDebugger_ && jsDebugger_->stopped()) refreshJsViews(-1);
        else if (debugger_ && debugger_->stopped()) refreshDebugViews();
        else watches_.showPlaceholders();
    });
    compilerPanel_.signalActivated().connect([this](const Diagnostic& d) {
        if (d.hasLocation()) openFile(d.file, d.line, d.column);
    });
    build_.signalLine().connect(sigc::mem_fun(*this, &MainWindow::onBuildLine));
    build_.signalFinished().connect(sigc::mem_fun(*this, &MainWindow::onBuildFinished));
    shell_.startShell(Glib::get_home_dir());

    // GTK binds F10 to "open the menu bar" and Alt+letter to menu mnemonics; Dev-C++ users expect
    // F10 to run the program and Alt+P to open Project Options, so claim those keys first.
    auto keys = Gtk::EventControllerKey::create();
    keys->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    keys->signal_key_pressed().connect(
        [this](guint keyval, guint, Gdk::ModifierType state) {
            const auto mods = state & (Gdk::ModifierType::SHIFT_MASK | Gdk::ModifierType::CONTROL_MASK |
                                       Gdk::ModifierType::ALT_MASK);
            if (keyval == GDK_KEY_F10 && mods == Gdk::ModifierType{}) {
                activate_action("win.run");
                return true;
            }
            if ((keyval == GDK_KEY_p || keyval == GDK_KEY_P) && mods == Gdk::ModifierType::ALT_MASK) {
                activate_action("win.project-options");
                return true;
            }
            return false;
        },
        false);
    add_controller(keys);

    signal_close_request().connect(
        [this] {
            saveProjectState(); // where breakpoints have moved with edits
            if (closeConfirmed_) return false;
            auto docs = documents();
            if (std::none_of(docs.begin(), docs.end(), [](DocumentPage* d) { return d->modified(); })) return false;
            closeAllEditors([this] {
                closeConfirmed_ = true;
                close();
            });
            return true;
        },
        false);

    applyProfile();

    // The Project tab's git icons follow commits made in a terminal (the index watch) and files
    // changed outside the IDE (looked at again when the window is used, and every few seconds).
    property_is_active().signal_changed().connect([this] {
        if (is_active()) refreshGitStatus();
    });
    Glib::signal_timeout().connect_seconds(sigc::track_obj(
                                               [this] {
                                                   if (is_active()) refreshGitStatus();
                                                   return true;
                                               },
                                               *this),
                                           5);

    if (auto missing = app_.toolchain().missingOptional(); !missing.empty()) {
        Glib::ustring msg = "Optional tools not found:";
        for (const ToolInfo* t : missing) msg += " " + t->name + " (" + t->purpose + ")";
        setStatus(msg);
    } else {
        setStatus("Ready");
    }
}

MainWindow::~MainWindow() {
    locationsPopover_.unparent();
}

void MainWindow::closeWithoutQuestions() {
    closeConfirmed_ = true;
    close();
}

void MainWindow::setStatus(const Glib::ustring& message) {
    status_.set_text(message);
}

void MainWindow::addStubAction(const Glib::ustring& name, const Glib::ustring& label) {
    add_action(name, [this, label] { setStatus(label + ": not implemented yet"); });
}

// ---------------------------------------------------------------------------------------------
// UI construction

void MainWindow::buildActions() {
    // File
    add_action("new-project", [this] { newProject(); });
    add_action("open-project", [this] { chooseFolder(); });
    add_action("close-project", [this] { closeProject(); });
    add_action("new-file", [this] { newFile(); });
    add_action("save", [this] { saveCurrent(); });
    add_action("save-all", [this] { saveAll(); });
    add_action("close-tab", [this] {
        if (auto* page = currentDocument()) closeEditor(page);
    });
    // Edit (forwarded to the focused editor's own GtkTextView actions)
    add_action("undo", [this] { forwardToEditor("text.undo"); });
    add_action("redo", [this] { forwardToEditor("text.redo"); });
    add_action("cut", [this] { forwardToEditor("clipboard.cut"); });
    add_action("copy", [this] { forwardToEditor("clipboard.copy"); });
    add_action("paste", [this] { forwardToEditor("clipboard.paste"); });
    add_action("select-all", [this] { forwardToEditor("selection.select-all"); });
    // Search
    add_action("find", [this] {
        replaceToggle_.set_active(false);
        showFindBar();
    });
    add_action("find-in-files", [this] { findInFiles(); });
    add_action("quick-search", [this] { focusQuickSearch(); });
    add_action("replace", [this] { showReplaceBar(); });
    add_action("goto-line", [this] { gotoLine(); });
    add_action("goto-definition", [this] { lookUpAtCursor(Lookup::Definition); });
    add_action("goto-declaration", [this] { lookUpAtCursor(Lookup::Declaration); });
    add_action("goto-type-definition", [this] { lookUpAtCursor(Lookup::TypeDefinition); });
    add_action("goto-implementation", [this] { lookUpAtCursor(Lookup::Implementation); });
    add_action("find-references", [this] { lookUpAtCursor(Lookup::References); });
    // View
    add_action("toggle-left-dock", [this] { leftDock_.set_visible(!leftDock_.get_visible()); });
    add_action("toggle-bottom-dock", [this] { bottomDock_.set_visible(!bottomDock_.get_visible()); });
    add_action("toggle-right-dock", [this] { rightDock_.set_visible(!rightDock_.get_visible()); });
    add_action("reset-panels", [this] {
        docks_->resetLayout();
        setStatus("Panels moved back to the bottom");
    });
    // View → Toolbars and View → Panels: shown or hidden for the current profile, remembered.
    for (const auto& [id, label] : kToolbarGroups) {
        const std::string group = id;
        toolbarActions_[group] = add_action_bool(
            "toolbar-" + group,
            [this, group] {
                setToolbarShownHere(group, !toolbarShown(group));
                applyProfile();
            },
            true);
    }
    for (const auto& [id, label] : kPanels) {
        const std::string panel = id;
        panelActions_[panel] = add_action_bool(
            "panel-" + panel,
            [this, panel] {
                const bool show = docks_->closed(panel);
                setPanelShownHere(panel, show);
                docks_->setClosed(panel, !show);
            },
            true);
    }
    // Project
    add_action("project-add-file", [this] { newFile(); });
    add_action("web-install", [this] { installDependencies(); });
    add_action("web-tests", [this] {
        const auto runners = folder_.empty() ? std::vector<TestRunner>{} : testRunners(folder_);
        if (runners.empty()) setStatus("No test runner installed: add Vitest (npm i -D vitest) or Playwright");
        else runTests(runners.front());
    });
    add_action_with_parameter("run-script", Glib::VARIANT_TYPE_STRING, [this](const Glib::VariantBase& v) {
        runScript(Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(v).get());
    });
    add_action("project-options", [this] { showProjectOptions(false); });
    add_action("packages", [this] { showProjectOptions(true); });
    // Execute
    add_action("compile", [this] { startBuild(BuildAction::Build, Task::Compile); });
    add_action("run", [this] { runProgram(); });
    add_action("compile-run", [this] { compileAndRun(); });
    add_action("rebuild", [this] { startBuild(BuildAction::Rebuild, Task::Rebuild); });
    add_action("clean", [this] { startBuild(BuildAction::Clean, Task::Clean); });
    add_action("stop", [this] { stopExecution(); });
    // Debug
    add_action("debug", [this] { startDebugging(); });
    // Each goes to whichever debugger is running: gdb, or js-debug for web projects.
    add_action("debug-continue", [this] {
        if (jsDebugger_) jsDebugger_->resume();
        else debugStep(&DebugController::resume);
    });
    add_action("step-over", [this] {
        if (jsDebugger_) jsDebugger_->stepOver();
        else debugStep(&DebugController::stepOver);
    });
    add_action("step-into", [this] {
        if (jsDebugger_) jsDebugger_->stepInto();
        else debugStep(&DebugController::stepInto);
    });
    add_action("step-out", [this] {
        if (jsDebugger_) jsDebugger_->stepOut();
        else debugStep(&DebugController::stepOut);
    });
    add_action("debug-pause", [this] {
        if (debugger_) debugger_->pause();
        if (jsDebugger_) jsDebugger_->pause();
    });
    add_action("debug-stop", [this] {
        // Also during the build that comes before debugging.
        if (debugger_ || jsDebugger_ || (build_.running() && buildTask_ == Task::Debug)) stopTask(Task::Debug);
        else setStatus("Not debugging");
    });
    add_action("toggle-breakpoint", [this] { toggleBreakpointAtCursor(); });
    add_action("if-breakpoint", [this] {
        fs::path file;
        int line = 0;
        if (cursorLine(file, line)) promptConditional(file, line);
    });
    add_action("live-breakpoint", [this] {
        fs::path file;
        int line = 0;
        if (cursorLine(file, line)) promptLive(file, line);
    });
    add_action("value-watch", [this] { promptValueWatch(); });
    add_action("add-watch", [this] { addWatch(); });
    // Tools
    add_action("dev-server", [this] { toggleDevServer(); });
    add_action("dev-server-open", [this] { openInBrowser(); });
    add_action("toolchain-info", [this] { showToolchainInfo(); });
    add_action("about", [this] { showAbout(); });
    add_action("check-updates", [this] { checkForUpdates(); });
}

Glib::RefPtr<Gio::Menu> MainWindow::buildMenuModel() {
    auto bar = Gio::Menu::create();
    auto submenu = [&bar](const Glib::ustring& title,
                          std::initializer_list<std::initializer_list<std::pair<const char*, const char*>>> sections) {
        auto menu = Gio::Menu::create();
        for (const auto& section : sections) {
            auto sec = Gio::Menu::create();
            for (const auto& [label, action] : section) sec->append(label, action);
            menu->append_section(sec);
        }
        bar->append_submenu(title, menu);
    };

    bar->append_submenu("_File", fileMenu_); // filled by updateMenus: Close Project only with one open
    submenu("_Edit", {
        {{"_Undo", "win.undo"}, {"_Redo", "win.redo"}},
        {{"Cu_t", "win.cut"}, {"_Copy", "win.copy"}, {"_Paste", "win.paste"}},
        {{"Select _All", "win.select-all"}},
    });
    submenu("_Search", {
        {{"_Find…", "win.find"}, {"_Replace…", "win.replace"}},
        {{"Search Files and _Text…", "win.quick-search"}, {"Find in F_iles…", "win.find-in-files"}},
        {{"_Go to Line…", "win.goto-line"}, {"Go to _Definition (Ctrl+Click)", "win.goto-definition"},
         {"Go to _Implementation", "win.goto-implementation"}, {"Go to T_ype Definition", "win.goto-type-definition"},
         {"Find All _References", "win.find-references"}},
    });
    {
        auto view = Gio::Menu::create();
        auto docks = Gio::Menu::create();
        docks->append("_Project Panel", "win.toggle-left-dock");
        docks->append("_Bottom Panel", "win.toggle-bottom-dock");
        docks->append("_Right Panel", "win.toggle-right-dock");
        view->append_section(docks);
        auto lists = Gio::Menu::create();
        lists->append_submenu("_Toolbars", viewToolbars_);
        lists->append_submenu("P_anels", viewPanels_);
        view->append_section(lists);
        auto reset = Gio::Menu::create();
        reset->append("Reset Panel _Layout", "win.reset-panels");
        view->append_section(reset);
        bar->append_submenu("_View", view);
    }
    // Filled by updateMenus, for what's open.
    bar->append_submenu("P_roject", projectMenu_);
    bar->append_submenu("E_xecute", executeMenu_);
    submenu("_Debug", {
        {{"_Debug", "win.debug"}, {"_Continue", "win.debug-continue"}, {"_Pause", "win.debug-pause"},
         {"_Stop", "win.debug-stop"}},
        {{"Step _Over", "win.step-over"}, {"Step _Into", "win.step-into"}, {"Step O_ut", "win.step-out"}},
        {{"Toggle _Breakpoint", "win.toggle-breakpoint"}, {"I_F Breakpoint…", "win.if-breakpoint"},
         {"_Live Breakpoint…", "win.live-breakpoint"}, {"Break When Value C_hanges…", "win.value-watch"}},
        {{"Add _Watch…", "win.add-watch"}},
    });
    bar->append_submenu("_Tools", toolsMenu_);
    submenu("_Help", {
        {{"Check for _Updates…", "win.check-updates"}},
        {{"_About Kev-C++", "win.about"}},
    });
    return bar;
}

Gtk::Widget& MainWindow::buildToolbar() {
    // Two rows, as in Dev-C++: files, editing and building on top, debugging below. Each row holds
    // named groups that View → Toolbars can hide (per profile; see applyProfile).
    auto* toolbar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    toolbar->add_css_class("kevc-toolbar");
    auto newRow = [this, toolbar] {
        if (!toolbarRows_.empty()) {
            auto* rule = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
            rule->add_css_class("kevc-toolbar-rule");
            toolbar->append(*rule);
            toolbarRules_.push_back(rule);
        }
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 1);
        toolbar->append(*row);
        toolbarRows_.push_back(row);
    };
    Gtk::Box* bar = nullptr; // the group the helpers below add to
    auto group = [this, &bar](const char* id) {
        Gtk::Box* row = toolbarRows_.back();
        auto* separator = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL);
        separator->set_margin_start(4);
        separator->set_margin_end(4);
        row->append(*separator);
        bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 1);
        row->append(*bar);
        toolbarGroups_.push_back({id, bar, separator, toolbarRows_.size() - 1});
    };
    // Buttons activate the window's actions (the same as the menus and keys) rather than being
    // bound to them, so their enabled state is ours to set in updateToolbar().
    auto button = [this, &bar](const char* icon, const char* action, const char* tooltip) {
        auto* image = Gtk::make_managed<Gtk::Image>();
        image->set_from_icon_name(icon);
        image->set_pixel_size(kToolbarIconSize);
        auto* b = Gtk::make_managed<Gtk::Button>();
        b->set_child(*image);
        b->set_tooltip_text(tooltip);
        b->set_has_frame(false);
        b->set_focus_on_click(false); // keep typing in the editor
        b->signal_clicked().connect([this, action] { activate_action(action); });
        bar->append(*b);
        return b;
    };
    // Starts a task, then shows Stop until that task is over.
    auto task = [this, &bar](Task t, const char* icon, const char* action, const char* tooltip, const char* stopTooltip) {
        auto* b = Gtk::make_managed<TaskButton>(icon, tooltip, "kevc-stop", stopTooltip);
        b->signalStart().connect([this, action] { activate_action(action); });
        b->signalStop().connect([this, t] { stopTask(t); });
        taskButtons_[t] = b;
        bar->append(*b);
    };

    newRow();
    group("file");
    button("kevc-new-project", "win.new-project", "New Project (Ctrl+Shift+N)");
    button("kevc-new-file", "win.new-file", "New Source File (Ctrl+N)");
    button("kevc-open", "win.open-project", "Open Project or Folder (Ctrl+O)");
    button("kevc-save", "win.save", "Save (Ctrl+S)");
    button("kevc-save-all", "win.save-all", "Save All (Ctrl+Shift+S)");
    group("edit");
    button("kevc-undo", "win.undo", "Undo (Ctrl+Z)");
    button("kevc-redo", "win.redo", "Redo (Ctrl+Shift+Z)");
    button("kevc-find", "win.find", "Find (Ctrl+F)");
    button("kevc-find-in-files", "win.find-in-files", "Find in Files (Ctrl+Shift+F)");
    group("build");
    task(Task::Compile, "kevc-compile", "win.compile", "Compile (F9)", "Stop compiling (Ctrl+F2)");
    task(Task::Run, "kevc-run", "win.run", "Run (F10)", "Stop the program (Ctrl+F2)");
    task(Task::CompileRun, "kevc-compile-run", "win.compile-run", "Compile & Run (F11)", "Stop (Ctrl+F2)");
    task(Task::Rebuild, "kevc-rebuild", "win.rebuild", "Rebuild All (F12)", "Stop rebuilding (Ctrl+F2)");
    task(Task::Clean, "kevc-clean", "win.clean", "Clean", "Stop cleaning (Ctrl+F2)");
    configDrop_ = Gtk::make_managed<Gtk::DropDown>(std::vector<Glib::ustring>{"Debug", "Release"});
    configDrop_->set_tooltip_text("Build configuration");
    configDrop_->set_valign(Gtk::Align::CENTER);
    configDrop_->set_margin_start(4);
    // The choice belongs to the project (saved in it); applyProfile sets it when one opens.
    configDrop_->property_selected().signal_changed().connect([this] {
        if (!session_ || applyingProfile_) return;
        session_->project().ui.buildConfig = configDrop_->get_selected() == 1 ? "Release" : "";
        saveProjectUi();
    });
    bar->append(*configDrop_);

    newRow();
    group("debug");
    task(Task::Debug, "kevc-debug", "win.debug", "Debug (F5)", "Stop debugging (Shift+F5)");
    continueButton_ = Gtk::make_managed<TaskButton>("kevc-continue", "Continue (F8)", "kevc-pause", "Pause");
    continueButton_->signalStart().connect([this] { activate_action("win.debug-continue"); });
    continueButton_->signalStop().connect([this] { activate_action("win.debug-pause"); });
    bar->append(*continueButton_);
    stepButtons_ = {button("kevc-step-over", "win.step-over", "Step Over (F6)"),
                    button("kevc-step-into", "win.step-into", "Step Into (F7)"),
                    button("kevc-step-out", "win.step-out", "Step Out (Shift+F7)")};
    button("kevc-breakpoint", "win.toggle-breakpoint", "Toggle Breakpoint (Ctrl+F5)");
    button("kevc-watch", "win.add-watch", "Add Watch");
    group("project");
    button("kevc-project", "win.project-options", "Project Options (Alt+P)");
    group("web");
    button("kevc-workspace", "win.web-install", "Install Dependencies (npm install)");
    button("kevc-settings", "win.packages", "Packages: versions, locking and where they come from");
    button("kevc-bool-variable", "win.web-tests", "Run Tests (Vitest or Playwright)");
    {
        auto* image = Gtk::make_managed<Gtk::Image>();
        image->set_from_icon_name("kevc-terminal");
        image->set_pixel_size(kToolbarIconSize);
        auto* scripts = Gtk::make_managed<Gtk::MenuButton>();
        scripts->set_child(*image);
        scripts->set_has_frame(false);
        scripts->set_tooltip_text("Run a package.json script");
        scripts->set_menu_model(scriptsMenu_);
        bar->append(*scripts);
    }
    devServerButton_ = Gtk::make_managed<TaskButton>("kevc-globe", "Start the dev server (Vite, or the bundled one)",
                                                     "kevc-stop", "Stop the dev server");
    devServerButton_->signalStart().connect([this] { startDevServer(false); });
    devServerButton_->signalStop().connect([this] {
        if (devServer_) devServer_->stop();
    });
    bar->append(*devServerButton_);
    updateToolbarRows();
    updateToolbar();
    return *toolbar;
}

void MainWindow::updateToolbarRows() {
    // A group's separator only shows after another visible group in its row, and a row (with the
    // line above it) only shows if it has a visible group.
    std::vector<bool> rowUsed(toolbarRows_.size(), false);
    for (auto& g : toolbarGroups_) {
        const bool visible = g.box->get_visible();
        g.separator->set_visible(visible && rowUsed[g.row]);
        if (visible) rowUsed[g.row] = true;
    }
    bool earlierRow = false;
    for (size_t r = 0; r < toolbarRows_.size(); ++r) {
        toolbarRows_[r]->set_visible(rowUsed[r]);
        if (r > 0) toolbarRules_[r - 1]->set_visible(rowUsed[r] && earlierRow);
        earlierRow = earlierRow || rowUsed[r];
    }
}

void MainWindow::applyProfile() {
    profile_ = profileFor(session_ ? &session_->project() : nullptr, !folder_.empty());
    const bool kit = web_.flavor == WebFlavor::SvelteKit;
    for (auto& g : toolbarGroups_) {
        const bool shown = toolbarGroupApplies(g.id, profile_) && toolbarShown(g.id);
        g.box->set_visible(shown);
        if (auto it = toolbarActions_.find(g.id); it != toolbarActions_.end())
            it->second->set_state(Glib::Variant<bool>::create(shown));
    }
    // Buttons for the compiler only (a web project has no Debug/Release build and nothing to clean),
    // and what the build buttons mean in a web project.
    const bool native = usesNativeTools(profile_), web = profile_ == "web";
    if (configDrop_) {
        configDrop_->set_visible(native);
        applyingProfile_ = true;
        configDrop_->set_selected(session_ && session_->project().ui.buildConfig == "Release" ? 1 : 0);
        applyingProfile_ = false;
    }
    if (!taskButtons_.empty()) {
        taskButtons_.at(Task::Clean)->set_visible(native);
        if (web) {
            taskButtons_.at(Task::Compile)->setTooltips("Check types (F9)", "Stop checking (Ctrl+F2)");
            taskButtons_.at(Task::Run)->setTooltips("Run: start the dev server and open the browser (F10)",
                                                    "Stop the dev server (Ctrl+F2)");
            taskButtons_.at(Task::CompileRun)->setTooltips("Check types, then run (F11)", "Stop (Ctrl+F2)");
            taskButtons_.at(Task::Rebuild)->setTooltips("Production build (F12)", "Stop building (Ctrl+F2)");
        } else {
            taskButtons_.at(Task::Compile)->setTooltips("Compile (F9)", "Stop compiling (Ctrl+F2)");
            taskButtons_.at(Task::Run)->setTooltips("Run (F10)", "Stop the program (Ctrl+F2)");
            taskButtons_.at(Task::CompileRun)->setTooltips("Compile & Run (F11)", "Stop (Ctrl+F2)");
            taskButtons_.at(Task::Rebuild)->setTooltips("Rebuild All (F12)", "Stop rebuilding (Ctrl+F2)");
        }
    }
    updateToolbarRows();
    if (docks_)
        for (const auto& [id, title] : docks_->panels())
            docks_->setClosed(id, !(panelApplies(id, profile_, kit) && panelShownHere(id)));
    updateMenus();
}

void MainWindow::updateMenus() {
    const bool kit = web_.flavor == WebFlavor::SvelteKit, web = profile_ == "web";
    auto section = [](std::initializer_list<std::pair<Glib::ustring, const char*>> items) {
        auto menu = Gio::Menu::create();
        for (const auto& [label, action] : items) menu->append(label, action);
        return menu;
    };
    fileMenu_->remove_all();
    fileMenu_->append_section(section({{"New _Project…", "win.new-project"}, {"_New Source File…", "win.new-file"}}));
    auto open = section({{"_Open Project or Folder…", "win.open-project"}});
    if (session_) open->append("Close P_roject", "win.close-project");
    else if (!folder_.empty()) open->append("Close _Folder", "win.close-project");
    fileMenu_->append_section(open);
    fileMenu_->append_section(section({{"_Save", "win.save"}, {"Save _All", "win.save-all"}, {"_Close", "win.close-tab"}}));
    fileMenu_->append_section(section({{"_Quit", "app.quit"}}));

    viewToolbars_->remove_all();
    for (const auto& [id, label] : kToolbarGroups)
        if (toolbarGroupApplies(id, profile_)) viewToolbars_->append(label, std::string("win.toolbar-") + id);
    viewPanels_->remove_all();
    for (const auto& [id, label] : kPanels)
        if (panelApplies(id, profile_, kit)) viewPanels_->append(label, std::string("win.panel-") + id);

    projectMenu_->remove_all();
    projectMenu_->append_section(section({{"_New File in Project…", "win.project-add-file"}}));
    if (usesWebTools(profile_)) {
        const std::string manager = web ? web_.packageManagerName() : std::string("npm");
        auto packages = section({{"_Install Dependencies (" + manager + " install)", "win.web-install"},
                                 {"Run _Tests", "win.web-tests"}});
        packages->append_submenu("_Scripts", scriptsMenu_);
        projectMenu_->append_section(packages);
    }
    if (session_ && !session_->project().buildFileManaged)
        projectMenu_->append_section(section({{"_Program to Run and Debug…", "win.project-options"}}));
    else if (usesNativeTools(profile_))
        projectMenu_->append_section(section({{"_Packages…", "win.packages"}, {"Project _Options…", "win.project-options"}}));
    else
        projectMenu_->append_section(section({{"_Packages (Versions, Locking, Source)…", "win.packages"}}));

    executeMenu_->remove_all();
    if (web) {
        executeMenu_->append_section(section({{"_Check Types", "win.compile"},
                                              {"_Run (Dev Server and Browser)", "win.run"},
                                              {"Check & R_un", "win.compile-run"}}));
        executeMenu_->append_section(section({{"_Production Build", "win.rebuild"}}));
    } else {
        executeMenu_->append_section(
            section({{"_Compile", "win.compile"}, {"_Run", "win.run"}, {"Compile & R_un", "win.compile-run"}}));
        executeMenu_->append_section(section({{"Rebuild _All", "win.rebuild"}, {"C_lean", "win.clean"}}));
    }
    executeMenu_->append_section(section({{"_Stop Build or Program", "win.stop"}}));

    toolsMenu_->remove_all();
    if (usesWebTools(profile_))
        toolsMenu_->append_section(
            section({{"Start/Stop the _Dev Server", "win.dev-server"}, {"_Open Dev Server in Browser", "win.dev-server-open"}}));
    toolsMenu_->append_section(section({{"_Toolchain Info", "win.toolchain-info"}}));
}

Gtk::Widget& MainWindow::buildLeftDock() {
    // Scrollable tabs, so a narrow pane scrolls them rather than clipping the whole panel.
    leftDock_.set_scrollable(true);
    leftDock_.add_css_class("kevc-compact-tabs");
    leftDock_.append_page(projectTree_, *makeIconLabel("kevc-project", "Project"));
    // Next to the Project tab and its file search: finding things is all in one corner.
    leftDock_.append_page(searchPanel_, *makeIconLabel("kevc-find-in-files", "Search"));
    leftDock_.append_page(symbols_, *makeIconLabel("kevc-class", "Classes"));
    leftDock_.append_page(watches_, *makeIconLabel("kevc-watch", "Debug"));
    leftDock_.set_size_request(180, -1);
    // After the switch (the current page isn't updated yet while switch-page runs).
    leftDock_.signal_switch_page().connect(
        [this](Gtk::Widget*, guint) { Glib::signal_idle().connect_once(sigc::track_obj([this] { fitLeftTabs(); }, *this)); });
    hpaned_.property_position().signal_changed().connect([this] { fitLeftTabs(); });
    // Again once the tabs have their real sizes.
    leftDock_.signal_map().connect([this] { Glib::signal_idle().connect_once(sigc::track_obj([this] { fitLeftTabs(); }, *this)); });
    return leftDock_;
}

void MainWindow::fitLeftTabs() {
    struct Tab {
        Gtk::Widget* box;
        Gtk::Label* label;
        int text; // the label's width
    };
    std::vector<Tab> tabs;
    int chrome = 22; // a tab's padding and border around its icon and label, measured below when shown
    for (int i = 0; i < leftDock_.get_n_pages(); ++i) {
        Gtk::Widget* box = leftDock_.get_tab_label(*leftDock_.get_nth_page(i));
        auto* label = box && box->get_first_child() ? dynamic_cast<Gtk::Label*>(box->get_first_child()->get_next_sibling()) : nullptr;
        if (!label) return;
        int width = 0, height = 0;
        label->create_pango_layout(label->get_text())->get_pixel_size(width, height);
        tabs.push_back({box, label, width});
        if (box->get_parent() && box->get_parent()->get_width() > box->get_width() && box->get_width() > 0)
            chrome = box->get_parent()->get_width() - box->get_width();
    }
    constexpr int kIcon = 16, kSpacing = 5; // makeIconLabel's
    const int available = hpaned_.get_position() - 8;
    int used = static_cast<int>(tabs.size()) * (kIcon + chrome);
    std::vector<bool> named(tabs.size(), false);
    std::vector<int> order{leftDock_.get_current_page()};
    for (int i = 0; i < static_cast<int>(tabs.size()); ++i)
        if (i != order.front()) order.push_back(i); // then left to right: Project, Search, ...
    for (int i : order) {
        if (i < 0 || i >= static_cast<int>(tabs.size())) continue;
        const int more = kSpacing + tabs[i].text;
        if (used + more > available) continue;
        used += more;
        named[i] = true;
    }
    for (size_t i = 0; i < tabs.size(); ++i) {
        tabs[i].label->set_visible(named[i]);
        tabs[i].box->set_tooltip_text(named[i] ? Glib::ustring() : tabs[i].label->get_text());
    }
}

Gtk::Widget& MainWindow::buildEditorArea() {
    auto* findBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    findEntry_.set_width_chars(32);
    auto* prev = Gtk::make_managed<Gtk::Button>();
    prev->set_icon_name("go-up-symbolic");
    prev->set_tooltip_text("Previous match (Shift+Enter)");
    prev->signal_clicked().connect([this] { findNext(false); });
    auto* next = Gtk::make_managed<Gtk::Button>();
    next->set_icon_name("go-down-symbolic");
    next->set_tooltip_text("Next match (Enter)");
    next->signal_clicked().connect([this] { findNext(true); });
    findCount_.add_css_class("dim-label");
    findBox->append(findEntry_);
    findBox->append(*prev);
    findBox->append(*next);
    findBox->append(findCase_);
    auto* everywhere = Gtk::make_managed<Gtk::Button>();
    {
        auto* image = Gtk::make_managed<Gtk::Image>();
        image->set_from_icon_name("kevc-find-in-files");
        everywhere->set_child(*image);
    }
    everywhere->set_tooltip_text("Find this in all files (Ctrl+Shift+F)");
    everywhere->signal_clicked().connect([this] { findInFiles(findEntry_.get_text()); });
    findBox->append(*everywhere);
    {
        auto* image = Gtk::make_managed<Gtk::Image>();
        image->set_from_icon_name("kevc-replace");
        replaceToggle_.set_child(*image);
    }
    replaceToggle_.set_tooltip_text("Replace (Ctrl+R)");
    replaceToggle_.signal_toggled().connect([this] { replaceRow_.set_visible(replaceToggle_.get_active()); });
    findBox->append(replaceToggle_);
    findBox->append(findCount_);
    // The Replace row, under the find field: Ctrl+R or the toggle shows it.
    replaceEntry_.set_placeholder_text("Replace with…");
    replaceEntry_.set_width_chars(32);
    replaceEntry_.signal_activate().connect([this] { replaceNext(); });
    auto* replaceOne = Gtk::make_managed<Gtk::Button>("Replace");
    replaceOne->set_tooltip_text("Replace this match and go to the next (Enter)");
    replaceOne->signal_clicked().connect([this] { replaceNext(); });
    auto* replaceEvery = Gtk::make_managed<Gtk::Button>("Replace All");
    replaceEvery->set_tooltip_text("Replace every match in this file (one Ctrl+Z undoes it)");
    replaceEvery->signal_clicked().connect([this] { replaceAllInFile(); });
    replaceRow_.append(replaceEntry_);
    replaceRow_.append(*replaceOne);
    replaceRow_.append(*replaceEvery);
    replaceRow_.set_visible(false);
    auto* findRows = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    findRows->append(*findBox);
    findRows->append(replaceRow_);
    findBar_.set_child(*findRows);
    findBar_.connect_entry(findEntry_);
    findBar_.set_show_close_button(true);
    findEntry_.signal_search_changed().connect([this] { findNext(true, true); });
    findEntry_.signal_activate().connect([this] { findNext(true); });
    findEntry_.signal_next_match().connect([this] { findNext(true); });
    findEntry_.signal_previous_match().connect([this] { findNext(false); });
    findEntry_.signal_stop_search().connect([this] {
        findBar_.set_search_mode(false);
        if (auto* tab = currentEditor()) {
            tab->clearSearch();
            tab->focusEditor();
        }
    });
    findCase_.signal_toggled().connect([this] { findNext(true, true); });

    editors_.set_scrollable(true);
    editors_.set_vexpand(true);
    showWelcome();

    editorArea_.append(findBar_);
    editorArea_.append(editors_);
    locationsPopover_.set_parent(editorArea_);
    locationsPopover_.set_position(Gtk::PositionType::BOTTOM);
    locationsPopover_.set_halign(Gtk::Align::START); // rightwards from the word, not centred on it
    // The editors' right-click menu: Go to… first (C/C++ also has declarations, in headers).
    auto lookups = [](bool declaration) {
        auto menu = Gio::Menu::create();
        auto section = Gio::Menu::create();
        section->append("Go to _Definition", "win.goto-definition");
        if (declaration) section->append("Go to D_eclaration", "win.goto-declaration");
        section->append("Go to _Implementation", "win.goto-implementation");
        section->append("Go to T_ype Definition", "win.goto-type-definition");
        section->append("Find All _References", "win.find-references");
        menu->append_section(section);
        // Right-clicking a line number shows this menu too, for that line.
        auto breakpoints = Gio::Menu::create();
        breakpoints->append("Toggle _Breakpoint", "win.toggle-breakpoint");
        breakpoints->append("I_F Breakpoint…", "win.if-breakpoint");
        breakpoints->append("_Live Breakpoint…", "win.live-breakpoint");
        breakpoints->append("Break When Value C_hanges…", "win.value-watch");
        menu->append_section(breakpoints);
        return menu;
    };
    editorMenu_ = lookups(false);
    cppEditorMenu_ = lookups(true);
    gutterMenu_ = Gio::Menu::create();
    gutterMenu_->append("Toggle _Breakpoint", "win.toggle-breakpoint");
    gutterMenu_->append("I_F Breakpoint…", "win.if-breakpoint");
    gutterMenu_->append("_Live Breakpoint…", "win.live-breakpoint");
    gutterMenu_->append("Break When Value C_hanges…", "win.value-watch");
    return editorArea_;
}

Gtk::Widget& MainWindow::buildBottomDock() {
    compileLog_.set_editable(false);
    compileLog_.set_monospace(true);
    compileLog_.add_css_class("kevc-log");
    compileLog_.set_cursor_visible(false);
    auto* logScroller = Gtk::make_managed<Gtk::ScrolledWindow>();
    logScroller->set_child(compileLog_);

    auto buffer = compileLog_.get_buffer();
    buffer->create_tag("command")->property_weight() = Pango::Weight::BOLD;
    buffer->create_tag("summary")->property_weight() = Pango::Weight::BOLD;
    buffer->create_tag("error")->property_foreground() = "#ff6b6b";
    buffer->create_tag("warning")->property_foreground() = "#f0c050";
    buffer->create_tag("success")->property_foreground() = "#7bd88f";
    logEnd_ = buffer->create_mark("end", buffer->end(), false);

    // Every panel can be dragged to the right dock or out into its own window, or moved from the
    // menu on its tab.
    docks_ = std::make_unique<DockManager>(*this, bottomDock_, rightDock_, rightPaned_);
    docks_->add("compiler", "Compiler", compilerPanel_, "kevc-error-list");
    docks_->add("compile-log", "Compile Log", *logScroller, "kevc-compile");
    docks_->add("run", "Run", runConsole_, "kevc-run");
    docks_->add("debug", "Debug", debugPanel_, "kevc-debug");
    docks_->add("terminal", "Terminal", shell_, "kevc-terminal");
    docks_->add("dev-server", "Dev Server", buildServerPage(), "kevc-globe");
    docks_->add("env", "Environment", envPanel_, "kevc-const-variable");
    docks_->add("tests", "Tests", testsPanel_, "kevc-bool-variable");
    docks_->add("traces", "Traces", tracesPanel_, "kevc-call-stack");
    docks_->add("deps", "Dependencies", depsPanel_, "kevc-workspace");
    bottomDock_.set_size_request(-1, 120);
    docks_->acceptFloatingDrops(root_);
    docks_->signalClosedChanged().connect([this](const std::string& id, bool closed, bool byUser) {
        if (byUser) setPanelShownHere(id, !closed);
        if (auto it = panelActions_.find(id); it != panelActions_.end())
            it->second->set_state(Glib::Variant<bool>::create(!closed));
    });
    // Put panels back where they were last time, once the main window is up.
    Glib::signal_idle().connect_once([this] { docks_->restoreLayout(); });
    return bottomDock_;
}

Gtk::Widget& MainWindow::buildStatusBar() {
    status_.set_xalign(0.0f);
    status_.set_ellipsize(Pango::EllipsizeMode::END);
    status_.set_margin_start(8);
    status_.set_margin_end(8);
    status_.set_margin_top(2);
    status_.set_margin_bottom(2);
    return status_;
}

// ---------------------------------------------------------------------------------------------
// Projects

void MainWindow::openPath(const fs::path& path) {
    std::error_code ec;
    const fs::path abs = fs::absolute(path, ec);
    if (fs::is_directory(abs, ec)) openFolder(abs);
    else if (abs.extension() == ".kevc") openFolder(abs.parent_path());
    else if (fs::is_regular_file(abs, ec)) openDocument(abs);
    else showError(*this, "Cannot open " + abs.string(), "The path does not exist.");
}

void MainWindow::newProject() {
    const fs::path location = session_ ? session_->root().parent_path() : defaultProjectLocation();
    auto* dialog = new NewProjectDialog(*this, location,
        [this](const std::string& name, const fs::path& parent, ProjectTemplate tmpl) -> Glib::ustring {
            if (tmpl == ProjectTemplate::SvelteKit) return createSvelteKitApp(name, parent);
            try {
                auto session = std::make_unique<ProjectSession>(ProjectSession::create(parent, name, tmpl));
                closeAllEditors([this, s = std::shared_ptr<ProjectSession>(session.release())]() mutable {
                    setProject(std::make_unique<ProjectSession>(std::move(*s)));
                });
                return {};
            } catch (const std::exception& e) {
                return e.what();
            }
        });
    dialog->present();
}

void MainWindow::chooseFolder() {
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Open Project or Folder");
    dialog->set_initial_folder(Gio::File::create_for_path(
        (session_ ? session_->root().parent_path() : defaultProjectLocation()).string()));
    dialog->select_folder(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
            if (auto folder = dialog->select_folder_finish(result)) openFolder(folder->get_path());
        } catch (const Glib::Error&) {
            // Cancelled.
        }
    });
}

void MainWindow::openFolder(const fs::path& root) {
    closeAllEditors([this, root] {
        std::error_code ec;
        try {
            if (auto kevc = ProjectSession::findProjectFile(root)) {
                setProject(std::make_unique<ProjectSession>(ProjectSession::open(*kevc)));
            } else if (fs::exists(root / "CMakeLists.txt", ec)) {
                const std::string kevcName = root.filename().string() + ".kevc";
                askChoice(*this, "How should Kev-C++ build this CMake project?",
                          "It wasn't created by Kev-C++.\n\n"
                          "Build As It Is: Kev-C++ builds, runs and debugs it with its own CMakeLists.txt, which "
                          "it never edits; it only adds " + kevcName + ". Best for projects with several targets "
                          "or their own way of listing sources.\n\n"
                          "Manage Sources: Kev-C++ also keeps the source list for you, in a marked block it adds "
                          "at the end of CMakeLists.txt (nothing else in the file is changed).",
                          {"Cancel", "Open as Plain Folder", "Manage Sources", "Build As It Is"}, 0, 3,
                          [this, root](int choice) {
                              if (choice == 1) setPlainFolder(root);
                              if (choice != 2 && choice != 3) return;
                              try {
                                  setProject(std::make_unique<ProjectSession>(choice == 3 ? ProjectSession::useCMakeAsIs(root)
                                                                                          : ProjectSession::adoptCMake(root)));
                              } catch (const std::exception& e) {
                                  showError(*this, "Could not open the project", e.what());
                              }
                          });
            } else if (fs::exists(root / "package.json", ec)) {
                setProject(std::make_unique<ProjectSession>(ProjectSession::openWebFolder(root)));
                setStatus("Opened " + session_->project().name + " as a " + web_.label() +
                          " project (it has a package.json)" +
                          (fs::exists(root / "node_modules", ec) || web_.dependencies.empty()
                               ? std::string()
                               : ". Its packages aren't installed yet: Project → Install Dependencies"));
            } else {
                setPlainFolder(root);
                if (fs::exists(root / "Makefile", ec))
                    setStatus("Opened as a plain folder: existing Makefiles are not managed by Kev-C++");
            }
        } catch (const std::exception& e) {
            showError(*this, "Could not open project", e.what());
        }
    });
}

void MainWindow::setProject(std::unique_ptr<ProjectSession> session) {
    saveProjectState(); // the project being left
    if (debugger_) debugger_->terminate();
    if (jsDebugger_) jsDebugger_->stop();
    if (devServer_ && devServer_->root() != session->root()) devServer_.reset();
    updateServerPage();
    breakpoints_.clear();
    valueWatches_.clear();
    clearLiveValues();
    updateBreakpointsPanel();
    watcher_.reset();
    collector_.reset();
    traces_.clear();
    tracesPanel_.setTraces(traces_);
    session_ = std::move(session);
    folder_ = session_->root();
    restoreProjectState();
    rememberFolder(folder_);
    refreshWebProject();
    refreshKit();
    lsp_->setWorkspace(folder_, &session_->project());
    // The Svelte server reads .svelte-kit/tsconfig.json at startup and doesn't notice it appearing
    // later, so generate it first in a project that has never been synced (sveltejs/language-tools#1529).
    if (std::error_code ec; web_.flavor == WebFlavor::SvelteKit && !fs::exists(folder_ / ".svelte-kit/tsconfig.json", ec)) {
        if (const fs::path kit = nodeBin(folder_, "svelte-kit"); !kit.empty()) {
            try {
                auto launcher = Gio::SubprocessLauncher::create(Gio::Subprocess::Flags::STDOUT_SILENCE |
                                                                Gio::Subprocess::Flags::STDERR_SILENCE);
                launcher->set_cwd(folder_.string());
                auto sync = launcher->spawn({app_.toolchain().pathOf("node").string(), kit.string(), "sync"});
                const fs::path root = folder_;
                sync->wait_async([this, sync, root, token = std::weak_ptr<int>(lifetime_)](const Glib::RefPtr<Gio::AsyncResult>&) {
                    if (token.expired() || !session_ || session_->root() != root) return;
                    lsp_->setWorkspace(folder_, &session_->project()); // restart the servers with the new config
                });
            } catch (const Glib::Error&) {
                // F9 (svelte-check) runs the sync as well.
            }
        }
    }
    projectTree_.setRoot(folder_);
    searchPanel_.setRoot(folder_);
    quickSearch_.setRoot(folder_);
    shell_.startShell(folder_);
    updateTitle();
    applyProfile();
    updateToolbar(); // web projects run by serving

    watcher_ = std::make_unique<SourceWatcher>(*session_);
    watcher_->signalSynced().connect(sigc::mem_fun(*this, &MainWindow::onProjectSynced));
    watcher_->syncNow(); // pick up files added while the project was closed

    const auto& sources = session_->project().sources;
    auto main = std::find_if(sources.begin(), sources.end(), [](const std::string& s) {
        const std::string stem = fs::path(s).stem().string();
        return stem == "main" && fs::path(s).extension() != ".h";
    });
    if (main != sources.end()) openFile(folder_ / *main);
    else if (!sources.empty()) openFile(folder_ / sources.front());
    // A GTK application's window design (as the GTK 4 Application templates make), in the designer.
    std::error_code ec;
    for (const char* design : {"src/main_window.ui", "main_window.ui"})
        if (fs::exists(folder_ / design, ec)) {
            openDesigner(folder_ / design);
            break;
        }
    setStatus("Opened " + session_->project().name + " (" + traitsOf(session_->project().kind).label + ")");
}

void MainWindow::closeProject() {
    if (folder_.empty()) return;
    saveProjectState(); // before closing the editors: they know where breakpoints moved
    closeAllEditors([this] {
        const std::string name = session_ ? session_->project().name : folder_.filename().string();
        const bool project = session_ != nullptr;
        // Everything the project was running stops with it.
        if (build_.running()) build_.cancel();
        if (runConsole_.childRunning()) runConsole_.stopChild();
        if (debugger_) debugger_->terminate();
        if (jsDebugger_) jsDebugger_->stop();
        devServer_.reset(); // stops the server
        updateServerPage();
        breakpoints_.clear();
        valueWatches_.clear();
        clearLiveValues();
        updateBreakpointsPanel();
        watches_.setExpressions({});
        watcher_.reset();
        collector_.reset();
        traces_.clear();
        tracesPanel_.setTraces(traces_);
        compilerPanel_.clear(); // its errors point into the project
        compileLog_.get_buffer()->set_text("");

        session_.reset();
        folder_.clear();
        lspRootForLooseFiles_.clear();
        refreshWebProject();
        refreshKit(); // the Routes tab goes
        lsp_->setWorkspace({}, nullptr);
        projectTree_.setRoot({});
        searchPanel_.setRoot({});
        quickSearch_.setRoot({});
        watchGit(); // with no folder: forgets the repository and its icons
        // The Terminal tab keeps its shell: something may be running in it.
        updateTitle();
        applyProfile();
        updateToolbar();
        showWelcome();
        setStatus("Closed " + std::string(project ? "project " : "folder ") + name);
    });
}

void MainWindow::setPlainFolder(const fs::path& root) {
    saveProjectState();
    if (debugger_) debugger_->terminate();
    if (jsDebugger_) jsDebugger_->stop();
    if (devServer_ && devServer_->root() != root) devServer_.reset();
    updateServerPage();
    breakpoints_.clear();
    valueWatches_.clear();
    clearLiveValues();
    updateBreakpointsPanel();
    watcher_.reset();
    collector_.reset();
    traces_.clear();
    tracesPanel_.setTraces(traces_);
    session_.reset();
    folder_ = root;
    restoreProjectState();
    refreshWebProject();
    refreshKit();
    lsp_->setWorkspace(folder_, nullptr);
    projectTree_.setRoot(folder_);
    searchPanel_.setRoot(folder_);
    quickSearch_.setRoot(folder_);
    shell_.startShell(folder_);
    updateTitle();
    applyProfile();
    updateToolbar(); // web projects run by serving
    rememberFolder(folder_);
    setStatus("Opened folder " + root.string());
}

void MainWindow::onProjectSynced(const ProjectSession::SyncResult& result) {
    if (result.changed()) scheduleKitRefresh(); // routes added or removed
    refreshGitStatus();                         // files added or removed
    const std::string target = session_ && !session_->buildFile().empty()
                                   ? session_->buildFile().filename().string()
                                   : std::string("the project");
    Glib::ustring msg;
    if (!result.added.empty()) msg += "Added " + joinList(result.added) + " to " + target + ". ";
    if (!result.removed.empty()) msg += "Removed " + joinList(result.removed) + " from " + target + ". ";
    for (const auto& w : result.warnings) msg += w + ". ";
    setStatus(msg);
}

void MainWindow::updateTitle() {
    if (session_)
        set_title(session_->project().name + " — Kev-C++ (" +
                  (session_->project().kind == ProjectKind::WebTs ? web_.label() : traitsOf(session_->project().kind).label) + ")");
    else if (!folder_.empty()) set_title(folder_.filename().string() + " — Kev-C++");
    else set_title("Kev-C++");
}

// ---------------------------------------------------------------------------------------------
// Editors

EditorTab* MainWindow::currentEditor() {
    const int page = editors_.get_current_page();
    return page < 0 ? nullptr : dynamic_cast<EditorTab*>(editors_.get_nth_page(page));
}

std::vector<EditorTab*> MainWindow::editors() {
    std::vector<EditorTab*> out;
    for (int i = 0; i < editors_.get_n_pages(); ++i)
        if (auto* tab = dynamic_cast<EditorTab*>(editors_.get_nth_page(i))) out.push_back(tab);
    return out;
}

DocumentPage* MainWindow::currentDocument() {
    const int page = editors_.get_current_page();
    return page < 0 ? nullptr : dynamic_cast<DocumentPage*>(editors_.get_nth_page(page));
}

std::vector<DocumentPage*> MainWindow::documents() {
    std::vector<DocumentPage*> out;
    for (int i = 0; i < editors_.get_n_pages(); ++i)
        if (auto* page = dynamic_cast<DocumentPage*>(editors_.get_nth_page(i))) out.push_back(page);
    return out;
}

DocumentPage* MainWindow::findDocument(const fs::path& file) {
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(file, ec);
    for (DocumentPage* page : documents())
        if (fs::weakly_canonical(page->path(), ec) == canonical) return page;
    return nullptr;
}

void MainWindow::openDocument(const fs::path& file) {
    if (file.extension() == ".ui") openDesigner(file);
    else openFile(file);
}

void MainWindow::addDocumentPage(DocumentPage& page) {
    if (welcomePage_) {
        editors_.remove_page(*welcomePage_);
        welcomePage_ = nullptr;
        welcomeLists_ = nullptr;
        welcomeRows_.clear();
    }
    const int index = editors_.append_page(page.pageWidget(), page.tabLabel());
    editors_.set_tab_reorderable(page.pageWidget(), true);
    page.signalCloseRequested().connect([this, &page] { closeEditor(&page); });
    editors_.set_current_page(index);
}

DesignerPage* MainWindow::openDesigner(const fs::path& file) {
    if (DocumentPage* open = findDocument(file)) {
        editors_.set_current_page(editors_.page_num(open->pageWidget()));
        open->focusPage();
        return dynamic_cast<DesignerPage*>(open);
    }
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(file, ec);
    DesignerPage* page = nullptr;
    try {
        page = Gtk::make_managed<DesignerPage>(canonical);
    } catch (const Glib::Error& e) {
        showError(*this, "Cannot open " + file.filename().string(), e.what());
        return nullptr;
    } catch (const std::exception& e) {
        showError(*this, "Cannot open " + file.filename().string(), e.what());
        return nullptr;
    }
    page->signalStatus().connect([this](const Glib::ustring& text) { setStatus(text); });
    page->signalOpenFile().connect([this](const fs::path& file, int line, int column) { openFile(file, line, column); });
    page->signalCodeWritten().connect([this](const std::string& module) { addSystemPackage(module); });
    addDocumentPage(*page);
    page->focusPage();
    return page;
}

EditorTab* MainWindow::openFile(const fs::path& file, int line, int column) {
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(file, ec);
    if (DocumentPage* open = findDocument(canonical)) {
        editors_.set_current_page(editors_.page_num(open->pageWidget()));
        auto* tab = dynamic_cast<EditorTab*>(open);
        if (tab && line > 0) tab->gotoLine(line, column);
        else open->focusPage();
        return tab; // nullptr when the file is open in the designer
    }

    EditorTab* tab = nullptr;
    try {
        const bool cppHeaders = !session_ || session_->project().kind == ProjectKind::Cpp;
        tab = Gtk::make_managed<EditorTab>(canonical, cppHeaders);
    } catch (const Glib::Error& e) {
        showError(*this, "Cannot open " + file.filename().string(), e.what());
        return nullptr;
    } catch (const std::exception& e) {
        showError(*this, "Cannot open " + file.filename().string(), e.what());
        return nullptr;
    }

    if (folder_.empty() && lspRootForLooseFiles_.empty()) {
        // A file opened on its own: give the language servers its folder as the workspace.
        lspRootForLooseFiles_ = canonical.parent_path();
        lsp_->setWorkspace(lspRootForLooseFiles_, nullptr);
    }
    setupIntelliSense(tab);
    addDocumentPage(*tab);
    tab->signalMatchCountChanged().connect([this, tab](int count) {
        if (tab == currentEditor() && findBar_.get_search_mode())
            findCount_.set_text(count < 0 ? "" : std::to_string(count) + (count == 1 ? " match" : " matches"));
    });
    if (line > 0) tab->gotoLine(line, column);
    else tab->focusEditor();
    return tab;
}

// The designer wrote gtkmm or C code: the project has to build against the library it uses.
void MainWindow::addSystemPackage(const std::string& module) {
    if (!session_ || session_->project().kind == ProjectKind::WebTs) return;
    auto& packages = session_->project().packages;
    if (std::any_of(packages.begin(), packages.end(), [&](const Package& p) { return p.name == module; })) return;
    packages.push_back(Package{Package::Source::System, module, {}, {}, {}, {}});
    try {
        session_->saveAndRegenerate();
        lsp_->setWorkspace(session_->root(), &session_->project());
        setStatus("Added the " + module + " package to the project (pkg-config)");
    } catch (const std::exception& e) {
        showError(*this, "Could not add " + module + " to the project", e.what());
    }
}

void MainWindow::newFile() {
    if (folder_.empty()) {
        showError(*this, "No project open", "Create or open a project first (File → New Project).");
        return;
    }
    const std::string suggestion = session_ ? traitsOf(session_->project().kind).newFileName : "untitled.txt";
    promptText(*this, "New Source File", "Path relative to " + folder_.filename().string() + ":", suggestion,
               [this](const Glib::ustring& text) -> Glib::ustring {
                   auto target = resolveInside(folder_, text);
                   if (!target) return "Enter a relative path inside the project.";
                   std::error_code ec;
                   if (fs::exists(*target, ec)) return target->filename().string() + " already exists.";
                   try {
                       fs::create_directories(target->parent_path());
                       const std::string ext = target->extension().string();
                       std::ofstream out(*target);
                       if (!out) return "Could not create the file.";
                       if (ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx") out << "#pragma once\n\n";
                   } catch (const std::exception& e) {
                       return e.what();
                   }
                   openDocument(*target);
                   // The SourceWatcher adds it to the build file shortly after.
                   return {};
               });
}

void MainWindow::newFileIn(const fs::path& dir) {
    const std::string where = dir == folder_ ? folder_.filename().string() : dir.lexically_relative(folder_).generic_string();
    const std::string suggestion = session_ ? traitsOf(session_->project().kind).newFileName : "untitled.txt";
    promptText(*this, "New File", "File name (in " + where + "):", fs::path(suggestion).filename().string(),
               [this, dir](const Glib::ustring& text) -> Glib::ustring {
                   const std::string name = text;
                   if (auto error = fileops::checkName(dir, name); !error.empty()) return error;
                   const fs::path target = dir / name;
                   {
                       std::ofstream out(target);
                       if (!out) return "Could not create " + name + ".";
                       const std::string ext = target.extension().string();
                       if (ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx") out << "#pragma once\n\n";
                   }
                   openDocument(target); // the SourceWatcher adds it to the build file shortly after
                   filesChanged();
                   return {};
               });
}

void MainWindow::newFolderIn(const fs::path& dir) {
    const std::string where = dir == folder_ ? folder_.filename().string() : dir.lexically_relative(folder_).generic_string();
    promptText(*this, "New Folder", "Folder name (in " + where + "):", "", [this, dir](const Glib::ustring& text) -> Glib::ustring {
        const std::string name = text;
        if (auto error = fileops::checkName(dir, name); !error.empty()) return error;
        std::error_code ec;
        fs::create_directory(dir / name, ec);
        if (ec) return "Could not create " + name + ": " + ec.message();
        setStatus("Created folder " + name);
        return {};
    });
}

void MainWindow::renamePath(const fs::path& path) {
    std::error_code ec;
    const bool folder = fs::is_directory(path, ec);
    const std::string name = path.filename().string();
    promptText(*this, folder ? "Rename Folder" : "Rename File", "New name for " + name + ":", name,
               [this, path](const Glib::ustring& text) -> Glib::ustring {
                   const std::string newName = text;
                   if (auto error = fileops::checkName(path.parent_path(), newName, path); !error.empty()) return error;
                   if (newName == path.filename().string()) return {}; // unchanged
                   std::error_code ec;
                   const fs::path from = fs::weakly_canonical(path, ec);
                   const fs::path to = path.parent_path() / newName;
                   if (auto error = fileops::move(path, to); !error.empty()) return "Could not rename: " + error;
                   relocated(from, fs::weakly_canonical(to, ec));
                   setStatus("Renamed " + path.filename().string() + " to " + newName);
                   return {};
               },
               fileops::stemLength(name, folder)); // typing replaces "main" and keeps ".cpp"
}

void MainWindow::deletePath(const fs::path& path) {
    std::error_code ec;
    const bool folder = fs::is_directory(path, ec);
    const fs::path canonical = fs::weakly_canonical(path, ec);
    std::string unsaved;
    for (DocumentPage* doc : documents())
        if (fileops::isInside(doc->path(), canonical) && doc->modified())
            unsaved += (unsaved.empty() ? "" : ", ") + doc->path().filename().string();
    std::string detail = folder ? "The folder and everything in it go to the trash." : "It goes to the trash, where you can restore it.";
    if (!unsaved.empty()) detail += " Unsaved changes to " + unsaved + " will be lost.";
    const std::string name = path.filename().string();
    askChoice(*this, "Delete " + std::string(folder ? "the folder " : "") + "\u201c" + name + "\u201d?", detail,
              {"Cancel", "Move to Trash"}, 0, 1, [this, path, canonical, name](int choice) {
                  if (choice != 1) return;
                  // Close its editors first, so nothing saves it back while it's going.
                  auto closeEditors = [this, canonical] {
                      for (DocumentPage* doc : documents())
                          if (fileops::isInside(doc->path(), canonical)) removeEditor(doc);
                      std::erase_if(breakpoints_, [&](const auto& entry) { return fileops::isInside(entry.first, canonical); });
                      updateBreakpointsPanel();
                      saveProjectState();
                  };
                  try {
                      Gio::File::create_for_path(path.string())->trash();
                      closeEditors();
                      setStatus("Moved " + name + " to the trash");
                      filesChanged();
                  } catch (const Glib::Error& e) {
                      // No trash here (some mounted or removable file systems): offer to delete it for good.
                      askChoice(*this, "\u201c" + name + "\u201d can't go to the trash.",
                                std::string(e.what()) + "\n\nDelete it permanently? This can't be undone.",
                                {"Cancel", "Delete Permanently"}, 0, 0, [this, path, name, closeEditors](int again) {
                                    if (again != 1) return;
                                    std::error_code ec;
                                    fs::remove_all(path, ec);
                                    if (ec) {
                                        showError(*this, "Could not delete " + name, ec.message());
                                        return;
                                    }
                                    closeEditors();
                                    setStatus("Deleted " + name);
                                    filesChanged();
                                });
                  }
              });
}

void MainWindow::movePath(const fs::path& source, const fs::path& targetDir) {
    if (fileops::sameFolder(source, targetDir)) return;
    const std::string name = source.filename().string();
    if (auto error = fileops::checkMove(source, targetDir); !error.empty()) {
        showError(*this, "Can't move " + name, error);
        return;
    }
    std::error_code ec;
    const fs::path from = fs::weakly_canonical(source, ec);
    const fs::path to = targetDir / source.filename();
    if (auto error = fileops::move(source, to); !error.empty()) {
        showError(*this, "Could not move " + name, error);
        return;
    }
    relocated(from, fs::weakly_canonical(to, ec));
    const fs::path rel = targetDir.lexically_relative(folder_);
    setStatus("Moved " + name + " to " + (rel.empty() || rel == "." ? folder_.filename().string() : rel.generic_string()));
}

void MainWindow::relocated(const fs::path& from, const fs::path& to) {
    for (DocumentPage* doc : documents()) {
        const fs::path moved = fileops::relocated(doc->path(), from, to);
        if (moved.empty()) continue;
        // The language servers know files by path: close the old one, open the new.
        auto* tab = dynamic_cast<EditorTab*>(doc);
        if (tab) lsp_->detach(tab);
        doc->moveTo(moved);
        if (tab) lsp_->attach(tab);
    }
    BreakpointMap breakpoints;
    for (auto& [file, lines] : breakpoints_) {
        const fs::path moved = fileops::relocated(file, from, to);
        breakpoints[moved.empty() ? file : moved] = lines;
    }
    breakpoints_ = std::move(breakpoints);
    updateBreakpointsPanel();
    saveProjectState();
    filesChanged();
}

void MainWindow::filesChanged() {
    // C and C++ build files follow by themselves (SourceWatcher); these views are refreshed here.
    scheduleKitRefresh();
    refreshGitStatus();
}

void MainWindow::saveCurrent() {
    DocumentPage* page = currentDocument();
    if (!page) return;
    try {
        page->save();
        setStatus("Saved " + page->path().filename().string());
        refreshGitStatus();
    } catch (const Glib::Error& e) {
        showError(*this, "Could not save " + page->path().filename().string(), e.what());
    }
}

bool MainWindow::saveAll() {
    int saved = 0;
    for (DocumentPage* page : documents()) {
        if (!page->modified()) continue;
        try {
            page->save();
            ++saved;
        } catch (const Glib::Error& e) {
            showError(*this, "Could not save " + page->path().filename().string(), e.what());
            return false;
        }
    }
    if (saved) setStatus("Saved " + std::to_string(saved) + (saved == 1 ? " file" : " files"));
    if (saved) refreshGitStatus();
    return true;
}

void MainWindow::closeEditor(DocumentPage* page) {
    if (!page->modified()) {
        removeEditor(page);
        return;
    }
    askChoice(*this, "Save changes to " + page->path().filename().string() + "?", "Unsaved changes will be lost.",
              {"Cancel", "Discard", "Save"}, 0, 2, [this, page](int choice) {
                  if (choice == 2) {
                      try {
                          page->save();
                      } catch (const Glib::Error& e) {
                          showError(*this, "Could not save " + page->path().filename().string(), e.what());
                          return;
                      }
                  }
                  if (choice == 1 || choice == 2) removeEditor(page);
              });
}

void MainWindow::closeAllEditors(std::function<void()> then) {
    auto docs = documents();
    const auto unsaved = std::count_if(docs.begin(), docs.end(), [](DocumentPage* d) { return d->modified(); });
    auto closeAll = [this, then] {
        for (DocumentPage* page : documents()) removeEditor(page);
        then();
    };
    if (unsaved == 0) {
        closeAll();
        return;
    }
    askChoice(*this, "Save changes to " + std::to_string(unsaved) + (unsaved == 1 ? " file?" : " files?"),
              "Unsaved changes will be lost.", {"Cancel", "Discard", "Save All"}, 0, 2,
              [this, closeAll](int choice) {
                  if (choice == 2 && !saveAll()) return;
                  if (choice == 1 || choice == 2) closeAll();
              });
}

void MainWindow::removeEditor(DocumentPage* page) {
    if (auto* tab = dynamic_cast<EditorTab*>(page)) {
        syncBreakpoints(tab);
        if (execTab_ == tab) execTab_ = nullptr;
        lsp_->detach(tab);
    }
    editors_.remove_page(page->pageWidget());
    if (editors_.get_n_pages() == 0) showWelcome(); // so the recent projects are a click away again
}

// ---------------------------------------------------------------------------------------------
// Welcome

void MainWindow::showWelcome() {
    if (welcomePage_) return;
    auto* shortcuts = Gtk::make_managed<Gtk::Label>("Welcome to Kev-C++\n\n"
                                                    "Ctrl+Shift+N  new project\n"
                                                    "Ctrl+O        open a project or folder\n"
                                                    "Ctrl+P        search files and text\n"
                                                    "Ctrl+Shift+F  find in all files\n"
                                                    "F9 / F10 / F11  compile / run / compile & run\n"
                                                    "F5            debug   (F6 step over, F7 step into, F8 continue)");
    shortcuts->add_css_class("dim-label");
    shortcuts->set_xalign(0);
    shortcuts->set_valign(Gtk::Align::START);
    shortcuts->set_margin_top(20); // level with the first list, under its heading

    welcomeLists_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    welcomeLists_->set_size_request(380, -1);
    welcomeLists_->set_valign(Gtk::Align::START);
    welcomeLists_->add_css_class("kevc-welcome-projects");

    auto* columns = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 48);
    columns->set_halign(Gtk::Align::CENTER);
    columns->set_valign(Gtk::Align::CENTER);
    columns->set_margin(24);
    columns->append(*welcomeLists_);
    columns->append(*shortcuts);
    auto* scroller = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroller->set_child(*columns);
    welcomePage_ = scroller;
    editors_.append_page(*welcomePage_, "Welcome");
    refreshWelcome();
}

void MainWindow::refreshWelcome() {
    if (!welcomeLists_) return;
    while (Gtk::Widget* child = welcomeLists_->get_first_child()) welcomeLists_->remove(*child);
    welcomeRows_.clear();
    std::error_code ec;
    auto section = [&](const Glib::ustring& title, const std::vector<fs::path>& folders, bool pinned) {
        if (folders.empty()) return;
        auto* heading = Gtk::make_managed<Gtk::Label>();
        heading->set_markup("<b>" + Glib::Markup::escape_text(title) + "</b>");
        heading->set_xalign(0);
        heading->set_margin_top(welcomeLists_->get_first_child() ? 12 : 0);
        welcomeLists_->append(*heading);
        auto* list = Gtk::make_managed<Gtk::ListBox>();
        list->set_selection_mode(Gtk::SelectionMode::NONE);
        list->add_css_class("kevc-welcome-list");
        for (const auto& folder : folders) list->append(welcomeRow(folder, pinned, *list));
        list->signal_row_activated().connect([this](Gtk::ListBoxRow* row) {
            auto it = welcomeRows_.find(row);
            if (it == welcomeRows_.end()) return;
            const fs::path folder = it->second;
            std::error_code missing;
            if (!fs::is_directory(folder, missing)) {
                setStatus(folder.string() + " no longer exists");
                return;
            }
            openPath(folder);
        });
        welcomeLists_->append(*list);
    };
    section("Pinned", recent_.pinned(), true);
    std::vector<fs::path> recent;
    for (const auto& folder : recent_.recent())
        if (fs::is_directory(folder, ec)) recent.push_back(folder); // gone folders aren't worth a row
    section("Recent", recent, false);
    if (!welcomeLists_->get_first_child()) {
        auto* hint = Gtk::make_managed<Gtk::Label>("Projects and folders you open are listed here.\n"
                                                   "Pin one to keep it at the top.");
        hint->add_css_class("dim-label");
        hint->set_xalign(0);
        welcomeLists_->append(*hint);
    }
}

Gtk::Widget& MainWindow::welcomeRow(const fs::path& folder, bool pinned, Gtk::ListBox&) {
    std::error_code ec;
    const bool exists = fs::is_directory(folder, ec);
    auto* pin = Gtk::make_managed<Gtk::Button>();
    pin->set_icon_name(pinned ? "kevc-pinned" : "kevc-unpinned");
    pin->set_has_frame(false);
    pin->set_valign(Gtk::Align::CENTER);
    pin->set_tooltip_text(pinned ? "Unpin" : "Pin, to keep it at the top of this list");
    pin->add_css_class("kevc-pin");
    pin->signal_clicked().connect([this, folder, pinned] {
        recent_.setPinned(folder, !pinned);
        // Later: rebuilding the list now would destroy this button inside its own handler.
        Glib::signal_idle().connect_once(sigc::track_obj([this] { refreshWelcome(); }, *this));
    });

    // ~ for the home folder, so the paths stay short.
    std::string shown = folder.string();
    if (const char* home = std::getenv("HOME"); home && *home && shown.rfind(std::string(home) + "/", 0) == 0)
        shown = "~" + shown.substr(std::strlen(home));
    auto* name = Gtk::make_managed<Gtk::Label>();
    name->set_markup("<b>" + Glib::Markup::escape_text(folder.filename().string()) + "</b>" +
                     (exists ? "" : "  <span alpha=\"60%\">(not found)</span>"));
    name->set_xalign(0);
    name->set_ellipsize(Pango::EllipsizeMode::END);
    name->set_max_width_chars(48);
    auto* path = Gtk::make_managed<Gtk::Label>(shown);
    path->set_xalign(0);
    path->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
    path->set_max_width_chars(48); // long paths are shortened in the middle, not widening the list
    path->add_css_class("dim-label");
    auto* text = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    text->set_hexpand(true);
    text->append(*name);
    text->append(*path);

    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    box->set_margin(4);
    box->append(*pin);
    box->append(*text);
    auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
    row->set_child(*box);
    row->set_tooltip_text(exists ? "Open " + folder.string() : folder.string() + " no longer exists");
    welcomeRows_[row] = folder;
    return *row;
}

void MainWindow::rememberFolder(const fs::path& root) {
    recent_.opened(root);
    refreshWelcome();
    watchGit();
}

// ---------------------------------------------------------------------------------------------
// git's view of the project (icons in the Project tab; committing is left to git itself)

void MainWindow::watchGit() {
    gitTopLevel_.clear();
    gitSoon_.disconnect();
    if (gitMonitor_) gitMonitor_->cancel();
    gitMonitor_.reset();
    projectTree_.setGitChanges({}, {});
    if (folder_.empty()) return;
    const fs::path root = folder_;
    captureOutput(git::topLevelCommand(), root,
                  [this, root](std::string out, int status) {
                      if (root != folder_ || status != 0) return; // another folder by now, or not in git
                      std::istringstream lines(out);
                      std::string top, gitDir;
                      std::getline(lines, top);
                      std::getline(lines, gitDir);
                      if (top.empty()) return;
                      gitTopLevel_ = top;
                      try {
                          gitMonitor_ = Gio::File::create_for_path(gitDir)->monitor_directory();
                          gitMonitor_->signal_changed().connect(
                              [this](const Glib::RefPtr<Gio::File>& file, const Glib::RefPtr<Gio::File>&,
                                     Gio::FileMonitor::Event) {
                                  // add, commit, restore, checkout… rewrite the index (by way of index.lock).
                                  const std::string name = file ? file->get_basename() : "";
                                  if (name == "index" || name == "index.lock" || name == "HEAD") refreshGitStatus();
                              });
                      } catch (const Glib::Error&) {
                          // Without the watch, the timer and window activation still refresh it.
                      }
                      runGitStatus();
                  },
                  lifetime_);
}

void MainWindow::refreshGitStatus() {
    if (gitTopLevel_.empty() || gitSoon_.connected()) return;
    gitSoon_ = Glib::signal_timeout().connect(sigc::track_obj(
                                                  [this] {
                                                      runGitStatus();
                                                      return false;
                                                  },
                                                  *this),
                                              300);
}

void MainWindow::runGitStatus() {
    if (gitTopLevel_.empty()) return;
    if (gitRunning_) {
        gitAgain_ = true; // once this one is done
        return;
    }
    gitRunning_ = true;
    const fs::path root = folder_, top = gitTopLevel_;
    captureOutput(git::statusCommand(), root,
                  [this, root, top](std::string out, int status) {
                      gitRunning_ = false;
                      if (root != folder_) return;
                      if (status == 0) {
                          // git names files from the work tree's top; the tree spells them from root.
                          std::error_code ec;
                          const fs::path base = fs::weakly_canonical(root, ec);
                          auto inTree = [&](const std::string& rel) -> std::optional<std::string> {
                              const fs::path fromRoot = (top / rel).lexically_relative(base);
                              if (fromRoot.empty() || *fromRoot.begin() == "..") return std::nullopt;
                              return (root / fromRoot).string();
                          };
                          const git::Changes changes = git::parseStatus(out);
                          std::map<std::string, git::FileState> files;
                          std::set<std::string> folders;
                          for (const auto& [rel, state] : changes)
                              if (auto path = inTree(rel)) files[*path] = state;
                          for (const auto& rel : git::changedFolders(changes))
                              if (auto path = inTree(rel)) folders.insert(*path);
                          projectTree_.setGitChanges(std::move(files), std::move(folders));
                      }
                      if (gitAgain_) {
                          gitAgain_ = false;
                          refreshGitStatus();
                      }
                  },
                  lifetime_);
}

void MainWindow::forwardToEditor(const char* action) {
    if (auto* page = currentDocument()) page->activateTextAction(action);
}

// ---------------------------------------------------------------------------------------------
// Search

Glib::ustring MainWindow::selectedLineText() {
    EditorTab* tab = currentEditor();
    if (!tab) return {};
    GtkTextBuffer* text = GTK_TEXT_BUFFER(tab->buffer());
    GtkTextIter start, end;
    if (!gtk_text_buffer_get_selection_bounds(text, &start, &end) ||
        gtk_text_iter_get_line(&start) != gtk_text_iter_get_line(&end))
        return {};
    char* selected = gtk_text_buffer_get_text(text, &start, &end, FALSE);
    Glib::ustring out = selected;
    g_free(selected);
    return out;
}

void MainWindow::findInFiles(Glib::ustring text) {
    if (text.empty()) text = selectedLineText();
    leftDock_.set_visible(true);
    leftDock_.set_current_page(leftDock_.page_num(searchPanel_));
    searchPanel_.focusSearch(text);
}

void MainWindow::focusQuickSearch() {
    quickSearch_.focusSearch();
}

std::map<fs::path, std::string> MainWindow::unsavedEditorText() {
    std::map<fs::path, std::string> unsaved;
    for (EditorTab* tab : editors()) {
        if (!tab->modified()) continue;
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(tab->buffer()), &start, &end);
        char* text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(tab->buffer()), &start, &end, TRUE);
        unsaved[tab->path()] = text;
        g_free(text);
    }
    return unsaved;
}

void MainWindow::showFindBar() {
    if (!currentEditor()) return;
    if (const Glib::ustring selected = selectedLineText(); !selected.empty()) findEntry_.set_text(selected);
    findBar_.set_search_mode(true);
    findEntry_.grab_focus();
}

void MainWindow::showReplaceBar() {
    if (!currentEditor()) return;
    showFindBar();
    replaceToggle_.set_active(true);
    // With something to find already, the cursor goes where the next thing to type is.
    if (!findEntry_.get_text().empty()) replaceEntry_.grab_focus();
}

void MainWindow::replaceNext() {
    EditorTab* tab = currentEditor();
    if (!tab || findEntry_.get_text().empty()) return;
    if (!tab->replace(findEntry_.get_text(), replaceEntry_.get_text(), findCase_.get_active()))
        findCount_.set_text("No matches");
}

void MainWindow::replaceAllInFile() {
    EditorTab* tab = currentEditor();
    if (!tab || findEntry_.get_text().empty()) return;
    const int n = tab->replaceAll(findEntry_.get_text(), replaceEntry_.get_text(), findCase_.get_active());
    findCount_.set_text(n == 0 ? "No matches" : "Replaced " + std::to_string(n));
    setStatus(n == 0 ? std::string("Nothing to replace")
                     : "Replaced " + std::to_string(n) + (n == 1 ? " match" : " matches") + " in " +
                           tab->path().filename().string() + " (Ctrl+Z undoes it)");
}

void MainWindow::findNext(bool forward, bool incremental) {
    EditorTab* tab = currentEditor();
    if (!tab) return;
    const bool found = tab->find(findEntry_.get_text(), forward, findCase_.get_active(), incremental);
    if (findEntry_.get_text().empty()) findCount_.set_text("");
    else if (!found) findCount_.set_text("No matches");
}

void MainWindow::gotoLine() {
    if (!currentEditor()) return;
    promptText(*this, "Go to Line", "Line number (optionally line:column):", "", [this](const Glib::ustring& text) {
        int line = 0, column = 0;
        if (std::sscanf(text.c_str(), "%d:%d", &line, &column) < 1 || line < 1) return Glib::ustring("Enter a line number.");
        if (auto* tab = currentEditor()) tab->gotoLine(line, column);
        return Glib::ustring();
    });
}

// ---------------------------------------------------------------------------------------------
// Build & run

BuildConfig MainWindow::currentConfig() const {
    return configDrop_ && configDrop_->get_selected() == 1 ? BuildConfig::Release : BuildConfig::Debug;
}

BuildTools MainWindow::buildTools() const {
    const Toolchain& tc = app_.toolchain();
    BuildTools tools;
    tools.gcc = tc.pathOf("gcc").string();
    tools.gxx = tc.pathOf("g++").string();
    tools.cmake = tc.pathOf("cmake").string();
    tools.make = tc.pathOf("make").string();
    const ToolInfo* ninja = tc.get("ninja");
    tools.ninja = ninja && ninja->found();
    tools.jobs = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    tools.node = tc.pathOf("node").string();
    std::error_code ec;
    const fs::path tsc = toolsDir_ / "node_modules/typescript/bin/tsc";
    if (fs::exists(tsc, ec)) tools.tsc = tsc.string();
    return tools;
}

void MainWindow::showBottomPage(Gtk::Widget& page) {
    docks_->show(page);
}

void MainWindow::appendLog(const std::string& text, const Glib::ustring& tag) {
    auto buffer = compileLog_.get_buffer();
    if (tag.empty()) buffer->insert(buffer->end(), text);
    else buffer->insert_with_tag(buffer->end(), text, tag);
    compileLog_.scroll_to(logEnd_);
}

void MainWindow::startBuild(BuildAction action, Task task, std::function<void(bool)> then,
                            std::optional<BuildConfig> config) {
    if (!session_) {
        setStatus("Open a Kev-C++ project to build it");
        return;
    }
    if (build_.running()) {
        setStatus("A build is already running");
        return;
    }
    if (!saveAll()) return;
    if (watcher_) watcher_->syncNow(); // make sure the build file lists every source first

    buildConfig_ = config.value_or(currentConfig());
    auto commands = buildCommands(session_->project(), session_->root(), action, buildConfig_, buildTools());

    compileLog_.get_buffer()->set_text("");
    compilerPanel_.clear();
    const bool web = session_->project().kind == ProjectKind::WebTs;
    if (web) refreshWebProject(); // package.json may have changed since the project was opened
    if (commands.empty()) {
        if (web && action != BuildAction::Clean && web_.svelte()) {
            compilerPanel_.setEmptyText("Nothing to check yet: svelte-check isn't installed.");
            appendLog("Checking a Svelte project runs svelte-check from node_modules. Install the dependencies first "
                      "(Project → Install Dependencies).\n");
            setStatus("svelte-check isn't installed: Project → Install Dependencies");
        } else if (web && action != BuildAction::Clean) {
            compilerPanel_.setEmptyText("Nothing to type-check: the project has no tsconfig.json.");
            appendLog("Type checking runs `tsc --noEmit` and needs a tsconfig.json in the project folder.\n");
            setStatus("No tsconfig.json: skipped type checking");
        } else {
            compilerPanel_.setEmptyText("Nothing to clean.");
            appendLog("Nothing to clean: this configuration has not been built yet.\n");
            setStatus("Nothing to clean");
        }
        if (then) then(true);
        return;
    }

    static const char* verbs[] = {"Building", "Rebuilding", "Cleaning"};
    std::string verb = verbs[static_cast<int>(action)];
    std::string done;
    if (web && action == BuildAction::Rebuild && web_.usesVite()) {
        verb = "Building for production";
        done = "Production build";
    } else if (web) {
        verb = web_.svelte() ? "Checking" : "Type-checking";
    }
    buildAction_ = action;
    runCommands(std::move(commands),
                verb + " " + session_->project().name + " (" + (buildConfig_ == BuildConfig::Debug ? "Debug" : "Release") + ")",
                task, std::move(then), done);
}

void MainWindow::runCommands(std::vector<Command> commands, const std::string& heading, Task task,
                             std::function<void(bool)> then, std::string doneLabel) {
    if (build_.running()) {
        setStatus("Something is already running in the Compile Log; wait for it or stop it first");
        return;
    }
    const fs::path base = folder_.empty() ? (commands.empty() ? fs::path{} : commands.front().cwd) : folder_;
    compilerPanel_.setEmptyText(heading + "…");
    parser_ = std::make_unique<DiagnosticParser>(base);
    afterBuild_ = std::move(then);
    doneLabel_ = std::move(doneLabel);
    buildStart_ = std::chrono::steady_clock::now();
    appendLog(heading + "\n\n", "summary");
    showBottomPage(*compileLog_.get_parent());
    setStatus(heading + "…");
    buildTask_ = task;
    build_.start(std::move(commands));
    updateToolbar();
}

void MainWindow::onBuildLine(const std::string& line, bool isCommand) {
    if (isCommand) {
        appendLog(line + "\n", "command");
        return;
    }
    Glib::ustring tag;
    for (const Diagnostic& d : parser_->feed(line)) {
        compilerPanel_.add(d);
        if (d.severity == Diagnostic::Severity::Error) tag = "error";
        else if (d.severity == Diagnostic::Severity::Warning && tag.empty()) tag = "warning";
    }
    appendLog(line + "\n", tag);
}

void MainWindow::onBuildFinished(bool success) {
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - buildStart_).count();
    const int errors = parser_ ? parser_->errors() : 0;
    const int warnings = parser_ ? parser_->warnings() : 0;
    const BuildConfig config = buildConfig_;

    std::ostringstream summary;
    summary << std::fixed << std::setprecision(2);
    summary << "\nCompilation results:\n"
            << "- Errors: " << errors << "\n"
            << "- Warnings: " << warnings << "\n";
    if (session_) {
        if (session_->project().kind == ProjectKind::Cpp) linkCompileCommands(session_->root(), config);
        lsp_->compileDatabaseMaybeChanged();
        if (auto out = runnableOutput(session_->project(), session_->root(), config);
            out && success && buildAction_ != BuildAction::Clean)
            summary << "- Output: " << out->string() << "\n";
    }
    summary << "- Time: " << seconds << " s\n";
    appendLog(summary.str(), "summary");
    appendLog(success ? "Succeeded\n" : "Failed\n", success ? "success" : "error");

    std::ostringstream status;
    status << std::fixed << std::setprecision(1);
    if (!doneLabel_.empty()) {
        status << doneLabel_ << (success ? " finished" : " failed") << " (" << seconds << " s)";
        compilerPanel_.setEmptyText(success ? doneLabel_ + " finished." : doneLabel_ + " failed. See the Compile Log.");
        doneLabel_.clear();
    } else if (buildAction_ == BuildAction::Clean) {
        status << (success ? "Clean finished" : "Clean failed");
        compilerPanel_.setEmptyText(success ? "Cleaned." : "Clean failed. See the Compile Log.");
    } else if (session_ && session_->project().kind == ProjectKind::WebTs) {
        status << (success ? "Check passed" : "Check failed: ") << (success ? "" : std::to_string(errors) + " errors")
               << " (" << seconds << " s)";
        compilerPanel_.setEmptyText(success ? "No type errors." : "Type check failed. See the Compile Log for details.");
    } else if (success) {
        status << "Build succeeded: " << errors << " errors, " << warnings << " warnings in " << seconds << " s";
        compilerPanel_.setEmptyText("Build succeeded with no errors or warnings.");
    } else {
        status << "Build failed: " << errors << " errors, " << warnings << " warnings";
        compilerPanel_.setEmptyText("Build failed. See the Compile Log for details.");
    }
    setStatus(status.str());
    if (errors > 0 || (warnings > 0 && success)) showBottomPage(compilerPanel_);

    if (auto then = std::move(afterBuild_)) then(success);
    updateToolbar(); // after `then`, which may have started the program or the debugger
}

void MainWindow::runProgram(Task task) {
    if (!session_) {
        setStatus("Open a Kev-C++ project to run it");
        return;
    }
    if (build_.running()) {
        setStatus("Wait for the build to finish, or stop it first");
        return;
    }
    if (session_->project().kind == ProjectKind::WebTs && jsDebugger_) {
        if (!jsServerUrl_.empty()) openUrl(jsServerUrl_);
        else setStatus("The debugger is still starting Vite");
        return;
    }
    if (session_->project().kind == ProjectKind::WebTs) {
        // Run = serve the project and open it in the browser.
        if (devServer_ && devServer_->ready() && devServer_->root() == session_->root()) openInBrowser();
        else startDevServer(true);
        showBottomPage(serverPage_);
        return;
    }
    const auto output = runnableOutput(session_->project(), session_->root(), currentConfig());
    if (!output) {
        setStatus("Library projects have nothing to run");
        return;
    }
    std::error_code ec;
    if (!fs::exists(*output, ec)) {
        setStatus("Not built yet, compiling first…");
        compileAndRun(task);
        return;
    }
    if (runConsole_.childRunning()) {
        setStatus("The program is already running; stop it first (Ctrl+F2)");
        return;
    }
    showBottomPage(runConsole_);
    programTask_ = task;
    runConsole_.runProgram({output->string()}, session_->root(), [this](const std::string& outcome) {
        setStatus(outcome);
        programTask_ = Task::None;
        updateToolbar();
    });
    runConsole_.focusTerminal();
    setStatus("Running " + output->filename().string() + "…");
    updateToolbar();
}

void MainWindow::compileAndRun(Task task) {
    startBuild(BuildAction::Build, task, [this, task](bool success) {
        if (success) runProgram(task);
    });
}

void MainWindow::stopExecution() {
    if (build_.running()) build_.cancel();
    else if (runConsole_.childRunning()) runConsole_.stopChild();
    else setStatus("Nothing is running");
}

void MainWindow::stopTask(Task task) {
    if (build_.running() && buildTask_ == task) {
        build_.cancel();
        return;
    }
    switch (task) {
    case Task::Debug:
        if (debugger_) debugger_->terminate();
        if (jsDebugger_) jsDebugger_->stop();
        break;
    case Task::Run:
    case Task::CompileRun:
        if (runConsole_.childRunning()) runConsole_.stopChild();
        else if (devServer_ && devServer_->running()) devServer_->stop(); // Run serves web projects
        break;
    default:
        break;
    }
}

bool MainWindow::programRunning() const {
    return runConsole_.childRunning();
}

void MainWindow::updateToolbar() {
    if (taskButtons_.empty()) return; // still building the window
    const bool building = build_.running();
    const bool program = programRunning();
    const bool debugging = debugger_ || jsDebugger_;
    const bool serving = devServer_ && devServer_->running();
    const bool web = session_ && session_->project().kind == ProjectKind::WebTs;
    auto set = [&](Task t, bool running, bool enabled) {
        TaskButton* b = taskButtons_.at(t);
        b->setRunning(running);
        b->set_sensitive(running || enabled);
    };
    auto builtBy = [&](Task t) { return building && buildTask_ == t; };
    // Without a project there's nothing to build, run or debug: those buttons and menu items are
    // greyed out (and their keys do nothing) until one is open.
    const bool project = session_ != nullptr;
    // Only one build at a time, and the Run tab runs one program (or the program being debugged).
    set(Task::Compile, builtBy(Task::Compile), project && !building);
    set(Task::Rebuild, builtBy(Task::Rebuild), project && !building);
    set(Task::Clean, builtBy(Task::Clean), project && !building);
    set(Task::CompileRun, builtBy(Task::CompileRun) || (program && programTask_ == Task::CompileRun),
        project && !building && !program && !debugging);
    set(Task::Run, builtBy(Task::Run) || (program && programTask_ == Task::Run) || (web && serving),
        project && !building && !program && !debugging);
    set(Task::Debug, builtBy(Task::Debug) || debugging, project && !building && !program);

    const auto state = debugger_ ? debugger_->state() : DebugController::State::Finished;
    const bool jsRunning = jsDebugger_ && jsDebugger_->state() == JsDebugger::State::Running;
    const bool jsStopped = jsDebugger_ && jsDebugger_->stopped();
    const bool running = state == DebugController::State::Running || jsRunning;
    const bool paused = state == DebugController::State::Stopped || jsStopped;
    continueButton_->setRunning(running);
    continueButton_->set_sensitive(running || paused);
    for (Gtk::Button* b : stepButtons_) b->set_sensitive(paused);
    devServerButton_->setRunning(serving);

    // The same for the menus (Execute, Debug, Project, Tools) and their keys.
    auto enable = [this](const char* name, bool on) {
        if (auto action = std::dynamic_pointer_cast<Gio::SimpleAction>(lookup_action(name))) action->set_enabled(on);
    };
    for (const char* name : {"compile", "rebuild", "clean", "compile-run", "run", "debug", "project-options", "packages"})
        enable(name, project);
    enable("stop", building || program || (web && serving));
    enable("debug-continue", running || paused);
    enable("debug-pause", running);
    enable("debug-stop", debugging || builtBy(Task::Debug));
    for (const char* name : {"step-over", "step-into", "step-out"}) enable(name, paused);
    enable("project-add-file", !folder_.empty());
    enable("close-project", !folder_.empty());
    for (const char* name : {"web-install", "web-tests", "dev-server", "dev-server-open"}) enable(name, web);
}

// ---------------------------------------------------------------------------------------------
// TypeScript dev server

Gtk::Widget& MainWindow::buildServerPage() {
    auto* bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    bar->set_margin(6);
    serverStatus_.set_xalign(0.0f);
    serverStatus_.set_hexpand(true);
    serverStatus_.set_ellipsize(Pango::EllipsizeMode::END);
    serverOpen_.signal_clicked().connect([this] { openInBrowser(); });
    serverToggle_.setLabels("Start", "Stop");
    serverToggle_.signalStart().connect([this] { startDevServer(false); });
    serverToggle_.signalStop().connect([this] {
        if (devServer_) devServer_->stop();
    });
    bar->append(serverStatus_);
    bar->append(serverOpen_);
    bar->append(serverToggle_);

    serverLog_.set_editable(false);
    serverLog_.set_cursor_visible(false);
    serverLog_.set_monospace(true);
    serverLog_.add_css_class("kevc-log");
    auto* scroller = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroller->set_child(serverLog_);
    scroller->set_vexpand(true);

    serverPage_.append(*bar);
    serverPage_.append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL));
    serverPage_.append(*scroller);
    updateServerPage();
    return serverPage_;
}

void MainWindow::updateServerPage() {
    const bool running = devServer_ && devServer_->running();
    const bool ready = running && devServer_->ready();
    serverToggle_.setRunning(running);
    updateToolbar();
    serverOpen_.set_sensitive(ready);
    if (ready) serverStatus_.set_markup((devServer_->label() == "Vite" ? "Vite is serving <b>" : "Serving <b>") +
                                        Glib::Markup::escape_text(devServer_->root().filename().string()) +
                                        "</b> at <a href=\"" + Glib::Markup::escape_text(devServer_->url()) + "\">" +
                                        Glib::Markup::escape_text(devServer_->url()) + "</a> (this computer only)");
    else if (running) serverStatus_.set_text("Starting…");
    else if (web_.usesVite()) serverStatus_.set_text("Stopped. Runs this project's Vite dev server on 127.0.0.1.");
    else serverStatus_.set_text("Stopped. Serves the open folder on http://127.0.0.1:" +
                                std::to_string(DevServer::kDefaultPort) + "/ with live reload.");
}

void MainWindow::toggleDevServer() {
    if (devServer_ && devServer_->running()) {
        devServer_->stop();
        return;
    }
    startDevServer(false);
}

void MainWindow::startDevServer(bool openBrowserWhenReady) {
    if (folder_.empty()) {
        setStatus("Open a project or folder to serve it");
        return;
    }
    openBrowserWhenReady_ = openBrowserWhenReady;
    if (devServer_ && devServer_->running()) {
        if (devServer_->ready() && openBrowserWhenReady) openInBrowser();
        return;
    }

    // Vite projects run their own Vite; anything else gets the bundled server.
    const bool web = session_ && session_->project().kind == ProjectKind::WebTs;
    if (web) refreshWebProject();
    const std::string node = app_.toolchain().pathOf("node").string();
    std::vector<std::string> argv;
    DevServer::ReadyParser ready;
    std::string label;
    std::error_code ec;
    if (web && web_.usesVite()) {
        if (!fs::exists(folder_ / "node_modules", ec)) {
            setStatus("This " + std::string(web_.label()) + " project's packages aren't installed: Project → Install Dependencies");
            return;
        }
        // Vite's usual port may belong to another of the user's projects: take the first free one.
        const int port = findFreePort(app_.settings().get<int>("/web/vitePort", 5173));
        argv = viteDevCommand(web_, folder_, node, port);
        ready = [](const std::string& line) { return viteReadyUrl(line); };
        label = "Vite";
    } else {
        const fs::path script = toolsDir_ / "devserver/devserver.mjs";
        if (!fs::exists(script, ec) || !fs::exists(toolsDir_ / "node_modules/typescript", ec)) {
            showError(*this, "The dev server is not installed",
                      "Expected " + script.string() + " and the bundled TypeScript (run npm ci in " + toolsDir_.string() + ").");
            return;
        }
        argv = DevServer::bundledCommand(node, script, folder_);
        ready = DevServer::bundledReady();
        label = "the dev server";
    }

    devServer_ = std::make_unique<DevServer>();
    serverWasReady_ = false;
    devServer_->signalLog().connect([this](const std::string& line) {
        auto buffer = serverLog_.get_buffer();
        buffer->insert(buffer->end(), line + "\n");
        auto end = buffer->create_mark(buffer->end());
        serverLog_.scroll_to(end);
        buffer->delete_mark(end);
    });
    devServer_->signalReady().connect([this](const std::string& url) {
        serverWasReady_ = true;
        updateServerPage();
        setStatus(devServer_->label() == "Vite" ? "Vite is serving at " + url + " (hot reload on save)"
                                                : "Dev server running at " + url + " (live reload on save)");
        if (openBrowserWhenReady_) openInBrowser();
        openBrowserWhenReady_ = false;
    });
    devServer_->signalStopped().connect([this](int status) {
        updateServerPage();
        const std::string label = devServer_ ? devServer_->label() : "the dev server";
        if (status == DevServer::kExitPortInUse && label != "Vite") {
            setStatus("Port " + std::to_string(DevServer::kDefaultPort) +
                      " is already in use. Stop whatever is using it, then press Start in the Dev Server tab.");
            showBottomPage(serverPage_);
        } else if (!serverWasReady_ && status != 0) {
            setStatus((label == "Vite" ? std::string("Vite") : std::string("The dev server")) +
                      " stopped before it was ready (exit status " + std::to_string(status) + "). See the Dev Server tab.");
            showBottomPage(serverPage_);
        } else {
            setStatus(label == "Vite" ? "Vite stopped" : "Dev server stopped");
        }
    });
    serverLog_.get_buffer()->set_text("");
    try {
        // node's folder first, so `npm run dev` and its scripts find the same node as the IDE.
        devServer_->start(argv, folder_, ready, label, fs::path(node).parent_path().string(),
                          label == "Vite" ? traceEnvironment() : std::vector<std::pair<std::string, std::string>>{});
    } catch (const Glib::Error& e) {
        showError(*this, "Could not start " + label, e.what());
        devServer_.reset();
    }
    updateServerPage();
}

void MainWindow::openInBrowser() {
    if (!devServer_ || !devServer_->ready()) {
        startDevServer(true);
        return;
    }
    std::string url = devServer_->url();
    if (!pendingRoute_.empty()) {
        if (url.ends_with("/")) url.pop_back();
        url += pendingRoute_;
        pendingRoute_.clear();
    }
    openUrl(url);
}

void MainWindow::openUrl(const std::string& url) {
    // Settings can name a browser command (tests point it at a script that records the URL).
    const std::string browser = app_.settings().getString("/web/browser");
    try {
        if (!browser.empty()) Glib::spawn_async("", std::vector<std::string>{browser, url}, Glib::SpawnFlags::SEARCH_PATH);
        else Gio::AppInfo::launch_default_for_uri(url);
    } catch (const Glib::Error& e) {
        setStatus("Could not open a browser: " + std::string(e.what()) + ". Visit " + url);
    }
}

// ---------------------------------------------------------------------------------------------
// Web projects

void MainWindow::refreshWebProject() {
    web_ = session_ && session_->project().kind == ProjectKind::WebTs ? WebProject::detect(session_->root()) : WebProject{};
    scriptsMenu_->remove_all();
    for (const auto& [name, command] : web_.scripts) {
        auto item = Gio::MenuItem::create(name + "  (" + (command.size() > 40 ? command.substr(0, 39) + "…" : command) + ")", "");
        item->set_action_and_target("win.run-script", Glib::Variant<Glib::ustring>::create(name));
        scriptsMenu_->append_item(item);
    }
    if (web_.scripts.empty()) scriptsMenu_->append("No package.json scripts", "win.no-scripts"); // no such action: greyed out
    testsPanel_.setRunners(session_ && session_->project().kind == ProjectKind::WebTs ? testRunners(session_->root())
                                                                                       : std::vector<TestRunner>{});
}

void MainWindow::installDependencies() {
    if (!session_ || session_->project().kind != ProjectKind::WebTs || !web_.hasPackageJson) {
        setStatus("Install Dependencies is for web projects with a package.json");
        return;
    }
    compileLog_.get_buffer()->set_text("");
    compilerPanel_.clear();
    buildAction_ = BuildAction::Build;
    runCommands({{web_.installCommand(), session_->root()}},
                "Installing " + session_->project().name + "'s packages (" + web_.packageManagerName() + " install)",
                Task::None, [this](bool ok) {
                    if (ok) refreshWebProject();
                },
                "Install");
}

void MainWindow::chooseProgram() {
    if (!session_) return;
    const auto programs = cmakeExecutables(session_->root());
    if (programs.empty()) {
        showInfo(*this, "No programs to choose from",
                 "None of this project's CMakeLists.txt files has an add_executable(). Run and Debug start the "
                 "program named " + session_->project().targetName + ".");
        return;
    }
    std::vector<Glib::ustring> buttons{"Cancel"};
    for (const auto& p : programs) buttons.push_back(p == session_->project().targetName ? p + " (now)" : p);
    askChoice(*this, "Which program should Run and Debug start?",
              "The executables this project's CMakeLists.txt files define. The choice is saved in its .kevc.",
              buttons, 0, 0, [this, programs](int choice) {
                  if (choice < 1 || !session_) return;
                  session_->project().targetName = programs[static_cast<size_t>(choice - 1)];
                  saveProjectUi(); // writes the .kevc
                  setStatus("Run and Debug start " + session_->project().targetName);
              });
}

void MainWindow::showNpmPackages() {
    if (!session_ || session_->project().kind != ProjectKind::WebTs || !web_.hasPackageJson) {
        setStatus("Packages are for web projects with a package.json");
        return;
    }
    auto* dialog = new NpmPackagesDialog(*this, session_->root(), web_.packageManagerName()); // deletes itself
    dialog->signalSaved().connect([this](bool install) {
        refreshWebProject();
        updateTitle();
        applyProfile();
        if (install) installDependencies();
        else setStatus("Saved package.json: install with Project → Install Dependencies");
    });
    dialog->signalLogin().connect([this](const std::string& registry) { npmLogin(registry); });
    dialog->present();
}

void MainWindow::npmLogin(const std::string& registry) {
    if (folder_.empty()) return;
    if (runConsole_.childRunning()) {
        setStatus("Something is already running in the Run tab; stop it first (Ctrl+F2)");
        return;
    }
    std::vector<std::string> argv{"npm", "login"};
    if (!registry.empty()) argv.push_back("--registry=" + registry);
    showBottomPage(runConsole_);
    runConsole_.runProgram(argv, folder_, [this](const std::string& outcome) {
        setStatus("npm login: " + outcome);
        updateToolbar();
    });
    runConsole_.focusTerminal();
    setStatus("Log in in the Run tab (your token is saved in ~/.npmrc)");
    updateToolbar();
}

void MainWindow::runScript(const std::string& name) {
    if (!session_ || session_->project().kind != ProjectKind::WebTs) return;
    if (runConsole_.childRunning()) {
        setStatus("Something is already running in the Run tab; stop it first (Ctrl+F2)");
        return;
    }
    const auto argv = web_.runScriptCommand(name);
    showBottomPage(runConsole_);
    runConsole_.runProgram(argv, session_->root(), [this, name](const std::string& outcome) {
        setStatus("Script " + name + ": " + outcome);
        updateToolbar();
    });
    runConsole_.focusTerminal();
    setStatus("Running " + displayCommand(argv) + "…");
    updateToolbar();
}

void MainWindow::refreshKit() {
    kitRefresh_.disconnect();
    const bool kit = web_.flavor == WebFlavor::SvelteKit;
    const int page = leftDock_.page_num(routesPanel_);
    if (!kit) {
        if (page >= 0) leftDock_.remove_page(routesPanel_);
        envPanel_.showMessage("Open a SvelteKit project to see its environment variables.");
        return;
    }
    if (page < 0) {
        // Show it when a SvelteKit project opens, so it's noticed.
        leftDock_.append_page(routesPanel_, *makeIconLabel("kevc-svelte-file", "Routes"));
        leftDock_.set_current_page(leftDock_.page_num(routesPanel_));
        fitLeftTabs();
    }
    routesPanel_.setApp(kit::scan(folder_), folder_);
    envPanel_.setReport(kit::envReport(folder_), folder_);
}

void MainWindow::scheduleKitRefresh() {
    if (web_.flavor != WebFlavor::SvelteKit) return;
    kitRefresh_.disconnect();
    kitRefresh_ = Glib::signal_timeout().connect(
        [this] {
            refreshKit();
            return false;
        },
        300);
}

void MainWindow::newRoute() {
    if (web_.flavor != WebFlavor::SvelteKit) return;
    promptText(*this, "New Route", "Route under src/routes, e.g. blog/[slug] or (app)/settings:", "",
               [this](const Glib::ustring& text) -> Glib::ustring {
                   const auto folder = kit::routeFolder(text.raw());
                   if (!folder) return "Use a path inside src/routes (no ..).";
                   const fs::path dir = folder_ / "src" / "routes" / *folder;
                   std::error_code ec;
                   if (fs::exists(dir / "+page.svelte", ec)) return "That route already has a +page.svelte.";
                   fs::create_directories(dir, ec);
                   if (ec) return "Could not create " + dir.string() + ": " + ec.message();
                   for (const auto& [name, content] : kit::newRouteFiles(*folder))
                       if (!fs::exists(dir / name, ec)) std::ofstream(dir / name) << content;
                   openFile(dir / "+page.svelte");
                   refreshKit();
                   setStatus("Created the route /" + *folder);
                   return {};
               });
}

void MainWindow::openRoute(const std::string& url) {
    auto open = [this](const std::string& path) {
        if (jsDebugger_ && !jsServerUrl_.empty()) { // the debugger's Vite is already serving
            std::string base = jsServerUrl_;
            if (base.ends_with("/")) base.pop_back();
            openUrl(base + path);
            return;
        }
        pendingRoute_ = path;
        if (devServer_ && devServer_->ready()) openInBrowser();
        else startDevServer(true);
    };
    if (url.find('[') == std::string::npos) {
        open(url);
        return;
    }
    promptText(*this, "Open Route", "Fill in the route's parameters:", url, [open](const Glib::ustring& text) -> Glib::ustring {
        const std::string path = kit::fillParams(text.raw());
        if (path.empty()) return "Replace every [parameter] with a value.";
        open(path);
        return {};
    });
}

void MainWindow::runTests(TestRunner runner, const fs::path& file, const std::string& title, int line,
                          const std::vector<fs::path>& files) {
    if (!session_ || session_->project().kind != ProjectKind::WebTs) return;
    if (build_.running()) {
        setStatus("Wait for the running build to finish first");
        return;
    }
    if (!saveAll()) return;
    const fs::path root = session_->root();
    const fs::path report = fs::path(Glib::get_user_cache_dir()) / "kev-cpp" / ("test-report-" + std::to_string(getpid()) + ".json");
    std::error_code ec;
    fs::create_directories(report.parent_path(), ec);
    fs::remove(report, ec);
    auto argv = testCommand(runner, root, app_.toolchain().pathOf("node").string(), report, file, title, line);
    for (const auto& f : files) argv.push_back(f.lexically_relative(root).generic_string());
    lastRunner_ = runner;
    testsPanel_.setBusy(true);
    compileLog_.get_buffer()->set_text("");
    compilerPanel_.clear();
    buildAction_ = BuildAction::Build;
    const std::string what = std::string(runnerName(runner)) + (title.empty() ? (files.empty() ? "" : " (failed tests)") : ": " + title);
    runCommands({{argv, root}}, "Running " + what, Task::None,
                [this, runner, report, root](bool) {
                    testsPanel_.setBusy(false);
                    std::ifstream in(report);
                    std::stringstream text;
                    text << in.rdbuf();
                    TestRun run;
                    if (text.str().empty()) run.error = "The test runner wrote no report: see the Compile Log.";
                    else run = runner == TestRunner::Vitest ? parseVitestJson(text.str()) : parsePlaywrightJson(text.str(), root);
                    testsPanel_.setRun(run, root);
                    lastFailed_.clear();
                    for (const auto& c : run.cases)
                        if (c.status == TestCase::Status::Failed) lastFailed_.push_back(c);
                    showBottomPage(testsPanel_);
                    setStatus(run.error.empty() ? "Tests: " + std::to_string(run.passed) + " passed, " + std::to_string(run.failed) +
                                                      " failed" + (run.skipped ? ", " + std::to_string(run.skipped) + " skipped" : "")
                                                : run.error);
                },
                "Tests");
}

std::vector<std::pair<std::string, std::string>> MainWindow::traceEnvironment() {
    if (web_.flavor != WebFlavor::SvelteKit || folder_.empty()) return {};
    if (!collector_ || !collector_->running()) {
        const fs::path script = toolsDir_ / "otel/collector.mjs";
        std::error_code ec;
        if (!fs::exists(script, ec)) return {};
        collectorPort_ = findFreePort(4318);
        if (!collectorPort_) return {};
        collector_ = std::make_unique<ManagedProcess>();
        collector_->signalLine().connect([this](const std::string& line) {
            if (line.starts_with("KEVC_SPANS ")) {
                traces_.add(parseOtlpJson(line.substr(11)));
                tracesPanel_.setTraces(traces_);
                tracesPanel_.setStatus(std::to_string(traces_.traces().size()) + " traces · collecting at 127.0.0.1:" +
                                       std::to_string(collectorPort_));
            } else if (line.starts_with("KEVC_OTLP_NOTE ")) {
                setStatus("Traces: " + line.substr(15));
            }
        });
        try {
            collector_->start({{app_.toolchain().pathOf("node").string(), script.string(), "--port", std::to_string(collectorPort_)},
                               folder_, {}, {}});
        } catch (const Glib::Error&) {
            collector_.reset();
            return {};
        }
        tracesPanel_.setStatus("Collecting at http://127.0.0.1:" + std::to_string(collectorPort_));
    }
    const std::string endpoint = "http://127.0.0.1:" + std::to_string(collectorPort_);
    return {{"OTEL_EXPORTER_OTLP_ENDPOINT", endpoint},
            {"OTEL_EXPORTER_OTLP_PROTOCOL", "http/json"},
            {"OTEL_SERVICE_NAME", folder_.filename().string()}};
}

void MainWindow::refreshDependencies() {
    if (!session_ || session_->project().kind != ProjectKind::WebTs || !web_.hasPackageJson) {
        depsPanel_.showMessage("Open a web project with a package.json.");
        return;
    }
    if (web_.packageManager != PackageManager::Npm) {
        depsPanel_.showMessage("This project uses " + web_.packageManagerName() +
                               "; the Dependencies panel reads npm's reports, which need an npm lockfile.");
        return;
    }
    depsPanel_.setBusy(true);
    struct Results {
        std::string outdated, audit;
        int remaining = 2;
    };
    auto results = std::make_shared<Results>();
    auto finish = [this, results] {
        if (--results->remaining) return;
        depsPanel_.setBusy(false);
        const auto outdated = parseNpmOutdated(results->outdated);
        const auto audit = parseNpmAudit(results->audit);
        depsPanel_.setReports(outdated, audit);
        setStatus("Dependencies: " + std::to_string(outdated.size()) + " outdated, " +
                  std::to_string(audit.vulnerabilities.size()) + " with known vulnerabilities");
    };
    // Both exit with status 1 when they find something; only the JSON matters.
    captureOutput({"npm", "outdated", "--json"}, session_->root(),
                  [results, finish](std::string out, int) {
                      results->outdated = std::move(out);
                      finish();
                  },
                  lifetime_);
    captureOutput({"npm", "audit", "--json"}, session_->root(),
                  [results, finish](std::string out, int) {
                      results->audit = std::move(out);
                      finish();
                  },
                  lifetime_);
}

Glib::ustring MainWindow::createSvelteKitApp(const std::string& name, const fs::path& parent) {
    if (name.empty() || name.find('/') != std::string::npos) return "Enter a folder name for the app.";
    std::error_code ec;
    const fs::path root = fs::absolute(parent / name, ec);
    if (fs::exists(root, ec) && !fs::is_empty(root, ec)) return root.string() + " already exists and is not empty.";
    if (!findInPath("npx", Glib::getenv("PATH"))) return "npx (part of npm) isn't on the PATH, so sv can't run.";
    if (build_.running()) return "Wait for the running build to finish first.";
    if (!fs::is_directory(parent, ec)) return parent.string() + " isn't a folder.";
    compileLog_.get_buffer()->set_text("");
    compilerPanel_.clear();
    buildAction_ = BuildAction::Build;
    runCommands({{svCreateCommand(name), parent}}, "Creating the SvelteKit app " + name + " with sv (this downloads packages)",
                Task::None, [this, root](bool ok) {
                    if (ok) openFolder(root);
                },
                "Creating the SvelteKit app");
    return {};
}

// ---------------------------------------------------------------------------------------------
// Debugging

void MainWindow::startDebugging() {
    if (debugger_) {
        if (debugger_->stopped()) debugger_->resume(); // F5 while paused continues, the same as F8
        return;
    }
    if (jsDebugger_) {
        if (jsDebugger_->stopped()) jsDebugger_->resume();
        return;
    }
    if (session_ && session_->project().kind == ProjectKind::WebTs) {
        startJsDebugging();
        return;
    }
    if (!session_) {
        setStatus("Open a C or C++ project to debug it");
        return;
    }
    if (!runnableOutput(session_->project(), session_->root(), BuildConfig::Debug)) {
        setStatus("Library projects can't be started on their own; debug a program that uses them");
        return;
    }
    if (runConsole_.childRunning()) {
        setStatus("Stop the running program first (Ctrl+F2)");
        return;
    }
    // Always a Debug build (-O0 -g), whatever the toolbar says.
    startBuild(BuildAction::Build, Task::Debug, [this](bool ok) {
        if (ok) launchDebugger();
    }, BuildConfig::Debug);
}

void MainWindow::startJsDebugging() {
    refreshWebProject();
    if (!web_.usesVite()) {
        setStatus("Debugging web code needs a Vite project (SvelteKit, Svelte or Vite); for plain TypeScript use the "
                  "browser's developer tools");
        return;
    }
    std::error_code ec;
    const fs::path vite = nodeBin(folder_, "vite");
    if (!fs::exists(folder_ / "node_modules", ec) || vite.empty()) {
        setStatus("This project's packages aren't installed: Project → Install Dependencies");
        return;
    }
    const fs::path server = toolsDir_ / "js-debug/src/dapDebugServer.js";
    if (!fs::exists(server, ec)) {
        showError(*this, "The JavaScript debugger isn't installed",
                  "Kev-C++'s build downloads Microsoft's js-debug into " + (toolsDir_ / "js-debug").string() +
                      ". Rebuild Kev-C++ (or run ./install.sh again) with a network connection.");
        return;
    }
    if (devServer_ && devServer_->running()) devServer_->stop(); // the debugger runs its own Vite
    for (EditorTab* tab : editors()) syncBreakpoints(tab);

    const int port = findFreePort(app_.settings().get<int>("/web/vitePort", 5173));
    JsDebugger::Options options;
    options.node = app_.toolchain().pathOf("node").string();
    options.server = server;
    options.root = folder_;
    options.viteArgs = {vite.string(), "dev", "--port", std::to_string(port), "--strictPort", "--host", "127.0.0.1"};
    options.chrome = dap::findChrome(app_.settings().getString("/web/chrome"), Glib::getenv("PATH"));
    options.profileDir = fs::path(Glib::get_user_cache_dir()) / "kev-cpp" / "debug-browser-profile";
    options.env = traceEnvironment();
    jsDebugger_ = std::make_shared<JsDebugger>(options);

    jsDebugger_->signalStopped().connect([this](const dap::Frame& top, const std::string& reason) {
        ++stopGeneration_;
        lsp_->setHoverEnabled(false);
        showFrame(StackFrame{0, top.name, top.file, top.line});
        setStatus("Paused (" + reason + ") in " + (top.name.empty() ? std::string("code") : top.name) +
                  (top.file.empty() ? std::string() : ", " + top.file.filename().string() + ":" + std::to_string(top.line)));
        noteWatchTriggered(reason);
        showBottomPage(debugPanel_);
        refreshJsViews(-1); // a watch's trap is the top frame: show the code that changed the value
    });
    jsDebugger_->signalLiveHit().connect(sigc::mem_fun(*this, &MainWindow::onLiveHit));
    jsDebugger_->signalWatchState().connect([this](const std::string& expression, bool armed, const std::string& note) {
        for (auto& w : valueWatches_)
            if (w.expression == expression) {
                w.armed = armed;
                w.state = armed ? "watching" : note;
            }
        updateBreakpointsPanel();
    });
    jsDebugger_->signalStateChanged().connect([this](JsDebugger::State state) {
        updateToolbar();
        if (state != JsDebugger::State::Running) return;
        ++stopGeneration_;
        clearExecutionLine();
        lsp_->setHoverEnabled(true);
        debugPanel_.showMessage("Running…  (Debug → Pause to break in)");
        setStatus("Running " + folder_.filename().string() + " under the debugger…");
    });
    jsDebugger_->signalOutput().connect([this](const std::string& text) {
        auto buffer = serverLog_.get_buffer();
        buffer->insert(buffer->end(), stripAnsi(text));
        auto end = buffer->create_mark(buffer->end());
        serverLog_.scroll_to(end);
        buffer->delete_mark(end);
    });
    jsDebugger_->signalServing().connect([this](const std::string& url) {
        jsServerUrl_ = url;
        const bool browser = jsDebugger_ && jsDebugger_->browserAttached() && dap::findChrome(app_.settings().getString("/web/chrome"), Glib::getenv("PATH"));
        serverStatus_.set_markup("Debugging <b>" + Glib::Markup::escape_text(folder_.filename().string()) + "</b>: Vite at <a href=\"" +
                                 Glib::Markup::escape_text(url) + "\">" + Glib::Markup::escape_text(url) + "</a>");
        if (browser) {
            setStatus("Debugging server and browser code: Vite at " + url + " (opened in a separate browser window)");
        } else {
            // No Chromium-based browser: server code (loads, actions, endpoints) still stops when the page is requested.
            setStatus("Debugging server code: Vite at " + url + ". Install Chrome or Chromium to debug browser code too");
            openUrl(url);
        }
    });
    jsDebugger_->signalFinished().connect([this](const std::string& outcome) {
        ++stopGeneration_;
        clearExecutionLine();
        lsp_->setHoverEnabled(true);
        jsFrames_.clear();
        frames_.clear();
        jsServerUrl_.clear();
        resetValueWatchStates();
        debugPanel_.showMessage(outcome + ". Press F5 to debug again.");
        watches_.showPlaceholders();
        setStatus(outcome);
        updateServerPage();
        // We are inside one of its signals: drop it once that returns.
        Glib::signal_idle().connect_once([this] {
            jsDebugger_.reset();
            updateToolbar();
        });
    });

    serverLog_.get_buffer()->set_text("");
    debugPanel_.clearConsole();
    debugPanel_.showMessage("Starting js-debug and Vite…");
    serverStatus_.set_text("Starting Vite under the debugger…");
    showBottomPage(serverPage_);
    setStatus(std::string("Starting the debugger") + (options.chrome ? "" : " (server code only: no Chrome or Chromium found)") + "…");
    clearLiveValues();
    jsDebugger_->start(breakpoints_, valueWatchExpressions());
    updateToolbar();
}

void MainWindow::refreshJsViews(int frameIndex) {
    if (!jsDebugger_ || !jsDebugger_->stopped()) return;
    const unsigned generation = stopGeneration_;
    std::weak_ptr<JsDebugger> weak = jsDebugger_;
    auto fetch = [weak](int reference, std::function<void(std::vector<dap::Variable>)> done) {
        if (auto d = weak.lock()) d->variables(reference, std::move(done));
    };
    jsDebugger_->frames([this, generation, weak, frameIndex, fetch](std::vector<dap::Frame> frames) {
        auto dbg = weak.lock();
        if (generation != stopGeneration_ || !dbg) return;
        jsFrames_ = frames;
        frames_.clear();
        for (size_t i = 0; i < frames.size(); ++i)
            frames_.push_back(StackFrame{static_cast<int>(i), frames[i].name, frames[i].file, frames[i].line});
        // -1: the first frame in a file (the user's code, not node's internals or a watch's trap).
        int index = std::max(frameIndex, 0);
        if (frameIndex < 0)
            for (size_t i = 0; i < frames.size(); ++i)
                if (!frames[i].file.empty()) {
                    index = static_cast<int>(i);
                    break;
                }
        debugPanel_.setFrames(frames_, index);
        if (frames.empty()) return;
        const int frameId = frames[std::min<size_t>(static_cast<size_t>(index), frames.size() - 1)].id;

        // Locals: the innermost scope's variables directly; the other scopes (Closure, Module, Global)
        // as rows that expand.
        dbg->scopes(frameId, [this, generation, weak, fetch](std::vector<dap::Scope> scopes) {
            auto dbg = weak.lock();
            if (generation != stopGeneration_ || !dbg) return;
            if (scopes.empty()) {
                debugPanel_.setLocals({});
                return;
            }
            dbg->variables(scopes.front().reference, [this, generation, weak, fetch, scopes](std::vector<dap::Variable> vars) {
                if (generation != stopGeneration_ || !weak.lock()) return;
                std::vector<Gtk::Widget*> rows;
                for (auto& v : vars) rows.push_back(Gtk::make_managed<DapVarNode>(std::move(v), 0, fetch));
                for (size_t i = 1; i < scopes.size(); ++i)
                    rows.push_back(Gtk::make_managed<DapVarNode>(dap::Variable{scopes[i].name, "…", "scope", scopes[i].reference}, 0, fetch));
                debugPanel_.setLocals(rows);
            });
        });

        // Watches, evaluated in the same frame.
        const auto exprs = watches_.expressions();
        if (exprs.empty()) {
            watches_.setRows({});
            return;
        }
        auto results = std::make_shared<std::vector<Gtk::Widget*>>(exprs.size(), nullptr);
        auto remaining = std::make_shared<size_t>(exprs.size());
        for (size_t i = 0; i < exprs.size(); ++i) {
            dbg->evaluate(exprs[i], frameId, "watch",
                          [this, generation, weak, fetch, results, remaining, i, name = exprs[i]](std::optional<dap::Variable> v, std::string error) {
                              if (generation != stopGeneration_ || !weak.lock()) return;
                              (*results)[i] = v ? static_cast<Gtk::Widget*>(Gtk::make_managed<DapVarNode>(*v, 0, fetch))
                                                : DapVarNode::error(name, error);
                              if (--*remaining == 0) watches_.setRows(*results);
                          });
        }
    });
}

void MainWindow::launchDebugger() {
    if (!session_ || debugger_) return;
    const auto exe = runnableOutput(session_->project(), session_->root(), BuildConfig::Debug);
    if (!exe) return;
    for (EditorTab* tab : editors()) syncBreakpoints(tab);

    const std::string tty = runConsole_.prepareForInferior();
    debugger_ = std::make_shared<DebugController>(app_.toolchain().pathOf("gdb").string(), *exe, session_->root(), tty);
    debugger_->signalStopped().connect(sigc::mem_fun(*this, &MainWindow::onDebugStopped));
    debugger_->signalFinished().connect(sigc::mem_fun(*this, &MainWindow::onDebugFinished));
    debugger_->signalConsole().connect([this](const std::string& text) { debugPanel_.appendConsole(text); });
    debugger_->signalLiveHit().connect(sigc::mem_fun(*this, &MainWindow::onLiveHit));
    debugger_->signalWatchState().connect([this](const std::string& expression, bool armed, const std::string& note) {
        for (auto& w : valueWatches_)
            if (w.expression == expression) {
                w.armed = armed;
                w.state = armed ? "watching" : note;
            }
        updateBreakpointsPanel();
    });
    debugger_->signalStateChanged().connect([this](DebugController::State state) {
        updateToolbar(); // Continue/Pause and the step buttons
        if (state != DebugController::State::Running) return;
        ++stopGeneration_;
        clearExecutionLine();
        lsp_->setHoverEnabled(true);
        debugPanel_.showMessage("Running…  (Debug → Pause to break in)");
        setStatus("Running " + session_->project().name + " under the debugger…");
    });

    bool anyBreakpoint = !valueWatches_.empty();
    for (const auto& [file, lines] : breakpoints_)
        for (const auto& [line, bp] : lines)
            if (bp.kind != BreakKind::Live) anyBreakpoint = true;
    debugPanel_.clearConsole();
    debugPanel_.showMessage("Starting gdb…");
    showBottomPage(runConsole_);
    setStatus("Debugging " + exe->filename().string() +
              (anyBreakpoint ? "" : " (no breakpoints set: it will run to the end)"));
    clearLiveValues();
    debugger_->start(breakpoints_, valueWatchExpressions());
    updateToolbar();
}

void MainWindow::debugStep(void (DebugController::*step)()) {
    if (!debugger_) {
        setStatus("Not debugging. Press F5 to start");
        return;
    }
    if (!debugger_->stopped()) {
        setStatus("The program is running; pause it first");
        return;
    }
    (debugger_.get()->*step)();
}

void MainWindow::onDebugStopped(const StopInfo& info) {
    ++stopGeneration_;
    lsp_->setHoverEnabled(false);
    showFrame(info.frame);
    setStatus(info.description);
    noteWatchTriggered(info.description);
    showBottomPage(debugPanel_);
    refreshDebugViews();
}

void MainWindow::showFrame(const StackFrame& frame) {
    clearExecutionLine();
    if (frame.file.empty()) return;
    if (EditorTab* tab = openFile(frame.file)) {
        tab->setExecutionLine(frame.line);
        execTab_ = tab;
    }
}

void MainWindow::clearExecutionLine() {
    if (execTab_) execTab_->setExecutionLine(0);
    execTab_ = nullptr;
}

void MainWindow::onDebugFinished(const std::string& outcome) {
    ++stopGeneration_;
    clearExecutionLine();
    lsp_->setHoverEnabled(true);
    frames_.clear();
    resetValueWatchStates();
    debugPanel_.showMessage(outcome + ". Press F5 to debug again.");
    watches_.showPlaceholders();
    // gdb reports the exit before the terminal has drawn the program's last output (VTE reads a pty
    // it didn't spawn in batches, more slowly while hidden), so show the Run tab and add the note later.
    showBottomPage(runConsole_);
    Glib::signal_timeout().connect_once(
        sigc::track_obj([this, outcome] { runConsole_.feedText("\n\x1b[2m[" + outcome + "]\x1b[0m\n"); }, *this), 1500);
    setStatus(outcome);
    // We are inside one of the controller's signals: drop it once that returns.
    Glib::signal_idle().connect_once([this] {
        debugger_.reset();
        updateToolbar();
    });
}

void MainWindow::refreshDebugViews() {
    if (!debugger_ || !debugger_->stopped()) return;
    const unsigned generation = stopGeneration_;
    std::weak_ptr<DebugController> weak = debugger_;

    debugger_->frames([this, generation, weak](std::vector<StackFrame> frames) {
        if (generation != stopGeneration_ || !weak.lock()) return;
        frames_ = std::move(frames);
        debugPanel_.setFrames(frames_, weak.lock()->selectedFrame());
    });

    // Creates a variable object per expression; once all have answered (and the program hasn't
    // moved on meanwhile) `show` gets one row per expression, in order.
    // `scope` keeps locals and watches with the same name apart in expandedRows_.
    auto evaluateAll = [this, generation, weak](std::string scope, std::vector<std::string> exprs,
                                                std::function<void(std::vector<Gtk::Widget*>)> show) {
        struct Pending {
            std::vector<std::optional<VarInfo>> values;
            std::vector<std::string> errors;
            size_t remaining;
        };
        auto pending = std::make_shared<Pending>(Pending{std::vector<std::optional<VarInfo>>(exprs.size()),
                                                         std::vector<std::string>(exprs.size()), exprs.size()});
        if (exprs.empty()) {
            show({});
            return;
        }
        for (size_t i = 0; i < exprs.size(); ++i) {
            weak.lock()->createVar(exprs[i], [this, generation, weak, pending, exprs, i, show, scope](std::optional<VarInfo> v, std::string error) {
                pending->values[i] = std::move(v);
                pending->errors[i] = std::move(error);
                if (--pending->remaining) return;
                auto dbg = weak.lock();
                if (generation != stopGeneration_ || !dbg) {
                    for (auto& value : pending->values)
                        if (value && dbg) dbg->deleteVar(value->varobj);
                    return;
                }
                std::vector<Gtk::Widget*> rows;
                for (size_t k = 0; k < exprs.size(); ++k) {
                    if (pending->values[k])
                        rows.push_back(Gtk::make_managed<VarNode>(weak, *pending->values[k], 0, true,
                                                                  scope + exprs[k], expandedRows_));
                    else rows.push_back(VarNode::error(exprs[k], pending->errors[k]));
                }
                show(rows);
            });
        }
    };

    debugger_->locals([this, generation, weak, evaluateAll](std::vector<std::string> names) {
        if (generation != stopGeneration_ || !weak.lock()) return;
        evaluateAll("L:", names, [this](std::vector<Gtk::Widget*> rows) { debugPanel_.setLocals(rows); });
    });
    evaluateAll("W:", watches_.expressions(), [this](std::vector<Gtk::Widget*> rows) { watches_.setRows(rows); });
}

bool MainWindow::debugHover(EditorTab* tab, const GtkTextIter& at, std::function<void(GtkWidget*)> show) {
    if (jsDebugger_ && jsDebugger_->stopped()) {
        const std::string line = tab->lineText(gtk_text_iter_get_line(&at) + 1);
        const char* start = line.c_str();
        const int byteColumn = static_cast<int>(g_utf8_offset_to_pointer(start, gtk_text_iter_get_line_offset(&at)) - start);
        const std::string expression = mi::expressionAt(line, byteColumn); // a.b.c works for JS too
        if (expression.empty()) return false;
        std::weak_ptr<JsDebugger> weak = jsDebugger_;
        const int frameId = jsFrames_.empty() ? 0 : jsFrames_.front().id;
        jsDebugger_->evaluate(expression, frameId, "hover", [weak, show](std::optional<dap::Variable> v, std::string) {
            if (!v || !weak.lock()) {
                show(nullptr);
                return;
            }
            auto fetch = [weak](int ref, std::function<void(std::vector<dap::Variable>)> done) {
                if (auto d = weak.lock()) d->variables(ref, std::move(done));
            };
            auto* node = Gtk::manage(new DapVarNode(*v, 0, fetch));
            node->set_margin(6);
            show(GTK_WIDGET(node->gobj()));
        });
        return true;
    }
    if (!debugger_ || !debugger_->stopped()) return false;
    const std::string line = tab->lineText(gtk_text_iter_get_line(&at) + 1);
    const char* start = line.c_str();
    const int byteColumn = static_cast<int>(g_utf8_offset_to_pointer(start, gtk_text_iter_get_line_offset(&at)) - start);
    const std::string expression = mi::expressionAt(line, byteColumn);
    if (expression.empty()) return false;
    std::weak_ptr<DebugController> weak = debugger_;
    debugger_->createVar(expression, [weak, show](std::optional<VarInfo> value, std::string) {
        if (!value || !weak.lock()) {
            show(nullptr);
            return;
        }
        auto* node = Gtk::manage(new VarNode(weak, *value, 0, true));
        node->set_margin(6);
        show(GTK_WIDGET(node->gobj()));
    });
    return true;
}

void MainWindow::toggleBreakpointAtCursor() {
    EditorTab* tab = currentEditor();
    if (!tab) return;
    GtkTextIter cursor;
    GtkTextBuffer* text = GTK_TEXT_BUFFER(tab->buffer());
    gtk_text_buffer_get_iter_at_mark(text, &cursor, gtk_text_buffer_get_insert(text));
    tab->toggleBreakpoint(gtk_text_iter_get_line(&cursor) + 1);
}

void MainWindow::setBreakpointAt(const fs::path& file, int line, const std::optional<Breakpoint>& breakpoint) {
    for (EditorTab* tab : editors())
        if (tab->path() == file) {
            tab->setBreakpoint(line, breakpoint); // comes back through signalBreakpointChanged
            return;
        }
    onBreakpointChanged(file, line, breakpoint);
}

void MainWindow::onBreakpointChanged(const fs::path& file, int line, const std::optional<Breakpoint>& breakpoint) {
    auto& lines = breakpoints_[file];
    if (breakpoint) lines[line] = *breakpoint;
    else lines.erase(line);
    if (lines.empty()) breakpoints_.erase(file);
    if (!breakpoint || breakpoint->kind != BreakKind::Live) {
        liveHits_.erase({file, line});
        liveDirty_.erase({file, line});
    }
    if (debugger_) debugger_->setBreakpoint(file, line, breakpoint);
    if (jsDebugger_) {
        auto it = breakpoints_.find(file);
        jsDebugger_->setBreakpoints(file, it == breakpoints_.end() ? FileBreakpoints{} : it->second);
    }
    updateBreakpointsPanel();
    saveProjectState();
    const std::string where = file.filename().string() + ":" + std::to_string(line);
    if (!breakpoint) setStatus("Breakpoint removed from " + where);
    else if (breakpoint->kind == BreakKind::Conditional) setStatus("IF breakpoint at " + where + ": stops when " + breakpoint->condition);
    else if (breakpoint->kind == BreakKind::Live)
        setStatus("Live breakpoint at " + where + ": shows " +
                  (breakpoint->show.empty() ? std::string("its local variables") : joinExpressions(breakpoint->show)) +
                  " each time it runs, without stopping");
    else setStatus("Breakpoint set at " + where);
}

bool MainWindow::cursorLine(fs::path& file, int& line) {
    EditorTab* tab = currentEditor();
    if (!tab) {
        setStatus("Open a file and put the cursor on the line first");
        return false;
    }
    GtkTextIter cursor;
    GtkTextBuffer* text = GTK_TEXT_BUFFER(tab->buffer());
    gtk_text_buffer_get_iter_at_mark(text, &cursor, gtk_text_buffer_get_insert(text));
    file = tab->path();
    line = gtk_text_iter_get_line(&cursor) + 1;
    return true;
}

std::string MainWindow::expressionAtCursor() {
    EditorTab* tab = currentEditor();
    if (!tab) return {};
    GtkTextBuffer* text = GTK_TEXT_BUFFER(tab->buffer());
    GtkTextIter s, e;
    if (gtk_text_buffer_get_selection_bounds(text, &s, &e)) {
        char* selected = gtk_text_buffer_get_text(text, &s, &e, FALSE);
        std::string out = selected;
        g_free(selected);
        if (out.find('\n') == std::string::npos) return out;
    }
    GtkTextIter cursor;
    gtk_text_buffer_get_iter_at_mark(text, &cursor, gtk_text_buffer_get_insert(text));
    GtkTextIter lineStart = cursor, lineEnd = cursor;
    gtk_text_iter_set_line_offset(&lineStart, 0);
    if (!gtk_text_iter_ends_line(&lineEnd)) gtk_text_iter_forward_to_line_end(&lineEnd);
    char* lineText = gtk_text_buffer_get_text(text, &lineStart, &lineEnd, FALSE);
    std::string out = mi::expressionAt(lineText, gtk_text_iter_get_line_offset(&cursor));
    g_free(lineText);
    return out;
}

void MainWindow::promptConditional(const fs::path& file, int line) {
    std::string initial;
    if (auto f = breakpoints_.find(file); f != breakpoints_.end())
        if (auto bp = f->second.find(line); bp != f->second.end()) initial = bp->second.condition;
    promptText(*this, "IF Breakpoint",
               "Stop at " + file.filename().string() + ":" + std::to_string(line) +
                   " only when this is true\n(e.g. i == 41, or name == \"Ann\" in JavaScript):",
               initial, [this, file, line](const Glib::ustring& text) -> Glib::ustring {
                   if (text.empty()) return "Enter a condition, e.g. i == 41";
                   setBreakpointAt(file, line, Breakpoint{BreakKind::Conditional, text, {}});
                   return {};
               }, -2); // all of it: typing replaces it
}

void MainWindow::promptLive(const fs::path& file, int line) {
    std::string initial;
    if (auto f = breakpoints_.find(file); f != breakpoints_.end())
        if (auto bp = f->second.find(line); bp != f->second.end()) initial = joinExpressions(bp->second.show);
    promptText(*this, "Live Breakpoint",
               "Each time " + file.filename().string() + ":" + std::to_string(line) +
                   " runs, show these values next to it without stopping\n(separate them with commas; "
                   "leave empty for all the local variables):",
               initial, [this, file, line](const Glib::ustring& text) -> Glib::ustring {
                   setBreakpointAt(file, line, Breakpoint{BreakKind::Live, {}, splitExpressions(text.raw())});
                   return {};
               }, -2);
}

void MainWindow::promptValueWatch() {
    promptText(*this, "Break When Value Changes",
               "Stop as soon as this changes\n(a variable such as total, or in JavaScript a property such as player.score):",
               Glib::ustring(expressionAtCursor()), [this](const Glib::ustring& text) -> Glib::ustring {
                   std::string expression = text.raw();
                   expression.erase(0, expression.find_first_not_of(" \t"));
                   expression.erase(expression.find_last_not_of(" \t") + 1);
                   if (expression.empty()) return "Enter what to watch, e.g. total";
                   for (const auto& w : valueWatches_)
                       if (w.expression == expression) return {};
                   valueWatches_.push_back({expression, "placed when debugging starts", false});
                   if (debugger_) {
                       valueWatches_.back().state = "placing…";
                       debugger_->addValueWatch(expression);
                   }
                   if (jsDebugger_) jsDebugger_->addValueWatch(expression);
                   updateBreakpointsPanel();
                   saveProjectState();
                   showBottomPage(debugPanel_);
                   debugPanel_.set_current_page(2); // Breakpoints
                   setStatus("The debugger will stop when " + expression + " changes");
                   return {};
               }, -2);
}

void MainWindow::removeValueWatch(const std::string& expression) {
    std::erase_if(valueWatches_, [&](const ValueWatchItem& w) { return w.expression == expression; });
    if (debugger_) debugger_->removeValueWatch(expression);
    if (jsDebugger_) jsDebugger_->removeValueWatch(expression);
    updateBreakpointsPanel();
    saveProjectState();
}

std::vector<std::string> MainWindow::valueWatchExpressions() const {
    std::vector<std::string> out;
    for (const auto& w : valueWatches_) out.push_back(w.expression);
    return out;
}

void MainWindow::resetValueWatchStates() {
    for (auto& w : valueWatches_) {
        w.armed = false;
        w.state = "placed when debugging starts";
    }
    updateBreakpointsPanel();
}

void MainWindow::noteWatchTriggered(const std::string& description) {
    // "total changed from 1.5 to 4.5 at main.cpp:12": show it on the watch's row.
    for (auto& w : valueWatches_)
        if (description.rfind(w.expression + " changed", 0) == 0) {
            w.state = description.substr(w.expression.size() + 1);
            updateBreakpointsPanel();
            return;
        }
}

void MainWindow::onLiveHit(const fs::path& file, int line, const std::vector<std::string>& names,
                           const std::vector<std::string>& values) {
    auto& hit = liveHits_[{file, line}];
    ++hit.hits;
    if (!names.empty()) hit.text = liveSummary(names, values);
    else if (values.empty()) hit.text = "(no local variables)";
    else hit.text = joinExpressions(values);
    liveDirty_.insert({file, line});
    // A loop can run its line thousands of times a second: redraw a few times a second at most.
    if (!liveFlush_.connected()) liveFlush_ = Glib::signal_timeout().connect(sigc::mem_fun(*this, &MainWindow::flushLiveValues), 150);
}

bool MainWindow::flushLiveValues() {
    for (const auto& key : liveDirty_) {
        const auto it = liveHits_.find(key);
        if (it == liveHits_.end()) continue;
        const std::string text = it->second.text + "   \u00d7" + std::to_string(it->second.hits);
        debugPanel_.setLiveValues(key.first, key.second, text);
        for (EditorTab* tab : editors())
            if (tab->path() == key.first) tab->showLiveValues(key.second, text);
    }
    liveDirty_.clear();
    return false; // once; the next hit starts it again
}

void MainWindow::clearLiveValues() {
    liveFlush_.disconnect();
    liveHits_.clear();
    liveDirty_.clear();
    debugPanel_.clearLiveValues();
    for (EditorTab* tab : editors()) tab->clearLiveValues();
}

void MainWindow::syncBreakpoints(EditorTab* tab) {
    // Marks follow edits, so the editor knows where breakpoints are now.
    const FileBreakpoints now = tab->breakpoints();
    auto it = breakpoints_.find(tab->path());
    if (it == breakpoints_.end() ? now.empty() : it->second == now) return;
    if (now.empty()) breakpoints_.erase(tab->path());
    else breakpoints_[tab->path()] = now;
    updateBreakpointsPanel();
}

void MainWindow::saveProjectState() {
    if (folder_.empty()) return;
    for (EditorTab* tab : editors()) syncBreakpoints(tab); // marks move with edits
    ProjectState state{breakpoints_, watches_.expressions(), valueWatchExpressions(), {}};
    // A project without a .kevc (a web folder) keeps its look here instead.
    if (session_ && session_->projectFile().empty()) state.ui = session_->project().ui;
    std::error_code ec;
    projectState_.save(fs::weakly_canonical(folder_, ec), state);
}

void MainWindow::restoreProjectState() {
    std::error_code ec;
    const ProjectState state = folder_.empty() ? ProjectState{} : projectState_.load(fs::weakly_canonical(folder_, ec));
    breakpoints_ = state.breakpoints;
    valueWatches_.clear();
    for (const auto& expression : state.valueWatches) valueWatches_.push_back({expression, "placed when debugging starts", false});
    updateBreakpointsPanel();
    watches_.setExpressions(state.watches);
    if (session_ && session_->projectFile().empty()) session_->project().ui = state.ui;
}

bool MainWindow::toolbarShown(const std::string& group) const {
    if (session_) {
        const auto& toolbars = session_->project().ui.toolbars;
        if (auto it = toolbars.find(group); it != toolbars.end()) return it->second;
    }
    return toolbarGroupShown(app_.settings(), group, profile_);
}

bool MainWindow::panelShownHere(const std::string& panel) const {
    if (session_) {
        const auto& panels = session_->project().ui.panels;
        if (auto it = panels.find(panel); it != panels.end()) return it->second;
    }
    return panelShown(app_.settings(), panel, profile_);
}

void MainWindow::setToolbarShownHere(const std::string& group, bool shown) {
    if (!session_) {
        setToolbarGroupShown(app_.settings(), group, profile_, shown);
        return;
    }
    session_->project().ui.toolbars[group] = shown;
    saveProjectUi();
}

void MainWindow::setPanelShownHere(const std::string& panel, bool shown) {
    if (!session_) {
        setPanelShown(app_.settings(), panel, profile_, shown);
        return;
    }
    session_->project().ui.panels[panel] = shown;
    saveProjectUi();
}

void MainWindow::saveProjectUi() {
    if (!session_) return;
    if (session_->projectFile().empty()) {
        saveProjectState();
        return;
    }
    try {
        session_->project().save(session_->projectFile());
    } catch (const std::exception& e) {
        setStatus(std::string("Couldn't save the project's settings: ") + e.what());
    }
}

void MainWindow::updateBreakpointsPanel() {
    std::vector<BreakpointItem> items;
    for (const auto& [file, lines] : breakpoints_)
        for (const auto& [line, bp] : lines) items.push_back({file, line, bp});
    debugPanel_.setBreakpoints(items, valueWatches_);
}

void MainWindow::addWatch() {
    Glib::ustring initial;
    if (EditorTab* tab = currentEditor()) {
        GtkTextIter s, e;
        if (gtk_text_buffer_get_selection_bounds(GTK_TEXT_BUFFER(tab->buffer()), &s, &e)) {
            char* text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(tab->buffer()), &s, &e, FALSE);
            initial = text;
            g_free(text);
        }
    }
    promptText(*this, "Add Watch", "Expression to watch (evaluated whenever the program pauses):", initial,
               [this](const Glib::ustring& text) -> Glib::ustring {
                   if (text.empty()) return "Enter an expression, e.g. p->next->value";
                   watches_.addExpression(text);
                   leftDock_.set_current_page(leftDock_.page_num(watches_));
                   return {};
               }, -2);
}

// ---------------------------------------------------------------------------------------------
// IntelliSense

void MainWindow::setupIntelliSense(EditorTab* tab) {
    lsp_->attach(tab);
    tab->signalSaved().connect([this, tab] {
        lsp_->saved(tab);
        scheduleKitRefresh(); // load keys, actions, .env values
        syncBreakpoints(tab);
        saveProjectState();
    });

    // Breakpoints set before the file was (re)opened.
    if (auto it = breakpoints_.find(tab->path()); it != breakpoints_.end()) tab->setBreakpoints(it->second);
    for (const auto& [key, hit] : liveHits_)
        if (key.first == tab->path()) tab->showLiveValues(key.second, hit.text + "   \u00d7" + std::to_string(hit.hits));
    tab->signalBreakpointChanged().connect(
        [this, tab](int line, std::optional<Breakpoint> bp) { onBreakpointChanged(tab->path(), line, bp); });

    // Values under the mouse while paused; added first so they appear above anything else.
    GtkSourceHoverProvider* debugHoverProvider = newDebugHoverProvider(
        [this, tab](const GtkTextIter& at, std::function<void(GtkWidget*)> show) { return debugHover(tab, at, std::move(show)); });
    gtk_source_hover_add_provider(gtk_source_view_get_hover(tab->view()), debugHoverProvider);
    g_object_unref(debugHoverProvider);

    GtkSourceCompletion* completion = gtk_source_view_get_completion(tab->view());
    GtkSourceCompletionProvider* provider = newLspCompletionProvider(lsp_.get(), tab);
    gtk_source_completion_add_provider(completion, provider);
    g_object_unref(provider);
    // Icons on: the ICON column shows the kind of each proposal (class, method, variable, ...).
    g_object_set(completion, "show-icons", TRUE, "select-on-show", TRUE, "page-size", 10, nullptr);

    GtkSourceHover* hover = gtk_source_view_get_hover(tab->view());
    GtkSourceHoverProvider* hoverProvider = newLspHoverProvider(lsp_.get(), tab);
    gtk_source_hover_add_provider(hover, hoverProvider);
    g_object_unref(hoverProvider);

    // Ctrl+click jumps to the definition.
    auto* view = Glib::wrap(GTK_WIDGET(tab->view()));
    auto click = Gtk::GestureClick::create();
    click->set_button(GDK_BUTTON_PRIMARY);
    click->signal_pressed().connect([this, tab, click](int, double x, double y) {
        if ((click->get_current_event_state() & Gdk::ModifierType::CONTROL_MASK) != Gdk::ModifierType::CONTROL_MASK) return;
        int bx = 0, by = 0;
        gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(tab->view()), GTK_TEXT_WINDOW_WIDGET,
                                              static_cast<int>(x), static_cast<int>(y), &bx, &by);
        GtkTextIter iter;
        if (!gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(tab->view()), &iter, bx, by)) return;
        click->set_state(Gtk::EventSequenceState::CLAIMED);
        lookUp(tab, iter, Lookup::Definition);
    });
    view->add_controller(click);
    // Right-click puts the cursor where it was clicked (unless that's inside the selection), so the
    // menu's Go to… items look up the word under the pointer.
    auto rightClick = Gtk::GestureClick::create();
    rightClick->set_button(GDK_BUTTON_SECONDARY);
    rightClick->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    rightClick->signal_pressed().connect([tab](int, double x, double y) {
        GtkTextView* text = GTK_TEXT_VIEW(tab->view());
        int bx = 0, by = 0;
        gtk_text_view_window_to_buffer_coords(text, GTK_TEXT_WINDOW_WIDGET, static_cast<int>(x), static_cast<int>(y), &bx, &by);
        GtkTextIter iter, start, end;
        if (!gtk_text_view_get_iter_at_location(text, &iter, bx, by)) return;
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(text);
        if (gtk_text_buffer_get_selection_bounds(buffer, &start, &end) && gtk_text_iter_in_range(&iter, &start, &end)) return;
        gtk_text_buffer_place_cursor(buffer, &iter);
    });
    view->add_controller(rightClick);
    // Right-click: the Go to… items above GTK's Cut/Copy/Paste.
    gtk_text_view_set_extra_menu(GTK_TEXT_VIEW(tab->view()),
                                 G_MENU_MODEL((lsp::serverFor(tab->path()) == lsp::Server::Clangd ? cppEditorMenu_ : editorMenu_)->gobj()));
    tab->setGutterMenu(gutterMenu_);
}

void MainWindow::lookUpAtCursor(Lookup what) {
    EditorTab* tab = currentEditor();
    if (!tab) return;
    GtkTextIter cursor;
    GtkTextBuffer* text = GTK_TEXT_BUFFER(tab->buffer());
    gtk_text_buffer_get_iter_at_mark(text, &cursor, gtk_text_buffer_get_insert(text));
    lookUp(tab, cursor, what);
}

void MainWindow::lookUp(EditorTab* tab, const GtkTextIter& at, Lookup what) {
    if (!lsp_->handles(tab)) {
        setStatus("Go to Definition and the like need IntelliSense for this file");
        return;
    }
    struct Kind {
        const char* method;
        const char* name; // "No … found"
    };
    static const std::map<Lookup, Kind> kinds{
        {Lookup::Definition, {"textDocument/definition", "definition"}},
        {Lookup::Declaration, {"textDocument/declaration", "declaration"}},
        {Lookup::TypeDefinition, {"textDocument/typeDefinition", "type definition"}},
        {Lookup::Implementation, {"textDocument/implementation", "implementation"}},
        {Lookup::References, {"textDocument/references", "references"}},
    };
    const Kind kind = kinds.at(what);
    setStatus(std::string("Finding ") + kind.name + "…");
    // Iterators don't survive the wait: keep the place as a line and UTF-16 column.
    const auto pos = LspManager::positionOf(at);
    const int line = pos.value("line", 0), utf16 = pos.value("character", 0);
    std::weak_ptr<int> alive = lifetime_;
    lsp_->locations(kind.method, tab, at, [this, tab, what, kind, line, utf16, alive](std::vector<LspLocation> found) {
        if (alive.expired()) return;
        const auto open = editors(); // the tab may have been closed while the server thought
        if (std::find(open.begin(), open.end(), tab) == open.end()) return;
        const GtkTextIter where = LspManager::iterAt(GTK_TEXT_BUFFER(tab->buffer()), line, utf16);
        auto inTypings = [](const std::vector<LspLocation>& locs) {
            return !locs.empty() && std::all_of(locs.begin(), locs.end(), [](const LspLocation& l) {
                return l.file.string().ends_with(".d.ts");
            });
        };
        // A library's .d.ts only declares it: TypeScript can say where its JavaScript really is.
        if (what == Lookup::Definition && inTypings(found)) {
            lsp_->sourceDefinition(tab, where, [this, tab, found, line, utf16, alive](std::vector<LspLocation> source) {
                if (alive.expired()) return;
                const auto open = editors();
                if (std::find(open.begin(), open.end(), tab) == open.end()) return;
                const bool better = !source.empty() && !std::all_of(source.begin(), source.end(), [](const LspLocation& l) {
                    return l.file.string().ends_with(".d.ts");
                });
                showLocations(tab, LspManager::iterAt(GTK_TEXT_BUFFER(tab->buffer()), line, utf16), "definition",
                              better ? source : found);
            });
            return;
        }
        showLocations(tab, where, kind.name, std::move(found));
    });
}

void MainWindow::openLocation(const LspLocation& location) {
    if (EditorTab* target = openFile(location.file)) {
        target->placeCursor(LspManager::iterAt(GTK_TEXT_BUFFER(target->buffer()), location.line, location.utf16));
        setStatus(location.file.filename().string() + ":" + std::to_string(location.line + 1));
    }
}

void MainWindow::showLocations(EditorTab* tab, const GtkTextIter& at, const Glib::ustring& what,
                               std::vector<LspLocation> locations) {
    if (locations.empty()) {
        setStatus("No " + what + " found here");
        return;
    }
    if (locations.size() == 1) {
        openLocation(locations.front());
        return;
    }
    // Several: a list at the cursor, each with its file, line and the line's text.
    auto* list = Gtk::make_managed<Gtk::ListBox>();
    list->set_selection_mode(Gtk::SelectionMode::BROWSE);
    list->set_activate_on_single_click(true);
    auto shown = std::make_shared<std::vector<LspLocation>>();
    for (const auto& loc : locations) {
        if (shown->size() == 100) break;
        std::string text;
        if (auto* open = dynamic_cast<EditorTab*>(findDocument(loc.file))) {
            text = open->lineText(loc.line + 1);
        } else {
            std::ifstream in(loc.file);
            for (int i = 0; i <= loc.line && std::getline(in, text); ++i) {
            }
        }
        const size_t first = text.find_first_not_of(" \t");
        text = first == std::string::npos ? std::string() : text.substr(first, 120);
        std::error_code ec;
        const fs::path rel = folder_.empty() ? loc.file : loc.file.lexically_relative(folder_);
        const std::string where = rel.empty() || *rel.begin() == ".." ? loc.file.string() : rel.generic_string();
        auto* label = Gtk::make_managed<Gtk::Label>();
        label->set_markup("<b>" + Glib::Markup::escape_text(loc.file.filename().string()) + "</b>:" +
                          std::to_string(loc.line + 1) + "  <span alpha=\"60%\">" + Glib::Markup::escape_text(where) +
                          "</span>\n<tt>" + Glib::Markup::escape_text(text) + "</tt>");
        label->set_xalign(0.0f);
        label->set_ellipsize(Pango::EllipsizeMode::END);
        label->set_margin(4);
        list->append(*label);
        shown->push_back(loc);
    }
    list->signal_row_activated().connect([this, shown](Gtk::ListBoxRow* row) {
        const LspLocation chosen = (*shown)[static_cast<size_t>(row->get_index())];
        locationsPopover_.popdown();
        Glib::signal_idle().connect_once(sigc::track_obj([this, chosen] { openLocation(chosen); }, *this));
    });
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    auto* title = Gtk::make_managed<Gtk::Label>();
    const Glib::ustring plural = what.raw().ends_with("s") ? what : what + "s"; // several, so plural
    title->set_markup("<b>" + std::to_string(locations.size()) + " " + Glib::Markup::escape_text(plural) + "</b>  " +
                      "<span alpha=\"60%\">click one to go there</span>");
    title->set_xalign(0.0f);
    box->append(*title);
    auto* scroller = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroller->set_child(*list);
    scroller->set_propagate_natural_height(true);
    scroller->set_max_content_height(360);
    scroller->set_min_content_width(520);
    box->append(*scroller);
    locationsPopover_.set_child(*box);
    // Under the word, in editorArea_'s coordinates.
    GdkRectangle rect;
    gtk_text_view_get_iter_location(GTK_TEXT_VIEW(tab->view()), &at, &rect);
    int wx = 0, wy = 0;
    gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(tab->view()), GTK_TEXT_WINDOW_WIDGET, rect.x, rect.y, &wx, &wy);
    graphene_point_t point = GRAPHENE_POINT_INIT(float(wx), float(wy)), inArea;
    if (!gtk_widget_compute_point(GTK_WIDGET(tab->view()), GTK_WIDGET(editorArea_.gobj()), &point, &inArea)) return;
    locationsPopover_.set_pointing_to(Gdk::Rectangle(int(inArea.x), int(inArea.y), std::max(rect.width, 1), rect.height));
    locationsPopover_.popup();
    setStatus(std::to_string(locations.size()) + " " + plural + ": choose one");
}

void MainWindow::scheduleSymbolsRefresh() {
    symbolsTimer_.disconnect();
    symbolsTimer_ = Glib::signal_timeout().connect(
        [this] {
            refreshSymbols();
            return false;
        },
        400);
}

void MainWindow::refreshSymbols() {
    EditorTab* tab = currentEditor();
    if (!tab) {
        symbols_.showMessage("Symbols of the current file appear here.");
        return;
    }
    if (!lsp_->handles(tab)) {
        symbols_.showMessage("No symbol information for this file.");
        return;
    }
    lsp_->documentSymbols(tab, [this, tab](const lsp::json& symbols) {
        if (tab == currentEditor()) symbols_.setSymbols(symbols);
    });
}

void MainWindow::showProjectOptions(bool packagesPage) {
    if (!session_) {
        showError(*this, "No project open", "Project options belong to a Kev-C++ project. Create or open one first.");
        return;
    }
    if (!session_->project().buildFileManaged) {
        chooseProgram();
        return;
    }
    if (session_->project().kind == ProjectKind::WebTs) {
        if (packagesPage) showNpmPackages();
        else setStatus("Web projects have no compiler or linker options: Project → Packages manages their npm packages");
        return;
    }
    using Page = ProjectOptionsDialog::Page;
    auto* dialog = new ProjectOptionsDialog(
        *this, session_->project(), app_.toolchain(), packagesPage ? Page::Packages : Page::General,
        [this](const Project& edited) {
            if (!session_) return;
            const auto oldPackages = session_->project().packages;
            Project updated = edited;
            // Sources may have changed while the dialog was open; keep the watcher's list.
            updated.sources = session_->project().sources;
            updated.excludedSources = session_->project().excludedSources;
            session_->project() = std::move(updated);
            try {
                const auto result = session_->saveAndRegenerate();
                // Include dirs, defines and the standard feed clangd's fallback flags.
                lsp_->setWorkspace(session_->root(), &session_->project());
                Glib::ustring msg = "Project options saved";
                if (result.buildFileWritten) msg += "; " + session_->buildFile().filename().string() + " updated";
                if (session_->project().packages != oldPackages && session_->project().kind == ProjectKind::Cpp)
                    msg += ". New packages are downloaded on the next build";
                for (const auto& w : result.warnings) msg += ". " + w;
                setStatus(msg);
            } catch (const std::exception& e) {
                showError(*this, "Could not save project options", e.what());
            }
        });
    dialog->present();
}

void MainWindow::checkForUpdates() {
    setStatus("Checking for updates…");
    const std::string repository = app_.settings().getString("/updates/repository", std::string(kReleaseRepository));
    captureOutput(
        {"curl", "-fsSL", "--max-time", "20", "-H", "Accept: application/vnd.github+json", latestReleaseUrl(repository)},
        Glib::get_home_dir(),
        [this](std::string reply, int status) {
            const Version running = buildVersion();
            if (status != 0) {
                // curl -f fails with 22 on an HTTP error: GitHub answers 404 while there's no release yet.
                showError(*this, "Couldn't check for updates",
                          status == 22  ? "GitHub has no Kev-C++ release to download yet."
                          : status < 0 ? "Checking needs curl, which isn't installed (sudo apt install curl)."
                                        : "Kev-C++ couldn't reach GitHub (curl exit status " + std::to_string(status) +
                                              "). Check the network connection and try again.");
                setStatus("Couldn't check for updates");
                return;
            }
            const auto release = parseLatestRelease(reply);
            if (!release) {
                showError(*this, "Couldn't check for updates", "The latest release on GitHub has no Linux package.");
                setStatus("Couldn't check for updates");
                return;
            }
            const std::string current = running.known() ? running.text : "a build without a version (made outside git)";
            switch (compareVersions(running, release->version)) {
            case Freshness::UpToDate:
                setStatus("Kev-C++ is up to date (" + running.text + ")");
                showInfo(*this, "Kev-C++ is up to date", "You have " + running.text + ", the latest release.");
                return;
            case Freshness::AheadOfRelease:
                setStatus("This build is newer than the latest release");
                showInfo(*this, "This build is newer than the latest release",
                         "You have " + running.text + ", made from a newer commit than the latest release, " +
                             release->version.text + ".");
                return;
            case Freshness::UpdateAvailable:
                break;
            }
            std::error_code ec;
            const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
            const fs::path prefix = installPrefix(exe).value_or(fs::path(Glib::get_home_dir()) / ".local");
            std::string notes = release->notes.size() > 700 ? release->notes.substr(0, 700) + "…" : release->notes;
            std::ostringstream size;
            size << std::fixed << std::setprecision(0) << static_cast<double>(release->assetSize) / (1024 * 1024);
            setStatus("Kev-C++ " + release->version.text + " is available");
            askChoice(*this, "Kev-C++ " + release->version.text + " is available",
                      "You have " + current + "." + (notes.empty() ? "" : "\n\n" + notes) + "\n\nUpdate Now downloads it (" +
                          size.str() + " MB) and installs it into " + prefix.string() +
                          ", replacing this version: the program, its tools and the menu entry.",
                      {"Later", "Update Now"}, 0, 1, [this, r = *release](int choice) {
                          if (choice == 1) installUpdate(r);
                      });
        },
        lifetime_);
}

void MainWindow::installUpdate(const Release& release) {
    if (runConsole_.childRunning()) {
        setStatus("Stop what's running in the Run tab first (Ctrl+F2), then update");
        return;
    }
    std::error_code ec;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    const fs::path prefix = installPrefix(exe).value_or(fs::path(Glib::get_home_dir()) / ".local");
    const fs::path cache = fs::path(Glib::get_user_cache_dir()) / "kev-cpp" / "updates";
    // In the Run tab, so the installer can ask for a sudo password if packages are missing.
    showBottomPage(runConsole_);
    setStatus("Updating to Kev-C++ " + release.version.text + "…");
    runConsole_.runProgram(updateCommand(release, cache, prefix), Glib::get_home_dir(),
                           [this, release, prefix](const std::string& outcome) {
                               updateToolbar();
                               if (!outcome.ends_with(" code 0")) {
                                   setStatus("The update didn't finish (" + outcome + "); see the Run tab");
                                   return;
                               }
                               setStatus("Kev-C++ " + release.version.text + " is installed");
                               askChoice(*this, "Kev-C++ " + release.version.text + " is installed",
                                         "Restart now to start using it? You'll be asked about unsaved changes first.",
                                         {"Later", "Restart Now"}, 0, 1, [this, prefix](int choice) {
                                             if (choice != 1) return;
                                             closeAllEditors([this, prefix] {
                                                 std::vector<std::string> argv{(prefix / "bin" / "kev-cpp").string()};
                                                 if (!folder_.empty()) argv.push_back(folder_.string());
                                                 try {
                                                     Glib::spawn_async(Glib::get_home_dir(), argv);
                                                 } catch (const Glib::Error& e) {
                                                     showError(*this, "Couldn't start the new Kev-C++", e.what());
                                                     return;
                                                 }
                                                 // After this dialog's callback returns: closing the
                                                 // window inside it leaves the dialog a dead parent.
                                                 Glib::signal_idle().connect_once([this] {
                                                     closeConfirmed_ = true;
                                                     close();
                                                 });
                                             });
                                         });
                           });
    runConsole_.focusTerminal();
    updateToolbar();
}

void MainWindow::showAbout() {
    auto* about = new Gtk::AboutDialog();
    about->set_transient_for(*this);
    about->set_modal(true);
    about->set_program_name("Kev-C++");
    about->set_version(buildVersion().text); // the commit it was built from: date, time and hash
    about->set_logo_icon_name("org.kevcpp.KevCpp");
    about->set_comments("A lightweight IDE for C, C++, JavaScript and TypeScript,\n"
                        "built on gcc, g++, CMake, Make and GTK 4.");
    about->signal_hide().connect([about] { Glib::signal_idle().connect_once([about] { delete about; }); });
    about->present();
}

void MainWindow::showToolchainInfo() {
    Glib::ustring detail;
    for (const ToolInfo& t : app_.toolchain().tools()) {
        detail += t.name + (t.required ? " (required)" : " (optional)") + ": ";
        detail += t.found() ? Glib::ustring(t.path->string()) + "\n    " + t.version : Glib::ustring("not found");
        detail += "\n";
    }
    auto dialog = Gtk::AlertDialog::create("Toolchain");
    dialog->set_detail(detail);
    dialog->show(*this);
}

} // namespace kevc
