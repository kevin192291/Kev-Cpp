#pragma once

#include "app/MainWindow.ui.h"

#include "build/BuildRunner.h"
#include "build/CompileLog.h"
#include "build/CompilerPanel.h"
#include "core/Boilerplate.h"
#include "core/BuildPlan.h"
#include "core/Diagnostics.h"
#include "core/ProjectSession.h"
#include "core/ProjectState.h"
#include "core/RecentProjects.h"
#include "core/Version.h"
#include "core/WebProject.h"
#include "debug/DebugController.h"
#include "debug/DebugPanel.h"
#include "debug/DebugViews.h"
#include "debug/WatchPanel.h"
#include "debug/JsDebugger.h"
#include "devserver/DevServer.h"
#include "editor/ProjectTree.h"
#include "editor/RoutesPanel.h"
#include "editor/SymbolsPanel.h"
#include "lsp/LspManager.h"
#include "ui/DockManager.h"
#include "ui/DependenciesPanel.h"
#include "ui/DevServerPanel.h"
#include "ui/EnvPanel.h"
#include "ui/QuickSearch.h"
#include "ui/SearchPanel.h"
#include "ui/TestsPanel.h"
#include "ui/TracesPanel.h"
#include "ui/WelcomePage.h"
#include "ui/IconWidgets.h"
#include "terminal/TerminalPane.h"

#include <gtkmm.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace kevc {

class Application;
class CodeFormatter;
class DesignerPage;
class DocumentPage;
class EditorTab;
class SourceWatcher;

// Kev-C++'s window. Its frame and menus are MainWindow.ui (edit it on Kev-C++'s Design page): the menu
// row, the toolbar's groups, the docks, the find bar, the status bar. This class fills the places the
// design leaves for what's made in code (the search field, the panels, the editors) and picks which
// of the design's menu sections and toolbar groups apply to what's open.
class MainWindow : public MainWindowUi {
public:
    static MainWindow* create(Application& app) { return MainWindowUi::create<MainWindow>(app); }
    MainWindow(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder, Application& app);
    ~MainWindow() override;
    // Closes as the window's close button does, but unsaved files aren't asked about (a SIGTERM at
    // logout: nobody is there to answer). Breakpoints and the project's state are still saved.
    void closeWithoutQuestions();

    void setStatus(const Glib::ustring& message);
    // Opens a project folder, a .kevc file, or a single file (from the command line).
    void openPath(const std::filesystem::path& path);

protected:
    // The find bar's handlers (named in MainWindow.ui).
    void on_find_changed() override;
    void on_find_activate() override;
    void on_find_next_match() override;
    void on_find_previous_match() override;
    void on_find_stop_search() override;
    void on_find_previous_clicked() override;
    void on_find_next_clicked() override;
    void on_find_case_toggled() override;
    void on_find_everywhere_clicked() override;
    void on_replace_toggled() override;
    void on_replace_activate() override;
    void on_replace_clicked() override;
    void on_replace_all_clicked() override;

private:
    // Filling in the design
    void buildActions();
    void setupToolbar();
    void setupLeftDock();
    // The left dock's tabs show their names where they fit (the selected tab first, then Project and
    // Search); the others show their icon, named in a tooltip, so no tab hides behind scroll arrows.
    void fitLeftTabs();
    void setupEditorArea();
    void setupBottomDock();
    // Registers a win.<name> action that only reports it is not implemented yet.
    void addStubAction(const Glib::ustring& name, const Glib::ustring& label);
    // Shows the toolbar groups and panels the current profile (what's open) wants; see
    // core/ProjectKinds. Called whenever a project or folder is opened.
    void applyProfile();
    // The File, View, Project, Execute and Tools menus list only what applies to what's open (see
    // core/ProjectKinds), and web projects get web names (Check Types, Production Build): the design
    // has a section for each case, and this picks them.
    void updateMenus();
    void updateToolbarRows();

