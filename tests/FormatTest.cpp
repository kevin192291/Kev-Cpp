#include "core/CppStyle.h"
#include "core/Format.h"
#include "core/Lsp.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <random>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace kevc;
using namespace kevc::format;
using json = nlohmann::json;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / ("kevc-format-" + std::to_string(::getpid()) + "-" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

// The byte offset of an LSP position in `text`: how much deleting everything before it removes.
size_t offsetAt(const std::string& text, int line, int utf16) {
    return text.size() - applyTextEdits(text, {{0, 0, line, utf16, ""}}).size();
}

// Where a mark at byte `mark` of `text` ends up after `edits`, the way GTK moves a left-gravity mark
// (a breakpoint's): text inserted at it goes after it, and deleting around it leaves it at the start.
size_t moveMark(const std::string& text, size_t mark, const std::vector<TextEdit>& edits) {
    // Applied last first, an edit's range in `text` is still its range in the text so far.
    for (const auto& e : lastFirst(edits)) {
        const size_t start = offsetAt(text, e.startLine, e.startUtf16), end = offsetAt(text, e.endLine, e.endUtf16);
        if (mark >= end) mark = mark - (end - start) + e.newText.size();
        else if (mark > start) mark = start;
    }
    return mark;
}

int lineOf(const std::string& text, size_t offset) {
    return static_cast<int>(std::count(text.begin(), text.begin() + static_cast<long>(offset), '\n'));
}

size_t lineStart(const std::string& text, int line) {
    size_t at = 0;
    for (int i = 0; i < line; ++i) at = text.find('\n', at) + 1;
    return at;
}

} // namespace

TEST(Format, ParsesServerEdits) {
    const json edits = json::parse(R"([
        {"range": {"start": {"line": 0, "character": 3}, "end": {"line": 1, "character": 0}}, "newText": " "},
        {"range": {"start": {"line": 2, "character": 0}, "end": {"line": 2, "character": 2}}, "newText": ""}
    ])");
    const auto parsed = parseTextEdits(edits);
    ASSERT_EQ(parsed.size(), 2u);
    EXPECT_EQ(parsed[0], (TextEdit{0, 3, 1, 0, " "}));
    EXPECT_EQ(parsed[1], (TextEdit{2, 0, 2, 2, ""}));
    EXPECT_TRUE(parseTextEdits(json()).empty()); // "no changes" can be null
    EXPECT_TRUE(parseTextEdits(json::object()).empty());
}

TEST(Format, AppliesEditsAgainstTheOriginalText) {
    EXPECT_EQ(applyTextEdits("int x;\n  int y;\n", {{1, 0, 1, 2, "    "}, {0, 3, 0, 4, "  "}}),
              "int  x;\n    int y;\n");
    // Inserts at the same place keep their order.
    EXPECT_EQ(applyTextEdits("ab", {{0, 1, 0, 1, "1"}, {0, 1, 0, 1, "2"}}), "a12b");
    // Columns count UTF-16 units: "😀" is two, "é" one.
    EXPECT_EQ(applyTextEdits("😀é x\n", {{0, 3, 0, 4, ""}}), "😀éx\n");
    // Past the line's end is its end; past the last line is the end of the text.
    EXPECT_EQ(applyTextEdits("ab\ncd", {{0, 9, 0, 9, "!"}, {5, 0, 5, 0, "?"}}), "ab!\ncd?");
}

TEST(Format, Utf16Offsets) {
    EXPECT_EQ(utf16Offset("ab\ncd\n", 1, 1), 4);
    EXPECT_EQ(utf16Offset("😀\nx", 1, 0), 3);
    EXPECT_EQ(utf16Offset("ab\n", 7, 0), 3);
}

TEST(Format, DiffEditsReproduceTheFormattedText) {
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"", ""},
        {"", "int x;\n"},
        {"int x;\n", ""},
        {"a\n", "a\nb\n"},
        {"a", "a\nb"},
        {"a\nb", "a\nb\n"},
        {"a\nb\n", "a\nb"},
        {"int main(){return 0;}\n", "int main()\n{\n    return 0;\n}\n"},
        {"int main()\n{\n    return 0;\n}\n", "int main() { return 0; }\n"},
        {"\tif (x)\n\t\ty();\n", "    if (x)\n        y();\n"},
        {"x = 'é';\ny = \"😀\";\n", "x = \"é\";\ny = '😀';\n"},
        {"a\n\n\n\nb\n", "a\n\nb\n"},
        {"\n\n", "\n"},
        {"one\r\ntwo\r\n", "one\ntwo\n"},
    };
    for (const auto& [before, after] : cases)
        EXPECT_EQ(applyTextEdits(before, diffEdits(before, after)), after) << "from " << testing::PrintToString(before);
    EXPECT_TRUE(diffEdits("same\n", "same\n").empty());
}

