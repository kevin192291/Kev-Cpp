#include "core/Diagnostics.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace kevc;
using Sev = Diagnostic::Severity;

namespace {

std::vector<Diagnostic> parseAll(DiagnosticParser& p, const std::vector<std::string>& lines) {
    std::vector<Diagnostic> out;
    for (const auto& l : lines)
        for (auto& d : p.feed(l)) out.push_back(d);
    return out;
}

} // namespace

TEST(Diagnostics, GccErrorWarningNote) {
    DiagnosticParser p("/proj");
    auto ds = parseAll(p, {
        "/proj/src/main.cpp: In function 'int main()':",
        "/proj/src/main.cpp:4:5: error: 'x' was not declared in this scope",
        "    4 |     x = 1;",
        "      |     ^",
        "src/util.c:10:12: warning: unused variable 'y' [-Wunused-variable]",
        "/usr/include/c++/13/bits/stl_vector.h:100: note: declared here",
        "main.cpp:2:10: fatal error: nope.h: No such file or directory",
    });
    ASSERT_EQ(ds.size(), 4u);
    EXPECT_EQ(ds[0].severity, Sev::Error);
    EXPECT_EQ(ds[0].file, "/proj/src/main.cpp");
    EXPECT_EQ(ds[0].line, 4);
    EXPECT_EQ(ds[0].column, 5);
    EXPECT_EQ(ds[0].message, "'x' was not declared in this scope");
    EXPECT_EQ(ds[1].severity, Sev::Warning);
    EXPECT_EQ(ds[1].file, "/proj/src/util.c"); // relative paths resolve against the base dir
    EXPECT_EQ(ds[2].severity, Sev::Note);
    EXPECT_EQ(ds[2].column, 0);
    EXPECT_EQ(ds[3].severity, Sev::Error);
    EXPECT_EQ(ds[3].message, "nope.h: No such file or directory");
    EXPECT_EQ(p.errors(), 2);
    EXPECT_EQ(p.warnings(), 1);
}

TEST(Diagnostics, StripsColour) {
    DiagnosticParser p("/proj");
    auto ds = p.feed("\x1b[01m\x1b[Ka.c:1:2:\x1b[m\x1b[K \x1b[01;31m\x1b[Kerror: \x1b[m\x1b[Kboom");
    ASSERT_EQ(ds.size(), 1u);
    EXPECT_EQ(ds[0].message, "boom");
}

TEST(Diagnostics, MakeEnteringDirectoryChangesBase) {
    DiagnosticParser p("/proj");
    auto ds = parseAll(p, {
        "make: Entering directory '/other/place'",
        "src/x.c:1:1: error: a",
        "make: Leaving directory '/other/place'",
        "src/y.c:1:1: error: b",
        "make: *** [Makefile:40: build/debug/obj/src/x.o] Error 1",
    });
    ASSERT_EQ(ds.size(), 2u);
    EXPECT_EQ(ds[0].file, "/other/place/src/x.c");
    EXPECT_EQ(ds[1].file, "/proj/src/y.c");
}

TEST(Diagnostics, LinkerErrors) {
    DiagnosticParser p("/proj");
    auto ds = parseAll(p, {
        "/usr/bin/ld: CMakeFiles/app.dir/src/main.cpp.o: in function `main':",
        "main.cpp:(.text+0x9): undefined reference to `helper()'",
        "/usr/bin/ld: cannot find -lnosuchlib: No such file or directory",
        "collect2: error: ld returned 1 exit status",
    });
    ASSERT_EQ(ds.size(), 2u);
    EXPECT_FALSE(ds[0].hasLocation());
    EXPECT_EQ(ds[0].message, "main.cpp: undefined reference to `helper()'");
    EXPECT_EQ(ds[1].message, "cannot find -lnosuchlib: No such file or directory");
    EXPECT_EQ(p.errors(), 2);
}

TEST(Diagnostics, LinkerErrorWithDebugInfoHasLocation) {
    DiagnosticParser p("/proj");
    auto ds = parseAll(p, {
        "/usr/bin/ld: m.o: in function `main':",
        "/proj/src/m.c:3:(.text+0x9): undefined reference to `helper'",
    });
    ASSERT_EQ(ds.size(), 1u);
    EXPECT_EQ(ds[0].file, "/proj/src/m.c");
    EXPECT_EQ(ds[0].line, 3);
    EXPECT_EQ(ds[0].message, "undefined reference to `helper'");
}

TEST(Diagnostics, TypeScriptCompiler) {
    DiagnosticParser p("/web");
    auto ds = parseAll(p, {"src/main.ts(5,7): error TS2345: Argument of type 'number' is not assignable to parameter of type 'string'."});
    ASSERT_EQ(ds.size(), 1u);
    EXPECT_EQ(ds[0].file, "/web/src/main.ts");
    EXPECT_EQ(ds[0].line, 5);
    EXPECT_EQ(ds[0].column, 7);
    EXPECT_EQ(ds[0].message, "Argument of type 'number' is not assignable to parameter of type 'string'. (TS2345)");
}

TEST(Diagnostics, CMakeErrors) {
    DiagnosticParser p("/proj");
    auto ds = parseAll(p, {
        "CMake Error at CMakeLists.txt:8 (target_sources):",
        "  Cannot find source file:",
        "",
        "    nope.cpp",
        "CMake Error: The source directory \"/gone\" does not exist.",
    });
    ASSERT_EQ(ds.size(), 2u);
    EXPECT_EQ(ds[0].file, "/proj/CMakeLists.txt");
    EXPECT_EQ(ds[0].line, 8);
    EXPECT_EQ(ds[0].message, "Cannot find source file:");
    EXPECT_FALSE(ds[1].hasLocation());
}

TEST(Diagnostics, CMakeHeaderWithoutMessageIsStillReported) {
    DiagnosticParser p("/proj");
    auto first = p.feed("CMake Warning at CMakeLists.txt:3:");
    EXPECT_TRUE(first.empty());
    auto ds = p.feed("a.cpp:1:1: error: next");
    ASSERT_EQ(ds.size(), 2u);
    EXPECT_EQ(ds[0].severity, Sev::Warning);
    EXPECT_EQ(ds[1].message, "next");
}

// Guards against gcc changing its format: parse what the installed g++ actually prints.
TEST(Diagnostics, RealGxxOutput) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("kevc-diag-" + std::to_string(::getpid()));
    fs::create_directories(dir);
    std::ofstream(dir / "bad.cpp") << "int helper();\nint main() {\n    int unused = 0;\n    return helper() + missing;\n}\n";

    const std::string cmd = "cd '" + dir.string() + "' && LC_ALL=C g++ -Wall -c bad.cpp 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    ASSERT_NE(pipe, nullptr);
    DiagnosticParser p(dir);
    std::vector<Diagnostic> ds;
    std::array<char, 1024> buf{};
    while (fgets(buf.data(), buf.size(), pipe)) {
        std::string line = buf.data();
        if (!line.empty() && line.back() == '\n') line.pop_back();
        for (auto& d : p.feed(line)) ds.push_back(d);
    }
    pclose(pipe);
    fs::remove_all(dir);

    auto err = std::find_if(ds.begin(), ds.end(), [](const Diagnostic& d) { return d.severity == Sev::Error; });
    ASSERT_NE(err, ds.end());
    EXPECT_EQ(err->file, (dir / "bad.cpp").string());
    EXPECT_EQ(err->line, 4);
    EXPECT_NE(err->message.find("missing"), std::string::npos);
    EXPECT_EQ(p.warnings(), 1); // unused variable
}
