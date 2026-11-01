#include "core/NpmPackages.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace kevc::npm {

namespace {

std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

// "5.1.0-beta" -> {5, 1, 0}; nullopt unless it starts with three numbers.
std::optional<std::array<long, 3>> numbers(std::string_view v) {
    std::array<long, 3> out{};
    size_t i = 0;
    for (int part = 0; part < 3; ++part) {
        if (i >= v.size() || !std::isdigit(static_cast<unsigned char>(v[i]))) return std::nullopt;
        long n = 0;
        while (i < v.size() && std::isdigit(static_cast<unsigned char>(v[i]))) n = std::min(n * 10 + (v[i++] - '0'), 999999999L);
        out[static_cast<size_t>(part)] = n;
        if (part < 2) {
            if (i >= v.size() || v[i] != '.') return std::nullopt;
            ++i;
        }
    }
    return out;
}

std::string join(const std::array<long, 3>& v) {
    return std::to_string(v[0]) + "." + std::to_string(v[1]) + "." + std::to_string(v[2]);
}

// ---- A small JSON reader that reports where things are, for edits that keep the text as it is.

size_t skipSpace(std::string_view t, size_t i) {
    while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) ++i;
    return i;
}

// The end of the string starting at the quote at i (just past the closing quote).
std::optional<size_t> stringEnd(std::string_view t, size_t i) {
    for (++i; i < t.size(); ++i) {
        if (t[i] == '\\') ++i;
        else if (t[i] == '"') return i + 1;
    }
    return std::nullopt;
}

std::optional<size_t> valueEnd(std::string_view t, size_t i) {
    if (i >= t.size()) return std::nullopt;
    if (t[i] == '"') return stringEnd(t, i);
    if (t[i] == '{' || t[i] == '[') {
        int depth = 0;
        for (; i < t.size(); ++i) {
            if (t[i] == '"') {
                auto end = stringEnd(t, i);
                if (!end) return std::nullopt;
                i = *end - 1;
            } else if (t[i] == '{' || t[i] == '[') {
                ++depth;
            } else if (t[i] == '}' || t[i] == ']') {
                if (--depth == 0) return i + 1;
            }
        }
        return std::nullopt;
    }
    while (i < t.size() && t[i] != ',' && t[i] != '}' && t[i] != ']' && !std::isspace(static_cast<unsigned char>(t[i]))) ++i;
    return i;
}

struct Member {
    std::string key;
    size_t keyBegin = 0, valueBegin = 0, valueEnd = 0;
};

struct Object {
    std::vector<Member> members;
    size_t open = 0, close = 0; // positions of { and }
};

