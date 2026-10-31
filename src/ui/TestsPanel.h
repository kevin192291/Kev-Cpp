#pragma once

#include "core/TestReport.h"

#include <gtkmm.h>

#include <filesystem>
#include <map>
#include <optional>
#include <vector>

namespace kevc {

// The Tests panel for web projects: run Vitest or Playwright, see each test's result grouped by
// file, run one test again, jump to where a test failed.
class TestsPanel : public Gtk::Box {
public:
    TestsPanel();

    // The runners the open project has installed (none hides the Run buttons).
    void setRunners(const std::vector<TestRunner>& runners);
    void setRun(const TestRun& run, const std::filesystem::path& root);
    void showMessage(const Glib::ustring& text);
    void setBusy(bool busy);

    sigc::signal<void(TestRunner)>& signalRunAll() { return runAll_; }
    sigc::signal<void()>& signalRunFailed() { return runFailed_; }
    sigc::signal<void(const TestCase&)>& signalRunOne() { return runOne_; }
    sigc::signal<void(const std::filesystem::path&, int)>& signalOpen() { return open_; }

private:
    void clear();

    Gtk::Label summary_;
    Gtk::Button vitest_{"Run Vitest"};
    Gtk::Button playwright_{"Run Playwright"};
    Gtk::Button failed_{"Re-run Failed"};
    Gtk::ScrolledWindow scroller_;
    Gtk::ListBox list_;
    Gtk::Label message_;
    std::map<Gtk::ListBoxRow*, TestCase> rows_;
    sigc::signal<void(TestRunner)> runAll_;
    sigc::signal<void()> runFailed_;
    sigc::signal<void(const TestCase&)> runOne_;
    sigc::signal<void(const std::filesystem::path&, int)> open_;
};

} // namespace kevc
