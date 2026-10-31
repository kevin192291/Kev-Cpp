#include "editor/ProjectTree.h"

#include "core/Icons.h"
#include "core/ProjectSession.h"

#include <cstring>

namespace kevc {

namespace {

constexpr const char* kAttributes = "standard::name,standard::display-name,standard::type";

gboolean keepEntry(gpointer item, gpointer) {
    GFileInfo* info = G_FILE_INFO(item);
    const std::string name = g_file_info_get_name(info);
    if (g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY) return !isIgnoredDirName(name);
    return !name.starts_with('.') && !name.ends_with(".kevc-tmp") && !name.ends_with(".tmp");
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

GFile* fileOf(const Glib::RefPtr<Gio::FileInfo>& info) {
    return G_FILE(g_file_info_get_attribute_object(info->gobj(), "standard::file"));
}

} // namespace

ProjectTree::ProjectTree() {
    placeholder_.set_text("No project open.\n\nFile → New Project or Open Project.");
    placeholder_.add_css_class("dim-label");
    placeholder_.set_wrap(true);
    placeholder_.set_justify(Gtk::Justification::CENTER);

    filter_ = Glib::wrap(GTK_FILTER(gtk_custom_filter_new(&keepEntry, nullptr, nullptr)));
    sorter_ = Glib::wrap(GTK_SORTER(gtk_custom_sorter_new(&compareEntries, nullptr, nullptr)));

    auto factory = Gtk::SignalListItemFactory::create();
    factory->signal_setup().connect(sigc::mem_fun(*this, &ProjectTree::onSetupItem));
    factory->signal_bind().connect(sigc::mem_fun(*this, &ProjectTree::onBindItem));
    factory->signal_unbind().connect(sigc::mem_fun(*this, &ProjectTree::onUnbindItem));
    list_.set_factory(factory);
    list_.signal_activate().connect(sigc::mem_fun(*this, &ProjectTree::onActivate));
    scroller_.set_child(list_);
    scroller_.set_vexpand(true);

    add(placeholder_, "empty");
    add(scroller_, "tree");
    set_visible_child("empty");
}

void ProjectTree::setRoot(const std::filesystem::path& root) {
    if (root.empty()) {
        gtk_list_view_set_model(list_.gobj(), nullptr);
        tree_.reset();
        selection_.reset();
        set_visible_child("empty");
        return;
    }
    auto rootModel = directoryModel(Gio::File::create_for_path(root.string()));
    tree_ = Gtk::TreeListModel::create(rootModel, [this](const Glib::RefPtr<Glib::ObjectBase>& item) {
        auto info = std::dynamic_pointer_cast<Gio::FileInfo>(item);
        if (!info || info->get_file_type() != Gio::FileType::DIRECTORY) return Glib::RefPtr<Gio::ListModel>();
        return directoryModel(Glib::wrap(fileOf(info), true));
    });
    selection_ = Gtk::SingleSelection::create(tree_);
    list_.set_model(selection_);
    set_visible_child("tree");
}

Glib::RefPtr<Gio::ListModel> ProjectTree::directoryModel(const Glib::RefPtr<Gio::File>& dir) {
    auto listing = Gtk::DirectoryList::create(kAttributes, dir);
    listing->set_monitored(true);
    auto filtered = Gtk::FilterListModel::create(listing, filter_);
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
    item->set_child(*expander);
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
    BoundRow bound{{}, info->get_file_type() == Gio::FileType::DIRECTORY, git};
    if (char* path = g_file_get_path(fileOf(info))) {
        bound.path = path;
        g_free(path);
    }
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
    if (char* path = g_file_get_path(fileOf(info))) {
        fileActivated_.emit(path);
        g_free(path);
    }
}

} // namespace kevc
