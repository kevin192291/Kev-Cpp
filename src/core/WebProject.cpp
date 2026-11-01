#include "core/WebProject.h"

#include "core/Diagnostics.h"

#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <fstream>

namespace fs = std::filesystem;

namespace kevc {

WebProject WebProject::detect(const fs::path& root) {
    WebProject web;
    std::error_code ec;
    // The lockfile says which manager installed node_modules; packageManager (below) wins.
    if (fs::exists(root / "pnpm-lock.yaml", ec)) web.packageManager = PackageManager::Pnpm;
    else if (fs::exists(root / "yarn.lock", ec)) web.packageManager = PackageManager::Yarn;
    else if (fs::exists(root / "bun.lockb", ec) || fs::exists(root / "bun.lock", ec)) web.packageManager = PackageManager::Bun;

    std::ifstream in(root / "package.json");
    if (!in) return web;
    nlohmann::ordered_json pkg; // keeps the scripts in package.json's order
    try {
        pkg = nlohmann::ordered_json::parse(in);
    } catch (const std::exception&) {
        return web;
    }
    if (!pkg.is_object()) return web;
    web.hasPackageJson = true;

    for (const char* section : {"dependencies", "devDependencies"})
        if (auto it = pkg.find(section); it != pkg.end() && it->is_object())
            for (const auto& [name, version] : it->items()) web.dependencies.insert(name);
    if (auto it = pkg.find("scripts"); it != pkg.end() && it->is_object())
        for (const auto& [name, command] : it->items())
            if (command.is_string()) web.scripts.emplace_back(name, command.get<std::string>());
    if (auto it = pkg.find("packageManager"); it != pkg.end() && it->is_string()) {
        const std::string pm = it->get<std::string>(); // e.g. "pnpm@9.1.0"
        if (pm.starts_with("pnpm")) web.packageManager = PackageManager::Pnpm;
        else if (pm.starts_with("yarn")) web.packageManager = PackageManager::Yarn;
        else if (pm.starts_with("bun")) web.packageManager = PackageManager::Bun;
        else if (pm.starts_with("npm")) web.packageManager = PackageManager::Npm;
    }

    auto has = [&web](const char* dep) { return web.dependencies.count(dep) > 0; };
    if (has("@sveltejs/kit")) web.flavor = WebFlavor::SvelteKit;
    else if (has("svelte") && has("vite")) web.flavor = WebFlavor::Svelte;
    else if (has("vite")) web.flavor = WebFlavor::Vite;
    return web;
}

bool WebProject::hasScript(const std::string& name) const {
    for (const auto& [script, command] : scripts)
        if (script == name) return true;
    return false;
}

const char* WebProject::label() const {
    switch (flavor) {
    case WebFlavor::SvelteKit: return "SvelteKit";
    case WebFlavor::Svelte: return "Svelte";
    case WebFlavor::Vite: return "Vite";
    case WebFlavor::Plain: return "TypeScript";
    }
    return "TypeScript";
}

std::string WebProject::packageManagerName() const {
    switch (packageManager) {
    case PackageManager::Npm: return "npm";
    case PackageManager::Pnpm: return "pnpm";
    case PackageManager::Yarn: return "yarn";
    case PackageManager::Bun: return "bun";
    }
    return "npm";
}

std::vector<std::string> WebProject::installCommand() const {
    return {packageManagerName(), "install"};
}

std::vector<std::string> WebProject::runScriptCommand(const std::string& script,
                                                      const std::vector<std::string>& args) const {
    std::vector<std::string> argv{packageManagerName(), "run", script};
    // npm needs "--" before arguments meant for the script; the others pass them on as they are.
    if (!args.empty() && packageManager == PackageManager::Npm) argv.push_back("--");
    argv.insert(argv.end(), args.begin(), args.end());
    return argv;
}

fs::path nodeBin(const fs::path& root, const std::string& name) {
    std::error_code ec;
    const fs::path entry = root / "node_modules" / ".bin" / name;
    const fs::path target = fs::canonical(entry, ec);
    if (ec) return {};
    // npm, yarn and bun make .bin entries symlinks to the package's script (which has a node shebang).
    // pnpm writes a shell script instead that ends in `exec node "$basedir/../vite/bin/vite.js" "$@"`
    // ($basedir being .bin): node can't run that, so run the script it names.
    std::ifstream in(target, std::ios::binary);
    std::string head(4096, '\0');
    in.read(head.data(), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<size_t>(in.gcount()));
    if (!head.starts_with("#!/bin/sh") && !head.starts_with("#!/usr/bin/env sh") && !head.starts_with("#!/bin/bash"))
        return target;
    const std::string marker = "\"$basedir/";
    for (size_t at = head.find(marker); at != std::string::npos; at = head.find(marker, at + 1)) {
        const size_t start = at + marker.size(), end = head.find('"', start);
        if (end == std::string::npos) break;
        const std::string rel = head.substr(start, end - start);
        if (rel == "node" || rel.empty()) continue; // `"$basedir/node"`: a node next to the shim, not the script
        const fs::path script = fs::canonical(entry.parent_path() / rel, ec);
        if (!ec && fs::is_regular_file(script, ec)) return script;
    }
    return {}; // a wrapper we can't see through: callers fall back to the package manager
}

std::vector<std::string> viteDevCommand(const WebProject& web, const fs::path& root, const std::string& node,
                                        int port) {
    const std::vector<std::string> args{"--port", std::to_string(port), "--strictPort", "--host", "127.0.0.1"};
    std::string dev;
    for (const auto& [name, command] : web.scripts)
        if (name == "dev") dev = command;
    const fs::path vite = nodeBin(root, "vite");
    if (!vite.empty() && (dev.empty() || dev == "vite" || dev == "vite dev")) {
        std::vector<std::string> argv{node, vite.string(), "dev"};
        argv.insert(argv.end(), args.begin(), args.end());
        return argv;
    }
    return web.runScriptCommand("dev", args);
}

std::string viteReadyUrl(std::string_view line) {
    const std::string text = stripAnsi(line);
    const auto local = text.find("Local:");
    if (local == std::string::npos) return {};
    const auto start = text.find("http", local);
    if (start == std::string::npos) return {};
    auto end = text.find_first_of(" \t\r\n", start);
    return text.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

std::vector<Command> svelteCheckCommands(const WebProject& web, const fs::path& root, const std::string& node) {
    const fs::path check = nodeBin(root, "svelte-check");
    if (check.empty()) return {};
    std::vector<Command> out;
    // Generates .svelte-kit/tsconfig.json and the $types that svelte-check needs.
    if (const fs::path kit = nodeBin(root, "svelte-kit"); web.flavor == WebFlavor::SvelteKit && !kit.empty())
        out.push_back({{node, kit.string(), "sync"}, root});
    std::vector<std::string> argv{node, check.string(), "--output", "machine-verbose"};
    std::error_code ec;
    if (fs::exists(root / "tsconfig.json", ec)) argv.insert(argv.end(), {"--tsconfig", "./tsconfig.json"});
    out.push_back({argv, root});
    return out;
}

std::vector<std::string> svCreateCommand(const std::string& name) {
    return {"npx", "--yes", "sv@1.1.1", "create", name, "--template", "minimal", "--types", "ts", "--no-add-ons",
            "--install", "npm"};
}

int findFreePort(int from) {
    for (int port = from; port < from + 100 && port < 65536; ++port) {
        const int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return 0;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        const bool taken = bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0;
        close(fd);
        if (!taken) return port;
    }
    return 0;
}

} // namespace kevc