TEST(Format, DiffEditsHoldUpOnRandomRewrites) {
    std::mt19937 random(7);
    const std::vector<std::string> pieces = {"{", "}",  "int x;", "  ",        "\t", "if (a)",
                                             "é", "😀", "",       "return 0;", " ",  "//"};
    auto line = [&] {
        std::string out;
        for (int n = random() % 4; n >= 0; --n) out += pieces[random() % pieces.size()];
        return out;
    };
    for (int round = 0; round < 400; ++round) {
        std::vector<std::string> lines;
        for (int n = random() % 30; n > 0; --n) lines.push_back(line());
        std::string before, after;
        for (const auto& l : lines) {
            before += l + "\n";
            switch (random() % 6) {
            case 0: break;                                    // dropped
            case 1: after += "    " + l + "\n"; break;        // indented
            case 2: after += l + "\n" + line() + "\n"; break; // one more after it
            case 3: after += l + " " + line() + "\n"; break;   // changed
            default: after += l + "\n";
            }
        }
        if (!before.empty() && random() % 2) before.pop_back(), after += "end"; // no newline at the end
        ASSERT_EQ(applyTextEdits(before, diffEdits(before, after)), after) << "round " << round;
    }
}

TEST(Format, DiffEditsAreSmallSoMarksStayOnTheirLines) {
    // Indenting a line inserts after its existing indentation, never at its start.
    EXPECT_EQ(diffEdits("a\n  b\n  c\n", "a\n    b\n    c\n"),
              (std::vector<TextEdit>{{1, 2, 1, 2, "  "}, {2, 2, 2, 2, "  "}}));
    // A brace moved onto its own line is one newline inserted.
    EXPECT_EQ(diffEdits("int main(){\n", "int main()\n{\n"), (std::vector<TextEdit>{{0, 10, 0, 10, "\n"}}));

    // A breakpoint on each line of a function clang-format rewrites: each stays on its statement.
    const std::string before = "void f(){\n  if(x){\n    y();\n  }\n  z();\n}\n";
    const std::string after = "void f()\n{\n    if (x)\n    {\n        y();\n    }\n    z();\n}\n";
    const auto edits = diffEdits(before, after);
    ASSERT_EQ(applyTextEdits(before, edits), after);
    const std::vector<std::pair<int, int>> moves = {{0, 0}, {1, 2}, {2, 4}, {4, 6}}; // line before → after
    for (const auto& [from, to] : moves)
        EXPECT_EQ(lineOf(after, moveMark(before, lineStart(before, from), edits)), to) << "line " << from;
}

TEST(Format, FindsTheProjectsClangFormatFile) {
    const fs::path home = scratch("config");
    const fs::path project = home / "repos/app";
    const fs::path file = project / "src/deep/main.cpp";
    fs::create_directories(project / "src/deep");
    EXPECT_EQ(clangFormatConfig(file, project, home), std::nullopt);
    // One in the home folder, or between it and the project, isn't the project's.
    std::ofstream(home / ".clang-format") << "BasedOnStyle: LLVM\n";
    std::ofstream(home / "repos/.clang-format") << "BasedOnStyle: LLVM\n";
    EXPECT_EQ(clangFormatConfig(file, project, home), std::nullopt);
    // The nearest of the project's, however the project folder is spelled.
    std::ofstream(project / "_clang-format") << "BasedOnStyle: LLVM\n";
    EXPECT_EQ(clangFormatConfig(file, project, home), project / "_clang-format");
    EXPECT_EQ(clangFormatConfig(file, project.string() + "/", home.string() + "/"), project / "_clang-format");
    std::ofstream(project / "src/.clang-format") << "BasedOnStyle: Google\n";
    EXPECT_EQ(clangFormatConfig(file, project, home), project / "src/.clang-format");
    fs::remove(project / "_clang-format");
    fs::remove(project / "src/.clang-format");

    // A project inside a git repository has the repository's.
    fs::create_directories(home / "repos/.git");
    EXPECT_EQ(clangFormatConfig(file, project, home), home / "repos/.clang-format");
    EXPECT_EQ(clangFormatConfig(file, {}, home), home / "repos/.clang-format");
    fs::remove_all(home / "repos/.git");

    // A file in no project or repository has only its own folder's.
    fs::create_directories(home / "notes/loose");
    std::ofstream(home / "notes/.clang-format") << "BasedOnStyle: LLVM\n";
    EXPECT_EQ(clangFormatConfig(home / "notes/loose/a.cpp", project, home), std::nullopt);
    std::ofstream(home / "notes/loose/.clang-format") << "BasedOnStyle: LLVM\n";
    EXPECT_EQ(clangFormatConfig(home / "notes/loose/a.cpp", project, home), home / "notes/loose/.clang-format");

    // The home folder never counts, not even as a repository's top (dotfiles kept in git) or as the
    // folder a file is in.
    fs::create_directories(home / ".git");
    EXPECT_EQ(clangFormatConfig(file, project, home), std::nullopt);
    EXPECT_EQ(clangFormatConfig(home / "a.cpp", {}, home), std::nullopt);
    // Without a home folder, the folders up to the project's top count, all of them.
    fs::remove(home / "repos/.clang-format");
    EXPECT_EQ(clangFormatConfig(file, home, {}), home / ".clang-format");
    fs::remove_all(home);
}

