#include "lsp/LspManager.h"

#include "core/WebProject.h"
#include "editor/EditorTab.h"

namespace fs = std::filesystem;

namespace kevc {

namespace {

constexpr unsigned kChangeDebounceMs = 300;

std::string lineText(GtkTextBuffer* buffer, int line) {
    GtkTextIter start, end;
    gtk_text_buffer_get_iter_at_line(buffer, &start, line);
    end = start;
    if (!gtk_text_iter_ends_line(&end)) gtk_text_iter_forward_to_line_end(&end);
    char* text = gtk_text_buffer_get_text(buffer, &start, &end, TRUE);
    std::string out(text);
    g_free(text);
    return out;
}

std::string documentText(GtkTextBuffer* buffer) {
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    char* text = gtk_text_buffer_get_text(buffer, &start, &end, TRUE);
    std::string out(text);
    g_free(text);
    return out;
}

std::string hoverContentsToMarkdown(const lsp::json& contents) {
    if (contents.is_string()) return contents.get<std::string>();
    if (contents.is_array()) {
        std::string out;
        for (const auto& part : contents) {
            if (!out.empty()) out += "\n\n";
            out += hoverContentsToMarkdown(part);
        }
        return out;
    }
    if (contents.is_object()) {
        if (contents.contains("kind")) return contents.value("value", "");
        if (contents.contains("language"))
            return "```" + contents.value("language", "") + "\n" + contents.value("value", "") + "\n```";
    }
    return {};
}

bool before(int lineA, int colA, int lineB, int colB) {
    return lineA < lineB || (lineA == lineB && colA < colB);
}

} // namespace

LspManager::LspManager(const Toolchain& toolchain, fs::path toolsDir)
    : toolchain_(toolchain), toolsDir_(std::move(toolsDir)) {}

LspManager::~LspManager() {
    // Editors may already be gone during window teardown: only drop our own state.
    for (auto& [tab, doc] : docs_) {
        doc->flushTimer.disconnect();
        doc->destroyed.disconnect();
    }
    docs_.clear();
    clients_.clear();
}

void LspManager::setWorkspace(const fs::path& root, const Project* project) {
    for (auto& [tab, doc] : docs_) doc->open = false;
    clients_.clear();
    unavailable_.clear();
    root_ = root;
    project_ = project ? std::optional<Project>(*project) : std::nullopt;
    std::error_code ec;
    hadCompileDatabase_ = fs::exists(root_ / "compile_commands.json", ec);
    for (auto& [tab, doc] : docs_)
        if (clientFor(doc->server)) openDocument(*doc);
}

LspManager::json LspManager::fallbackFlags() const {
    json flags = json::array();
    if (project_ && project_->kind == ProjectKind::C) {
        flags.push_back("-std=c" + (project_->standard.empty() ? std::string("17") : project_->standard));
    } else {
        flags.push_back("-std=c++" + (project_ && !project_->standard.empty() ? project_->standard : std::string("20")));
    }
    if (project_) {
        for (const auto& dir : project_->includeDirs)
            flags.push_back("-I" + (fs::path(dir).is_absolute() ? dir : (root_ / dir).string()));
        for (const auto& def : project_->defines) flags.push_back("-D" + def);
    }
    return flags;
}

std::unique_ptr<LspClient> LspManager::createClient(lsp::Server server) {
    const fs::path logs = fs::path(Glib::get_user_cache_dir()) / "kev-cpp" / "logs";
    if (server == lsp::Server::Clangd) {
        const ToolInfo* clangd = toolchain_.get("clangd");
        if (!clangd || !clangd->found()) {
            status_.emit("IntelliSense for C/C++ is off: clangd is not installed (sudo apt install clangd)");
            return nullptr;
        }
        std::vector<std::string> argv{
            clangd->path->string(),
            "--background-index",
            "--query-driver=" + toolchain_.pathOf("gcc").string() + "," + toolchain_.pathOf("g++").string(),
            "--header-insertion=iwyu",
            "--limit-results=300",
            "--pch-storage=memory",
            "--log=error",
        };
        return std::make_unique<LspClient>("clangd", argv, root_, json{{"fallbackFlags", fallbackFlags()}},
                                           logs / "clangd.log");
    }
    if (server == lsp::Server::TypeScript) {
        const fs::path cli = toolsDir_ / "node_modules/typescript-language-server/lib/cli.mjs";
        const fs::path tsserver = toolsDir_ / "node_modules/typescript/lib/tsserver.js";
        std::error_code ec;
        if (toolsDir_.empty() || !fs::exists(cli, ec) || !fs::exists(tsserver, ec)) {
            status_.emit("IntelliSense for TypeScript is off: run `npm install` in Kev-C++'s tools/ folder");
            return nullptr;
        }
        std::vector<std::string> argv{toolchain_.pathOf("node").string(), cli.string(), "--stdio"};
        json init = {{"hostInfo", "kev-cpp"},
                     {"tsserver", {{"path", tsserver.string()}}},
                     {"preferences", {{"includeCompletionsForModuleExports", true}}}};
        // In Svelte projects, .ts files import .svelte components: the plugin lets tsserver follow them.
        const fs::path plugin = toolsDir_ / "node_modules/typescript-svelte-plugin";
        if (WebProject::detect(root_).svelte() && fs::exists(plugin, ec))
            init["plugins"] = json::array({{{"name", "typescript-svelte-plugin"}, {"location", (toolsDir_ / "node_modules").string()}}});
        return std::make_unique<LspClient>("typescript-language-server", argv, root_, init,
                                           logs / "typescript-language-server.log");
    }
    if (server == lsp::Server::Svelte) {
        // The project's own copy if it has one (it matches the project's Svelte), else ours.
        fs::path script = root_ / "node_modules/svelte-language-server/bin/server.js";
        std::error_code ec;
        if (!fs::exists(script, ec)) script = toolsDir_ / "node_modules/svelte-language-server/bin/server.js";
        if (!fs::exists(script, ec)) {
            status_.emit("IntelliSense for Svelte is off: run `npm install` in Kev-C++'s tools/ folder");
            return nullptr;
        }
        std::vector<std::string> argv{toolchain_.pathOf("node").string(), script.string(), "--stdio"};
        auto client = std::make_unique<LspClient>("svelte-language-server", argv, root_,
                                                  lsp::svelteInitializationOptions(), logs / "svelte-language-server.log");
        client->setConfigurationHandler(
            [](const std::string& section) { return lsp::configurationFor(lsp::Server::Svelte, section); });
        return client;
    }
    return nullptr;
}

LspClient* LspManager::clientFor(lsp::Server server) {
    if (server == lsp::Server::None || root_.empty()) return nullptr;
    if (auto it = clients_.find(server); it != clients_.end()) return it->second.get();
    if (unavailable_[server]) return nullptr;

    auto client = createClient(server);
    if (!client) {
        unavailable_[server] = true;
        return nullptr;
    }
    try {
        client->start();
    } catch (const Glib::Error& e) {
        status_.emit(client->name() + " could not start: " + e.what());
        unavailable_[server] = true;
        return nullptr;
    }
    LspClient* raw = client.get();
    raw->signalDiagnostics().connect(sigc::mem_fun(*this, &LspManager::onDiagnostics));
    raw->signalReady().connect([this, raw] { status_.emit("IntelliSense ready (" + raw->name() + ")"); });
    raw->signalExited().connect([this, server, name = raw->name()](const std::string& reason) {
        status_.emit(name + " stopped: " + reason);
        // Don't restart in a loop; a new workspace or compile database will try again.
        unavailable_[server] = true;
        for (auto& [tab, doc] : docs_)
            if (doc->server == server) doc->open = false;
        // Destroy the client later: we are inside one of its callbacks.
        Glib::signal_idle().connect_once([this, server] { clients_.erase(server); });
    });
    clients_[server] = std::move(client);
    return raw;
}

void LspManager::stopServer(lsp::Server server) {
    for (auto& [tab, doc] : docs_)
        if (doc->server == server) doc->open = false;
    clients_.erase(server);
    unavailable_.erase(server);
}

void LspManager::attach(EditorTab* tab) {
    if (docs_.count(tab)) return;
    const lsp::Server server = lsp::serverFor(tab->path());
    if (server == lsp::Server::None) return;

    auto doc = std::make_unique<Document>();
    doc->manager = this;
    doc->tab = tab;
    doc->uri = lsp::pathToUri(tab->path());
    doc->server = server;
    doc->languageId = lsp::languageIdFor(tab->path(), !project_ || project_->kind != ProjectKind::C);
    doc->changedHandler =
        g_signal_connect(tab->buffer(), "changed", G_CALLBACK(&LspManager::onBufferChanged), doc.get());
    doc->destroyed = tab->signal_destroy().connect([this, tab] { detach(tab); });
    Document& ref = *doc;
    docs_[tab] = std::move(doc);
    if (clientFor(server)) openDocument(ref);
}

void LspManager::detach(EditorTab* tab) {
    auto it = docs_.find(tab);
    if (it == docs_.end()) return;
    Document& doc = *it->second;
    doc.flushTimer.disconnect();
    doc.destroyed.disconnect();
    if (doc.changedHandler) g_signal_handler_disconnect(tab->buffer(), doc.changedHandler);
    if (doc.open)
        if (auto c = clients_.find(doc.server); c != clients_.end())
            c->second->notify("textDocument/didClose", {{"textDocument", {{"uri", doc.uri}}}});
    docs_.erase(it);
}

bool LspManager::handles(EditorTab* tab) const {
    const Document* doc = find(tab);
    return doc && clients_.count(doc->server);
}

void LspManager::openDocument(Document& doc) {
    auto it = clients_.find(doc.server);
    if (it == clients_.end()) return;
    doc.flushTimer.disconnect();
    doc.dirty = false;
    doc.open = true;
    it->second->notify("textDocument/didOpen", {{"textDocument",
                                                 {{"uri", doc.uri},
                                                  {"languageId", doc.languageId},
                                                  {"version", ++doc.version},
                                                  {"text", documentText(GTK_TEXT_BUFFER(doc.tab->buffer()))}}}});
}

void LspManager::onBufferChanged(GtkTextBuffer*, gpointer data) {
    auto* doc = static_cast<Document*>(data);
    doc->dirty = true;
    doc->flushTimer.disconnect();
    // The manager owns the Document and disconnects this timer before dropping it.
    doc->flushTimer = Glib::signal_timeout().connect(
        [doc] {
            doc->manager->flush(*doc);
            return false;
        },
        kChangeDebounceMs);
}

void LspManager::flush(Document& doc) {
    if (!doc.dirty) return;
    doc.flushTimer.disconnect();
    doc.dirty = false;
    auto it = clients_.find(doc.server);
    if (it == clients_.end()) return;
    if (!doc.open) {
        openDocument(doc);
        return;
    }
    it->second->notify("textDocument/didChange",
                       {{"textDocument", {{"uri", doc.uri}, {"version", ++doc.version}}},
                        {"contentChanges", {{{"text", documentText(GTK_TEXT_BUFFER(doc.tab->buffer()))}}}}});
}

void LspManager::saved(EditorTab* tab) {
    Document* doc = find(tab);
    if (!doc) return;
    flush(*doc);
    if (auto it = clients_.find(doc->server); it != clients_.end() && doc->open)
        it->second->notify("textDocument/didSave", {{"textDocument", {{"uri", doc->uri}}}});
    // The Svelte server keeps its own copy of the .ts/.js files components import; without this it
    // shows errors from their old contents (sveltejs/language-tools#2008).
    if (doc->server == lsp::Server::TypeScript)
        if (auto it = clients_.find(lsp::Server::Svelte); it != clients_.end())
            it->second->notify("$/onDidChangeTsOrJsFile", {{"uri", doc->uri}});
}

LspManager::Document* LspManager::find(EditorTab* tab) {
    auto it = docs_.find(tab);
    return it == docs_.end() ? nullptr : it->second.get();
}

const LspManager::Document* LspManager::find(EditorTab* tab) const {
    auto it = docs_.find(tab);
    return it == docs_.end() ? nullptr : it->second.get();
}

GtkTextIter LspManager::iterAt(GtkTextBuffer* buffer, int line, int utf16) {
    GtkTextIter iter;
    if (line >= gtk_text_buffer_get_line_count(buffer)) {
        gtk_text_buffer_get_end_iter(buffer, &iter);
        return iter;
    }
    gtk_text_buffer_get_iter_at_line(buffer, &iter, std::max(0, line));
    const int chars = lsp::utf16ToChars(lineText(buffer, line), std::max(0, utf16));
    const int lineChars = gtk_text_iter_get_chars_in_line(&iter);
    // get_chars_in_line counts the newline; never step past it.
    const bool hasNewline = gtk_text_iter_get_line(&iter) < gtk_text_buffer_get_line_count(buffer) - 1;
    gtk_text_iter_set_line_offset(&iter, std::min(chars, lineChars - (hasNewline ? 1 : 0)));
    return iter;
}

LspManager::json LspManager::positionOf(const GtkTextIter& iter) {
    GtkTextBuffer* buffer = gtk_text_iter_get_buffer(&iter);
    const int line = gtk_text_iter_get_line(&iter);
    return {{"line", line}, {"character", lsp::charsToUtf16(lineText(buffer, line), gtk_text_iter_get_line_offset(&iter))}};
}

bool LspManager::completion(EditorTab* tab, const GtkTextIter& at, std::optional<std::string> trigger,
                            std::function<void(const json&, bool)> done) {
    Document* doc = find(tab);
    LspClient* client = doc ? clientFor(doc->server) : nullptr;
    if (!client) return false;
    flush(*doc);
    json context = trigger ? json{{"triggerKind", 2}, {"triggerCharacter", *trigger}} : json{{"triggerKind", 1}};
    client->request("textDocument/completion",
                    {{"textDocument", {{"uri", doc->uri}}}, {"position", positionOf(at)}, {"context", context}},
                    [done](const json& result, const json* error) {
                        if (error || result.is_null()) {
                            done(json::array(), false);
                            return;
                        }
                        if (result.is_array()) done(result, false);
                        else done(result.value("items", json::array()), result.value("isIncomplete", false));
                    });
    return true;
}

void LspManager::hover(EditorTab* tab, const GtkTextIter& at, std::function<void(const std::string&)> done) {
    Document* doc = find(tab);
    LspClient* client = doc && hoverEnabled_ ? clientFor(doc->server) : nullptr;
    if (!client) {
        done({});
        return;
    }
    flush(*doc);
    const json pos = positionOf(at);
    const int line = pos["line"], col = pos["character"];

    // Diagnostics under the cursor go first, like an editor's error tooltip.
    std::string diagnostics;
    for (const auto& d : doc->diagnostics) {
        if (before(line, col, d.startLine, d.startUtf16) || before(d.endLine, d.endUtf16, line, col)) continue;
        static const char* names[] = {"", "error", "warning", "info", "hint"};
        diagnostics += "**" + std::string(names[static_cast<int>(d.severity)]) + ":** " + d.message + "\n\n";
    }
    client->request("textDocument/hover", {{"textDocument", {{"uri", doc->uri}}}, {"position", pos}},
                    [done, diagnostics](const json& result, const json* error) {
                        std::string md = diagnostics;
                        if (!error && result.is_object()) md += hoverContentsToMarkdown(result.value("contents", json()));
                        done(md.empty() ? std::string() : lsp::markdownToPango(md));
                    });
}

void LspManager::definition(EditorTab* tab, const GtkTextIter& at, std::function<void(std::optional<LspLocation>)> done) {
    Document* doc = find(tab);
    LspClient* client = doc ? clientFor(doc->server) : nullptr;
    if (!client) {
        done(std::nullopt);
        return;
    }
    flush(*doc);
    client->request("textDocument/definition", {{"textDocument", {{"uri", doc->uri}}}, {"position", positionOf(at)}},
                    [done](const json& result, const json* error) {
                        if (error || result.is_null() || (result.is_array() && result.empty())) {
                            done(std::nullopt);
                            return;
                        }
                        const json& first = result.is_array() ? result[0] : result;
                        const bool isLink = first.contains("targetUri");
                        const std::string uri = first.value(isLink ? "targetUri" : "uri", "");
                        const json& range = first[isLink ? "targetSelectionRange" : "range"];
                        auto path = lsp::uriToPath(uri);
                        if (!path) {
                            done(std::nullopt);
                            return;
                        }
                        done(LspLocation{*path, range["start"].value("line", 0), range["start"].value("character", 0)});
                    });
}

void LspManager::documentSymbols(EditorTab* tab, std::function<void(const json&)> done) {
    Document* doc = find(tab);
    LspClient* client = doc ? clientFor(doc->server) : nullptr;
    if (!client) return;
    flush(*doc);
    client->request("textDocument/documentSymbol", {{"textDocument", {{"uri", doc->uri}}}},
                    [done](const json& result, const json* error) {
                        done(error || !result.is_array() ? json::array() : result);
                    });
}

std::vector<std::string> LspManager::triggerCharacters(EditorTab* tab) const {
    const Document* doc = find(tab);
    if (!doc) return {};
    auto it = clients_.find(doc->server);
    if (it == clients_.end()) return {};
    const json& caps = it->second->capabilities();
    if (!caps.contains("completionProvider")) return {};
    return caps["completionProvider"].value("triggerCharacters", std::vector<std::string>{});
}

const std::vector<LspDiagnostic>& LspManager::diagnosticsFor(EditorTab* tab) const {
    static const std::vector<LspDiagnostic> none;
    const Document* doc = find(tab);
    return doc ? doc->diagnostics : none;
}

void LspManager::onDiagnostics(const std::string& uri, const json& diagnostics) {
    for (auto& [tab, doc] : docs_) {
        if (doc->uri != uri) continue;
        doc->diagnostics.clear();
        for (const auto& d : diagnostics) {
            LspDiagnostic out;
            out.severity = static_cast<LspDiagnostic::Severity>(std::clamp(d.value("severity", 1), 1, 4));
            out.startLine = d["range"]["start"].value("line", 0);
            out.startUtf16 = d["range"]["start"].value("character", 0);
            out.endLine = d["range"]["end"].value("line", 0);
            out.endUtf16 = d["range"]["end"].value("character", 0);
            out.message = d.value("message", "");
            doc->diagnostics.push_back(std::move(out));
        }
        diagnosticsChanged_.emit(tab);
    }
}

void LspManager::compileDatabaseMaybeChanged() {
    std::error_code ec;
    const bool has = fs::exists(root_ / "compile_commands.json", ec);
    if (has == hadCompileDatabase_) return;
    hadCompileDatabase_ = has;
    if (!clients_.count(lsp::Server::Clangd) && !unavailable_.count(lsp::Server::Clangd)) return;
    stopServer(lsp::Server::Clangd);
    if (clientFor(lsp::Server::Clangd))
        for (auto& [tab, doc] : docs_)
            if (doc->server == lsp::Server::Clangd) openDocument(*doc);
}

} // namespace kevc
