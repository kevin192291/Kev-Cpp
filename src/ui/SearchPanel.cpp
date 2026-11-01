#include "ui/SearchPanel.h"

#include "core/Icons.h"
#include "ui/Dialogs.h"
#include "ui/SearchMarkup.h"

#include <chrono>

namespace fs = std::filesystem;

namespace kevc {

namespace {

// Enough to see where something is used; past that, narrowing the search beats scrolling.
constexpr size_t kMaxMatches = 2000;

Glib::ustring esc(std::string_view s) {
    return Glib::Markup::escape_text(std::string(s));
}

std::string thousands(size_t n) {
    std::string digits = std::to_string(n), out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return out;
}

} // namespace

SearchPanel::SearchPanel() : Gtk::Box(Gtk::Orientation::VERTICAL, 4) {
    set_margin_top(4);

    auto* top = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 2);
    top->set_margin_start(4);
    top->set_margin_end(4);
    query_.set_placeholder_text("Find in files (Ctrl+Shift+F)");
    query_.set_hexpand(true);
    query_.set_search_delay(250);
    query_.signal_search_changed().connect(sigc::mem_fun(*this, &SearchPanel::search));
    query_.signal_activate().connect(sigc::mem_fun(*this, &SearchPanel::search)); // Enter: again, now
    query_.signal_stop_search().connect([this] { done_.emit(); });
    // Down goes from the field into the results.
    auto keys = Gtk::EventControllerKey::create();
    keys->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    keys->signal_key_pressed().connect(
        [this](guint keyval, guint, Gdk::ModifierType) {
            if ((keyval != GDK_KEY_Down && keyval != GDK_KEY_KP_Down) || files_->get_n_items() == 0) return false;
            selection_->set_selected(1); // the first match, under the first file
            list_.grab_focus();
            return true;
        },
        false);
    query_.add_controller(keys);
    top->append(query_);
    setupOption(matchCase_, "Aa", "Match case");
    setupOption(wholeWord_, "<u>ab</u>", "Whole words only");
    setupOption(regex_, ".*", "Regular expression (ECMAScript, one line at a time)");
    top->append(matchCase_);
    top->append(wholeWord_);
    top->append(regex_);
    append(*top);

    // Replace: always there, under the search field, so it's found without looking for it.
    auto* replaceRow = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    replaceRow->set_margin_start(4);
    replaceRow->set_margin_end(4);
    replace_.set_placeholder_text("Replace with…");
    replace_.set_tooltip_text("What every match becomes when you press Replace All. With .* on, $1 inserts the "
                              "first ( ) group of the match");
    replace_.set_hexpand(true);
    replace_.signal_activate().connect(sigc::mem_fun(*this, &SearchPanel::replaceAll));
    replaceAll_.set_tooltip_text("Replace every match in the list (asks first)");
    replaceAll_.set_sensitive(false);
    replaceAll_.signal_clicked().connect(sigc::mem_fun(*this, &SearchPanel::replaceAll));
    replaceRow->append(replace_);
    replaceRow->append(replaceAll_);
    append(*replaceRow);

    include_.set_placeholder_text("Files to include: *.cpp, src/, !tests/");
    include_.set_tooltip_text("Comma-separated: *.h matches file names, src/ a folder, src/*.h paths; "
                              "a leading ! leaves files out");
    include_.set_margin_start(4);
    include_.set_margin_end(4);
    include_.signal_changed().connect([this] {
        includeTimer_.disconnect();
        includeTimer_ = Glib::signal_timeout().connect(
            [this] {
                search();
                return false;
            },
            400);
    });
    include_.signal_activate().connect(sigc::mem_fun(*this, &SearchPanel::search));
    append(include_);

    summary_.add_css_class("dim-label");
    summary_.set_xalign(0.0f);
    summary_.set_wrap(true);
    summary_.set_margin_start(6);
    summary_.set_margin_end(4);
    append(summary_);

