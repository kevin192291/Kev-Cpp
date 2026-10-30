#pragma once

#include "core/Lsp.h"
#include "core/Project.h"
#include "core/Toolchain.h"
#include "lsp/LspClient.h"

#include <gtkmm.h>

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kevc {

class EditorTab;

struct LspDiagnostic {
    enum class Severity { Error = 1, Warning = 2, Information = 3, Hint = 4 };
    Severity severity = Severity::Error;
    int startLine = 0, startUtf16 = 0, endLine = 0, endUtf16 = 0;
    std::string message;
};

struct LspLocation {
    std::filesystem::path file;
    int line = 0;  // 0-based
    int utf16 = 0; // 0-based UTF-16 column
};

// Runs clangd and the TypeScript language server for the open workspace and keeps every open
// editor in sync with them (didOpen / didChange / didSave / didClose).
class LspManager {
public:
    using json = lsp::json;

    LspManager(const Toolchain& toolchain, std::filesystem::path toolsDir);
    ~LspManager();

    // Stops all servers; documents attached later start servers for this root lazily.
    void setWorkspace(const std::filesystem::path& root, const Project* project);

    void attach(EditorTab* tab);
    void detach(EditorTab* tab);
    void saved(EditorTab* tab);
    bool handles(EditorTab* tab) const;

    // Requests run against the latest text (pending edits are sent first).
    // completion() returns false, without calling `done`, when no server handles the file.
    // `incomplete` is the server's isIncomplete flag: typing more should ask again, not filter locally.
    bool completion(EditorTab* tab, const GtkTextIter& at, std::optional<std::string> triggerCharacter,
                    std::function<void(const json& items, bool incomplete)> done);
    void hover(EditorTab* tab, const GtkTextIter& at, std::function<void(const std::string& pangoMarkup)> done);
    void definition(EditorTab* tab, const GtkTextIter& at, std::function<void(std::optional<LspLocation>)> done);
    void documentSymbols(EditorTab* tab, std::function<void(const json& symbols)> done);

    std::vector<std::string> triggerCharacters(EditorTab* tab) const;
    // While the debugger is paused, hovering shows values instead of type information.
    void setHoverEnabled(bool enabled) { hoverEnabled_ = enabled; }
    const std::vector<LspDiagnostic>& diagnosticsFor(EditorTab* tab) const;

    // clangd only reads compile_commands.json at startup; restart it once a build creates one.
    void compileDatabaseMaybeChanged();

    sigc::signal<void(EditorTab*)>& signalDiagnosticsChanged() { return diagnosticsChanged_; }
    sigc::signal<void(const std::string&)>& signalStatus() { return status_; }

    // GTK <-> LSP position helpers.
    static GtkTextIter iterAt(GtkTextBuffer* buffer, int line, int utf16);
    static json positionOf(const GtkTextIter& iter);

private:
    struct Document {
        LspManager* manager = nullptr;
        EditorTab* tab = nullptr;
        std::string uri;
        std::string languageId;
        lsp::Server server = lsp::Server::None;
        int version = 0;
        bool open = false;  // didOpen sent to the current client
        bool dirty = false; // edits not yet sent
        sigc::connection flushTimer;
        sigc::connection destroyed;
        gulong changedHandler = 0;
        std::vector<LspDiagnostic> diagnostics;
    };

    LspClient* clientFor(lsp::Server server);
    std::unique_ptr<LspClient> createClient(lsp::Server server);
    void openDocument(Document& doc);
    void flush(Document& doc);
    Document* find(EditorTab* tab);
    const Document* find(EditorTab* tab) const;
    void onDiagnostics(const std::string& uri, const json& diagnostics);
    void stopServer(lsp::Server server);
    json fallbackFlags() const;
    static void onBufferChanged(GtkTextBuffer*, gpointer doc);

    const Toolchain& toolchain_;
    std::filesystem::path toolsDir_;
    std::filesystem::path root_;
    std::optional<Project> project_;
    bool hadCompileDatabase_ = false;
    bool hoverEnabled_ = true;
    std::map<EditorTab*, std::unique_ptr<Document>> docs_;
    std::map<lsp::Server, std::unique_ptr<LspClient>> clients_;
    std::map<lsp::Server, bool> unavailable_; // reported once per workspace
    sigc::signal<void(EditorTab*)> diagnosticsChanged_;
    sigc::signal<void(const std::string&)> status_;
};

} // namespace kevc
