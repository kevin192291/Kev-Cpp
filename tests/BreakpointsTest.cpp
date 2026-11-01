#include "core/Breakpoints.h"
#include "core/Mi.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <unistd.h>

using namespace kevc;

TEST(Breakpoints, SplitExpressions) {
    EXPECT_EQ(splitExpressions("i, total"), (std::vector<std::string>{"i", "total"}));
    EXPECT_EQ(splitExpressions(" f(a, b) , items[1,2], \"x, y\" ,"), (std::vector<std::string>{"f(a, b)", "items[1,2]", "\"x, y\""}));
    EXPECT_EQ(splitExpressions(""), std::vector<std::string>{});
    EXPECT_EQ(splitExpressions("broken(a, b"), (std::vector<std::string>{"broken(a, b"}));
    EXPECT_EQ(joinExpressions({"i", "total"}), "i, total");
    EXPECT_EQ(liveSummary({"i", "total"}, {"41", "1523.5"}), "i = 41, total = 1523.5");
    EXPECT_EQ(liveSummary({"i", "j"}, {"1"}), "i = 1, j = ?");
}

TEST(Breakpoints, LiveLogpointRoundTrip) {
    EXPECT_EQ(liveLogMessage(7, {"i", "player.name"}), "<<kevc-live:7>>{i}<<kevc>>{player.name}");
    // What js-debug prints for it: the values in place of the {…}.
    auto log = parseLiveLog("<<kevc-live:7>>41<<kevc>>'Ann'\n");
    ASSERT_TRUE(log);
    EXPECT_EQ(log->id, 7);
    EXPECT_EQ(log->values, (std::vector<std::string>{"41", "'Ann'"}));
    EXPECT_FALSE(parseLiveLog("just program output\n"));
    EXPECT_FALSE(parseLiveLog("<<kevc-live:>>x"));
    EXPECT_TRUE(parseLiveLog("<<kevc-live:3>>")->values.empty()); // nothing to show: a hit counter
}

TEST(Breakpoints, LiveLogMustStartWithItsMarker) {
    EXPECT_FALSE(parseLiveLog(R"(=breakpoint-modified,bkpt={script={"printf \"<<kevc-live:1>>%s\""}})"));
    EXPECT_FALSE(parseLiveLog("text <<kevc-live:1>>4\n"));
    ASSERT_TRUE(parseLiveLog("  <<kevc-live:1>>4\n"));
}

TEST(Breakpoints, PropertyPathsForJavaScriptWatches) {
    using P = std::pair<std::string, std::string>;
    EXPECT_EQ(splitPropertyPath("player.score"), (P{"player", "score"}));
    EXPECT_EQ(splitPropertyPath("game.players[0].score"), (P{"game.players[0]", "score"}));
    EXPECT_EQ(splitPropertyPath("items[3]"), (P{"items", "3"}));
    EXPECT_EQ(splitPropertyPath("cfg['max-size']"), (P{"cfg", "max-size"}));
    EXPECT_EQ(splitPropertyPath("a?.b"), (P{"a", "b"}));
    EXPECT_EQ(splitPropertyPath("getState().count"), (P{"getState()", "count"}));
    EXPECT_FALSE(splitPropertyPath("count"));      // a plain variable
    EXPECT_FALSE(splitPropertyPath("items[i]"));   // a computed key
    EXPECT_FALSE(splitPropertyPath("a.b()"));      // a call, not a property
    EXPECT_NE(jsWatchInstall(2, "player", "score").find("\"score\""), std::string::npos);
    EXPECT_NE(jsWatchRemove(2).find("reg.get(2)"), std::string::npos);
}

TEST(Breakpoints, WatchChangeReport) {
    auto c = parseWatchChange("'4<<kevc>>3<<kevc>>5'"); // as the debugger shows a string result
    ASSERT_TRUE(c);
    EXPECT_EQ(c->id, 4);
    EXPECT_EQ(c->before, "3");
    EXPECT_EQ(c->after, "5");
    EXPECT_EQ(parseWatchChange("1<<kevc>>\"a\"<<kevc>>\"b\"")->after, "\"b\"");
    EXPECT_FALSE(parseWatchChange("''"));
    EXPECT_FALSE(parseWatchChange("x<<kevc>>1<<kevc>>2"));
}

TEST(Breakpoints, SavedFormKeepsOldFilesWorking) {
    FileBreakpoints file;
    file[3] = {};
    file[14] = {BreakKind::Live, "", {"i", "total"}};
    file[20] = {BreakKind::Conditional, "i == 41", {}};
    const auto json = toJson(file);
    EXPECT_EQ(json[0], 3); // standard ones stay plain line numbers, as older Kev-C++ saved them
    EXPECT_EQ(json[1]["kind"], "live");
    EXPECT_EQ(fileBreakpointsFromJson(json), file);
    // An older file: just lines.
    EXPECT_EQ(fileBreakpointsFromJson(nlohmann::json::array({5, 9})).size(), 2u);
    EXPECT_TRUE(fileBreakpointsFromJson(nlohmann::json::array({-1, "x", nlohmann::json::object()})).empty());
}

