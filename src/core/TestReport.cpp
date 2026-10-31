#include "core/TestReport.h"

#include "core/Diagnostics.h"
#include "core/WebProject.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <functional>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace kevc {

namespace {

std::string firstLines(const std::string& text, int count) {
    std::istringstream in(stripAnsi(text));
    std::string line, out;
    for (int i = 0; i < count && std::getline(in, line); ++i) out += (out.empty() ? "" : "\n") + line;
    return out;
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void count(TestRun& run, const TestCase& c) {
    if (c.status == TestCase::Status::Passed) ++run.passed;
    else if (c.status == TestCase::Status::Failed) ++run.failed;
    else ++run.skipped;
}

} // namespace

std::vector<TestRunner> testRunners(const fs::path& root) {
    std::vector<TestRunner> out;
    if (!nodeBin(root, "vitest").empty()) out.push_back(TestRunner::Vitest);
    if (!nodeBin(root, "playwright").empty()) out.push_back(TestRunner::Playwright);
    return out;
}

const char* runnerName(TestRunner runner) {
    return runner == TestRunner::Vitest ? "Vitest" : "Playwright";
}

std::vector<std::string> testCommand(TestRunner runner, const fs::path& root, const std::string& node,
                                     const fs::path& report, const fs::path& file, const std::string& fullName, int line) {
    if (runner == TestRunner::Vitest) {
        std::vector<std::string> argv{node, nodeBin(root, "vitest").string(), "run", "--reporter=json",
                                      "--outputFile=" + report.string()};
        if (!file.empty()) argv.push_back(file.lexically_relative(root).generic_string());
        // -t is a regular expression over the full name: "adds" would also run "adds wrong".
        if (!fullName.empty()) {
            std::string pattern = "^";
            for (char ch : fullName) {
                if (std::string_view("\\^$.|?*+()[]{}").find(ch) != std::string_view::npos) pattern += '\\';
                pattern += ch;
            }
            argv.insert(argv.end(), {"-t", pattern + "$"});
        }
        return argv;
    }
    // Playwright writes its JSON report to the file named in this variable.
    std::vector<std::string> argv{"env", "PLAYWRIGHT_JSON_OUTPUT_NAME=" + report.string(), node,
                                  nodeBin(root, "playwright").string(), "test", "--reporter=json"};
    if (!file.empty()) argv.push_back(file.lexically_relative(root).generic_string() + (line ? ":" + std::to_string(line) : ""));
    return argv;
}

int stackLine(std::string_view message, const fs::path& file) {
    const std::string needle = file.string() + ":";
    for (size_t at = message.find(needle); at != std::string_view::npos; at = message.find(needle, at + 1)) {
        int line = 0;
        for (size_t i = at + needle.size(); i < message.size() && std::isdigit(static_cast<unsigned char>(message[i])); ++i)
            line = line * 10 + (message[i] - '0');
        if (line > 0) return line;
    }
    return 0;
}

int declarationLine(std::string_view source, std::string_view title) {
    for (const char quote : {'\'', '"', '`'}) {
        const std::string needle = std::string(1, quote) + std::string(title) + quote;
        if (const auto at = source.find(needle); at != std::string_view::npos)
            return 1 + static_cast<int>(std::count(source.begin(), source.begin() + static_cast<long>(at), '\n'));
    }
    return 0;
}

TestRun parseVitestJson(std::string_view text) {
    TestRun run;
    json report;
    try {
        report = json::parse(text);
    } catch (const std::exception& e) {
        run.error = std::string("unreadable Vitest report: ") + e.what();
        return run;
    }
    std::map<fs::path, std::string> sources;
    for (const auto& file : report.value("testResults", json::array())) {
        const fs::path path = file.value("name", "");
        for (const auto& a : file.value("assertionResults", json::array())) {
            TestCase c;
            c.file = path;
            c.title = a.value("title", "");
            std::string name;
            for (const auto& t : a.value("ancestorTitles", json::array()))
                if (t.is_string()) name += t.get<std::string>() + " › ";
            c.name = name + c.title;
            c.fullName = a.value("fullName", c.title);
            const std::string status = a.value("status", "");
            c.status = status == "passed" ? TestCase::Status::Passed
                       : status == "failed" ? TestCase::Status::Failed
                                            : TestCase::Status::Skipped;
            c.ms = a.value("duration", 0.0);
            std::string messages;
            for (const auto& m : a.value("failureMessages", json::array()))
                if (m.is_string()) messages += m.get<std::string>() + "\n";
            c.message = firstLines(messages, 3);
            if (auto loc = a.find("location"); loc != a.end() && loc->is_object()) c.line = loc->value("line", 0);
            if (c.status == TestCase::Status::Failed) c.line = stackLine(messages, path) ? stackLine(messages, path) : c.line;
            if (c.line == 0) {
                if (!sources.count(path)) sources[path] = readFile(path);
                c.line = declarationLine(sources[path], c.title);
            }
            count(run, c);
            run.cases.push_back(std::move(c));
        }
        // A file that failed to load has no tests, only a message.
        if (file.value("assertionResults", json::array()).empty() && file.value("status", "") == "failed") {
            TestCase c;
            c.file = path;
            c.name = path.filename().string();
            c.status = TestCase::Status::Failed;
            c.line = 1;
            c.message = firstLines(file.value("message", ""), 3);
            count(run, c);
            run.cases.push_back(std::move(c));
        }
    }
    return run;
}

TestRun parsePlaywrightJson(std::string_view text, const fs::path& root) {
    TestRun run;
    json report;
    try {
        report = json::parse(text);
    } catch (const std::exception& e) {
        run.error = std::string("unreadable Playwright report: ") + e.what();
        return run;
    }
    fs::path base = root;
    if (auto config = report.find("config"); config != report.end() && config->is_object())
        if (const std::string dir = config->value("rootDir", ""); !dir.empty()) base = dir;

    // Suites nest (file → describe → …); specs hold the tests, one per browser project.
    std::function<void(const json&, const std::string&)> walk = [&](const json& suite, const std::string& prefix) {
        const std::string title = suite.value("title", "");
        const bool fileSuite = suite.contains("file") && suite.value("line", 0) == 0;
        const std::string here = fileSuite || title.empty() ? prefix : prefix + title + " › ";
        for (const auto& spec : suite.value("specs", json::array())) {
            for (const auto& test : spec.value("tests", json::array())) {
                TestCase c;
                c.file = base / spec.value("file", suite.value("file", ""));
                c.title = spec.value("title", "");
                const std::string project = test.value("projectName", "");
                c.name = here + c.title + (project.empty() ? "" : "  [" + project + "]");
                c.line = spec.value("line", 0);
                const auto results = test.value("results", json::array());
                const json last = results.empty() ? json::object() : results.back();
                const std::string status = last.value("status", test.value("status", "skipped"));
                c.status = status == "passed" ? TestCase::Status::Passed
                           : status == "skipped" ? TestCase::Status::Skipped
                                                 : TestCase::Status::Failed;
                c.ms = last.value("duration", 0.0);
                if (auto error = last.find("error"); error != last.end() && error->is_object()) {
                    c.message = firstLines(error->value("message", ""), 3);
                    if (auto loc = error->find("location"); loc != error->end() && loc->is_object()) c.line = loc->value("line", c.line);
                }
                count(run, c);
                run.cases.push_back(std::move(c));
            }
        }
        for (const auto& child : suite.value("suites", json::array())) walk(child, here);
    };
    for (const auto& suite : report.value("suites", json::array())) walk(suite, "");
    return run;
}

} // namespace kevc
