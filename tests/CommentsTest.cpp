#include "core/Comments.h"

#include <gtest/gtest.h>

using namespace kevc;
using namespace kevc::comments;
using format::applyTextEdits;

namespace {

const Style cpp{"//", "/*", "*/"};
const Style css{"", "/*", "*/"};
const Style html{"", "<!--", "-->"};
const Style python{"#", "", ""};

std::string after(const std::string& text, int first, int last, const std::string& language, const Style& style) {
    const Toggle t = toggle(text, first, last, language, style);
    EXPECT_FALSE(t.edits.empty()) << t.message;
    return applyTextEdits(text, t.edits);
}

// The byte offset of an LSP position in `text`: how much deleting everything before it removes.
size_t offsetAt(const std::string& text, int line, int utf16) {
    return text.size() - applyTextEdits(text, {{0, 0, line, utf16, ""}}).size();
}

// Where a mark at byte `mark` ends up after `edits`, the way GTK moves a breakpoint's: EditorTab
// deletes each range and inserts at its start, and a left-gravity mark there stays before the text.
size_t moveMark(const std::string& text, size_t mark, const std::vector<format::TextEdit>& edits) {
    for (const auto& e : format::lastFirst(edits)) {
        const size_t start = offsetAt(text, e.startLine, e.startUtf16), end = offsetAt(text, e.endLine, e.endUtf16);
        if (mark > end) mark = mark - (end - start) + e.newText.size();
        else if (mark > start) mark = start;
    }
    return mark;
}

} // namespace

TEST(Comments, OneLineGetsALineCommentAfterItsIndentation) {
    const std::string text = "int main() {\n    return 0;\n}\n";
    const std::string commented = after(text, 1, 1, "cpp", cpp);
    EXPECT_EQ(commented, "int main() {\n    // return 0;\n}\n");
    EXPECT_EQ(after(commented, 1, 1, "cpp", cpp), text);
    // Written without the space, it comes off too.
    EXPECT_EQ(after("    //return 0;\n", 0, 0, "cpp", cpp), "    return 0;\n");
    // A blank line gets a comment to write.
    EXPECT_EQ(after("a;\n    \nb;\n", 1, 1, "cpp", cpp), "a;\n    // \nb;\n");
    EXPECT_FALSE(toggle(text, 1, 1, "cpp", cpp).select.has_value());
}

TEST(Comments, SeveralLinesGetABlockCommentOnLinesOfTheirOwn) {
    const std::string text = "void f() {\n    int a = 1;\n        int b = 2;\n}\n";
    const Toggle t = toggle(text, 1, 2, "cpp", cpp);
    const std::string commented = applyTextEdits(text, t.edits);
    EXPECT_EQ(commented, "void f() {\n    /*\n    int a = 1;\n        int b = 2;\n    */\n}\n");
    ASSERT_TRUE(t.select.has_value());
    EXPECT_EQ(*t.select, (std::pair{1, 4})); // the markers too, so Ctrl+/ again takes it off

    const Toggle back = toggle(commented, 1, 4, "cpp", cpp);
    EXPECT_EQ(applyTextEdits(commented, back.edits), text);
    ASSERT_TRUE(back.select.has_value());
    EXPECT_EQ(*back.select, (std::pair{1, 2}));
}

TEST(Comments, BlockCommentsAtTheTopAndBottomOfTheFile) {
    EXPECT_EQ(after("a;\nb;", 0, 1, "c", cpp), "/*\na;\nb;\n*/");
    EXPECT_EQ(after("/*\na;\nb;\n*/", 0, 3, "c", cpp), "a;\nb;");
    EXPECT_EQ(after("/*\n*/\nx;\n", 0, 1, "c", cpp), "\nx;\n"); // an empty block
}

TEST(Comments, BlankLinesAtTheSelectionsEdgesStayOutside) {
    const std::string text = "a;\n\nb;\nc;\n\nd;\n";
    const Toggle t = toggle(text, 1, 4, "cpp", cpp);
    EXPECT_EQ(applyTextEdits(text, t.edits), "a;\n\n/*\nb;\nc;\n*/\n\nd;\n");
    ASSERT_TRUE(t.select.has_value());
    EXPECT_EQ(*t.select, (std::pair{1, 6}));
    // Only blank lines: nothing to do.
    const Toggle blank = toggle("a;\n\n  \nb;\n", 1, 2, "cpp", cpp);
    EXPECT_TRUE(blank.edits.empty());
    EXPECT_FALSE(blank.message.empty());
    // One line and blank ones: that line's comment.
    EXPECT_EQ(after("\n    x;\n\n", 0, 2, "cpp", cpp), "\n    // x;\n\n");
}

TEST(Comments, LinesHoldingTheBlocksEndGetLineComments) {
    // A /* … */ around these would end at the first */.
    const std::string text = "  int a; /* one */\n    int b;\n";
    const std::string commented = after(text, 0, 1, "cpp", cpp);
    EXPECT_EQ(commented, "  // int a; /* one */\n  //   int b;\n");
    EXPECT_EQ(after(commented, 0, 1, "cpp", cpp), text); // every line a // comment: they come off
}

