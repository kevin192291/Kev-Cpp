#include "core/Dap.h"
#include "core/Lsp.h"

#include <gtest/gtest.h>

#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace kevc;
using json = nlohmann::json;

TEST(Dap, ServerReadyLine) {
    EXPECT_EQ(dap::serverPort("Debug server listening at 127.0.0.1:41117"), 41117);
    EXPECT_EQ(dap::serverPort("Debug server listening at ::1:8123"), 8123);
    EXPECT_EQ(dap::serverPort("Starting…"), 0);
}

TEST(Dap, MessagesUseLspFraming) {
    // A response and an event arriving in one read, the way js-debug sends them.
    lsp::MessageFramer framer;
    const json response = {{"seq", 3}, {"type", "response"}, {"request_seq", 1}, {"success", true}, {"command", "initialize"},
                           {"body", {{"supportsConfigurationDoneRequest", true}}}};
    const json event = {{"seq", 4}, {"type", "event"}, {"event", "initialized"}};
    framer.feed(lsp::frame(response) + lsp::frame(event));
    EXPECT_EQ(framer.next()->value("command", ""), "initialize");
    EXPECT_EQ(framer.next()->value("event", ""), "initialized");
    EXPECT_FALSE(framer.next());
}

TEST(Dap, LaunchConfigurations) {
    const json node = dap::nodeLaunch("/home/me/app", "/usr/bin/node", {"/home/me/app/node_modules/vite/bin/vite.js", "dev"});
    EXPECT_EQ(node["type"], "pwa-node");
    EXPECT_EQ(node["request"], "launch");
    EXPECT_EQ(node["runtimeExecutable"], "/usr/bin/node");
    EXPECT_EQ(node["runtimeArgs"][1], "dev");
    EXPECT_EQ(node["cwd"], "/home/me/app");
    EXPECT_TRUE(node["sourceMaps"].get<bool>());
    EXPECT_EQ(node["resolveSourceMapLocations"][0], "/home/me/app/**");

    const json chrome = dap::chromeLaunch("http://127.0.0.1:5174/", "/home/me/app", "/usr/bin/chromium", "/tmp/profile");
    EXPECT_EQ(chrome["type"], "pwa-chrome");
    EXPECT_EQ(chrome["url"], "http://127.0.0.1:5174/");
    EXPECT_EQ(chrome["webRoot"], "/home/me/app");
    EXPECT_EQ(chrome["userDataDir"], "/tmp/profile");

    const json init = dap::initializeArguments("pwa-node");
    EXPECT_TRUE(init["supportsStartDebuggingRequest"].get<bool>());
    EXPECT_TRUE(init["linesStartAt1"].get<bool>());
}

TEST(Dap, Breakpoints) {
    FileBreakpoints file;
    file[12] = {};
    file[4] = {};
    const json args = dap::setBreakpointsArguments("/app/src/routes/+page.server.ts", file);
    EXPECT_EQ(args["source"]["path"], "/app/src/routes/+page.server.ts");
    ASSERT_EQ(args["breakpoints"].size(), 2u);
    EXPECT_EQ(args["breakpoints"][0]["line"], 4); // sorted
    EXPECT_FALSE(args["breakpoints"][0].contains("condition"));
    // IF breakpoints are js-debug conditions; live ones logpoints carrying their id.
    file[20] = {BreakKind::Conditional, "count > 3", {}};
    file[30] = {BreakKind::Live, "", {"count"}};
    const json rich = dap::setBreakpointsArguments("/app/a.ts", file, {{30, 5}});
    EXPECT_EQ(rich["breakpoints"][2]["condition"], "count > 3");
    EXPECT_EQ(rich["breakpoints"][3]["logMessage"], "<<kevc-live:5>>{count}");
    // One naming nothing stays a plain breakpoint (JsDebugger reads the locals there).
    file[40] = {BreakKind::Live, "", {}};
    const json locals = dap::setBreakpointsArguments("/app/a.ts", file, {{30, 5}, {40, 6}});
    EXPECT_FALSE(locals["breakpoints"][4].contains("logMessage"));
    EXPECT_TRUE(dap::debuggableSource("a/+page.svelte"));
    EXPECT_TRUE(dap::debuggableSource("hooks.server.ts"));
    EXPECT_FALSE(dap::debuggableSource("main.cpp"));
    EXPECT_TRUE(dap::sessionWantsBreakpoints("pwa-node", "/app/src/routes/+page.server.ts"));
    EXPECT_FALSE(dap::sessionWantsBreakpoints("pwa-node", "/app/src/routes/+page.svelte"));
    EXPECT_TRUE(dap::sessionWantsBreakpoints("pwa-chrome", "/app/src/routes/+page.svelte"));
}

TEST(Dap, RepliesFromJsDebug) {
    // Recorded from js-debug: a SvelteKit load function paused, one node-internal frame.
    const json stack = json::parse(R"({"stackFrames":[
        {"id":7,"name":"load","line":4,"column":3,"source":{"name":"+page.server.ts","path":"/app/src/routes/blog/[slug]/+page.server.ts","sourceReference":0}},
        {"id":8,"name":"processTicksAndRejections","line":105,"column":5,"source":{"name":"<node_internals>/internal/process/task_queues","sourceReference":17}}],
        "totalFrames":2})");
    const auto frames = dap::framesOf(stack);
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[0].id, 7);
    EXPECT_EQ(frames[0].name, "load");
    EXPECT_EQ(frames[0].file, "/app/src/routes/blog/[slug]/+page.server.ts");
    EXPECT_EQ(frames[0].line, 4);
    EXPECT_TRUE(frames[1].file.empty());

    const auto scopes = dap::scopesOf(json::parse(R"({"scopes":[
        {"name":"Local","variablesReference":21,"expensive":false},{"name":"Global","variablesReference":22,"expensive":true}]})"));
    ASSERT_EQ(scopes.size(), 2u);
    EXPECT_EQ(scopes[0].reference, 21);
    EXPECT_TRUE(scopes[1].expensive);

    const auto vars = dap::variablesOf(json::parse(R"({"variables":[
        {"name":"params","value":"{slug: 'hello'}","type":"Object","variablesReference":31},
        {"name":"count","value":"3","type":"number","variablesReference":0}]})"));
    ASSERT_EQ(vars.size(), 2u);
    EXPECT_EQ(vars[0].name, "params");
    EXPECT_EQ(vars[0].reference, 31);
    EXPECT_EQ(vars[1].value, "3");
}

TEST(Dap, FindsAChromiumBrowser) {
    const fs::path dir = fs::temp_directory_path() / ("kevc-dap-" + std::to_string(::getpid()));
    fs::create_directories(dir);
    const fs::path brave = dir / "brave-browser";
    std::ofstream(brave) << "#!/bin/sh\n";
    fs::permissions(brave, fs::perms::owner_all);
    EXPECT_EQ(dap::findChrome("", dir.string()), brave);
    EXPECT_FALSE(dap::findChrome("", "/nonexistent"));
    EXPECT_EQ(dap::findChrome(brave.string(), ""), brave); // a full path from settings
    fs::remove_all(dir);
}