std::optional<Object> readObject(std::string_view t, size_t open) {
    if (open >= t.size() || t[open] != '{') return std::nullopt;
    Object obj{{}, open, 0};
    size_t i = skipSpace(t, open + 1);
    if (i < t.size() && t[i] == '}') {
        obj.close = i;
        return obj;
    }
    while (i < t.size()) {
        if (t[i] != '"') return std::nullopt;
        auto keyEnd = stringEnd(t, i);
        if (!keyEnd) return std::nullopt;
        Member m;
        m.keyBegin = i;
        try {
            m.key = nlohmann::json::parse(t.substr(i, *keyEnd - i)).get<std::string>();
        } catch (const std::exception&) {
            return std::nullopt;
        }
        i = skipSpace(t, *keyEnd);
        if (i >= t.size() || t[i] != ':') return std::nullopt;
        i = skipSpace(t, i + 1);
        auto end = valueEnd(t, i);
        if (!end) return std::nullopt;
        m.valueBegin = i;
        m.valueEnd = *end;
        obj.members.push_back(std::move(m));
        i = skipSpace(t, *end);
        if (i < t.size() && t[i] == ',') {
            i = skipSpace(t, i + 1);
            continue;
        }
        if (i < t.size() && t[i] == '}') {
            obj.close = i;
            return obj;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

// The whitespace a line starts with, for the line holding position `pos`.
std::string indentAt(std::string_view t, size_t pos) {
    const size_t lineStart = t.rfind('\n', pos == 0 ? 0 : pos - 1);
    size_t i = lineStart == std::string_view::npos ? 0 : lineStart + 1;
    std::string out;
    while (i < pos && (t[i] == ' ' || t[i] == '\t')) out += t[i++];
    return out;
}

std::string quoted(const std::string& s) {
    return nlohmann::json(s).dump();
}

// The indent one level deeper than `outer`, guessing the unit from the file (2 spaces by default).
std::string deeper(std::string_view t, const std::string& outer) {
    // The first member of the root object shows the file's indent unit.
    if (auto root = readObject(t, skipSpace(t, 0)); root && !root->members.empty()) {
        const std::string unit = indentAt(t, root->members.front().keyBegin);
        if (!unit.empty()) return outer + unit;
    }
    return outer + "  ";
}

} // namespace

// ---------------------------------------------------------------------------------------------

bool isVersion(std::string_view text) {
    if (!numbers(text)) return false;
    // Three numbers, then nothing or a -prerelease / +build part.
    size_t dots = 0, i = 0;
    for (; i < text.size() && (std::isdigit(static_cast<unsigned char>(text[i])) || text[i] == '.'); ++i)
        if (text[i] == '.') ++dots;
    if (dots != 2) return false;
    if (i == text.size()) return true;
    if (text[i] != '-' && text[i] != '+') return false;
    return std::all_of(text.begin() + static_cast<long>(i) + 1, text.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '+';
    }) && i + 1 < text.size();
}

int compareVersions(std::string_view a, std::string_view b) {
    const auto x = numbers(a), y = numbers(b);
    if (!x || !y) return 0;
    for (size_t i = 0; i < 3; ++i)
        if ((*x)[i] != (*y)[i]) return (*x)[i] < (*y)[i] ? -1 : 1;
    return 0;
}

VersionSpec parseSpec(std::string_view text) {
    VersionSpec spec;
    spec.raw = std::string(text);
    const std::string s = trim(text);
    auto startsWith = [&s](std::string_view p) { return s.rfind(p, 0) == 0; };
    if (s.empty() || s == "*" || s == "x" || s == "X") {
        spec.updates = Updates::Any;
        return spec;
    }
    if (startsWith("workspace:")) return spec.source = Source::Workspace, spec;
    if (startsWith("file:") || startsWith("link:") || startsWith("./") || startsWith("../") || startsWith("/") ||
        startsWith("~/"))
        return spec.source = Source::Local, spec;
    if (startsWith("npm:")) return spec.source = Source::Alias, spec;
    if (startsWith("http://") || startsWith("https://")) return spec.source = Source::Url, spec;
    if (startsWith("git") || startsWith("github:") || startsWith("gitlab:") || startsWith("bitbucket:") ||
        (s.find('/') != std::string::npos && s.find(' ') == std::string::npos && !std::isdigit(static_cast<unsigned char>(s[0]))))
        return spec.source = Source::Git, spec;

    // An operator, then one full version.
    std::string op, rest = s;
    for (const char* o : {">=", "^", "~", "=", "v"})
        if (startsWith(o)) {
            op = o;
            rest = trim(std::string_view(s).substr(op.size()));
            break;
        }
    if (isVersion(rest)) {
        spec.version = rest;
        if (op == "^") spec.updates = Updates::Features;
        else if (op == "~") spec.updates = Updates::Fixes;
        else if (op == ">=") spec.updates = Updates::Newer;
        else spec.updates = Updates::Locked; // "", "=", "v"
        return spec;
    }
    // A dist-tag: a name, not a range ("latest", "next", "beta").
    if (std::isalpha(static_cast<unsigned char>(s[0])) && std::all_of(s.begin(), s.end(), [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.';
        })) {
        spec.source = Source::Tag;
        return spec;
    }
    // Anything else ("1.2.x", ">=1 <3", "^2 || ^3") stays as written; its first full version, if
    // any, is still a starting point for a new rule.
    for (size_t i = 0; i < s.size(); ++i)
        if (std::isdigit(static_cast<unsigned char>(s[i])) && (i == 0 || !std::isalnum(static_cast<unsigned char>(s[i - 1])))) {
            size_t j = i;
            while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '.' || s[j] == '-' || s[j] == '+')) ++j;
            if (isVersion(s.substr(i, j - i))) {
                spec.version = s.substr(i, j - i);
                break;
            }
        }
    return spec;
}

std::string makeSpec(Updates updates, const std::string& version) {
    switch (updates) {
    case Updates::Locked: return version;
    case Updates::Fixes: return "~" + version;
    case Updates::Features: return "^" + version;
    case Updates::Newer: return ">=" + version;
    case Updates::Any: return "*";
    case Updates::Custom: return version;
    }
    return version;
}

const char* updatesLabel(Updates updates) {
    switch (updates) {
    case Updates::Locked: return "Locked: this version only";
    case Updates::Fixes: return "Bug fixes only (~)";
    case Updates::Features: return "New features and fixes (^)";
    case Updates::Newer: return "Any newer version (>=)";
    case Updates::Any: return "Any version (*)";
    case Updates::Custom: return "As written";
    }
    return "";
}

