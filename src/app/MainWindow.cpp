#include "app/MainWindow.h"

#include "app/Application.h"
#include "editor/EditorTab.h"
#include "core/Mi.h"
#include "debug/DebugHover.h"
#include "lsp/LspBridges.h"
#include "project/SourceWatcher.h"
#include "ui/Dialogs.h"
#include "ui/ProjectOptionsDialog.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

namespace kevc {

namespace {

Gtk::Widget& placeholder(const Glib::ustring& text) {
    auto* label = Gtk::make_managed<Gtk::Label>(text);
    label->add_css_class("dim-label");
    label->set_wrap(true);
    label->set_margin(12);
    auto* scroller = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroller->set_child(*label);
    return *scroller;
}

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

fs::path defaultProjectLocation() {
    const fs::path home = Glib::get_home_dir();
    std::error_code ec;
    for (const char* candidate : {"source/repos", "Projects", "projects", "src"})
        if (fs::is_directory(home / candidate, ec)) return home / candidate;
    return home;
}

std::string kindLabel(ProjectKind kind) {
    switch (kind) {
    case ProjectKind::Cpp: return "C++ · CMake";
    case ProjectKind::C: return "C · Make";
    case ProjectKind::WebTs: return "TypeScript";
    }
    return {};
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
    : app_(app), toolsDir_(findToolsDir()), lsp_(std::make_unique<LspManager>(app.toolchain(), toolsDir_)) {
    set_default_size(1280, 800);
    updateTitle();

    buildActions();

    auto* menubar = Gtk::make_managed<Gtk::PopoverMenuBar>(buildMenuModel());
    root_.append(*menubar);
    root_.append(buildToolbar());

    vpaned_.set_start_child(buildEditorArea());
    vpaned_.set_end_child(buildBottomDock());
    vpaned_.set_resize_start_child(true);
    vpaned_.set_resize_end_child(false);
    vpaned_.set_position(560);

    rightDock_.set_size_request(260, -1);
    rightPaned_.set_start_child(vpaned_);
    rightPaned_.set_end_child(rightDock_);
    rightPaned_.set_resize_start_child(true);
    rightPaned_.set_resize_end_child(false);
    rightPaned_.set_shrink_end_child(false);

    hpaned_.set_start_child(buildLeftDock());
    hpaned_.set_end_child(rightPaned_);
    hpaned_.set_resize_start_child(false);
    hpaned_.set_position(240);
    hpaned_.set_vexpand(true);

    root_.append(hpaned_);
    root_.append(buildStatusBar());
    set_child(root_);

    projectTree_.signalFileActivated().connect([this](const fs::path& file) { openFile(file); });
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
        if (!debugger_ || !debugger_->stopped()) return;
        debugger_->selectFrame(level);
        for (const auto& f : frames_)
            if (f.level == level) showFrame(f);
        refreshDebugViews();
    });
    debugPanel_.signalBreakpointActivated().connect([this](const fs::path& file, int line) { openFile(file, line); });
    debugPanel_.signalBreakpointRemoved().connect([this](const fs::path& file, int line) {
        for (EditorTab* tab : editors())
            if (tab->path() == file) {
                tab->toggleBreakpoint(line); // updates everything through signalBreakpointToggled
                return;
            }
        breakpoints_[file].erase(line);
        if (debugger_) debugger_->setBreakpoint(file, line, false);
        updateBreakpointsPanel();
    });
    debugPanel_.signalConsoleCommand().connect([this](const std::string& command) {
        if (debugger_) debugger_->consoleCommand(command);
        else debugPanel_.appendConsole("Not debugging. Press F5 to start.\n");
    });
    watches_.signalChanged().connect([this] {
        if (debugger_ && debugger_->stopped()) refreshDebugViews();
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
            if (closeConfirmed_) return false;
            auto eds = editors();
            if (std::none_of(eds.begin(), eds.end(), [](EditorTab* t) { return t->modified(); })) return false;
            closeAllEditors([this] {
                closeConfirmed_ = true;
                close();
            });
            return true;
        },
        false);

