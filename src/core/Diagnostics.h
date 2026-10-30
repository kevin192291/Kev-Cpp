#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kevc {

struct Diagnostic {
    enum class Severity { Error, Warning, Note };

    Severity severity = Severity::Error;
    std::string file; // absolute when the tool reported a path; empty for linker-level errors
    int line = 0;     // 1-based; 0 if unknown
    int column = 0;   // 1-based; 0 if unknown
    std::string message;

    bool hasLocation() const { return !file.empty() && line > 0; }
};

// Turns build output (gcc, g++, ld, CMake, make) into diagnostics, one line at a time.
class DiagnosticParser {
public:
    // Relative paths in the output are resolved against `baseDir` (and make's "Entering directory").
    explicit DiagnosticParser(std::filesystem::path baseDir);

    // Usually returns zero or one diagnostic; two when a CMake header had no message line.
    std::vector<Diagnostic> feed(std::string_view line);

    int errors() const { return errors_; }
    int warnings() const { return warnings_; }

private:
    std::optional<Diagnostic> parse(const std::string& line);
    Diagnostic count(Diagnostic d);
    std::string resolve(const std::string& file) const;

    std::vector<std::filesystem::path> dirs_;
    std::optional<Diagnostic> pendingCMake_; // header seen, waiting for the indented message line
    int errors_ = 0;
    int warnings_ = 0;
};

// Removes ANSI colour/escape sequences.
std::string stripAnsi(std::string_view text);

} // namespace kevc