std::string describe(const VersionSpec& spec) {
    switch (spec.source) {
    case Source::Tag: return "Whatever version is tagged “" + trim(spec.raw) + "” when installing";
    case Source::Git: return "From a git repository: " + trim(spec.raw);
    case Source::Local: return "From a folder on this computer: " + trim(spec.raw);
    case Source::Workspace: return "Another package of this workspace";
    case Source::Url: return "A package downloaded from " + trim(spec.raw);
    case Source::Alias: return "Another package under this name: " + trim(spec.raw).substr(4);
    case Source::Registry: break;
    }
    const auto v = numbers(spec.version);
    switch (spec.updates) {
    case Updates::Locked: return "Exactly " + spec.version + ": never changes when installing";
    case Updates::Fixes:
        if (v) return "Bug fixes only: " + spec.version + " up to, not including, " + join({(*v)[0], (*v)[1] + 1, 0});
        break;
    case Updates::Features:
        if (v) {
            // Below 1.0.0 the first non-zero number counts as the major one: ^0.2.3 stays below 0.3.0.
            const std::array<long, 3> upper = (*v)[0] > 0   ? std::array<long, 3>{(*v)[0] + 1, 0, 0}
                                              : (*v)[1] > 0 ? std::array<long, 3>{0, (*v)[1] + 1, 0}
                                                            : std::array<long, 3>{0, 0, (*v)[2] + 1};
            return "New features and fixes: " + spec.version + " up to, not including, " + join(upper);
        }
        break;
    case Updates::Newer: return spec.version + " or any newer version, even one that may break your code";
    case Updates::Any: return "Any version: whatever is newest when installing";
    case Updates::Custom: break;
    }
    return "As written in package.json: " + trim(spec.raw);
}

// ---------------------------------------------------------------------------------------------

std::vector<Dependency> readDependencies(const fs::path& root) {
    std::vector<Dependency> out;
    std::ifstream in(root / "package.json");
    nlohmann::ordered_json pkg;
    try {
        pkg = nlohmann::ordered_json::parse(in);
    } catch (const std::exception&) {
        return out;
    }
    if (!pkg.is_object()) return out;
    for (const char* section : {"dependencies", "devDependencies", "optionalDependencies", "peerDependencies"}) {
        const auto it = pkg.find(section);
        if (it == pkg.end() || !it->is_object()) continue;
        for (const auto& [name, spec] : it->items()) {
            if (!spec.is_string()) continue;
            Dependency d{name, section, spec.get<std::string>(), {}};
            std::ifstream installed(root / "node_modules" / name / "package.json");
            try {
                if (installed) d.installed = nlohmann::json::parse(installed).value("version", "");
            } catch (const std::exception&) {
            }
            out.push_back(std::move(d));
        }
    }
    return out;
}

std::optional<std::string> setDependency(std::string_view text, const std::string& section, const std::string& name,
                                         const std::string& spec) {
    const auto root = readObject(text, skipSpace(text, 0));
    if (!root) return std::nullopt;
    std::string out(text);
    const std::string entry = quoted(name) + ": " + quoted(spec);
    const auto sec = std::find_if(root->members.begin(), root->members.end(), [&](const Member& m) { return m.key == section; });

    if (sec == root->members.end()) {
        // A new section at the end of package.json.
        const std::string inner = root->members.empty() ? std::string("  ") : indentAt(text, root->members.back().keyBegin);
        const std::string block = quoted(section) + ": {\n" + deeper(text, inner) + entry + "\n" + inner + "}";
        if (root->members.empty()) {
            out.replace(root->open + 1, root->close - root->open - 1, "\n" + inner + block + "\n");
        } else {
            out.insert(root->members.back().valueEnd, ",\n" + inner + block);
        }
        return out;
    }
    const auto deps = readObject(text, sec->valueBegin);
    if (!deps) return std::nullopt;
    const auto same = std::find_if(deps->members.begin(), deps->members.end(), [&](const Member& m) { return m.key == name; });
    if (same != deps->members.end()) {
        out.replace(same->valueBegin, same->valueEnd - same->valueBegin, quoted(spec));
        return out;
    }
    if (deps->members.empty()) {
        const std::string outer = indentAt(text, sec->keyBegin);
        out.replace(deps->open + 1, deps->close - deps->open - 1, "\n" + deeper(text, outer) + entry + "\n" + outer);
        return out;
    }
    const std::string indent = indentAt(text, deps->members.front().keyBegin);
    const auto after = std::find_if(deps->members.begin(), deps->members.end(), [&](const Member& m) { return m.key > name; });
    if (after == deps->members.end()) out.insert(deps->members.back().valueEnd, ",\n" + indent + entry);
    else out.insert(after->keyBegin, entry + ",\n" + indent);
    return out;
}

