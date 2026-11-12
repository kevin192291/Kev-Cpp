#include "core/QuickFix.h"

#include "core/Lsp.h"

#include <gtest/gtest.h>

#include <fstream>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace kevc;
using quickfix::Fix;
using Names = std::set<std::string>;

namespace {

fs::path scratch(const char* name) {
    const fs::path dir = fs::temp_directory_path() /
                         ("kevc-quickfix-" + std::string(name) + "-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void write(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file) << text;
}

} // namespace

TEST(QuickFix, UnknownNameFromClangdsMessages) {
    EXPECT_EQ(quickfix::unknownName("Unknown type name 'TileMap'"), "TileMap");
    EXPECT_EQ(quickfix::unknownName("unknown type name 'TileMap'"), "TileMap");
    EXPECT_EQ(quickfix::unknownName("Use of undeclared identifier 'clampValue'"), "clampValue");
    EXPECT_EQ(quickfix::unknownName("Use of undeclared identifier 'Playr'; did you mean 'Player'?"), "Playr");
    EXPECT_EQ(quickfix::unknownName("No member named 'cout' in namespace 'std'"), "std::cout");
    EXPECT_EQ(quickfix::unknownName("No template named 'vector' in namespace 'std'"), "std::vector");
    EXPECT_EQ(quickfix::unknownName("No type named 'string' in namespace 'std'"), "std::string");
    EXPECT_EQ(quickfix::unknownName("No template named 'Grid'"), "Grid");
    EXPECT_EQ(quickfix::unknownName("Variable has incomplete type 'TileMap'"), "TileMap");
    EXPECT_EQ(quickfix::unknownName("Member access into incomplete type 'sf::Texture'"), "sf::Texture");
    EXPECT_EQ(quickfix::unknownName("Variable has incomplete type 'struct point'"), "point");
    EXPECT_EQ(quickfix::unknownName("Call to undeclared function 'helper'; ISO C99 and later do not support implicit "
                                    "function declarations"),
              "helper");
    // A class's missing member: no #include adds one.
    EXPECT_EQ(quickfix::unknownName("No member named 'jump' in 'Player'"), std::nullopt);
    EXPECT_EQ(quickfix::unknownName("Expected ';' after expression"), std::nullopt);
    EXPECT_EQ(quickfix::unknownName("Unused variable 'x'"), std::nullopt);
}

TEST(QuickFix, UnknownNameFromGccsMessages) {
    EXPECT_EQ(quickfix::unknownName("‘TileMap’ was not declared in this scope"), "TileMap");
    EXPECT_EQ(quickfix::unknownName("'TileMap' was not declared in this scope; did you mean 'TileMa'?"), "TileMap");
    EXPECT_EQ(quickfix::unknownName("‘TileMap’ does not name a type"), "TileMap");
    EXPECT_EQ(quickfix::unknownName("‘Player’ has not been declared"), "Player");
    EXPECT_EQ(quickfix::unknownName("‘cout’ is not a member of ‘std’"), "std::cout");
    EXPECT_EQ(quickfix::unknownName("‘string’ in namespace ‘std’ does not name a type"), "std::string");
    EXPECT_EQ(quickfix::unknownName("invalid use of incomplete type ‘class TileMap’"), "TileMap");
    EXPECT_EQ(quickfix::unknownName("‘x’ is used uninitialized"), std::nullopt);
}

TEST(QuickFix, DeclaredNamesAreTypesFunctionsVariablesAndMacros) {
    const char* header = R"cpp(// A grid of tiles.
#pragma once
#include <SFML/Graphics.hpp>
#define TILE_SIZE 32
#define MAX(a, b) ((a) > (b) ? (a) : (b))

class TileMap : public sf::Drawable, public sf::Transformable
{
public:
    bool load(const std::filesystem::path& tileset, sf::Vector2u tileSize);
    int notMe = 0;
private:
    void draw(sf::RenderTarget& target, sf::RenderStates states) const override
    {
        int local = 3;
        target.draw(vertices_, states);
    }
    sf::VertexArray vertices_;
};

struct Point { int x, y; };
struct [[nodiscard]] Result final { bool ok; };
class SFML_API Texture : public Resource<Texture> {};
union Value { int i; float f; };
enum Direction { North, East = 2, South = (1 << 2), West };
enum class Mode : unsigned char { Edit, Play };
using Grid = std::vector<std::vector<int>>;
typedef unsigned int Count, *CountPtr;
typedef void (*Callback)(int value, void* data);
typedef struct { int r, g, b; } Colour;
template <typename T> class Matrix { T cell; };
template <typename T>
T clampValue(T v, T lo, T hi) { return v < lo ? lo : v > hi ? hi : v; }
template <typename T> concept Drawable = requires(T t) { t.draw(); };
inline constexpr int kTileCount = 64;
extern const sf::Vector2u kMapSize;
std::vector<int> makeLevel(int width, int height);
static int counter{0};
namespace game {
    class Player {};
    namespace detail { int helperFunction(); }
}
namespace a::b { struct Deep {}; }
extern "C" { int c_function(void); }
)cpp";
    const Names names = quickfix::declaredNames(header);
    for (const char* name :
         {"TILE_SIZE", "MAX",    "TileMap",        "Point",      "Result",    "Texture",    "Value",    "Direction",
          "North",     "East",   "South",          "West",       "Mode",      "Grid",       "Count",    "CountPtr",
          "Callback",  "Colour", "Matrix",         "clampValue", "Drawable",  "kTileCount", "kMapSize", "makeLevel",
          "counter",   "Player", "helperFunction", "Deep",       "c_function"})
        EXPECT_TRUE(names.count(name)) << name;
    // Members, locals, parameters, base classes, a scoped enum's values and keywords aren't.
    for (const char* name : {"load", "notMe", "draw", "local", "vertices_", "tileset",       "tileSize", "target",
                             "x",    "y",     "Edit", "Play",  "Drawable_", "Transformable", "sf",       "std",
                             "T",    "v",     "lo",   "value", "data",      "width",         "game",     "detail",
                             "a",    "b",     "int",  "const", "cell",      "Resource",      "ok",       "r"})
        EXPECT_FALSE(names.count(name)) << name;
}

