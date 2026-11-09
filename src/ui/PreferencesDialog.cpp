#include "ui/PreferencesDialog.h"

#include "ui/Dialogs.h"

#include <algorithm>

namespace kevc {

namespace {

using Slant = EditorFont::Slant;

// CSS matches family names without regard to case, so a hand-written "dejavu sans mono" counts.
bool sameFamily(const Glib::ustring& a, const Glib::ustring& b) {
    return a.casefold() == b.casefold();
}

Slant slantOf(Pango::Style style) {
    switch (style) {
    case Pango::Style::OBLIQUE: return Slant::Oblique;
    case Pango::Style::ITALIC: return Slant::Italic;
    default: return Slant::Normal;
    }
}

// The styles of a family Pango doesn't have: the CSS asks the fallback font for these.
std::vector<FontFace> genericFaces() {
    return {{"Regular", 400, Slant::Normal},
            {"Italic", 400, Slant::Italic},
            {"Bold", 700, Slant::Normal},
            {"Bold Italic", 700, Slant::Italic}};
}

} // namespace

PreferencesDialog::PreferencesDialog(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder,
                                     Gtk::Window& parent, const EditorFont& font, FontHandler onChange)
    : PreferencesDialogUi(object, builder), font_(font), onChange_(std::move(onChange)) {
    set_transient_for(parent);
    for (const auto& family : get_pango_context()->list_families()) installed_.push_back({family->get_name(), family});
    std::sort(installed_.begin(), installed_.end(), [](const Family& a, const Family& b) {
        return Glib::ustring(a.name).casefold() < Glib::ustring(b.name).casefold();
    });
    // Typing in the font list finds a family by any part of its name ("mono", "code").
    family_->set_expression(Gtk::PropertyExpression<Glib::ustring>::create(GTK_TYPE_STRING_OBJECT, "string"));
#if GTK_CHECK_VERSION(4, 12, 0)
    gtk_drop_down_set_search_match_mode(family_->gobj(), GTK_STRING_FILTER_MATCH_MODE_SUBSTRING);
#endif
    size_->set_range(EditorFont::kMinSize, EditorFont::kMaxSize);

    // A proportional font chosen earlier: list every font, so the check box doesn't hide the choice.
    const auto current = std::find_if(installed_.begin(), installed_.end(),
                                      [&](const Family& f) { return sameFamily(f.name, font_.family); });
    syncing_ = true;
    monospaceOnly_->set_active(current == installed_.end() || current->pango->is_monospace());
    syncing_ = false;
    showFont();

    closeOnEscape(*this);
    deleteWhenHidden(this);
    close_->grab_focus();
}

void PreferencesDialog::showFont() {
    syncing_ = true;
    fillFamilies();
    fillFaces();
    size_->set_value(font_.size);
    syncing_ = false;
}

void PreferencesDialog::fillFamilies() {
    families_.clear();
    bool listed = false;
    for (const Family& f : installed_) {
        const bool current = sameFamily(f.name, font_.family);
        listed = listed || current;
        if (current || !monospaceOnly_->get_active() || f.pango->is_monospace()) families_.push_back(f);
    }
    // Settings from another computer can name a font this one lacks: show it as the choice anyway.
    if (!listed) families_.insert(families_.begin(), Family{font_.family, {}});

    std::vector<Glib::ustring> labels;
    guint selected = 0;
    for (guint i = 0; i < families_.size(); ++i) {
        labels.push_back(families_[i].pango ? families_[i].name : families_[i].name + " (not installed)");
        if (sameFamily(families_[i].name, font_.family)) selected = i;
    }
    family_->set_model(Gtk::StringList::create(labels));
    family_->set_selected(selected);
}

void PreferencesDialog::fillFaces() {
    faces_.clear();
    Glib::RefPtr<Pango::FontFamily> family;
    for (const Family& f : installed_)
        if (sameFamily(f.name, font_.family)) family = f.pango;
    if (family) {
        const auto faces = family->list_faces();
        // Real faces first: a synthesized one (Pango slanting the upright face) stays only where the
        // family has no face of its own with that weight and slant.
        for (bool synthesized : {false, true})
            for (const auto& face : faces) {
                if (face->is_synthesized() != synthesized) continue;
                const Pango::FontDescription d = face->describe();
                FontFace f{face->get_name(), static_cast<int>(d.get_weight()), slantOf(d.get_style())};
                if (std::none_of(faces_.begin(), faces_.end(),
                                 [&](const FontFace& g) { return g.weight == f.weight && g.slant == f.slant; }))
                    faces_.push_back(f);
            }
        sortFaces(faces_);
    }
    if (faces_.empty()) faces_ = genericFaces();

    std::vector<Glib::ustring> labels;
    for (const FontFace& f : faces_) labels.push_back(f.name);
    style_->set_model(Gtk::StringList::create(labels));
    style_->set_selected(static_cast<guint>(std::max(0, closestFace(faces_, font_.weight, font_.slant))));
}

void PreferencesDialog::changed() {
    if (onChange_) onChange_(font_);
}

void PreferencesDialog::on_family_changed() {
    if (syncing_) return;
    const guint i = family_->get_selected();
    if (i >= families_.size()) return;
    font_.family = families_[i].name;
    // The new family's face nearest to the old style: Bold stays bold if it can.
    syncing_ = true;
    fillFaces();
    syncing_ = false;
    const FontFace& face = faces_[std::min<size_t>(style_->get_selected(), faces_.size() - 1)];
    font_.weight = face.weight;
    font_.slant = face.slant;
    changed();
}

void PreferencesDialog::on_monospace_only_toggled() {
    if (syncing_) return;
    syncing_ = true;
    fillFamilies();
    syncing_ = false;
}

void PreferencesDialog::on_style_changed() {
    if (syncing_) return;
    const guint i = style_->get_selected();
    if (i >= faces_.size()) return;
    font_.weight = faces_[i].weight;
    font_.slant = faces_[i].slant;
    changed();
}

void PreferencesDialog::on_size_changed() {
    if (syncing_) return;
    font_.size = size_->get_value_as_int();
    changed();
}

void PreferencesDialog::on_reset_clicked() {
    font_ = EditorFont{};
    syncing_ = true;
    monospaceOnly_->set_active(true);
    syncing_ = false;
    showFont();
    changed();
}

void PreferencesDialog::on_close_clicked() {
    set_visible(false);
}

} // namespace kevc
