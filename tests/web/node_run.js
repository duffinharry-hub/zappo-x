// Runs the browser build in node: unpacks the card, starts Zappo, presses
// buttons, and writes raw screenshots (640x480 RGBA) for check.py.
// usage: node node_run.js BUILD_WEB_DIR OUT_DIR "steps"   steps: a b down... wait:N shot:NAME
const fs = require("fs"), path = require("path");
globalThis.window = globalThis;
globalThis.performance = globalThis.performance || require("perf_hooks").performance;
require(path.resolve(__dirname, "../../web/zappo-runtime.js"));
const [dir, out, steps] = process.argv.slice(2);
const B = { up: 0, down: 1, left: 2, right: 3, a: 4, b: 5, x: 6, y: 7, l1: 8, r1: 9, l2: 10, r2: 11, select: 12, start: 13, menu: 14 };
function unpack(fsys, dir) {
  for (const f of JSON.parse(fs.readFileSync(path.join(dir, "card/index.json"))))
    fsys.writeFile(f.p, f.b !== undefined ? new Uint8Array(Buffer.from(f.b, "base64")) : new Uint8Array(fs.readFileSync(path.join(dir, "card", f.f))));
}
(async () => {
  const { MemFS, Zappo } = window.ZappoRuntime;
  const mem = new MemFS();
  unpack(mem, dir);
  let last = null;
  const canvas = { getContext: () => ({ createImageData: (w, h) => ({ data: new Uint8ClampedArray(w * h * 4) }), putImageData: (img) => { last = img; } }) };
  const sound = { ch: 2, open() {}, push() {}, queuedMs: () => 0, pause() {}, stop() {} };
  const z = new Zappo({ fs: mem, canvas, sound, log: (s) => console.log("  log:", s) });
  await z.load(fs.readFileSync(path.join(dir, "zappo.wasm")));
  const t0 = performance.now();
  await z.start();
  console.log("started in", (performance.now() - t0).toFixed(0), "ms");
  fs.mkdirSync(out, { recursive: true });
  const run = (n) => { for (let i = 0; i < n; i++) if (!z.frame()) return false; return true; };
  run(30);
  for (const s of (steps || "").split(/\s+/).filter(Boolean)) {
    if (s.startsWith("wait:")) run(+s.slice(5));
    else if (s.startsWith("shot:")) { fs.writeFileSync(path.join(out, s.slice(5) + ".rgba"), Buffer.from(last.data.buffer)); console.log("shot", s.slice(5)); }
    else { z.key(B[s], 1); run(3); z.key(B[s], 0); run(12); }
    if (!z.running) { console.log("finished, state", z.state, "launched:", JSON.stringify(z.launched)); break; }
  }
  const ms = z.frameMs.slice().sort((a, b) => a - b);
  console.log("frames", ms.length, "median ms", ms[ms.length >> 1]?.toFixed(2), "p95", ms[Math.floor(ms.length * 0.95)]?.toFixed(2));
})().catch((e) => { console.error("FAILED", e); process.exit(1); });