TEST(QuickFix, ForwardDeclarationsAndTemplateParametersDontCount) {
    const Names names = quickfix::declaredNames(R"cpp(
class TileMap;
struct Point;
template <class T, class Alloc = std::allocator<T>> struct Holder { T* p; };
void use(class Widget* w, struct Gadget g);
enum class Opaque : int;
)cpp");
    EXPECT_FALSE(names.count("TileMap"));
    EXPECT_FALSE(names.count("Point"));
    EXPECT_FALSE(names.count("T"));
    EXPECT_FALSE(names.count("Alloc"));
    EXPECT_FALSE(names.count("Widget"));
    EXPECT_FALSE(names.count("Gadget"));
    EXPECT_FALSE(names.count("Opaque"));
    EXPECT_TRUE(names.count("Holder"));
    EXPECT_TRUE(names.count("use"));
}

TEST(QuickFix, CommentsStringsAndDisabledTextAreSkipped) {
    const Names names = quickfix::declaredNames(R"cpp(
// class InComment {};
/* struct InBlock {};
   int inBlock; */
const char* text = "class InString {};";
const char* raw = R"x(struct InRaw {}; )" )x";
char quote = '"';
#define LONG_MACRO(x) \
    class InMacro {}; \
    int alsoInMacro;
class AfterAll {};
)cpp");
    EXPECT_EQ(names, (Names{"AfterAll", "LONG_MACRO", "quote", "raw", "text"}));
}

TEST(QuickFix, HeadersByExtension) {
    for (const char* name : {"a.h", "a.hpp", "a.hh", "a.hxx", "a.h++", "a.inl", "a.HPP", "x/y.tpp"})
        EXPECT_TRUE(quickfix::isHeader(name)) << name;
    for (const char* name : {"a.cpp", "a.c", "a.txt", "Makefile", "a.h.bak"})
        EXPECT_FALSE(quickfix::isHeader(name)) << name;
}

TEST(QuickFix, IncludeSpellingIsTheShortestInsidePath) {
    const fs::path root = "/p";
    EXPECT_EQ(quickfix::includeSpelling(root / "src/main.cpp", root / "src/TileMap.hpp", {}), "\"TileMap.hpp\"");
    EXPECT_EQ(quickfix::includeSpelling(root / "src/main.cpp", root / "src/gfx/TileMap.hpp", {}),
              "\"gfx/TileMap.hpp\"");
    // Up and over: from the file's folder, unless an include folder holds it.
    EXPECT_EQ(quickfix::includeSpelling(root / "src/main.cpp", root / "include/game/TileMap.hpp", {}),
              "\"../include/game/TileMap.hpp\"");
    EXPECT_EQ(quickfix::includeSpelling(root / "src/main.cpp", root / "include/game/TileMap.hpp", {root / "include"}),
              "\"game/TileMap.hpp\"");
    // The nearer of the two.
    EXPECT_EQ(quickfix::includeSpelling(root / "src/main.cpp", root / "src/gfx/deep/TileMap.hpp", {root / "src/gfx"}),
              "\"deep/TileMap.hpp\"");
    EXPECT_EQ(quickfix::includeSpelling(root / "src/./main.cpp", root / "src/../src/TileMap.hpp", {}),
              "\"TileMap.hpp\"");
}

