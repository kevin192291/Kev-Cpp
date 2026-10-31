#include "core/ProjectKinds.h"
#include "core/Settings.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace kevc;

namespace {

class SettingsTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() / ("kevc-settings-" + std::to_string(::getpid()) + "-" +
                                            ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(dir_);
        fs::create_directories(dir_);
    }
    void TearDown() override { fs::remove_all(dir_); }

    std::string slurp(const fs::path& p) {
        std::ifstream in(p);
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    fs::path dir_;
};

} // namespace

TEST_F(SettingsTest, MissingFileGivesDefaultsAndSetCreatesIt) {
    Settings s(dir_ / "kev-cpp" / "settings.json");
    s.load();
    EXPECT_EQ(s.getString("/web/browser", "xdg-open"), "xdg-open");
    EXPECT_EQ(s.get<int>("/web/port", 5173), 5173);

    s.set("/web/browser", "firefox");
    EXPECT_TRUE(fs::exists(dir_ / "kev-cpp" / "settings.json"));
    Settings reread(dir_ / "kev-cpp" / "settings.json");
    reread.load();
    EXPECT_EQ(reread.getString("/web/browser"), "firefox");
}

TEST_F(SettingsTest, KeepsKeysItDoesNotUse) {
    std::ofstream(dir_ / "settings.json") << R"({"future": {"feature": [1, 2]}, "web": {"browser": "a"}})";
    Settings s(dir_ / "settings.json");
    s.load();
    s.set("/toolbars/debug/cpp", "hidden");
    const std::string text = slurp(dir_ / "settings.json");
    EXPECT_NE(text.find("\"future\""), std::string::npos);
    EXPECT_NE(text.find("\"browser\": \"a\""), std::string::npos);
    EXPECT_NE(text.find("\"hidden\""), std::string::npos);
}

TEST_F(SettingsTest, DamagedFileOrWrongTypesFallBack) {
    std::ofstream(dir_ / "settings.json") << "{ not json";
    Settings s(dir_ / "settings.json");
    s.load();
    EXPECT_EQ(s.getString("/a", "fallback"), "fallback");
    s.set("/a", 5);
    EXPECT_EQ(s.getString("/a", "fallback"), "fallback"); // an int, not a string
    EXPECT_EQ(s.get<int>("/a", 0), 5);
    EXPECT_EQ(s.getString("not a pointer", "x"), "x");
}

TEST_F(SettingsTest, RemoveDeletesOneKey) {
    Settings s(dir_ / "settings.json");
    s.set("/tools/vite", "/opt/vite");
    s.set("/tools/node", "/opt/node");
    s.remove("/tools/vite");
    EXPECT_EQ(s.getString("/tools/vite"), "");
    EXPECT_EQ(s.getString("/tools/node"), "/opt/node");
}

TEST(ProjectKinds, ProfilesAndTraits) {
    Project p;
    p.kind = ProjectKind::WebTs;
    EXPECT_EQ(profileFor(&p, true), "web");
    p.kind = ProjectKind::C;
    EXPECT_EQ(profileFor(&p, true), "c");
    EXPECT_EQ(profileFor(nullptr, true), "folder");
    EXPECT_EQ(profileFor(nullptr, false), "none");
    EXPECT_STREQ(traitsOf(ProjectKind::C).standard, "17");
    EXPECT_STREQ(traitsOf(ProjectKind::Cpp).newFileName, "src/untitled.cpp");
    EXPECT_STREQ(traitsOf(ProjectKind::WebTs).gitignore, "node_modules/\n");
}

TEST(ProjectKinds, ToolbarAndPanelVisibilityPerProfile) {
    Settings s; // in memory
    // Defaults: the web toolbar and the Dev Server panel only for web projects.
    EXPECT_TRUE(toolbarGroupShown(s, "web", "web"));
    EXPECT_FALSE(toolbarGroupShown(s, "web", "cpp"));
    EXPECT_TRUE(toolbarGroupShown(s, "debug", "cpp"));
    EXPECT_TRUE(panelShown(s, "dev-server", "web"));
    EXPECT_FALSE(panelShown(s, "dev-server", "c"));
    EXPECT_TRUE(panelShown(s, "terminal", "none"));

    // A choice applies to that profile only.
    setToolbarGroupShown(s, "debug", "cpp", false);
    EXPECT_FALSE(toolbarGroupShown(s, "debug", "cpp"));
    EXPECT_TRUE(toolbarGroupShown(s, "debug", "c"));
    setPanelShown(s, "dev-server", "cpp", true);
    EXPECT_TRUE(panelShown(s, "dev-server", "cpp"));
    EXPECT_FALSE(panelShown(s, "dev-server", "c"));
}
