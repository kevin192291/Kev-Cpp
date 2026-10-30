#!/usr/bin/env node
// Kev-C++ TypeScript dev server.
//
//   node devserver.mjs --root <project dir> [--port 5465] [--host 127.0.0.1]
//
// Serves the project folder, converts .ts/.tsx to JavaScript on request (type-only stripping via
// ts.transpileModule, so it is fast; type errors come from Compile / tsc --noEmit), and reloads open
// pages when files change. Only listens on the loopback interface.
//
// Prints "KEVC_DEVSERVER_READY <url>" once listening. Exits with code 2 if the port is taken.

import { createServer } from "node:http";
import { createRequire } from "node:module";
import { existsSync, statSync, readFileSync, watch } from "node:fs";
import { extname, join, resolve, sep, relative } from "node:path";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const here = fileURLToPath(new URL(".", import.meta.url));
const ts = require(join(here, "..", "node_modules", "typescript"));

const RELOAD_PATH = "/__kevc/reload";
const BARE_PREFIX = "/__kevc/bare/";
const TS_EXTENSIONS = [".ts", ".tsx", ".mts", ".cts"];
// Tried, in order, for an import without an extension (or a .js import of a .ts file).
const RESOLVE_SUFFIXES = [".ts", ".tsx", ".js", ".mjs", "/index.ts", "/index.tsx", "/index.js"];
const IGNORED_DIRS = new Set(["node_modules", "build", ".git", ".cache"]);

const MIME = {
  ".html": "text/html; charset=utf-8",
  ".htm": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".mjs": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".map": "application/json; charset=utf-8",
  ".svg": "image/svg+xml",
  ".png": "image/png",
  ".jpg": "image/jpeg",
  ".jpeg": "image/jpeg",
  ".gif": "image/gif",
  ".webp": "image/webp",
  ".ico": "image/x-icon",
  ".wasm": "application/wasm",
  ".txt": "text/plain; charset=utf-8",
  ".woff": "font/woff",
  ".woff2": "font/woff2",
};

const RELOAD_CLIENT = `<script type="module">
// Injected by the Kev-C++ dev server: reload when project files change.
const source = new EventSource("${RELOAD_PATH}");
source.addEventListener("reload", () => location.reload());
</script>`;

function parseArgs(argv) {
  const args = { port: 5465, host: "127.0.0.1", root: process.cwd() };
  for (let i = 0; i < argv.length; i++) {
    const [key, value] = [argv[i], argv[i + 1]];
    if (key === "--root") { args.root = value; i++; }
    else if (key === "--port") { args.port = Number(value); i++; }
    else if (key === "--host") { args.host = value; i++; }
  }
  args.root = resolve(args.root);
  return args;
}

function isFile(path) {
  try { return statSync(path).isFile(); } catch { return false; }
}

function isDirectory(path) {
  try { return statSync(path).isDirectory(); } catch { return false; }
}

// Maps a URL path to a file under root, or null if it would escape root.
function insideRoot(root, urlPath) {
  const full = resolve(root, "." + urlPath);
  return full === root || full.startsWith(root + sep) ? full : null;
}

// Finds the file a browser request refers to, following TypeScript's import conventions.
export function resolveRequest(root, urlPath) {
  const full = insideRoot(root, urlPath);
  if (!full) return { forbidden: true };
  if (isFile(full)) return { file: full };
  if (isDirectory(full) && isFile(join(full, "index.html"))) return { file: join(full, "index.html") };
  // "./greet.js" written in TypeScript source refers to greet.ts.
  const base = /\.(m?js)$/.test(full) ? full.replace(/\.(m?js)$/, "") : full;
  for (const suffix of RESOLVE_SUFFIXES) {
    if (isFile(base + suffix)) return { file: base + suffix };
  }
  return {};
}