    // Projects
    void newProject();
    void chooseFolder();
    void openFolder(const std::filesystem::path& root);
    void setProject(std::unique_ptr<ProjectSession> session);
    void setPlainFolder(const std::filesystem::path& root);
    // File → Close Project (or Close Folder): asks about unsaved files, stops what the project runs
    // (build, program, debugger, dev server) and goes back to the Welcome tab with nothing open.
    void closeProject();
    void onProjectSynced(const ProjectSession::SyncResult& result);
    void updateTitle();

    // Editors. Every page in the editor area is a DocumentPage (a file being edited); text files
    // are EditorTabs, .ui files open in the GTK designer.
    EditorTab* currentEditor();
    std::vector<EditorTab*> editors();
    DocumentPage* currentDocument();
    std::vector<DocumentPage*> documents();
    DocumentPage* findDocument(const std::filesystem::path& file);
    // Opens a file the way its type wants: .ui files in the designer, anything else as text.
    void openDocument(const std::filesystem::path& file);
    EditorTab* openFile(const std::filesystem::path& file, int line = 0, int column = 0);
    DesignerPage* openDesigner(const std::filesystem::path& file);
    void addDocumentPage(DocumentPage& page);
    // The Welcome tab, shown when no file is open: pinned and recent projects, and the main keys.
    void showWelcome();
    void refreshWelcome();
    void rememberFolder(const std::filesystem::path& root);
    // The Project tab's git icons: files with changes git hasn't committed.
    void watchGit();          // after opening a folder: find its repository, watch its index
    void refreshGitStatus();  // soon (bursts of requests make one run)
    void runGitStatus();
    void addSystemPackage(const std::string& module);
    // Links SFML the way this computer's SFML needs (sfml::packagesFor), with the components the
    // project already uses plus `components`. What changed, for the status line; empty if nothing.
    std::string linkSfml(const std::vector<std::string>& components);
    // After a project opens or its packages change: SFML through pkg-config modules that name
    // libraries the computer doesn't have (a static SFML 3) builds against another SFML; offers
    // SFML's CMake package instead. Asks once per project in a session.
    void checkSfmlPackages();
    std::set<std::filesystem::path> sfmlAsked_;
    void newFile();
    // The Project tab's context menu and drag and drop. Open editors follow renamed and moved files.
    void newFileIn(const std::filesystem::path& dir);
    void newFolderIn(const std::filesystem::path& dir);
    void renamePath(const std::filesystem::path& path);
    void deletePath(const std::filesystem::path& path);
    void openTerminalIn(const std::filesystem::path& dir); // the Project tab's Open Terminal Here
    void movePath(const std::filesystem::path& source, const std::filesystem::path& targetDir);
    // After `from` became `to`: editors, breakpoints and the routes and git views catch up.
    void relocated(const std::filesystem::path& from, const std::filesystem::path& to);
    void filesChanged();
    void saveCurrent();
    bool saveAll();
    void closeEditor(DocumentPage* page);
    // Closes every editor, asking about unsaved changes first; runs `then` unless cancelled.
    void closeAllEditors(std::function<void()> then);
    void removeEditor(DocumentPage* page);
    void forwardToEditor(const char* action);

    // Search
    // Find in Files (the Search tab), shown and focused; the menu bar's search field (Ctrl+P).
    void findInFiles(Glib::ustring text = {});
    void focusQuickSearch();
    // Open editors' text where it has unsaved changes, by path: what searches look at for those files.
    std::map<std::filesystem::path, std::string> unsavedEditorText();
    Glib::ustring selectedLineText(); // the current editor's selection, if it's on one line
    void showFindBar();
    void showReplaceBar(); // the find bar with its Replace row (Ctrl+R)
    void replaceNext();
    void replaceAllInFile();
    void findNext(bool forward, bool incremental = false);
    void gotoLine();

