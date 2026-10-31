// The GTK designer's building tools: the palette, dragging widgets in and around the design, the
// clipboard, and the context menu. The rest of DesignerPage is in DesignerPage.cpp.
#include "designer/DesignerPage.h"

#include "designer/DesignerTypes.h"

#include <algorithm>
#include <cmath>

namespace kevc {

using designer::catalog;
using designer::catalogInfo;
using designer::isWindowClass;
using ui::Node;
using ui::Placement;
using ui::Slots;
using ui::UiDocument;

namespace {

bool stacksVertically(const Node& container) {
    if (auto o = UiDocument::property(container, "orientation")) return *o == "vertical" || *o == "GTK_ORIENTATION_VERTICAL";
    // A list box has no orientation; it stacks its rows.
    const GType type = designer::typeOf(UiDocument::className(container));
    return type && g_type_is_a(type, GTK_TYPE_LIST_BOX);
}

int number(const std::optional<std::string>& text, int fallback) {
    try {
        return text ? std::stoi(*text) : fallback;
    } catch (const std::exception&) {
        return fallback;
    }
}

Placement placement(Placement::Kind kind, Node* parent = nullptr) {
    Placement p;
    p.kind = kind;
    p.parent = parent;
    return p;
}

Glib::RefPtr<Gdk::ContentProvider> textContent(const std::string& text) {
    Glib::Value<Glib::ustring> value;
    value.init(value.value_type());
    value.set(text);
    return Gdk::ContentProvider::create(value);
}

} // namespace

// ---- the palette

void DesignerPage::buildPalette() {
    const ui::Catalog& c = catalog();
    for (const std::string& group : c.groups()) {
        auto* title = Gtk::make_managed<Gtk::Label>(group);
        title->set_xalign(0);
        title->add_css_class("kevc-palette-group");
        palette_.append(*title);
        for (const ui::ClassInfo& info : c.classes()) {
            if (info.group != group) continue;
            auto* item = Gtk::make_managed<Gtk::Button>(info.label);
            item->set_has_frame(false);
            item->add_css_class("kevc-palette-item");
            if (auto* label = dynamic_cast<Gtk::Label*>(item->get_child())) label->set_xalign(0);
            item->set_tooltip_text(info.cls + "\nDrag it into the design, or click to add it to the selection");
            const std::string cls = info.cls;
            item->signal_clicked().connect([this, cls] { insertObject(cls, {}, insertPlacement(cls)); });
            auto source = Gtk::DragSource::create();
            source->set_actions(Gdk::DragAction::COPY);
            source->signal_prepare().connect(
                [this, cls](double, double) -> Glib::RefPtr<Gdk::ContentProvider> {
                    if (view_ != View::Design) return {};
                    dragging_ = "new:" + cls;
                    return textContent(dragging_);
                },
                false);
            source->signal_drag_end().connect(
                [this](const Glib::RefPtr<Gdk::Drag>&, bool) {
                    dragging_.clear();
                    dropPlan_.reset();
                    selectionLayer_.queue_draw();
                },
                false);
            item->add_controller(source);
            palette_.append(*item);
        }
    }
}

// ---- dragging and dropping

int DesignerPage::objectAt(double x, double y) {
    if (!previewRoot_) return 0;
    graphene_point_t point = GRAPHENE_POINT_INIT(float(x), float(y)), inFrame;
    if (!gtk_widget_compute_point(GTK_WIDGET(selectionLayer_.gobj()), GTK_WIDGET(frame_.gobj()), &point, &inFrame))
        return 0;
    GtkWidget* hit = gtk_widget_pick(GTK_WIDGET(frame_.gobj()), inFrame.x, inFrame.y,
                                     GtkPickFlags(GTK_PICK_NON_TARGETABLE | GTK_PICK_INSENSITIVE));
    for (GtkWidget* w = hit; w && w != GTK_WIDGET(frame_.gobj()); w = gtk_widget_get_parent(w))
        if (const int uid = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "kevc-node"))) return uid;
    if (hit == GTK_WIDGET(fakeTitle_.gobj())) return shownRoot_;
    return 0;
}

