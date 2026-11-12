// The Boilerplate menu: starters and patterns kept as files in folders (data/boilerplate, and the
// user's ~/.config/kev-cpp/boilerplate), shown as nested submenus and inserted into the editor.
// core/Boilerplate reads the folders and prepares the text; this file is the menu and the editor side.
#include "app/MainWindow.h"

#include "core/Packages.h"
#include "core/ProjectKinds.h"
#include "core/Settings.h"
#include "editor/EditorTab.h"
#include "process/FileManager.h"
#include "ui/Dialogs.h"

#include <cstdlib>

namespace fs = std::filesystem;

namespace kevc {

struct MainWindow::BoilerplateRequest {
    boilerplate::Snippet snippet;
    fs::path file;         // the snippet's own file: its extension says which files it fits
    std::string title;     // "C/C++ → SFML → Window"
    boilerplate::Values values; // the answers to its `ask:` lines
    fs::path openFile;          // the file open when it was chosen: {{file}}, even for a new file
};

namespace {

// The boilerplate that comes with Kev-C++: $KEVC_BOILERPLATE_DIR (tests), the install location next to
// the binary, or the source tree for development builds.
fs::path builtInBoilerplateDir() {
    if (const char* env = std::getenv("KEVC_BOILERPLATE_DIR")) return env;
    std::error_code ec;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        const fs::path installed = exe.parent_path().parent_path() / "share/kev-cpp/boilerplate";
        if (fs::is_directory(installed, ec)) return installed;
    }
    return fs::path(KEVC_SOURCE_DIR) / "data/boilerplate";
}

std::string join(const std::vector<std::string>& parts, const std::string& separator) {
    std::string out;
    for (const auto& p : parts) out += (out.empty() ? "" : separator) + p;
    return out;
}

// A folder's entries as menu sections: a new section (a separator) wherever the section name changes.
void fillMenu(const Glib::RefPtr<Gio::Menu>& menu, const boilerplate::Entry& folder) {
    Glib::RefPtr<Gio::Menu> section;
    const std::string* current = nullptr;
    for (const auto& entry : folder.children) {
        if (!current || *current != entry.section) {
            section = Gio::Menu::create();
            menu->append_section(section);
            current = &entry.section;
        }
        if (entry.folder) {
            auto submenu = Gio::Menu::create();
            fillMenu(submenu, entry);
            section->append_submenu(boilerplate::menuLabel(entry.label), submenu);
        } else {
            auto item = Gio::MenuItem::create(boilerplate::menuLabel(entry.label), "");
            item->set_action_and_target("win.boilerplate", Glib::Variant<Glib::ustring>::create(entry.key));
            section->append_item(item);
        }
    }
}

// Whether a menu of the menu bar (or any popover under `widget`) is showing.
bool anyPopoverOpen(Gtk::Widget& widget) {
    for (Gtk::Widget* child = widget.get_first_child(); child; child = child->get_next_sibling()) {
        if (GTK_IS_POPOVER(child->gobj()) && child->get_visible()) return true;
        if (anyPopoverOpen(*child)) return true;
    }
    return false;
}

// Closes the menus showing under `widget`; whether there were any.
bool closePopovers(Gtk::Widget& widget) {
    bool any = false;
    for (Gtk::Widget* child = widget.get_first_child(); child; child = child->get_next_sibling()) {
        if (GTK_IS_POPOVER(child->gobj()) && child->get_visible()) {
            gtk_popover_popdown(GTK_POPOVER(child->gobj()));
            any = true;
        }
        any = closePopovers(*child) || any;
    }
    return any;
}

std::string bufferText(GtkTextBuffer* buffer, const GtkTextIter* from, const GtkTextIter* to) {
    char* text = gtk_text_buffer_get_text(buffer, from, to, TRUE);
    std::string out = text;
    g_free(text);
    return out;
}

bool blankText(const std::string& text) {
    return text.find_first_not_of(" \t\r\n") == std::string::npos;
}

// Rejects absolute paths and anything that escapes `root`.
std::optional<fs::path> inside(const fs::path& root, const std::string& relative) {
    const fs::path rel = fs::path(relative).lexically_normal();
    if (relative.empty() || rel.is_absolute() || rel.empty() || *rel.begin() == "..") return std::nullopt;
    return root / rel;
}

} // namespace

std::vector<fs::path> MainWindow::boilerplateRoots() const {
    return {builtInBoilerplateDir(), userBoilerplateDir()};
}

fs::path MainWindow::userBoilerplateDir() const {
    return Settings::defaultFile().parent_path() / "boilerplate";
}

