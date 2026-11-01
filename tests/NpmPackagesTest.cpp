#include "core/NpmPackages.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace kevc;
using npm::Source;
using npm::Updates;

TEST(Npm, SpecsAndTheirRules) {
    auto rule = [](const char* s) { return npm::parseSpec(s).updates; };
    EXPECT_EQ(rule("5.1.0"), Updates::Locked);
    EXPECT_EQ(rule("=5.1.0"), Updates::Locked);
    EXPECT_EQ(rule("~5.1.0"), Updates::Fixes);
    EXPECT_EQ(rule("^5.1.0"), Updates::Features);
    EXPECT_EQ(rule("^1.0.0-beta.2"), Updates::Features);
    EXPECT_EQ(rule(">= 5.1.0"), Updates::Newer);
    EXPECT_EQ(rule("*"), Updates::Any);
    EXPECT_EQ(rule(""), Updates::Any);
    EXPECT_EQ(rule("1.2.x"), Updates::Custom);
    EXPECT_EQ(rule("^2 || ^3"), Updates::Custom);
    EXPECT_EQ(npm::parseSpec("^5.1.0").version, "5.1.0");
    EXPECT_EQ(npm::parseSpec(">=1.2.3 <2.0.0").version, "1.2.3"); // custom, but a starting point
    EXPECT_EQ(npm::parseSpec("latest").source, Source::Tag);
    EXPECT_EQ(npm::parseSpec("workspace:*").source, Source::Workspace);
    EXPECT_EQ(npm::parseSpec("file:../lib").source, Source::Local);
    EXPECT_EQ(npm::parseSpec("github:acme/tool#v2").source, Source::Git);
    EXPECT_EQ(npm::parseSpec("acme/tool").source, Source::Git);
    EXPECT_EQ(npm::parseSpec("git+https://example.com/x.git").source, Source::Git);
    EXPECT_EQ(npm::parseSpec("https://example.com/x.tgz").source, Source::Url);
    EXPECT_EQ(npm::parseSpec("npm:other@^1.0.0").source, Source::Alias);
}

TEST(Npm, MakeAndDescribe) {
    EXPECT_EQ(npm::makeSpec(Updates::Features, "5.1.0"), "^5.1.0");
    EXPECT_EQ(npm::makeSpec(Updates::Fixes, "5.1.0"), "~5.1.0");
    EXPECT_EQ(npm::makeSpec(Updates::Locked, "5.1.0"), "5.1.0");
    EXPECT_EQ(npm::makeSpec(Updates::Newer, "5.1.0"), ">=5.1.0");
    EXPECT_EQ(npm::makeSpec(Updates::Any, "5.1.0"), "*");
    auto words = [](const char* s) { return npm::describe(npm::parseSpec(s)); };
    EXPECT_EQ(words("^5.1.0"), "New features and fixes: 5.1.0 up to, not including, 6.0.0");
    EXPECT_EQ(words("^0.2.3"), "New features and fixes: 0.2.3 up to, not including, 0.3.0"); // 0.x: minor is major
    EXPECT_EQ(words("^0.0.3"), "New features and fixes: 0.0.3 up to, not including, 0.0.4");
    EXPECT_EQ(words("~5.1.4"), "Bug fixes only: 5.1.4 up to, not including, 5.2.0");
    EXPECT_EQ(words("5.1.0"), "Exactly 5.1.0: never changes when installing");
    EXPECT_NE(words(">=5.1.0").find("may break"), std::string::npos);
    EXPECT_NE(words("latest").find("latest"), std::string::npos);
    EXPECT_NE(words("1.2.x").find("1.2.x"), std::string::npos);
}

TEST(Npm, Versions) {
    EXPECT_TRUE(npm::isVersion("1.2.3"));
    EXPECT_TRUE(npm::isVersion("1.2.3-rc.1"));
    EXPECT_FALSE(npm::isVersion("1.2"));
    EXPECT_FALSE(npm::isVersion("1.2.3.4"));
    EXPECT_FALSE(npm::isVersion("1.2.3-"));
    EXPECT_LT(npm::compareVersions("1.9.0", "1.10.0"), 0);
    EXPECT_GT(npm::compareVersions("2.0.0", "1.99.99"), 0);
    EXPECT_EQ(npm::compareVersions("1.0.0", "1.0.0-beta"), 0);
}

namespace {

const char* kPackage = R"({
	"name": "site",
	"private": true,
	"scripts": { "dev": "vite dev" },
	"devDependencies": {
		"@sveltejs/kit": "^2.50.2",
		"vite": "^7.3.1"
	},
	"dependencies": {
		"zod": "^3.24.0"
	}
}
)";

nlohmann::json parse(const std::string& text) {
    return nlohmann::json::parse(text);
}

} // namespace