std::optional<DesignerPage::DropPlan> DesignerPage::planDrop(double x, double y) {
    if (dragging_.empty() || view_ != View::Design) return std::nullopt;
    Node* moving = nullptr;
    std::string cls;
    if (dragging_.rfind("move:", 0) == 0) {
        moving = doc_.find(std::atoi(dragging_.c_str() + 5));
        if (!moving) return std::nullopt;
        cls = UiDocument::className(*moving);
    } else {
        cls = dragging_.substr(4);
    }
    GtkWidget* layer = GTK_WIDGET(selectionLayer_.gobj());
    auto bounds = [&](const Node* n) -> std::optional<graphene_rect_t> {
        auto it = n ? widgets_.find(n->uid) : widgets_.end();
        graphene_rect_t r;
        if (it == widgets_.end() || !gtk_widget_get_mapped(it->second) || !gtk_widget_compute_bounds(it->second, layer, &r))
            return std::nullopt;
        return r;
    };
    const graphene_rect_t whole = GRAPHENE_RECT_INIT(4, 4, float(selectionLayer_.get_width() - 8),
                                                     float(selectionLayer_.get_height() - 8));
    // A window can only be a top-level object; so can anything dropped on an empty design.
    const int hit = objectAt(x, y);
    if (isWindowClass(cls) || (!hit && !previewRoot_)) {
        if (moving || !doc_.interface()) return std::nullopt;
        return DropPlan{placement(Placement::Kind::TopLevel), whole, false};
    }
    auto without = [&](std::vector<Node*> objects) {
        std::erase(objects, moving);
        return objects;
    };

    // The innermost container under the pointer with room for it.
    for (Node* at = doc_.find(hit); at; at = UiDocument::parentObject(*at)) {
        if (moving && ui::isWithin(*at, *moving)) continue;
        const ui::ClassInfo* info = catalogInfo(UiDocument::className(*at));
        if (!info || info->slots == Slots::None) continue;
        const auto box = bounds(at);
        if (!box) continue;
        const float bx = box->origin.x, by = box->origin.y, bw = box->size.width, bh = box->size.height;
        const double fx = (x - bx) / std::max(1.f, bw), fy = (y - by) / std::max(1.f, bh);
        DropPlan plan{placement(Placement::Kind::Child, at), *box, false};

        switch (info->slots) {
        case Slots::Children: {
            const bool down = stacksVertically(*at);
            plan.line = true;
            std::optional<graphene_rect_t> last;
            for (Node* c : without(ui::childObjectsOfType(*at, ""))) {
                const auto r = bounds(c);
                if (!r) continue;
                if (down ? y < r->origin.y + r->size.height / 2 : x < r->origin.x + r->size.width / 2) {
                    plan.placement.before = c;
                    plan.indicator = down ? GRAPHENE_RECT_INIT(r->origin.x, r->origin.y - 2, r->size.width, 0)
                                          : GRAPHENE_RECT_INIT(r->origin.x - 2, r->origin.y, 0, r->size.height);
                    return plan;
                }
                last = r;
            }
            if (last)
                plan.indicator = down ? GRAPHENE_RECT_INIT(last->origin.x, last->origin.y + last->size.height + 2, last->size.width, 0)
                                      : GRAPHENE_RECT_INIT(last->origin.x + last->size.width + 2, last->origin.y, 0, last->size.height);
            else
                plan.line = false; // empty: the whole box
            return plan;
        }
        case Slots::Window:
        case Slots::Single:
        case Slots::Overlay: {
            const GType type = designer::typeOf(cls);
            if (info->slots == Slots::Window && type && g_type_is_a(type, GTK_TYPE_HEADER_BAR) &&
                without(ui::childObjectsOfType(*at, "titlebar")).empty()) {
                plan.placement.name = "titlebar";
                return plan;
            }
            Node* propertyChild = ui::propertyObject(*at, "child");
            const bool full = !without(ui::childObjectsOfType(*at, "")).empty() || (propertyChild && propertyChild != moving);
            if (!full) return plan;
            if (info->slots == Slots::Overlay) {
                plan.placement.name = "overlay";
                return plan;
            }
            continue; // no room: the container around it
        }
        case Slots::Properties: {
            if (info->names.empty()) continue;
            const bool down = stacksVertically(*at);
            const size_t n = info->names.size();
            const size_t part = std::min(n - 1, size_t(std::max(0.0, (down ? fy : fx) * double(n))));
            Node* there = ui::propertyObject(*at, info->names[part]);
            if (there && there != moving) continue;
            plan.placement.kind = Placement::Kind::Property;
            plan.placement.name = info->names[part];
            plan.indicator = down ? GRAPHENE_RECT_INIT(bx, by + bh * float(part) / float(n), bw, bh / float(n))
                                  : GRAPHENE_RECT_INIT(bx + bw * float(part) / float(n), by, bw / float(n), bh);
            return plan;
        }
        case Slots::Typed: {
            const float third = bw / 3;
            if (fx > 1.0 / 3 && fx < 2.0 / 3 && !info->property.empty()) {
                Node* there = ui::propertyObject(*at, info->property);
                if (!there || there == moving) {
                    plan.placement.kind = Placement::Kind::Property;
                    plan.placement.name = info->property;
                    plan.indicator = GRAPHENE_RECT_INIT(bx + third, by, third, bh);
                    return plan;
                }
            }
            const bool start = fx < 0.5;
            if (!info->names.empty()) plan.placement.name = start ? info->names.front() : info->names.back();
            plan.indicator = GRAPHENE_RECT_INIT(start ? bx : bx + 2 * third, by, third, bh);
            return plan;
        }
        case Slots::Grid: {
            // The columns and rows, from where the children are; a new one past the last.
            std::map<int, std::pair<float, float>> columns, rows;
            struct Cell {
                int column, row, width, height;
            };
            std::vector<Cell> cells;
            for (Node* c : without(ui::childObjectsOfType(*at, ""))) {
                const Cell cell{number(ui::layoutProperty(*c, "column"), 0), number(ui::layoutProperty(*c, "row"), 0),
                                std::max(1, number(ui::layoutProperty(*c, "column-span"), 1)),
                                std::max(1, number(ui::layoutProperty(*c, "row-span"), 1))};
                cells.push_back(cell);
                const auto r = bounds(c);
                if (!r) continue;
                auto widen = [](std::map<int, std::pair<float, float>>& lines, int i, float from, float to) {
                    auto [it, fresh] = lines.try_emplace(i, from, to);
                    if (!fresh) it->second = {std::min(it->second.first, from), std::max(it->second.second, to)};
                };
                if (cell.width == 1) widen(columns, cell.column, r->origin.x, r->origin.x + r->size.width);
                if (cell.height == 1) widen(rows, cell.row, r->origin.y, r->origin.y + r->size.height);
            }
            auto locate = [](const std::map<int, std::pair<float, float>>& lines, double p, float lo, float hi) {
                if (lines.empty()) return std::pair{0, std::pair{lo, hi}};
                for (const auto& [i, span] : lines)
                    if (p >= span.first && p <= span.second) return std::pair{i, span};
                const auto& [first, firstSpan] = *lines.begin();
                if (p < firstSpan.first) return std::pair{std::max(0, first - 1), std::pair{lo, firstSpan.first}};
                for (const auto& [i, span] : lines)
                    if (p < span.first) return std::pair{i, span}; // in the spacing: the next one
                const auto& [last, lastSpan] = *lines.rbegin();
                return std::pair{last + 1, std::pair{lastSpan.second, hi}};
            };
            const auto [column, columnSpan] = locate(columns, x, bx, bx + bw);
            const auto [row, rowSpan] = locate(rows, y, by, by + bh);
            for (const Cell& c : cells)
                if (column >= c.column && column < c.column + c.width && row >= c.row && row < c.row + c.height)
                    return std::nullopt; // taken
            plan.placement.kind = Placement::Kind::Grid;
            plan.placement.column = column;
            plan.placement.row = row;
            plan.indicator = GRAPHENE_RECT_INIT(columnSpan.first, rowSpan.first, std::max(8.f, columnSpan.second - columnSpan.first),
                                                std::max(8.f, rowSpan.second - rowSpan.first));
            return plan;
        }
        case Slots::Pages:
            plan.placement.kind = Placement::Kind::Page;
            return plan;
        case Slots::None: break;
        }
    }
    return std::nullopt;
}