void MainWindow::refreshBoilerplate() {
    if (anyPopoverOpen(*menuBar_)) { // rebuilding would close it under the pointer
        refreshBoilerplateSoon();
        return;
    }
    watchBoilerplate();
    // SFML 2 and 3 are written differently (the event loop above all): only the installed one's.
    if (!sfmlMajor_) sfmlMajor_ = sfml::detect().major();
    boilerplate::Entry tree = boilerplate::forSfml(boilerplate::scan(boilerplateRoots()), sfmlMajor_);
    std::string sig = boilerplate::signature(tree);
    if (sig == boilerplateSignature_) return;
    boilerplate_ = std::move(tree);
    boilerplateSignature_ = std::move(sig);
    // The design holds the menu's last section (saving and the user's folder); the rest is made here.
    // The editors' right-click menus have the same tree under Insert Boilerplate.
    for (const auto& menu : {boilerplateMenu_, editorBoilerplateMenu_, cppEditorBoilerplateMenu_}) {
        menu->remove_all();
        fillMenu(menu, boilerplate_);
        menu->append_section(boilerplateToolsSection_);
    }
}

void MainWindow::refreshBoilerplateSoon() {
    // Saving a file is a burst of events (a temporary file, a rename): one rescan after them.
    boilerplateSoon_.disconnect();
    boilerplateSoon_ = Glib::signal_timeout().connect(sigc::track_obj(
                                                          [this] {
                                                              refreshBoilerplate();
                                                              return false;
                                                          },
                                                          *this),
                                                      400);
}

void MainWindow::afterMenuChoice(std::function<void()> then) {
    // GTK 4.14's menu bar often opens the menu left of Boilerplate as an item three menus deep closes
    // (the closing submenus seem to reach the bar as a move to the left). Nobody opens a menu that
    // quickly on purpose, so whatever opens in the first moments is closed again.
    const auto chosen = std::chrono::steady_clock::now();
    Glib::signal_timeout().connect(sigc::track_obj(
                                       [this, then, chosen] {
                                           const auto since = std::chrono::steady_clock::now() - chosen;
                                           const bool closed = closePopovers(*menuBar_);
                                           if ((closed || since < std::chrono::milliseconds(250)) &&
                                               since < std::chrono::seconds(2))
                                               return true;
                                           then();
                                           return false;
                                       },
                                       *this),
                                   50);
}

void MainWindow::watchBoilerplate() {
    // Every folder of both trees (a new one counts once its parent reports it), and for a tree that
    // doesn't exist yet (the user's, before Open My Boilerplate Folder), the folder it would be made in.
    std::vector<fs::path> dirs;
    std::error_code ec;
    for (const auto& root : boilerplateRoots()) {
        if (!fs::is_directory(root, ec)) {
            if (fs::is_directory(root.parent_path(), ec)) dirs.push_back(root.parent_path());
            continue;
        }
        dirs.push_back(root);
        for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = it->path().filename().string();
            if (it->is_directory(ec) && !name.empty() && name[0] != '.' && name[0] != '_') dirs.push_back(it->path());
            if (it.depth() > 8) it.disable_recursion_pending();
        }
    }
    boilerplateMonitors_.clear();
    for (const auto& dir : dirs) {
        try {
            auto monitor = Gio::File::create_for_path(dir.string())->monitor_directory();
            monitor->signal_changed().connect(
                [this](const Glib::RefPtr<Gio::File>&, const Glib::RefPtr<Gio::File>&, Gio::FileMonitor::Event) {
                    refreshBoilerplateSoon();
                });
            boilerplateMonitors_.push_back(monitor);
        } catch (const Glib::Error&) {
            // Unwatchable (permissions): its changes show after a restart.
        }
    }
}

void MainWindow::insertBoilerplate(const std::string& key) {
    const auto file = boilerplate::locate(boilerplateRoots(), key);
    if (!file) {
        refreshBoilerplate();
        setStatus("That boilerplate is gone: its file was moved or deleted");
        return;
    }
    auto request = std::make_shared<BoilerplateRequest>();
    try {
        request->snippet = boilerplate::parseSnippet(Glib::file_get_contents(file->string()));
    } catch (const Glib::Error& e) {
        showError(*this, "Could not read " + file->filename().string(), e.what());
        return;
    }
    request->file = *file;
    if (EditorTab* tab = currentEditor()) request->openFile = tab->path();
    const auto path = boilerplate::labelPath(boilerplate_, key);
    request->title = path.empty() ? file->stem().string() : join(path, " → ");
    askBoilerplateValues(request, 0);
}

