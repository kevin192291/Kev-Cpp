#include "core/Version.h"

#include <gtest/gtest.h>

using namespace kevc;

TEST(Version, TextFromCommitTime) {
    // 2026-10-31 19:59:00 UTC
    EXPECT_EQ(versionText(1793476740, "3fa834c"), "2026.10.31.1959-3fa834c");
}

TEST(Version, ParseTagsAndBuildVersions) {
    auto v = parseVersion("v2026.10.31.1959-3fa834c");
    ASSERT_TRUE(v);
    EXPECT_EQ(v->time, 1793476740);
    EXPECT_EQ(v->commit, "3fa834c");
    EXPECT_EQ(v->text, "2026.10.31.1959-3fa834c");
    auto dirty = parseVersion("2026.10.31.1959-3fa834c+changes");
    ASSERT_TRUE(dirty);
    EXPECT_EQ(dirty->time, v->time);
    EXPECT_FALSE(parseVersion("0.1.0"));
    EXPECT_FALSE(parseVersion("v2026.10.31-3fa834c"));
    EXPECT_FALSE(parseVersion("unknown"));
}

TEST(Version, Comparison) {
    const Version older = *parseVersion("2026.10.30.0800-aaaaaaa");
    const Version newer = *parseVersion("2026.10.31.1959-3fa834c");
    EXPECT_EQ(compareVersions(older, newer), Freshness::UpdateAvailable);
    EXPECT_EQ(compareVersions(newer, newer), Freshness::UpToDate);
    EXPECT_EQ(compareVersions(newer, older), Freshness::AheadOfRelease);
    EXPECT_EQ(compareVersions(Version{}, older), Freshness::UpdateAvailable); // built outside git
    // A build knows its commit to the second; the release tag only to the minute.
    const Version build{newer.time + 42, "3fa834c", "2026.10.31.1959-3fa834c"};
    EXPECT_EQ(compareVersions(build, newer), Freshness::UpToDate);
    const Version other{newer.time + 42, "bbbbbbb", "2026.10.31.1959-bbbbbbb"};
    EXPECT_EQ(compareVersions(other, newer), Freshness::UpToDate); // same minute: can't tell
}

TEST(Version, LatestReleaseFromGitHub) {
    // Abridged from GET /repos/{owner}/{repo}/releases/latest
    const std::string reply = R"({"tag_name":"v2026.10.31.1959-3fa834c","name":"Kev-C++ 2026.10.31.1959-3fa834c",
        "html_url":"https://github.com/kevin192291/Kev-Cpp/releases/tag/v2026.10.31.1959-3fa834c","body":"Add the roadmap",
        "assets":[{"name":"checksums.txt","browser_download_url":"https://x/checksums.txt","size":90},
                  {"name":"kev-cpp-2026.10.31.1959-3fa834c-linux-x86_64.tar.gz",
                   "browser_download_url":"https://github.com/kevin192291/Kev-Cpp/releases/download/v2026.10.31.1959-3fa834c/kev-cpp-2026.10.31.1959-3fa834c-linux-x86_64.tar.gz",
                   "size":31457280}]})";
    auto r = parseLatestRelease(reply);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->version.commit, "3fa834c");
    EXPECT_EQ(r->notes, "Add the roadmap");
    EXPECT_EQ(r->assetName, "kev-cpp-2026.10.31.1959-3fa834c-linux-x86_64.tar.gz");
    EXPECT_EQ(r->assetSize, 31457280);
    EXPECT_FALSE(parseLatestRelease(R"({"message":"Not Found"})")); // no releases yet
    EXPECT_FALSE(parseLatestRelease(R"({"tag_name":"v2026.10.31.1959-3fa834c","assets":[]})")); // no package
    EXPECT_FALSE(parseLatestRelease("<html>"));
    EXPECT_EQ(latestReleaseUrl(), "https://api.github.com/repos/kevin192291/Kev-Cpp/releases/latest");
}

TEST(Version, InstallPrefixAndUpdateCommand) {
    EXPECT_EQ(installPrefix("/home/me/.local/bin/kev-cpp"), std::filesystem::path("/home/me/.local"));
    EXPECT_FALSE(installPrefix("/home/me/src/Kev-Cpp/build/kev-cpp"));

    Release r;
    r.version = *parseVersion("2026.10.31.1959-3fa834c");
    r.assetName = "kev-cpp-2026.10.31.1959-3fa834c-linux-x86_64.tar.gz";
    r.assetUrl = "https://example.invalid/k.tar.gz";
    const auto argv = updateCommand(r, "/home/me/.cache/kev-cpp/updates", "/home/me/.local");
    ASSERT_EQ(argv.size(), 8u);
    EXPECT_EQ(argv[0], "sh");
    EXPECT_NE(argv[2].find("install.sh\" --prefix \"$prefix\""), std::string::npos);
    EXPECT_EQ(argv[4], "https://example.invalid/k.tar.gz");
    EXPECT_EQ(argv[5], "/home/me/.cache/kev-cpp/updates/kev-cpp-2026.10.31.1959-3fa834c-linux-x86_64.tar.gz");
    EXPECT_EQ(argv[6], "/home/me/.cache/kev-cpp/updates/kev-cpp-2026.10.31.1959-3fa834c");
    EXPECT_EQ(argv[7], "/home/me/.local");
}
