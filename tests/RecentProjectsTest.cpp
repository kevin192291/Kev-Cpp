#include "core/RecentProjects.h"

#include <gtest/gtest.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace kevc;

namespace {

class RecentProjectsTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() / ("kevc-recent-" + std::to_string(::getpid()));
        fs::remove_all(dir_);
        for (const char* p : {"a", "b", "c"}) fs::create_directories(dir_ / p);
    }
    void TearDown() override { fs::remove_all(dir_); }
    fs::path dir_;
};

} // namespace

TEST_F(RecentProjectsTest, MostRecentFirstWithoutDuplicates) {
    Settings settings(dir_ / "settings.json");
    RecentProjects recent(settings);
    recent.opened(dir_ / "a");
    recent.opened(dir_ / "b");
    recent.opened(dir_ / "a/"); // the same folder, spelled differently
    EXPECT_EQ(recent.recent(), (std::vector<fs::path>{dir_ / "a", dir_ / "b"}));

    // Saved: a new instance reading the same file sees the same list.
    Settings again(dir_ / "settings.json");
    again.load();
    EXPECT_EQ(RecentProjects(again).recent(), (std::vector<fs::path>{dir_ / "a", dir_ / "b"}));
}

TEST_F(RecentProjectsTest, KeepsOnlyTheLatestTen) {
    Settings settings;
    RecentProjects recent(settings);
    for (int i = 0; i < 14; ++i) recent.opened(dir_ / ("p" + std::to_string(i)));
    const auto list = recent.recent();
    ASSERT_EQ(list.size(), RecentProjects::kMaxRecent);
    EXPECT_EQ(list.front(), dir_ / "p13");
    EXPECT_EQ(list.back(), dir_ / "p4");
}

TEST_F(RecentProjectsTest, PinnedOnesAreListedApartAndStay) {
    Settings settings;
    RecentProjects recent(settings);
    recent.opened(dir_ / "a");
    recent.opened(dir_ / "b");
    recent.setPinned(dir_ / "a", true);
    recent.setPinned(dir_ / "c", true); // never opened: can still be pinned
    EXPECT_EQ(recent.pinned(), (std::vector<fs::path>{dir_ / "a", dir_ / "c"}));
    EXPECT_EQ(recent.recent(), (std::vector<fs::path>{dir_ / "b"}));
    EXPECT_TRUE(recent.isPinned(dir_ / "a"));
    EXPECT_FALSE(recent.isPinned(dir_ / "b"));

    // Opening more never pushes a pinned one out.
    for (int i = 0; i < 12; ++i) recent.opened(dir_ / ("p" + std::to_string(i)));
    EXPECT_EQ(recent.pinned(), (std::vector<fs::path>{dir_ / "a", dir_ / "c"}));

    // Unpinned, it goes back to the top of the recent list.
    recent.setPinned(dir_ / "c", false);
    EXPECT_EQ(recent.pinned(), (std::vector<fs::path>{dir_ / "a"}));
    EXPECT_EQ(recent.recent().front(), dir_ / "c");
}

TEST_F(RecentProjectsTest, ForgetsAFolder) {
    Settings settings;
    RecentProjects recent(settings);
    recent.opened(dir_ / "a");
    recent.opened(dir_ / "b");
    recent.forget(dir_ / "a");
    EXPECT_EQ(recent.recent(), (std::vector<fs::path>{dir_ / "b"}));
}
