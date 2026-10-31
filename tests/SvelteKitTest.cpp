#include "core/SvelteKit.h"

#include <gtest/gtest.h>

#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace kevc::kit;

namespace {

class SvelteKitTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() / ("kevc-kit-" + std::to_string(::getpid()) + "-" +
                                            ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(dir_);
        fs::create_directories(dir_);
    }
    void TearDown() override { fs::remove_all(dir_); }

    void write(const fs::path& rel, const std::string& text) {
        fs::create_directories((dir_ / rel).parent_path());
        std::ofstream(dir_ / rel) << text;
    }

    // A small app with groups, params, a layout reset, server and universal loads, actions,
    // an endpoint and remote functions.
    void writeApp() {
        write("src/routes/+layout.svelte", "<slot />\n");
        write("src/routes/+layout.server.ts",
              "export const load = async ({ locals }) => {\n  return { session: locals.user, theme: 'dark' };\n};\n");
        write("src/routes/+page.svelte", "<h1>Home</h1>\n");
        write("src/routes/(app)/+layout.svelte", "<nav />\n");
        write("src/routes/(app)/+layout.ts", "export function load() {\n  return { nav: [] };\n}\n");
        write("src/routes/(app)/blog/[slug]/+page.svelte", "<script lang=\"ts\">let { data } = $props();</script>\n");
        write("src/routes/(app)/blog/[slug]/+page.server.ts", R"(import type { PageServerLoad, Actions } from './$types';

export const load: PageServerLoad = async ({ params }) => {
	const post = await getPost(params.slug);
	if (!post) return { missing: true };
	// a nested function's return is not the page's data
	const shape = (c) => { return { id: c.id }; };
	return { post, comments: post.comments.map(shape), 'quoted-key': 1 };
};

export const actions = {
	default: async ({ request }) => {
		return { ok: true };
	},
	async like(event) {},
	'un-like': async () => {},
} satisfies Actions;

export const prerender = false;
)");
        write("src/routes/(app)/docs/[[lang]]/[...path]/+page@.svelte", "<p>docs</p>\n");
        write("src/routes/api/items/[id=integer]/+server.ts",
              "export async function GET({ params }) {\n  return new Response('x');\n}\nexport const POST = async () => new Response();\n");
        write("src/lib/data.remote.ts",
              "import { query, form } from '$app/server';\n\nexport const getPosts = query(async () => []);\nexport const addPost = form(async (data) => {});\n");
    }

    fs::path dir_;
};

} // namespace

TEST(SvelteKitScanners, Params) {
    EXPECT_EQ(paramOf("[slug]"), "slug");
    EXPECT_EQ(paramOf("[[lang]]"), "lang?");
    EXPECT_EQ(paramOf("[...rest]"), "...rest");
    EXPECT_EQ(paramOf("[id=integer]"), "id");
    EXPECT_EQ(paramOf("(app)"), "");
    EXPECT_EQ(paramOf("blog"), "");
}

TEST(SvelteKitScanners, Exports) {
    const auto e = exportsOf("import x from 'y';\nexport async function GET() {}\nexport const POST = 1;\n"
                             "export { handler as PUT, other };\nexport let ssr = false;\n");
    ASSERT_EQ(e.size(), 5u);
    EXPECT_EQ(e[0].name, "GET");
    EXPECT_EQ(e[0].line, 2);
    EXPECT_EQ(e[1].name, "POST");
    EXPECT_EQ(e[2].name, "PUT");
    EXPECT_EQ(e[3].name, "other");
    EXPECT_EQ(e[4].name, "ssr");
}

TEST(SvelteKitScanners, LoadReturnShapes) {
    EXPECT_EQ(returnedKeys("export const load = () => ({ user: { name: 'Ada' }, count: 3 });", "load"),
              (std::vector<std::string>{"user", "count"}));
    EXPECT_EQ(returnedKeys("export function load({ params }) { const a = { x: 1 }; return { a, b: [1, {c: 2}] }; }", "load"),
              (std::vector<std::string>{"a", "b"}));
    EXPECT_EQ(returnedKeys("export const load = async function (e) { return { only: 1 }; };", "load"),
              (std::vector<std::string>{"only"}));
    EXPECT_EQ(returnedKeys("export const load = async event => { return { s: `}{`, t: '}' }; };", "load"),
              (std::vector<std::string>{"s", "t"}));
    EXPECT_TRUE(returnedKeys("export const load = () => fetchAll();", "load").empty());
    EXPECT_TRUE(returnedKeys("const other = 1;", "load").empty());
}

