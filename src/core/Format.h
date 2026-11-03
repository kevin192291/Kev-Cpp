#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kevc::format {

// Edit → Auto Format's GTK-free half: which tool formats a file, the edits it answers with, and the
// small edits that turn the old text into the formatted one (so breakpoints stay on their lines).

// Replace the range [start, end) with `newText`. Lines are 0-based; columns count UTF-16 code units,
// as LSP's do. Every edit in a list refers to the text before any of them is applied.
struct TextEdit {
    int startLine = 0, startUtf16 = 0, endLine = 0, endUtf16 = 0;
    std::string newText;
    bool operator==(const TextEdit&) const = default;
};

// A language server's TextEdit[] (anything else: none).
std::vector<TextEdit> parseTextEdits(const nlohmann::json& edits);

// The order to apply edits in, one at a time, so each one's range is still where it was: last first.
// Insertions at the same place keep the order they had in `edits` (as LSP says they must), so this
// takes edits in a server's order or diffEdits', not ones it already sorted.
std::vector<TextEdit> lastFirst(std::vector<TextEdit> edits);

std::string applyTextEdits(std::string_view text, const std::vector<TextEdit>& edits);

// The edits that turn `before` into `after`, each as small as it can be. Lines are matched ignoring
// whitespace, so a line that only moved or changed spacing gets an edit inside it rather than being
// replaced: a breakpoint's mark at its start stays on it. Lines split or joined are edited in place
// too ("{" moved onto a line of its own is one inserted newline). In the order they come in the text.
std::vector<TextEdit> diffEdits(std::string_view before, std::string_view after);

// How many UTF-16 code units come before (line, column) in `text`: the offsets prettier's
// --range-start and --range-end take.
int utf16Offset(std::string_view text, int line, int utf16Column);

// The .clang-format (or _clang-format) Auto Format uses for `file`: the nearest in its folder or one
// above, up to the top of the project folder or of the git repository holding the file, whichever is
// higher (only its own folder for a file in neither). One further up isn't the project's, and the
// home folder's never counts: clang-format itself would take a ~/.clang-format made once for something
// else, and lay out every project in the home folder that has none with it (LLVM's 2-space indents
// in place of Kev-C++'s 4). Where this finds none, Kev-C++'s own layout applies.
std::optional<std::filesystem::path> clangFormatConfig(const std::filesystem::path& file,
                                                       const std::filesystem::path& projectFolder,
                                                       const std::filesystem::path& home);

// prettier as the project installed it: the script `node` runs, and the folder whose node_modules has it,
// looked for from the file's folder up. Empty `script` if there's none.
struct Prettier {
    std::filesystem::path script, root;
};
Prettier findPrettier(const std::filesystem::path& file);

enum class Formatter {
    None,
    Clangd,     // C and C++
    Prettier,   // JavaScript, TypeScript, CSS, JSON, HTML, Markdown, YAML… in projects that have prettier
    TypeScript, // typescript-language-server, for scripts in projects without prettier
    Svelte,     // svelte-language-server (it runs prettier itself, the project's or its own)
};
Formatter formatterFor(const std::filesystem::path& file, bool projectHasPrettier);

// clangd formats a file with the .clang-format it finds above it. Where a project has none, Kev-C++
// asks clangd to format a stand-in document in a folder of its own that holds Kev-C++'s style (see
// cppstyle::clangFormatStyle). The stand-in's extension is one clangd can't compile, so it never
// parses or indexes the copy (its symbols would turn up in Find All References), while clang-format
// reads any extension it doesn't know as C++. Each request gets its own name.
std::string standInName(int serial);

} // namespace kevc::format