TEST(Format, FindsTheProjectsPrettier) {
    const fs::path dir = scratch("prettier");
    fs::create_directories(dir / "node_modules/prettier/bin");
    fs::create_directories(dir / "node_modules/.bin");
    fs::create_directories(dir / "src/lib");
    std::ofstream(dir / "node_modules/prettier/bin/prettier.cjs") << "#!/usr/bin/env node\n";
    fs::create_symlink("../prettier/bin/prettier.cjs", dir / "node_modules/.bin/prettier");
    const Prettier found = findPrettier(dir / "src/lib/app.ts");
    EXPECT_EQ(found.script, fs::canonical(dir / "node_modules/prettier/bin/prettier.cjs"));
    EXPECT_EQ(found.root, dir);
    fs::remove_all(dir);
    EXPECT_TRUE(findPrettier(dir / "src/lib/app.ts").script.empty());
}

TEST(Format, ChoosesTheFormatter) {
    EXPECT_EQ(formatterFor("/p/main.cpp", true), Formatter::Clangd);
    EXPECT_EQ(formatterFor("/p/util.H", false), Formatter::Clangd);
    EXPECT_EQ(formatterFor("/p/App.svelte", true), Formatter::Svelte);
    EXPECT_EQ(formatterFor("/p/app.ts", true), Formatter::Prettier);
    EXPECT_EQ(formatterFor("/p/app.ts", false), Formatter::TypeScript);
    EXPECT_EQ(formatterFor("/p/package.json", true), Formatter::Prettier);
    EXPECT_EQ(formatterFor("/p/package.json", false), Formatter::None);
    EXPECT_EQ(formatterFor("/p/CMakeLists.txt", true), Formatter::None);
    EXPECT_EQ(formatterFor("/p/Makefile", false), Formatter::None);
}

