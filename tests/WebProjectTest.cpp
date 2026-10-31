#include "core/BuildPlan.h"
#include "core/Diagnostics.h"
#include "core/WebProject.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <fstream>

namespace fs = std::filesystem;
using namespace kevc;

namespace {

class WebProjectTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() / ("kevc-web-" + std::to_string(::getpid()) + "-" +
                                            ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(dir_);
        fs::create_directories(dir_);
    }
    void TearDown() override { fs::remove_all(dir_); }

    void write(const fs::path& rel, const std::string& text) {
        fs::create_directories((dir_ / rel).parent_path());
        std::ofstream(dir_ / rel) << text;
    }
    // node_modules/.bin/<name> -> ../<package>/<script>, as npm lays it out.
    void bin(const std::string& name, const std::string& target) {
        write("node_modules/" + target, "#!/usr/bin/env node\n");
        fs::create_directories(dir_ / "node_modules/.bin");
        fs::create_symlink("../" + target, dir_ / "node_modules/.bin" / name);
    }

    fs::path dir_;
};

const char* kKitPackage = R"({
  "name": "kitapp",
  "scripts": {"dev": "vite dev", "build": "vite build", "check": "svelte-kit sync && svelte-check"},
  "devDependencies": {"@sveltejs/kit": "^3.0.0", "svelte": "^5.0.0", "vite": "^8.0.0", "svelte-check": "^4"}
})";

} // namespace

TEST_F(WebProjectTest, FlavourFromDependencies) {
    EXPECT_EQ(WebProject::detect(dir_).flavor, WebFlavor::Plain); // no package.json at all
    EXPECT_FALSE(WebProject::detect(dir_).hasPackageJson);

    write("package.json", kKitPackage);
    auto kit = WebProject::detect(dir_);
    EXPECT_EQ(kit.flavor, WebFlavor::SvelteKit);
    EXPECT_STREQ(kit.label(), "SvelteKit");
    EXPECT_TRUE(kit.svelte());
    EXPECT_TRUE(kit.usesVite());
    ASSERT_EQ(kit.scripts.size(), 3u);
    EXPECT_EQ(kit.scripts[0].first, "dev"); // package.json order
    EXPECT_TRUE(kit.hasScript("build"));

    write("package.json", R"({"dependencies": {"svelte": "5"}, "devDependencies": {"vite": "8"}})");
    EXPECT_EQ(WebProject::detect(dir_).flavor, WebFlavor::Svelte);
    write("package.json", R"({"devDependencies": {"vite": "8", "react": "19"}})");
    EXPECT_EQ(WebProject::detect(dir_).flavor, WebFlavor::Vite);
    write("package.json", R"({"dependencies": {"express": "5"}})");
    EXPECT_EQ(WebProject::detect(dir_).flavor, WebFlavor::Plain);
    write("package.json", "{ broken");
    EXPECT_EQ(WebProject::detect(dir_).flavor, WebFlavor::Plain);
}

TEST_F(WebProjectTest, PackageManagerFromFieldOrLockfile) {
    write("package.json", "{}");
    EXPECT_EQ(WebProject::detect(dir_).packageManagerName(), "npm");
    write("pnpm-lock.yaml", "");
    EXPECT_EQ(WebProject::detect(dir_).packageManagerName(), "pnpm");
    write("package.json", R"({"packageManager": "yarn@4.1.0"})");
    EXPECT_EQ(WebProject::detect(dir_).packageManagerName(), "yarn"); // the field wins over the lockfile
}

TEST_F(WebProjectTest, ScriptCommandsPerManager) {
    WebProject npm;
    EXPECT_EQ(npm.installCommand(), (std::vector<std::string>{"npm", "install"}));
    EXPECT_EQ(npm.runScriptCommand("dev", {"--port", "5174"}),
              (std::vector<std::string>{"npm", "run", "dev", "--", "--port", "5174"}));
    EXPECT_EQ(npm.runScriptCommand("build"), (std::vector<std::string>{"npm", "run", "build"}));
    WebProject pnpm;
    pnpm.packageManager = PackageManager::Pnpm;
    EXPECT_EQ(pnpm.runScriptCommand("dev", {"--port", "5174"}),
              (std::vector<std::string>{"pnpm", "run", "dev", "--port", "5174"}));
}

TEST_F(WebProjectTest, ViteRunsDirectlyWhenDevIsPlainVite) {
    write("package.json", kKitPackage);
    bin("vite", "vite/bin/vite.js");
    auto web = WebProject::detect(dir_);
    const auto argv = viteDevCommand(web, dir_, "/usr/bin/node", 5174);
    ASSERT_GE(argv.size(), 3u);
    EXPECT_EQ(argv[0], "/usr/bin/node");
    EXPECT_EQ(argv[1], fs::canonical(dir_ / "node_modules/vite/bin/vite.js").string());
    EXPECT_EQ(std::vector<std::string>(argv.begin() + 2, argv.end()),
              (std::vector<std::string>{"dev", "--port", "5174", "--strictPort", "--host", "127.0.0.1"}));

    // A custom dev script goes through the package manager.
    write("package.json", R"({"scripts": {"dev": "vite dev --mode staging"}, "devDependencies": {"vite": "8"}})");
    web = WebProject::detect(dir_);
    EXPECT_EQ(viteDevCommand(web, dir_, "node", 5174),
              (std::vector<std::string>{"npm", "run", "dev", "--", "--port", "5174", "--strictPort", "--host", "127.0.0.1"}));
}

