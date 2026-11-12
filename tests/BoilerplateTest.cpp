#include "core/Boilerplate.h"
#include "core/Packages.h"

#include <gtest/gtest.h>

#include <fstream>
#include <atomic>
#include <functional>
#include <regex>
#include <set>
#include <sstream>
#include <thread>
#include <unistd.h>

using namespace kevc;
using namespace kevc::boilerplate;
namespace fs = std::filesystem;

namespace {

class TempDir {
public:
    TempDir() {
        path_ = fs::temp_directory_path() / ("kevc-boilerplate-" + std::to_string(::getpid()) + "-" + std::to_string(counter_++));
        fs::remove_all(path_);
        fs::create_directories(path_);
    }
    ~TempDir() { fs::remove_all(path_); }
    const fs::path& path() const { return path_; }
    void write(const std::string& rel, const std::string& text) const {
        fs::create_directories((path_ / rel).parent_path());
        std::ofstream(path_ / rel) << text;
    }

private:
    fs::path path_;
    static inline int counter_ = 0;
};

std::vector<std::string> labels(const Entry& folder) {
    std::vector<std::string> out;
    for (const auto& c : folder.children) out.push_back(c.label);
    return out;
}

const Entry* childNamed(const Entry& folder, const std::string& label) {
    for (const auto& c : folder.children)
        if (c.label == label) return &c;
    return nullptr;
}

// The text after inserting `body` where `before` ends and `selected` follows (selected), as MainWindow
// does it: the insertion replaces the selection, and the line's indentation too when fromLineStart.
std::string inserted(const std::string& before, const std::string& selected, const std::string& after,
                     const std::string& body, const Values& values = {}) {
    const size_t lineStart = before.rfind('\n') == std::string::npos ? 0 : before.rfind('\n') + 1;
    const std::string prefix = before.substr(lineStart);
    const Insertion ins = expand(body, prefix, selected, after.substr(0, after.find('\n')), values, false);
    std::string text = ins.text;
    if (ins.cursor != std::string::npos) text.insert(ins.cursor, "|");
    return (ins.fromLineStart ? before.substr(0, lineStart) : before) + text + after;
}

} // namespace

TEST(Boilerplate, HeaderBetweenDashLinesAndTheBodyAfterIt) {
    const Snippet s = parseSnippet("---\n"
                                   "label: Read/Write a File\n"
                                   "order: 5\n"
                                   "section: Starters\n"
                                   "description: Reads it, writes it\n"
                                   "file: {{Name}}.hpp\n"
                                   "whole-file: yes\n"
                                   "ask: Name = Player\n"
                                   "ask: Speed=10\n"
                                   "packages: sfml-graphics, sfml-audio  gtk4\n"
                                   "---\n"
                                   "int main()\n{\n}\n");
    EXPECT_EQ(s.header.label, "Read/Write a File");
    EXPECT_EQ(s.header.order, 5);
    EXPECT_EQ(s.header.section, "Starters");
    EXPECT_EQ(s.header.description, "Reads it, writes it");
    EXPECT_EQ(s.header.file, "{{Name}}.hpp");
    EXPECT_TRUE(s.header.wholeFile);
    ASSERT_EQ(s.header.asks.size(), 2u);
    EXPECT_EQ(s.header.asks[0], (std::pair<std::string, std::string>{"Name", "Player"}));
    EXPECT_EQ(s.header.asks[1], (std::pair<std::string, std::string>{"Speed", "10"}));
    EXPECT_EQ(s.header.packages, (std::vector<std::string>{"sfml-graphics", "sfml-audio", "gtk4"}));
    EXPECT_EQ(s.body, "int main()\n{\n}\n");
}

TEST(Boilerplate, WithoutAHeaderEverythingIsTheBody) {
    EXPECT_EQ(parseSnippet("int x;\n").body, "int x;\n");
    // An opening line with no closing one isn't a header.
    EXPECT_EQ(parseSnippet("---\nlabel: x\nint y;\n").body, "---\nlabel: x\nint y;\n");
    const Snippet s = parseSnippet("---\r\nlabel: CRLF\r\n---\r\na\r\nb\r\n");
    EXPECT_EQ(s.header.label, "CRLF");
    EXPECT_EQ(s.body, "a\nb\n");
    EXPECT_FALSE(s.header.wholeFile);
    EXPECT_EQ(s.header.order, 100);
    // Bad values and names are ignored rather than fatal.
    const Snippet bad = parseSnippet("---\norder: soon\nask: 2fast = x\nwhole-file: maybe\n---\n");
    EXPECT_EQ(bad.header.order, 100);
    EXPECT_TRUE(bad.header.asks.empty());
    EXPECT_FALSE(bad.header.wholeFile);
    EXPECT_EQ(bad.body, "");
}

