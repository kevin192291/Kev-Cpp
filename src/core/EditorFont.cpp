#include "core/EditorFont.h"

#include <algorithm>
#include <cstdlib>
#include <tuple>

namespace kevc {

namespace {

const char* slantName(EditorFont::Slant slant) {
    switch (slant) {
    case EditorFont::Slant::Oblique: return "oblique";
    case EditorFont::Slant::Italic: return "italic";
    case EditorFont::Slant::Normal: break;
    }
    return "normal";
}

EditorFont::Slant slantFromName(const std::string& name) {
    if (name == "oblique") return EditorFont::Slant::Oblique;
    if (name == "italic") return EditorFont::Slant::Italic;
    return EditorFont::Slant::Normal;
}

// A CSS string: quotes and backslashes escaped, control characters (which a font name never has,
// but a hand-edited settings file might) dropped so they can't end the rule.
std::string cssString(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        if (static_cast<unsigned char>(c) < 0x20) continue;
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

bool slanted(EditorFont::Slant slant) {
    return slant != EditorFont::Slant::Normal;
}

} // namespace

EditorFont loadEditorFont(const Settings& settings) {
    const EditorFont defaults;
    EditorFont font;
    font.family = settings.getString("/editor/font/family", defaults.family);
    if (font.family.find_first_not_of(" \t") == std::string::npos) font.family = defaults.family;
    font.size = std::clamp(settings.get<int>("/editor/font/size", defaults.size), EditorFont::kMinSize,
                           EditorFont::kMaxSize);
    font.weight = std::clamp(settings.get<int>("/editor/font/weight", defaults.weight), 100, 1000);
    font.slant = slantFromName(settings.getString("/editor/font/style", "normal"));
    return font;
}

void saveEditorFont(Settings& settings, const EditorFont& font) {
    // One write for the four values (Ctrl+Plus held down saves at every step), keeping any other
    // key a newer Kev-C++ put there.
    nlohmann::json value = settings.get<nlohmann::json>("/editor/font", nlohmann::json::object());
    if (!value.is_object()) value = nlohmann::json::object();
    value["family"] = font.family;
    value["size"] = font.size;
    value["weight"] = font.weight;
    value["style"] = slantName(font.slant);
    settings.set("/editor/font", value);
}

int zoomedSize(int size, int steps) {
    return std::clamp(size + steps, EditorFont::kMinSize, EditorFont::kMaxSize);
}

std::string editorFontCss(const EditorFont& font, const std::string& selector) {
    std::string families = cssString(font.family);
    for (const char* fallback : {"DejaVu Sans Mono", "Liberation Mono"})
        if (font.family != fallback) families += std::string(", ") + cssString(fallback);
    families += ", monospace";
    return selector + " { font-family: " + families + "; font-size: " + std::to_string(font.size) +
           "pt; font-weight: " + std::to_string(font.weight) + "; font-style: " + slantName(font.slant) + "; }\n";
}

void sortFaces(std::vector<FontFace>& faces) {
    std::stable_sort(faces.begin(), faces.end(), [](const FontFace& a, const FontFace& b) {
        return std::tuple(a.weight, static_cast<int>(a.slant)) < std::tuple(b.weight, static_cast<int>(b.slant));
    });
}

int closestFace(const std::vector<FontFace>& faces, int weight, EditorFont::Slant slant) {
    int best = -1;
    long bestScore = 0;
    for (size_t i = 0; i < faces.size(); ++i) {
        const FontFace& f = faces[i];
        // Upright against slanted outweighs any weight difference; oblique against italic is
        // nearly the same thing (many monospace fonts have only one of them).
        long score = std::labs(static_cast<long>(f.weight) - weight);
        if (slanted(f.slant) != slanted(slant)) score += 100000;
        else if (f.slant != slant) score += 1;
        if (best < 0 || score < bestScore) {
            best = static_cast<int>(i);
            bestScore = score;
        }
    }
    return best;
}

} // namespace kevc
