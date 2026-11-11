#include "core/Boilerplate.h"

#include "core/Packages.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace kevc;
using boilerplate::Snippet;

namespace {

std::string inserted(const std::string& text, int line, const Snippet& s, int* cursor = nullptr) {
    const auto plan = boilerplate::insertion(text, line, s);
    if (cursor) *cursor = plan.cursorLine;
    return format::applyTextEdits(text, plan.edits);
}

Snippet piece(std::string code, std::vector<std::string> includes = {}) {
    Snippet s;
    s.id = "piece";
    s.code = std::move(code);
    s.includes = std::move(includes);
    return s;
}

} // namespace

TEST(Boilerplate, SnippetsAreWellFormed) {
    std::set<std::string> ids;
    for (const Snippet& s : boilerplate::all()) {
        EXPECT_TRUE(ids.insert(s.id).second) << s.id << " is listed twice";
        EXPECT_FALSE(s.library.empty()) << s.id;
        EXPECT_FALSE(s.title.empty()) << s.id;
        ASSERT_FALSE(s.code.empty()) << s.id;
        EXPECT_EQ(s.code.back(), '\n') << s.id;
        EXPECT_EQ(s.code.find('\t'), std::string::npos) << s.id;
        EXPECT_NE(s.code[0], ' ') << s.id << " starts indented";
        // A program brings its own includes and main(); a piece names the includes it needs.
        EXPECT_EQ(s.program, s.code.find("int main(") != std::string::npos) << s.id;
        EXPECT_EQ(s.program, s.includes.empty()) << s.id;
        // Something has to say what the project links.
        EXPECT_NE(s.pkgConfig.empty(), s.sfmlComponents.empty()) << s.id;
        EXPECT_EQ(s.sfmlMajor != 0, !s.sfmlComponents.empty()) << s.id;
        EXPECT_EQ(boilerplate::find(s.id), &s);
    }
    EXPECT_EQ(boilerplate::find("nope"), nullptr);
}

TEST(Boilerplate, OffersTheInstalledSfmlsApi) {
    for (int major : {2, 3}) {
        bool any = false;
        for (const Snippet* s : boilerplate::offered(major)) {
            EXPECT_TRUE(s->sfmlMajor == 0 || s->sfmlMajor == major) << s->id;
            any = any || s->sfmlMajor == major;
        }
        EXPECT_TRUE(any);
    }
    // The two APIs differ where it matters: the event loop.
    EXPECT_NE(boilerplate::find("sfml3-window")->code.find("std::optional event = window.pollEvent()"), std::string::npos);
    EXPECT_NE(boilerplate::find("sfml2-window")->code.find("window.pollEvent(event)"), std::string::npos);
}

TEST(Boilerplate, LanguageByFileName) {
    EXPECT_EQ(boilerplate::languageOf("src/main.cpp"), boilerplate::Language::Cpp);
    EXPECT_EQ(boilerplate::languageOf("window.h"), boilerplate::Language::Cpp);
    EXPECT_EQ(boilerplate::languageOf("main.c"), boilerplate::Language::C);
    EXPECT_EQ(boilerplate::languageOf("index.ts"), std::nullopt);
    EXPECT_EQ(boilerplate::languageOf("CMakeLists.txt"), std::nullopt);
}

TEST(Boilerplate, AProgramReplacesABlankFile) {
    const Snippet& s = *boilerplate::find("sfml3-window");
    EXPECT_EQ(inserted("", 0, s), s.code);
    EXPECT_EQ(inserted("\n  \n\n", 2, s), s.code);
}

TEST(Boilerplate, AProgramGoesBelowTheCursorsLineInAFileWithText) {
    Snippet s;
    s.program = true;
    s.code = "int main()\n{\n    return 0;\n}\n";
    int cursor = -1;
    EXPECT_EQ(inserted("// mine\n", 0, s, &cursor), "// mine\nint main()\n{\n    return 0;\n}\n");
    EXPECT_EQ(cursor, 1);
    // On a blank line it takes the line, never its indentation.
    EXPECT_EQ(inserted("// mine\n    \n// more\n", 1, s, &cursor), "// mine\nint main()\n{\n    return 0;\n}\n// more\n");
    EXPECT_EQ(cursor, 1);
}

TEST(Boilerplate, APieceIsIndentedLikeTheCodeAroundIt) {
    const Snippet s = piece("a();\nif (b)\n{\n    c();\n}\n");
    const std::string file = "int main()\n{\n    first();\n\n    last();\n}\n";
    // On a blank line: the indentation of the code above.
    EXPECT_EQ(inserted(file, 3, s),
              "int main()\n{\n    first();\n    a();\n    if (b)\n    {\n        c();\n    }\n    last();\n}\n");
    // On a line with code: below it, at its indentation.
    int cursor = -1;
    EXPECT_EQ(inserted(file, 2, s, &cursor),
              "int main()\n{\n    first();\n    a();\n    if (b)\n    {\n        c();\n    }\n\n    last();\n}\n");
    EXPECT_EQ(cursor, 3);
    // Straight after an opening brace: one step in.
    EXPECT_EQ(inserted("void f()\n{\n\n}\n", 2, piece("a();\n")), "void f()\n{\n    a();\n}\n");
    // The blank line's own indentation wins.
    EXPECT_EQ(inserted("void f()\n{\n        \n}\n", 2, piece("a();\n")), "void f()\n{\n        a();\n}\n");
    // Blank lines inside a piece get no trailing spaces.
    EXPECT_EQ(inserted("{\n    x();\n}\n", 1, piece("a();\n\nb();\n")), "{\n    x();\n    a();\n\n    b();\n}\n");
    // The last line of a file without a final newline.
    EXPECT_EQ(inserted("    x();", 0, piece("a();\n")), "    x();\n    a();");
}

