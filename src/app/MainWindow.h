#pragma once

#include "build/BuildRunner.h"
#include "build/CompilerPanel.h"
#include "core/BuildPlan.h"
#include "core/Diagnostics.h"
#include "core/ProjectSession.h"
#include "debug/DebugController.h"
#include "debug/DebugViews.h"
#include "devserver/DevServer.h"
#include "editor/ProjectTree.h"
#include "editor/SymbolsPanel.h"
#include "lsp/LspManager.h"
#include "ui/DockManager.h"
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

    // Build & run
    BuildConfig currentConfig() const;
    BuildTools buildTools() const;
    // Builds the open project; `then` runs with the result unless the build could not start.
    // `config` overrides the toolbar's Debug/Release choice (debugging always builds Debug).
    void startBuild(BuildAction action, std::function<void(bool)> then = {},
                    std::optional<BuildConfig> config = std::nullopt);
    void onBuildLine(const std::string& line, bool isCommand);
    void onBuildFinished(bool success);
    void runProgram();
    void compileAndRun();
    void stopExecution();
    void appendLog(const std::string& text, const Glib::ustring& tag = {});
    void showBottomPage(Gtk::Widget& page);

    void showToolchainInfo();
    void showAbout();

    // TypeScript dev server
    Gtk::Widget& buildServerPage();
    void toggleDevServer();
    void startDevServer(bool openBrowserWhenReady);
    void openInBrowser();
    void updateServerPage();

    // Debugging
    void startDebugging();
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

    BuildRunner build_;
    std::unique_ptr<DiagnosticParser> parser_;
    std::function<void(bool)> afterBuild_;
    BuildAction buildAction_ = BuildAction::Build;
    std::chrono::steady_clock::time_point buildStart_;
    BuildConfig buildConfig_ = BuildConfig::Debug;

    std::unique_ptr<DevServer> devServer_;
    bool openBrowserWhenReady_ = false;
    Gtk::Box serverPage_{Gtk::Orientation::VERTICAL};
    Gtk::Label serverStatus_;
    Gtk::Button serverOpen_{"Open in Browser"};
    Gtk::Button serverToggle_{"Start"};
    Gtk::TextView serverLog_;

    std::shared_ptr<DebugController> debugger_;
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