std::optional<Placement> DesignerPage::insertPlacement(const std::string& cls) {
    if (!doc_.interface()) return std::nullopt;
    Node* selection = doc_.find(selected_);
    if (isWindowClass(cls) || !selection) return placement(Placement::Kind::TopLevel);
    // Into the selection if it has room, else after it, else into the containers around it.
    Node* after = nullptr;
    for (Node* at = selection; at; after = at, at = UiDocument::parentObject(*at)) {
        const ui::ClassInfo* info = catalogInfo(UiDocument::className(*at));
        if (!info) continue;
        Placement p = placement(Placement::Kind::Child, at);
        auto nextAfter = [&](const std::string& type) -> Node* {
            const auto siblings = ui::childObjectsOfType(*at, type);
            const auto it = std::find(siblings.begin(), siblings.end(), after);
            return it != siblings.end() && it + 1 != siblings.end() ? *(it + 1) : nullptr;
        };
        switch (info->slots) {
        case Slots::Children:
            p.before = nextAfter("");
            return p;
        case Slots::Window:
        case Slots::Single:
            if (ui::childObjectsOfType(*at, "").empty() && !ui::propertyObject(*at, "child")) return p;
            break;
        case Slots::Overlay:
            if (!ui::childObjectsOfType(*at, "").empty() || ui::propertyObject(*at, "child")) p.name = "overlay";
            return p;
        case Slots::Properties:
            for (const auto& name : info->names)
                if (!ui::propertyObject(*at, name)) {
                    p.kind = Placement::Kind::Property;
                    p.name = name;
                    return p;
                }
            break;
        case Slots::Typed: {
            p.name = info->names.empty() ? "" : info->names.front();
            if (after)
                if (const std::string holder = ui::holderOf(*after); holder.rfind("child:", 0) == 0) p.name = holder.substr(6);
            p.before = nextAfter(p.name);
            return p;
        }
        case Slots::Grid: {
            int rows = 0;
            for (Node* c : ui::childObjectsOfType(*at, ""))
                rows = std::max(rows, number(ui::layoutProperty(*c, "row"), 0) +
                                          std::max(1, number(ui::layoutProperty(*c, "row-span"), 1)));
            p.kind = Placement::Kind::Grid;
            p.row = rows; // a new row at the bottom
            return p;
        }
        case Slots::Pages:
            p.kind = Placement::Kind::Page;
            return p;
        case Slots::None: break;
        }
    }
    return placement(Placement::Kind::TopLevel);
}