std::optional<std::string> removeDependency(std::string_view text, const std::string& section, const std::string& name) {
    const auto root = readObject(text, skipSpace(text, 0));
    if (!root) return std::nullopt;
    const auto sec = std::find_if(root->members.begin(), root->members.end(), [&](const Member& m) { return m.key == section; });
    if (sec == root->members.end()) return std::string(text);
    const auto deps = readObject(text, sec->valueBegin);
    if (!deps) return std::nullopt;
    const auto it = std::find_if(deps->members.begin(), deps->members.end(), [&](const Member& m) { return m.key == name; });
    if (it == deps->members.end()) return std::string(text);
    std::string out(text);
    if (deps->members.size() == 1) {
        out.replace(deps->open, deps->close - deps->open + 1, "{}");
    } else if (it + 1 != deps->members.end()) {
        out.erase(it->keyBegin, (it + 1)->keyBegin - it->keyBegin); // the entry, its comma and the line break
    } else {
        out.erase((it - 1)->valueEnd, it->valueEnd - (it - 1)->valueEnd); // the last one: the comma before it too
    }
    return out;
}

// ---------------------------------------------------------------------------------------------

namespace {

// key and value of an .npmrc line ("registry = https://x/" -> registry, https://x/), or nullopt.
std::optional<std::pair<std::string, std::string>> setting(const std::string& line) {
    const std::string t = trim(line);
    if (t.empty() || t[0] == '#' || t[0] == ';') return std::nullopt;
    const size_t eq = t.find('=');
    if (eq == std::string::npos) return std::nullopt;
    return std::make_pair(trim(std::string_view(t).substr(0, eq)), trim(std::string_view(t).substr(eq + 1)));
}

bool isScopeKey(const std::string& key) {
    return key.size() > 10 && key[0] == '@' && key.ends_with(":registry");
}

} // namespace

Registries readRegistries(std::string_view npmrc) {
    Registries out;
    std::istringstream in{std::string(npmrc)};
    for (std::string line; std::getline(in, line);) {
        auto kv = setting(line);
        if (!kv) continue;
        if (kv->first == "registry") out.main = kv->second;
        else if (isScopeKey(kv->first)) out.scoped.emplace_back(kv->first.substr(0, kv->first.size() - 9), kv->second);
    }
    return out;
}

std::string writeRegistries(std::string_view npmrc, const Registries& registries) {
    std::vector<std::string> lines;
    std::istringstream in{std::string(npmrc)};
    for (std::string line; std::getline(in, line);) lines.push_back(line);
    std::vector<std::pair<std::string, std::string>> wanted; // key -> value, what the file should say
    if (!registries.main.empty()) wanted.emplace_back("registry", registries.main);
    for (const auto& [scope, url] : registries.scoped)
        if (!scope.empty() && !url.empty()) wanted.emplace_back((scope[0] == '@' ? scope : "@" + scope) + ":registry", url);

    std::vector<std::string> out;
    std::vector<bool> written(wanted.size(), false);
    for (const auto& line : lines) {
        auto kv = setting(line);
        if (!kv || (kv->first != "registry" && !isScopeKey(kv->first))) {
            out.push_back(line); // auth, comments, everything else: as it was
            continue;
        }
        const auto it = std::find_if(wanted.begin(), wanted.end(), [&](const auto& w) { return w.first == kv->first; });
        if (it == wanted.end()) continue; // a registry no longer wanted
        const size_t i = static_cast<size_t>(it - wanted.begin());
        if (written[i]) continue;
        out.push_back(it->first + "=" + it->second);
        written[i] = true;
    }
    for (size_t i = 0; i < wanted.size(); ++i)
        if (!written[i]) out.push_back(wanted[i].first + "=" + wanted[i].second);
    while (!out.empty() && trim(out.back()).empty()) out.pop_back();
    std::string text;
    for (const auto& line : out) text += line + "\n";
    return text;
}

std::string checkRegistryUrl(std::string_view url) {
    const std::string u = trim(url);
    if (u.empty()) return {};
    if (u.rfind("https://", 0) != 0 && u.rfind("http://", 0) != 0)
        return "A package source is a web address starting with https:// (or http://).";
    const size_t host = u.find("://") + 3;
    if (host >= u.size() || u[host] == '/' || u.find(' ') != std::string::npos) return "That address has no server name.";
    return {};
}

std::string normalRegistryUrl(std::string_view url) {
    std::string u = trim(url);
    if (!u.empty() && u.back() != '/') u += '/';
    return u;
}

} // namespace kevc::npm
