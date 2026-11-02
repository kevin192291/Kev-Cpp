#include "editor/CodeFormatter.h"

#include "core/CppStyle.h"
#include "core/Format.h"
#include "core/Toolchain.h"
#include "editor/EditorTab.h"
#include "lsp/LspManager.h"
#include "process/Capture.h"

#include <fstream>
#include <iterator>
#include <optional>

namespace fs = std::filesystem;

namespace kevc {

namespace {

std::string textOf(GtkTextBuffer* buffer) {
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    char* text = gtk_text_buffer_get_text(buffer, &start, &end, TRUE);
    std::string out(text);
    g_free(text);
    return out;
}

// prettier's first complaint, without its "[error] /path/to/file: " prefix.
std::string firstLine(std::string text, const fs::path& file) {
    if (const size_t nl = text.find('\n'); nl != std::string::npos) text.resize(nl);
    if (text.starts_with("[error] ")) text.erase(0, 8);
    if (const std::string path = file.string() + ": "; text.starts_with(path)) text.erase(0, path.size());
    return text.empty() ? std::string("it failed") : text;
}

} // namespace

CodeFormatter::CodeFormatter(LspManager& lsp, const Toolchain& toolchain, Report report)
    : lsp_(lsp), toolchain_(toolchain), report_(std::move(report)) {}

fs::path CodeFormatter::styleFolder() {
    const fs::path dir = fs::path(Glib::get_user_cache_dir()) / "kev-cpp" / "format";
    const fs::path file = dir / ".clang-format";
    const std::string style = cppstyle::clangFormatStyle();
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::ifstream in(file, std::ios::binary);
    const std::string current((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (current != style) std::ofstream(file, std::ios::binary | std::ios::trunc) << style;
    return dir;
}

void CodeFormatter::format(EditorTab& tab) {
    const fs::path file = tab.path();
    const std::string name = file.filename().string();
    GtkTextBuffer* buffer = GTK_TEXT_BUFFER(tab.buffer());
    const std::string before = textOf(buffer);
    std::optional<std::pair<GtkTextIter, GtkTextIter>> selection;
    if (GtkTextIter from, to; gtk_text_buffer_get_selection_bounds(buffer, &from, &to)) selection.emplace(from, to);

    const format::Prettier prettier = format::findPrettier(file);
    const format::Formatter formatter = format::formatterFor(file, !prettier.script.empty());
    if (formatter == format::Formatter::None) {
        const std::string ext = file.extension().string();
        report_("Auto Format has nothing for " + (ext.empty() ? name : ext + " files") +
                ": it formats C, C++, TypeScript, JavaScript and Svelte (CSS, JSON, HTML and more in projects with "
                "prettier)");
        return;
    }
    if (formatter == format::Formatter::Clangd && !lsp_.handles(&tab)) {
        const ToolInfo* clangd = toolchain_.get("clangd");
        report_(clangd && clangd->found() ? "Auto Format for C and C++ needs clangd, which isn't running"
                                          : "Auto Format for C and C++ needs clangd: sudo apt install clangd");
        return;
    }
    report_("Formatting " + name + "…");

    // The tab may close while the tool works; its answer is dropped then.
    auto gone = std::make_shared<bool>(false);
    auto watch = std::make_shared<sigc::connection>(tab.signal_destroy().connect([gone] { *gone = true; }));
    std::weak_ptr<int> alive = lifetime_;
    EditorTab* target = &tab;
    auto stillThere = [alive, gone, watch] {
        if (alive.expired() || *gone) return false;
        watch->disconnect();
        return true;
    };

    if (formatter == format::Formatter::Prettier) {
        std::vector<std::string> argv{toolchain_.pathOf("node").string(), prettier.script.string(), "--stdin-filepath",
                                      file.string()};
        if (selection) {
            // prettier widens the range to the statements it touches.
            const auto from = LspManager::positionOf(selection->first), to = LspManager::positionOf(selection->second);
            auto offset = [&before](const lsp::json& at) {
                return std::to_string(format::utf16Offset(before, at.value("line", 0), at.value("character", 0)));
            };
            argv.insert(argv.end(), {"--range-start", offset(from), "--range-end", offset(to)});
        }
        const bool ranged = selection.has_value();
        captureOutput(
            argv, prettier.root, before,
            [this, target, stillThere, before, ranged, name, file](std::string out, std::string errors, int status) {
                if (!stillThere()) return;
                if (status != 0) report_("prettier couldn't format " + name + ": " + firstLine(errors, file));
                else apply(*target, before, out, ranged);
            },
            alive);
        return;
    }

    // clangd uses the project's .clang-format; without one, a stand-in in our style folder.
    const bool ownStyle = formatter == format::Formatter::Clangd && !format::clangFormatConfig(file);
    const fs::path standIns = ownStyle ? styleFolder() : fs::path();
    const bool wanted = selection.has_value();
    lsp_.format(&tab, selection, standIns,
                [this, target, stillThere, before, wanted, name](std::vector<format::TextEdit> edits, bool ranged,
                                                                 std::string error) {
                    if (!stillThere()) return;
                    if (!error.empty()) {
                        report_("Couldn't format " + name + ": " + error);
                        return;
                    }
                    apply(*target, before, format::applyTextEdits(before, edits), ranged,
                          wanted && !ranged ? " (its language server formats whole files only)" : "");
                });
}

void CodeFormatter::apply(EditorTab& tab, const std::string& before, const std::string& after, bool ranged,
                          const std::string& note) {
    const std::string name = tab.path().filename().string();
    if (textOf(GTK_TEXT_BUFFER(tab.buffer())) != before) {
        report_(name + " changed while it was being formatted: press Ctrl+I again");
        return;
    }
    if (after == before) {
        report_((ranged ? "The selected lines of " + name + " are" : name + " is") + " already formatted" + note);
        return;
    }
    tab.applyEdits(format::diffEdits(before, after));
    report_("Formatted " + (ranged ? "the selected lines of " + name : name) + note);
}

} // namespace kevc