void DesignerPage::insertObject(const std::string& cls, const std::string& xml, const std::optional<Placement>& where) {
    if (!where || view_ != View::Design) return;
    int added = 0;
    try {
        edit([&] {
            const std::string object = xml.empty() ? ui::newObjectXml(doc_, cls, catalog().find(cls)) : ui::withUniqueIds(doc_, xml);
            if (Node* n = ui::place(doc_, *where, object, catalog())) added = n->uid;
        });
    } catch (const std::exception& e) {
        showMessage(Glib::ustring("Can't add ") + cls + ": " + e.what(), true);
        return;
    }
    if (added) select(added);
}

bool DesignerPage::drop(double x, double y) {
    const auto plan = planDrop(x, y);
    const std::string what = dragging_;
    dragging_.clear();
    dropPlan_.reset();
    selectionLayer_.queue_draw();
    if (!plan) return false;
    if (what.rfind("move:", 0) == 0) {
        const int uid = std::atoi(what.c_str() + 5);
        int moved = 0;
        edit([&] {
            if (Node* o = doc_.find(uid))
                if (Node* n = ui::moveObject(doc_, *o, plan->placement, catalog())) moved = n->uid;
        });
        if (moved) select(moved);
        return true;
    }
    insertObject(what.substr(4), {}, plan->placement);
    return true;
}

// ---- the clipboard and the context menu

bool DesignerPage::textFieldFocused() {
    auto* root = get_root();
    GtkWidget* focus = root ? gtk_root_get_focus(root->gobj()) : nullptr;
    return focus && (GTK_IS_TEXT(focus) || GTK_IS_TEXT_VIEW(focus)) && gtk_widget_is_ancestor(focus, GTK_WIDGET(gobj()));
}

void DesignerPage::copySelected() {
    Node* object = doc_.find(selected_);
    if (!object) return;
    get_clipboard()->set_text(ui::objectXml(*object));
    status_.emit("Copied " + UiDocument::className(*object));
}

void DesignerPage::cutSelected() {
    copySelected();
    deleteSelected();
}

void DesignerPage::deleteSelected() {
    Node* object = doc_.find(selected_);
    if (!object) return;
    Node* parent = UiDocument::parentObject(*object);
    const int parentUid = parent ? parent->uid : 0;
    edit([&] { ui::removeObject(doc_, *object); });
    select(parentUid);
}

