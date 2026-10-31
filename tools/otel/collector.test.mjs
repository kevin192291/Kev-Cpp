// Tests for Kev-C++'s OpenTelemetry collector: node --test tools/otel/collector.test.mjs
import { test, before, after } from "node:test";
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createServer } from "node:net";
import { fileURLToPath } from "node:url";

const SCRIPT = fileURLToPath(new URL("./collector.mjs", import.meta.url));
let child, base;
const lines = [];

function freePort() {
  return new Promise((resolve) => {
    const s = createServer().listen(0, "127.0.0.1", () => {
      const { port } = s.address();
      s.close(() => resolve(port));
    });
  });
}

function waitFor(predicate) {
  return new Promise((resolve, reject) => {
    const started = Date.now();
    const tick = () => {
      const hit = lines.find(predicate);
      if (hit) return resolve(hit);
      if (Date.now() - started > 5000) return reject(new Error("timed out; got: " + lines.join(" | ")));
      setTimeout(tick, 20);
    };
    tick();
  });
}

before(async () => {
  const port = await freePort();
  child = spawn(process.execPath, [SCRIPT, "--port", String(port)], { stdio: ["pipe", "pipe", "inherit"] });
  let partial = "";
  child.stdout.on("data", (d) => {
    partial += d;
    let i;
    while ((i = partial.indexOf("\n")) >= 0) {
      lines.push(partial.slice(0, i));
      partial = partial.slice(i + 1);
    }
  });
  const ready = await waitFor((l) => l.startsWith("KEVC_OTLP_READY "));
  base = ready.slice("KEVC_OTLP_READY ".length);
  assert.equal(base, `http://127.0.0.1:${port}`);
});

after(() => child.kill());

test("a JSON trace export comes out as one KEVC_SPANS line", async () => {
  const body = { resourceSpans: [{ scopeSpans: [{ spans: [{ traceId: "t1", spanId: "a", name: "sveltekit.handle.root" }] }] }] };
  const res = await fetch(base + "/v1/traces", { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify(body) });
  assert.equal(res.status, 200);
  const line = await waitFor((l) => l.startsWith("KEVC_SPANS "));
  assert.deepEqual(JSON.parse(line.slice("KEVC_SPANS ".length)), body);
});

test("protobuf exports are refused with a hint", async () => {
  const res = await fetch(base + "/v1/traces", { method: "POST", headers: { "content-type": "application/x-protobuf" }, body: "x" });
  assert.equal(res.status, 415);
  await waitFor((l) => l.startsWith("KEVC_OTLP_NOTE ") && l.includes("http/json"));
});

test("other paths and methods are not found", async () => {
  assert.equal((await fetch(base + "/v1/metrics", { method: "POST", body: "{}" })).status, 404);
  assert.equal((await fetch(base + "/v1/traces")).status, 404);
});

test("it exits when its stdin closes (the IDE went away)", async () => {
  const exited = new Promise((r) => child.on("exit", r));
  child.stdin.end();
  assert.equal(await exited, 0);
});