    if (auto missing = app_.toolchain().missingOptional(); !missing.empty()) {
        Glib::ustring msg = "Optional tools not found:";
        for (const ToolInfo* t : missing) msg += " " + t->name + " (" + t->purpose + ")";
        setStatus(msg);
    } else {
        setStatus("Ready");
    }
}

MainWindow::~MainWindow() = default;

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
    add_action("new-file", [this] { newFile(); });
    add_action("save", [this] { saveCurrent(); });
    add_action("save-all", [this] { saveAll(); });
    add_action("close-tab", [this] {
        if (auto* tab = currentEditor()) closeEditor(tab);
    });
    // Edit (forwarded to the focused editor's own GtkTextView actions)
    add_action("undo", [this] { forwardToEditor("text.undo"); });
    add_action("redo", [this] { forwardToEditor("text.redo"); });
    add_action("cut", [this] { forwardToEditor("clipboard.cut"); });
    add_action("copy", [this] { forwardToEditor("clipboard.copy"); });
    add_action("paste", [this] { forwardToEditor("clipboard.paste"); });
    add_action("select-all", [this] { forwardToEditor("selection.select-all"); });
    // Search
    add_action("find", [this] { showFindBar(); });
    addStubAction("replace", "Replace");
    add_action("goto-line", [this] { gotoLine(); });
    add_action("goto-definition", [this] {
        if (auto* tab = currentEditor()) {
            GtkTextIter cursor;
            GtkTextBuffer* text = GTK_TEXT_BUFFER(tab->buffer());
            gtk_text_buffer_get_iter_at_mark(text, &cursor, gtk_text_buffer_get_insert(text));
            gotoDefinition(tab, cursor);
        }
    });
    // View
    add_action("toggle-left-dock", [this] { leftDock_.set_visible(!leftDock_.get_visible()); });
    add_action("toggle-bottom-dock", [this] { bottomDock_.set_visible(!bottomDock_.get_visible()); });
    add_action("toggle-right-dock", [this] { rightDock_.set_visible(!rightDock_.get_visible()); });
    add_action("reset-panels", [this] {
        docks_->resetLayout();
        setStatus("Panels moved back to the bottom");
    });
    // Project
    add_action("project-add-file", [this] { newFile(); });
    add_action("project-options", [this] { showProjectOptions(false); });
    add_action("packages", [this] { showProjectOptions(true); });
    // Execute
    add_action("compile", [this] { startBuild(BuildAction::Build); });
    add_action("run", [this] { runProgram(); });
    add_action("compile-run", [this] { compileAndRun(); });
    add_action("rebuild", [this] { startBuild(BuildAction::Rebuild); });
    add_action("clean", [this] { startBuild(BuildAction::Clean); });
    add_action("stop", [this] { stopExecution(); });
    // Debug
    add_action("debug", [this] { startDebugging(); });
    add_action("debug-continue", [this] { debugStep(&DebugController::resume); });
    add_action("step-over", [this] { debugStep(&DebugController::stepOver); });
    add_action("step-into", [this] { debugStep(&DebugController::stepInto); });
    add_action("step-out", [this] { debugStep(&DebugController::stepOut); });
    add_action("debug-pause", [this] {
        if (debugger_) debugger_->pause();
    });
    add_action("debug-stop", [this] {
        if (debugger_) debugger_->terminate();
        else setStatus("Not debugging");
    });
    add_action("toggle-breakpoint", [this] { toggleBreakpointAtCursor(); });
    add_action("add-watch", [this] { addWatch(); });
    // Tools
    add_action("dev-server", [this] { toggleDevServer(); });
    add_action("dev-server-open", [this] { openInBrowser(); });
    add_action("toolchain-info", [this] { showToolchainInfo(); });
    add_action("about", [this] { showAbout(); });
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

