#include "core/TestReport.h"
#include "core/WebReports.h"

#include <gtest/gtest.h>

#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace kevc;

TEST(TestReports, Vitest) {
    // Recorded from Vitest 4 (one file, abridged), with a test file to find declaration lines in.
    const fs::path dir = fs::temp_directory_path() / ("kevc-vitest-" + std::to_string(::getpid()));
    fs::create_directories(dir);
    const fs::path test = dir / "math.test.ts";
    std::ofstream(test) << "import { it } from 'vitest';\n\ndescribe('math', () => {\n\tit('adds', () => {});\n"
                           "\tit('adds wrong', () => {});\n\tit.skip('later', () => {});\n});\n";
    const std::string report = R"({"numTotalTests":3,"testResults":[{"name":")" + test.string() + R"(","status":"failed","message":"",
      "assertionResults":[
        {"ancestorTitles":["math"],"fullName":"math adds","status":"passed","title":"adds","duration":1.1,"failureMessages":[]},
        {"ancestorTitles":["math"],"fullName":"math adds wrong","status":"failed","title":"adds wrong","duration":4.0,
         "failureMessages":["AssertionError: expected 3 to be 4 // Object.is equality\n    at )" + test.string() + R"(:9:21\n    at file:///x/node_modules/@vitest/runner/dist/chunk.js:302:11"]},
        {"ancestorTitles":["math"],"fullName":"math later","status":"skipped","title":"later","failureMessages":[]}]}]})";
    const TestRun run = parseVitestJson(report);
    EXPECT_TRUE(run.error.empty());
    ASSERT_EQ(run.cases.size(), 3u);
    EXPECT_EQ(run.passed, 1);
    EXPECT_EQ(run.failed, 1);
    EXPECT_EQ(run.skipped, 1);
    EXPECT_EQ(run.cases[0].name, "math › adds");
    EXPECT_EQ(run.cases[0].line, 4); // found by its title
    EXPECT_EQ(run.cases[1].status, TestCase::Status::Failed);
    EXPECT_EQ(run.cases[1].line, 9); // from the stack
    EXPECT_EQ(run.cases[1].message.substr(0, 31), "AssertionError: expected 3 to b");
    EXPECT_EQ(run.cases[2].status, TestCase::Status::Skipped);
    EXPECT_FALSE(parseVitestJson("not json").error.empty());
    fs::remove_all(dir);
}

TEST(TestReports, Playwright) {
    const std::string report = R"({"config":{"rootDir":"/app/tests"},"suites":[{"title":"home.spec.ts","file":"home.spec.ts","line":0,"column":0,"specs":[],
      "suites":[{"title":"home","file":"home.spec.ts","line":3,"column":6,"specs":[
        {"title":"has a heading","ok":true,"file":"home.spec.ts","line":4,"column":2,"tests":[{"projectName":"chromium","results":[{"status":"passed","duration":812}]}]},
        {"title":"logs in","ok":false,"file":"home.spec.ts","line":8,"column":2,"tests":[{"projectName":"chromium","results":[
          {"status":"failed","duration":5000,"error":{"message":"Error: expect(locator).toBeVisible() failed","location":{"file":"/app/tests/home.spec.ts","line":11,"column":3}}}]}]}]}]}],
      "stats":{"expected":1,"unexpected":1}})";
    const TestRun run = parsePlaywrightJson(report, "/app");
    ASSERT_EQ(run.cases.size(), 2u);
    EXPECT_EQ(run.cases[0].file, "/app/tests/home.spec.ts");
    EXPECT_EQ(run.cases[0].name, "home › has a heading  [chromium]");
    EXPECT_EQ(run.cases[0].line, 4);
    EXPECT_EQ(run.cases[1].status, TestCase::Status::Failed);
    EXPECT_EQ(run.cases[1].line, 11);
    EXPECT_EQ(run.failed, 1);
}

TEST(TestReports, Helpers) {
    EXPECT_EQ(stackLine("x\n    at /p/a.test.ts:12:5\n    at /p/a.test.ts:3:1", "/p/a.test.ts"), 12);
    EXPECT_EQ(stackLine("at /p/other.ts:3:1", "/p/a.test.ts"), 0);
    EXPECT_EQ(declarationLine("a\nit(\"two words\", () => {})\n", "two words"), 2);
    const auto cmd = testCommand(TestRunner::Playwright, "/app", "node", "/tmp/r.json", "/app/tests/a.spec.ts", "", 7);
    EXPECT_EQ(cmd[0], "env");
    EXPECT_EQ(cmd[1], "PLAYWRIGHT_JSON_OUTPUT_NAME=/tmp/r.json");
    EXPECT_EQ(cmd.back(), "tests/a.spec.ts:7");
    const auto one = testCommand(TestRunner::Vitest, "/app", "node", "/tmp/r.json", "/app/src/a.test.ts", "math adds (x)");
    EXPECT_EQ(one[one.size() - 2], "-t");
    EXPECT_EQ(one.back(), "^math adds \\(x\\)$"); // exactly this test, not "math adds (x) again"
}

