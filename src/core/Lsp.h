#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace kevc::lsp {

using json = nlohmann::json;

// Splits a byte stream of "Content-Length: N\r\n\r\n<json>" messages.
class MessageFramer {
public:
    void feed(std::string_view bytes);
    // Next complete message, or nullopt if more bytes are needed.
    // Throws std::runtime_error on a malformed header or body.
    std::optional<json> next();

private:
    std::string buffer_;
};

std::string frame(const json& message);

// file:// URIs, with percent-encoding of anything outside the unreserved set and '/'.
std::string pathToUri(const std::filesystem::path& path);
std::optional<std::filesystem::path> uriToPath(std::string_view uri);

// LSP positions count UTF-16 code units; GTK counts characters (code points).
int utf16ToChars(std::string_view utf8Line, int utf16Column);
int charsToUtf16(std::string_view utf8Line, int charColumn);

// Renders hover/documentation Markdown as Pango markup: fenced and inline code in monospace,
// **bold**, headings in bold, horizontal rules dropped, everything else escaped.
std::string markdownToPango(std::string_view markdown);

enum class Server { None, Clangd, TypeScript, Svelte };

// Which language server handles a file, and the LSP languageId to send for it.
Server serverFor(const std::filesystem::path& file);
std::string languageIdFor(const std::filesystem::path& file, bool cppHeaders);

// svelte-language-server's initializationOptions: every feature on, settings for its plugins.
json svelteInitializationOptions();
// The answer to a workspace/configuration request for `section` ("svelte", "typescript", ...).
// Servers merge it over their defaults; null tells some of them (Svelte's) nothing at all.
json configurationFor(Server server, const std::string& section);

} // namespace kevc::lsp
