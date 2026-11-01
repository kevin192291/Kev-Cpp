#include "core/CppStyle.h"

#include <gtest/gtest.h>

using namespace kevc;
namespace cs = kevc::cppstyle;

TEST(CppStyle, OpenBracketsSkipCommentsAndStrings) {
    const auto open = cs::openBrackets("namespace a\n{\n    void f(\"{\", '{') // {\n    {\n        /* { */ int x[] = {1, 2};\n");
    ASSERT_EQ(open.size(), 2u);
    EXPECT_EQ(open[0].bracket, '{');
    EXPECT_EQ(open[0].lineIndent, "");
    EXPECT_EQ(open[1].lineIndent, "    ");
    EXPECT_TRUE(cs::openBrackets("auto s = R\"x({ ( [)x\";\nint n = 1'000;\n").empty());
}

TEST(CppStyle, NewLineIndent) {
    const std::string unit = "    ";
    EXPECT_EQ(cs::newLineIndent("int main()\n{\n", "", unit), "    ");
    EXPECT_EQ(cs::newLineIndent("int main()\n{\n    if (x)\n    {\n", "", unit), "        ");
    // Before a closing brace: the brace's level.
    EXPECT_EQ(cs::newLineIndent("int main()\n{\n", "}", unit), "");
    // After an access specifier, members go one level in from it (the class's level + 1).
    EXPECT_EQ(cs::newLineIndent("class A\n{\npublic:\n", "", unit), "    ");
    // case labels: their statements one level in from the label.
    EXPECT_EQ(cs::newLineIndent("switch (x)\n{\ncase 1:\n", "", unit), "    ");
    EXPECT_EQ(cs::newLineIndent("    switch (x)\n    {\n        case 1:\n", "", unit), "            ");
    // Inside parentheses: one level in from the line they opened on.
    EXPECT_EQ(cs::newLineIndent("    call(a,\n", "", unit), "        ");
    EXPECT_EQ(cs::newLineIndent("", "", unit), "");
    EXPECT_EQ(cs::newLineIndent("int x;\n", "", "\t"), "");
    EXPECT_EQ(cs::newLineIndent("void f()\n{\n", "", "\t"), "\t");
}

TEST(CppStyle, AccessSpecifiersSnapToTheClass) {
    EXPECT_EQ(cs::accessSpecifierIndent("class A\n{\n", "    public:"), "");
    EXPECT_EQ(cs::accessSpecifierIndent("namespace n\n{\n    class A\n    {\n", "        private:"), "    ");
    EXPECT_EQ(cs::accessSpecifierIndent("class A {\n    int x;\n", "  protected :"), "");
    EXPECT_EQ(cs::accessSpecifierIndent("class A\n{\n", "public slots:"), "");
    EXPECT_FALSE(cs::accessSpecifierIndent("class A\n{\n", "    std::"));   // a scope
    EXPECT_FALSE(cs::accessSpecifierIndent("class A\n{\n", "    label:"));
    EXPECT_FALSE(cs::accessSpecifierIndent("class A\n{\n", "    case 1:"));
}

TEST(CppStyle, BracesGoOnTheirOwnLine) {
    EXPECT_EQ(cs::braceOnOwnLine("class Foo {"), "class Foo\n{");
    EXPECT_EQ(cs::braceOnOwnLine("class Foo : public Bar<int> {"), "class Foo : public Bar<int>\n{");
    EXPECT_EQ(cs::braceOnOwnLine("    if (x > 0) {"), "    if (x > 0)\n    {");
    EXPECT_EQ(cs::braceOnOwnLine("int main(){"), "int main()\n{");
    EXPECT_EQ(cs::braceOnOwnLine("    void f() const override {"), "    void f() const override\n    {");
    EXPECT_EQ(cs::braceOnOwnLine("namespace kevc {"), "namespace kevc\n{");
    EXPECT_EQ(cs::braceOnOwnLine("enum class Color : int {"), "enum class Color : int\n{");
    EXPECT_EQ(cs::braceOnOwnLine("    } else {"), "    } else\n    {");
    EXPECT_EQ(cs::braceOnOwnLine("bool operator==(const A&) const {"), "bool operator==(const A&) const\n{");
    // Kept: already alone, initialisers, lambdas, strings, comments, extern "C".
    EXPECT_FALSE(cs::braceOnOwnLine("    {"));
    EXPECT_FALSE(cs::braceOnOwnLine("int a[] = {"));
    EXPECT_FALSE(cs::braceOnOwnLine("    return {"));
    EXPECT_FALSE(cs::braceOnOwnLine("    call(a, {"));
    EXPECT_FALSE(cs::braceOnOwnLine("    auto f = [this]() {"));
    EXPECT_FALSE(cs::braceOnOwnLine("    std::sort(v.begin(), v.end(), [](int a, int b) {"));
    EXPECT_FALSE(cs::braceOnOwnLine("    puts(\"{"));
    EXPECT_FALSE(cs::braceOnOwnLine("    // if (x) {"));
    EXPECT_FALSE(cs::braceOnOwnLine("extern \"C\" {"));
    EXPECT_FALSE(cs::braceOnOwnLine("Point p{"));
}

TEST(CppStyle, ClosingBraces) {
    // A class body: its semicolon, at the class's level.
    auto c = cs::closingBrace("class Foo\n{\npublic:\n    int x;\n", "");
    ASSERT_TRUE(c.indent);
    EXPECT_EQ(*c.indent, "");
    EXPECT_TRUE(c.semicolon);
    EXPECT_TRUE(cs::closingBrace("#include <string>\nstruct P : Base\n{\n", "").semicolon);
    EXPECT_TRUE(cs::closingBrace("enum class Color\n{\n    Red,\n", "").semicolon);
    EXPECT_TRUE(cs::closingBrace("template <class T>\nclass Box\n{\n", "").semicolon);
    EXPECT_TRUE(cs::closingBrace("class A\n{\npublic:\n    struct Inner\n    {\n", "    ").semicolon);
    EXPECT_EQ(*cs::closingBrace("class A\n{\npublic:\n    struct Inner\n    {\n", "        ").indent, "    ");
    // Functions, control statements, namespaces, initialisers: no semicolon.
    EXPECT_FALSE(cs::closingBrace("int main()\n{\n    return 0;\n", "").semicolon);
    EXPECT_FALSE(cs::closingBrace("void f()\n{\n    if (x)\n    {\n", "    ").semicolon);
    EXPECT_FALSE(cs::closingBrace("namespace kevc\n{\n", "").semicolon);
    EXPECT_FALSE(cs::closingBrace("struct Foo make()\n{\n", "").semicolon);
    EXPECT_FALSE(cs::closingBrace("template <class T> void f(T t)\n{\n", "").semicolon);
    EXPECT_FALSE(cs::closingBrace("int a[] = {1, 2", "int a[] = {1, 2").semicolon);
    // After code on the same line: the line's indentation is left alone.
    EXPECT_FALSE(cs::closingBrace("void f() { int x;", "void f() { int x;").indent);
    EXPECT_FALSE(cs::closingBrace("int x;\n", "").indent); // nothing open
}
