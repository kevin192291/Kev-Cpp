#include "core/ManagedBlock.h"

#include <cstring>

namespace kevc::managed_block {

namespace {

struct Span {
    size_t beginLine; // offset of the begin-marker line
    size_t bodyStart; // offset just past the begin-marker line
    size_t endLine;   // offset of the end-marker line
    size_t endAfter;  // offset just past the end-marker line (including its newline, if any)
};

// Finds a marker that starts a line. Returns npos if absent.
size_t findLineStarting(const std::string& text, const char* marker, size_t from = 0) {
    size_t pos = from;
    while ((pos = text.find(marker, pos)) != std::string::npos) {
        if (pos == 0 || text[pos - 1] == '\n') return pos;
        pos += std::strlen(marker);
    }
    return std::string::npos;
}

size_t pastLine(const std::string& text, size_t pos) {
    size_t nl = text.find('\n', pos);
    return nl == std::string::npos ? text.size() : nl + 1;
}

std::optional<Span> locate(const std::string& text) {
    size_t begin = findLineStarting(text, kBeginMarker);
    size_t end = findLineStarting(text, kEndMarker);
    if (begin == std::string::npos && end == std::string::npos) return std::nullopt;
    if (begin == std::string::npos) throw MalformedBlock("end marker without a begin marker");
    if (end == std::string::npos || end < begin) throw MalformedBlock("begin marker without an end marker");
    if (findLineStarting(text, kBeginMarker, begin + 1) != std::string::npos)
        throw MalformedBlock("more than one managed block");
    return Span{begin, pastLine(text, begin), end, pastLine(text, end)};
}

} // namespace

std::optional<std::string> extract(const std::string& text) {
    auto span = locate(text);
    if (!span) return std::nullopt;
    return text.substr(span->bodyStart, span->endLine - span->bodyStart);
}

std::string wrap(const std::string& body) {
    std::string out = std::string(kBeginMarker) + kBeginNotice + "\n";
    out += body;
    if (!body.empty() && body.back() != '\n') out += '\n';
    out += std::string(kEndMarker) + "\n";
    return out;
}

std::string splice(const std::string& text, const std::string& body) {
    auto span = locate(text);
    if (!span) {
        std::string out = text;
        if (!out.empty() && out.back() != '\n') out += '\n';
        if (!out.empty()) out += '\n';
        return out + wrap(body);
    }
    std::string block = wrap(body);
    // Keep the original end-of-file state when the end marker was the last line without a newline.
    if (span->endAfter == text.size() && text.back() != '\n') block.pop_back();
    return text.substr(0, span->beginLine) + block + text.substr(span->endAfter);
}

} // namespace kevc::managed_block
