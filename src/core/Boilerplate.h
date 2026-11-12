#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The Boilerplate menu: starters and patterns kept as plain files in folder trees. Folders are
// submenus and each file is an item; data/boilerplate/_README.md describes the format for people
// adding their own. This is the part without GTK: reading the folders, the files' headers, and
// turning a file's text into what goes into the editor.
namespace kevc::boilerplate {

// What a snippet's or a folder's header says. Every key is optional.
struct Header {
    std::string label; // the menu text; empty: the file or folder name
    int order = 100;   // lower first; then alphabetical
    std::string section; // neighbours in different sections get a separator between them
    std::string description;
    std::string file;  // the name suggested for a new file
    bool wholeFile = false;
    std::vector<std::pair<std::string, std::string>> asks; // name, default value
    std::vector<std::string> packages;                      // pkg-config modules
    std::vector<std::string> includes; // "<vector>", "\"player.h\"": added to a C/C++ file that lacks them
    int sfml = 0; // the SFML major version it's written for; only shown when that's installed (or none)
};

// A snippet file's text split into its header (between two "---" lines at the very top) and the text
// that gets inserted.
struct Snippet {
    Header header;
    std::string body;
};
Snippet parseSnippet(std::string_view text);
// A folder's _folder.txt: the same "key: value" lines, without the "---" lines.
Header parseFolderFile(std::string_view text);

// One item or submenu of the menu.
struct Entry {
    std::string name;  // on disk; what entries from different roots are merged by
    std::string label; // shown
    int order = 100;
    std::string section;
    bool folder = false;
    std::vector<Entry> children; // a folder's, sorted
    std::string key;             // a snippet's path relative to its root ("C and C++/SFML/Window.cpp")
    std::filesystem::path file;  // a snippet's file
    int sfml = 0;                // a snippet's Header::sfml
};

// The folders merged into one tree, sorted: a later root's file replaces an earlier one's at the same
// place, and its _folder.txt replaces the earlier folder's header. Names starting with '_' or '.',
// and editors' backups ending in '~', aren't items; folders with no items are left out.
Entry scan(const std::vector<std::filesystem::path>& roots);
// The tree without the snippets written for another SFML than `installedMajor` (2 or 3), and without
// the folders that leaves empty: SFML 2 code doesn't build against SFML 3, nor the other way round.
Entry forSfml(const Entry& root, int installedMajor);
// Labels, keys, orders and sections in one string: equal for trees that make the same menu.
std::string signature(const Entry& root);
// The labels from the top of the menu down to a snippet ("C/C++", "SFML", "Window"); empty if the key
// isn't in the tree.
std::vector<std::string> labelPath(const Entry& root, const std::string& key);
// The file a key names: the last root that has it (the one the menu shows). Keys that leave the root
// find nothing.
std::optional<std::filesystem::path> locate(const std::vector<std::filesystem::path>& roots, const std::string& key);

// A label for a GMenu item: GTK reads '_' as a mnemonic, so underscores are doubled.
std::string menuLabel(std::string_view label);

// Whether a snippet (by its file name: its extension is its language) belongs in a file: C++ snippets
// in C++ files and headers, CSS in CSS, Svelte and HTML files, a .txt snippet anywhere, and so on.
bool fits(const std::filesystem::path& snippetFile, const std::filesystem::path& target);

// "ClassName" → "Class name": what the box asking for an `ask:` value says.
std::string promptFor(std::string_view name);
// A case modifier applied to a value ("upper", "lower", "snake", "kebab", "camel", "pascal");
// nullopt for an unknown modifier.
std::optional<std::string> applyCase(std::string_view value, std::string_view modifier);

using Values = std::map<std::string, std::string>;
// Replaces {{name}} and {{name:modifier…}} where `name` is in `values`; anything else in double braces
// stays as it is. For `file:` suggestions; insertion uses expand().
std::string substitute(std::string_view text, const Values& values);

// What replaces the selection (or goes in at the cursor) in the editor.
struct Insertion {
    std::string text;
    size_t cursor = std::string::npos; // byte offset in `text` where the cursor goes; npos: the end
    // The text replaces the line's indentation before the insertion point too (it's indented itself).
    bool fromLineStart = false;
};
// A snippet's body made ready to insert. `linePrefix` is the text between the start of the line and
// the insertion point (the selection's start), `selection` the selected text, and `restOfLine` what
// follows the selection on its last line. The body is indented like the line it goes into,
// {{selection}} becomes the selection re-indented to fit, {{cursor}} marks where the cursor goes, and
// `values` fill the other placeholders. The body's final newline is dropped unless `keepFinalNewline`
// (a whole new file); text that starts its own line and has text after it ends with a line break, so
// that text stays on a line of its own, at its indentation.
Insertion expand(std::string_view body, std::string_view linePrefix, std::string_view selection,
                 std::string_view restOfLine, const Values& values, bool keepFinalNewline);

// The #include lines `includes` asks for that a C/C++ file's text lacks, and where they go: after the
// file's last #include, or at the top (below a leading comment and #pragma once) when it has none.
// nullopt when nothing is missing.
struct IncludeInsertion {
    size_t offset = 0; // bytes into the text
    std::string text;
};
std::optional<IncludeInsertion> missingIncludes(std::string_view text, const std::vector<std::string>& includes);

// The indentation for code typed on a blank line that has none, given the text above that line: the
// last non-blank line's, one step further in when it opens a block ("{", or ":" after public/case).
std::string indentForBlankLine(std::string_view textAbove);

// The text of a selection saved as a new snippet: its common indentation removed, ending in a newline.
std::string dedent(std::string_view text);
// Where Save Selection as Boilerplate puts `menuPath` ("C and C++/My Boilerplate/Swap Two Values")
// with the selected file's extension, inside `root`; nullopt (and `error` says why) for a path that's
// empty, leaves the folder, or has a part the menu would ignore.
std::optional<std::filesystem::path> savePath(const std::filesystem::path& root, std::string_view menuPath,
                                              const std::string& extension, std::string& error);
// The extension a selection from `file` is saved with: its own, or what stands for files known by
// name (CMakeLists.txt → .cmake, Makefile → .mk, .gitignore → .gitignore).
std::string snippetExtension(const std::filesystem::path& file);
// The folder Save Selection as Boilerplate suggests for a file: the built-in menu for its language
// ("C and C++/My Boilerplate"), or "My Boilerplate".
std::string suggestedFolder(const std::filesystem::path& file);

} // namespace kevc::boilerplate