TEST(WebProject, ViteReadyLine) {
    EXPECT_EQ(viteReadyUrl("  \xe2\x9e\x9c  Local:   http://127.0.0.1:5243/"), "http://127.0.0.1:5243/");
    EXPECT_EQ(viteReadyUrl("\x1b[32m  ➜\x1b[39m  \x1b[1mLocal\x1b[22m:   \x1b[36mhttp://localhost:\x1b[1m5173\x1b[22m/\x1b[39m"),
              "http://localhost:5173/");
    EXPECT_EQ(viteReadyUrl("  VITE v8.3.5  ready in 473 ms"), "");
    EXPECT_EQ(viteReadyUrl("  ➜  Network: use --host to expose"), "");
}

TEST_F(WebProjectTest, SvelteCheckCommands) {
    write("package.json", kKitPackage);
    auto web = WebProject::detect(dir_);
    EXPECT_TRUE(svelteCheckCommands(web, dir_, "node").empty()); // not installed yet

    bin("svelte-check", "svelte-check/bin/svelte-check");
    bin("svelte-kit", "@sveltejs/kit/svelte-kit.js");
    write("tsconfig.json", "{}");
    const auto cmds = svelteCheckCommands(web, dir_, "node");
    ASSERT_EQ(cmds.size(), 2u);
    EXPECT_EQ(cmds[0].argv, (std::vector<std::string>{"node", fs::canonical(dir_ / "node_modules/@sveltejs/kit/svelte-kit.js").string(), "sync"}));
    EXPECT_EQ(cmds[1].argv.back(), "./tsconfig.json");
    EXPECT_EQ(cmds[1].argv[2], "--output");
    EXPECT_EQ(cmds[1].cwd, dir_);

    // F9 on a SvelteKit project is the check; F12 is the production build.
    Project p;
    p.kind = ProjectKind::WebTs;
    BuildTools tools;
    EXPECT_EQ(buildCommands(p, dir_, BuildAction::Build, BuildConfig::Debug, tools).size(), 2u);
    const auto rebuild = buildCommands(p, dir_, BuildAction::Rebuild, BuildConfig::Debug, tools);
    ASSERT_EQ(rebuild.size(), 1u);
    EXPECT_EQ(rebuild[0].argv, (std::vector<std::string>{"npm", "run", "build"}));
    EXPECT_TRUE(buildCommands(p, dir_, BuildAction::Clean, BuildConfig::Debug, tools).empty());
}

TEST(WebProject, SvelteCheckMachineVerboseOutput) {
    DiagnosticParser parser("/home/me/kitapp");
    EXPECT_TRUE(parser.feed("1793440778355 START \"/home/me/kitapp\"").empty());
    auto ds = parser.feed(
        R"(1793440778357 {"type":"ERROR","filename":"src/routes/+page.svelte","start":{"line":1,"character":5},)"
        R"("end":{"line":1,"character":10},"message":"Type 'string' is not assignable to type 'number'.","code":2322,"source":"ts"})");
    ASSERT_EQ(ds.size(), 1u);
    EXPECT_EQ(ds[0].severity, Diagnostic::Severity::Error);
    EXPECT_EQ(ds[0].file, "/home/me/kitapp/src/routes/+page.svelte");
    EXPECT_EQ(ds[0].line, 2); // svelte-check counts from 0
    EXPECT_EQ(ds[0].column, 6);
    EXPECT_EQ(ds[0].message, "Type 'string' is not assignable to type 'number'. (ts2322)");
    ds = parser.feed(R"(1793440778358 {"type":"WARNING","filename":"src/lib/A.svelte","start":{"line":0,"character":0},"message":"Unused CSS selector","source":"css"})");
    ASSERT_EQ(ds.size(), 1u);
    EXPECT_EQ(ds[0].severity, Diagnostic::Severity::Warning);
    EXPECT_TRUE(parser.feed("1793440778357 COMPLETED 172 FILES 1 ERRORS 1 WARNINGS 2 FILES_WITH_PROBLEMS").empty());
    EXPECT_EQ(parser.errors(), 1);
    EXPECT_EQ(parser.warnings(), 1);
}

TEST(WebProject, FreePortSkipsOneInUse) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ASSERT_EQ(bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr), 0);
    ASSERT_EQ(listen(fd, 1), 0);
    socklen_t len = sizeof addr;
    getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    const int busy = ntohs(addr.sin_port);
    const int port = findFreePort(busy);
    EXPECT_NE(port, busy);
    EXPECT_GT(port, busy);
    close(fd);
}

TEST(WebProject, SvCreateIsPinnedAndNonInteractive) {
    const auto argv = svCreateCommand("my-app");
    EXPECT_EQ(argv[0], "npx");
    EXPECT_EQ(argv[1], "--yes");
    EXPECT_TRUE(argv[2].starts_with("sv@"));
    EXPECT_EQ(argv[4], "my-app");
    EXPECT_NE(std::find(argv.begin(), argv.end(), "--no-add-ons"), argv.end());
}