// The real thing: clangd formats a stand-in document in a folder holding Kev-C++'s style, and the
// stand-in never reaches its index (a .cpp beside it, the control, does).
TEST(Format, ClangdFormatsAStandInInKevCppsLayout) {
    if (std::system("command -v clangd >/dev/null 2>&1") != 0) GTEST_SKIP() << "no clangd";
    const fs::path dir = scratch("clangd");
    std::ofstream(dir / ".clang-format") << cppstyle::clangFormatStyle();
    const std::string source = "namespace demo {\n"
                               "class Box : public Base {\n"
                               "public:\n"
                               "  int get() const { return v_; }\n"
                               "    void set(int v){ if(v<0) return; v_=v; }\n"
                               "private:\n"
                               "  int *v_;\n"
                               "};\n"
                               "}\n"
                               "int kevcStandInSymbol(){auto twice=[](int x){return x*2;};\n"
                               "switch(twice(2)){case 4: return 0; default: break;}\n"
                               "for(int i=0;i<3;i++){\n"
                               "twice(i);\n"
                               "}\n"
                               "return 1;}\n"
                               "void later(){connect(argv, [](bool ok){\n"
                               "if(ok) go();\n"
                               "}, false);}\n";
    const std::string expected = "namespace demo\n"
                                 "{\n"
                                 "    class Box : public Base\n"
                                 "    {\n"
                                 "    public:\n"
                                 "        int get() const { return v_; }\n"
                                 "        void set(int v)\n"
                                 "        {\n"
                                 "            if (v < 0)\n"
                                 "                return;\n"
                                 "            v_ = v;\n"
                                 "        }\n"
                                 "    private:\n"
                                 "        int *v_;\n" // the side the file's pointers already have
                                 "    };\n"
                                 "}\n"
                                 "int kevcStandInSymbol()\n"
                                 "{\n"
                                 "    auto twice = [](int x) { return x * 2; };\n"
                                 "    switch (twice(2))\n"
                                 "    {\n"
                                 "    case 4:\n"
                                 "        return 0;\n"
                                 "    default:\n"
                                 "        break;\n"
                                 "    }\n"
                                 "    for (int i = 0; i < 3; i++)\n"
                                 "    {\n"
                                 "        twice(i);\n"
                                 "    }\n"
                                 "    return 1;\n"
                                 "}\n"
                                 // A lambda with more arguments after it (no column limit garbles this).
                                 "void later()\n"
                                 "{\n"
                                 "    connect(\n"
                                 "        argv,\n"
                                 "        [](bool ok) {\n"
                                 "            if (ok)\n"
                                 "                go();\n"
                                 "        },\n"
                                 "        false);\n"
                                 "}\n";
    auto message = [](const json& m) { return lsp::frame(m); };
    const std::string standIn = lsp::pathToUri(dir / standInName(1));
    const std::string control = lsp::pathToUri(dir / "control.cpp");
    {
        std::ofstream in(dir / "in.txt", std::ios::binary);
        in << message({{"jsonrpc", "2.0"},
                       {"id", 1},
                       {"method", "initialize"},
                       {"params",
                        {{"processId", nullptr}, {"rootUri", lsp::pathToUri(dir)}, {"capabilities", json::object()}}}})
           << message({{"jsonrpc", "2.0"}, {"method", "initialized"}, {"params", json::object()}})
           << message({{"jsonrpc", "2.0"},
                       {"method", "textDocument/didOpen"},
                       {"params",
                        {{"textDocument",
                          {{"uri", control},
                           {"languageId", "cpp"},
                           {"version", 1},
                           {"text", "int kevcControlSymbol() { return 0; }\n"}}}}}})
           << message(
                  {{"jsonrpc", "2.0"},
                   {"method", "textDocument/didOpen"},
                   {"params",
                    {{"textDocument", {{"uri", standIn}, {"languageId", "cpp"}, {"version", 1}, {"text", source}}}}}})
           << message(
                  {{"jsonrpc", "2.0"},
                   {"id", 2},
                   {"method", "textDocument/formatting"},
                   {"params",
                    {{"textDocument", {{"uri", standIn}}}, {"options", {{"tabSize", 4}, {"insertSpaces", true}}}}}});
        // Asked after a pause, with the stand-in still open: had clangd parsed it, it'd be indexed by now.
        std::ofstream later(dir / "later.txt", std::ios::binary);
        later << message({{"jsonrpc", "2.0"},
                          {"id", 3},
                          {"method", "workspace/symbol"},
                          {"params", {{"query", "kevcStandInSymbol"}}}})
              << message({{"jsonrpc", "2.0"},
                          {"id", 4},
                          {"method", "workspace/symbol"},
                          {"params", {{"query", "kevcControlSymbol"}}}});
    }
    const std::string run =
        "cd " + dir.string() +
        " && (cat in.txt; sleep 3; cat later.txt; sleep 1) | timeout 20 clangd --log=error 2>/dev/null";
    FILE* pipe = popen(run.c_str(), "r");
    ASSERT_TRUE(pipe);
    lsp::MessageFramer framer;
    char buffer[8192];
    for (size_t n; (n = std::fread(buffer, 1, sizeof buffer, pipe)) > 0;) framer.feed(std::string_view(buffer, n));
    pclose(pipe);
    std::map<int, json> replies;
    while (auto m = framer.next())
        if (m->contains("id") && !m->contains("method")) replies[(*m)["id"].get<int>()] = *m;
    fs::remove_all(dir);

    ASSERT_TRUE(replies.count(2)) << "clangd didn't answer the formatting request";
    EXPECT_EQ(applyTextEdits(source, parseTextEdits(replies[2]["result"])), expected);
    ASSERT_TRUE(replies.count(3) && replies.count(4));
    EXPECT_TRUE(replies[3]["result"].empty()) << replies[3].dump();
    EXPECT_FALSE(replies[4]["result"].empty()) << "the control wasn't indexed, so the check above proves nothing";
}
