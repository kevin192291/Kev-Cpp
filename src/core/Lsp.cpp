#include "core/Lsp.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace kevc::lsp {

namespace fs = std::filesystem;

void MessageFramer::feed(std::string_view bytes) {
    buffer_.append(bytes);
}

std::optional<json> MessageFramer::next() {
    const auto headerEnd = buffer_.find("\r\n\r\n");
    if (headerEnd == std::string::npos) return std::nullopt;

    std::optional<size_t> length;
    size_t pos = 0;
    while (pos < headerEnd) {
        size_t eol = buffer_.find("\r\n", pos);
        if (eol == std::string::npos || eol > headerEnd) eol = headerEnd;
        std::string line = buffer_.substr(pos, eol - pos);
        const auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string name = line.substr(0, colon);
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
            if (name == "content-length") {
                try {
                    length = std::stoul(line.substr(colon + 1));
                } catch (const std::exception&) {
                    throw std::runtime_error("bad Content-Length header: " + line);
                }
            }
        }
        pos = eol + 2;
    }
    if (!length) throw std::runtime_error("message without Content-Length");

    const size_t bodyStart = headerEnd + 4;
    if (buffer_.size() < bodyStart + *length) return std::nullopt;
    json message = json::parse(buffer_.begin() + static_cast<std::ptrdiff_t>(bodyStart),
                               buffer_.begin() + static_cast<std::ptrdiff_t>(bodyStart + *length));
    buffer_.erase(0, bodyStart + *length);
    return message;
}

