#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kevc {

// Results from a JavaScript test runner's JSON reporter (Vitest's or Playwright's), one per test.
struct TestCase {
    enum class Status { Passed, Failed, Skipped };
    std::filesystem::path file;
    std::string name;    // with its describe() blocks: "math › adds"
    std::string title;   // the test's own title
    std::string fullName; // the runner's name for it (Vitest: "math adds"), for running just this one
    Status status = Status::Passed;
    int line = 0;        // where it failed, or where it's declared; 0 if unknown
    std::string message; // the failure, first lines
    double ms = 0;
};

struct TestRun {
    std::vector<TestCase> cases;
    int passed = 0, failed = 0, skipped = 0;
    std::string error; // the report couldn't be read
};

enum class TestRunner { Vitest, Playwright };

// The runners a project has installed (node_modules/.bin), Vitest first.
std::vector<TestRunner> testRunners(const std::filesystem::path& root);
const char* runnerName(TestRunner runner);

// The command that writes `report` (JSON). `file` narrows it to one file; `fullName` (Vitest) or
// `line` (Playwright) to one test.
std::vector<std::string> testCommand(TestRunner runner, const std::filesystem::path& root, const std::string& node,
                                     const std::filesystem::path& report, const std::filesystem::path& file = {},
                                     const std::string& fullName = {}, int line = 0);

TestRun parseVitestJson(std::string_view json);
TestRun parsePlaywrightJson(std::string_view json, const std::filesystem::path& root);

// The first "file:line" in a stack trace that is `file` itself, or 0.
int stackLine(std::string_view message, const std::filesystem::path& file);
// The line where a test with this title is declared (it('title', …)), or 0.
int declarationLine(std::string_view source, std::string_view title);

} // namespace kevc
