#include "core/ManagedBlock.h"

#include <gtest/gtest.h>

namespace mb = kevc::managed_block;

namespace {

const std::string kBegin = std::string(mb::kBeginMarker) + mb::kBeginNotice + "\n";
const std::string kEnd = std::string(mb::kEndMarker) + "\n";

} // namespace

TEST(ManagedBlock, ExtractReturnsNulloptWithoutMarkers) {
    EXPECT_FALSE(mb::extract("project(x)\n"));
}

TEST(ManagedBlock, ExtractReturnsBody) {
    auto body = mb::extract("a\n" + kBegin + "one\ntwo\n" + kEnd + "b\n");
    ASSERT_TRUE(body);
    EXPECT_EQ(*body, "one\ntwo\n");
}

TEST(ManagedBlock, SplicePreservesUserTextByteForByte) {
    const std::string before = "cmake_minimum_required(VERSION 3.20)\r\n  # odd   spacing\t\n\n";
    const std::string after = "\n# user stuff\nadd_subdirectory(tests)   \n# no trailing newline";
    const std::string text = before + kBegin + "old body\n" + kEnd + after;

    const std::string out = mb::splice(text, "new body\nline 2\n");
    EXPECT_EQ(out, before + kBegin + "new body\nline 2\n" + kEnd + after);
}

TEST(ManagedBlock, SpliceIsIdempotent) {
    const std::string text = "x\n" + kBegin + "body\n" + kEnd + "y\n";
    EXPECT_EQ(mb::splice(text, "body\n"), text);
}

TEST(ManagedBlock, SpliceAppendsWhenMissing) {
    EXPECT_EQ(mb::splice("project(x)", "b\n"), "project(x)\n\n" + kBegin + "b\n" + kEnd);
    EXPECT_EQ(mb::splice("", "b\n"), kBegin + "b\n" + kEnd);
}

TEST(ManagedBlock, SpliceKeepsMissingFinalNewline) {
    const std::string text = "x\n" + kBegin + "old\n" + std::string(mb::kEndMarker);
    EXPECT_EQ(mb::splice(text, "new\n"), "x\n" + kBegin + "new\n" + std::string(mb::kEndMarker));
}

TEST(ManagedBlock, MarkersMustStartALine) {
    // A marker quoted mid-line (e.g. in a message()) is not a block boundary.
    const std::string text = "message(\"" + std::string(mb::kBeginMarker) + "\")\n";
    EXPECT_FALSE(mb::extract(text));
}

TEST(ManagedBlock, RejectsBrokenMarkers) {
    EXPECT_THROW(mb::extract(kBegin + "body\n"), mb::MalformedBlock);
    EXPECT_THROW(mb::extract("body\n" + kEnd), mb::MalformedBlock);
    EXPECT_THROW(mb::extract(kEnd + kBegin), mb::MalformedBlock);
    EXPECT_THROW(mb::splice(kBegin + "a\n" + kEnd + kBegin + "b\n" + kEnd, "c\n"), mb::MalformedBlock);
}
