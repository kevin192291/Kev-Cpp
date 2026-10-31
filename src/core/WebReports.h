#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Two web-project reports Kev-C++ shows in panels: OpenTelemetry traces that a SvelteKit app sends to
// the IDE's local collector, and npm's view of the project's dependencies (outdated, vulnerable).
namespace kevc {

// ---- traces

struct Span {
    std::string traceId;
    std::string spanId;
    std::string parentId; // empty for a trace's root span
    std::string name;     // "sveltekit.handle.root", "sveltekit.load", …
    uint64_t startNs = 0;
    uint64_t endNs = 0;
    bool error = false;
    std::vector<std::pair<std::string, std::string>> attributes;

    double ms() const { return endNs > startNs ? static_cast<double>(endNs - startNs) / 1e6 : 0.0; }
    std::string attribute(std::string_view key) const;
};

// The spans in an OTLP/HTTP JSON export (resourceSpans → scopeSpans → spans).
std::vector<Span> parseOtlpJson(std::string_view body);

struct Trace {
    std::string id;
    std::vector<Span> spans; // in start order
    uint64_t startNs = 0;
    uint64_t endNs = 0;
    std::string title() const; // the root span's name, with the request's method and route if it has them
    double ms() const { return endNs > startNs ? static_cast<double>(endNs - startNs) / 1e6 : 0.0; }
};

// One bar of a trace's waterfall: how deep the span is, and where it sits as fractions of the trace.
struct WaterfallRow {
    const Span* span;
    int depth = 0;
    double offset = 0; // 0..1
    double width = 0;  // 0..1
};
std::vector<WaterfallRow> waterfall(const Trace& trace);

// Recent traces, newest first, assembled as their spans arrive (spans of one trace may come in
// several exports).
class TraceStore {
public:
    explicit TraceStore(size_t capacity = 100) : capacity_(capacity) {}
    void add(const std::vector<Span>& spans);
    const std::deque<Trace>& traces() const { return traces_; }
    void clear() { traces_.clear(); }

private:
    size_t capacity_;
    std::deque<Trace> traces_;
};

// ---- npm

struct OutdatedPackage {
    std::string name, current, wanted, latest;
};
// `npm outdated --json`
std::vector<OutdatedPackage> parseNpmOutdated(std::string_view json);

struct Vulnerability {
    std::string name, severity, range;
    bool direct = false;
    std::vector<std::string> via; // packages or advisories it comes through
    std::string fix;              // "npm audit fix", "breaking change: name@version", or "" if none
};
struct AuditReport {
    std::vector<Vulnerability> vulnerabilities; // most severe first
    std::map<std::string, int> counts;          // severity → number
    std::string error;
};
// `npm audit --json` (npm 7 and later)
AuditReport parseNpmAudit(std::string_view json);
int severityRank(std::string_view severity); // critical 4 … info 0

} // namespace kevc