std::string frame(const json& message) {
    const std::string body = message.dump();
    return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

std::string pathToUri(const fs::path& path) {
    static const char* hex = "0123456789ABCDEF";
    std::string out = "file://";
    for (unsigned char c : fs::absolute(path).lexically_normal().string()) {
        if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::optional<fs::path> uriToPath(std::string_view uri) {
    constexpr std::string_view scheme = "file://";
    if (!uri.starts_with(scheme)) return std::nullopt;
    uri.remove_prefix(scheme.size());
    if (!uri.starts_with('/')) {
        // file://host/path: only an empty host or localhost is local.
        const auto slash = uri.find('/');
        if (slash == std::string_view::npos || uri.substr(0, slash) != "localhost") return std::nullopt;
        uri.remove_prefix(slash);
    }
    std::string out;
    for (size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size() && std::isxdigit(static_cast<unsigned char>(uri[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(uri[i + 2]))) {
            out += static_cast<char>(std::stoi(std::string(uri.substr(i + 1, 2)), nullptr, 16));
            i += 2;
        } else {
            out += uri[i];
        }
    }
    return fs::path(out);
}

namespace {

// Decodes one UTF-8 code point starting at `i`; returns its byte length (1 for invalid bytes).
size_t utf8Length(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead >> 5) == 0x6) return 2;
    if ((lead >> 4) == 0xE) return 3;
    if ((lead >> 3) == 0x1E) return 4;
    return 1;
}

} // namespace

int utf16ToChars(std::string_view line, int utf16Column) {
    int units = 0;
    int chars = 0;
    for (size_t i = 0; i < line.size() && units < utf16Column;) {
        const size_t len = utf8Length(static_cast<unsigned char>(line[i]));
        units += len == 4 ? 2 : 1;
        ++chars;
        i += len;
    }
    // Columns past the end of the line (e.g. a range ending at the newline) are kept as-is.
    return units < utf16Column ? chars + (utf16Column - units) : chars;
}

int charsToUtf16(std::string_view line, int charColumn) {
    int units = 0;
    int chars = 0;
    for (size_t i = 0; i < line.size() && chars < charColumn;) {
        const size_t len = utf8Length(static_cast<unsigned char>(line[i]));
        units += len == 4 ? 2 : 1;
        ++chars;
        i += len;
    }
    return chars < charColumn ? units + (charColumn - chars) : units;
}

namespace {

std::string escape(std::string_view text) {
    std::string out;
    for (char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}

// Inline formatting for one line of prose: `code`, **bold**, and Markdown backslash escapes.
std::string inlineMarkup(std::string_view line) {
    std::string out;
    bool bold = false;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\\' && i + 1 < line.size() && std::ispunct(static_cast<unsigned char>(line[i + 1]))) {
            out += escape(line.substr(++i, 1));
        } else if (line[i] == '`') {
            const auto close = line.find('`', i + 1);
            if (close == std::string_view::npos) {
                out += '`';
                continue;
            }
            out += "<tt>" + escape(line.substr(i + 1, close - i - 1)) + "</tt>";
            i = close;
        } else if (line.substr(i, 2) == "**") {
            out += bold ? "</b>" : "<b>";
            bold = !bold;
            ++i;
        } else {
            out += escape(line.substr(i, 1));
        }
    }
    if (bold) out += "</b>";
    return out;
}

} // namespace

std::string markdownToPango(std::string_view md) {
    std::string out;
    bool inFence = false;
    size_t pos = 0;
    while (pos <= md.size()) {
        auto eol = md.find('\n', pos);
        if (eol == std::string_view::npos) eol = md.size();
        std::string_view line = md.substr(pos, eol - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        pos = eol + 1;

        if (line.starts_with("```")) {
            inFence = !inFence;
            out += inFence ? "<tt>" : "</tt>";
            continue;
        }
        if (inFence) {
            out += escape(line) + "\n";
            continue;
        }
        if (line == "---" || line == "***" || line == "___") continue;
        if (line.starts_with("#")) {
            const auto text = line.find_first_not_of("# ");
            out += "<b>" + (text == std::string_view::npos ? std::string() : inlineMarkup(line.substr(text))) + "</b>\n";
            continue;
        }
        out += inlineMarkup(line) + "\n";
        if (eol == md.size()) break;
    }
    if (inFence) out += "</tt>";
    // Fenced blocks end with a newline before </tt>; tidy trailing whitespace.
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
    std::string tidy;
    for (size_t i = 0; i < out.size(); ++i) {
        if (out.compare(i, 7, "\n</tt>") == 0) {
            tidy += "</tt>";
            i += 5;
        } else {
            tidy += out[i];
        }
    }
    return tidy;
}

Server serverFor(const fs::path& file) {
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    static const char* cFamily[] = {".c", ".h", ".cpp", ".cc", ".cxx", ".c++", ".hpp", ".hh", ".hxx", ".inl", ".tpp"};
    static const char* tsFamily[] = {".ts", ".tsx", ".js", ".jsx", ".mjs", ".cjs", ".mts", ".cts"};
    for (const char* e : cFamily)
        if (ext == e) return Server::Clangd;
    for (const char* e : tsFamily)
        if (ext == e) return Server::TypeScript;
    if (ext == ".svelte") return Server::Svelte;
    return Server::None;
}

std::string languageIdFor(const fs::path& file, bool cppHeaders) {
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    if (ext == ".c") return "c";
    if (ext == ".h") return cppHeaders ? "cpp" : "c";
    if (ext == ".ts" || ext == ".mts" || ext == ".cts") return "typescript";
    if (ext == ".tsx") return "typescriptreact";
    if (ext == ".jsx") return "javascriptreact";
    if (ext == ".js" || ext == ".mjs" || ext == ".cjs") return "javascript";
    if (ext == ".svelte") return "svelte";
    if (serverFor(file) == Server::Clangd) return "cpp";
    return "plaintext";
}

json svelteInitializationOptions() {
    const json on = {{"enable", true}};
    return {
        {"configuration",
         {{"svelte",
           {{"plugin",
             {{"typescript",
               {{"enable", true}, {"diagnostics", on}, {"hover", on}, {"completions", on}, {"definitions", on},
                {"documentSymbols", on}, {"signatureHelp", on}, {"codeActions", on}, {"rename", on}}},
              {"css", {{"enable", true}, {"diagnostics", on}, {"hover", on}, {"completions", {{"enable", true}, {"emmet", false}}}}},
              {"html", {{"enable", true}, {"hover", on}, {"tagComplete", on}, {"completions", {{"enable", true}, {"emmet", false}}}}},
              {"svelte", {{"enable", true}, {"diagnostics", on}, {"hover", on}, {"completions", on}, {"compilerWarnings", json::object()}}}}}}},
          {"typescript", json::object()},
          {"javascript", json::object()},
          {"prettier", json::object()},
          {"emmet", json::object()},
          {"css", json::object()},
          {"html", json::object()}}},
        // Kev-C++ filters completions itself as you type (see LspBridges).
        {"dontFilterIncompleteCompletions", true},
        {"isTrustedWorkspace", true},
    };
}

json configurationFor(Server server, const std::string& section) {
    if (server != Server::Svelte) return nullptr;
    const json config = svelteInitializationOptions()["configuration"];
    // Sections may be dotted paths, e.g. "svelte.plugin.typescript".
    json node = config;
    size_t start = 0;
    while (start <= section.size()) {
        const size_t dot = section.find('.', start);
        const std::string key = section.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (!node.is_object() || !node.contains(key)) return json::object();
        node = node[key];
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return node;
}

} // namespace kevc::lsp
