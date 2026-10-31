#pragma once

#include "build/BuildRunner.h"
#include "build/CompilerPanel.h"
#include "core/BuildPlan.h"
#include "core/Diagnostics.h"
#include "core/ProjectSession.h"
#include "core/Version.h"
#include "core/WebProject.h"
#include "debug/DebugController.h"
#include "debug/DebugViews.h"
#include "debug/JsDebugger.h"
#include "devserver/DevServer.h"
#include "editor/ProjectTree.h"
#include "editor/RoutesPanel.h"
#include "editor/SymbolsPanel.h"
#include "lsp/LspManager.h"
#include "ui/DockManager.h"
#include "ui/DependenciesPanel.h"
#include "ui/EnvPanel.h"
#include "ui/TestsPanel.h"
#include "ui/TracesPanel.h"
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
class EditorTab;
class SourceWatcher;

class MainWindow : public Gtk::ApplicationWindow {
public:
    explicit MainWindow(Application& app);
    ~MainWindow() override;

    void setStatus(const Glib::ustring& message);
    // Opens a project folder, a .kevc file, or a single file (from the command line).
    void openPath(const std::filesystem::path& path);

private:
    // UI construction
    void buildActions();
    Glib::RefPtr<Gio::Menu> buildMenuModel();
    Gtk::Widget& buildToolbar();
    Gtk::Widget& buildLeftDock();
    Gtk::Widget& buildEditorArea();
    Gtk::Widget& buildBottomDock();
    Gtk::Widget& buildStatusBar();
    // Registers a win.<name> action that only reports it is not implemented yet.
    void addStubAction(const Glib::ustring& name, const Glib::ustring& label);
    // Shows the toolbar groups and panels the current profile (what's open) wants; see
    // core/ProjectKinds. Called whenever a project or folder is opened.
    void applyProfile();
    void updateToolbarRows();

    // Projects
    void newProject();
    void chooseFolder();
    void openFolder(const std::filesystem::path& root);
    void setProject(std::unique_ptr<ProjectSession> session);
    void setPlainFolder(const std::filesystem::path& root);
    void onProjectSynced(const ProjectSession::SyncResult& result);
    void updateTitle();

    // Editors
    EditorTab* currentEditor();
    std::vector<EditorTab*> editors();
    EditorTab* openFile(const std::filesystem::path& file, int line = 0, int column = 0);
    void newFile();
    void saveCurrent();
    bool saveAll();
    void closeEditor(EditorTab* tab);
    // Closes every editor, asking about unsaved changes first; runs `then` unless cancelled.
    void closeAllEditors(std::function<void()> then);
    void removeEditor(EditorTab* tab);
    void forwardToEditor(const char* action);

    // Search
    void showFindBar();
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

    void showToolchainInfo();
    // Help → Check for Updates: the latest GitHub release, downloaded and installed over this one.
    void checkForUpdates();
    void installUpdate(const Release& release);
    void showAbout();

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
    void onBreakpointToggled(EditorTab* tab, int line, bool on);
    void syncBreakpoints(EditorTab* tab);
    void updateBreakpointsPanel();
    void addWatch();
    bool debugHover(EditorTab* tab, const GtkTextIter& at, std::function<void(GtkWidget*)> show);

    // IntelliSense
    void setupIntelliSense(EditorTab* tab);
    void gotoDefinition(EditorTab* tab, const GtkTextIter& at);
    void refreshSymbols();
    void scheduleSymbolsRefresh();
    void showProjectOptions(bool packagesPage);

    Application& app_;
    // Async callbacks (subprocesses) hold a weak_ptr to this and do nothing once the window is gone.
    std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
    std::filesystem::path toolsDir_; // bundled Node tools: language server, tsc, dev server
    // Declared before the widgets so it outlives the editors during teardown.
    std::unique_ptr<LspManager> lsp_;
    std::unique_ptr<ProjectSession> session_;
    std::unique_ptr<SourceWatcher> watcher_;
    std::filesystem::path folder_; // project root or plain folder; empty if nothing is open
    std::filesystem::path lspRootForLooseFiles_;
    bool closeConfirmed_ = false;