TEST(Boilerplate, IncludesAndSfmlVersion) {
    const Snippet s = parseSnippet("---\nincludes: <SFML/Graphics.hpp>, <vector> \"my file.h\"\nsfml: 3\n---\nx\n");
    EXPECT_EQ(s.header.includes, (std::vector<std::string>{"<SFML/Graphics.hpp>", "<vector>", "\"my file.h\""}));
    EXPECT_EQ(s.header.sfml, 3);
}

TEST(Boilerplate, MissingIncludesGoAfterTheLastOne) {
    const std::vector<std::string> wanted{"<vector>", "<SFML/Graphics.hpp>"};
    auto apply = [&](const std::string& text) {
        const auto ins = missingIncludes(text, wanted);
        return ins ? text.substr(0, ins->offset) + ins->text + text.substr(ins->offset) : text;
    };
    EXPECT_EQ(apply("#include <iostream>\n\nint main()\n{\n}\n"),
              "#include <iostream>\n#include <vector>\n#include <SFML/Graphics.hpp>\n\nint main()\n{\n}\n");
    // One already there (with other spacing) isn't repeated.
    EXPECT_EQ(apply("#include  <vector>\n#include \"game.h\"\nint x;\n"),
              "#include  <vector>\n#include \"game.h\"\n#include <SFML/Graphics.hpp>\nint x;\n");
    EXPECT_FALSE(missingIncludes("#include <vector>\n#include <SFML/Graphics.hpp>\n", wanted));
    // A file ending in an #include without a newline.
    EXPECT_EQ(apply("#include <cstdio>"), "#include <cstdio>\n#include <vector>\n#include <SFML/Graphics.hpp>");
    // No includes yet: below a leading comment and #pragma once, a blank line before the code.
    EXPECT_EQ(apply("// Player\n#pragma once\nclass Player;\n"),
              "// Player\n#pragma once\n#include <vector>\n#include <SFML/Graphics.hpp>\n\nclass Player;\n");
    EXPECT_EQ(apply(""), "#include <vector>\n#include <SFML/Graphics.hpp>\n");
    // A commented-out include doesn't count.
    EXPECT_EQ(apply("// #include <vector>\n#include <SFML/Graphics.hpp>\n"),
              "// #include <vector>\n#include <SFML/Graphics.hpp>\n#include <vector>\n");
}

TEST(Boilerplate, OnlyTheInstalledSfmlsSnippets) {
    TempDir dir;
    dir.write("C/SFML/Window.cpp", "---\nsfml: 3\n---\nx\n");
    dir.write("C/SFML/Shader.frag", "x\n");
    dir.write("C/SFML 2/Window.cpp", "---\nsfml: 2\n---\nx\n");
    const Entry tree = scan({dir.path()});
    const Entry three = forSfml(tree, 3), two = forSfml(tree, 2);
    ASSERT_EQ(three.children.size(), 1u);
    EXPECT_EQ(labels(three.children[0]), (std::vector<std::string>{"SFML"}));
    EXPECT_EQ(labels(three.children[0].children[0]), (std::vector<std::string>{"Shader", "Window"}));
    EXPECT_EQ(labels(two.children[0]), (std::vector<std::string>{"SFML", "SFML 2"}));
    EXPECT_EQ(labels(two.children[0].children[0]), (std::vector<std::string>{"Shader"}));
    EXPECT_NE(signature(tree), signature(three));
}

TEST(Boilerplate, FolderFileHasTheSameLines) {
    const Header h = parseFolderFile("label: C/C++\norder: 10\n# a comment\nsection: languages\n");
    EXPECT_EQ(h.label, "C/C++");
    EXPECT_EQ(h.order, 10);
    EXPECT_EQ(h.section, "languages");
}