    // Build & run. A Task is the toolbar button that started a build or program: that button shows
    // Stop until it's over.
    enum class Task { None, Compile, Rebuild, Clean, CompileRun, Run, Debug };
    BuildConfig currentConfig() const;
    BuildTools buildTools() const;
    // Builds the open project; `then` runs with the result unless the build could not start.
    // `config` overrides the toolbar's Debug/Release choice (debugging always builds Debug).
    void startBuild(BuildAction action, Task task, std::function<void(bool)> then = {},
                    std::optional<BuildConfig> config = std::nullopt);
    void onBuildLine(const std::string& line, bool isCommand);
    void onBuildFinished(bool success);
    void runProgram(Task task = Task::Run);
    void compileAndRun(Task task = Task::CompileRun);
    void stopExecution();
    void stopTask(Task task);
    bool programRunning() const;
    // Start/Stop and enabled states of the toolbar buttons, from what is running now.
    void updateToolbar();
    void appendLog(const std::string& text, const Glib::ustring& tag = {});
    void showBottomPage(Gtk::Widget& page);

    // Runs commands in the Compile Log like a build (output parsed into the Compiler panel).
    // `doneLabel` names the job in the status bar when it ends ("Install"); empty for builds.
    void runCommands(std::vector<Command> commands, const std::string& heading, Task task,
                     std::function<void(bool)> then, std::string doneLabel = {});

    // Web projects (package.json): flavour, scripts, dependencies, SvelteKit creation.
    void refreshWebProject();
    void installDependencies();
    // Project → Packages in a web project: versions, locking, the package source (NpmPackagesDialog).
    void showNpmPackages();
    // A project that uses its own CMakeLists.txt: which of its add_executable() programs Run and Debug start.
    void chooseProgram();
    void npmLogin(const std::string& registry); // `npm login` in the Run tab, where the password is typed
    void runScript(const std::string& name);
    Glib::ustring createSvelteKitApp(const std::string& name, const std::filesystem::path& parent);
    void openUrl(const std::string& url);
    // SvelteKit: the Routes tab and the Environment panel, rescanned as files change.
    void refreshKit();
    void scheduleKitRefresh();
    void newRoute();
    void openRoute(const std::string& url);
    // Tests (Vitest, Playwright), traces (a local OpenTelemetry collector) and npm's dependency report.
    void runTests(TestRunner runner, const std::filesystem::path& file = {}, const std::string& title = {}, int line = 0,
                  const std::vector<std::filesystem::path>& files = {});
    std::vector<std::pair<std::string, std::string>> traceEnvironment();
    void refreshDependencies();

    // The Boilerplate menu (MainWindowBoilerplate.cpp): starters and patterns from folders of snippet
    // files (core/Boilerplate), Kev-C++'s own and the user's, inserted at the cursor or into a new file.
    struct BoilerplateRequest; // a chosen snippet on its way into the editor
    std::vector<std::filesystem::path> boilerplateRoots() const; // built-in first, then the user's
    std::filesystem::path userBoilerplateDir() const;
    // Rescans the folders and rebuilds the menu when what it shows changed; not while a menu is open
    // (that would close it), but soon after. The folders are watched: a change rescans them.
    void refreshBoilerplate();
    void refreshBoilerplateSoon();
    // Runs `then` once the menu an item was chosen from has gone, closing a menu GTK opens by itself
    // meanwhile (see the .cpp).
    void afterMenuChoice(std::function<void()> then);
    void watchBoilerplate();
    void insertBoilerplate(const std::string& key);
    void askBoilerplateValues(std::shared_ptr<BoilerplateRequest> request, size_t next);
    void placeBoilerplate(std::shared_ptr<BoilerplateRequest> request);
    void insertBoilerplateInto(EditorTab* tab, const BoilerplateRequest& request, bool replaceAll);
    void newFileWithBoilerplate(std::shared_ptr<BoilerplateRequest> request, const std::string& why);
    void boilerplateInserted(const BoilerplateRequest& request, const std::string& where);
    boilerplate::Values boilerplateValues(const BoilerplateRequest& request) const;
    void saveSelectionAsBoilerplate();
    void openBoilerplateFolder();