TEST_F(SvelteKitTest, RoutesFromTheFileSystem) {
    writeApp();
    const App app = scan(dir_);
    std::vector<std::string> ids;
    for (const auto& r : app.routes) ids.push_back(r.id);
    EXPECT_EQ(ids, (std::vector<std::string>{"/", "/(app)", "/(app)/blog/[slug]", "/(app)/docs/[[lang]]/[...path]",
                                             "/api/items/[id=integer]"}));

    const Route* blog = app.find("/(app)/blog/[slug]");
    ASSERT_TRUE(blog);
    EXPECT_EQ(blog->url, "/blog/[slug]");
    EXPECT_EQ(blog->params, std::vector<std::string>{"slug"});
    const RouteFile* server = blog->file("page", true);
    ASSERT_TRUE(server);
    EXPECT_EQ(server->name, "+page.server.ts");
    EXPECT_EQ(server->lineOf("load"), 3);
    EXPECT_GT(server->lineOf("prerender"), 0);
    EXPECT_EQ(server->loadKeys, (std::vector<std::string>{"missing", "post", "comments", "quoted-key"}));
    EXPECT_EQ(server->actions, (std::vector<std::string>{"default", "like", "un-like"}));

    const Route* docs = app.find("/(app)/docs/[[lang]]/[...path]");
    ASSERT_TRUE(docs);
    EXPECT_EQ(docs->url, "/docs/[[lang]]/[...path]");
    EXPECT_EQ(docs->params, (std::vector<std::string>{"lang?", "...path"}));
    EXPECT_EQ(docs->files.front().layoutReset, "/");

    const Route* api = app.find("/api/items/[id=integer]");
    ASSERT_TRUE(api);
    EXPECT_EQ(api->params, std::vector<std::string>{"id"});
    EXPECT_FALSE(api->hasPage());
    const RouteFile* endpoint = api->file("server", true);
    ASSERT_TRUE(endpoint);
    EXPECT_EQ(endpoint->lineOf("GET"), 1);
    EXPECT_EQ(endpoint->lineOf("POST"), 4);

    ASSERT_EQ(app.remote.size(), 2u);
    EXPECT_EQ(app.remote[0].name, "getPosts");
    EXPECT_EQ(app.remote[0].kind, "query");
    EXPECT_EQ(app.remote[0].line, 3);
    EXPECT_EQ(app.remote[1].kind, "form");
}

TEST_F(SvelteKitTest, LayoutsAndDataFlow) {
    writeApp();
    const App app = scan(dir_);
    const Route& blog = *app.find("/(app)/blog/[slug]");
    const auto layouts = layoutChain(app, blog);
    ASSERT_EQ(layouts.size(), 2u);
    EXPECT_EQ(layouts[0]->path, dir_ / "src/routes/+layout.svelte");
    EXPECT_EQ(layouts[1]->path, dir_ / "src/routes/(app)/+layout.svelte");

    const auto flow = dataFlow(app, blog);
    ASSERT_EQ(flow.size(), 3u);
    EXPECT_EQ(flow[0].file->name, "+layout.server.ts");
    EXPECT_EQ(flow[0].keys, (std::vector<std::string>{"session", "theme"}));
    EXPECT_EQ(flow[1].keys, std::vector<std::string>{"nav"});
    EXPECT_EQ(flow[2].file->name, "+page.server.ts");

    // +page@.svelte renders in the root layout only.
    const Route& docs = *app.find("/(app)/docs/[[lang]]/[...path]");
    const auto docsLayouts = layoutChain(app, docs);
    ASSERT_EQ(docsLayouts.size(), 1u);
    EXPECT_EQ(docsLayouts[0]->path, dir_ / "src/routes/+layout.svelte");
}

TEST_F(SvelteKitTest, NoRoutesFolder) {
    const App app = scan(dir_);
    EXPECT_TRUE(app.routes.empty());
    EXPECT_TRUE(app.remote.empty());
}