TEST(Boilerplate, ScanMergesRootsSortsAndSkipsHiddenNames) {
    TempDir builtIn, mine;
    builtIn.write("C and C++/_folder.txt", "label: C/C++\norder: 10\n");
    builtIn.write("C and C++/SFML/Window.cpp", "---\norder: 1\n---\nwindow\n");
    builtIn.write("C and C++/SFML/Custom Drawable Class.cpp", "drawable\n");
    builtIn.write("C and C++/SFML/Animation.cpp", "animation\n");
    builtIn.write("C and C++/SFML/_notes.txt", "not an item\n");
    builtIn.write("C and C++/SFML/.hidden.cpp", "not an item\n");
    builtIn.write("C and C++/SFML/Window.cpp~", "a backup\n");
    builtIn.write("C and C++/Empty/_folder.txt", "label: Nothing here\n");
    builtIn.write("CSS/Reset.css", "reset\n");
    builtIn.write("_README.md", "docs\n");
    mine.write("C and C++/SFML/Window.cpp", "---\nlabel: My Window\norder: 1\n---\nmine\n");
    mine.write("C and C++/SFML/Extra.cpp", "extra\n");
    mine.write("Mine/Thing.txt", "thing\n");

    const Entry root = scan({builtIn.path(), mine.path()});
    EXPECT_EQ(labels(root), (std::vector<std::string>{"C/C++", "CSS", "Mine"}));
    const Entry* cpp = childNamed(root, "C/C++");
    ASSERT_TRUE(cpp);
    EXPECT_EQ(cpp->order, 10);
    EXPECT_EQ(labels(*cpp), (std::vector<std::string>{"SFML"})); // the empty folder is left out
    const Entry* sfml = childNamed(*cpp, "SFML");
    ASSERT_TRUE(sfml);
    EXPECT_EQ(labels(*sfml), (std::vector<std::string>{"My Window", "Animation", "Custom Drawable Class", "Extra"}));
    const Entry& window = sfml->children[0];
    EXPECT_EQ(window.key, "C and C++/SFML/Window.cpp");
    EXPECT_EQ(window.file, mine.path() / "C and C++/SFML/Window.cpp");
    EXPECT_EQ(sfml->children[1].file, builtIn.path() / "C and C++/SFML/Animation.cpp");

    EXPECT_EQ(labelPath(root, "C and C++/SFML/Animation.cpp"), (std::vector<std::string>{"C/C++", "SFML", "Animation"}));
    EXPECT_TRUE(labelPath(root, "C and C++/SFML/Nope.cpp").empty());

    EXPECT_EQ(locate({builtIn.path(), mine.path()}, "C and C++/SFML/Window.cpp"), mine.path() / "C and C++/SFML/Window.cpp");
    EXPECT_EQ(locate({builtIn.path(), mine.path()}, "CSS/Reset.css"), builtIn.path() / "CSS/Reset.css");
    EXPECT_FALSE(locate({builtIn.path(), mine.path()}, "../etc/passwd"));
    EXPECT_FALSE(locate({builtIn.path(), mine.path()}, "/etc/passwd"));
    EXPECT_FALSE(locate({builtIn.path(), mine.path()}, "CSS"));

    // A missing root is skipped.
    EXPECT_EQ(labels(scan({builtIn.path() / "nowhere", mine.path()})), (std::vector<std::string>{"C and C++", "Mine"}));
}

TEST(Boilerplate, SignatureChangesWithWhatTheMenuShows) {
    TempDir dir;
    dir.write("A/One.txt", "one\n");
    const std::string first = signature(scan({dir.path()}));
    EXPECT_EQ(signature(scan({dir.path()})), first);
    dir.write("A/One.txt", "different text, same item\n");
    EXPECT_EQ(signature(scan({dir.path()})), first);
    dir.write("A/One.txt", "---\nlabel: Uno\n---\n");
    EXPECT_NE(signature(scan({dir.path()})), first);
}

TEST(Boilerplate, MenuLabelsKeepTheirUnderscores) {
    EXPECT_EQ(menuLabel("my_function"), "my__function");
    EXPECT_EQ(menuLabel("Window"), "Window");
}