TEST(Boilerplate, APiecesMissingIncludesFollowTheFilesLast) {
    const Snippet s = piece("a();\n", {"<SFML/Graphics.hpp>", "<SFML/Audio.hpp>"});
    int cursor = -1;
    EXPECT_EQ(inserted("#include <vector>\n#include <SFML/Graphics.hpp>\n\nint main()\n{\n\n}\n", 5, s, &cursor),
              "#include <vector>\n#include <SFML/Graphics.hpp>\n#include <SFML/Audio.hpp>\n\nint main()\n{\n    a();\n}\n");
    EXPECT_EQ(cursor, 6);
    // Nothing to add when they're all there, however they're spaced.
    EXPECT_EQ(inserted("  #include <SFML/Graphics.hpp>\n#include  <SFML/Audio.hpp>\nx\n", 2, s),
              "  #include <SFML/Graphics.hpp>\n#include  <SFML/Audio.hpp>\nx\na();\n");
    // A commented-out include doesn't count.
    EXPECT_EQ(inserted("// #include <SFML/Audio.hpp>\n#include <SFML/Graphics.hpp>\nx\n", 2, s),
              "// #include <SFML/Audio.hpp>\n#include <SFML/Graphics.hpp>\n#include <SFML/Audio.hpp>\nx\na();\n");
}

TEST(Boilerplate, IncludesStartAFileThatHasNone) {
    const Snippet s = piece("a();\n", {"<gtkmm.h>"});
    int cursor = -1;
    EXPECT_EQ(inserted("void f()\n{\n\n}\n", 2, s, &cursor), "#include <gtkmm.h>\n\nvoid f()\n{\n    a();\n}\n");
    EXPECT_EQ(cursor, 4);
    // After #pragma once and a heading comment.
    EXPECT_EQ(inserted("#pragma once\n\nvoid f()\n{\n\n}\n", 4, s), "#pragma once\n#include <gtkmm.h>\n\nvoid f()\n{\n    a();\n}\n");
    // A file that is only the cursor's blank line.
    EXPECT_EQ(inserted("", 0, s), "#include <gtkmm.h>\na();");
}

TEST(Boilerplate, ColumnsCountUtf16) {
    // "ü" is two bytes and one UTF-16 unit; "😀" four bytes and two units.
    EXPECT_EQ(inserted("x = \"ü😀\";", 0, piece("a();\n")), "x = \"ü😀\";\na();");
}

// Every snippet compiles against the libraries this computer has (the others are skipped).
TEST(Boilerplate, SnippetsCompile) {
    const fs::path dir = fs::temp_directory_path() / ("kevc-boilerplate-" + std::to_string(::getpid()));
    fs::create_directories(dir);
    const sfml::Install sfml = sfml::detect();
    int compiled = 0;
    for (const Snippet& s : boilerplate::all()) {
        std::string flags;
        if (s.sfmlMajor) {
            if (!sfml.installed() || sfml.major() != s.sfmlMajor) continue;
            flags = sfml.pkgConfigVersion.empty() ? "-I" + (sfml.config->dir / "../../../include").lexically_normal().string()
                                                  : runPkgConfig("--cflags", {"sfml-graphics"});
        } else {
            flags = runPkgConfig("--cflags", s.pkgConfig);
            if (flags.empty()) continue;
        }
        const bool cpp = s.language == boilerplate::Language::Cpp;
        std::string text = s.code;
        if (!s.program) {
            text.clear();
            for (const auto& include : s.includes) text += "#include " + include + "\n";
            // A piece goes inside a function; gtkmm's inside a window's.
            if (s.code.find("*this") != std::string::npos)
                text += "struct Test : Gtk::Window\n{\nint f()\n{\n" + s.code + "return 0;\n}\n};\n";
            else text += std::string("int main(") + (cpp ? "" : "void") + ")\n{\n" + s.code + "return 0;\n}\n";
        }
        const fs::path file = dir / (s.id + (cpp ? ".cpp" : ".c"));
        std::ofstream(file) << text;
        const std::string command = std::string(cpp ? "g++ -std=c++20" : "gcc -std=c17") +
                                    " -fsyntax-only -Wall -Wextra -Werror -Wno-unused-variable "
                                    "-Wno-deprecated-declarations " + flags + " " + file.string() + " 2>&1";
        EXPECT_EQ(std::system(command.c_str()), 0) << s.id;
        ++compiled;
    }
    fs::remove_all(dir);
    if (!compiled) GTEST_SKIP() << "neither SFML nor GTK 4 is installed";
}
