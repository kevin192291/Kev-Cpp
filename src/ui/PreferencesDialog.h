#pragma once

#include "ui/PreferencesDialog.ui.h"

#include "core/EditorFont.h"

#include <functional>
#include <string>
#include <vector>

namespace kevc {

// View → Preferences: the editors' font, font style and text size. Its layout is
// PreferencesDialog.ui (edit it on Kev-C++'s Design page); this class fills the font and style
// lists from Pango and reports every change through `onChange` at once, so the editors behind it
// (and the preview, which has the editors' CSS class) show it while the dialog is open. It deletes
// itself when closed.
class PreferencesDialog : public PreferencesDialogUi {
public:
    using FontHandler = std::function<void(const EditorFont&)>;

    static PreferencesDialog* create(Gtk::Window& parent, const EditorFont& font, FontHandler onChange) {
        return PreferencesDialogUi::create<PreferencesDialog>(parent, font, std::move(onChange));
    }
    PreferencesDialog(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder, Gtk::Window& parent,
                      const EditorFont& font, FontHandler onChange);

protected:
    void on_family_changed() override;
    void on_monospace_only_toggled() override;
    void on_style_changed() override;
    void on_size_changed() override;
    void on_reset_clicked() override;
    void on_close_clicked() override;

private:
    struct Family {
        std::string name;
        Glib::RefPtr<Pango::FontFamily> pango; // null for a family that isn't installed
    };

    // The controls from font_; the family list honours the check box.
    void showFont();
    void fillFamilies();
    // The style list for font_.family, on the face closest to font_'s weight and slant.
    void fillFaces();
    void changed();

    EditorFont font_;
    FontHandler onChange_;
    std::vector<Family> installed_; // every family, sorted by name
    std::vector<Family> families_;  // the family drop-down's choices, in order
    std::vector<FontFace> faces_;   // the style drop-down's choices, in order
    // Set while code changes the controls, whose signals would otherwise report it as the user's.
    bool syncing_ = false;
};

} // namespace kevc