void MainWindow::askBoilerplateValues(std::shared_ptr<BoilerplateRequest> request, size_t next) {
    const auto& asks = request->snippet.header.asks;
    if (next >= asks.size()) {
        placeBoilerplate(request);
        return;
    }
    const auto& [name, initial] = asks[next];
    promptText(*this, request->title, boilerplate::promptFor(name) + ":", initial,
               [this, token = std::weak_ptr<int>(lifetime_), request, next](const Glib::ustring& text) -> Glib::ustring {
                   std::string value = text;
                   value.erase(0, value.find_first_not_of(" \t"));
                   value.erase(value.find_last_not_of(" \t") + 1);
                   if (value.empty()) return "Type a value, or Cancel.";
                   request->values[request->snippet.header.asks[next].first] = value;
                   // The next question, or the insertion, after this box has gone.
                   Glib::signal_idle().connect_once([this, token, request, next] {
                       if (!token.expired()) askBoilerplateValues(request, next + 1);
                   });
                   return {};
               },
               -2);
}

void MainWindow::placeBoilerplate(std::shared_ptr<BoilerplateRequest> request) {
    EditorTab* tab = currentEditor();
    if (tab && boilerplate::fits(request->file, tab->path())) {
        GtkTextBuffer* buffer = GTK_TEXT_BUFFER(tab->buffer());
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        const bool blank = blankText(bufferText(buffer, &start, &end));
        if (!request->snippet.header.wholeFile || blank) {
            insertBoilerplateInto(tab, *request, blank && request->snippet.header.wholeFile);
            return;
        }
        // A whole file (a program, a component) into a file that already has text: the user says how.
        const std::string name = tab->path().filename().string();
        askChoice(*this, request->title + " is a whole file",
                  name + " already has text. Replace it with this (Ctrl+Z brings it back), insert this at the cursor, "
                         "or put it in a new file?",
                  {"Replace " + name, "Insert at the Cursor", "New File…", "Cancel"}, 3, 0,
                  [this, token = std::weak_ptr<int>(lifetime_), request, tab](int choice) {
                      if (token.expired() || choice == 3) return;
                      // Still open (the box was modal, but a file can be closed from outside)?
                      const auto open = editors();
                      if (std::find(open.begin(), open.end(), tab) == open.end()) return;
                      if (choice == 2) newFileWithBoilerplate(request, "");
                      else insertBoilerplateInto(tab, *request, choice == 0);
                  });
        return;
    }
    if (!folder_.empty() || tab) {
        std::string why;
        if (tab) {
            const std::string ext = request->file.extension().string();
            why = "This goes in a" + std::string(ext.empty() ? "nother" : " " + ext) + " file, not in " +
                  tab->path().filename().string() + ", so it goes into a new one.\n";
        }
        newFileWithBoilerplate(request, why);
        return;
    }
    // Nothing open to put it in.
    const auto ins = boilerplate::expand(request->snippet.body, "", "", "", boilerplateValues(*request), true);
    get_clipboard()->set_text(ins.text);
    setStatus("Copied " + request->title + " to the clipboard. Open a file to insert boilerplate at its cursor.");
}

boilerplate::Values MainWindow::boilerplateValues(const BoilerplateRequest& request) const {
    boilerplate::Values values = request.values;
    // The open file, also for text that goes into a new one: a test file is named after the code it
    // tests (`file: {{fileStem}}.test.ts`) and imports it.
    values["file"] = request.openFile.filename().string();
    values["fileStem"] = request.openFile.stem().string();
    values["project"] = !folder_.empty() ? folder_.filename().string() : request.openFile.parent_path().filename().string();
    const auto now = Glib::DateTime::create_now_local();
    values["date"] = now.format("%Y-%m-%d");
    values["year"] = now.format("%Y");
    return values;
}

