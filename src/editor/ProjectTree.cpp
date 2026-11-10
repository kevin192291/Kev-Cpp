#include "editor/ProjectTree.h"

#include "core/FileOps.h"
#include "core/Icons.h"
#include "core/ProjectSession.h"
#include "core/Search.h"
#include "ui/SearchMarkup.h"

#include <algorithm>
#include <cstring>

namespace fs = std::filesystem;

namespace kevc {

namespace {

constexpr const char* kAttributes = "standard::name,standard::display-name,standard::type";

gboolean keepEntry(gpointer item, gpointer) {
    GFileInfo* info = G_FILE_INFO(item);
    const std::string name = g_file_info_get_name(info);
    if (g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY) return !isIgnoredDirName(name);
    return !search::isHiddenFileName(name);
}

int compareEntries(gconstpointer a, gconstpointer b, gpointer) {
    GFileInfo* ia = G_FILE_INFO(const_cast<gpointer>(a));
    GFileInfo* ib = G_FILE_INFO(const_cast<gpointer>(b));
    const bool da = g_file_info_get_file_type(ia) == G_FILE_TYPE_DIRECTORY;
    const bool db = g_file_info_get_file_type(ib) == G_FILE_TYPE_DIRECTORY;
    if (da != db) return da ? GTK_ORDERING_SMALLER : GTK_ORDERING_LARGER;
    const int c = g_ascii_strcasecmp(g_file_info_get_name(ia), g_file_info_get_name(ib));
    return c < 0 ? GTK_ORDERING_SMALLER : (c > 0 ? GTK_ORDERING_LARGER : GTK_ORDERING_EQUAL);
}

GFile* fileOf(GFileInfo* info) {
    return G_FILE(g_file_info_get_attribute_object(info, "standard::file"));
}

GFile* fileOf(const Glib::RefPtr<Gio::FileInfo>& info) {
    return fileOf(info->gobj());
}

std::string pathOf(GFileInfo* info) {
    std::string out;
    if (GFile* file = fileOf(info))
        if (char* path = g_file_get_path(file)) {
            out = path;
            g_free(path);
        }
    return out;
}

Glib::RefPtr<Gio::FileInfo> infoOf(Gtk::ListItem* item) {
    auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(item->get_item());
    return row ? std::dynamic_pointer_cast<Gio::FileInfo>(row->get_item()) : nullptr;
}

// A folder's entries, kept up to date: the GFileInfos of what's in it, each with its standard::file,
// as GtkDirectoryList lists them. That stops following its folder for good once a file it was told
// about is gone before it reads it (GTK 4.14.5, and GTK's main branch in November 2026: it applies
// changes in order and waits for that one forever), and saving does exactly that: g_file_set_contents writes a temporary file
// and renames it over the file. So after a save, new files in that folder (File → New Source File)
// didn't show until the folder was closed and opened again. This one skips what's already gone.
class FolderListing {
public:
    // The listing lives as long as the list it fills.
    static Glib::RefPtr<Gio::ListModel> create(const Glib::RefPtr<Gio::File>& dir) {
        GListStore* store = g_list_store_new(G_TYPE_FILE_INFO);
        g_object_set_data_full(G_OBJECT(store), "kevc-folder-listing", new FolderListing(store, dir),
                               [](gpointer listing) { delete static_cast<FolderListing*>(listing); });
        return Glib::wrap(G_LIST_MODEL(store));
    }

    ~FolderListing() {
        loading_.disconnect();
        changed_.disconnect();
        if (monitor_) monitor_->cancel();
    }

private:
    FolderListing(GListStore* store, Glib::RefPtr<Gio::File> dir) : store_(store), dir_(std::move(dir)) {
        // Not straight away: GtkTreeListModel also makes a folder's list just to see whether it can be
        // expanded, and drops it at once. Before the next frame, so an opened folder never shows empty.
        loading_ = Glib::signal_idle().connect(
            [this] {
                load();
                return false;
            },
            Glib::PRIORITY_HIGH_IDLE);
    }