    Gtk::Box root_{Gtk::Orientation::VERTICAL};
    Gtk::Paned hpaned_{Gtk::Orientation::HORIZONTAL};
    Gtk::Paned vpaned_{Gtk::Orientation::VERTICAL};
    Gtk::Paned rightPaned_{Gtk::Orientation::HORIZONTAL}; // editor + bottom dock | right dock
    Gtk::Notebook rightDock_;
    Gtk::Notebook leftDock_;
    ProjectTree projectTree_;
    SymbolsPanel symbols_;
    RoutesPanel routesPanel_;
    EnvPanel envPanel_;
    sigc::connection kitRefresh_;
    std::string pendingRoute_; // opened once the dev server is ready
    TestsPanel testsPanel_;
    TracesPanel tracesPanel_;
    DependenciesPanel depsPanel_;
    TraceStore traces_;
    std::unique_ptr<ManagedProcess> collector_;
    int collectorPort_ = 0;
    TestRunner lastRunner_ = TestRunner::Vitest;
    std::vector<TestCase> lastFailed_;
    sigc::connection symbolsTimer_;
    Gtk::Box editorArea_{Gtk::Orientation::VERTICAL};
    Gtk::Notebook editors_;
    Gtk::Widget* welcomePage_ = nullptr;
    Gtk::SearchBar findBar_;
    Gtk::SearchEntry findEntry_;
    Gtk::CheckButton findCase_{"Match case"};
    Gtk::Label findCount_;
    Gtk::Notebook bottomDock_;
    Gtk::TextView compileLog_;
    Glib::RefPtr<Gtk::TextMark> logEnd_;
    CompilerPanel compilerPanel_;
    TerminalPane runConsole_;
    TerminalPane shell_;
    Gtk::DropDown* configDrop_ = nullptr;
    std::map<Task, TaskButton*> taskButtons_;
    // Named groups of toolbar buttons that View → Toolbars shows or hides per profile.
    struct ToolbarGroup {
        std::string id;
        Gtk::Box* box;
        Gtk::Separator* separator; // before the group, hidden when it's the first one in its row
        size_t row;
    };
    std::vector<ToolbarGroup> toolbarGroups_;
    std::vector<Gtk::Box*> toolbarRows_;
    std::vector<Gtk::Separator*> toolbarRules_; // the line above each row after the first
    // View → Toolbars / Panels check items, by group or panel id.
    std::map<std::string, Glib::RefPtr<Gio::SimpleAction>> toolbarActions_;
    std::map<std::string, Glib::RefPtr<Gio::SimpleAction>> panelActions_;
    std::string profile_ = "none";
    TaskButton* continueButton_ = nullptr; // Continue while paused, Pause while the program runs
    TaskButton* devServerButton_ = nullptr;
    std::vector<Gtk::Button*> stepButtons_; // only while paused in the debugger

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
    Glib::RefPtr<Gio::Menu> scriptsMenu_ = Gio::Menu::create(); // Project → Scripts and the web toolbar
    std::string doneLabel_;       // see runCommands
    bool openBrowserWhenReady_ = false;
    Gtk::Box serverPage_{Gtk::Orientation::VERTICAL};
    Gtk::Label serverStatus_;
    Gtk::Button serverOpen_{"Open in Browser"};
    TaskButton serverToggle_{"kevc-globe", "Start the dev server", "kevc-stop", "Stop the dev server", 16};
    Gtk::TextView serverLog_;

    std::shared_ptr<DebugController> debugger_;
    std::shared_ptr<JsDebugger> jsDebugger_;
    std::vector<dap::Frame> jsFrames_;
    std::string jsServerUrl_; // the Vite the JS debugger runs, once it's serving
    DebugPanel debugPanel_;
    WatchPanel watches_;
    std::map<std::filesystem::path, std::set<int>> breakpoints_; // file -> 1-based lines
    EditorTab* execTab_ = nullptr;
    std::vector<StackFrame> frames_;
    unsigned stopGeneration_ = 0; // results from an earlier pause are ignored
    // Rows the user expanded in Locals/Watch; kept across steps (and debug sessions).
    ExpandedRows expandedRows_ = std::make_shared<std::set<std::string>>();

    // Last member: destroyed first, so floating panel windows are emptied while the panels exist.
    std::unique_ptr<DockManager> docks_;
    Gtk::Label status_;
};

} // namespace kevc
