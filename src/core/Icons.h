#pragma once

#include <filesystem>
#include <string>
#include <string_view>

// Which kevc-* icon (data/icons/hicolor/*/actions, cut from data/art/icon-sprite-sheet.png by
// make_icons.py) stands for a file, a code symbol or a debugger value.
namespace kevc::icons {

// Project tree.
std::string forFile(const std::filesystem::path& file);
std::string forFolder(bool expanded);

// Classes panel: an LSP SymbolKind. Variables inside a function (`parentKind` Function, Method or
// Constructor) are locals; elsewhere they're globals. clangd reports a struct as a Class whose
// `detail` is "struct".
std::string forSymbolKind(int kind, int parentKind = 0, std::string_view detail = {});

// Completion popup: an LSP CompletionItemKind.
std::string forCompletionKind(int kind);

// Locals and Watch: the type gdb reports, e.g. "int", "const char *", "double [3]", "std::string".
std::string forType(std::string_view type);

} // namespace kevc::icons