function isBare(specifier) {
  return !(specifier.startsWith("./") || specifier.startsWith("../") || specifier.startsWith("/") ||
           /^[a-z][a-z0-9+.-]*:/i.test(specifier));
}

// Rewrites bare npm imports ("lodash") to a URL that explains they aren't supported, instead of the
// browser's opaque "Failed to resolve module specifier".
function bareImportRewriter(context) {
  const f = context.factory;
  const rewrite = (spec) =>
    ts.isStringLiteral(spec) && isBare(spec.text) ? f.createStringLiteral(BARE_PREFIX + spec.text) : spec;
  const visit = (node) => {
    if (ts.isImportDeclaration(node) && node.moduleSpecifier) {
      return f.updateImportDeclaration(node, node.modifiers, node.importClause, rewrite(node.moduleSpecifier),
                                       node.attributes);
    }
    if (ts.isExportDeclaration(node) && node.moduleSpecifier) {
      return f.updateExportDeclaration(node, node.modifiers, node.isTypeOnly, node.exportClause,
                                       rewrite(node.moduleSpecifier), node.attributes);
    }
    if (ts.isCallExpression(node) && node.expression.kind === ts.SyntaxKind.ImportKeyword && node.arguments.length) {
      return f.updateCallExpression(node, node.expression, node.typeArguments,
                                    [rewrite(node.arguments[0]), ...node.arguments.slice(1)]);
    }
    return ts.visitEachChild(node, visit, context);
  };
  return (sourceFile) => ts.visitNode(sourceFile, visit);
}

export function transpile(file, source) {
  const output = ts.transpileModule(source, {
    fileName: file,
    reportDiagnostics: true,
    compilerOptions: {
      module: ts.ModuleKind.ESNext,
      target: ts.ScriptTarget.ES2022,
      jsx: ts.JsxEmit.ReactJSX,
      inlineSourceMap: true,
      inlineSources: true,
      verbatimModuleSyntax: false,
    },
    transformers: { before: [bareImportRewriter] },
  });
  const errors = (output.diagnostics ?? []).filter((d) => d.category === ts.DiagnosticCategory.Error);
  if (errors.length) {
    const messages = errors.map((d) => {
      const where = d.file && d.start !== undefined ? d.file.getLineAndCharacterOfPosition(d.start) : null;
      const text = ts.flattenDiagnosticMessageText(d.messageText, "\n");
      return where ? `${file}:${where.line + 1}:${where.character + 1}: ${text}` : text;
    });
    return { error: messages.join("\n") };
  }
  return { code: output.outputText };
}

function injectReloadClient(html) {
  const at = html.search(/<\/body>/i);
  return at >= 0 ? html.slice(0, at) + RELOAD_CLIENT + "\n" + html.slice(at) : html + "\n" + RELOAD_CLIENT;
}

