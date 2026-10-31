#include "core/Lsp.h"

#include <gtest/gtest.h>

using namespace kevc::lsp;

TEST(LspFraming, RoundTripsAndHandlesPartialInput) {
    const json a = {{"jsonrpc", "2.0"}, {"id", 1}, {"result", {{"text", "héllo"}}}};
    const json b = {{"jsonrpc", "2.0"}, {"method", "initialized"}, {"params", json::object()}};
    const std::string stream = frame(a) + frame(b);

    MessageFramer framer;
    // Feed one byte at a time: nothing is produced until a whole message has arrived.
    std::vector<json> got;
    for (char c : stream) {
        framer.feed(std::string_view(&c, 1));
        while (auto m = framer.next()) got.push_back(*m);
    }
    ASSERT_EQ(got.size(), 2u);
    EXPECT_EQ(got[0], a);
    EXPECT_EQ(got[1], b);
}

TEST(LspFraming, ContentLengthCountsBytesAndIgnoresOtherHeaders) {
    const std::string body = R"({"x":"日本"})";
    MessageFramer framer;
    framer.feed("Content-Type: application/vscode-jsonrpc; charset=utf-8\r\ncontent-length: " +
                std::to_string(body.size()) + "\r\n\r\n" + body);
    auto m = framer.next();
    ASSERT_TRUE(m);
    EXPECT_EQ((*m)["x"], "日本");
    EXPECT_FALSE(framer.next());
}

TEST(LspFraming, MissingLengthThrows) {
    MessageFramer framer;
    framer.feed("X: 1\r\n\r\n{}");
    EXPECT_THROW(framer.next(), std::runtime_error);
}

TEST(LspUri, RoundTripsUnusualPaths) {
    const std::filesystem::path p = "/tmp/My Project/src/naïve#1.cpp";
    const std::string uri = pathToUri(p);
    EXPECT_EQ(uri, "file:///tmp/My%20Project/src/na%C3%AFve%231.cpp");
    EXPECT_EQ(uriToPath(uri), p);
    EXPECT_EQ(uriToPath("file://localhost/a/b"), std::filesystem::path("/a/b"));
    EXPECT_FALSE(uriToPath("file://server/share/x"));
    EXPECT_FALSE(uriToPath("https://example.com/x"));
}

TEST(LspPositions, Utf16AndCharColumns) {
    // "a" (1 unit) + "é" (1 unit, 2 bytes) + "😀" (2 units, 4 bytes) + "b"
    const std::string line = "a\xC3\xA9\xF0\x9F\x98\x80" "b";
    EXPECT_EQ(charsToUtf16(line, 0), 0);
    EXPECT_EQ(charsToUtf16(line, 2), 2);
    EXPECT_EQ(charsToUtf16(line, 3), 4); // after the emoji
    EXPECT_EQ(charsToUtf16(line, 4), 5);
    EXPECT_EQ(utf16ToChars(line, 4), 3);
    EXPECT_EQ(utf16ToChars(line, 5), 4);
    EXPECT_EQ(utf16ToChars(line, 7), 6); // past the end is kept
    for (int c = 0; c <= 4; ++c) EXPECT_EQ(utf16ToChars(line, charsToUtf16(line, c)), c);
}

TEST(LspMarkdown, RendersCodeBoldAndEscapes) {
    EXPECT_EQ(markdownToPango("### function `max`\n\n---\n→ `int`\nUses **a < b** & more\n\n```cpp\nint max(int a, int b)\n```"),
              "<b>function <tt>max</tt></b>\n\n→ <tt>int</tt>\nUses <b>a &lt; b</b> &amp; more\n\n"
              "<tt>int max(int a, int b)</tt>");
    EXPECT_EQ(markdownToPango("std\\_vector \\*x"), "std_vector *x");
    EXPECT_EQ(markdownToPango("unclosed `tick and **bold"), "unclosed `tick and <b>bold</b>");
}

TEST(LspLanguages, ServerAndLanguageIds) {
    EXPECT_EQ(serverFor("a/main.cpp"), Server::Clangd);
    EXPECT_EQ(serverFor("x.H"), Server::Clangd);
    EXPECT_EQ(serverFor("app.tsx"), Server::TypeScript);
    EXPECT_EQ(serverFor("index.html"), Server::None);
    EXPECT_EQ(languageIdFor("x.h", true), "cpp");
    EXPECT_EQ(languageIdFor("x.h", false), "c");
    EXPECT_EQ(languageIdFor("x.hpp", false), "cpp");
    EXPECT_EQ(languageIdFor("m.mjs", false), "javascript");
    EXPECT_EQ(serverFor("src/routes/+page.svelte"), Server::Svelte);
    EXPECT_EQ(languageIdFor("src/lib/Counter.svelte", false), "svelte");
}

TEST(LspLanguages, SvelteConfiguration) {
    const json init = svelteInitializationOptions();
    EXPECT_TRUE(init["configuration"]["svelte"]["plugin"]["typescript"]["enable"].get<bool>());
    EXPECT_TRUE(init["dontFilterIncompleteCompletions"].get<bool>());
    // workspace/configuration: objects (never null) for the sections Svelte's server asks about.
    EXPECT_TRUE(configurationFor(Server::Svelte, "svelte")["plugin"].is_object());
    EXPECT_TRUE(configurationFor(Server::Svelte, "svelte.plugin.css")["enable"].get<bool>());
    EXPECT_TRUE(configurationFor(Server::Svelte, "prettier").is_object());
    EXPECT_TRUE(configurationFor(Server::Svelte, "unknown.section").is_object());
    EXPECT_TRUE(configurationFor(Server::TypeScript, "typescript").is_null()); // unchanged for the others
}
