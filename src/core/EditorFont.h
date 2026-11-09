#pragma once

#include "core/Settings.h"

#include <string>
#include <vector>

namespace kevc {

// The editors' text: a font family, a face of it (weight and slant) and a size in points. Kept in
// settings under /editor/font; View → Preferences sets all of it, View → Larger/Smaller Text
// (Ctrl+Plus, Ctrl+Minus) the size.
struct EditorFont {
    enum class Slant { Normal, Oblique, Italic };

    static constexpr int kMinSize = 6;
    static constexpr int kMaxSize = 72;
    static constexpr int kDefaultSize = 11;

    std::string family = "DejaVu Sans Mono";
    int size = kDefaultSize; // points
    int weight = 400;        // as in CSS and Pango: 400 regular, 700 bold
    Slant slant = Slant::Normal;

    bool operator==(const EditorFont&) const = default;
};

// Missing or unusable values (an empty family, a size out of range) give the defaults.
EditorFont loadEditorFont(const Settings& settings);
void saveEditorFont(Settings& settings, const EditorFont& font);

// `size` made `steps` points larger (negative: smaller), within kMinSize..kMaxSize.
int zoomedSize(int size, int steps);

// The CSS rule that gives `selector` the font. Monospace fonts follow the family, so a family that
// isn't installed (settings copied from another computer) still gives a code font.
std::string editorFontCss(const EditorFont& font, const std::string& selector = ".kevc-editor");

// One face of a family, as Pango lists it: the Style drop-down's choices.
struct FontFace {
    std::string name; // "Bold Oblique"
    int weight = 400;
    EditorFont::Slant slant = EditorFont::Slant::Normal;
};
// The order the Style drop-down shows them in: by weight, upright before slanted.
void sortFaces(std::vector<FontFace>& faces);
// The face nearest to `weight` and `slant` (another family's face, when the family changes):
// upright or slanted as asked if the family has one, then the closest weight. -1 for no faces.
int closestFace(const std::vector<FontFace>& faces, int weight, EditorFont::Slant slant);

} // namespace kevc
