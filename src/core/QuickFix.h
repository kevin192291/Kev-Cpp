#pragma once

#include "core/Format.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// Possible fixes for an error under the mouse (the hover's lightbulb, Edit → Show Possible Fixes):
// the language server's quick fixes, plus, for C/C++, an #include of each project header that
// declares the name the error says is unknown. clangd finds a header only once its index has it,
// and a header no compiled file includes yet (a new class's) never gets there.
namespace kevc::quickfix {

struct Fix {
    std::string title;                   // what the list shows
    std::string include;                 // "\"TileMap.hpp\"": add this #include, placed when it's applied
    std::vector<format::TextEdit> edits; // otherwise these edits to the document (a server's fix)
    bool operator==(const Fix&) const = default;
};

// The name a C/C++ error says isn't declared: clang's "Unknown type name 'TileMap'", "Use of
// undeclared identifier 'x'", "No member named 'cout' in namespace 'std'" (std::cout), "Variable has
// incomplete type 'X'"; gcc's "'TileMap' was not declared in this scope", "'X' does not name a type",
// "'cout' is not a member of 'std'". nullopt for any other message.
std::optional<std::string> unknownName(std::string_view message);

// The names a C/C++ header declares outside functions and classes: the classes, structs, unions and
// enums it defines (not ones it only forward-declares), unscoped enums' values, aliases, typedefs,
// functions, variables and macros. Names inside namespaces count, without the namespace.
std::set<std::string> declaredNames(std::string_view text);

bool isHeader(const std::filesystem::path& file); // .h, .hpp, .hh, .hxx, .h++, .inl, .ipp, .tpp

// How `file` #includes `header`, quotes and all: the shortest path from the file's own folder or one
// of `includeDirs` that stays inside it ("TileMap.hpp", "gfx/TileMap.hpp"), else from the file's
// folder with "../".
std::string includeSpelling(const std::filesystem::path& file, const std::filesystem::path& header,
                            const std::vector<std::filesystem::path>& includeDirs);

// The headers under some folders that declare a name, remembering each file's names until its size
// or time changes, so hovering error after error doesn't read every header each time.
class HeaderIndex {
public:
    // Headers among the Project tab's files under `folders` (no build output, dot folders or
    // node_modules) declaring `name`; a qualified name ("game::TileMap") is looked up by its last part.
    // At most `limit` headers are read.
    std::vector<std::filesystem::path> find(const std::vector<std::filesystem::path>& folders, std::string_view name,
                                            size_t limit = 5000);

private:
    struct Entry {
        std::filesystem::file_time_type time;
        std::uintmax_t size = 0;
        std::set<std::string> names;
    };
    std::map<std::filesystem::path, Entry> files_;
};

// The #include fixes for `headers` in `file` (its current text), best first: the nearest header
// first. Headers the file already includes, the file itself, and includes one of `serverFixes`
// already adds are left out.
std::vector<Fix> includeFixes(const std::filesystem::path& file, std::string_view text,
                              const std::vector<std::filesystem::path>& headers,
                              const std::vector<std::filesystem::path>& includeDirs,
                              const std::vector<Fix>& serverFixes);

// The fixes in a textDocument/codeAction answer that only edit `file`: CodeActions with an edit,
// preferred ones first. Commands (which the server would carry out itself), disabled actions,
// refactorings and edits to other files are left out. One that only adds an #include line (clangd's
// "Include "TileMap.hpp" for symbol TileMap") becomes an #include fix like includeFixes' own, so it
// reads and goes in the same way whoever found the header.
std::vector<Fix> parseCodeActions(const nlohmann::json& result, const std::filesystem::path& file);

// A diagnostic's message without clangd's " (fix available)" / " (fixes available)": the hover's
// lightbulb says so.
std::string withoutFixCount(std::string message);

} // namespace kevc::quickfix