    void load() {
        // Watched first, so nothing made while the folder is read is missed (what's listed twice is
        // found the second time).
        try {
            monitor_ = dir_->monitor_directory(Gio::FileMonitorFlags::WATCH_MOVES);
            changed_ = monitor_->signal_changed().connect(sigc::mem_fun(*this, &FolderListing::onChanged));
        } catch (const Glib::Error&) {
            // Unwatched, it still shows what's there now.
        }
        std::vector<Glib::RefPtr<Gio::FileInfo>> infos;
        try {
            auto children = dir_->enumerate_children(kAttributes);
            while (auto info = children->next_file()) {
                setFile(info, children->get_child(info));
                infos.push_back(info);
            }
        } catch (const Glib::Error&) {
            // Gone, or not readable: whatever was read.
        }
        std::vector<gpointer> items;
        for (const auto& info : infos) items.push_back(info->gobj());
        g_list_store_splice(store_, 0, 0, items.data(), static_cast<guint>(items.size()));
    }

    void onChanged(const Glib::RefPtr<Gio::File>& file, const Glib::RefPtr<Gio::File>& other,
                   Gio::FileMonitor::Event event) {
        using E = Gio::FileMonitor::Event;
        switch (event) {
        case E::CREATED:
        case E::MOVED_IN: add(file); break;
        case E::DELETED:
        case E::MOVED_OUT: remove(file); break;
        case E::RENAMED:
            remove(file);
            if (other) add(other);
            break;
        default: break; // contents and attributes: the tree shows neither
        }
    }

    void add(const Glib::RefPtr<Gio::File>& file) {
        Glib::RefPtr<Gio::FileInfo> info;
        try {
            info = file->query_info(kAttributes);
        } catch (const Glib::Error&) {
            return; // gone already: a temporary file
        }
        setFile(info, file);
        if (const auto at = find(info->get_name())) {
            // Listed already (saved over, or seen while the folder was read). Only a file turned folder
            // matters to the tree; a new row would close the folder's row if it was open.
            GFileInfo* listed = G_FILE_INFO(g_list_model_get_item(G_LIST_MODEL(store_), *at));
            const bool same = g_file_info_get_file_type(listed) == g_file_info_get_file_type(info->gobj());
            g_object_unref(listed);
            if (!same) {
                gpointer replacement = info->gobj();
                g_list_store_splice(store_, *at, 1, &replacement, 1);
            }
            return;
        }
        g_list_store_append(store_, info->gobj());
    }

    void remove(const Glib::RefPtr<Gio::File>& file) {
        if (const auto at = find(file->get_basename())) g_list_store_remove(store_, *at);
    }

    std::optional<guint> find(const std::string& name) const {
        const guint n = g_list_model_get_n_items(G_LIST_MODEL(store_));
        for (guint i = 0; i < n; ++i) {
            GFileInfo* info = G_FILE_INFO(g_list_model_get_item(G_LIST_MODEL(store_), i));
            const bool match = name == g_file_info_get_name(info);
            g_object_unref(info);
            if (match) return i;
        }
        return std::nullopt;
    }

    static void setFile(const Glib::RefPtr<Gio::FileInfo>& info, const Glib::RefPtr<Gio::File>& file) {
        g_file_info_set_attribute_object(info->gobj(), "standard::file", G_OBJECT(file->gobj()));
    }

