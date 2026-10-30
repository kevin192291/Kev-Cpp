// Tests for the Kev-C++ dev server: node --test tools/devserver/devserver.test.mjs
import { test, before, after } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, mkdirSync, writeFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { request } from "node:http";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import { startServer } from "./devserver.mjs";

const SCRIPT = fileURLToPath(new URL("./devserver.mjs", import.meta.url));
let root, server, base;

before(async () => {
  root = mkdtempSync(join(tmpdir(), "kevc-devserver-"));
  mkdirSync(join(root, "src"));
  mkdirSync(join(root, "node_modules"));
  writeFileSync(join(root, "index.html"), "<!doctype html><html><body><h1>hi</h1></body></html>");
  writeFileSync(join(root, "src/main.ts"),
    'import { greet } from "./greet";\nimport { helper } from "./util.js";\nimport _ from "lodash";\n' +
    "const n: number = 1;\nconsole.log(greet(\"x\"), helper(n), _);\n");
  writeFileSync(join(root, "src/greet.ts"), "export function greet(name: string): string { return `hi ${name}`; }\n");
  writeFileSync(join(root, "src/util.ts"), "export const helper = (x: number) => x * 2;\n");
  writeFileSync(join(root, "src/broken.ts"), "const = ;\n");
  writeFileSync(join(root, "style.css"), "body { color: red; }\n");
  writeFileSync(join(tmpdir(), "kevc-devserver-secret.txt"), "secret");
  server = startServer({ root, port: 0, host: "127.0.0.1" }, () => {});
  await new Promise((r) => server.on("listening", r));
  base = `http://127.0.0.1:${server.address().port}`;
});

after(() => {
  server.close();
  rmSync(root, { recursive: true, force: true });
  rmSync(join(tmpdir(), "kevc-devserver-secret.txt"), { force: true });
});

// Raw request so ".." isn't normalised away by a URL parser.
function get(path) {
  return new Promise((resolve, reject) => {
    const { port } = server.address();
    const req = request({ host: "127.0.0.1", port, path, method: "GET" }, (res) => {
      let body = "";
      res.on("data", (c) => (body += c));
      res.on("end", () => resolve({ status: res.statusCode, type: res.headers["content-type"], body }));
    });
    req.on("error", reject);
    req.end();
  });
}

test("listens on loopback only", () => {
  assert.equal(server.address().address, "127.0.0.1");
});

test("serves index.html with the live-reload client injected before </body>", async () => {
  const r = await get("/");
  assert.equal(r.status, 200);
  assert.match(r.type, /text\/html/);
  assert.match(r.body, /<h1>hi<\/h1><script type="module">[\s\S]*EventSource\("\/__kevc\/reload"\)[\s\S]*<\/script>\n<\/body>/);
});

test("transpiles TypeScript and strips types", async () => {
  const r = await get("/src/greet.ts");
  assert.equal(r.status, 200);
  assert.match(r.type, /text\/javascript/);
  assert.match(r.body, /export function greet\(name\) \{/);
  assert.doesNotMatch(r.body, /: string/);
  assert.match(r.body, /sourceMappingURL=data:application\/json/);
});

test("resolves extensionless and .js imports to .ts files", async () => {
  const extless = await get("/src/greet");
  assert.equal(extless.status, 200);
  assert.match(extless.body, /function greet/);
  const js = await get("/src/util.js");
  assert.equal(js.status, 200);
  assert.match(js.body, /helper = \(x\) => x \* 2/);
});

test("rewrites bare npm imports to an explanatory module", async () => {
  const main = await get("/src/main.ts");
  assert.match(main.body, /from "\/__kevc\/bare\/lodash"/);
  assert.match(main.body, /from "\.\/greet"/); // relative imports untouched
  const bare = await get("/__kevc/bare/lodash");
  assert.match(bare.body, /throw new Error\(.*npm package \\"lodash\\" is not supported/);
});

test("syntax errors become a module that throws with the location", async () => {
  const r = await get("/src/broken.ts");
  assert.equal(r.status, 200);
  assert.match(r.body, /throw new SyntaxError\(".*src\/broken\.ts:1:7: Variable declaration expected/);
});

test("refuses paths outside the project", async () => {
  assert.equal((await get("/../kevc-devserver-secret.txt")).status, 403);
  assert.equal((await get("/src/../../kevc-devserver-secret.txt")).status, 403);
  assert.equal((await get("/%2e%2e/kevc-devserver-secret.txt")).status, 403);
});

test("static files get their MIME type; missing files 404", async () => {
  const css = await get("/style.css");
  assert.equal(css.status, 200);
  assert.match(css.type, /text\/css/);
  assert.equal((await get("/nope.js")).status, 404);
});

test("pushes a reload event when a file changes", async () => {
  const { port } = server.address();
  const event = await new Promise((resolve, reject) => {
    const req = request({ host: "127.0.0.1", port, path: "/__kevc/reload" }, (res) => {
      let buffer = "";
      res.on("data", (chunk) => {
        buffer += chunk;
        if (buffer.includes(": connected")) writeFileSync(join(root, "src/greet.ts"), "export const greet = 1;\n");
        if (buffer.includes("event: reload")) {
          req.destroy();
          resolve(buffer);
        }
      });
    });
    req.on("error", reject);
    req.end();
    setTimeout(() => reject(new Error("no reload event within 3s")), 3000);
  });
  assert.match(event, /event: reload\ndata: src\/greet\.ts/);
});

test("ignores changes inside node_modules", async () => {
  await new Promise((r) => setTimeout(r, 500)); // let reloads from earlier tests' writes settle
  const { port } = server.address();
  const got = await new Promise((resolve) => {
    const req = request({ host: "127.0.0.1", port, path: "/__kevc/reload" }, (res) => {
      let buffer = "";
      res.on("data", (chunk) => {
        buffer += chunk;
        if (buffer.includes(": connected")) writeFileSync(join(root, "node_modules/x.js"), "1");
        if (buffer.includes("event: reload")) resolve(true);
      });
    });
    req.end();
    setTimeout(() => {
      req.destroy();
      resolve(false);
    }, 700);
  });
  assert.equal(got, false);
});

test("exits with code 2 and a clear message when the port is taken", async () => {
  const { port } = server.address();
  const child = spawn(process.execPath, [SCRIPT, "--root", root, "--port", String(port)]);
  let stderr = "";
  child.stderr.on("data", (c) => (stderr += c));
  const code = await new Promise((r) => child.on("exit", r));
  assert.equal(code, 2);
  assert.match(stderr, new RegExp(`Port ${port} is already in use`));
});
