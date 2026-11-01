#include "core/Mi.h"

#include <gtest/gtest.h>

using namespace kevc::mi;
using T = Record::Type;

TEST(MiParse, ResultWithTokenAndNestedValues) {
    auto r = parseLine(R"(12^done,bkpt={number="1",type="breakpoint",file="main.cpp",line="7",thread-groups=["i1"]})");
    EXPECT_EQ(r.type, T::Result);
    ASSERT_TRUE(r.token);
    EXPECT_EQ(*r.token, 12);
    EXPECT_EQ(r.resultClass, "done");
    EXPECT_EQ(r.results["bkpt"]["number"], "1");
    EXPECT_EQ(r.results["bkpt"]["line"], "7");
    EXPECT_EQ(r.results["bkpt"]["thread-groups"], json::array({"i1"}));
}

TEST(MiParse, StoppedRecord) {
    auto r = parseLine(R"(*stopped,reason="breakpoint-hit",disp="keep",bkptno="1",frame={addr="0x0000555555555189",func="main",args=[],file="src/main.cpp",fullname="/p/src/main.cpp",line="7",arch="i386:x86-64"},thread-id="1",stopped-threads="all",core="3")");
    EXPECT_EQ(r.type, T::Exec);
    EXPECT_FALSE(r.token);
    EXPECT_EQ(r.resultClass, "stopped");
    EXPECT_EQ(r.results["reason"], "breakpoint-hit");
    EXPECT_EQ(r.results["frame"]["fullname"], "/p/src/main.cpp");
    EXPECT_EQ(r.results["frame"]["args"], json::array());
}

TEST(MiParse, ListOfResultsBecomesArray) {
    auto r = parseLine(R"(5^done,stack=[frame={level="0",func="f"},frame={level="1",func="main"}])");
    ASSERT_EQ(r.results["stack"].size(), 2u);
    EXPECT_EQ(r.results["stack"][1]["func"], "main");
    auto v = parseLine(R"(6^done,variables=[{name="p",type="Point *",value="0x5555"},{name="n",type="int",value="3"}])");
    EXPECT_EQ(v.results["variables"][0]["type"], "Point *");
}

TEST(MiParse, StreamsAndEscapes) {
    auto c = parseLine(R"(~"Breakpoint 1 at 0x1189: file main.cpp, line 7.\n")");
    EXPECT_EQ(c.type, T::Console);
    EXPECT_EQ(c.text, "Breakpoint 1 at 0x1189: file main.cpp, line 7.\n");
    auto v = parseLine(R"(3^done,value="0x555555 \"hi\\\\there\" \303\251")");
    // gdb shows the value with C escapes, then MI escapes that again: one decode leaves "\\".
    EXPECT_EQ(v.results["value"], "0x555555 \"hi\\\\there\" \xC3\xA9");
    EXPECT_EQ(parseLine(R"(&"warning: x\n")").type, T::Log);
    EXPECT_EQ(parseLine(R"(@"out")").type, T::Target);
}

TEST(MiParse, ErrorsNotifyPromptAndGarbage) {
    auto e = parseLine(R"(9^error,msg="No symbol \"zz\" in current context.")");
    EXPECT_EQ(e.resultClass, "error");
    EXPECT_EQ(e.results["msg"], "No symbol \"zz\" in current context.");
    EXPECT_EQ(parseLine("^running").resultClass, "running");
    EXPECT_EQ(parseLine("=thread-group-added,id=\"i1\"").type, T::Notify);
    EXPECT_EQ(parseLine("(gdb) ").type, T::Prompt);
    auto u = parseLine("Reading symbols from a.out...");
    EXPECT_EQ(u.type, T::Unknown);
    EXPECT_EQ(u.text, "Reading symbols from a.out...");
    EXPECT_EQ(parseLine("^done,x={unterminated").type, T::Unknown);
}

// gdb writes a breakpoint's commands as a brace list of bare strings.
TEST(MiParse, BreakpointScriptIsAList) {
    auto r = parseLine(R"mi(=breakpoint-modified,bkpt={number="1",type="dprintf",script={"printf \"<<kevc-live:1>>%s\\n\",$_kevc_value(\"i\")"},times="2"})mi");
    EXPECT_EQ(r.type, T::Notify);
    EXPECT_EQ(r.results["bkpt"]["times"], "2");
    ASSERT_TRUE(r.results["bkpt"]["script"].is_array());
    EXPECT_EQ(r.results["bkpt"]["script"].size(), 1u);
}

TEST(MiQuote, Escapes) {
    EXPECT_EQ(quote("/a b/c\"d\\e"), R"("/a b/c\"d\\e")");
}

TEST(MiPointers, DataPointers) {
    EXPECT_TRUE(isDataPointer("Point *"));
    EXPECT_TRUE(isDataPointer("const Node *"));
    EXPECT_TRUE(isDataPointer("int * const"));
    EXPECT_TRUE(isDataPointer("char **"));
    EXPECT_FALSE(isDataPointer("char *"));
    EXPECT_FALSE(isDataPointer("const char *"));
    EXPECT_FALSE(isDataPointer("void *"));
    EXPECT_FALSE(isDataPointer("int (*)(int)"));
    EXPECT_FALSE(isDataPointer("int"));
    EXPECT_FALSE(isDataPointer("std::vector<int>"));
    EXPECT_TRUE(isNullPointer("0x0"));
    EXPECT_TRUE(isNullPointer("0x0000000000000000"));
    EXPECT_FALSE(isNullPointer("0x5555"));
    EXPECT_FALSE(isNullPointer("0"));
}

TEST(MiExpression, MemberChainsAndSubscripts) {
    const std::string line = "    total += p->pos.x * scale + items[i].weight;";
    auto at = [&](std::string_view word, int offset = 0) {
        return expressionAt(line, static_cast<int>(line.find(word)) + offset);
    };
    EXPECT_EQ(at("total"), "total");
    EXPECT_EQ(at("p->"), "p");
    EXPECT_EQ(at("pos"), "p->pos");
    EXPECT_EQ(at("x *"), "p->pos.x");
    EXPECT_EQ(at("scale", 2), "scale");
    EXPECT_EQ(at("weight"), "items[i].weight");
    EXPECT_EQ(at("items"), "items");
    EXPECT_EQ(at("+="), "");
}

TEST(MiExpression, UnaryOperatorsAndScopes) {
    EXPECT_EQ(expressionAt("return *head;", 9), "*head");
    EXPECT_EQ(expressionAt("int y = a * b;", 12), "b"); // multiplication, not a dereference
    EXPECT_EQ(expressionAt("use(&value);", 6), "&value");
    EXPECT_EQ(expressionAt("if (a && b)", 9), "b");
    EXPECT_EQ(expressionAt("x = ns::Config::limit;", 17), "ns::Config::limit");
    EXPECT_EQ(expressionAt("f().member", 5), "member");
    EXPECT_EQ(expressionAt("count", 5), "count"); // just past the end
    EXPECT_EQ(expressionAt("n = 42;", 4), "");    // number literal
    EXPECT_EQ(expressionAt("grid[r][c].alive", 12), "grid[r][c].alive");
}