    submenu("_File", {
        {{"New _Project…", "win.new-project"}, {"_New Source File…", "win.new-file"}},
        {{"_Open Project or Folder…", "win.open-project"}},
        {{"_Save", "win.save"}, {"Save _All", "win.save-all"}, {"_Close", "win.close-tab"}},
        {{"_Quit", "app.quit"}},
    });
    submenu("_Edit", {
        {{"_Undo", "win.undo"}, {"_Redo", "win.redo"}},
        {{"Cu_t", "win.cut"}, {"_Copy", "win.copy"}, {"_Paste", "win.paste"}},
        {{"Select _All", "win.select-all"}},
    });
    submenu("_Search", {
        {{"_Find…", "win.find"}, {"_Replace…", "win.replace"}},
        {{"_Go to Line…", "win.goto-line"}, {"Go to _Definition (Ctrl+Click)", "win.goto-definition"}},
    });
    submenu("_View", {
        {{"_Project Panel", "win.toggle-left-dock"}, {"_Bottom Panel", "win.toggle-bottom-dock"},
         {"_Right Panel", "win.toggle-right-dock"}},
        {{"Reset Panel _Layout", "win.reset-panels"}},
    });
    submenu("P_roject", {
        {{"_New File in Project…", "win.project-add-file"}},
        {{"_Packages…", "win.packages"}, {"Project _Options…", "win.project-options"}},
    });
    submenu("E_xecute", {
        {{"_Compile", "win.compile"}, {"_Run", "win.run"}, {"Compile & R_un", "win.compile-run"}},
        {{"Rebuild _All", "win.rebuild"}, {"C_lean", "win.clean"}},
        {{"_Stop Build or Program", "win.stop"}},
    });
    submenu("_Debug", {
        {{"_Debug", "win.debug"}, {"_Continue", "win.debug-continue"}, {"_Pause", "win.debug-pause"},
         {"_Stop", "win.debug-stop"}},
        {{"Step _Over", "win.step-over"}, {"Step _Into", "win.step-into"}, {"Step O_ut", "win.step-out"}},
        {{"Toggle _Breakpoint", "win.toggle-breakpoint"}, {"Add _Watch…", "win.add-watch"}},
    });
    submenu("_Tools", {
        {{"Start/Stop TypeScript _Dev Server (port 5465)", "win.dev-server"},
         {"_Open Dev Server in Browser", "win.dev-server-open"}},
        {{"_Toolchain Info", "win.toolchain-info"}},
    });
    submenu("_Help", {
        {{"_About Kev-C++", "win.about"}},
    });
    return bar;
}

Gtk::Widget& MainWindow::buildToolbar() {
    auto* bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 2);
    bar->set_margin(4);
    auto button = [bar](const char* icon, const char* action, const char* tooltip) {
        auto* b = Gtk::make_managed<Gtk::Button>();
        b->set_icon_name(icon);
        b->set_action_name(action);
        b->set_tooltip_text(tooltip);
        b->set_has_frame(false);
        bar->append(*b);
    };
    auto separator = [bar] {
        auto* s = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL);
        s->set_margin_start(4);
        s->set_margin_end(4);
        bar->append(*s);
    };

    button("folder-new-symbolic", "win.new-project", "New Project (Ctrl+Shift+N)");
    button("document-new-symbolic", "win.new-file", "New Source File (Ctrl+N)");
    button("document-open-symbolic", "win.open-project", "Open Project or Folder (Ctrl+O)");
    button("document-save-symbolic", "win.save", "Save (Ctrl+S)");
    separator();
    button("edit-find-symbolic", "win.find", "Find (Ctrl+F)");
    separator();
    button("system-run-symbolic", "win.compile", "Compile (F9)");
    button("media-playback-start-symbolic", "win.run", "Run (F10)");
    button("media-seek-forward-symbolic", "win.compile-run", "Compile & Run (F11)");
    button("view-refresh-symbolic", "win.rebuild", "Rebuild All (F12)");
    button("media-playback-stop-symbolic", "win.stop", "Stop build or running program (Ctrl+F2)");
    configDrop_ = Gtk::make_managed<Gtk::DropDown>(std::vector<Glib::ustring>{"Debug", "Release"});
    configDrop_->set_tooltip_text("Build configuration");
    configDrop_->set_margin_start(4);
    bar->append(*configDrop_);
    separator();
    button("media-record-symbolic", "win.debug", "Debug (F5)");
    button("go-next-symbolic", "win.step-over", "Step Over (F6)");
    button("go-down-symbolic", "win.step-into", "Step Into (F7)");
    button("go-up-symbolic", "win.step-out", "Step Out (Shift+F7)");
    button("media-skip-forward-symbolic", "win.debug-continue", "Continue (F8)");
    button("process-stop-symbolic", "win.debug-stop", "Stop Debugging (Shift+F5)");
    separator();
    button("network-server-symbolic", "win.dev-server", "TypeScript Dev Server on port 5465");
    return *bar;
}

