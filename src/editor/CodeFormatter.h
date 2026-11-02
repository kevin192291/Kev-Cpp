#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace kevc {

class EditorTab;
class LspManager;
class Toolchain;

// Edit → Auto Format (Ctrl+I): formats an editor's file, or the lines selected in it, the way its
// project would. C and C++ go to clangd, which uses the project's .clang-format, or Kev-C++'s own
// layout (cppstyle::clangFormatStyle) where there's none. Web files go to the project's prettier when
// it has one installed, else to the TypeScript or Svelte language server. The answer goes in as one
// undo step of small edits (format::diffEdits), so breakpoints stay on their lines; if the text
// changed while the tool worked, nothing is applied.
class CodeFormatter {
public:
    using Report = std::function<void(const std::string& status)>;
    CodeFormatter(LspManager& lsp, const Toolchain& toolchain, Report report);

    void format(EditorTab& tab);

private:
    // The folder whose .clang-format holds Kev-C++'s layout (written there when it isn't yet), for
    // clangd's stand-in documents.
    std::filesystem::path styleFolder();
    // `ranged`: only the selected lines were formatted.
    void apply(EditorTab& tab, const std::string& before, const std::string& after, bool ranged,
               const std::string& note = {});

    LspManager& lsp_;
    const Toolchain& toolchain_;
    Report report_;
    // Answers that come after the formatter is gone are dropped.
    std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
};

} // namespace kevc
