#include "editor/RoutesPanel.h"

#include "core/Icons.h"
#include "core/Search.h"

namespace fs = std::filesystem;

namespace kevc {

namespace {

Glib::ustring esc(const std::string& s) {
    return Glib::Markup::escape_text(s);
}

std::string join(const std::vector<std::string>& items, const char* sep = ", ") {
    std::string out;
    for (const auto& i : items) out += (out.empty() ? "" : sep) + i;
    return out;
}

} // namespace

RoutesPanel::RoutesPanel(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder)
    : RoutesPanelUi(object, builder) {
    // A double-click (or Enter) opens the file.
    list_->signal_row_activated().connect([this](Gtk::ListBoxRow* row) {
        if (auto it = targets_.find(row); it != targets_.end()) open_.emit(it->second.file, it->second.line);
    });
    list_->set_filter_func([this](Gtk::ListBoxRow* row) {
        const std::string query = filter_->get_text();
        auto it = rowGroup_.find(row);
        return query.empty() || it == rowGroup_.end() || search::containsAllWords(groupText_[it->second], query);
    });
}

void RoutesPanel::on_filter_search_changed() {
    onFilterChanged();
}

void RoutesPanel::on_filter_stop_search() {
    filter_->set_text("");
}

void RoutesPanel::on_new_route_clicked() {
    newRoute_.emit();
}

void RoutesPanel::showMessage(const Glib::ustring& text) {
    targets_.clear();
    rowGroup_.clear();
    groupText_.clear();
    while (auto* row = list_->get_row_at_index(0)) list_->remove(*row);
    emptyMessage_ = text;
    message_->set_text(text);
    onFilterChanged();
}

void RoutesPanel::beginGroup(std::string text) {
    groupText_.push_back(std::move(text));
}

void RoutesPanel::onFilterChanged() {
    const std::string query = filter_->get_text();
    // The placeholder shows whenever no row is visible: say why.
    message_->set_text(query.empty() || groupText_.empty()
                           ? emptyMessage_
                           : Glib::ustring("No routes match \u201c" + query + "\u201d."));
    list_->invalidate_filter();
}

Gtk::Widget& RoutesPanel::line(const char* icon, const Glib::ustring& markup) {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    if (icon) {
        auto* image = Gtk::make_managed<Gtk::Image>();
        image->set_from_icon_name(icon);
        image->set_pixel_size(16);
        box->append(*image);
    }
    auto* label = Gtk::make_managed<Gtk::Label>();
    label->set_markup(markup);
    label->set_xalign(0.0f);
    label->set_hexpand(true);
    label->set_ellipsize(Pango::EllipsizeMode::END);
    label->set_tooltip_markup(markup);
    box->append(*label);
    return *box;
}

Gtk::ListBoxRow& RoutesPanel::addRow(Gtk::Widget& content, int depth, std::optional<Target> target) {
    auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
    content.set_margin_start(6 + depth * 18);
    content.set_margin_end(4);
    content.set_margin_top(1);
    content.set_margin_bottom(1);
    row->set_child(content);
    list_->append(*row);
    if (target) targets_[row] = *target;
    if (!groupText_.empty()) rowGroup_[row] = groupText_.size() - 1;
    return *row;
}

void RoutesPanel::setApp(const kit::App& app, const fs::path& root) {
    showMessage("No routes yet: add one with +, or create src/routes/+page.svelte.");
    // showMessage counted no groups yet; once rows exist, an empty list means the filter hid them.
    const fs::path routes = root / "src" / "routes";
    for (const auto& route : app.routes) {
        // What the filter looks in: the URL, the id, each file's path and exports.
        std::string text = route.url + " " + route.id;
        for (const auto& f : route.files) {
            text += " " + f.path.lexically_relative(routes).generic_string();
            for (const auto& e : f.exports) text += " " + e.name;
            for (const auto& a : f.actions) text += " " + a;
        }
        beginGroup(std::move(text));

        // The route itself: its URL (and its id when groups make them differ).
        Glib::ustring markup = "<b>" + esc(route.url) + "</b>";
        if (route.id != route.url) markup += "  <span alpha=\"60%\">" + esc(route.id) + "</span>";
        auto& head = line(route.hasPage() ? "kevc-globe" : "kevc-folder", markup);
        if (route.hasPage()) {
            auto* browse = Gtk::make_managed<Gtk::Button>();
            browse->set_icon_name("send-to-symbolic");
            browse->set_has_frame(false);
            browse->set_tooltip_text("Open " + route.url + " in the browser (starts the dev server)");
            browse->add_css_class("kevc-tab-menu");
            const std::string url = route.url;
            browse->signal_clicked().connect([this, url] { openInBrowser_.emit(url); });
            static_cast<Gtk::Box&>(head).append(*browse);
        }
        const kit::RouteFile* first = route.files.empty() ? nullptr : &route.files.front();
        if (const auto* page = route.file("page", false)) first = page;
        addRow(head, 0, first ? std::optional<Target>(Target{first->path, 1}) : std::nullopt);

        for (const auto& f : route.files) {
            std::string detail;
            int target = 1;
            if (f.lineOf("load")) {
                detail = "load → " + (f.loadKeys.empty() ? std::string("…") : join(f.loadKeys));
                target = f.lineOf("load");
            }
            if (!f.actions.empty()) {
                detail += (detail.empty() ? "" : " · ") + std::string("actions: ") + join(f.actions);
                if (target == 1) target = f.lineOf("actions");
            }
            if (f.role == "server") {
                std::vector<std::string> verbs;
                for (const auto& e : f.exports)
                    if (e.name == "GET" || e.name == "POST" || e.name == "PUT" || e.name == "PATCH" ||
                        e.name == "DELETE" || e.name == "OPTIONS" || e.name == "HEAD" || e.name == "fallback") {
                        verbs.push_back(e.name);
                        if (target == 1) target = e.line;
                    }
                detail = join(verbs, " ");
            }
            for (const char* option : {"prerender", "ssr", "csr", "trailingSlash"})
                if (f.lineOf(option)) detail += (detail.empty() ? "" : " · ") + std::string(option);
            Glib::ustring fileMarkup = esc(f.name);
            if (!detail.empty()) fileMarkup += "  <span alpha=\"60%\">" + esc(detail) + "</span>";
            addRow(line(icons::forFile(f.path).c_str(), fileMarkup), 1, Target{f.path, target});
        }

        // Where the page's `data` comes from, outermost first.
        if (route.hasPage()) {
            std::string sources;
            for (const auto& source : kit::dataFlow(app, route)) {
                const std::string where = source.file->path.lexically_relative(routes).generic_string();
                sources += (sources.empty() ? "" : "  ·  ") +
                           (source.keys.empty() ? std::string("…") : join(source.keys)) + " ← " + where;
            }
            if (!sources.empty())
                addRow(line(nullptr, "<span alpha=\"70%\"><i>data</i>: " + esc(sources) + "</span>"), 1, std::nullopt);
        }
    }

    if (!app.remote.empty()) {
        // The heading shows while any remote function does.
        std::string all = "remote";
        for (const auto& fn : app.remote)
            all += " " + fn.name + " " + fn.kind + " " + fn.file.lexically_relative(root / "src").generic_string();
        beginGroup(std::move(all));
        addRow(line(nullptr, "<b>Remote functions</b>"), 0, std::nullopt);
        for (const auto& fn : app.remote) {
            const std::string where = fn.file.lexically_relative(root / "src").generic_string();
            beginGroup("remote " + fn.name + " " + fn.kind + " " + where);
            addRow(line(icons::forFile(fn.file).c_str(),
                        esc(fn.name) + "  <span alpha=\"60%\">" + esc(fn.kind + " · " + where) + "</span>"),
                   1, Target{fn.file, fn.line});
        }
    }
    onFilterChanged();
}

} // namespace kevc