TEST(Breakpoints, GdbLiveArguments) {
    EXPECT_EQ(gdbLiveArguments(3, {"i", "name == \"a\\b\""}),
              (std::vector<std::string>{"<<kevc-live:3>>%s<<kevc>>%s\n", "$_kevc_value(\"i\")",
                                        "$_kevc_value(\"name == \\\"a\\\\b\\\"\")"}));
    EXPECT_EQ(gdbLiveArguments(4, {}), (std::vector<std::string>{"<<kevc-live:4>>%s\n", "$_kevc_locals()"}));
    auto [names, values] = splitNamedValues({"i = 3", "p = {x = 1, y = 2}", "odd"});
    EXPECT_EQ(names, (std::vector<std::string>{"i", "p", "odd"}));
    EXPECT_EQ(values, (std::vector<std::string>{"3", "{x = 1, y = 2}", ""}));
}

// The real thing: gdb runs a program with two live breakpoints, one naming a variable that isn't
// there. Both print every time and the program runs to its end.
TEST(Breakpoints, GdbLiveBreakpointsPrintWithoutStopping) {
    if (std::system("command -v gdb >/dev/null 2>&1 && command -v g++ >/dev/null 2>&1") != 0) GTEST_SKIP() << "no gdb or g++";
    char pattern[] = "/tmp/kevc-live-XXXXXX";
    ASSERT_TRUE(mkdtemp(pattern));
    const std::filesystem::path dir = pattern;
    std::ofstream(dir / "t.cpp") << "#include <string>\nint main()\n{\n    double total = 0;\n    std::string s = \"hi\";\n"
                                    "    for (int i = 0; i < 3; ++i)\n    {\n        total += i * 1.5;\n    }\n    return 0;\n}\n";
    ASSERT_EQ(std::system(("g++ -g -O0 -o " + (dir / "t").string() + " " + (dir / "t.cpp").string()).c_str()), 0);
    {
        std::ofstream in(dir / "in.txt");
        in << "-interpreter-exec console " << mi::quote(gdbLivePython()) << "\n";
        auto command = [](const std::vector<std::string>& args) {
            std::string out = "-dprintf-insert -f t.cpp:8";
            for (const auto& a : args) out += " " + mi::quote(a);
            return out + "\n";
        };
        in << command(gdbLiveArguments(1, {"i", "total", "nosuch + 1"}));
        in << command(gdbLiveArguments(2, {}));
        in << "-exec-run\n";
    }
    const std::string run = "cd " + dir.string() + " && (cat in.txt; sleep 3; echo -gdb-exit) | timeout 20 gdb -q -nx --interpreter=mi3 ./t 2>&1";
    FILE* pipe = popen(run.c_str(), "r");
    ASSERT_TRUE(pipe);
    std::vector<LiveLog> logs;
    bool exited = false, python = true;
    char buffer[8192];
    while (std::fgets(buffer, sizeof buffer, pipe)) {
        std::string line = buffer;
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        const auto record = mi::parseLine(line);
        if (record.type == mi::Record::Type::Console)
            if (auto log = parseLiveLog(record.text)) logs.push_back(*log);
        if (record.type == mi::Record::Type::Exec && record.results.value("reason", "") == "exited-normally") exited = true;
        if (record.type == mi::Record::Type::Result && record.resultClass == "error" &&
            record.results.value("msg", "").find("Python") != std::string::npos)
            python = false;
    }
    pclose(pipe);
    std::filesystem::remove_all(dir);
    if (!python) GTEST_SKIP() << "this gdb has no Python";
    EXPECT_TRUE(exited);
    ASSERT_EQ(logs.size(), 6u);
    const auto& first = logs[0].id == 1 ? logs[0] : logs[1];
    const auto& locals = logs[0].id == 1 ? logs[1] : logs[0];
    ASSERT_EQ(first.values.size(), 3u);
    EXPECT_EQ(first.values[0], "0");
    EXPECT_EQ(first.values[1], "0");
    EXPECT_NE(first.values[2].find("nosuch"), std::string::npos);
    EXPECT_EQ(first.values[2].front(), '<');
    auto [names, values] = splitNamedValues(locals.values);
    EXPECT_EQ(names, (std::vector<std::string>{"i", "total", "s"}));
    EXPECT_EQ(values[2], "\"hi\"");
    const auto& last = logs[4].id == 1 ? logs[4] : logs[5];
    EXPECT_EQ(last.values[0], "2");
    EXPECT_EQ(last.values[1], "1.5");
}