void DesignerPage::paste() {
    get_clipboard()->read_text_async(sigc::track_obj(
        [this](Glib::RefPtr<Gio::AsyncResult>& result) {
            std::string text;
            try {
                text = get_clipboard()->read_text_finish(result);
            } catch (const Glib::Error&) {
                return;
            }
            try {
                const UiDocument pasted = UiDocument::parse(text);
                for (const auto& n : pasted.root().children)
                    if (n->isElement("object")) {
                        const std::string cls = UiDocument::className(*n);
                        insertObject(cls, UiDocument::textOf(*n), insertPlacement(cls));
                        return;
                    }
            } catch (const ui::ParseError&) {
            }
            showMessage("The clipboard doesn't hold a GTK object to paste.", true);
        },
        *this));
}

void DesignerPage::wrapSelected(const std::string& cls) {
    Node* object = doc_.find(selected_);
    if (!object || !UiDocument::parentObject(*object)) return;
    int container = 0;
    edit([&] {
        if (Node* c = ui::wrapObject(doc_, *object, ui::newObjectXml(doc_, cls, catalog().find(cls)), catalog())) container = c->uid;
    });
    if (container) select(container);
}

void DesignerPage::shiftSelected(int delta) {
    Node* object = doc_.find(selected_);
    if (!object) return;
    int moved = 0;
    edit([&] {
        if (Node* n = ui::shiftObject(doc_, *object, delta)) moved = n->uid;
    });
    if (moved) select(moved);
}

void DesignerPage::updateEditActions() {
    Node* object = doc_.find(selected_);
    const bool inside = object && UiDocument::parentObject(*object);
    bool up = false, down = false;
    if (inside && object->parent && object->parent->isElement("child")) {
        const auto siblings = ui::childObjectsOfType(*UiDocument::parentObject(*object), object->parent->attribute("type"));
        const auto it = std::find(siblings.begin(), siblings.end(), object);
        up = it != siblings.end() && it != siblings.begin();
        down = it != siblings.end() && it + 1 != siblings.end();
    }
    auto enable = [this](const char* name, bool on) {
        if (auto action = std::dynamic_pointer_cast<Gio::SimpleAction>(editActions_->lookup_action(name))) action->set_enabled(on);
    };
    enable("cut", object != nullptr);
    enable("copy", object != nullptr);
    enable("delete", object != nullptr);
    enable("wrap", inside);
    enable("move-up", up);
    enable("move-down", down);
}

void DesignerPage::showContextMenu(Gtk::Widget& over, double x, double y) {
    updateEditActions();
    graphene_point_t point = GRAPHENE_POINT_INIT(float(x), float(y)), inPage;
    if (!gtk_widget_compute_point(over.gobj(), GTK_WIDGET(gobj()), &point, &inPage)) return;
    contextMenu_.set_pointing_to(Gdk::Rectangle(int(inPage.x), int(inPage.y), 1, 1));
    contextMenu_.popup();
}