    files_ = Gio::ListStore<SearchItem>::create();
    tree_ = Gtk::TreeListModel::create(
        files_,
        [](const Glib::RefPtr<Glib::ObjectBase>& item) -> Glib::RefPtr<Gio::ListModel> {
            auto it = std::dynamic_pointer_cast<SearchItem>(item);
            if (it && it->children) return it->children;
            return {};
        },
        false, true); // every file opens to show its matches
    selection_ = Gtk::SingleSelection::create(tree_);
    selection_->set_autoselect(false);
    auto factory = Gtk::SignalListItemFactory::create();
    factory->signal_setup().connect(sigc::mem_fun(*this, &SearchPanel::onSetupItem));
    factory->signal_bind().connect(sigc::mem_fun(*this, &SearchPanel::onBindItem));
    list_.set_factory(factory);
    list_.set_model(selection_);
    list_.set_single_click_activate(true);
    list_.signal_activate().connect(sigc::mem_fun(*this, &SearchPanel::onActivate));
    list_.add_css_class("kevc-search-results");
    scroller_.set_child(list_);
    scroller_.set_vexpand(true);
    append(scroller_);

    setRoot({});
}

SearchPanel::~SearchPanel() {
    includeTimer_.disconnect();
    job_.cancel();
}

void SearchPanel::setupOption(Gtk::ToggleButton& button, const char* markup, const char* tooltip) {
    auto* label = Gtk::make_managed<Gtk::Label>();
    label->set_markup(markup);
    button.set_child(*label);
    button.set_tooltip_text(tooltip);
    button.set_has_frame(false);
    button.set_focus_on_click(false);
    button.set_valign(Gtk::Align::CENTER);
    button.add_css_class("kevc-search-option");
    button.signal_toggled().connect(sigc::mem_fun(*this, &SearchPanel::search));
}

void SearchPanel::setRoot(const fs::path& root) {
    root_ = root;
    job_.cancel();
    clearResults();
    replaceResults_ = false;
    if (root_.empty()) summary_.set_text("Open a project or folder to search its files.");
    else if (!query_.get_text().empty()) search();
    else summary_.set_text("");
}

void SearchPanel::focusSearch(const Glib::ustring& text) {
    if (!text.empty() && text != query_.get_text()) {
        query_.set_text(text);
        search(); // now, not after the typing delay
    }
    query_.grab_focus();
    query_.select_region(0, -1);
}

void SearchPanel::clearResults() {
    files_->remove_all();
    shownFiles_ = shownMatches_ = 0;
    replaceAll_.set_sensitive(false);
}

std::shared_ptr<const search::Matcher> SearchPanel::matcher(const std::string& text) const {
    return std::make_shared<const search::Matcher>(
        search::TextQuery{text, matchCase_.get_active(), wholeWord_.get_active(), regex_.get_active()});
}

void SearchPanel::replaceAll() {
    const std::string text = query_.get_text(), with = replace_.get_text();
    auto* window = dynamic_cast<Gtk::Window*>(get_root());
    if (text.empty() || root_.empty() || shownMatches_ == 0 || !window) return;
    const Glib::ustring what = shownMatches_ == 1 ? "1 match" : thousands(shownMatches_) + " matches";
    const Glib::ustring where = shownFiles_ == 1 ? "1 file" : thousands(shownFiles_) + " files";
    askChoice(*window, "Replace " + what + " in " + where + "?",
              "\u201c" + text + "\u201d becomes \u201c" + with +
                  "\u201d. Files open with unsaved changes change in their editor (save them to keep it); the "
                  "others are saved now. Undo with Ctrl+Z in an editor, or with git.",
              {"Cancel", "Replace All"}, 0, 1, [this, text, with](int choice) {
                  if (choice == 1) doReplaceAll(text, with);
              });
}