TEST(Boilerplate, SnippetsFitFilesOfTheirLanguage) {
    EXPECT_TRUE(fits("Window.cpp", "src/main.cpp"));
    EXPECT_TRUE(fits("Window.cpp", "player.h"));
    EXPECT_TRUE(fits("Class.hpp", "game.cc"));
    EXPECT_FALSE(fits("Window.cpp", "main.c"));
    EXPECT_TRUE(fits("List.c", "list.h"));
    EXPECT_FALSE(fits("List.c", "list.cpp"));
    EXPECT_TRUE(fits("Guard.h", "x.c"));
    EXPECT_TRUE(fits("Guard.h", "x.cpp"));
    EXPECT_TRUE(fits("Flex.css", "App.svelte"));
    EXPECT_TRUE(fits("Flex.css", "index.html"));
    EXPECT_TRUE(fits("Flex.css", "site.scss"));
    EXPECT_FALSE(fits("Flex.css", "main.cpp"));
    EXPECT_TRUE(fits("Debounce.ts", "App.svelte"));
    EXPECT_FALSE(fits("Debounce.ts", "app.js"));
    EXPECT_TRUE(fits("Fetch.js", "app.ts"));
    EXPECT_TRUE(fits("Counter.svelte", "Counter.svelte"));
    EXPECT_FALSE(fits("Counter.svelte", "index.html"));
    EXPECT_TRUE(fits("Page.html", "+page.svelte"));
    EXPECT_TRUE(fits("FetchContent.cmake", "CMakeLists.txt"));
    EXPECT_FALSE(fits("FetchContent.cmake", "notes.txt"));
    EXPECT_TRUE(fits("Program.mk", "Makefile"));
    EXPECT_TRUE(fits("Blur.frag", "shader.glsl"));
    EXPECT_TRUE(fits("License.txt", "anything.rs"));
    EXPECT_TRUE(fits("C and C++.gitignore", "project/.gitignore"));
    EXPECT_FALSE(fits("C and C++.gitignore", "main.cpp"));
    EXPECT_TRUE(fits("Kev-C++ Style.clang-format", ".clang-format"));
    EXPECT_FALSE(fits("Kev-C++ Style.clang-format", "main.cpp"));
    EXPECT_TRUE(fits("Thing.rs", "lib.rs"));
    EXPECT_FALSE(fits("Thing", "Other"));
}

TEST(Boilerplate, PromptsAndCases) {
    EXPECT_EQ(promptFor("ClassName"), "Class name");
    EXPECT_EQ(promptFor("componentName"), "Component name");
    EXPECT_EQ(promptFor("HTTPPort"), "HTTP port");
    EXPECT_EQ(promptFor("url"), "Url");

    EXPECT_EQ(applyCase("PlayerShip", "snake"), "player_ship");
    EXPECT_EQ(applyCase("PlayerShip", "kebab"), "player-ship");
    EXPECT_EQ(applyCase("player ship", "pascal"), "PlayerShip");
    EXPECT_EQ(applyCase("player-ship", "camel"), "playerShip");
    EXPECT_EQ(applyCase("HTTPServer", "snake"), "http_server");
    EXPECT_EQ(applyCase("Vector2D", "snake"), "vector2d");
    EXPECT_EQ(applyCase("PlayerShip", "upper"), "PLAYERSHIP");
    EXPECT_EQ(applyCase("PlayerShip", "lower"), "playership");
    EXPECT_FALSE(applyCase("x", "shout"));
}

TEST(Boilerplate, SubstituteOnlyTouchesKnownNames) {
    const Values v{{"ClassName", "PlayerShip"}};
    EXPECT_EQ(substitute("{{ClassName}}.hpp", v), "PlayerShip.hpp");
    EXPECT_EQ(substitute("{{ClassName:snake:upper}}_HPP", v), "PLAYER_SHIP_HPP");
    EXPECT_EQ(substitute("`${name}` {{1, 2}} {value} {{Other}} {{ ClassName }}", v), "`${name}` {{1, 2}} {value} {{Other}} {{ ClassName }}");
    EXPECT_EQ(substitute("{{ClassName:shout}}", v), "{{ClassName:shout}}"); // left for the author to see
    EXPECT_EQ(substitute("{{{ClassName}}}", v), "{PlayerShip}");
    EXPECT_EQ(substitute("{{cursor}}", v), "{{cursor}}"); // only insertion knows where the cursor goes
}