TEST(SvelteKitScanners, NewRoutes) {
    EXPECT_EQ(routeFolder("/blog/[slug]/"), "blog/[slug]");
    EXPECT_EQ(routeFolder("(app)/settings"), "(app)/settings");
    EXPECT_EQ(routeFolder("/"), "");
    EXPECT_FALSE(routeFolder("../outside"));
    EXPECT_FALSE(routeFolder("a/../../b"));
    const auto files = newRouteFiles("(app)/blog/[slug]");
    ASSERT_EQ(files.size(), 2u);
    EXPECT_EQ(files[0].first, "+page.svelte");
    EXPECT_NE(files[0].second.find("<h1>/blog/[slug]</h1>"), std::string::npos); // groups aren't in the URL
    EXPECT_NE(files[1].second.find("export const load"), std::string::npos);
    EXPECT_EQ(fillParams("/blog/hello"), "/blog/hello");
    EXPECT_EQ(fillParams("/blog/[slug]"), "");
}

TEST(SvelteKitEnv, DotEnvSyntax) {
    const auto vars = parseDotEnv("# comment\nAPI_KEY=abc123\nexport PUBLIC_URL=\"https://x.dev/#a\"\n"
                                  "EMPTY=\nSPACED = value # trailing\nnot valid\nSINGLE='a b'\n", "/p/.env");
    ASSERT_EQ(vars.size(), 5u);
    EXPECT_EQ(vars[0].name, "API_KEY");
    EXPECT_EQ(vars[0].value, "abc123");
    EXPECT_EQ(vars[0].line, 2);
    EXPECT_EQ(vars[1].value, "https://x.dev/#a");
    EXPECT_EQ(vars[2].value, "");
    EXPECT_EQ(vars[3].name, "SPACED");
    EXPECT_EQ(vars[3].value, "value");
    EXPECT_EQ(vars[4].value, "a b");
}

TEST(SvelteKitEnv, WhereCodeRuns) {
    EXPECT_TRUE(isClientSide("routes/+page.svelte"));
    EXPECT_TRUE(isClientSide("routes/+page.ts"));
    EXPECT_TRUE(isClientSide("lib/api.ts"));
    EXPECT_FALSE(isClientSide("routes/+page.server.ts"));
    EXPECT_FALSE(isClientSide("routes/api/+server.ts"));
    EXPECT_FALSE(isClientSide("hooks.server.ts"));
    EXPECT_FALSE(isClientSide("lib/server/db.ts"));
}

TEST_F(SvelteKitTest, EnvReport) {
    write(".env", "DATABASE_URL=postgres://x\nPUBLIC_API=https://api\nUNUSED_TOKEN=1\n");
    write(".env.local", "DATABASE_URL=postgres://local\n");
    write("src/routes/+page.server.ts", "import { DATABASE_URL, STRIPE_KEY } from '$env/static/private';\n");
    write("src/routes/+page.svelte", "<script>\nimport { DATABASE_URL } from '$env/static/private';\nimport { PUBLIC_API } from '$env/static/public';\n</script>\n");
    write("src/lib/config.ts", "import { env } from '$env/dynamic/public';\nexport const base = env.PUBLIC_API + env.SECRET;\n");

    const EnvReport report = envReport(dir_);
    std::map<std::string, const EnvRow*> rows;
    for (const auto& r : report.rows) rows[r.name] = &r;
    ASSERT_TRUE(rows.count("DATABASE_URL"));
    EXPECT_EQ(rows["DATABASE_URL"]->definitions.size(), 2u);
    EXPECT_EQ(rows["DATABASE_URL"]->definitions.back().value, "postgres://local"); // .env.local wins
    EXPECT_TRUE(rows["PUBLIC_API"]->isPublic);
    EXPECT_EQ(rows["PUBLIC_API"]->uses.size(), 2u);

    ASSERT_GE(report.problems.size(), 4u);
    // Most serious first: a private variable in browser code.
    EXPECT_EQ(report.problems[0].kind, EnvProblem::Kind::PrivateInClient);
    EXPECT_EQ(report.problems[0].name, "DATABASE_URL");
    EXPECT_EQ(report.problems[0].file, dir_ / "src/routes/+page.svelte");
    EXPECT_EQ(report.problems[0].line, 2);
    auto has = [&](EnvProblem::Kind kind, const std::string& name) {
        return std::any_of(report.problems.begin(), report.problems.end(),
                           [&](const EnvProblem& p) { return p.kind == kind && p.name == name; });
    };
    EXPECT_TRUE(has(EnvProblem::Kind::Undefined, "STRIPE_KEY"));
    EXPECT_TRUE(has(EnvProblem::Kind::PublicPrefix, "SECRET"));
    EXPECT_TRUE(has(EnvProblem::Kind::Unused, "UNUSED_TOKEN"));
    EXPECT_FALSE(has(EnvProblem::Kind::Undefined, "SECRET")); // dynamic: may come from the environment
}