void MainWindow::insertBoilerplateInto(EditorTab* tab, const BoilerplateRequest& request, bool replaceAll) {
    GtkTextBuffer* buffer = GTK_TEXT_BUFFER(tab->buffer());
    GtkTextIter start, end, first, last;
    gtk_text_buffer_get_bounds(buffer, &first, &last);
    const bool blank = blankText(bufferText(buffer, &first, &last));
    if (replaceAll) {
        start = first;
        end = last;
    } else {
        gtk_text_buffer_get_selection_bounds(buffer, &start, &end);
    }
    GtkTextIter lineStart = start;
    gtk_text_iter_set_line_offset(&lineStart, 0);
    GtkTextIter lineEnd = end;
    if (!gtk_text_iter_ends_line(&lineEnd)) gtk_text_iter_forward_to_line_end(&lineEnd);
    std::string prefix = bufferText(buffer, &lineStart, &start);
    const std::string selected = bufferText(buffer, &start, &end), rest = bufferText(buffer, &end, &lineEnd);
    // On a blank line with no indentation, a piece of code lines up with the code above it.
    if (!replaceAll && !request.snippet.header.wholeFile && prefix.empty() && selected.empty() && blankText(rest))
        prefix = boilerplate::indentForBlankLine(bufferText(buffer, &first, &lineStart));
    // A whole new text keeps its final newline; text inserted into other text doesn't bring one.
    const auto ins = boilerplate::expand(request.snippet.body, prefix, selected, rest, boilerplateValues(request),
                                         replaceAll || blank);
    if (ins.fromLineStart) start = lineStart;
    const int at = gtk_text_iter_get_offset(&start);
    gtk_text_buffer_begin_user_action(buffer);
    gtk_text_buffer_delete(buffer, &start, &end);
    gtk_text_buffer_insert(buffer, &start, ins.text.data(), int(ins.text.size()));
    const size_t cursorBytes = ins.cursor == std::string::npos ? ins.text.size() : ins.cursor;
    GtkTextIter cursor;
    gtk_text_buffer_get_iter_at_offset(buffer, &cursor, at + int(g_utf8_strlen(ins.text.data(), gssize(cursorBytes))));
    // The #includes the code needs, after the file's last one (usually above it): a mark keeps the cursor's place.
    GtkTextMark* place = gtk_text_buffer_create_mark(buffer, nullptr, &cursor, FALSE);
    gtk_text_buffer_get_bounds(buffer, &first, &last);
    const std::string all = bufferText(buffer, &first, &last);
    if (const auto includes = boilerplate::missingIncludes(all, request.snippet.header.includes)) {
        GtkTextIter there;
        gtk_text_buffer_get_iter_at_offset(buffer, &there, int(g_utf8_strlen(all.data(), gssize(includes->offset))));
        gtk_text_buffer_insert(buffer, &there, includes->text.data(), int(includes->text.size()));
    }
    gtk_text_buffer_end_user_action(buffer);
    gtk_text_buffer_get_iter_at_mark(buffer, &cursor, place);
    gtk_text_buffer_delete_mark(buffer, place);
    tab->placeCursor(cursor);
    boilerplateInserted(request, tab->path().filename().string());
}

void MainWindow::newFileWithBoilerplate(std::shared_ptr<BoilerplateRequest> request, const std::string& why) {
    EditorTab* tab = currentEditor();
    const fs::path base = !folder_.empty() ? folder_ : tab->path().parent_path();
    // `file:` with a folder in it is relative to the project; a bare name goes beside the open file.
    std::string suggestion = request->snippet.header.file.empty()
                                 ? request->file.filename().string()
                                 : boilerplate::substitute(request->snippet.header.file, boilerplateValues(*request));
    if (suggestion.find('/') == std::string::npos && tab) {
        const fs::path dir = tab->path().parent_path().lexically_relative(base);
        if (!dir.empty() && dir != "." && *dir.begin() != "..") suggestion = (dir / suggestion).generic_string();
    }
    promptText(*this, request->title, why + "New file, relative to " + base.filename().string() + ":", suggestion,
               [this, request, base](const Glib::ustring& text) -> Glib::ustring {
                   const auto target = inside(base, text);
                   if (!target) return "Enter a relative path inside " + base.filename().string() + ".";
                   std::error_code ec;
                   if (fs::exists(*target, ec)) return target->filename().string() + " already exists.";
                   auto ins = boilerplate::expand(request->snippet.body, "", "", "", boilerplateValues(*request), true);
                   if (const auto includes = boilerplate::missingIncludes(ins.text, request->snippet.header.includes)) {
                       ins.text.insert(includes->offset, includes->text);
                       if (ins.cursor != std::string::npos && ins.cursor >= includes->offset)
                           ins.cursor += includes->text.size();
                   }
                   try {
                       fs::create_directories(target->parent_path());
                       Glib::file_set_contents(target->string(), ins.text);
                   } catch (const std::exception& e) { // Glib::Error is one too
                       return std::string("Could not create the file: ") + e.what();
                   }
                   if (EditorTab* opened = openFile(*target)) {
                       const size_t cursorBytes = ins.cursor == std::string::npos ? 0 : ins.cursor;
                       GtkTextIter cursor;
                       gtk_text_buffer_get_iter_at_offset(GTK_TEXT_BUFFER(opened->buffer()), &cursor,
                                                          int(g_utf8_strlen(ins.text.data(), gssize(cursorBytes))));
                       opened->placeCursor(cursor);
                   }
                   filesChanged(); // the SourceWatcher adds a C/C++ source to the build file by itself
                   boilerplateInserted(*request, target->filename().string());
                   return {};
               });
}

