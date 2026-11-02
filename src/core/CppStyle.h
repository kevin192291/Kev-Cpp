#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kevc::cppstyle {

// The C/C++ layout Kev-C++ keeps while you type (Allman braces, Dev-C++ style):
//
//   class Window : public Widget
//   {
//   public:
//       void show();
//   };
//
// Braces go on lines of their own, access specifiers sit at the class's own indentation, members one
// level in, and a class body's closing brace gets its semicolon. GTK-free, so it's tested; the editor
// calls these from its indenter (editor/CppIndenter) on Return, ':', '{' and '}'.

std::string indentOf(std::string_view line);

// The brackets still open at the end of `code`, innermost last, each with the indentation of the line
// it's on. Comments, strings, character literals and raw strings are skipped.
struct Open {
    char bracket = '{';
    size_t pos = 0;
    std::string lineIndent;
};
std::vector<Open> openBrackets(std::string_view code);

// Return: the indentation of the new line. `before` is the text up to the start of the new line
// (ending in '\n'); `after` is what follows the cursor on it (a '}' there closes a level).
std::string newLineIndent(std::string_view before, std::string_view after, const std::string& unit);

// ':' finishing an access specifier (public:, private:, protected:, signals:…): the indentation it
// gets, its class's. `before` is the text before the line, `line` the line so far. nullopt otherwise.
std::optional<std::string> accessSpecifierIndent(std::string_view before, std::string_view line);

// '{' typed at the end of `line` (which ends with it) after a class, struct, namespace, function or
// control statement header: what the line becomes with the brace on a line of its own
// ("class Foo\n{"). nullopt when the brace stays: initialisers, lambdas, strings, an empty line.
std::optional<std::string> braceOnOwnLine(std::string_view line);

// '}' typed. `before`: the text up to it (not including it); `lineBefore`: its line up to it.
struct Closing {
    std::optional<std::string> indent; // when the '}' starts its line: the matching '{' line's indent
    bool semicolon = false;            // it closes a class, struct, union or enum body
};
Closing closingBrace(std::string_view before, std::string_view lineBefore);

// The same layout as a .clang-format, for Edit → Auto Format in projects that have none of their own.
// Lines up to 120 columns; the bodies of ifs, loops and case labels on lines of their own, short
// member functions inside a class on one line. Includes keep their order.
std::string clangFormatStyle();

} // namespace kevc::cppstyle
