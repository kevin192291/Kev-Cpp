#include "core/Icons.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <regex>

namespace kevc::icons {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

template <size_t N>
bool oneOf(const std::string& s, const std::array<const char*, N>& list) {
    return std::find(list.begin(), list.end(), s) != list.end();
}

std::string trim(std::string_view s) {
    const auto b = s.find_first_not_of(' ');
    if (b == std::string_view::npos) return {};
    return std::string(s.substr(b, s.find_last_not_of(' ') - b + 1));
}

} // namespace

std::string forFile(const std::filesystem::path& file) {
    const std::string raw = file.extension().string();
    // On Unix, upper-case .C and .H are C++ files.
    if (raw == ".C") return "kevc-cpp-file";
    if (raw == ".H") return "kevc-header-file";
    const std::string ext = lower(raw);
    if (oneOf(ext, std::array{".cpp", ".cc", ".cxx", ".c++", ".cp", ".cppm", ".ixx"})) return "kevc-cpp-file";
    if (ext == ".c") return "kevc-c-file";
    if (oneOf(ext, std::array{".h", ".hpp", ".hh", ".hxx", ".h++", ".inl", ".ipp", ".tpp"})) return "kevc-header-file";
    if (oneOf(ext, std::array{".ts", ".tsx", ".mts", ".cts"})) return "kevc-ts-file";
    if (oneOf(ext, std::array{".js", ".jsx", ".mjs", ".cjs"})) return "kevc-js-file";
    if (oneOf(ext, std::array{".png", ".jpg", ".jpeg", ".gif", ".bmp", ".svg", ".ico", ".webp", ".rc"}))
        return "kevc-resource-file";
    if (ext == ".kevc") return "kevc-project";
    return "kevc-document";
}

std::string forFolder(bool expanded) {
    return expanded ? "kevc-folder-open" : "kevc-folder";
}

std::string forSymbolKind(int kind, int parentKind, std::string_view detail) {
    if (kind == 5 && detail == "struct") return "kevc-struct";
    switch (kind) {
    case 2: case 3: case 4: return "kevc-namespace";          // module, namespace, package
    case 5: case 11: case 26: return "kevc-class";            // class, interface, type parameter
    case 23: return "kevc-struct";
    case 6: return "kevc-method";
    case 9: return "kevc-constructor";
    case 12: case 24: case 25: return "kevc-function";        // function, event, operator
    case 7: case 8: return "kevc-member-variable";            // property, field
    case 13: {
        const bool inFunction = parentKind == 6 || parentKind == 9 || parentKind == 12;
        return inFunction ? "kevc-local-variable" : "kevc-global-variable";
    }
    case 14: case 22: return "kevc-const-variable";           // constant, enum member
    case 10: return "kevc-enum";
    case 15: return "kevc-string-variable";
    case 16: return "kevc-integer-variable";
    case 17: return "kevc-bool-variable";
    case 18: return "kevc-array-variable";
    default: return "kevc-document";
    }
}

std::string forCompletionKind(int kind) {
    switch (kind) {
    case 2: return "kevc-method";
    case 3: case 23: case 24: return "kevc-function";         // function, event, operator
    case 4: return "kevc-constructor";
    case 5: case 10: return "kevc-member-variable";           // field, property
    case 6: return "kevc-local-variable";
    case 7: case 8: case 25: return "kevc-class";             // class, interface, type parameter
    case 22: return "kevc-struct";
    case 9: return "kevc-namespace";
    case 13: return "kevc-enum";
    case 20: case 21: return "kevc-const-variable";           // enum member, constant
    case 11: case 12: return "kevc-integer-variable";         // unit, value
    case 18: return "kevc-pointer-variable";                  // reference
    case 15: return "kevc-paste";                             // snippet
    case 16: return "kevc-resource-file";                     // colour
    case 17: return "kevc-header-file";                       // file (#include completions)
    case 19: return "kevc-folder";
    default: return "kevc-string-variable";                   // text, keyword: the "abc" block
    }
}

std::string forType(std::string_view type) {
    std::string t = trim(type);
    if (t.empty()) return "kevc-local-variable";
    // A reference shows what it refers to.
    while (!t.empty() && t.back() == '&') t = trim(std::string_view(t).substr(0, t.size() - 1));
    const bool isConst = t.starts_with("const ");
    const std::string base = isConst ? trim(std::string_view(t).substr(6)) : t;

    // C strings before arrays and pointers: "const char *" and "char [6]" hold text.
    static const std::regex cString(R"(^(const )?char( const)? ?(\*|\[\d*\])$)");
    if (std::regex_match(t, cString) || base.find("string") != std::string::npos) return "kevc-string-variable";
    if (t.back() == ']' || base.starts_with("std::vector<") || base.starts_with("std::array<"))
        return "kevc-array-variable";
    if (t.back() == '*' || t.find("(*)") != std::string::npos || base.starts_with("std::unique_ptr<") ||
        base.starts_with("std::shared_ptr<"))
        return "kevc-pointer-variable";
    if (isConst) return "kevc-const-variable";

    if (base == "bool" || base == "_Bool") return "kevc-bool-variable";
    static const std::regex character(R"(^((un)?signed )?char$|^wchar_t$|^char(8|16|32)_t$)");
    if (std::regex_match(base, character)) return "kevc-char-variable";
    if (base == "float" || base == "double" || base == "long double") return "kevc-float-variable";
    static const std::regex integer(
        R"(^((un)?signed|short|long|int)( (short|long|int))*$|^__int128$|^unsigned __int128$|)"
        R"(^u?int(8|16|32|64)_t$|^u?int_(fast|least)(8|16|32|64)_t$|^(s)?size_t$|^ptrdiff_t$|^u?intptr_t$)");
    if (std::regex_match(base, integer)) return "kevc-integer-variable";
    if (base.starts_with("struct ") || base.starts_with("union ")) return "kevc-struct";
    if (base.starts_with("enum ")) return "kevc-enum";
    return "kevc-class"; // some other named type: an object
}

} // namespace kevc::icons