TEST(Comments, LinesCommentedOneByOneComeBackOneByOne) {
    EXPECT_EQ(after("// a;\n\n//b;\n", 0, 2, "cpp", cpp), "a;\n\nb;\n");
    EXPECT_EQ(after("/* a: 1; */\n/* b: 2; */\n", 0, 1, "css", css), "a: 1;\nb: 2;\n");
    // Some commented, some not: the lot is commented out.
    EXPECT_EQ(after("// a;\nb;\n", 0, 1, "cpp", cpp), "/*\n// a;\nb;\n*/\n");
}

TEST(Comments, ABlockCommentWrittenInlineComesOff) {
    EXPECT_EQ(after("  /* a;\n  b; */\n", 0, 1, "cpp", cpp), "  a;\n  b;\n");
    EXPECT_EQ(after("/*a;\nb;*/\n", 0, 1, "cpp", cpp), "a;\nb;\n");
    // Two comments, not one around both: a // on each line instead.
    EXPECT_EQ(after("/* a */\nx;\n/* b */\n", 0, 2, "cpp", cpp), "// /* a */\n// x;\n// /* b */\n");
}

TEST(Comments, TheCursorOnABlocksMarkerTakesItOff) {
    const std::string text = "x;\n    /*\n    a;\n    b;\n    */\ny;\n";
    EXPECT_EQ(after(text, 1, 1, "cpp", cpp), "x;\n    a;\n    b;\ny;\n");
    EXPECT_EQ(after(text, 4, 4, "cpp", cpp), "x;\n    a;\n    b;\ny;\n");
    // A doc comment's start isn't a commented-out block's.
    EXPECT_EQ(after("/**\n * Doc.\n */\n", 0, 0, "cpp", cpp), "// /**\n * Doc.\n */\n");
    EXPECT_EQ(after("/**\n * Doc.\n */\n", 2, 2, "cpp", cpp), "/**\n * Doc.\n // */\n");
}

TEST(Comments, CssAndHtmlLinesGetABlockCommentAroundThem) {
    const std::string text = "p {\n    color: red;  \n}\n";
    const std::string commented = after(text, 1, 1, "css", css);
    EXPECT_EQ(commented, "p {\n    /* color: red; */  \n}\n");
    EXPECT_EQ(after(commented, 1, 1, "css", css), text);
    EXPECT_EQ(after("/*color: red;*/\n", 0, 0, "css", css), "color: red;\n");

    EXPECT_EQ(after("  <p>Hi</p>\n", 0, 0, "html", html), "  <!-- <p>Hi</p> -->\n");
    EXPECT_EQ(after("  <!-- <p>Hi</p> -->\n", 0, 0, "html", html), "  <p>Hi</p>\n");
    EXPECT_EQ(after("<ul>\n  <li>a</li>\n</ul>\n", 0, 2, "html", html), "<!--\n<ul>\n  <li>a</li>\n</ul>\n-->\n");

    // A comment inside would end the new one early, and CSS has no line comments to use instead.
    const Toggle nested = toggle("a { b: c; } /* d */ e { }\n", 0, 0, "css", css);
    EXPECT_TRUE(nested.edits.empty());
    EXPECT_NE(nested.message.find("*/"), std::string::npos);
    EXPECT_TRUE(toggle("<p>\n<!-- x -->\n</p>\n", 0, 2, "html", html).edits.empty());
}

TEST(Comments, ScriptsAndStylesInHtmlAndSvelteCommentInTheirOwnLanguage) {
    const std::string page = "<p>Hi</p>\n"           // 0
                             "<script lang=\"ts\">\n" // 1
                             "    let a = 1;\n"       // 2
                             "</script>\n"            // 3
                             "<style>\n"              // 4
                             "    p { color: red; }\n" // 5
                             "</style>\n"             // 6
                             "<style lang=\"scss\">\n" // 7
                             "    p { b: c; }\n"      // 8
                             "</style>\n";            // 9
    for (const char* language : {"html", "svelte"}) {
        SCOPED_TRACE(language);
        auto line = [&](int i) {
            const std::string out = after(page, i, i, language, html);
            size_t at = 0;
            for (int n = 0; n < i; ++n) at = out.find('\n', at) + 1;
            return out.substr(at, out.find('\n', at) - at);
        };
        EXPECT_EQ(line(0), "<!-- <p>Hi</p> -->");
        EXPECT_EQ(line(1), "<!-- <script lang=\"ts\"> -->");
        EXPECT_EQ(line(2), "    // let a = 1;");
        EXPECT_EQ(line(3), "<!-- </script> -->");
        EXPECT_EQ(line(5), "    /* p { color: red; } */");
        EXPECT_EQ(line(6), "<!-- </style> -->");
        EXPECT_EQ(line(8), "    // p { b: c; }");
    }
    // The whole script element is markup to comment out.
    EXPECT_EQ(after("<script>\nlet a;\n</script>\n", 0, 2, "svelte", html), "<!--\n<script>\nlet a;\n</script>\n-->\n");
    // Several lines of a script: a JavaScript block comment.
    EXPECT_EQ(after("<script>\na;\nb;\n</script>\n", 1, 2, "svelte", html), "<script>\n/*\na;\nb;\n*/\n</script>\n");
    // Other files keep their own style, whatever they hold.
    EXPECT_EQ(styleAt("<script>\nx\n", 9, "cpp", cpp), cpp);
}