    void showToolchainInfo();
    // Help → Check for Updates: the latest GitHub release, downloaded and installed over this one.
    void checkForUpdates();
    void installUpdate(const Release& release);
    void showAbout();
    // View → Preferences, and View → Larger/Smaller/Default Text Size (Ctrl+Plus, Ctrl+Minus, Ctrl+0):
    // the editors' font, kept in settings for every project.
    void showPreferences();
    void zoomText(int steps); // 0: back to the default size

    // TypeScript dev server
    Gtk::Widget& buildServerPage();
    void toggleDevServer();
    void startDevServer(bool openBrowserWhenReady);
    void openInBrowser();
    void updateServerPage();

    // Debugging
    void startDebugging();
    // JavaScript/TypeScript (Vite projects) through js-debug: server code, and browser code when a
    // Chromium-based browser is installed.
    void startJsDebugging();
    void refreshJsViews(int frameIndex);
    void launchDebugger();
    void debugStep(void (DebugController::*step)());
    void onDebugStopped(const StopInfo& info);
    void onDebugFinished(const std::string& outcome);
    void refreshDebugViews();
    void showFrame(const StackFrame& frame);
    void clearExecutionLine();
    void toggleBreakpointAtCursor();
    // The four kinds of breakpoint (core/Breakpoints): standard, IF, live, and break when a value
    // changes. setBreakpointAt goes through the file's editor when it's open, so its marks agree;
    // onBreakpointChanged then hands the change to the debugger, the panel and the saved state.
    void setBreakpointAt(const std::filesystem::path& file, int line, const std::optional<Breakpoint>& breakpoint);
    void onBreakpointChanged(const std::filesystem::path& file, int line, const std::optional<Breakpoint>& breakpoint);
    void promptConditional(const std::filesystem::path& file, int line);
    void promptLive(const std::filesystem::path& file, int line);
    void promptValueWatch();
    void removeValueWatch(const std::string& expression);
    std::vector<std::string> valueWatchExpressions() const;
    void resetValueWatchStates(); // nothing debugging: they wait for it
    void noteWatchTriggered(const std::string& description);
    void onLiveHit(const std::filesystem::path& file, int line, const std::vector<std::string>& names,
                   const std::vector<std::string>& values);
    bool flushLiveValues(); // the latest values on screen, a few times a second at most
    void clearLiveValues();
    bool cursorLine(std::filesystem::path& file, int& line); // false (and says why) without an editor
    std::string expressionAtCursor();
    void syncBreakpoints(EditorTab* tab);
    // Breakpoints and watches are remembered per project (core/ProjectState): saved whenever they
    // change and before the project closes, restored when it opens.
    void saveProjectState();
    void restoreProjectState();
    // The open project's look (core/Project's ProjectUi): toolbars and panels shown, Debug/Release.
    // Kept in its .kevc, or with the remembered state for a web folder; with nothing open, View →
    // Toolbars/Panels change the IDE's defaults for that kind of project instead.
    bool toolbarShown(const std::string& group) const;
    bool panelShownHere(const std::string& panel) const;
    void setToolbarShownHere(const std::string& group, bool shown);
    void setPanelShownHere(const std::string& panel, bool shown);
    void saveProjectUi();
    void updateBreakpointsPanel();
    void addWatch();
    bool debugHover(EditorTab* tab, const GtkTextIter& at, std::function<void(GtkWidget*)> show);