void MainWindow::boilerplateInserted(const BoilerplateRequest& request, const std::string& where) {
    std::string message = "Inserted " + request.title + " into " + where;
    if (!request.snippet.header.description.empty()) message += ": " + request.snippet.header.description;
    const auto& modules = request.snippet.header.packages;
    if (!modules.empty()) {
        // What the code needs to build: added to a C/C++ project whose CMakeLists.txt Kev-C++ writes.
        // SFML goes through linkSfml, which picks pkg-config, SFML's CMake package or a download.
        const std::string needed = join(modules, ", ");
        if (!session_ || session_->project().kind == ProjectKind::WebTs) {
            message += ". It needs " + needed + " to build";
        } else if (!session_->project().buildFileManaged) {
            message += ". Link " + needed + " in the project's own " + session_->buildFile().filename().string();
        } else {
            std::vector<std::string> sfmlParts, added;
            for (const auto& module : modules) {
                if (module.starts_with("sfml-") && module.size() > 5) {
                    std::string part = module.substr(5);
                    part[0] = char(std::toupper(static_cast<unsigned char>(part[0])));
                    sfmlParts.push_back(part); // sfml-graphics → Graphics
                    continue;
                }
                const auto& packages = session_->project().packages;
                if (std::none_of(packages.begin(), packages.end(), [&](const Package& p) { return p.name == module; })) {
                    addSystemPackage(module);
                    added.push_back(module);
                }
            }
            if (!added.empty()) message += ". Added " + join(added, ", ") + " to the project's packages";
            if (!sfmlParts.empty()) message += linkSfml(sfmlParts);
        }
    }
    setStatus(message);
}

void MainWindow::saveSelectionAsBoilerplate() {
    EditorTab* tab = currentEditor();
    GtkTextIter start, end;
    if (!tab || !gtk_text_buffer_get_selection_bounds(GTK_TEXT_BUFFER(tab->buffer()), &start, &end)) {
        showInfo(*this, "Select some text first",
                 "Select the code to keep in an editor, then choose Boilerplate → Save Selection as Boilerplate. "
                 "It becomes an item in the Boilerplate menu.");
        return;
    }
    GtkTextBuffer* buffer = GTK_TEXT_BUFFER(tab->buffer());
    // From the start of the first line when only its indentation is left out, so it's removed evenly.
    GtkTextIter lineStart = start;
    gtk_text_iter_set_line_offset(&lineStart, 0);
    if (blankText(bufferText(buffer, &lineStart, &start))) start = lineStart;
    const std::string text = boilerplate::dedent(bufferText(buffer, &start, &end));
    const std::string extension = boilerplate::snippetExtension(tab->path());
    const fs::path root = userBoilerplateDir();
    promptText(*this, "Save Selection as Boilerplate",
               "Where it goes in the Boilerplate menu: folders and a name, separated by /",
               boilerplate::suggestedFolder(tab->path()) + "/New Snippet",
               [this, text, extension, root](const Glib::ustring& menuPath) -> Glib::ustring {
                   std::string error;
                   const auto target = boilerplate::savePath(root, std::string(menuPath), extension, error);
                   if (!target) return error;
                   std::error_code ec;
                   if (fs::exists(*target, ec)) return "There's already an item there. Choose another name.";
                   try {
                       fs::create_directories(target->parent_path());
                       Glib::file_set_contents(target->string(), text);
                   } catch (const std::exception& e) {
                       return std::string("Could not save it: ") + e.what();
                   }
                   refreshBoilerplate();
                   const auto path = boilerplate::labelPath(boilerplate_, target->lexically_relative(root).generic_string());
                   setStatus("Saved as Boilerplate → " + join(path, " → ") + " (" + target->string() + ")");
                   return {};
               });
}

void MainWindow::openBoilerplateFolder() {
    const fs::path dir = userBoilerplateDir();
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        showError(*this, "Could not make " + dir.string(), ec.message());
        return;
    }
    // How to write them, right where they go.
    fs::copy_file(builtInBoilerplateDir() / "_README.md", dir / "_README.md", fs::copy_options::skip_existing, ec);
    openInFileManager(dir, [this, token = std::weak_ptr<int>(lifetime_)](const std::string& message) {
        if (!token.expired()) showError(*this, "Could not open a file manager", message);
    });
    setStatus("Your boilerplate folder is " + dir.string() + ": files you put there show up in the Boilerplate menu");
}

} // namespace kevc
