#include "ui/TestsPanel.h"

#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace kevc {

namespace {

Glib::ustring esc(const std::string& s) {
    return Glib::Markup::escape_text(s);
}

} // namespace

TestsPanel::TestsPanel(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder)
    : TestsPanelUi(object, builder) {
    list_->signal_row_activated().connect([this](Gtk::ListBoxRow* row) {
        if (auto it = rows_.find(row); it != rows_.end()) open_.emit(it->second.file, it->second.line);
    });
    setRunners({});
}

void TestsPanel::on_vitest_clicked() {
    runAll_.emit(TestRunner::Vitest);
}

void TestsPanel::on_playwright_clicked() {
    runAll_.emit(TestRunner::Playwright);
}

void TestsPanel::on_failed_clicked() {
    runFailed_.emit();
}

void TestsPanel::clear() {
    rows_.clear();
    while (auto* row = list_->get_row_at_index(0)) list_->remove(*row);
}

void TestsPanel::setRunners(const std::vector<TestRunner>& runners) {
    auto has = [&](TestRunner r) { return std::find(runners.begin(), runners.end(), r) != runners.end(); };
    vitest_->set_visible(has(TestRunner::Vitest));
    playwright_->set_visible(has(TestRunner::Playwright));
    clear();
    summary_->set_text("");
    failed_->set_sensitive(false);
    message_->set_text(runners.empty() ? "No test runner installed. Add Vitest (npm i -D vitest) or Playwright "
                                         "(npm init playwright@latest) to run tests here."
                                       : "Press Run to run the tests.");
}

void TestsPanel::showMessage(const Glib::ustring& text) {
    clear();
    message_->set_text(text);
}

void TestsPanel::setBusy(bool busy) {
    vitest_->set_sensitive(!busy);
    playwright_->set_sensitive(!busy);
    if (busy) summary_->set_text("Running tests…");
}

void TestsPanel::setRun(const TestRun& run, const fs::path& root) {
    clear();
    if (!run.error.empty()) {
        summary_->set_text("");
        message_->set_text(run.error);
        failed_->set_sensitive(false);
        return;
    }
    message_->set_text("No tests ran.");
    summary_->set_markup("<span foreground=\"#7bd88f\">" + std::to_string(run.passed) + " passed</span>" +
                         (run.failed
                              ? "  ·  <span foreground=\"#ff6b6b\">" + std::to_string(run.failed) + " failed</span>"
                              : std::string()) +
                         (run.skipped ? "  ·  " + std::to_string(run.skipped) + " skipped" : std::string()));
    failed_->set_sensitive(run.failed > 0);

    fs::path currentFile;
    for (const auto& c : run.cases) {
        if (c.file != currentFile) {
            currentFile = c.file;
            auto* header = Gtk::make_managed<Gtk::Label>();
            header->set_markup("<b>" + esc(c.file.lexically_relative(root).generic_string()) + "</b>");
            header->set_xalign(0.0f);
            header->set_margin_start(6);
            header->set_margin_top(4);
            auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
            row->set_child(*header);
            row->set_activatable(false);
            list_->append(*row);
        }
        auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        box->set_margin_start(18);
        const char* mark = c.status == TestCase::Status::Passed   ? "<span foreground=\"#7bd88f\">✔</span>"
                           : c.status == TestCase::Status::Failed ? "<span foreground=\"#ff6b6b\">✘</span>"
                                                                  : "<span alpha=\"60%\">○</span>";
        std::ostringstream ms;
        ms << std::fixed << std::setprecision(0) << c.ms << " ms";
        Glib::ustring markup = Glib::ustring(mark) + "  " + esc(c.name);
        if (c.ms > 0) markup += "  <span alpha=\"55%\">" + ms.str() + "</span>";
        if (!c.message.empty()) {
            const std::string first = c.message.substr(0, c.message.find('\n'));
            markup += "\n<span alpha=\"75%\" font_family=\"monospace\">" + esc(first) + "</span>";
        }
        auto* label = Gtk::make_managed<Gtk::Label>();
        label->set_markup(markup);
        label->set_xalign(0.0f);
        label->set_hexpand(true);
        label->set_ellipsize(Pango::EllipsizeMode::END);
        label->set_tooltip_text(c.message.empty() ? c.name : c.message);
        auto* again = Gtk::make_managed<Gtk::Button>();
        again->set_icon_name("media-playback-start-symbolic");
        again->set_has_frame(false);
        again->set_tooltip_text("Run just this test");
        again->add_css_class("kevc-tab-menu");
        again->signal_clicked().connect([this, c] { runOne_.emit(c); });
        box->append(*label);
        box->append(*again);
        auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_child(*box);
        list_->append(*row);
        rows_[row] = c;
    }
}

} // namespace kevc