    // IntelliSense
    void setupIntelliSense(EditorTab* tab);
    // Go to Definition (through to a TypeScript library's own source when it lands in a .d.ts),
    // Declaration, Type Definition, Implementation, or Find All References: the editor's right-click
    // menu and the Search menu. One answer opens it; several are listed at the cursor to choose from.
    enum class Lookup { Definition, Declaration, TypeDefinition, Implementation, References };
    void lookUp(EditorTab* tab, const GtkTextIter& at, Lookup what);
    void lookUpAtCursor(Lookup what);
    void showLocations(EditorTab* tab, const GtkTextIter& at, const Glib::ustring& what, std::vector<LspLocation> locations);
    void openLocation(const LspLocation& location);
    // Edit → Show Possible Fixes (Ctrl+.), the editor's right-click menu and the hover's lightbulb:
    // the fixes for the error or warning at `at` (with `orLine`, else on its line) listed there; the
    // one chosen is applied.
    void showFixes(EditorTab* tab, const GtkTextIter& at, bool orLine);
    // Opens a popover parented to editorArea_ under the text at `at`.
    void popUpAtText(Gtk::Popover& popover, EditorTab* tab, const GtkTextIter& at);
    void refreshSymbols();
    void scheduleSymbolsRefresh();
    void showProjectOptions(bool packagesPage);

    Application& app_;
    RecentProjects recent_; // the Welcome tab's recent and pinned projects
    // Async callbacks (subprocesses) hold a weak_ptr to this and do nothing once the window is gone.
    std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
    std::filesystem::path toolsDir_; // bundled Node tools: language server, tsc, dev server
    // Declared before the widgets so it outlives the editors during teardown.
    std::unique_ptr<LspManager> lsp_;
    std::unique_ptr<CodeFormatter> formatter_; // Edit → Auto Format
    std::unique_ptr<ProjectSession> session_;
    std::unique_ptr<SourceWatcher> watcher_;
    std::filesystem::path folder_; // project root or plain folder; empty if nothing is open
    std::filesystem::path lspRootForLooseFiles_;
    bool closeConfirmed_ = false;

    Glib::RefPtr<ProjectTree> projectTree_ = ProjectTree::create();
    Glib::RefPtr<SearchPanel> searchPanel_ = SearchPanel::create();
    QuickSearch quickSearch_;
    Glib::RefPtr<SymbolsPanel> symbols_ = SymbolsPanel::create();
    Glib::RefPtr<RoutesPanel> routesPanel_ = RoutesPanel::create();
    Glib::RefPtr<EnvPanel> envPanel_ = EnvPanel::create();
    sigc::connection kitRefresh_;
    std::string pendingRoute_; // opened once the dev server is ready
    Glib::RefPtr<TestsPanel> testsPanel_ = TestsPanel::create();
    Glib::RefPtr<TracesPanel> tracesPanel_ = TracesPanel::create();
    Glib::RefPtr<DependenciesPanel> depsPanel_ = DependenciesPanel::create();
    TraceStore traces_;
    std::unique_ptr<ManagedProcess> collector_;
    int collectorPort_ = 0;
    TestRunner lastRunner_ = TestRunner::Vitest;
    std::vector<TestCase> lastFailed_;
    sigc::connection symbolsTimer_;
    Gtk::Popover locationsPopover_; // several answers to a Go to…, at the cursor
    Gtk::Popover fixesPopover_;     // Show Possible Fixes' list, at the error
    std::set<std::filesystem::path> warnedUnlisted_; // designer code already reported as not in CMakeLists.txt
    Glib::RefPtr<WelcomePage> welcomePage_; // while no file is open
    std::filesystem::path gitTopLevel_; // the open folder's git work tree ("" if it isn't in one)
    Glib::RefPtr<Gio::FileMonitor> gitMonitor_;
    sigc::connection gitSoon_;
    bool gitRunning_ = false, gitAgain_ = false;
    Glib::RefPtr<CompileLog> compileLog_ = CompileLog::create();
    Glib::RefPtr<CompilerPanel> compilerPanel_ = CompilerPanel::create();
    Glib::RefPtr<TerminalPane> runConsole_ = TerminalPane::create(); // the Run tab
    Glib::RefPtr<TerminalPane> shell_ = TerminalPane::create();      // the Terminal tab
    std::map<Task, std::unique_ptr<TaskButton>> taskButtons_; // the design's buttons that show Stop
    // Named groups of toolbar buttons that View → Toolbars shows or hides per profile: the design's
    // boxes in its toolbar rows, each after a separator.
    std::vector<std::pair<std::string, Gtk::Box*>> toolbarGroups_;
    // View → Toolbars / Panels check items, by group or panel id.
    std::map<std::string, Glib::RefPtr<Gio::SimpleAction>> toolbarActions_;
    std::map<std::string, Glib::RefPtr<Gio::SimpleAction>> panelActions_;
    boilerplate::Entry boilerplate_;  // what the Boilerplate menu shows
    std::string boilerplateSignature_; // of boilerplate_, to rebuild the menu only when it changes
    std::vector<Glib::RefPtr<Gio::FileMonitor>> boilerplateMonitors_;
    sigc::connection boilerplateSoon_;
    int sfmlMajor_ = 0; // the installed SFML's (sfml::detect), for which SFML snippets the menu shows
    std::string profile_ = "none";
    bool applyingProfile_ = false; // setting the Debug/Release choice from the project, not the user
    std::unique_ptr<TaskButton> continueTask_; // Continue while paused, Pause while the program runs
    std::unique_ptr<TaskButton> devServerTask_;