TEST(QuickFix, HeaderIndexFindsDeclaringHeadersAndNoticesChanges) {
    const fs::path dir = scratch("index");
    write(dir / "src/main.cpp", "TileMap map;\n");
    write(dir / "src/TileMap.hpp", "#pragma once\nclass TileMap {};\n");
    write(dir / "src/Forward.hpp", "class TileMap;\n");
    write(dir / "include/game/Player.h", "namespace game { struct Player {}; }\n");
    write(dir / "build/generated/TileMap.hpp", "class TileMap {};\n"); // build output: the Project tab hides it
    write(dir / "notes.txt", "class TileMap {};\n");

    quickfix::HeaderIndex index;
    EXPECT_EQ(index.find({dir}, "TileMap"), (std::vector<fs::path>{dir / "src/TileMap.hpp"}));
    EXPECT_EQ(index.find({dir}, "game::Player"), (std::vector<fs::path>{dir / "include/game/Player.h"}));
    EXPECT_TRUE(index.find({dir}, "Nothing").empty());
    // A folder given twice (the project and an include folder in it) lists a header once.
    EXPECT_EQ(index.find({dir, dir / "include"}, "Player").size(), 1u);

    std::this_thread::sleep_for(std::chrono::milliseconds(20)); // a new modification time
    write(dir / "src/Forward.hpp", "class TileMap {\n};\n");
    EXPECT_EQ(index.find({dir}, "TileMap"), (std::vector<fs::path>{dir / "src/Forward.hpp", dir / "src/TileMap.hpp"}));
    // Reading stops at the limit: Player.h and Forward.hpp, not TileMap.hpp.
    EXPECT_EQ(index.find({dir}, "TileMap", 2), (std::vector<fs::path>{dir / "src/Forward.hpp"}));
    fs::remove_all(dir);
}

TEST(QuickFix, IncludeFixesNearestFirstWithoutOnesAlreadyThere) {
    const fs::path root = "/p";
    const fs::path file = root / "src/main.cpp";
    const std::string text = "#include <SFML/Graphics.hpp>\n#include \"Other.hpp\"\n\nTileMap map;\n";
    const std::vector<fs::path> headers{root / "include/TileMap.hpp", root / "src/gfx/TileMap.hpp",
                                        root / "src/TileMap.hpp", root / "src/Other.hpp", file};
    const auto fixes = quickfix::includeFixes(file, text, headers, {}, {});
    ASSERT_EQ(fixes.size(), 3u);
    EXPECT_EQ(fixes[0], (Fix{"#include \"TileMap.hpp\"", "\"TileMap.hpp\"", {}}));
    EXPECT_EQ(fixes[1].include, "\"gfx/TileMap.hpp\"");
    EXPECT_EQ(fixes[2].include, "\"../include/TileMap.hpp\"");

    // A server offering the same #include: its fix is the one shown.
    const Fix clangd{"#include \"TileMap.hpp\"", "\"TileMap.hpp\"", {}};
    const auto rest = quickfix::includeFixes(file, text, headers, {}, {clangd});
    ASSERT_EQ(rest.size(), 2u);
    EXPECT_EQ(rest[0].include, "\"gfx/TileMap.hpp\"");
    const Fix asEdit{"Add it", "", {{1, 0, 1, 0, "#include \"gfx/TileMap.hpp\"\n"}}};
    EXPECT_EQ(quickfix::includeFixes(file, text, headers, {}, {asEdit}).size(), 2u);
}

