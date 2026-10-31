#include "core/ToolLocator.h"

#include <gtest/gtest.h>

#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace kevc;

namespace {

class ToolLocatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() / ("kevc-tools-" + std::to_string(::getpid()) + "-" +
                                            ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(dir_);
        fs::create_directories(dir_);
    }
    void TearDown() override { fs::remove_all(dir_); }

    fs::path tool(const fs::path& relative, bool executable = true) {
        const fs::path p = dir_ / relative;
        fs::create_directories(p.parent_path());
        std::ofstream(p) << "#!/bin/sh\n";
        fs::permissions(p, executable ? fs::perms::owner_all : fs::perms::owner_read | fs::perms::owner_write);
        return p;
    }

    fs::path dir_;
};

} // namespace

TEST(ToolLocatorVersions, NumbersInPathOrder) {
    EXPECT_EQ(versionKey("stm32cubeide_1.19.0"), (std::vector<long long>{32, 1, 19, 0}));
    EXPECT_GT(versionKey("/opt/st/stm32cubeide_1.19.0/x"), versionKey("/opt/st/stm32cubeide_1.18.1/x"));
    EXPECT_GT(versionKey("tools-13.3.rel1"), versionKey("tools-12.10.rel1"));
}

TEST_F(ToolLocatorTest, PicksNewestStm32CubeIdeOffThePath) {
    const auto older = tool("opt/st/stm32cubeide_1.18.1/plugins/com.st.gnu-tools.13.3.rel1/tools/bin/arm-none-eabi-gcc");
    const auto newer = tool("opt/st/stm32cubeide_1.19.0/plugins/com.st.gnu-tools.13.3.rel1/tools/bin/arm-none-eabi-gcc");
    tool("opt/st/stm32cubeide_1.19.0/plugins/com.st.stlink-gdb-server_2.2.200/tools/bin/ST-LINK_gdbserver");

    ToolLocator locator({{}, {}, "", ToolLocator::defaultVendorGlobs(dir_ / "opt")});
    auto found = locator.find("arm-none-eabi-gcc");
    ASSERT_TRUE(found);
    EXPECT_EQ(found->path, newer);
    EXPECT_EQ(found->source, "vendor");
    const auto all = locator.findAll("arm-none-eabi-gcc");
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[1].path, older);
    EXPECT_TRUE(locator.find("ST-LINK_gdbserver"));
    EXPECT_FALSE(locator.find("openocd"));
}

TEST_F(ToolLocatorTest, StmCubeCltLayout) {
    const auto gdb = tool("opt/st/stm32cubeclt_1.18.0/GNU-tools-for-STM32/bin/arm-none-eabi-gdb");
    const auto cli = tool("opt/st/stm32cubeclt_1.18.0/STM32CubeProgrammer/bin/STM32_Programmer_CLI");
    ToolLocator locator({{}, {}, "", ToolLocator::defaultVendorGlobs(dir_ / "opt")});
    EXPECT_EQ(locator.find("arm-none-eabi-gdb")->path, gdb);
    EXPECT_EQ(locator.find("STM32_Programmer_CLI")->path, cli);
}

TEST_F(ToolLocatorTest, OrderIsSettingsThenNodeModulesThenPathThenVendor) {
    const auto chosen = tool("chosen/vite");
    const auto project = tool("app/node_modules/.bin/vite");
    const auto onPath = tool("bin/vite");
    tool("opt/st/stm32cubeide_1.19.0/plugins/p/tools/bin/vite");

    ToolLocator::Config config{{}, {dir_ / "app/node_modules/.bin"}, (dir_ / "bin").string(),
                               ToolLocator::defaultVendorGlobs(dir_ / "opt")};
    EXPECT_EQ(ToolLocator(config).find("vite")->path, project);
    EXPECT_EQ(ToolLocator(config).find("vite")->source, "node_modules");
    EXPECT_EQ(ToolLocator(config).findAll("vite").size(), 3u);

    config.overrides["vite"] = chosen;
    EXPECT_EQ(ToolLocator(config).find("vite")->path, chosen);

    config.overrides.clear();
    config.binDirs.clear();
    EXPECT_EQ(ToolLocator(config).find("vite")->path, onPath);
}

TEST_F(ToolLocatorTest, SkipsFilesThatAreNotExecutable) {
    tool("bin/svelte-check", false);
    ToolLocator locator({{}, {}, (dir_ / "bin").string(), {}});
    EXPECT_FALSE(locator.find("svelte-check"));
}