TEST(Comments, SectionsOfAnHtmlFile) {
    const std::string page = "<!-- <script> -->\n<script src=\"a.js\" />\n<SCRIPT type=module>\nx\n</script >\ny\n<scripts>\nz\n";
    auto at = [&](const std::string& what) { return sectionAt(page, page.find(what)); };
    EXPECT_EQ(sectionAt(page, 0), Section::Markup);
    EXPECT_EQ(at("\n<script src"), Section::Markup); // the <script> in the comment doesn't count
    EXPECT_EQ(at("\n<SCRIPT"), Section::Markup);     // a self-closing one has no inside
    EXPECT_EQ(at("type=module"), Section::Markup);   // inside the opening tag
    EXPECT_EQ(at("x\n"), Section::Script);
    EXPECT_EQ(at("</script >"), Section::Markup);
    EXPECT_EQ(at("y\n"), Section::Markup);
    EXPECT_EQ(at("z\n"), Section::Markup); // <scripts> is another element
    EXPECT_EQ(sectionAt("<style>\na {}\n", 9), Section::Style);
}

TEST(Comments, LanguagesWithOnlyLineComments) {
    const std::string text = "if x:\n    a()\n    b()\n";
    const std::string commented = after(text, 1, 2, "python", python);
    EXPECT_EQ(commented, "if x:\n    # a()\n    # b()\n");
    EXPECT_EQ(after(commented, 1, 2, "python", python), text);
}

TEST(Comments, FilesWithoutComments) {
    EXPECT_TRUE(fileStyle("package.json", "json", {}).none());
    EXPECT_FALSE(toggle("{\n}\n", 0, 0, "json", {}).message.empty());
    EXPECT_FALSE(toggle("text\n", 0, 0, "", {}).message.empty());
    // JSON with comments takes JavaScript's.
    EXPECT_EQ(fileStyle("/p/tsconfig.app.json", "json", {}), cpp);
    EXPECT_EQ(fileStyle("/p/.vscode/settings.json", "json", {}), cpp);
    EXPECT_EQ(fileStyle("/p/x.jsonc", "", {}), cpp);
    EXPECT_EQ(fileStyle("/p/a.cpp", "cpp", cpp), cpp);
}

TEST(Comments, ColumnsCountUtf16AndLineEndsAreKept) {
    // The closing marker goes after characters outside ASCII, counted as LSP (and GTK) columns.
    EXPECT_EQ(after("a { content: \"日本 🎉\"; }\n", 0, 0, "css", css), "/* a { content: \"日本 🎉\"; } */\n");
    EXPECT_EQ(after("é;\n🎉;\n", 0, 1, "cpp", cpp), "/*\né;\n🎉;\n*/\n");
    // Windows line ends stay Windows line ends.
    EXPECT_EQ(after("x;\r\na;\r\nb;\r\n", 1, 2, "cpp", cpp), "x;\r\n/*\r\na;\r\nb;\r\n*/\r\n");
    EXPECT_EQ(after("x;\r\n/*\r\na;\r\nb;\r\n*/\r\n", 1, 4, "cpp", cpp), "x;\r\na;\r\nb;\r\n");
}

TEST(Comments, BreakpointsStayOnTheirLines) {
    // Marks at the start of each line, as breakpoints are: each stays at its own line's text.
    const std::string text = "x;\n    a;\n    b;\ny;\n";
    for (const auto& [first, last] : {std::pair{1, 2}, std::pair{1, 1}, std::pair{1, 3}, std::pair{0, 0}}) {
        const Toggle t = toggle(text, first, last, "cpp", cpp);
        const std::string out = applyTextEdits(text, t.edits);
        for (const std::string line : {"x;", "    a;", "    b;", "y;"}) {
            const size_t mark = moveMark(text, text.find(line), t.edits);
            const size_t lineStart = out.rfind('\n', mark == 0 ? 0 : mark - 1);
            const std::string there = out.substr(mark == 0 ? 0 : lineStart + 1);
            EXPECT_NE(there.substr(0, there.find('\n')).find(line.substr(line.find_first_not_of(' '))), std::string::npos)
                << "lines " << first << "-" << last << ": " << line << " → " << there.substr(0, there.find('\n'));
        }
        // And coming off again, they're back where they were.
        const Toggle back = toggle(out, t.select ? t.select->first : first, t.select ? t.select->second : last, "cpp", cpp);
        EXPECT_EQ(applyTextEdits(out, back.edits), text);
    }
}
