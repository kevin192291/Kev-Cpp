#include "core/BuildPlan.h"
#include "core/ManagedBlock.h"
#include "core/ProjectSession.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace kevc;

namespace {

std::string slurp(const fs::path& p) {
    std::ifstream in(p);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void touch(const fs::path& p, const std::string& content = "") {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << content;
}

int run(const std::string& cmd) {
    int status = std::system((cmd + " >/dev/null 2>&1").c_str());
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

class ProjectSessionTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() / ("kevc-session-" + std::to_string(::getpid()) + "-" +
                                            ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(dir_);
        fs::create_directories(dir_);
    }
    void TearDown() override { fs::remove_all(dir_); }

    fs::path dir_;
};

} // namespace

TEST_F(ProjectSessionTest, CreateCppConsoleWritesProjectFiles) {
    auto s = ProjectSession::create(dir_, "Hello World", ProjectTemplate::CppConsole);
    const fs::path root = dir_ / "Hello World";
    EXPECT_EQ(s.root(), root);
    EXPECT_TRUE(fs::exists(root / "Hello World.kevc"));
    EXPECT_TRUE(fs::exists(root / "src/main.cpp"));
    EXPECT_EQ(s.project().sources, std::vector<std::string>{"src/main.cpp"});

    const std::string cmake = slurp(root / "CMakeLists.txt");
    EXPECT_NE(cmake.find("add_executable(hello_world)"), std::string::npos);
    EXPECT_NE(cmake.find("target_sources(hello_world PRIVATE src/main.cpp)"), std::string::npos);

    auto reopened = ProjectSession::open(*ProjectSession::findProjectFile(root));
    EXPECT_EQ(reopened.project().targetName, "hello_world");
    EXPECT_EQ(reopened.project().sources, s.project().sources);
}

TEST_F(ProjectSessionTest, CreateRefusesNonEmptyDirectory) {
    touch(dir_ / "taken/file.txt");
    EXPECT_THROW(ProjectSession::create(dir_, "taken", ProjectTemplate::CppConsole), std::runtime_error);
}

TEST_F(ProjectSessionTest, SyncAddsAndRemovesSourcesAndKeepsUserEdits) {
    auto s = ProjectSession::create(dir_, "app", ProjectTemplate::CppConsole);
    const fs::path cmakeFile = s.root() / "CMakeLists.txt";
    std::ofstream(cmakeFile, std::ios::app) << "add_subdirectory(extras) # mine\n";

    touch(s.root() / "src/extra.cpp");
    touch(s.root() / "include/extra.hpp");
    touch(s.root() / "notes.txt");             // not a source
    touch(s.root() / "build/generated.cpp");   // ignored dir
    touch(s.root() / ".hidden/secret.cpp");    // ignored dir

    auto r = s.sync();
    EXPECT_EQ(r.added, (std::vector<std::string>{"include/extra.hpp", "src/extra.cpp"}));
    EXPECT_TRUE(r.removed.empty());
    EXPECT_TRUE(r.buildFileWritten);

    std::string cmake = slurp(cmakeFile);
    EXPECT_NE(cmake.find("    src/extra.cpp\n"), std::string::npos);
    EXPECT_NE(cmake.find("add_subdirectory(extras) # mine\n"), std::string::npos);

    fs::remove(s.root() / "src/extra.cpp");
    r = s.sync();
    EXPECT_EQ(r.removed, std::vector<std::string>{"src/extra.cpp"});
    EXPECT_EQ(slurp(cmakeFile).find("src/extra.cpp"), std::string::npos);

    // Nothing changed: no rewrite.
    r = s.sync();
    EXPECT_FALSE(r.changed());
    EXPECT_FALSE(r.buildFileWritten);
}

TEST_F(ProjectSessionTest, ExcludedSourcesStayOut) {
    auto s = ProjectSession::create(dir_, "app", ProjectTemplate::CppConsole);
    s.project().excludedSources = {"src/scratch.cpp"};
    touch(s.root() / "src/scratch.cpp");
    EXPECT_FALSE(s.sync().changed());
}

TEST_F(ProjectSessionTest, MalformedBlockIsReportedNotOverwritten) {
    auto s = ProjectSession::create(dir_, "app", ProjectTemplate::CppConsole);
    const fs::path cmakeFile = s.root() / "CMakeLists.txt";
    const std::string broken = "project(app)\n" + std::string(managed_block::kBeginMarker) + "\nhalf a block\n";
    std::ofstream(cmakeFile) << broken;

    touch(s.root() / "src/more.cpp");
    auto r = s.sync();
    EXPECT_FALSE(r.buildFileWritten);
    ASSERT_EQ(r.warnings.size(), 1u);
    EXPECT_EQ(slurp(cmakeFile), broken);
}

TEST_F(ProjectSessionTest, CProjectWritesMakefileAndCompileCommands) {
    auto s = ProjectSession::create(dir_, "calc", ProjectTemplate::CConsole);
    s.setPkgConfigCflags([](const std::vector<std::string>&) { return std::vector<std::string>{}; });
    touch(s.root() / "src/util.c", "int twice(int x) { return 2 * x; }\n");
    s.sync();

    EXPECT_NE(slurp(s.root() / "Makefile").find("SRCS           := src/main.c src/util.c\n"), std::string::npos);
    EXPECT_NE(slurp(s.root() / "compile_commands.json").find("src/util.c"), std::string::npos);
}

TEST_F(ProjectSessionTest, AdoptExistingCMakeProject) {
    touch(dir_ / "legacy/CMakeLists.txt",
          "cmake_minimum_required(VERSION 3.20)\nproject(legacy CXX)\nadd_executable(tool main.cpp)\n");
    touch(dir_ / "legacy/main.cpp");
    touch(dir_ / "legacy/util.cpp");

    auto s = ProjectSession::adoptCMake(dir_ / "legacy");
    EXPECT_FALSE(s.project().ownsTarget);
    EXPECT_EQ(s.project().targetName, "tool");

    const std::string cmake = slurp(dir_ / "legacy/CMakeLists.txt");
    EXPECT_TRUE(cmake.starts_with(
        "cmake_minimum_required(VERSION 3.20)\nproject(legacy CXX)\nadd_executable(tool main.cpp)\n"));
    EXPECT_NE(cmake.find("target_sources(tool PRIVATE\n    main.cpp\n    util.cpp\n)"), std::string::npos);
}

TEST_F(ProjectSessionTest, PackageJsonFolderOpensAsTypeScriptWithoutWritingAnything) {
    touch(dir_ / "webapp/package.json", "{\"name\": \"webapp\"}\n");
    touch(dir_ / "webapp/src/main.ts");
    touch(dir_ / "webapp/index.html");
    touch(dir_ / "webapp/node_modules/lib/index.js");

    auto s = ProjectSession::openWebFolder(dir_ / "webapp/");
    EXPECT_EQ(s.project().kind, ProjectKind::WebTs);
    EXPECT_EQ(s.project().name, "webapp");
    EXPECT_EQ(s.root(), dir_ / "webapp");
    EXPECT_TRUE(s.projectFile().empty());
    EXPECT_EQ(s.project().sources, (std::vector<std::string>{"index.html", "src/main.ts"}));

    touch(dir_ / "webapp/src/util.ts");
    const auto result = s.sync();
    EXPECT_EQ(result.added, std::vector<std::string>{"src/util.ts"});
    s.saveAndRegenerate();
    EXPECT_FALSE(ProjectSession::findProjectFile(dir_ / "webapp"));
}

TEST_F(ProjectSessionTest, KevcKeepsKeysThisVersionDoesNotUse) {
    auto s = ProjectSession::create(dir_, "keep", ProjectTemplate::CppConsole);
    std::string text = slurp(s.projectFile());
    text.insert(text.rfind('}'), ",\n  \"stm32\": {\"ioc\": \"keep.ioc\", \"probe\": \"0670FF\"}\n");
    std::ofstream(s.projectFile()) << text;

    auto reopened = ProjectSession::open(s.projectFile());
    reopened.project().defines.push_back("NEW=1");
    reopened.saveAndRegenerate();
    const std::string saved = slurp(s.projectFile());
    EXPECT_NE(saved.find("\"stm32\""), std::string::npos);
    EXPECT_NE(saved.find("\"0670FF\""), std::string::npos);
    EXPECT_NE(saved.find("NEW=1"), std::string::npos);
}

// End-to-end: generated projects must actually build with gcc/g++.
TEST_F(ProjectSessionTest, GeneratedCppProjectBuildsWithCMakeAndGxx) {
    auto s = ProjectSession::create(dir_, "built", ProjectTemplate::CppConsole);
    touch(s.root() / "src/extra.cpp", "int extra() { return 42; }\n");
    s.sync();

    const std::string root = "'" + s.root().string() + "'";
    ASSERT_EQ(run("cmake -S " + root + " -B " + root + "/build/debug -DCMAKE_BUILD_TYPE=Debug"
                  " -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++"), 0);
    ASSERT_EQ(run("cmake --build " + root + "/build/debug"), 0);
    EXPECT_EQ(run(root + "/build/debug/built"), 0);
    EXPECT_NE(slurp(s.root() / "build/debug/compile_commands.json").find("src/extra.cpp"), std::string::npos);
}

TEST_F(ProjectSessionTest, GeneratedCProjectBuildsWithMakeAndGcc) {
    auto s = ProjectSession::create(dir_, "cbuilt", ProjectTemplate::CConsole);
    s.project().linkLibraries = {"m"};
    touch(s.root() / "src/root.c", "#include <math.h>\ndouble root2(void) { return sqrt(2.0); }\n");
    s.sync();

    const std::string root = "'" + s.root().string() + "'";
    ASSERT_EQ(run("make -C " + root + " DEBUG=1"), 0);
    EXPECT_EQ(run(root + "/build/debug/cbuilt"), 0);
    ASSERT_EQ(run("make -C " + root), 0);
    EXPECT_EQ(run(root + "/build/release/cbuilt"), 0);
}

TEST_F(ProjectSessionTest, GeneratedStaticLibraryBuilds) {
    auto s = ProjectSession::create(dir_, "mylib", ProjectTemplate::CppStatic);
    const std::string root = "'" + s.root().string() + "'";
    ASSERT_EQ(run("cmake -S " + root + " -B " + root + "/build/debug -DCMAKE_CXX_COMPILER=g++"), 0);
    ASSERT_EQ(run("cmake --build " + root + "/build/debug"), 0);
    EXPECT_TRUE(fs::exists(s.root() / "build/debug/libmylib.a"));
}

TEST_F(ProjectSessionTest, GtkmmApplicationStartsWithADesignedWindow) {
    auto s = ProjectSession::create(dir_, "my notes", ProjectTemplate::GtkmmApp);
    for (const char* f : {"src/main.cpp", "src/main_window.ui", "src/main_window.ui.h", "src/main_window.ui.cpp",
                          "src/main_window.h", "src/main_window.cpp"})
        EXPECT_TRUE(fs::exists(s.root() / f)) << f;
    ASSERT_EQ(s.project().packages.size(), 1u);
    EXPECT_EQ(s.project().packages[0].name, "gtkmm-4.0");
    EXPECT_EQ(s.project().packages[0].source, Package::Source::System);
    EXPECT_NE(slurp(s.root() / "src/main_window.ui").find("<property name=\"title\">my notes</property>"), std::string::npos);
    EXPECT_NE(slurp(s.root() / "src/main_window.cpp").find("greeting->set_text("), std::string::npos);
    EXPECT_NE(slurp(s.root() / "src/main.cpp").find("Gtk::Application::create(\"org.kevcpp.my_notes\")"), std::string::npos);
    EXPECT_NE(slurp(s.root() / "CMakeLists.txt").find("gtkmm-4.0"), std::string::npos);

    if (run("pkg-config --exists gtkmm-4.0") != 0) GTEST_SKIP() << "gtkmm-4.0 development files aren't installed";
    const std::string root = "'" + s.root().string() + "'";
    ASSERT_EQ(run("cmake -S " + root + " -B " + root + "/build/debug -DCMAKE_BUILD_TYPE=Debug"
                  " -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++"), 0);
    ASSERT_EQ(run("cmake --build " + root + "/build/debug"), 0);
    EXPECT_TRUE(fs::exists(s.root() / "build/debug" / s.project().effectiveOutputName()));
}

TEST_F(ProjectSessionTest, GtkCApplicationBuildsWithMake) {
    auto s = ProjectSession::create(dir_, "cnotes", ProjectTemplate::GtkCApp);
    for (const char* f : {"src/main.c", "src/main_window.ui", "src/main_window-ui.h", "src/main_window-ui.c", "src/main_window.c"})
        EXPECT_TRUE(fs::exists(s.root() / f)) << f;
    EXPECT_NE(slurp(s.root() / "src/main_window.c").find("gtk_label_set_text(ui->greeting"), std::string::npos);
    ASSERT_EQ(s.project().packages.size(), 1u);
    EXPECT_EQ(s.project().packages[0].name, "gtk4");

    if (run("pkg-config --exists gtk4") != 0) GTEST_SKIP() << "gtk4 development files aren't installed";
    const std::string root = "'" + s.root().string() + "'";
    ASSERT_EQ(run("make -C " + root + " DEBUG=1"), 0);
    EXPECT_TRUE(fs::exists(s.root() / "build/debug/cnotes"));
}

// A project with its own CMake set-up (several targets, sources listed its own way), opened with
// "Build As It Is": Kev-C++ writes only the .kevc and runs the executable it picks.
TEST_F(ProjectSessionTest, BuildAsItIsLeavesCMakeListsAlone) {
    const fs::path root = dir_ / "tool";
    const std::string top = "cmake_minimum_required(VERSION 3.20)\nproject(tool CXX)\n"
                            "add_library(core STATIC src/core.cpp)\n"
                            "add_executable(Qt5::moc IMPORTED)\n"
                            "add_executable(tool-app\n  src/main.cpp)\n"
                            "add_subdirectory(tests)\n";
    touch(root / "CMakeLists.txt", top);
    touch(root / "tests/CMakeLists.txt", "add_executable(tool_tests t.cpp)\nadd_executable(alias ALIAS tool_tests)\n");
    touch(root / "build/debug/CMakeLists.txt", "add_executable(ignored x.cpp)\n");
    touch(root / "src/main.cpp", "int main() {}\n");
    touch(root / "src/core.cpp");

    EXPECT_EQ(cmakeExecutables(root), (std::vector<std::string>{"tool-app", "tool_tests"}));
    ProjectSession s = ProjectSession::useCMakeAsIs(root);
    EXPECT_EQ(slurp(root / "CMakeLists.txt"), top); // untouched
    EXPECT_TRUE(fs::exists(root / "tool.kevc"));
    EXPECT_EQ(s.project().targetName, "tool-app");
    EXPECT_FALSE(s.project().buildFileManaged);
    // New files don't touch anything either.
    touch(root / "src/extra.cpp");
    EXPECT_TRUE(s.sync().added.empty());
    EXPECT_EQ(slurp(root / "CMakeLists.txt"), top);
    // Reopened from its .kevc it's still as-it-is.
    const ProjectSession again = ProjectSession::open(root / "tool.kevc");
    EXPECT_FALSE(again.project().buildFileManaged);

    // Run finds the program wherever CMake put it.
    Project p = s.project();
    p.targetName = "tool_tests";
    touch(root / "build/debug/tests/tool_tests", "#!/bin/sh\n");
    fs::permissions(root / "build/debug/tests/tool_tests", fs::perms::owner_all);
    touch(root / "build/debug/CMakeFiles/tool_tests", ""); // not the program
    EXPECT_EQ(runnableOutput(p, root, BuildConfig::Debug), root / "build/debug/tests/tool_tests");
}