void SearchPanel::doReplaceAll(const std::string& text, const std::string& with) {
    const auto m = matcher(text);
    if (!m->valid()) return;
    const search::PathFilter filter{std::string(include_.get_text())};
    auto unsaved = unsaved_ ? unsaved_() : std::map<fs::path, std::string>{};
    // The files in the list (what the user saw), each searched again as it is now.
    std::vector<std::string> files;
    for (guint i = 0; i < files_->get_n_items(); ++i) files.push_back(files_->get_item(i)->rel);
    size_t total = 0, changedFiles = 0;
    std::vector<std::string> failed;
    for (const auto& rel : files) {
        std::error_code ec;
        const fs::path file = fs::weakly_canonical(root_ / rel, ec);
        size_t n = 0;
        if (auto it = unsaved.find(file); it != unsaved.end()) {
            std::string replaced = m->replace(it->second, with, &n);
            if (n) replaceInEditor_.emit(file, replaced);
        } else {
            try {
                const std::string replaced = m->replace(Glib::file_get_contents(file.string()), with, &n);
                if (n) Glib::file_set_contents(file.string(), replaced); // an open editor reloads it
            } catch (const Glib::Error&) {
                failed.push_back(rel);
                n = 0;
            }
        }
        total += n;
        if (n) ++changedFiles;
    }
    clearResults();
    replaceResults_ = false;
    Glib::ustring summary = "Replaced " + thousands(total) + (total == 1 ? " match in " : " matches in ") +
                            thousands(changedFiles) + (changedFiles == 1 ? " file." : " files.");
    if (!failed.empty()) summary += " Couldn't change " + failed.front() + (failed.size() > 1 ? " and others." : ".");
    summary_.set_text(summary);
}

void SearchPanel::search() {
    includeTimer_.disconnect();
    const std::string text = query_.get_text();
    auto stop = [this](const Glib::ustring& summary) {
        job_.cancel();
        clearResults();
        replaceResults_ = false;
        summary_.set_text(summary);
    };
    if (root_.empty()) return stop("Open a project or folder to search its files.");
    if (text.empty()) return stop("");
    auto matcher = this->matcher(text);
    if (!matcher->valid()) return stop(matcher->error());
    auto filter = std::make_shared<const search::PathFilter>(std::string(include_.get_text()));

    // Open editors as they are now, so line numbers match what's on screen.
    std::map<std::string, std::string> unsaved;
    if (unsaved_) {
        std::error_code ec;
        const fs::path root = fs::weakly_canonical(root_, ec);
        for (auto& [path, contents] : unsaved_()) {
            const fs::path rel = path.lexically_relative(root);
            if (!rel.empty() && *rel.begin() != "..") unsaved[rel.generic_string()] = std::move(contents);
        }
    }

    replaceResults_ = true;
    summary_.set_text("Searching…");
    job_.start([this, root = root_, matcher, filter, unsaved = std::move(unsaved)](
                   const std::atomic<bool>& cancelled, const BackgroundJob::Post& post) {
        const auto files = search::listFiles(root, 200000, &cancelled);
        std::vector<search::FileResult> batch;
        auto sent = std::chrono::steady_clock::now();
        const auto totals = search::searchFiles(
            root, files, *matcher, *filter, unsaved, kMaxMatches, &cancelled, [&](search::FileResult&& result) {
                batch.push_back(std::move(result));
                // Results show as they're found, a few times a second rather than file by file.
                if (std::chrono::steady_clock::now() - sent > std::chrono::milliseconds(150)) {
                    post([this, b = std::move(batch)]() mutable { addResults(std::move(b)); });
                    batch.clear();
                    sent = std::chrono::steady_clock::now();
                }
            });
        post([this, b = std::move(batch), totals]() mutable {
            addResults(std::move(b));
            finish(totals);
        });
    });
}