    GListStore* store_; // owns this listing
    Glib::RefPtr<Gio::File> dir_;
    Glib::RefPtr<Gio::FileMonitor> monitor_;
    sigc::connection loading_, changed_;
};

} // namespace

ProjectTree::ProjectTree(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder)
    : ProjectTreeUi(object, builder) {
    filter_ = Glib::wrap(GTK_FILTER(gtk_custom_filter_new(&keepEntry, nullptr, nullptr)));
    sorter_ = Glib::wrap(GTK_SORTER(gtk_custom_sorter_new(&compareEntries, nullptr, nullptr)));

    auto factory = Gtk::SignalListItemFactory::create();
    factory->signal_setup().connect(sigc::mem_fun(*this, &ProjectTree::onSetupItem));
    factory->signal_bind().connect(sigc::mem_fun(*this, &ProjectTree::onBindItem));
    factory->signal_unbind().connect(sigc::mem_fun(*this, &ProjectTree::onUnbindItem));
    factory->signal_teardown().connect([this](const Glib::RefPtr<Gtk::ListItem>& item) {
        std::erase_if(rowItems_, [&item](const auto& entry) { return entry.second == item.get(); });
    });
    list_->set_factory(factory);
    list_->signal_activate().connect(sigc::mem_fun(*this, &ProjectTree::onActivate));
    search_->set_visible(false); // until a project is open
    // Up and Down move through the results while typing goes on in the field.
    auto searchKeys = Gtk::EventControllerKey::create();
    searchKeys->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    searchKeys->signal_key_pressed().connect(
        [this](guint keyval, guint, Gdk::ModifierType) {
            if (search_->get_text().empty()) return false;
            if (keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down) moveResultSelection(1);
            else if (keyval == GDK_KEY_Up || keyval == GDK_KEY_KP_Up) moveResultSelection(-1);
            else if (keyval == GDK_KEY_Page_Down) moveResultSelection(10);
            else if (keyval == GDK_KEY_Page_Up) moveResultSelection(-10);
            else return false;
            return true;
        },
        false);
    search_->add_controller(searchKeys);
    results_->signal_row_activated().connect([this](Gtk::ListBoxRow* row) {
        if (auto it = resultPaths_.find(row); it != resultPaths_.end()) fileActivated_.emit(root_ / it->second);
    });

    setupContextMenu();

    // Dropped on the space below the last row: into the project folder itself.
    auto rootDrop = Gtk::DropTarget::create(Gio::FileInfo::get_base_type(), Gdk::DragAction::MOVE);
    rootDrop->signal_drop().connect(
        [this](const Glib::ValueBase& value, double, double) { return !root_.empty() && dropOn(value, root_); }, false);
    list_->add_controller(rootDrop);
}

void ProjectTree::on_search_stop_search() {
    if (search_->get_text().empty()) searchDone_.emit();
    else search_->set_text("");
}

ProjectTree::~ProjectTree() {
    lister_.cancel();
    hoverExpand_.disconnect();
    menu_.unparent();
}

void ProjectTree::setRoot(const fs::path& root) {
    root_ = root;
    lister_.cancel();
    files_.clear();
    filesListed_ = false;
    searching_ = false;
    search_->set_text("");
    search_->set_visible(!root.empty());
    if (root.empty()) {
        gtk_list_view_set_model(list_->gobj(), nullptr);
        tree_.reset();
        selection_.reset();
        stack_->set_visible_child("empty");
        return;
    }
    auto rootModel = directoryModel(Gio::File::create_for_path(root.string()));
    tree_ = Gtk::TreeListModel::create(rootModel, [this](const Glib::RefPtr<Glib::ObjectBase>& item) {
        auto info = std::dynamic_pointer_cast<Gio::FileInfo>(item);
        if (!info || info->get_file_type() != Gio::FileType::DIRECTORY) return Glib::RefPtr<Gio::ListModel>();
        return directoryModel(Glib::wrap(fileOf(info), true));
    });
    selection_ = Gtk::SingleSelection::create(tree_);
    list_->set_model(selection_);
    stack_->set_visible_child("tree");
}

Glib::RefPtr<Gio::ListModel> ProjectTree::directoryModel(const Glib::RefPtr<Gio::File>& dir) {
    auto filtered = Gtk::FilterListModel::create(FolderListing::create(dir), filter_);
    return Gtk::SortListModel::create(filtered, sorter_);
}

void ProjectTree::onSetupItem(const Glib::RefPtr<Gtk::ListItem>& item) {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    box->append(*Gtk::make_managed<Gtk::Image>());
    auto* label = Gtk::make_managed<Gtk::Label>();
    label->set_xalign(0.0f);
    label->set_ellipsize(Pango::EllipsizeMode::END);
    box->append(*label);
    auto* git = Gtk::make_managed<Gtk::Image>(); // uncommitted changes (see setGitChanges)
    git->set_visible(false);
    box->append(*git);
    auto* expander = Gtk::make_managed<Gtk::TreeExpander>();
    expander->set_child(*box);
    expander->add_css_class("kevc-tree-row");
    item->set_child(*expander);
    rowItems_[expander] = item.get();
    setupDrag(*expander, item.get());
}

void ProjectTree::onBindItem(const Glib::RefPtr<Gtk::ListItem>& item) {
    auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(item->get_item());
    auto* expander = dynamic_cast<Gtk::TreeExpander*>(item->get_child());
    if (!row || !expander) return;
    expander->set_list_row(row);
    auto info = std::dynamic_pointer_cast<Gio::FileInfo>(row->get_item());
    auto* box = dynamic_cast<Gtk::Box*>(expander->get_child());
    if (!info || !box) return;
    auto* image = dynamic_cast<Gtk::Image*>(box->get_first_child());
    auto* label = dynamic_cast<Gtk::Label*>(image->get_next_sibling());
    auto* git = dynamic_cast<Gtk::Image*>(label->get_next_sibling());
    label->set_text(info->get_display_name());
    onUnbindItem(item);
    BoundRow bound{pathOf(info->gobj()), info->get_file_type() == Gio::FileType::DIRECTORY, git};
    showGitState(bound);
    bound_[item.get()] = bound;
    if (!bound.folder) {
        image->set_from_icon_name(icons::forFile(info->get_name()));
        return;
    }
    image->set_from_icon_name(icons::forFolder(row->get_expanded()));
    // Raw pointers: the row owns this connection, and unbinding disconnects it before the image
    // can go away or be reused for another row.
    Gtk::TreeListRow* r = row.get();
    expandWatch_[item.get()] = row->property_expanded().signal_changed().connect(
        [r, image] { image->set_from_icon_name(icons::forFolder(r->get_expanded())); });
}

void ProjectTree::onUnbindItem(const Glib::RefPtr<Gtk::ListItem>& item) {
    if (auto it = expandWatch_.find(item.get()); it != expandWatch_.end()) {
        it->second.disconnect();
        expandWatch_.erase(it);
    }
    bound_.erase(item.get());
}

void ProjectTree::setGitChanges(std::map<std::string, git::FileState> files, std::set<std::string> folders) {
    if (files == gitFiles_ && folders == gitFolders_) return;
    gitFiles_ = std::move(files);
    gitFolders_ = std::move(folders);
    for (const auto& [item, row] : bound_) showGitState(row);
}

void ProjectTree::showGitState(const BoundRow& row) const {
    const char* icon = nullptr;
    const char* tip = nullptr;
    if (row.folder) {
        if (gitFolders_.count(row.path)) icon = "kevc-git-modified", tip = "Holds changes that aren't committed";
    } else if (auto it = gitFiles_.find(row.path); it != gitFiles_.end()) {
        switch (it->second) {
        case git::FileState::Modified: icon = "kevc-git-modified", tip = "Changed since the last commit"; break;
        case git::FileState::Added: icon = "kevc-git-added", tip = "New, staged for the next commit"; break;
        case git::FileState::Untracked: icon = "kevc-git-added", tip = "New: not in git yet"; break;
        case git::FileState::Conflicted: icon = "kevc-git-conflict", tip = "Has merge conflicts"; break;
        case git::FileState::Deleted: break; // not in the tree
        }
    }
    row.git->set_visible(icon != nullptr);
    if (icon) {
        row.git->set_from_icon_name(icon);
        row.git->set_tooltip_text(tip);
    }
}

void ProjectTree::onActivate(guint position) {
    auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(selection_->get_object(position));
    if (!row) return;
    if (row->is_expandable()) {
        row->set_expanded(!row->get_expanded());
        return;
    }
    auto info = std::dynamic_pointer_cast<Gio::FileInfo>(row->get_item());
    if (!info) return;
    if (const std::string path = pathOf(info->gobj()); !path.empty()) fileActivated_.emit(path);
}

// ---------------------------------------------------------------------------------------------
// Search

void ProjectTree::focusSearch() {
    if (root_.empty()) return;
    search_->grab_focus();
    search_->select_region(0, -1);
}

void ProjectTree::onSearchChanged() {
    if (root_.empty()) return;
    if (search_->get_text().empty()) {
        searching_ = false;
        stack_->set_visible_child("tree");
        return;
    }
    // A new search lists the files again, so it finds what was added since the last one.
    if (!searching_) {
        searching_ = true;
        listFiles();
    }
    showResults();
    stack_->set_visible_child("results");
}

void ProjectTree::listFiles() {
    lister_.start([this, root = root_](const std::atomic<bool>& cancelled, const BackgroundJob::Post& post) {
        auto files = search::listFiles(root, 200000, &cancelled);
        post([this, files = std::move(files)]() mutable {
            files_ = std::move(files);
            filesListed_ = true;
            if (!search_->get_text().empty()) showResults();
        });
    });
}

void ProjectTree::showResults() {
    const std::string query = search_->get_text();
    resultPaths_.clear();
    while (auto* row = results_->get_row_at_index(0)) results_->remove(*row);
    if (!filesListed_) {
        resultsMessage_->set_text("Listing files…");
        return;
    }
    resultsMessage_->set_text("No file names match \u201c" + query + "\u201d.");
    for (const auto& match : search::rankFiles(files_, query, 200)) {
        const std::set<size_t> positions(match.positions.begin(), match.positions.end());
        const size_t slash = match.path.rfind('/');
        const size_t nameStart = slash == std::string::npos ? 0 : slash + 1;
        const std::string_view path = match.path;

        auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        box->set_margin_start(6);
        box->set_margin_end(4);
        box->set_margin_top(1);
        box->set_margin_bottom(1);
        auto* image = Gtk::make_managed<Gtk::Image>();
        image->set_from_icon_name(icons::forFile(fs::path(match.path).filename()));
        box->append(*image);
        auto* name = Gtk::make_managed<Gtk::Label>();
        name->set_markup(highlightLetters(path.substr(nameStart), nameStart, positions));
        name->set_xalign(0.0f);
        box->append(*name);
        if (nameStart > 0) { // the folder, dimmed; its end is what tells files apart, so cut its start
            auto* folder = Gtk::make_managed<Gtk::Label>();
            folder->set_markup("<span alpha=\"60%\">" + highlightLetters(path.substr(0, nameStart - 1), 0, positions) + "</span>");
            folder->set_xalign(0.0f);
            folder->set_hexpand(true);
            folder->set_ellipsize(Pango::EllipsizeMode::START);
            box->append(*folder);
        }
        auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_child(*box);
        row->set_tooltip_text(match.path);
        results_->append(*row);
        resultPaths_[row] = match.path;
    }
    if (auto* first = results_->get_row_at_index(0)) results_->select_row(*first);
}

void ProjectTree::moveResultSelection(int step) {
    const int count = static_cast<int>(resultPaths_.size());
    if (count == 0) return;
    Gtk::ListBoxRow* selected = results_->get_selected_row();
    const int index = std::clamp((selected ? selected->get_index() : -1) + step, 0, count - 1);
    if (auto* row = results_->get_row_at_index(index)) {
        results_->select_row(*row);
        // Keep it in view: scroll the row's area into the scroller's window.
        auto adjustment = resultsScroller_->get_vadjustment();
        graphene_rect_t bounds;
        if (gtk_widget_compute_bounds(GTK_WIDGET(row->gobj()), GTK_WIDGET(results_->gobj()), &bounds)) {
            const double top = bounds.origin.y, bottom = top + bounds.size.height;
            if (top < adjustment->get_value()) adjustment->set_value(top);
            else if (bottom > adjustment->get_value() + adjustment->get_page_size())
                adjustment->set_value(bottom - adjustment->get_page_size());
        }
    }
}

bool ProjectTree::openSelectedResult() {
    Gtk::ListBoxRow* row = results_->get_selected_row();
    if (!row) row = results_->get_row_at_index(0);
    auto it = row ? resultPaths_.find(row) : resultPaths_.end();
    if (it == resultPaths_.end()) return false;
    fileActivated_.emit(root_ / it->second);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Context menu

void ProjectTree::setupContextMenu() {
    actions_ = Gio::SimpleActionGroup::create();
    actions_->add_action("open", [this] {
        if (menuRow_ && !menuRow_->folder) fileActivated_.emit(menuRow_->path);
    });
    actions_->add_action("show-in-file-manager", [this] {
        if (menuRow_ && !menuRow_->folder) showInFileManager_.emit(menuRow_->path);
    });
    actions_->add_action("open-in-file-manager", [this] { openInFileManager_.emit(menuFolder()); });
    actions_->add_action("open-terminal", [this] { openTerminal_.emit(menuFolder()); });
    actions_->add_action("new-file", [this] {
        expandMenuRow();
        newFile_.emit(menuFolder());
    });
    actions_->add_action("new-folder", [this] {
        expandMenuRow();
        newFolder_.emit(menuFolder());
    });
    actions_->add_action("rename", [this] {
        if (menuRow_) rename_.emit(menuRow_->path);
    });
    actions_->add_action("delete", [this] {
        if (menuRow_) delete_.emit(menuRow_->path);
    });
    actions_->add_action("copy-path", [this] {
        get_clipboard()->set_text(menuRow_ ? menuRow_->path : root_.string());
    });
    actions_->add_action("copy-relative-path", [this] {
        const fs::path path = menuRow_ ? fs::path(menuRow_->path) : root_;
        const fs::path rel = path.lexically_relative(root_);
        get_clipboard()->set_text(rel.empty() || rel == "." ? std::string(".") : rel.generic_string());
    });
    insert_action_group("tree", actions_);

    menu_.set_menu_model(contextMenu_);
    menu_.set_has_arrow(false);
    menu_.set_halign(Gtk::Align::START);
    menu_.set_parent(*this);

    auto rightClick = Gtk::GestureClick::create();
    rightClick->set_button(GDK_BUTTON_SECONDARY);
    rightClick->signal_pressed().connect([this](int, double x, double y) {
        if (root_.empty()) return;
        Gtk::ListItem* item = itemAt(x, y);
        if (item && selection_) selection_->set_selected(item->get_position());
        showContextMenu(item, x, y);
    });
    list_->add_controller(rightClick);

    // F2 and Delete act on the selected row, as in file managers.
    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect(
        [this](guint keyval, guint, Gdk::ModifierType state) {
            const auto mods = state & (Gdk::ModifierType::SHIFT_MASK | Gdk::ModifierType::CONTROL_MASK |
                                       Gdk::ModifierType::ALT_MASK);
            if (mods != Gdk::ModifierType{} || !selection_) return false;
            if (keyval != GDK_KEY_F2 && keyval != GDK_KEY_Delete && keyval != GDK_KEY_KP_Delete) return false;
            auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(selection_->get_selected_item());
            auto info = row ? std::dynamic_pointer_cast<Gio::FileInfo>(row->get_item()) : nullptr;
            const std::string path = info ? pathOf(info->gobj()) : std::string();
            if (path.empty()) return false;
            (keyval == GDK_KEY_F2 ? rename_ : delete_).emit(path);
            return true;
        },
        false);
    list_->add_controller(keys);
}

Gtk::ListItem* ProjectTree::itemAt(double x, double y) {
    for (Gtk::Widget* w = list_->pick(x, y, Gtk::PickFlags::DEFAULT); w && w != list_; w = w->get_parent())
        if (auto it = rowItems_.find(w); it != rowItems_.end()) return it->second;
    return nullptr;
}

void ProjectTree::showContextMenu(Gtk::ListItem* item, double x, double y) {
    menuRow_.reset();
    menuTreeRow_.reset();
    if (item)
        if (auto info = infoOf(item)) {
            menuRow_ = BoundRow{pathOf(info->gobj()), info->get_file_type() == Gio::FileType::DIRECTORY, nullptr};
            menuTreeRow_ = std::dynamic_pointer_cast<Gtk::TreeListRow>(item->get_item());
        }
    auto enable = [this](const char* name, bool on) {
        if (auto action = std::dynamic_pointer_cast<Gio::SimpleAction>(actions_->lookup_action(name)))
            action->set_enabled(on);
    };
    // The design hides the items whose action is off: a file gets Open Containing Folder, a folder
    // (or the empty space, meaning the project folder) Open This Folder and Open Terminal Here.
    const bool file = menuRow_ && !menuRow_->folder;
    enable("open", file);
    enable("show-in-file-manager", file);
    enable("open-in-file-manager", !file);
    enable("open-terminal", !file);
    enable("rename", menuRow_.has_value());
    enable("delete", menuRow_.has_value());

    graphene_point_t point = GRAPHENE_POINT_INIT(float(x), float(y)), inTree;
    if (!gtk_widget_compute_point(GTK_WIDGET(list_->gobj()), GTK_WIDGET(gobj()), &point, &inTree)) return;
    menu_.set_pointing_to(Gdk::Rectangle(int(inTree.x), int(inTree.y), 1, 1));
    menu_.popup();
}

fs::path ProjectTree::menuFolder() const {
    if (!menuRow_) return root_;
    const fs::path path = menuRow_->path;
    return menuRow_->folder ? path : path.parent_path();
}

void ProjectTree::expandMenuRow() {
    if (menuRow_ && menuRow_->folder && menuTreeRow_) menuTreeRow_->set_expanded(true);
}

// ---------------------------------------------------------------------------------------------
// Drag and drop

void ProjectTree::setupDrag(Gtk::Widget& row, Gtk::ListItem* item) {
    // The drag carries the row's GFileInfo, a type no other program can read: rows only move
    // inside the tree, and a drop anywhere else does nothing.
    auto source = Gtk::DragSource::create();
    source->set_actions(Gdk::DragAction::MOVE);
    source->signal_prepare().connect(
        [item](double, double) -> Glib::RefPtr<Gdk::ContentProvider> {
            auto info = infoOf(item);
            if (!info) return {};
            Glib::Value<Glib::RefPtr<Gio::FileInfo>> value;
            value.init(Glib::Value<Glib::RefPtr<Gio::FileInfo>>::value_type());
            value.set(info);
            return Gdk::ContentProvider::create(value);
        },
        false);
    Gtk::DragSource* src = source.get(); // the controller outlives its own handlers
    source->signal_drag_begin().connect([src, &row](const Glib::RefPtr<Gdk::Drag>&) {
        src->set_icon(Gtk::WidgetPaintable::create(row), 0, 0);
    });
    row.add_controller(source);

    // Dropped on a folder: into it. On a file: into the folder the file is in.
    auto target = Gtk::DropTarget::create(Gio::FileInfo::get_base_type(), Gdk::DragAction::MOVE);
    target->signal_drop().connect(
        [this, item](const Glib::ValueBase& value, double, double) {
            auto it = bound_.find(item);
            if (it == bound_.end()) return false;
            const fs::path path = it->second.path;
            return dropOn(value, it->second.folder ? path : path.parent_path());
        },
        false);
    // A drag resting on a closed folder opens it, so files can go deeper than what's showing.
    target->signal_enter().connect(
        [this, item](double, double) {
            hoverExpand_.disconnect();
            auto treeRow = std::dynamic_pointer_cast<Gtk::TreeListRow>(item->get_item());
            if (treeRow && treeRow->is_expandable() && !treeRow->get_expanded())
                hoverExpand_ = Glib::signal_timeout().connect(
                    [treeRow] {
                        treeRow->set_expanded(true);
                        return false;
                    },
                    700);
            return Gdk::DragAction::MOVE;
        },
        false);
    target->signal_leave().connect([this] { hoverExpand_.disconnect(); });
    row.add_controller(target);
}

bool ProjectTree::dropOn(const Glib::ValueBase& value, const fs::path& targetDir) {
    hoverExpand_.disconnect();
    if (!G_VALUE_HOLDS(value.gobj(), G_TYPE_FILE_INFO)) return false;
    auto* info = G_FILE_INFO(g_value_get_object(value.gobj()));
    const fs::path source = info ? pathOf(info) : std::string();
    if (source.empty() || source == targetDir || fileops::sameFolder(source, targetDir)) return false;
    // After the drop finishes: moving may ask questions or show an error.
    Glib::signal_idle().connect_once(
        sigc::track_obj([this, source, targetDir] { move_.emit(source, targetDir); }, *this));
    return true;
}

} // namespace kevc