TEST(Boilerplate, InsertionIsIndentedLikeItsLine) {
    // At the cursor on an indented, otherwise empty line: every line gets that indentation.
    EXPECT_EQ(inserted("void f()\n{\n    ", "", "\n}\n", "if (x)\n{\n    {{cursor}}\n}\n"),
              "void f()\n{\n    if (x)\n    {\n        |\n    }\n}\n");
    // Blank lines stay empty, not indented.
    EXPECT_EQ(inserted("    ", "", "", "a();\n\nb();\n"), "    a();\n\n    b();");
    // Mid-line: the first line goes where the cursor is, the rest get the line's indentation.
    EXPECT_EQ(inserted("    auto v = ", "", ";", "std::vector<int>{\n    1, 2,\n}"), "    auto v = std::vector<int>{\n        1, 2,\n    };");
    // Without {{cursor}} the cursor goes to the end; the first {{cursor}} wins.
    EXPECT_EQ(inserted("", "", "", "x{{cursor}}y{{cursor}}z\n"), "x|yz");
    // An empty indented line with only the cursor keeps its indentation for typing.
    EXPECT_EQ(inserted("", "", "", "{\n    {{cursor}}\n}\n"), "{\n    |\n}");
}

TEST(Boilerplate, TextAfterTheCursorKeepsItsOwnLine) {
    // At the start of a line with code after the cursor: that code moves below, at its indentation, and
    // the cursor stays at the end of what was inserted.
    EXPECT_EQ(inserted("", "", "#include <iostream>\n", "int main()\n{\n}\n"), "int main()\n{\n}|\n#include <iostream>\n");
    EXPECT_EQ(inserted("{\n    ", "", "foo();\n}", "if (x)\n{\n    {{cursor}}\n}\n"),
              "{\n    if (x)\n    {\n        |\n    }\n    foo();\n}");
    // Mid-line, the text stays on its line.
    EXPECT_EQ(inserted("    x = ", "", ";", "abs(y)"), "    x = abs(y);");
}

TEST(Boilerplate, BlankLinesTakeTheIndentationAboveThem) {
    EXPECT_EQ(indentForBlankLine("int main()\n{\n    std::cout << 1;\n\n"), "    ");
    EXPECT_EQ(indentForBlankLine("int main()\n{\n"), "    ");
    EXPECT_EQ(indentForBlankLine("class A\n{\npublic:\n"), "    ");
    EXPECT_EQ(indentForBlankLine("\tif (x) {\n"), "\t\t");
    EXPECT_EQ(indentForBlankLine("        x = 1;\n    }\n"), "    ");
    EXPECT_EQ(indentForBlankLine("std::cout << a::b;\n"), "");
    EXPECT_EQ(indentForBlankLine(""), "");
}

TEST(Boilerplate, SelectionIsWrapped) {
    const std::string tryCatch = "try\n{\n    {{selection}}\n}\ncatch (const std::exception& e)\n{\n    {{cursor}}\n}\n";
    // Whole lines selected (from the start of the first to the start of the line after the last).
    EXPECT_EQ(inserted("void f()\n{\n", "    foo();\n    if (x)\n        bar();\n", "}\n", tryCatch),
              "void f()\n{\n"
              "    try\n    {\n        foo();\n        if (x)\n            bar();\n    }\n"
              "    catch (const std::exception& e)\n    {\n        |\n    }\n"
              "}\n");
    // Selected from the first statement's first character: the same.
    EXPECT_EQ(inserted("{\n    ", "foo();", "\n}", tryCatch),
              "{\n    try\n    {\n        foo();\n    }\n    catch (const std::exception& e)\n    {\n        |\n    }\n}");
    // Nothing selected: the placeholder goes, its line stays empty.
    EXPECT_EQ(inserted("", "", "", "{#if ok}\n    {{selection}}\n{/if}\n"), "{#if ok}\n\n{/if}");
    // Mid-line: an expression wrapped where it is; its later lines stay where they were.
    EXPECT_EQ(inserted("    x = ", "a +\n        b", ";", "std::abs({{selection}})"), "    x = std::abs(a +\n        b);");
}

TEST(Boilerplate, FinalNewlineOnlyForWholeFiles) {
    EXPECT_EQ(expand("int main()\n{\n}\n", "", "", "", {}, true).text, "int main()\n{\n}\n");
    EXPECT_EQ(expand("int main()\n{\n}\n", "", "", "", {}, false).text, "int main()\n{\n}");
    const Insertion ins = expand("class {{Name}}\n{\n    {{cursor}}\n};\n", "", "", "", {{"Name", "Ship"}}, true);
    EXPECT_EQ(ins.text, "class Ship\n{\n    \n};\n");
    EXPECT_EQ(ins.cursor, std::string("class Ship\n{\n    ").size());
}

