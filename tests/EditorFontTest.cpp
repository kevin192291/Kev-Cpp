#include "core/EditorFont.h"

#include <gtest/gtest.h>

using namespace kevc;
using Slant = EditorFont::Slant;

TEST(EditorFont, DefaultsWithoutSettings) {
    Settings s; // in memory
    const EditorFont font = loadEditorFont(s);
    EXPECT_EQ(font.family, "DejaVu Sans Mono");
    EXPECT_EQ(font.size, 11);
    EXPECT_EQ(font.weight, 400);
    EXPECT_EQ(font.slant, Slant::Normal);
}

TEST(EditorFont, SavesAndLoadsBack) {
    Settings s;
    EditorFont font;
    font.family = "Fira Code";
    font.size = 14;
    font.weight = 700;
    font.slant = Slant::Italic;
    saveEditorFont(s, font);
    EXPECT_EQ(loadEditorFont(s), font);
    EXPECT_EQ(s.getString("/editor/font/style"), "italic");
}

TEST(EditorFont, KeepsOtherKeysUnderFont) {
    Settings s;
    s.set("/editor/font/ligatures", true); // from a newer Kev-C++
    saveEditorFont(s, EditorFont{});
    EXPECT_TRUE(s.get<bool>("/editor/font/ligatures", false));
}

TEST(EditorFont, UnusableValuesFallBack) {
    Settings s;
    s.set("/editor/font/family", "   ");
    s.set("/editor/font/size", 500);
    s.set("/editor/font/weight", "bold"); // wrong type
    s.set("/editor/font/style", "sideways");
    const EditorFont font = loadEditorFont(s);
    EXPECT_EQ(font.family, "DejaVu Sans Mono");
    EXPECT_EQ(font.size, EditorFont::kMaxSize);
    EXPECT_EQ(font.weight, 400);
    EXPECT_EQ(font.slant, Slant::Normal);

    s.set("/editor/font/size", 1);
    EXPECT_EQ(loadEditorFont(s).size, EditorFont::kMinSize);
}

TEST(EditorFont, ZoomStaysInRange) {
    EXPECT_EQ(zoomedSize(11, 1), 12);
    EXPECT_EQ(zoomedSize(11, -1), 10);
    EXPECT_EQ(zoomedSize(EditorFont::kMaxSize, 1), EditorFont::kMaxSize);
    EXPECT_EQ(zoomedSize(EditorFont::kMinSize, -1), EditorFont::kMinSize);
    EXPECT_EQ(zoomedSize(100, -1), EditorFont::kMaxSize);
}

TEST(EditorFont, Css) {
    EditorFont font;
    EXPECT_EQ(editorFontCss(font), ".kevc-editor { font-family: \"DejaVu Sans Mono\", \"Liberation Mono\", monospace; "
                                   "font-size: 11pt; font-weight: 400; font-style: normal; }\n");
    font.family = "Source Code Pro";
    font.size = 16;
    font.weight = 600;
    font.slant = Slant::Oblique;
    EXPECT_EQ(editorFontCss(font, ".x"),
              ".x { font-family: \"Source Code Pro\", \"DejaVu Sans Mono\", \"Liberation Mono\", "
              "monospace; font-size: 16pt; font-weight: 600; font-style: oblique; }\n");
}

TEST(EditorFont, CssEscapesTheFamily) {
    EditorFont font;
    font.family = "Odd \"Name\" \\ }\n.x{";
    const std::string css = editorFontCss(font);
    EXPECT_NE(css.find("\"Odd \\\"Name\\\" \\\\ }.x{\""), std::string::npos) << css;
    EXPECT_EQ(css.find('\n'), css.size() - 1);
}

TEST(EditorFont, FacesSortByWeightThenSlant) {
    std::vector<FontFace> faces = {
        {"Bold Oblique", 700, Slant::Oblique}, {"Bold", 700, Slant::Normal},       {"Oblique", 400, Slant::Oblique},
        {"Book", 400, Slant::Normal},          {"ExtraLight", 200, Slant::Normal},
    };
    sortFaces(faces);
    std::vector<std::string> names;
    for (const auto& f : faces) names.push_back(f.name);
    EXPECT_EQ(names, (std::vector<std::string>{"ExtraLight", "Book", "Oblique", "Bold", "Bold Oblique"}));
}

TEST(EditorFont, ClosestFace) {
    const std::vector<FontFace> faces = {
        {"Regular", 400, Slant::Normal},
        {"Oblique", 400, Slant::Oblique},
        {"Bold", 700, Slant::Normal},
        {"Bold Oblique", 700, Slant::Oblique},
    };
    EXPECT_EQ(closestFace(faces, 400, Slant::Normal), 0);
    EXPECT_EQ(closestFace(faces, 700, Slant::Oblique), 3);
    // Italic asked, the family has only oblique faces.
    EXPECT_EQ(closestFace(faces, 400, Slant::Italic), 1);
    // A medium weight goes to the nearer one.
    EXPECT_EQ(closestFace(faces, 600, Slant::Normal), 2);
    // Upright matters more than weight.
    EXPECT_EQ(closestFace({{"Light Italic", 300, Slant::Italic}, {"Black", 900, Slant::Normal}}, 300, Slant::Normal),
              1);
    EXPECT_EQ(closestFace({}, 400, Slant::Normal), -1);
}