TEST(Npm, EditingPackageJsonKeepsItsLooks) {
    // Changing a version touches only that string.
    auto changed = npm::setDependency(kPackage, "devDependencies", "vite", "7.3.1");
    ASSERT_TRUE(changed);
    std::string expected = kPackage;
    expected.replace(expected.find("\"^7.3.1\""), 8, "\"7.3.1\"");
    EXPECT_EQ(*changed, expected);

    // A new package goes in alphabetical order, indented like its neighbours (tabs here).
    auto added = npm::setDependency(kPackage, "devDependencies", "svelte", "^5.0.0");
    ASSERT_TRUE(added);
    EXPECT_NE(added->find("\t\t\"@sveltejs/kit\": \"^2.50.2\",\n\t\t\"svelte\": \"^5.0.0\",\n\t\t\"vite\""), std::string::npos);
    added = npm::setDependency(*added, "dependencies", "zzz", "1.0.0");
    EXPECT_NE(added->find("\"zod\": \"^3.24.0\",\n\t\t\"zzz\": \"1.0.0\"\n\t}"), std::string::npos);
    EXPECT_EQ(parse(*added)["devDependencies"]["svelte"], "^5.0.0");

    // A section that doesn't exist yet goes at the end.
    auto section = npm::setDependency(kPackage, "optionalDependencies", "fsevents", "^2.3.0");
    ASSERT_TRUE(section);
    EXPECT_EQ(parse(*section)["optionalDependencies"]["fsevents"], "^2.3.0");
    EXPECT_NE(section->find("\t\"optionalDependencies\": {\n\t\t\"fsevents\": \"^2.3.0\"\n\t}\n}"), std::string::npos);
    EXPECT_EQ(parse(*npm::setDependency("{}", "dependencies", "a", "1.0.0"))["dependencies"]["a"], "1.0.0");
    EXPECT_EQ(parse(*npm::setDependency(R"({"dependencies": {}})", "dependencies", "a", "^1.0.0"))["dependencies"]["a"], "^1.0.0");

    // Removing: first, middle, last and only entries, each leaving valid JSON.
    auto removed = npm::removeDependency(kPackage, "devDependencies", "@sveltejs/kit");
    ASSERT_TRUE(removed);
    EXPECT_FALSE(parse(*removed)["devDependencies"].contains("@sveltejs/kit"));
    EXPECT_EQ(parse(*removed)["devDependencies"]["vite"], "^7.3.1");
    removed = npm::removeDependency(kPackage, "devDependencies", "vite");
    EXPECT_EQ(parse(*removed)["devDependencies"].size(), 1u);
    EXPECT_NE(removed->find("\"^2.50.2\"\n\t}"), std::string::npos); // no comma left behind
    removed = npm::removeDependency(kPackage, "dependencies", "zod");
    EXPECT_TRUE(parse(*removed)["dependencies"].empty());
    // Not there: unchanged. Not JSON: nothing.
    EXPECT_EQ(*npm::removeDependency(kPackage, "dependencies", "nope"), kPackage);
    EXPECT_FALSE(npm::setDependency("not json", "dependencies", "a", "1"));
    EXPECT_FALSE(npm::setDependency(R"({"dependencies": "oops"})", "dependencies", "a", "1"));
}

TEST(Npm, ReadsDependenciesAndWhatIsInstalled) {
    const fs::path dir = fs::temp_directory_path() / ("kevc-npm-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir / "node_modules/@sveltejs/kit");
    fs::create_directories(dir / "node_modules/vite");
    std::ofstream(dir / "package.json") << kPackage;
    std::ofstream(dir / "node_modules/@sveltejs/kit/package.json") << R"({"name": "@sveltejs/kit", "version": "2.50.2"})";
    std::ofstream(dir / "node_modules/vite/package.json") << R"({"name": "vite", "version": "7.3.1"})";
    const auto deps = npm::readDependencies(dir);
    ASSERT_EQ(deps.size(), 3u);
    EXPECT_EQ(deps[0].name, "zod"); // runtime first
    EXPECT_EQ(deps[0].installed, "");
    EXPECT_EQ(deps[1].name, "@sveltejs/kit");
    EXPECT_EQ(deps[1].section, "devDependencies");
    EXPECT_EQ(deps[1].installed, "2.50.2");
    EXPECT_EQ(deps[2].spec, "^7.3.1");
    fs::remove_all(dir);
}

TEST(Npm, RegistriesInNpmrc) {
    const std::string npmrc = "# company settings\n"
                              "registry = https://npm.acme.test/\n"
                              "@acme:registry=https://npm.acme.test/private/\n"
                              "//npm.acme.test/:_authToken=${NPM_TOKEN}\n"
                              "save-exact=true\n";
    const auto r = npm::readRegistries(npmrc);
    EXPECT_EQ(r.main, "https://npm.acme.test/");
    ASSERT_EQ(r.scoped.size(), 1u);
    EXPECT_EQ(r.scoped[0], (std::pair<std::string, std::string>{"@acme", "https://npm.acme.test/private/"}));
    // Unchanged registries: the file comes back the same (apart from the spacing we normalise).
    EXPECT_EQ(npm::readRegistries(npm::writeRegistries(npmrc, r)), r);
    // Back to the public registry, a scope added: other lines stay.
    const std::string out = npm::writeRegistries(npmrc, {"", {{"@acme", "https://npm.acme.test/private/"}, {"tools", "https://t.test/"}}});
    EXPECT_EQ(out, "# company settings\n"
                   "@acme:registry=https://npm.acme.test/private/\n"
                   "//npm.acme.test/:_authToken=${NPM_TOKEN}\n"
                   "save-exact=true\n"
                   "@tools:registry=https://t.test/\n");
    EXPECT_EQ(npm::writeRegistries("", {"https://r.test/", {}}), "registry=https://r.test/\n");
    EXPECT_EQ(npm::writeRegistries("registry=https://r.test/\n", {}), "");

    EXPECT_EQ(npm::checkRegistryUrl("https://npm.acme.test"), "");
    EXPECT_EQ(npm::checkRegistryUrl(""), "");
    EXPECT_NE(npm::checkRegistryUrl("npm.acme.test"), "");
    EXPECT_NE(npm::checkRegistryUrl("https://"), "");
    EXPECT_EQ(npm::normalRegistryUrl(" https://npm.acme.test "), "https://npm.acme.test/");
}
