#pragma once

#include "core/Format.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kevc::boilerplate {

// Edit → Insert Boilerplate: starting code for the libraries Kev-C++ projects use (SFML, GTK 4),
// either a whole program for an empty file or a piece to drop into one, and what the project has to
// link for it to build.

enum class Language { Cpp, C };

struct Snippet {
    std::string id;      // "sfml3-window"; the menu item's target
    std::string library; // the menu section it's listed under: "SFML 3", "GTK 4 with gtkmm (C++)"
    std::string title;   // "Window and Game Loop"
    Language language = Language::Cpp;
    bool program = false; // a whole file with main(), rather than lines for inside a function
    std::string code;     // indented with 4 spaces from column 0, ending in a newline
    // A piece's #include lines ("<SFML/Graphics.hpp>"), added to the file when it lacks them. A
    // program's are in its code.
    std::vector<std::string> includes;
    std::vector<std::string> pkgConfig;      // modules the project must link: "gtk4", "gtkmm-4.0"
    std::vector<std::string> sfmlComponents; // "Graphics", "Audio": see sfml::packagesFor
    int sfmlMajor = 0;                       // the SFML API it's written for (2 or 3); 0: not SFML
};

// Every snippet, in menu order.
const std::vector<Snippet>& all();
// The ones to offer on a computer whose SFML has this major version (2 or 3).
std::vector<const Snippet*> offered(int sfmlMajor);
const Snippet* find(std::string_view id);

// The language of a source file by its name; nothing for files boilerplate doesn't go in.
std::optional<Language> languageOf(const std::filesystem::path& file);
const char* languageName(Language language); // "C++", "C"

struct Insertion {
    std::vector<format::TextEdit> edits; // against `text`
    int cursorLine = 0;                  // 0-based line of the inserted code's first line, afterwards
};
// Where the snippet goes in a file whose cursor is on `cursorLine` (0-based). A program replaces a
// file that's empty or blank. Otherwise the code takes the cursor's line when that's blank, or goes
// below it, a piece indented like the code around it; includes the file lacks follow its last
// #include (or start the file).
Insertion insertion(std::string_view text, int cursorLine, const Snippet& snippet);

} // namespace kevc::boilerplate