TEST(Boilerplate, SavingASelection) {
    EXPECT_EQ(dedent("\n        a();\n            b();\n\n        c();\n\n"), "a();\n    b();\n\nc();\n");
    std::string error;
    EXPECT_EQ(savePath("/u", "C and C++/My Boilerplate/Swap Two Values", ".cpp", error),
              fs::path("/u/C and C++/My Boilerplate/Swap Two Values.cpp"));
    EXPECT_EQ(savePath("/u", " Mine / Thing.cpp ", ".cpp", error), fs::path("/u/Mine/Thing.cpp"));
    EXPECT_EQ(savePath("/u", "Notes", "", error), fs::path("/u/Notes.txt"));
    EXPECT_FALSE(savePath("/u", "Mine//Thing", ".cpp", error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(savePath("/u", "../Thing", ".cpp", error));
    EXPECT_FALSE(savePath("/u", "Mine/_hidden", ".cpp", error));
    EXPECT_FALSE(savePath("/u", "", ".cpp", error));

    EXPECT_EQ(snippetExtension("src/main.cpp"), ".cpp");
    EXPECT_EQ(snippetExtension("CMakeLists.txt"), ".cmake");
    EXPECT_EQ(snippetExtension("Makefile"), ".mk");
    EXPECT_EQ(snippetExtension("app/.gitignore"), ".gitignore");
    EXPECT_EQ(snippetExtension("README"), "");
    EXPECT_TRUE(fits(fs::path("x").concat(snippetExtension("CMakeLists.txt")), "CMakeLists.txt"));
    EXPECT_TRUE(fits(fs::path("x").concat(snippetExtension("app/.gitignore")), "app/.gitignore"));

    EXPECT_EQ(suggestedFolder("src/main.cpp"), "C and C++/My Boilerplate");
    EXPECT_EQ(suggestedFolder("src/routes/+page.svelte"), "Svelte and SvelteKit/My Boilerplate");
    EXPECT_EQ(suggestedFolder("notes.rs"), "My Boilerplate");
}

// The boilerplate that comes with Kev-C++: every file reads as a snippet, says what it is, and uses only
// placeholders that will be filled. A slip here would put "{{ClassNmae}}" into someone's code.
TEST(Boilerplate, BuiltInBoilerplateIsWellFormed) {
    const fs::path root = fs::path(KEVC_SOURCE_DIR) / "data/boilerplate";
    const Entry tree = scan({root});
    const std::set<std::string> builtIns{"cursor", "selection", "file", "fileStem", "project", "date", "year"};
    int count = 0;
    std::function<void(const Entry&)> check = [&](const Entry& folder) {
        std::set<std::string> seen;
        for (const auto& e : folder.children) {
            EXPECT_TRUE(seen.insert(e.label).second) << "two items named " << e.label << " in " << folder.label;
            if (e.folder) {
                check(e);
                continue;
            }
            ++count;
            std::ifstream in(e.file);
            std::stringstream text;
            text << in.rdbuf();
            const Snippet s = parseSnippet(text.str());
            EXPECT_FALSE(s.header.description.empty()) << e.key << " has no description:";
            EXPECT_FALSE(s.body.empty()) << e.key << " is empty";
            EXPECT_TRUE(s.body.empty() || s.body.back() == '\n') << e.key << " doesn't end with a newline";
            std::set<std::string> known = builtIns, asked;
            for (const auto& [name, def] : s.header.asks) {
                known.insert(name);
                asked.insert(name);
            }
            // Every placeholder in the text and the file name is one that gets filled. A placeholder is
            // what the engine reads as one, `{{name}}` closed straight after its modifiers, so C++'s
            // `{{1, 2}, {3, 4}}` isn't; modifiers are matched loosely here to catch a `:Upper`.
            static const std::regex placeholder(R"(\{\{([A-Za-z_][A-Za-z0-9_]*)((?::[A-Za-z0-9_]*)*)\}\})");
            for (const std::string& where : {s.body, s.header.file}) {
                for (std::sregex_iterator it(where.begin(), where.end(), placeholder), end; it != end; ++it) {
                    const std::string name = (*it)[1];
                    EXPECT_TRUE(known.count(name)) << e.key << " uses {{" << name << "}}, which nothing fills";
                    asked.erase(name);
                    std::stringstream modifiers((*it)[2].str());
                    std::string modifier;
                    std::getline(modifiers, modifier, ':'); // the empty part before the first colon
                    while (std::getline(modifiers, modifier, ':'))
                        EXPECT_TRUE(applyCase("x", modifier)) << e.key << " uses an unknown modifier :" << modifier;
                }
            }
            EXPECT_TRUE(asked.empty()) << e.key << " asks for " << *asked.begin() << " but never uses it";
            // SFML code is for one version, and the menu shows it only when that one is installed.
            const bool usesSfml = std::any_of(s.header.packages.begin(), s.header.packages.end(),
                                              [](const std::string& m) { return m.starts_with("sfml-"); });
            EXPECT_TRUE(!usesSfml || s.header.sfml == 2 || s.header.sfml == 3) << e.key << " needs an sfml: line";
            for (const auto& include : s.header.includes)
                EXPECT_TRUE(std::regex_match(include, std::regex(R"(<[^<>\s]+>|"[^"]+")"))) << e.key << ": " << include;
        }
    };
    check(tree);
    EXPECT_GE(count, 150);
}

// Every whole C or C++ file that comes with Kev-C++ compiles with what this computer has installed:
// g++/gcc with its packages' flags. One whose packages aren't installed, or that's for another SFML, is
// skipped; so is a source that includes its partner header ("{{ClassName}}.hpp"). Headers are compiled
// through a file that includes them.
TEST(Boilerplate, BuiltInWholeFilesCompile) {
    const fs::path root = fs::path(KEVC_SOURCE_DIR) / "data/boilerplate";
    TempDir dir;
    const sfml::Install installed = sfml::detect();
    struct Job {
        std::string key, command, output;
        int status = 0;
    };
    std::vector<Job> jobs;
    std::function<void(const Entry&)> collect = [&](const Entry& folder) {
        for (const auto& e : folder.children) {
            if (e.folder) {
                collect(e);
                continue;
            }
            const std::string ext = e.file.extension().string();
            const bool c = ext == ".c", header = ext == ".hpp" || ext == ".h";
            if (!c && ext != ".cpp" && !header) continue;
            std::ifstream in(e.file);
            std::stringstream text;
            text << in.rdbuf();
            const Snippet s = parseSnippet(text.str());
            if (!s.header.wholeFile) continue;
            if (s.header.sfml && (!installed.installed() || installed.major() != s.header.sfml)) continue;
            Values values{{"file", "main.cpp"}, {"fileStem", "main"}, {"project", "demo"}, {"date", "2026-01-01"}, {"year", "2026"}};
            for (const auto& [name, def] : s.header.asks) values[name] = def;
            const std::string code = expand(s.body, "", "", "", values, true).text;
            if (!header && std::regex_search(code, std::regex(R"(#include\s+")"))) continue;
            std::string flags;
            if (!s.header.packages.empty()) {
                if (runPkgConfig("--modversion", s.header.packages).empty()) continue;
                flags = runPkgConfig("--cflags", s.header.packages);
            }
            const std::string name = "snippet" + std::to_string(jobs.size());
            std::ofstream(dir.path() / (name + ext)) << code;
            fs::path source = dir.path() / (name + ext);
            if (header) {
                source = dir.path() / (name + "-check.cpp");
                std::ofstream(source) << "#include \"" << name << ext << "\"\n";
            }
            Job job;
            job.key = e.key;
            job.command = std::string(c ? "gcc -std=c17" : "g++ -std=c++20") +
                          " -fsyntax-only -Wall -Wextra -Werror -Wno-unused-function -Wno-deprecated-declarations " + flags +
                          " '" + source.string() + "' 2>&1";
            jobs.push_back(std::move(job));
        }
    };
    collect(scan({root}));

    std::atomic<size_t> next{0};
    std::vector<std::thread> workers;
    for (unsigned i = 0; i < std::max(2u, std::thread::hardware_concurrency()); ++i)
        workers.emplace_back([&] {
            for (size_t j; (j = next++) < jobs.size();) {
                FILE* pipe = popen(jobs[j].command.c_str(), "r");
                if (!pipe) {
                    jobs[j].status = -1;
                    continue;
                }
                char buffer[4096];
                for (size_t n; (n = fread(buffer, 1, sizeof buffer, pipe)) > 0;) jobs[j].output.append(buffer, n);
                jobs[j].status = pclose(pipe);
            }
        });
    for (auto& w : workers) w.join();
    for (const auto& job : jobs) EXPECT_EQ(job.status, 0) << job.key << "\n" << job.output;
    if (jobs.empty()) GTEST_SKIP() << "no compiler or libraries for the C/C++ boilerplate";
}
