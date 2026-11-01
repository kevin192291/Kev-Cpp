#include "core/ProjectState.h"

#include <gtest/gtest.h>

#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace kevc;

namespace {

class ProjectStateTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() / ("kevc-state-" + std::to_string(::getpid()) + "-" +
                                            ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(dir_);
        fs::create_directories(dir_ / "proj/src");
        fs::create_directories(dir_ / "lib");
        for (const char* f : {"proj/src/main.cpp", "proj/src/util.cpp", "lib/vec.h"}) std::ofstream(dir_ / f) << "\n";
    }
    void TearDown() override { fs::remove_all(dir_); }
    fs::path dir_;
};

} // namespace

TEST_F(ProjectStateTest, BreakpointsAndWatchesSurviveARestart) {
    const fs::path file = dir_ / "state/projects.json", root = dir_ / "proj";
    ProjectState state;
    state.breakpoints[root / "src/main.cpp"] = {3, 12};
    state.breakpoints[dir_ / "lib/vec.h"] = {7}; // outside the project: kept by its full path
    state.breakpoints[root / "src/util.cpp"] = {};  // nothing there: not saved
    state.watches = {"x", "p->next"};
    ProjectStateStore(file).save(root, state);

    const ProjectState again = ProjectStateStore(file).load(root);
    EXPECT_EQ(again.breakpoints.size(), 2u);
    EXPECT_EQ(again.breakpoints.at(root / "src/main.cpp"), (std::set<int>{3, 12}));
    EXPECT_EQ(again.breakpoints.at(dir_ / "lib/vec.h"), (std::set<int>{7}));
    EXPECT_EQ(again.watches, (std::vector<std::string>{"x", "p->next"}));
    // Files inside the project are stored relative to it.
    std::ifstream in(file);
    const auto json = nlohmann::json::parse(in);
    EXPECT_TRUE(json.at(root.string()).at("breakpoints").contains("src/main.cpp"));
    // Another project has nothing; the same folder spelled with a slash is the same project.
    EXPECT_EQ(ProjectStateStore(file).load(dir_ / "other"), ProjectState{});
    EXPECT_EQ(ProjectStateStore(file).load(root.string() + "/").watches.size(), 2u);
}

TEST_F(ProjectStateTest, GoneFilesAreDroppedAndEmptyStatesRemoved) {
    const fs::path file = dir_ / "projects.json", root = dir_ / "proj";
    ProjectStateStore store(file);
    store.save(root, {{{root / "src/main.cpp", {1}}, {root / "src/util.cpp", {2}}}, {}});
    fs::remove(root / "src/util.cpp");
    EXPECT_EQ(ProjectStateStore(file).load(root).breakpoints.size(), 1u);
    store.save(root, {});
    std::ifstream in(file);
    EXPECT_FALSE(nlohmann::json::parse(in).contains(root.string()));
}

TEST_F(ProjectStateTest, KeepsTheMostRecentProjectsAndSurvivesADamagedFile) {
    const fs::path file = dir_ / "projects.json";
    ProjectStateStore store(file);
    for (int i = 0; i < 5; ++i) store.save(dir_ / ("p" + std::to_string(i)), {{}, {"w"}}, 3);
    ProjectStateStore reread(file);
    EXPECT_TRUE(reread.load(dir_ / "p0").watches.empty()); // the oldest went
    EXPECT_FALSE(reread.load(dir_ / "p4").watches.empty());
    std::ofstream(file) << "{ not json";
    ProjectStateStore damaged(file);
    EXPECT_EQ(damaged.load(dir_ / "p4"), ProjectState{});
    damaged.save(dir_ / "p9", {{}, {"y"}});
    EXPECT_EQ(ProjectStateStore(file).load(dir_ / "p9").watches, (std::vector<std::string>{"y"}));
}

TEST_F(ProjectStateTest, AWebProjectsLookIsRememberedToo) {
    const fs::path file = dir_ / "projects.json", root = dir_ / "proj";
    ProjectState state;
    state.ui.panels["traces"] = false;
    state.ui.toolbars["web"] = true;
    ProjectStateStore(file).save(root, state);
    const auto again = ProjectStateStore(file).load(root);
    EXPECT_EQ(again.ui.panels.at("traces"), false);
    EXPECT_EQ(again.ui.toolbars.at("web"), true);
}

TEST_F(ProjectStateTest, KevcKeepsTheProjectsLook) {
    const fs::path kevc = dir_ / "proj/proj.kevc";
    Project p;
    p.name = "proj";
    p.targetName = "proj";
    p.ui.toolbars["debug"] = false;
    p.ui.panels["compile-log"] = false;
    p.ui.panels["terminal"] = true;
    p.ui.buildConfig = "Release";
    p.save(kevc);
    const Project again = Project::load(kevc);
    EXPECT_EQ(again.ui, p.ui);
    // Nothing set: no "ui" key at all, so older project files stay as they were.
    Project plain;
    plain.name = "plain";
    plain.targetName = "plain";
    plain.save(kevc);
    std::ifstream in(kevc);
    EXPECT_FALSE(nlohmann::json::parse(in).contains("ui"));
    // Junk is ignored, not an error.
    EXPECT_TRUE(uiFromJson(nlohmann::json{{"toolbars", {{"web", "yes"}}}, {"buildConfig", "Fast"}}).empty());
}