    BuildRunner build_;
    Task buildTask_ = Task::None;   // which button started the running build
    Task programTask_ = Task::None; // which button started the running program
    std::unique_ptr<DiagnosticParser> parser_;
    std::function<void(bool)> afterBuild_;
    BuildAction buildAction_ = BuildAction::Build;
    std::chrono::steady_clock::time_point buildStart_;
    BuildConfig buildConfig_ = BuildConfig::Debug;

    std::unique_ptr<DevServer> devServer_;
    bool serverWasReady_ = false; // tells a failed start from a server that stopped later
    WebProject web_;              // the open web project's package.json, read when it opens
    std::string installLabel_; // the design's label for Install Dependencies, naming npm
    std::string doneLabel_;       // see runCommands
    bool openBrowserWhenReady_ = false;
    Glib::RefPtr<DevServerPanel> serverPanel_ = DevServerPanel::create();

    std::shared_ptr<DebugController> debugger_;
    std::shared_ptr<JsDebugger> jsDebugger_;
    std::vector<dap::Frame> jsFrames_;
    std::string jsServerUrl_; // the Vite the JS debugger runs, once it's serving
    Glib::RefPtr<DebugPanel> debugPanel_ = DebugPanel::create();
    Glib::RefPtr<WatchPanel> watches_ = WatchPanel::create();
    BreakpointMap breakpoints_;                // file -> 1-based line -> kind
    std::vector<ValueWatchItem> valueWatches_; // break when these change
    struct LiveHit {
        std::string text;
        long hits = 0;
    };
    std::map<std::pair<std::filesystem::path, int>, LiveHit> liveHits_;
    std::set<std::pair<std::filesystem::path, int>> liveDirty_; // not yet on screen
    sigc::connection liveFlush_;
    ProjectStateStore projectState_{ProjectStateStore::defaultFile()};
    EditorTab* execTab_ = nullptr;
    std::vector<StackFrame> frames_;
    unsigned stopGeneration_ = 0; // results from an earlier pause are ignored
    // Rows the user expanded in Locals/Watch; kept across steps (and debug sessions).
    ExpandedRows expandedRows_ = std::make_shared<std::set<std::string>>();

    // Last member: destroyed first, so floating panel windows are emptied while the panels exist.
    std::unique_ptr<DockManager> docks_;
};

} // namespace kevc