void DesignerPage::setupEditing() {
    editActions_ = Gio::SimpleActionGroup::create();
    editActions_->add_action("cut", [this] { cutSelected(); });
    editActions_->add_action("copy", [this] { copySelected(); });
    editActions_->add_action("paste", [this] { paste(); });
    editActions_->add_action("delete", [this] { deleteSelected(); });
    editActions_->add_action("move-up", [this] { shiftSelected(-1); });
    editActions_->add_action("move-down", [this] { shiftSelected(1); });
    editActions_->add_action_with_parameter("wrap", Glib::VariantType("s"), [this](const Glib::VariantBase& v) {
        wrapSelected(Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(v).get());
    });
    editActions_->add_action_with_parameter("write-code", Glib::VariantType("s"), [this](const Glib::VariantBase& v) {
        if (view_ == View::Xml && !applyXml()) return;
        writeCode(doc_, Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(v).get(), false);
    });
    editActions_->add_action("blueprint", [this] {
        if (view_ == View::Xml && !applyXml()) return;
        exportBlueprint();
    });
    insert_action_group("design", editActions_);

    auto menu = Gio::Menu::create();
    auto clipboard = Gio::Menu::create();
    clipboard->append("Cu_t", "design.cut");
    clipboard->append("_Copy", "design.copy");
    clipboard->append("_Paste", "design.paste");
    clipboard->append("_Delete", "design.delete");
    menu->append_section(clipboard);
    auto wrap = Gio::Menu::create();
    for (const auto& [label, cls] : {std::pair{"Wrap in _Box", "GtkBox"}, {"Wrap in _Grid", "GtkGrid"},
                                     {"Wrap in _Scrolled Window", "GtkScrolledWindow"}, {"Wrap in _Frame", "GtkFrame"}}) {
        auto item = Gio::MenuItem::create(label, "");
        item->set_action_and_target("design.wrap", Glib::Variant<Glib::ustring>::create(cls));
        wrap->append_item(item);
    }
    menu->append_section(wrap);
    auto order = Gio::Menu::create();
    order->append("Move _Up", "design.move-up");
    order->append("Move _Down", "design.move-down");
    menu->append_section(order);
    contextMenu_.set_menu_model(menu);
    contextMenu_.set_has_arrow(false);
    contextMenu_.set_halign(Gtk::Align::START);
    contextMenu_.set_parent(*this);

    auto rightClick = Gtk::GestureClick::create();
    rightClick->set_button(GDK_BUTTON_SECONDARY);
    rightClick->signal_pressed().connect([this](int, double x, double y) {
        if (const int uid = objectAt(x, y)) select(uid);
        showContextMenu(selectionLayer_, x, y);
    });
    selectionLayer_.add_controller(rightClick);
    auto outlineRightClick = Gtk::GestureClick::create();
    outlineRightClick->set_button(GDK_BUTTON_SECONDARY);
    outlineRightClick->signal_pressed().connect([this](int, double x, double y) {
        if (Gtk::ListBoxRow* row = outline_.get_row_at_y(int(y))) outline_.select_row(*row);
        showContextMenu(outline_, x, y);
    });
    outline_.add_controller(outlineRightClick);

    // Widgets dragged from the palette, or around the design.
    auto target = Gtk::DropTarget::create(G_TYPE_STRING, Gdk::DragAction::COPY | Gdk::DragAction::MOVE);
    target->signal_motion().connect(
        [this](double x, double y) {
            dropPlan_ = planDrop(x, y);
            selectionLayer_.queue_draw();
            if (!dropPlan_) return Gdk::DragAction(0);
            return dragging_.rfind("move:", 0) == 0 ? Gdk::DragAction::MOVE : Gdk::DragAction::COPY;
        },
        false);
    target->signal_leave().connect(
        [this] {
            dropPlan_.reset();
            selectionLayer_.queue_draw();
        },
        false);
    target->signal_drop().connect([this](const Glib::ValueBase&, double x, double y) { return drop(x, y); }, false);
    selectionLayer_.add_controller(target);

    auto source = Gtk::DragSource::create();
    source->set_actions(Gdk::DragAction::MOVE);
    source->signal_prepare().connect(
        [this, src = source.get()](double x, double y) -> Glib::RefPtr<Gdk::ContentProvider> {
            const int uid = objectAt(x, y);
            Node* n = doc_.find(uid);
            // Top-level objects stay where they are; everything else can move.
            if (!n || !UiDocument::parentObject(*n) || view_ != View::Design) return {};
            select(uid);
            dragging_ = "move:" + std::to_string(uid);
            if (auto it = widgets_.find(uid); it != widgets_.end()) {
                graphene_point_t at = GRAPHENE_POINT_INIT(float(x), float(y)), inWidget = GRAPHENE_POINT_INIT(0, 0);
                if (!gtk_widget_compute_point(GTK_WIDGET(selectionLayer_.gobj()), it->second, &at, &inWidget))
                    inWidget = GRAPHENE_POINT_INIT(0, 0);
                GdkPaintable* picture = gtk_widget_paintable_new(it->second);
                gtk_drag_source_set_icon(src->gobj(), picture, int(inWidget.x), int(inWidget.y));
                g_object_unref(picture);
            }
            return textContent(dragging_);
        },
        false);
    source->signal_drag_end().connect(
        [this](const Glib::RefPtr<Gdk::Drag>&, bool) {
            dragging_.clear();
            dropPlan_.reset();
            selectionLayer_.queue_draw();
        },
        false);
    selectionLayer_.add_controller(source);
}

} // namespace kevc
