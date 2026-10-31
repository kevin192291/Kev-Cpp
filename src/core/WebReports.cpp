#include "core/WebReports.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <functional>
#include <set>

using json = nlohmann::json;

namespace kevc {

namespace {

// OTLP JSON writes 64-bit numbers as strings ("1700000000000000000"), some exporters as numbers.
uint64_t nanos(const json& v) {
    if (v.is_number_unsigned()) return v.get<uint64_t>();
    if (v.is_number_integer()) return static_cast<uint64_t>(v.get<int64_t>());
    if (v.is_string()) {
        try {
            return std::stoull(v.get<std::string>());
        } catch (const std::exception&) {
        }
    }
    return 0;
}

std::string anyValue(const json& v) {
    if (!v.is_object()) return {};
    if (v.contains("stringValue")) return v["stringValue"].get<std::string>();
    if (v.contains("intValue")) return v["intValue"].is_string() ? v["intValue"].get<std::string>() : v["intValue"].dump();
    if (v.contains("doubleValue")) return v["doubleValue"].dump();
    if (v.contains("boolValue")) return v["boolValue"].get<bool>() ? "true" : "false";
    return v.dump();
}

} // namespace

std::string Span::attribute(std::string_view key) const {
    for (const auto& [k, v] : attributes)
        if (k == key) return v;
    return {};
}

std::vector<Span> parseOtlpJson(std::string_view body) {
    std::vector<Span> out;
    json export_;
    try {
        export_ = json::parse(body);
    } catch (const std::exception&) {
        return out;
    }
    for (const auto& resource : export_.value("resourceSpans", json::array()))
        for (const auto& scope : resource.value("scopeSpans", json::array()))
            for (const auto& s : scope.value("spans", json::array())) {
                Span span;
                span.traceId = s.value("traceId", "");
                span.spanId = s.value("spanId", "");
                span.parentId = s.value("parentSpanId", "");
                span.name = s.value("name", "");
                span.startNs = nanos(s.value("startTimeUnixNano", json()));
                span.endNs = nanos(s.value("endTimeUnixNano", json()));
                // STATUS_CODE_ERROR is 2.
                if (auto status = s.find("status"); status != s.end() && status->is_object())
                    span.error = status->value("code", 0) == 2;
                for (const auto& a : s.value("attributes", json::array()))
                    span.attributes.emplace_back(a.value("key", ""), anyValue(a.value("value", json::object())));
                if (!span.traceId.empty() && !span.spanId.empty()) out.push_back(std::move(span));
            }
    return out;
}

std::string Trace::title() const {
    for (const auto& s : spans) {
        if (!s.parentId.empty()) continue;
        std::string title = s.name;
        const std::string method = s.attribute("http.method").empty() ? s.attribute("http.request.method") : s.attribute("http.method");
        const std::string route = !s.attribute("http.route").empty() ? s.attribute("http.route") : s.attribute("sveltekit.route.id");
        const std::string url = s.attribute("http.url").empty() ? s.attribute("url.path") : s.attribute("http.url");
        if (!method.empty() || !route.empty() || !url.empty())
            title = (method.empty() ? "" : method + " ") + (!route.empty() ? route : url) + "  (" + s.name + ")";
        return title;
    }
    return spans.empty() ? id : spans.front().name;
}

std::vector<WaterfallRow> waterfall(const Trace& trace) {
    std::vector<WaterfallRow> rows;
    const double total = trace.endNs > trace.startNs ? static_cast<double>(trace.endNs - trace.startNs) : 1.0;
    std::set<std::string> ids;
    for (const auto& s : trace.spans) ids.insert(s.spanId);
    // Depth-first from the roots (and from spans whose parent hasn't arrived), children in start order.
    std::function<void(const Span&, int)> visit = [&](const Span& span, int depth) {
        rows.push_back({&span, depth, static_cast<double>(span.startNs - trace.startNs) / total,
                        std::max(static_cast<double>(span.endNs - span.startNs) / total, 0.0)});
        for (const auto& child : trace.spans)
            if (child.parentId == span.spanId) visit(child, depth + 1);
    };
    for (const auto& s : trace.spans)
        if (s.parentId.empty() || !ids.count(s.parentId)) visit(s, 0);
    return rows;
}

void TraceStore::add(const std::vector<Span>& spans) {
    for (const auto& span : spans) {
        auto it = std::find_if(traces_.begin(), traces_.end(), [&](const Trace& t) { return t.id == span.traceId; });
        if (it == traces_.end()) {
            traces_.push_front(Trace{span.traceId, {}, span.startNs, span.endNs});
            it = traces_.begin();
        }
        Trace& t = *it;
        t.spans.insert(std::upper_bound(t.spans.begin(), t.spans.end(), span,
                                        [](const Span& a, const Span& b) { return a.startNs < b.startNs; }),
                       span);
        t.startNs = std::min(t.startNs, span.startNs);
        t.endNs = std::max(t.endNs, span.endNs);
    }
    while (traces_.size() > capacity_) traces_.pop_back();
}

std::vector<OutdatedPackage> parseNpmOutdated(std::string_view text) {
    std::vector<OutdatedPackage> out;
    json report;
    try {
        report = json::parse(text);
    } catch (const std::exception&) {
        return out;
    }
    if (!report.is_object()) return out;
    for (const auto& [name, info] : report.items()) {
        // With several copies of one package, npm gives an array.
        const json& i = info.is_array() && !info.empty() ? info.front() : info;
        if (!i.is_object()) continue;
        out.push_back({name, i.value("current", ""), i.value("wanted", ""), i.value("latest", "")});
    }
    return out;
}

int severityRank(std::string_view s) {
    if (s == "critical") return 4;
    if (s == "high") return 3;
    if (s == "moderate") return 2;
    if (s == "low") return 1;
    return 0;
}

AuditReport parseNpmAudit(std::string_view text) {
    AuditReport report;
    json audit;
    try {
        audit = json::parse(text);
    } catch (const std::exception& e) {
        report.error = std::string("unreadable npm audit output: ") + e.what();
        return report;
    }
    if (audit.contains("error")) report.error = audit["error"].value("summary", "npm audit failed");
    // Named, not iterated straight off value(): items() of a temporary would dangle.
    const json vulnerabilities = audit.value("vulnerabilities", json::object());
    for (const auto& [name, v] : vulnerabilities.items()) {
        Vulnerability vuln;
        vuln.name = name;
        vuln.severity = v.value("severity", "");
        vuln.range = v.value("range", "");
        vuln.direct = v.value("isDirect", false);
        for (const auto& via : v.value("via", json::array())) {
            if (via.is_string()) vuln.via.push_back(via.get<std::string>());
            else if (via.is_object()) vuln.via.push_back(via.value("title", via.value("name", "")));
        }
        const json fix = v.value("fixAvailable", json(false));
        if (fix.is_boolean() && fix.get<bool>()) vuln.fix = "npm audit fix";
        else if (fix.is_object())
            vuln.fix = std::string(fix.value("isSemVerMajor", false) ? "breaking change: " : "") + fix.value("name", "") +
                       "@" + fix.value("version", "");
        report.vulnerabilities.push_back(std::move(vuln));
    }
    std::stable_sort(report.vulnerabilities.begin(), report.vulnerabilities.end(),
                     [](const Vulnerability& a, const Vulnerability& b) { return severityRank(a.severity) > severityRank(b.severity); });
    if (auto meta = audit.find("metadata"); meta != audit.end() && meta->is_object()) {
        const json counts = meta->value("vulnerabilities", json::object());
        for (const auto& [severity, n] : counts.items())
            if (n.is_number_integer() && severity != "total") report.counts[severity] = n.get<int>();
    }
    return report;
}

} // namespace kevc