Gtk::Widget& MainWindow::buildLeftDock() {
    leftDock_.append_page(projectTree_, "Project");
    leftDock_.append_page(symbols_, "Classes");
    leftDock_.append_page(watches_, "Debug");
    leftDock_.set_size_request(180, -1);
    return leftDock_;
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
    findBox->append(findCount_);
    findBar_.set_child(*findBox);
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
    welcomePage_ = &placeholder("Welcome to Kev-C++\n\n"
                                "Ctrl+Shift+N  new project\n"
                                "Ctrl+O        open a project or folder\n"
                                "F9 / F10 / F11  compile / run / compile & run\n"
                                "F5            debug   (F6 step over, F7 step into, F8 continue)");
    editors_.append_page(*welcomePage_, "Welcome");

    editorArea_.append(findBar_);
    editorArea_.append(editors_);
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
    buffer->create_tag("error")->property_foreground() = "#e01b24";
    buffer->create_tag("warning")->property_foreground() = "#e5a50a";
    buffer->create_tag("success")->property_foreground() = "#2ec27e";
    logEnd_ = buffer->create_mark("end", buffer->end(), false);

    // Every panel can be dragged to the right dock or out into its own window, or moved from the
    // menu on its tab.
    docks_ = std::make_unique<DockManager>(*this, bottomDock_, rightDock_, rightPaned_);
    docks_->add("compiler", "Compiler", compilerPanel_);
    docks_->add("compile-log", "Compile Log", *logScroller);
    docks_->add("run", "Run", runConsole_);
    docks_->add("debug", "Debug", debugPanel_);
    docks_->add("find", "Find Results", placeholder("Search results appear here."));
    docks_->add("terminal", "Terminal", shell_);
    docks_->add("dev-server", "Dev Server", buildServerPage());
    bottomDock_.set_size_request(-1, 120);
    docks_->acceptFloatingDrops(root_);
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
    else if (fs::is_regular_file(abs, ec)) openFile(abs);
    else showError(*this, "Cannot open " + abs.string(), "The path does not exist.");
}

