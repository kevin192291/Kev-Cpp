#include "core/Diagnostics.h"

#include <nlohmann/json.hpp>

#include <regex>

namespace kevc {

namespace {

// main.cpp:12:5: error: 'x' was not declared in this scope
const std::regex kGcc(R"(^(.+?):(\d+):(?:(\d+):)?\s+(fatal error|error|warning|note):\s+(.*)$)");
// src/main.ts(5,7): error TS2345: Argument of type 'number' is not assignable ...
const std::regex kTsc(R"(^(.+?)\((\d+),(\d+)\):\s+(error|warning)\s+(TS\d+):\s+(.*)$)");
// main.cpp:(.text+0x9): undefined reference to `foo()'          (no debug info)
// /proj/src/main.c:3:(.text+0x9): undefined reference to `foo'   (with -g)
const std::regex kLdSymbol(R"(^(.+?)(?::(\d+))?:\(\.[^)]*\):\s+(.*)$)");
// /usr/bin/ld: cannot find -lfoo: No such file or directory
const std::regex kLd(R"(^(?:\S*/)?ld(?:\.\w+)?:\s+(.*)$)");
// CMake Error at CMakeLists.txt:8 (add_executable):
const std::regex kCMakeAt(R"(^CMake (Error|Warning)(?: \(dev\))? at (.+?):(\d+)(?: \((.*)\))?:\s*$)");
// CMake Error: The source directory "/x" does not exist.
const std::regex kCMake(R"(^CMake (Error|Warning)(?: \(dev\))?:\s+(.*)$)");
// make[1]: Entering directory '/home/me/proj'
const std::regex kMakeDir(R"(^g?make(?:\[\d+\])?: (Entering|Leaving) directory ['`](.*)'$)");

std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r");
    return s.substr(first, last - first + 1);
}

} // namespace

std::string stripAnsi(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\x1b' && i + 1 < text.size() && text[i + 1] == '[') {
            i += 2;
            while (i < text.size() && !(text[i] >= '@' && text[i] <= '~')) ++i;
            continue;
        }
        if (text[i] != '\r') out += text[i];
    }
    return out;
}

DiagnosticParser::DiagnosticParser(std::filesystem::path baseDir) : dirs_{std::move(baseDir)} {}

std::string DiagnosticParser::resolve(const std::string& file) const {
    std::filesystem::path p(file);
    if (p.is_relative()) p = dirs_.back() / p;
    return p.lexically_normal().string();
}

Diagnostic DiagnosticParser::count(Diagnostic d) {
    if (d.severity == Diagnostic::Severity::Error) ++errors_;
    else if (d.severity == Diagnostic::Severity::Warning) ++warnings_;
    return d;
}

std::vector<Diagnostic> DiagnosticParser::feed(std::string_view raw) {
    const std::string line = stripAnsi(raw);
    std::vector<Diagnostic> out;
    if (pendingCMake_) {
        if (trim(line).empty()) return out;
        Diagnostic d = std::move(*pendingCMake_);
        pendingCMake_.reset();
        if (line.starts_with(" ")) {
            d.message = trim(line);
            out.push_back(count(std::move(d)));
            return out;
        }
        out.push_back(count(std::move(d))); // header without a message; report it and parse this line too
    }
    if (auto d = parse(line)) out.push_back(std::move(*d));
    return out;
}

std::optional<Diagnostic> DiagnosticParser::parse(const std::string& line) {
    std::smatch m;
    // svelte-check --output machine-verbose:
    // 1793440778357 {"type":"ERROR","filename":"src/routes/+page.svelte","start":{"line":1,"character":5},...}
    if (const auto brace = line.find(" {"); brace != std::string::npos && brace > 0 &&
        line.find_first_not_of("0123456789") == brace) {
        try {
            const auto j = nlohmann::json::parse(line.substr(brace + 1));
            const std::string type = j.value("type", "");
            Diagnostic d;
            d.severity = type == "WARNING" ? Diagnostic::Severity::Warning
                       : type == "ERROR"   ? Diagnostic::Severity::Error
                                           : Diagnostic::Severity::Note;
            d.file = resolve(j.value("filename", ""));
            // Its positions are 0-based.
            const auto start = j.value("start", nlohmann::json::object());
            d.line = start.value("line", -1) + 1;
            d.column = start.value("character", -1) + 1;
            d.message = j.value("message", "");
            if (j.contains("code") && j["code"].is_number()) d.message += " (" + j.value("source", "") + std::to_string(j["code"].get<int>()) + ")";
            return count(std::move(d));
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    if (std::regex_match(line, m, kMakeDir)) {
        if (m[1] == "Entering") dirs_.push_back(m[2].str());
        else if (dirs_.size() > 1) dirs_.pop_back();
        return std::nullopt;
    }
    if (line.starts_with("collect2:") || line.starts_with("make: ***") || line.starts_with("make[")) return std::nullopt;
    if (line.starts_with("In file included from") || line.starts_with("                 from ")) return std::nullopt;

    if (std::regex_match(line, m, kGcc)) {
        Diagnostic d;
        const std::string sev = m[4];
        d.severity = sev == "warning" ? Diagnostic::Severity::Warning
                   : sev == "note"    ? Diagnostic::Severity::Note
                                      : Diagnostic::Severity::Error;
        d.file = resolve(m[1]);
        d.line = std::stoi(m[2]);
        d.column = m[3].matched ? std::stoi(m[3]) : 0;
        d.message = m[5];
        return count(std::move(d));
    }
    if (std::regex_match(line, m, kTsc)) {
        Diagnostic d;
        d.severity = m[4] == "warning" ? Diagnostic::Severity::Warning : Diagnostic::Severity::Error;
        d.file = resolve(m[1]);
        d.line = std::stoi(m[2]);
        d.column = std::stoi(m[3]);
        d.message = m[6].str() + " (" + m[5].str() + ")";
        return count(std::move(d));
    }
    if (std::regex_match(line, m, kCMakeAt)) {
        Diagnostic d;
        d.severity = m[1] == "Error" ? Diagnostic::Severity::Error : Diagnostic::Severity::Warning;
        d.file = resolve(m[2]);
        d.line = std::stoi(m[3]);
        d.message = m[4].matched ? "in " + m[4].str() + "()" : "CMake " + m[1].str();
        pendingCMake_ = d;
        return std::nullopt;
    }
    if (std::regex_match(line, m, kCMake)) {
        Diagnostic d;
        d.severity = m[1] == "Error" ? Diagnostic::Severity::Error : Diagnostic::Severity::Warning;
        d.message = m[2];
        return count(std::move(d));
    }
    if (std::regex_match(line, m, kLd)) {
        const std::string msg = m[1];
        if (msg.find(": in function") != std::string::npos || msg.ends_with("in function `main':")) return std::nullopt;
        if (msg.starts_with("warning:")) return count({Diagnostic::Severity::Warning, {}, 0, 0, trim(msg.substr(8))});
        return count({Diagnostic::Severity::Error, {}, 0, 0, msg});
    }
    if (std::regex_match(line, m, kLdSymbol)) {
        const std::string msg = m[3];
        if (!msg.starts_with("undefined reference") && !msg.starts_with("multiple definition")) return std::nullopt;
        if (m[2].matched) return count({Diagnostic::Severity::Error, resolve(m[1]), std::stoi(m[2]), 0, msg});
        return count({Diagnostic::Severity::Error, {}, 0, 0, std::filesystem::path(m[1].str()).filename().string() + ": " + msg});
    }
    return std::nullopt;
}

} // namespace kevc
