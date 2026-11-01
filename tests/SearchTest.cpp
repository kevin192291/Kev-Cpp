#include "core/Search.h"

#include <gtest/gtest.h>

#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace kevc;

namespace {

std::vector<std::string> ranked(const std::vector<std::string>& paths, const char* query) {
    std::vector<std::string> out;
    for (const auto& m : search::rankFiles(paths, query, 50)) out.push_back(m.path);
    return out;
}

std::vector<std::pair<size_t, size_t>> find(search::TextQuery q, std::string_view line) {
    return search::Matcher(std::move(q)).find(line);
}

using Spans = std::vector<std::pair<size_t, size_t>>;

} // namespace

TEST(Search, ListFilesSkipsWhatTheProjectTabHides) {
    const fs::path dir = fs::temp_directory_path() / ("kevc-search-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    for (const char* d : {"src/core", "build", "node_modules/x", ".git", "cmake-build-debug"}) fs::create_directories(dir / d);
    for (const char* f : {"src/main.cpp", "src/core/a.h", "build/out.o", "node_modules/x/i.js", ".git/HEAD",
                          "cmake-build-debug/c", ".env", "README.md", "save.kevc-tmp"})
        std::ofstream(dir / f) << "x\n";
    EXPECT_EQ(search::listFiles(dir), (std::vector<std::string>{"README.md", "src/core/a.h", "src/main.cpp"}));
    EXPECT_EQ(search::listFiles(dir, 2).size(), 2u);
    fs::remove_all(dir);
}

TEST(Search, FuzzyFileMatchingFindsLettersInOrder) {
    EXPECT_TRUE(search::matchFile("mwc", "src/app/MainWindow.cpp"));
    EXPECT_TRUE(search::matchFile("MAIN", "src/main.cpp")); // case is ignored
    EXPECT_TRUE(search::matchFile("app/main", "src/app/MainWindow.cpp"));
    EXPECT_TRUE(search::matchFile("main win", "src/app/MainWindow.cpp")); // spaces don't count
    EXPECT_FALSE(search::matchFile("mwx", "src/app/MainWindow.cpp"));
    EXPECT_FALSE(search::matchFile("niam", "src/main.cpp")); // order matters
    // Letters scattered through the middles of words don't count; word starts and runs do.
    EXPECT_FALSE(search::matchFile("greeting", "kev/src/designer/DesignerEditing.cpp"));
    EXPECT_TRUE(search::matchFile("pjt", "src/editor/ProjectTree.cpp"));
    EXPECT_TRUE(search::matchFile("ains", "docs/remains.md"));
    // The matched letters, for bold text: P and T of ProjectTree, not the t inside "Project".
    const auto m = search::matchFile("pt", "src/editor/ProjectTree.cpp");
    ASSERT_TRUE(m);
    EXPECT_EQ(m->positions, (std::vector<size_t>{11, 18}));
}

TEST(Search, RankingPrefersNamesWordStartsAndShortPaths) {
    const std::vector<std::string> files = {"src/domain/maintenance.cpp", "docs/remains.md", "src/main.cpp",
                                            "tests/main_test.cpp", "src/app/MainWindow.cpp"};
    // The file called exactly that first, then names that start with it; "domain" and "remains"
    // only hold the letters mid-word.
    const auto r = ranked(files, "main");
    ASSERT_EQ(r.size(), 5u);
    EXPECT_EQ(r[0], "src/main.cpp");
    EXPECT_EQ(r.back(), "docs/remains.md");
    EXPECT_EQ(ranked(files, "mw").front(), "src/app/MainWindow.cpp");
    EXPECT_EQ(ranked({"src/editor/ProjectTree.cpp", "src/app/Application.cpp"}, "pt").front(),
              "src/editor/ProjectTree.cpp");
    EXPECT_EQ(ranked({"src/util.cpp", "src/util.h"}, "util").front(), "src/util.h"); // a tie: shorter wins
    EXPECT_EQ(search::rankFiles(files, "m", 2).size(), 2u);
}

TEST(Search, ContainsAllWords) {
    EXPECT_TRUE(search::containsAllWords("/blog/[slug] +page.server.ts load", "blog LOAD"));
    EXPECT_TRUE(search::containsAllWords("anything", "   "));
    EXPECT_FALSE(search::containsAllWords("/blog/[slug] +page.svelte", "blog server"));
}

TEST(Search, PlainTextMatches) {
    EXPECT_EQ(find({"foo"}, "Foo foo FOO"), (Spans{{0, 3}, {4, 3}, {8, 3}}));
    EXPECT_EQ(find({"foo", true}, "Foo foo FOO"), (Spans{{4, 3}}));
    EXPECT_EQ(find({"aa"}, "aaaa"), (Spans{{0, 2}, {2, 2}})); // not overlapping
    // Whole words: not inside another name.
    EXPECT_EQ(find({"id", false, true}, "id idx _id id_ (id)"), (Spans{{0, 2}, {16, 2}}));
    EXPECT_TRUE(find({""}, "anything").empty());
}

TEST(Search, RegularExpressions) {
    EXPECT_EQ(find({"fo+", false, false, true}, "f fo FOOO"), (Spans{{2, 2}, {5, 4}}));
    EXPECT_EQ(find({"\\bint\\b", true, false, true}, "int print int"), (Spans{{0, 3}, {10, 3}}));
    EXPECT_EQ(find({"int", true, true, true}, "int print"), (Spans{{0, 3}})); // whole word wraps it in \b
    EXPECT_EQ(find({"^x", false, false, true}, "x x x"), (Spans{{0, 1}}));    // ^ only at the line's start
    EXPECT_TRUE(find({"a*", false, false, true}, "bbb").empty());             // empty matches don't count
    search::Matcher bad({"(", false, false, true});
    EXPECT_FALSE(bad.valid());
    EXPECT_FALSE(bad.error().empty());
    EXPECT_TRUE(bad.find("(").empty());
}

TEST(Search, SearchReportsLinesAndSkipsBinary) {
    search::Matcher m({"greeting"});
    const auto matches = m.search("#include \"util.h\"\r\n\nstd::string greeting();\n  return greeting();", 100);
    ASSERT_EQ(matches.size(), 2u);
    EXPECT_EQ(matches[0].line, 3);
    EXPECT_EQ(matches[0].column, 12u);
    EXPECT_EQ(matches[1].line, 4);
    EXPECT_EQ(matches[1].preview, "return greeting();"); // leading spaces trimmed
    EXPECT_EQ(matches[1].previewColumn, 7u);
    EXPECT_EQ(m.search("greeting greeting greeting", 2).size(), 2u);
    EXPECT_TRUE(m.search(std::string("greeting\0\x01", 10), 100).empty());
}

TEST(Search, PreviewKeepsTheMatchInSight) {
    const std::string line = "\t\tconst auto value = computeSomethingRatherLong(argument, otherArgument, needle);";
    const size_t column = line.find("needle");
    const auto p = search::makePreview(line, column, 6, 10);
    EXPECT_EQ(p.preview.substr(p.previewColumn, p.previewLength), "needle");
    EXPECT_TRUE(p.preview.starts_with("…"));
    EXPECT_EQ(p.column, column); // the real column, for the editor
    // Cut at characters, never inside one; the match is whole characters; tabs become spaces.
    const std::string accents = "éééééé x";
    const auto q = search::makePreview(accents, accents.find('x'), 1, 3);
    EXPECT_EQ(q.preview, "…é x");
    EXPECT_EQ(search::makePreview("a\tb", 2, 1).preview, "a b");
    // Long lines are shortened after the match too.
    const auto r = search::makePreview("x" + std::string(500, 'y'), 0, 1, 16, 50);
    EXPECT_LE(r.preview.size(), 50u + 3);
    EXPECT_TRUE(r.preview.ends_with("…"));
    // Bytes that aren't UTF-8 (a Latin-1 file) show as ?, so offsets stay put.
    const auto latin = search::makePreview("caf\xe9 bar", 5, 3);
    EXPECT_EQ(latin.preview, "caf? bar");
    EXPECT_EQ(latin.previewColumn, 5u);
}

TEST(Search, PathFilter) {
    EXPECT_TRUE(search::PathFilter("").matches("src/a.cpp"));
    search::PathFilter cpp("*.cpp, *.h");
    EXPECT_TRUE(cpp.matches("src/a.cpp"));
    EXPECT_TRUE(cpp.matches("a.h"));
    EXPECT_FALSE(cpp.matches("src/a.c"));
    search::PathFilter folder("src/");
    EXPECT_TRUE(folder.matches("src/core/a.cpp"));
    EXPECT_FALSE(folder.matches("srcx/a.cpp"));
    EXPECT_FALSE(folder.matches("tests/src.cpp"));
    EXPECT_TRUE(search::PathFilter("src/*.h").matches("src/core/a.h"));
    EXPECT_TRUE(search::PathFilter("tests").matches("tests/a.cpp")); // a bare name: a folder on the way too
    search::PathFilter exclude("*.cpp !tests/");
    EXPECT_TRUE(exclude.matches("src/a.cpp"));
    EXPECT_FALSE(exclude.matches("tests/a.cpp"));
    EXPECT_FALSE(search::PathFilter("!*.md").matches("README.md"));
    EXPECT_TRUE(search::PathFilter("!*.md").matches("main.cpp"));
}

TEST(Search, SearchFilesUsesUnsavedTextAndStopsAtTheLimit) {
    const fs::path dir = fs::temp_directory_path() / ("kevc-searchfiles-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir / "src");
    std::ofstream(dir / "src/a.cpp") << "int hello = 1;\nint other;\n";
    std::ofstream(dir / "src/b.cpp") << "// nothing\n";
    std::ofstream(dir / "notes.md") << "hello hello\n";
    const auto files = search::listFiles(dir);
    search::Matcher m({"hello"});

    std::vector<search::FileResult> found;
    auto collect = [&found](search::FileResult&& r) { found.push_back(std::move(r)); };
    auto totals = search::searchFiles(dir, files, m, search::PathFilter(""), {}, 100, nullptr, collect);
    EXPECT_EQ(totals.files, 3u);
    EXPECT_EQ(totals.matches, 3u);
    EXPECT_FALSE(totals.truncated);
    ASSERT_EQ(found.size(), 2u);
    EXPECT_EQ(found[0].path, "notes.md");
    EXPECT_EQ(found[1].path, "src/a.cpp");

    // An editor's unsaved text wins over the file on disk; the filter limits the files.
    found.clear();
    totals = search::searchFiles(dir, files, m, search::PathFilter("*.cpp"), {{"src/b.cpp", "x\nhello\n"}}, 100,
                                 nullptr, collect);
    ASSERT_EQ(found.size(), 2u);
    EXPECT_EQ(found[1].path, "src/b.cpp");
    EXPECT_EQ(found[1].matches[0].line, 2);

    found.clear();
    totals = search::searchFiles(dir, files, m, search::PathFilter(""), {}, 2, nullptr, collect);
    EXPECT_EQ(totals.matches, 2u);
    EXPECT_TRUE(totals.truncated);

    std::atomic<bool> cancelled{true};
    found.clear();
    search::searchFiles(dir, files, m, search::PathFilter(""), {}, 100, &cancelled, collect);
    EXPECT_TRUE(found.empty());
    fs::remove_all(dir);
}

TEST(Search, ReplaceKeepsEverythingButTheMatches) {
    size_t n = 0;
    EXPECT_EQ(search::Matcher({"foo"}).replace("Foo foo\r\nbar FOO\n", "baz", &n), "baz baz\r\nbar baz\n");
    EXPECT_EQ(n, 3u);
    EXPECT_EQ(search::Matcher({"foo", true}).replace("Foo foo", "x", &n), "Foo x");
    EXPECT_EQ(search::Matcher({"id", false, true}).replace("id idx (id)", "key", &n), "key idx (key)");
    EXPECT_EQ(n, 2u);
    // Regular expressions: groups in the replacement.
    EXPECT_EQ(search::Matcher({"(\\w+)\\.size\\(\\)", true, false, true}).replace("a.size() + bb.size()", "std::size($1)", &n),
              "std::size(a) + std::size(bb)");
    EXPECT_EQ(n, 2u);
    // Nothing found, no trailing newline, binary: unchanged.
    EXPECT_EQ(search::Matcher({"zzz"}).replace("abc", "x", &n), "abc");
    EXPECT_EQ(n, 0u);
    const std::string binary("foo\0foo", 7);
    EXPECT_EQ(search::Matcher({"foo"}).replace(binary, "x", &n), binary);
    EXPECT_EQ(n, 0u);
}