void MainWindow::newProject() {
    const fs::path location = session_ ? session_->root().parent_path() : defaultProjectLocation();
    auto* dialog = new NewProjectDialog(*this, location,
        [this](const std::string& name, const fs::path& parent, ProjectTemplate tmpl) -> Glib::ustring {
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
                askChoice(*this, "Enable Kev-C++ management?",
                          "This CMake project was not created by Kev-C++. Enabling management adds " +
                              root.filename().string() + ".kevc and a marked block at the end of CMakeLists.txt "
                              "that lists your sources. Nothing else in the file is changed.",
                          {"Cancel", "Open as Plain Folder", "Enable"}, 0, 2, [this, root](int choice) {
                              if (choice == 1) setPlainFolder(root);
                              if (choice != 2) return;
                              try {
                                  setProject(std::make_unique<ProjectSession>(ProjectSession::adoptCMake(root)));
                              } catch (const std::exception& e) {
                                  showError(*this, "Could not enable Kev-C++ management", e.what());
                              }
                          });
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
    if (debugger_) debugger_->terminate();
    if (devServer_ && devServer_->root() != session->root()) devServer_.reset();
    updateServerPage();
    breakpoints_.clear();
    updateBreakpointsPanel();
    watcher_.reset();
    session_ = std::move(session);
    folder_ = session_->root();
    lsp_->setWorkspace(folder_, &session_->project());
    projectTree_.setRoot(folder_);
    shell_.startShell(folder_);
    updateTitle();

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
    setStatus("Opened " + session_->project().name + " (" + kindLabel(session_->project().kind) + ")");
}

void MainWindow::setPlainFolder(const fs::path& root) {
    if (debugger_) debugger_->terminate();
    if (devServer_ && devServer_->root() != root) devServer_.reset();
    updateServerPage();
    breakpoints_.clear();
    updateBreakpointsPanel();
    watcher_.reset();
    session_.reset();
    folder_ = root;
    lsp_->setWorkspace(folder_, nullptr);
    projectTree_.setRoot(folder_);
    shell_.startShell(folder_);
    updateTitle();
    setStatus("Opened folder " + root.string());
}

void MainWindow::onProjectSynced(const ProjectSession::SyncResult& result) {
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
    if (session_) set_title(session_->project().name + " — Kev-C++ (" + kindLabel(session_->project().kind) + ")");
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

EditorTab* MainWindow::openFile(const fs::path& file, int line, int column) {
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(file, ec);
    for (EditorTab* tab : editors()) {
        if (fs::weakly_canonical(tab->path(), ec) == canonical) {
            editors_.set_current_page(editors_.page_num(*tab));
            if (line > 0) tab->gotoLine(line, column);
            else tab->focusEditor();
            return tab;
        }
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

    if (welcomePage_) {
        editors_.remove_page(*welcomePage_);
        welcomePage_ = nullptr;
    }
    if (folder_.empty() && lspRootForLooseFiles_.empty()) {
        // A file opened on its own: give the language servers its folder as the workspace.
        lspRootForLooseFiles_ = canonical.parent_path();
        lsp_->setWorkspace(lspRootForLooseFiles_, nullptr);
    }
    setupIntelliSense(tab);
    const int page = editors_.append_page(*tab, tab->tabLabel());
    editors_.set_tab_reorderable(*tab, true);
    tab->signalCloseRequested().connect([this, tab] { closeEditor(tab); });
    tab->signalMatchCountChanged().connect([this, tab](int count) {
        if (tab == currentEditor() && findBar_.get_search_mode())
            findCount_.set_text(count < 0 ? "" : std::to_string(count) + (count == 1 ? " match" : " matches"));
    });
    editors_.set_current_page(page);
    if (line > 0) tab->gotoLine(line, column);
    else tab->focusEditor();
    return tab;
}

void MainWindow::newFile() {
    if (folder_.empty()) {
        showError(*this, "No project open", "Create or open a project first (File → New Project).");
        return;
    }
    std::string suggestion = "untitled.txt";
    if (session_) {
        switch (session_->project().kind) {
        case ProjectKind::Cpp: suggestion = "src/untitled.cpp"; break;
        case ProjectKind::C: suggestion = "src/untitled.c"; break;
        case ProjectKind::WebTs: suggestion = "src/untitled.ts"; break;
        }
    }
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
                   openFile(*target);
                   // The SourceWatcher adds it to the build file shortly after.
                   return {};
               });
}

void MainWindow::saveCurrent() {
    EditorTab* tab = currentEditor();
    if (!tab) return;
    try {
        tab->save();
        setStatus("Saved " + tab->path().filename().string());
    } catch (const Glib::Error& e) {
        showError(*this, "Could not save " + tab->path().filename().string(), e.what());
    }
}

bool MainWindow::saveAll() {
    int saved = 0;
    for (EditorTab* tab : editors()) {
        if (!tab->modified()) continue;
        try {
            tab->save();
            ++saved;
        } catch (const Glib::Error& e) {
            showError(*this, "Could not save " + tab->path().filename().string(), e.what());
            return false;
        }
    }
    if (saved) setStatus("Saved " + std::to_string(saved) + (saved == 1 ? " file" : " files"));
    return true;
}

void MainWindow::closeEditor(EditorTab* tab) {
    if (!tab->modified()) {
        removeEditor(tab);
        return;
    }
    askChoice(*this, "Save changes to " + tab->path().filename().string() + "?", "Unsaved changes will be lost.",
              {"Cancel", "Discard", "Save"}, 0, 2, [this, tab](int choice) {
                  if (choice == 2) {
                      try {
                          tab->save();
                      } catch (const Glib::Error& e) {
                          showError(*this, "Could not save " + tab->path().filename().string(), e.what());
                          return;
                      }
                  }
                  if (choice == 1 || choice == 2) removeEditor(tab);
              });
}

void MainWindow::closeAllEditors(std::function<void()> then) {
    auto eds = editors();
    const auto unsaved = std::count_if(eds.begin(), eds.end(), [](EditorTab* t) { return t->modified(); });
    auto closeAll = [this, then] {
        for (EditorTab* tab : editors()) removeEditor(tab);
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

void MainWindow::removeEditor(EditorTab* tab) {
    syncBreakpoints(tab);
    if (execTab_ == tab) execTab_ = nullptr;
    lsp_->detach(tab);
    editors_.remove_page(*tab);
}

void MainWindow::forwardToEditor(const char* action) {
    if (auto* tab = currentEditor()) tab->activateTextAction(action);
}

// ---------------------------------------------------------------------------------------------
// Search

void MainWindow::showFindBar() {
    EditorTab* tab = currentEditor();
    if (!tab) return;
    GtkTextBuffer* text = GTK_TEXT_BUFFER(tab->buffer());
    GtkTextIter start, end;
    if (gtk_text_buffer_get_selection_bounds(text, &start, &end) &&
        gtk_text_iter_get_line(&start) == gtk_text_iter_get_line(&end)) {
        char* selected = gtk_text_buffer_get_text(text, &start, &end, FALSE);
        findEntry_.set_text(selected);
        g_free(selected);
    }
    findBar_.set_search_mode(true);
    findEntry_.grab_focus();
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

void MainWindow::startBuild(BuildAction action, std::function<void(bool)> then, std::optional<BuildConfig> config) {
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
    if (commands.empty()) {
        if (session_->project().kind == ProjectKind::WebTs && action != BuildAction::Clean) {
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
    const char* verb = session_->project().kind == ProjectKind::WebTs ? "Type-checking" : verbs[static_cast<int>(action)];
    compilerPanel_.setEmptyText(std::string(verb) + "…");
    parser_ = std::make_unique<DiagnosticParser>(session_->root());
    afterBuild_ = std::move(then);
    buildAction_ = action;
    buildStart_ = std::chrono::steady_clock::now();
    appendLog(std::string(verb) + " " + session_->project().name + " (" +
                  (buildConfig_ == BuildConfig::Debug ? "Debug" : "Release") + ")\n\n",
              "summary");
    showBottomPage(*compileLog_.get_parent());
    setStatus(std::string(verb) + "…");
    build_.start(std::move(commands));
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
    if (buildAction_ == BuildAction::Clean) {
        status << (success ? "Clean finished" : "Clean failed");
        compilerPanel_.setEmptyText(success ? "Cleaned." : "Clean failed. See the Compile Log.");
    } else if (session_ && session_->project().kind == ProjectKind::WebTs) {
        status << (success ? "Type check passed" : "Type check failed: ") << (success ? "" : std::to_string(errors) + " errors")
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
}

void MainWindow::runProgram() {
    if (!session_) {
        setStatus("Open a Kev-C++ project to run it");
        return;
    }
    if (build_.running()) {
        setStatus("Wait for the build to finish, or stop it first");
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
        compileAndRun();
        return;
    }
    showBottomPage(runConsole_);
    runConsole_.runProgram({output->string()}, session_->root(), [this](const std::string& outcome) {
        setStatus(outcome);
    });
    runConsole_.focusTerminal();
    setStatus("Running " + output->filename().string() + "…");
}

void MainWindow::compileAndRun() {
    startBuild(BuildAction::Build, [this](bool success) {
        if (success) runProgram();
    });
}

void MainWindow::stopExecution() {
    if (build_.running()) build_.cancel();
    else if (runConsole_.childRunning()) runConsole_.stopChild();
    else setStatus("Nothing is running");
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
    serverToggle_.signal_clicked().connect([this] { toggleDevServer(); });
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
    serverToggle_.set_label(running ? "Stop" : "Start");
    serverOpen_.set_sensitive(ready);
    if (ready) serverStatus_.set_markup("Serving <b>" + Glib::Markup::escape_text(devServer_->root().filename().string()) +
                                        "</b> at <a href=\"" + Glib::Markup::escape_text(devServer_->url()) + "\">" +
                                        Glib::Markup::escape_text(devServer_->url()) + "</a> (this computer only)");
    else if (running) serverStatus_.set_text("Starting…");
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
    const fs::path script = toolsDir_ / "devserver/devserver.mjs";
    std::error_code ec;
    if (!fs::exists(script, ec) || !fs::exists(toolsDir_ / "node_modules/typescript", ec)) {
        showError(*this, "The dev server is not installed",
                  "Expected " + script.string() + " and the bundled TypeScript (run npm ci in " + toolsDir_.string() + ").");
        return;
    }
    openBrowserWhenReady_ = openBrowserWhenReady;
    if (devServer_ && devServer_->running()) {
        if (devServer_->ready() && openBrowserWhenReady) openInBrowser();
        return;
    }
    devServer_ = std::make_unique<DevServer>(app_.toolchain().pathOf("node").string(), script);
    devServer_->signalLog().connect([this](const std::string& line) {
        auto buffer = serverLog_.get_buffer();
        buffer->insert(buffer->end(), line + "\n");
        auto end = buffer->create_mark(buffer->end());
        serverLog_.scroll_to(end);
        buffer->delete_mark(end);
    });
    devServer_->signalReady().connect([this](const std::string& url) {
        updateServerPage();
        setStatus("Dev server running at " + url + " (live reload on save)");
        if (openBrowserWhenReady_) openInBrowser();
        openBrowserWhenReady_ = false;
    });
    devServer_->signalStopped().connect([this](int status) {
        updateServerPage();
        if (status == DevServer::kExitPortInUse) {
            setStatus("Port " + std::to_string(DevServer::kDefaultPort) +
                      " is already in use. Stop whatever is using it, then press Start in the Dev Server tab.");
            showBottomPage(serverPage_);
        } else {
            setStatus("Dev server stopped");
        }
    });
    serverLog_.get_buffer()->set_text("");
    try {
        devServer_->start(folder_);
    } catch (const Glib::Error& e) {
        showError(*this, "Could not start the dev server", e.what());
        devServer_.reset();
    }
    updateServerPage();
}

void MainWindow::openInBrowser() {
    if (!devServer_ || !devServer_->ready()) {
        startDevServer(true);
        return;
    }
    try {
        Gio::AppInfo::launch_default_for_uri(devServer_->url());
    } catch (const Glib::Error& e) {
        setStatus("Could not open a browser: " + std::string(e.what()) + ". Visit " + devServer_->url());
    }
}

// ---------------------------------------------------------------------------------------------
// Debugging

void MainWindow::startDebugging() {
    if (debugger_) {
        if (debugger_->stopped()) debugger_->resume(); // F5 while paused continues, the same as F8
        return;
    }
    if (session_ && session_->project().kind == ProjectKind::WebTs) {
        setStatus("Debug TypeScript in the browser's developer tools (F12 there); source maps point at your .ts files");
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
    startBuild(BuildAction::Build, [this](bool ok) {
        if (ok) launchDebugger();
    }, BuildConfig::Debug);
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
    debugger_->signalStateChanged().connect([this](DebugController::State state) {
        if (state != DebugController::State::Running) return;
        ++stopGeneration_;
        clearExecutionLine();
        lsp_->setHoverEnabled(true);
        debugPanel_.showMessage("Running…  (Debug → Pause to break in)");
        setStatus("Running " + session_->project().name + " under the debugger…");
    });

    std::vector<std::pair<fs::path, int>> bps;
    for (const auto& [file, lines] : breakpoints_)
        for (int line : lines) bps.emplace_back(file, line);
    debugPanel_.clearConsole();
    debugPanel_.showMessage("Starting gdb…");
    showBottomPage(runConsole_);
    setStatus("Debugging " + exe->filename().string() +
              (bps.empty() ? " (no breakpoints set: it will run to the end)" : ""));
    debugger_->start(bps);
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
    debugPanel_.showMessage(outcome + ". Press F5 to debug again.");
    watches_.showPlaceholders();
    // gdb reports the exit before the terminal has drawn the program's last output (VTE reads a pty
    // it didn't spawn in batches, more slowly while hidden), so show the Run tab and add the note later.
    showBottomPage(runConsole_);
    Glib::signal_timeout().connect_once(
        sigc::track_obj([this, outcome] { runConsole_.feedText("\n\x1b[2m[" + outcome + "]\x1b[0m\n"); }, *this), 1500);
    setStatus(outcome);
    // We are inside one of the controller's signals: drop it once that returns.
    Glib::signal_idle().connect_once([this] { debugger_.reset(); });
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

void MainWindow::onBreakpointToggled(EditorTab* tab, int line, bool on) {
    if (on) breakpoints_[tab->path()].insert(line);
    else breakpoints_[tab->path()].erase(line);
    if (debugger_) debugger_->setBreakpoint(tab->path(), line, on);
    updateBreakpointsPanel();
    setStatus(std::string(on ? "Breakpoint set at " : "Breakpoint removed from ") + tab->path().filename().string() +
              ":" + std::to_string(line));
}

void MainWindow::syncBreakpoints(EditorTab* tab) {
    // Marks follow edits, so the editor knows where breakpoints are now.
    const auto lines = tab->breakpoints();
    auto& stored = breakpoints_[tab->path()];
    const std::set<int> now(lines.begin(), lines.end());
    if (now == stored) return;
    stored = now;
    updateBreakpointsPanel();
}

void MainWindow::updateBreakpointsPanel() {
    std::vector<std::pair<fs::path, int>> items;
    for (const auto& [file, lines] : breakpoints_)
        for (int line : lines) items.emplace_back(file, line);
    debugPanel_.setBreakpoints(items);
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
               });
}

// ---------------------------------------------------------------------------------------------
// IntelliSense

void MainWindow::setupIntelliSense(EditorTab* tab) {
    lsp_->attach(tab);
    tab->signalSaved().connect([this, tab] {
        lsp_->saved(tab);
        syncBreakpoints(tab);
    });

    // Breakpoints set before the file was (re)opened.
    if (auto it = breakpoints_.find(tab->path()); it != breakpoints_.end())
        tab->setBreakpoints(std::vector<int>(it->second.begin(), it->second.end()));
    tab->signalBreakpointToggled().connect([this, tab](int line, bool on) { onBreakpointToggled(tab, line, on); });

    // Values under the mouse while paused; added first so they appear above anything else.
    GtkSourceHoverProvider* debugHoverProvider = newDebugHoverProvider(
        [this, tab](const GtkTextIter& at, std::function<void(GtkWidget*)> show) { return debugHover(tab, at, std::move(show)); });
    gtk_source_hover_add_provider(gtk_source_view_get_hover(tab->view()), debugHoverProvider);
    g_object_unref(debugHoverProvider);

    GtkSourceCompletion* completion = gtk_source_view_get_completion(tab->view());
    GtkSourceCompletionProvider* provider = newLspCompletionProvider(lsp_.get(), tab);
    gtk_source_completion_add_provider(completion, provider);
    g_object_unref(provider);
    g_object_set(completion, "show-icons", FALSE, "select-on-show", TRUE, "page-size", 10, nullptr);

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
        gotoDefinition(tab, iter);
    });
    view->add_controller(click);
}

void MainWindow::gotoDefinition(EditorTab* tab, const GtkTextIter& at) {
    const bool sent = lsp_->handles(tab);
    if (!sent) {
        setStatus("Go to Definition needs IntelliSense for this file");
        return;
    }
    setStatus("Finding definition…");
    lsp_->definition(tab, at, [this](std::optional<LspLocation> location) {
        if (!location) {
            setStatus("No definition found");
            return;
        }
        if (EditorTab* target = openFile(location->file)) {
            target->placeCursor(LspManager::iterAt(GTK_TEXT_BUFFER(target->buffer()), location->line, location->utf16));
            setStatus(location->file.filename().string() + ":" + std::to_string(location->line + 1));
        }
    });
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
    if (session_->project().kind == ProjectKind::WebTs) {
        setStatus("TypeScript projects have no compiler or linker options");
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

void MainWindow::showAbout() {
    auto* about = new Gtk::AboutDialog();
    about->set_transient_for(*this);
    about->set_modal(true);
    about->set_program_name("Kev-C++");
    about->set_version(KEVC_VERSION);
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