TEST(Traces, OtlpJsonToWaterfall) {
    // A SvelteKit request: handle → load (page) → fetch, sent as two exports.
    const std::string first = R"({"resourceSpans":[{"resource":{},"scopeSpans":[{"scope":{"name":"@sveltejs/kit"},"spans":[
        {"traceId":"t1","spanId":"a","name":"sveltekit.handle.root","startTimeUnixNano":"1000000000","endTimeUnixNano":"1100000000",
         "attributes":[{"key":"http.method","value":{"stringValue":"GET"}},{"key":"http.route","value":{"stringValue":"/blog/[slug]"}}]},
        {"traceId":"t1","spanId":"b","parentSpanId":"a","name":"sveltekit.load","startTimeUnixNano":"1010000000","endTimeUnixNano":"1090000000",
         "status":{"code":2}}]}]}]})";
    const std::string second = R"({"resourceSpans":[{"scopeSpans":[{"spans":[
        {"traceId":"t1","spanId":"c","parentSpanId":"b","name":"fetch","startTimeUnixNano":1020000000,"endTimeUnixNano":1050000000},
        {"traceId":"t2","spanId":"z","name":"sveltekit.handle.root","startTimeUnixNano":"2000000000","endTimeUnixNano":"2001000000"}]}]}]})";
    const auto spans = parseOtlpJson(first);
    ASSERT_EQ(spans.size(), 2u);
    EXPECT_EQ(spans[0].attribute("http.route"), "/blog/[slug]");
    EXPECT_DOUBLE_EQ(spans[0].ms(), 100.0);
    EXPECT_TRUE(spans[1].error);

    TraceStore store;
    store.add(spans);
    store.add(parseOtlpJson(second));
    ASSERT_EQ(store.traces().size(), 2u);
    EXPECT_EQ(store.traces()[0].id, "t2"); // newest first
    const Trace& t = store.traces()[1];
    EXPECT_EQ(t.title(), "GET /blog/[slug]  (sveltekit.handle.root)");
    const auto rows = waterfall(t);
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0].span->name, "sveltekit.handle.root");
    EXPECT_EQ(rows[1].depth, 1);
    EXPECT_EQ(rows[2].depth, 2);
    EXPECT_NEAR(rows[1].offset, 0.1, 1e-9);
    EXPECT_NEAR(rows[2].width, 0.3, 1e-9);
    EXPECT_TRUE(parseOtlpJson("{").empty());
}

TEST(NpmReports, OutdatedAndAudit) {
    const auto outdated = parseNpmOutdated(R"({"typescript":{"current":"6.0.3","wanted":"6.0.3","latest":"7.0.2","dependent":"kitapp"},
        "vitest":{"current":"4.1.12","wanted":"4.1.12","latest":"5.0.5"}})");
    ASSERT_EQ(outdated.size(), 2u);
    EXPECT_EQ(outdated[0].name, "typescript");
    EXPECT_EQ(outdated[0].latest, "7.0.2");
    EXPECT_TRUE(parseNpmOutdated("").empty()); // npm prints nothing when all is current

    const auto audit = parseNpmAudit(R"({"auditReportVersion":2,"vulnerabilities":{
        "@sveltejs/kit":{"name":"@sveltejs/kit","severity":"low","isDirect":true,"via":["@sveltejs/vite-plugin-svelte"],"range":">=1.0.0",
                         "fixAvailable":{"name":"@sveltejs/kit","version":"0.0.30","isSemVerMajor":true}},
        "cookie":{"name":"cookie","severity":"high","isDirect":false,"via":[{"source":1,"title":"cookie accepts out of bounds characters","severity":"high"}],
                  "range":"<0.7.0","fixAvailable":true}},
        "metadata":{"vulnerabilities":{"info":0,"low":1,"moderate":0,"high":1,"critical":0,"total":2}}})");
    ASSERT_EQ(audit.vulnerabilities.size(), 2u);
    EXPECT_EQ(audit.vulnerabilities[0].name, "cookie"); // most severe first
    EXPECT_EQ(audit.vulnerabilities[0].via[0], "cookie accepts out of bounds characters");
    EXPECT_EQ(audit.vulnerabilities[0].fix, "npm audit fix");
    EXPECT_EQ(audit.vulnerabilities[1].fix, "breaking change: @sveltejs/kit@0.0.30");
    EXPECT_EQ(audit.counts.at("high"), 1);
    EXPECT_EQ(audit.counts.count("total"), 0u);
}
