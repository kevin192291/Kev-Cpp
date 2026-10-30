#include "core/Icons.h"

#include <gtest/gtest.h>

using namespace kevc;

TEST(Icons, FilesByExtension) {
    EXPECT_EQ(icons::forFile("src/main.cpp"), "kevc-cpp-file");
    EXPECT_EQ(icons::forFile("a.CC"), "kevc-cpp-file");
    EXPECT_EQ(icons::forFile("legacy.C"), "kevc-cpp-file"); // upper-case .C is C++
    EXPECT_EQ(icons::forFile("main.c"), "kevc-c-file");
    EXPECT_EQ(icons::forFile("util.hpp"), "kevc-header-file");
    EXPECT_EQ(icons::forFile("util.h"), "kevc-header-file");
    EXPECT_EQ(icons::forFile("app.ts"), "kevc-ts-file");
    EXPECT_EQ(icons::forFile("app.mjs"), "kevc-js-file");
    EXPECT_EQ(icons::forFile("logo.png"), "kevc-resource-file");
    EXPECT_EQ(icons::forFile("demo.kevc"), "kevc-project");
    EXPECT_EQ(icons::forFile("CMakeLists.txt"), "kevc-document");
    EXPECT_EQ(icons::forFile("Makefile"), "kevc-document");
    EXPECT_EQ(icons::forFolder(false), "kevc-folder");
    EXPECT_EQ(icons::forFolder(true), "kevc-folder-open");
}

TEST(Icons, SymbolKinds) {
    EXPECT_EQ(icons::forSymbolKind(5), "kevc-class");
    EXPECT_EQ(icons::forSymbolKind(23), "kevc-struct");
    EXPECT_EQ(icons::forSymbolKind(5, 0, "struct"), "kevc-struct"); // how clangd reports a struct
    EXPECT_EQ(icons::forSymbolKind(6), "kevc-method");
    EXPECT_EQ(icons::forSymbolKind(9), "kevc-constructor");
    EXPECT_EQ(icons::forSymbolKind(12), "kevc-function");
    EXPECT_EQ(icons::forSymbolKind(8), "kevc-member-variable");
    EXPECT_EQ(icons::forSymbolKind(3), "kevc-namespace");
    EXPECT_EQ(icons::forSymbolKind(10), "kevc-enum");
    EXPECT_EQ(icons::forSymbolKind(22), "kevc-const-variable");
    EXPECT_EQ(icons::forSymbolKind(13), "kevc-global-variable");
    EXPECT_EQ(icons::forSymbolKind(13, 12), "kevc-local-variable"); // a variable inside a function
    EXPECT_EQ(icons::forSymbolKind(999), "kevc-document");
}

TEST(Icons, CompletionKinds) {
    EXPECT_EQ(icons::forCompletionKind(2), "kevc-method");
    EXPECT_EQ(icons::forCompletionKind(3), "kevc-function");
    EXPECT_EQ(icons::forCompletionKind(7), "kevc-class");
    EXPECT_EQ(icons::forCompletionKind(22), "kevc-struct");
    EXPECT_EQ(icons::forCompletionKind(17), "kevc-header-file");
    EXPECT_EQ(icons::forCompletionKind(14), "kevc-string-variable"); // keyword
}

TEST(Icons, DebuggerTypes) {
    EXPECT_EQ(icons::forType("int"), "kevc-integer-variable");
    EXPECT_EQ(icons::forType("unsigned long long"), "kevc-integer-variable");
    EXPECT_EQ(icons::forType("size_t"), "kevc-integer-variable");
    EXPECT_EQ(icons::forType("uint8_t"), "kevc-integer-variable");
    EXPECT_EQ(icons::forType("double"), "kevc-float-variable");
    EXPECT_EQ(icons::forType("long double"), "kevc-float-variable");
    EXPECT_EQ(icons::forType("bool"), "kevc-bool-variable");
    EXPECT_EQ(icons::forType("char"), "kevc-char-variable");
    EXPECT_EQ(icons::forType("unsigned char"), "kevc-char-variable");
    EXPECT_EQ(icons::forType("std::string"), "kevc-string-variable");
    EXPECT_EQ(icons::forType("std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >"),
              "kevc-string-variable");
    EXPECT_EQ(icons::forType("const char *"), "kevc-string-variable");
    EXPECT_EQ(icons::forType("char [6]"), "kevc-string-variable");
    EXPECT_EQ(icons::forType("int [4]"), "kevc-array-variable");
    EXPECT_EQ(icons::forType("std::vector<int, std::allocator<int> >"), "kevc-array-variable");
    EXPECT_EQ(icons::forType("Point *"), "kevc-pointer-variable");
    EXPECT_EQ(icons::forType("void (*)(int)"), "kevc-pointer-variable");
    EXPECT_EQ(icons::forType("std::unique_ptr<Node, std::default_delete<Node> >"), "kevc-pointer-variable");
    EXPECT_EQ(icons::forType("const int"), "kevc-const-variable");
    EXPECT_EQ(icons::forType("int &"), "kevc-integer-variable"); // a reference shows its target
    EXPECT_EQ(icons::forType("struct Point"), "kevc-struct");
    EXPECT_EQ(icons::forType("enum Colour"), "kevc-enum");
    EXPECT_EQ(icons::forType("Point"), "kevc-class");
    EXPECT_EQ(icons::forType(""), "kevc-local-variable");
}