void SearchPanel::addResults(std::vector<search::FileResult> results) {
    if (results.empty()) return;
    if (replaceResults_) {
        clearResults();
        replaceResults_ = false;
    }
    std::vector<Glib::RefPtr<SearchItem>> rows;
    for (auto& result : results) {
        auto file = SearchItem::create();
        file->rel = result.path;
        file->count = result.matches.size();
        file->children = Gio::ListStore<SearchItem>::create();
        std::vector<Glib::RefPtr<SearchItem>> matches;
        for (auto& m : result.matches) {
            auto item = SearchItem::create();
            item->rel = result.path;
            item->match = std::move(m);
            matches.push_back(item);
        }
        file->children->splice(0, 0, matches);
        rows.push_back(file);
        ++shownFiles_;
        shownMatches_ += file->count;
    }
    files_->splice(files_->get_n_items(), 0, rows);
    summary_.set_text("Searching… " + thousands(shownMatches_) + (shownMatches_ == 1 ? " result" : " results") +
                      " so far");
}

void SearchPanel::finish(const search::Totals& totals) {
    if (replaceResults_) { // nothing found: now the old results go
        clearResults();
        replaceResults_ = false;
    }
    if (shownMatches_ == 0) {
        summary_.set_text("No results in " + thousands(totals.files) + (totals.files == 1 ? " file." : " files."));
        return;
    }
    replaceAll_.set_sensitive(true);
    std::string text = thousands(shownMatches_) + (shownMatches_ == 1 ? " result in " : " results in ") +
                       thousands(shownFiles_) + (shownFiles_ == 1 ? " file" : " files");
    if (totals.truncated) text += ". Stopped at " + thousands(kMaxMatches) + ": narrow the search to see the rest";
    summary_.set_text(text + ".");
}

void SearchPanel::onSetupItem(const Glib::RefPtr<Gtk::ListItem>& item) {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    box->append(*Gtk::make_managed<Gtk::Image>());
    auto* text = Gtk::make_managed<Gtk::Label>();
    text->set_xalign(0.0f);
    text->set_hexpand(true);
    text->set_ellipsize(Pango::EllipsizeMode::END);
    box->append(*text);
    auto* count = Gtk::make_managed<Gtk::Label>();
    count->add_css_class("dim-label");
    count->set_margin_end(4);
    box->append(*count);
    auto* expander = Gtk::make_managed<Gtk::TreeExpander>();
    expander->set_child(*box);
    item->set_child(*expander);
}

void SearchPanel::onBindItem(const Glib::RefPtr<Gtk::ListItem>& item) {
    auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(item->get_item());
    auto* expander = dynamic_cast<Gtk::TreeExpander*>(item->get_child());
    if (!row || !expander) return;
    expander->set_list_row(row);
    auto it = std::dynamic_pointer_cast<SearchItem>(row->get_item());
    auto* box = dynamic_cast<Gtk::Box*>(expander->get_child());
    if (!it || !box) return;
    auto* image = dynamic_cast<Gtk::Image*>(box->get_first_child());
    auto* text = dynamic_cast<Gtk::Label*>(image->get_next_sibling());
    auto* count = dynamic_cast<Gtk::Label*>(text->get_next_sibling());

    const fs::path rel = it->rel;
    if (!it->match) { // a file: its name, the folder dimmed, how many matches
        image->set_visible(true);
        image->set_from_icon_name(icons::forFile(rel.filename()));
        const std::string folder = rel.parent_path().generic_string();
        text->set_markup(esc(rel.filename().string()) +
                         (folder.empty() ? Glib::ustring() : "  <span alpha=\"60%\">" + esc(folder) + "</span>"));
        count->set_text(std::to_string(it->count));
        count->set_visible(true);
        expander->set_tooltip_text(it->rel);
        return;
    }
    // A match: its line number, then the line with the match highlighted as the editor shows them.
    image->set_visible(false);
    count->set_visible(false);
    text->set_markup(matchMarkup(*it->match));
    expander->set_tooltip_text(it->rel + ":" + std::to_string(it->match->line));
}

void SearchPanel::onActivate(guint position) {
    auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(selection_->get_object(position));
    auto it = row ? std::dynamic_pointer_cast<SearchItem>(row->get_item()) : nullptr;
    if (!it) return;
    if (!it->match) {
        row->set_expanded(!row->get_expanded());
        return;
    }
    open_.emit(root_ / it->rel, it->match->line, it->match->column, it->match->length);
}

} // namespace kevc