export function startServer({ root, port, host }, log = console.log) {
  const clients = new Set();

  const server = createServer((req, res) => {
    const started = Date.now();
    const done = (status, type, body) => {
      res.writeHead(status, { "Content-Type": type, "Cache-Control": "no-store" });
      res.end(body);
      log(`${req.method} ${req.url} ${status} ${Date.now() - started}ms`);
    };
    let urlPath;
    try {
      // URL parsing would quietly turn "/../x" into "/x"; refuse ".." segments outright instead.
      const rawPath = req.url.split("?")[0];
      if (rawPath.split("/").some((segment) => decodeURIComponent(segment) === "..")) {
        return done(403, "text/plain; charset=utf-8", "Forbidden: outside the project folder");
      }
      urlPath = decodeURIComponent(new URL(req.url, "http://localhost").pathname);
    } catch {
      return done(400, "text/plain; charset=utf-8", "Bad request");
    }
    if (req.method !== "GET" && req.method !== "HEAD") return done(405, "text/plain; charset=utf-8", "Method not allowed");

    if (urlPath === RELOAD_PATH) {
      res.writeHead(200, { "Content-Type": "text/event-stream", "Cache-Control": "no-store", Connection: "keep-alive" });
      res.write(": connected\n\n");
      clients.add(res);
      req.on("close", () => clients.delete(res));
      return;
    }
    if (urlPath.startsWith(BARE_PREFIX)) {
      const name = urlPath.slice(BARE_PREFIX.length);
      const message = `Kev-C++ dev server: importing the npm package "${name}" is not supported yet. ` +
                      `Use a relative import (./file) or a full URL (e.g. https://esm.sh/${name}).`;
      return done(200, MIME[".js"], `throw new Error(${JSON.stringify(message)});\n`);
    }

    const found = resolveRequest(root, urlPath);
    if (found.forbidden) return done(403, "text/plain; charset=utf-8", "Forbidden: outside the project folder");
    if (!found.file) return done(404, "text/plain; charset=utf-8", `Not found: ${urlPath}`);

    const ext = extname(found.file).toLowerCase();
    try {
      if (TS_EXTENSIONS.includes(ext)) {
        const result = transpile(relative(root, found.file), readFileSync(found.file, "utf8"));
        if (result.error) {
          log(`error: ${result.error}`);
          return done(200, MIME[".js"], `throw new SyntaxError(${JSON.stringify(result.error)});\n`);
        }
        return done(200, MIME[".js"], result.code);
      }
      if (ext === ".html" || ext === ".htm") {
        return done(200, MIME[".html"], injectReloadClient(readFileSync(found.file, "utf8")));
      }
      return done(200, MIME[ext] ?? "application/octet-stream", readFileSync(found.file));
    } catch (e) {
      return done(500, "text/plain; charset=utf-8", `Error: ${e.message}`);
    }
  });

  // Live reload: one event per burst of changes (editors often write several times per save).
  let timer = null;
  let watcher = null;
  try {
    watcher = watch(root, { recursive: true }, (_event, name) => {
      if (name && String(name).split(sep).some((part) => IGNORED_DIRS.has(part) || part.startsWith("."))) return;
      clearTimeout(timer);
      timer = setTimeout(() => {
        for (const client of clients) client.write(`event: reload\ndata: ${name ?? ""}\n\n`);
        if (clients.size) log(`reload: ${name} changed, ${clients.size} page(s) reloading`);
      }, 120);
    });
  } catch (e) {
    log(`warning: live reload is off (${e.message})`);
  }

  server.on("close", () => {
    watcher?.close();
    clearTimeout(timer);
  });
  // SSE connections stay open; end them so close() can finish.
  const originalClose = server.close.bind(server);
  server.close = (cb) => {
    for (const client of clients) client.end();
    clients.clear();
    return originalClose(cb);
  };

  server.listen(port, host);
  return server;
}

// Run as a program (not when imported by the tests).
if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const args = parseArgs(process.argv.slice(2));
  if (!existsSync(args.root)) {
    console.error(`Project folder not found: ${args.root}`);
    process.exit(1);
  }
  const server = startServer(args);
  server.on("listening", () => {
    const { port } = server.address();
    console.log(`KEVC_DEVSERVER_READY http://${args.host}:${port}/`);
    console.log(`Serving ${args.root}`);
  });
  server.on("error", (e) => {
    if (e.code === "EADDRINUSE") {
      console.error(`Port ${args.port} is already in use. Stop the other server or pick another port.`);
      process.exit(2);
    }
    console.error(`Dev server error: ${e.message}`);
    process.exit(1);
  });
  const stop = () => server.close(() => process.exit(0));
  process.on("SIGTERM", stop);
  process.on("SIGINT", stop);
  // Started by the IDE with a pipe on stdin: if the IDE goes away (even by crashing) the pipe
  // closes, and we stop rather than keep holding the port.
  if (!process.stdin.isTTY) {
    process.stdin.on("end", stop);
    process.stdin.on("close", stop);
    process.stdin.resume();
  }
}