TEST(QuickFix, CodeActionsWithEditsToThisFile) {
    const fs::path file = "/p/src/main.cpp";
    ASSERT_EQ(lsp::pathToUri(file), "file:///p/src/main.cpp");
    const auto result = nlohmann::json::parse(R"json([
        {"title": "change '.' to '->'", "kind": "quickfix",
         "edit": {"changes": {"file:///p/src/main.cpp": [
             {"range": {"start": {"line": 3, "character": 4}, "end": {"line": 3, "character": 5}}, "newText": "->"}]}}},
        {"title": "Include \"TileMap.hpp\" for symbol TileMap", "kind": "quickfix", "isPreferred": true,
         "edit": {"documentChanges": [{"textDocument": {"uri": "file:///p/src/main.cpp", "version": 3}, "edits": [
             {"range": {"start": {"line": 1, "character": 0}, "end": {"line": 1, "character": 0}},
              "newText": "#include \"TileMap.hpp\"\n"}]}]}},
        {"title": "Run something", "command": "clangd.applyTweak"},
        {"title": "Extract function", "kind": "refactor.extract",
         "edit": {"changes": {"file:///p/src/main.cpp": [
             {"range": {"start": {"line": 3, "character": 0}, "end": {"line": 3, "character": 9}}, "newText": "f()"}]}}},
        {"title": "Disabled", "kind": "quickfix", "disabled": {"reason": "no"},
         "edit": {"changes": {"file:///p/src/main.cpp": [
             {"range": {"start": {"line": 3, "character": 0}, "end": {"line": 3, "character": 9}}, "newText": "x"}]}}},
        {"title": "Other file", "kind": "quickfix",
         "edit": {"changes": {"file:///p/src/other.cpp": [
             {"range": {"start": {"line": 0, "character": 0}, "end": {"line": 0, "character": 0}}, "newText": "x"}]}}},
        {"title": "Create file", "kind": "quickfix",
         "edit": {"documentChanges": [{"kind": "create", "uri": "file:///p/src/new.hpp"}]}}
    ])json");
    // Preferred first; a bare Command, a refactoring, a disabled action and edits elsewhere are left out.
    const auto fixes = quickfix::parseCodeActions(result, file);
    ASSERT_EQ(fixes.size(), 2u);
    // clangd's own #include fix reads like the header scan's, and goes in the same place.
    EXPECT_EQ(fixes[0], (Fix{"#include \"TileMap.hpp\"", "\"TileMap.hpp\"", {}}));
    EXPECT_EQ(fixes[1], (Fix{"change '.' to '->'", "", {{3, 4, 3, 5, "->"}}}));
    EXPECT_TRUE(quickfix::parseCodeActions(nullptr, file).empty());
}

TEST(QuickFix, OnlyAnAddedIncludeLineCountsAsAnIncludeFix) {
    const fs::path file = "/p/main.cpp";
    auto action = [](const std::string& newText, int endCharacter) {
        nlohmann::json edit = nlohmann::json::parse(R"json({"range": {"start": {"line": 0, "character": 0},
                                                                     "end": {"line": 0, "character": 0}}})json");
        edit["range"]["end"]["character"] = endCharacter;
        edit["newText"] = newText;
        nlohmann::json result = nlohmann::json::parse(
            R"json([{"title": "t", "kind": "quickfix", "edit": {"changes": {}}}])json");
        result[0]["edit"]["changes"]["file:///p/main.cpp"] = nlohmann::json::array({edit});
        return result;
    };
    EXPECT_EQ(quickfix::parseCodeActions(action("#include <vector>\n", 0), file)[0].include, "<vector>");
    EXPECT_EQ(quickfix::parseCodeActions(action("\n#include \"a.h\"", 0), file)[0].include, "\"a.h\"");
    // Replacing text, two lines, or something else: the server's edits as they are.
    for (const auto& other : {action("#include <vector>\n", 3), action("#include <a>\n#include <b>\n", 0),
                              action("#include\n", 0), action("int x;\n", 0)}) {
        const auto fixes = quickfix::parseCodeActions(other, file);
        ASSERT_EQ(fixes.size(), 1u);
        EXPECT_EQ(fixes[0].include, "");
        EXPECT_EQ(fixes[0].title, "t");
    }
}

TEST(QuickFix, FixCountsComeOffMessages) {
    EXPECT_EQ(quickfix::withoutFixCount("Unknown type name 'TileMap' (fix available)"), "Unknown type name 'TileMap'");
    EXPECT_EQ(quickfix::withoutFixCount("Expected ';' (fixes available)"), "Expected ';'");
    EXPECT_EQ(quickfix::withoutFixCount("Unused variable 'x'"), "Unused variable 'x'");
}
