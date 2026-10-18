/* Zappo X in the browser: the runtime around zappo.wasm.
 *
 *  - a small in-memory file system (the kit's themes, fonts and art, plus a
 *    few test files) behind the WASI calls the C library makes;
 *  - the "zappo" imports: draw the 640x480 picture, the clock, battery,
 *    brightness, the sound (Web Audio) and what a game launch would start.
 *
 * Plain script, no libraries.  Exposes window.ZappoRuntime. */
(function () {
  "use strict";

  /* ------------------------------------------------------------ files */

  const DIR = 3, FILE = 4;
  let inoNext = 10;

  function mkdir() { return { type: DIR, ino: inoNext++, kids: new Map(), mtime: Date.now() }; }
  function mkfile(data) { return { type: FILE, ino: inoNext++, data: data || new Uint8Array(0), size: data ? data.length : 0, mtime: Date.now() }; }

  class MemFS {
    constructor() { this.root = mkdir(); this.changed = new Set(); }
    split(path) { return path.split("/").filter(p => p && p !== "."); }
    lookup(path, from) {
      let node = from || this.root;
      const stack = [];
      for (const part of this.split(path)) {
        if (part === "..") { node = stack.length ? stack.pop() : this.root; continue; }
        if (node.type !== DIR) return null;
        const next = node.kids.get(part);
        if (!next) return null;
        stack.push(node);
        node = next;
      }
      return node;
    }
    parentOf(path, from) {
      const parts = this.split(path);
      const name = parts.pop();
      const dir = parts.length ? this.lookup(parts.join("/"), from) : (from || this.root);
      return { dir, name };
    }
    mkdirp(path) {
      let node = this.root;
      for (const part of this.split(path)) {
        let next = node.kids.get(part);
        if (!next) { next = mkdir(); node.kids.set(part, next); }
        node = next;
      }
      return node;
    }
    writeFile(path, bytes) {
      const parts = this.split(path);
      const name = parts.pop();
      const dir = this.mkdirp(parts.join("/"));
      const f = mkfile(bytes);
      dir.kids.set(name, f);
      return f;
    }
    readFile(path) {
      const n = this.lookup(path);
      return n && n.type === FILE ? n.data.subarray(0, n.size) : null;
    }
    absPath(node, target = this.root, prefix = "") {
      if (target === node) return prefix || "/";
      if (target.type !== DIR) return null;
      for (const [k, v] of target.kids) {
        const r = this.absPath(node, v, prefix + "/" + k);
        if (r) return r;
      }
      return null;
    }
  }

  /* ------------------------------------------------------------- WASI */

  const E = { OK: 0, ACCES: 2, BADF: 8, EXIST: 20, INVAL: 28, ISDIR: 31, NOENT: 44, NOSYS: 52, NOTDIR: 54, NOTEMPTY: 55, SPIPE: 70 };
  const ALL = 0xffffffffffffffffn;

  class ExitStatus extends Error { constructor(code) { super("exit " + code); this.code = code; } }

  function makeWasi(fs, getMem, log) {
    const fds = new Map();
    fds.set(0, { kind: "tty" });
    fds.set(1, { kind: "tty", out: "log" });
    fds.set(2, { kind: "tty", out: "err" });
    fds.set(3, { kind: "node", node: fs.root, pre: "/", pos: 0 });
    let nextFd = 4;
    const tdec = new TextDecoder();
    const tenc = new TextEncoder();
    const view = () => new DataView(getMem().buffer);
    const bytes = () => new Uint8Array(getMem().buffer);
    const str = (p, n) => tdec.decode(bytes().slice(p, p + n));
    const lines = { log: "", err: "" };

    function fdNode(fd) { const f = fds.get(fd); return f && f.kind === "node" ? f : null; }
    function grow(node, need) {
      if (need <= node.data.length) return;
      let cap = Math.max(need, node.data.length * 2, 4096);
      const d = new Uint8Array(cap);
      d.set(node.data.subarray(0, node.size));
      node.data = d;
    }
    function stat(ptr, node) {
      const v = view();
      v.setBigUint64(ptr, 1n, true);
      v.setBigUint64(ptr + 8, BigInt(node.ino), true);
      v.setUint8(ptr + 16, node.type);
      v.setBigUint64(ptr + 24, 1n, true);
      v.setBigUint64(ptr + 32, BigInt(node.type === FILE ? node.size : 4096), true);
      const t = BigInt(Math.floor(node.mtime)) * 1000000n;
      v.setBigUint64(ptr + 40, t, true);
      v.setBigUint64(ptr + 48, t, true);
      v.setBigUint64(ptr + 56, t, true);
    }
    function markChanged(f) {
      if (!f || !f.node) return;
      const p = fs.absPath(f.node);
      if (p) fs.changed.add(p);
    }

    const api = {
      args_get: () => E.OK,
      args_sizes_get: (argc, size) => { view().setUint32(argc, 0, true); view().setUint32(size, 0, true); return E.OK; },
      environ_get: () => E.OK,
      environ_sizes_get: (c, s) => { view().setUint32(c, 0, true); view().setUint32(s, 0, true); return E.OK; },
      clock_res_get: (id, out) => { view().setBigUint64(out, 1000n, true); return E.OK; },
      clock_time_get: (id, prec, out) => {
        const ns = id === 0 ? BigInt(Date.now()) * 1000000n : BigInt(Math.floor(performance.now() * 1e6));
        view().setBigUint64(out, ns, true);
        return E.OK;
      },
      fd_advise: () => E.OK,
      fd_allocate: () => E.OK,
      fd_datasync: () => E.OK,
      fd_sync: () => E.OK,
      fd_close: (fd) => {
        const f = fds.get(fd);
        if (!f) return E.BADF;
        if (f.dirty) markChanged(f);
        if (fd > 3) fds.delete(fd);
        return E.OK;
      },
      fd_fdstat_get: (fd, out) => {
        const f = fds.get(fd);
        if (!f) return E.BADF;
        const v = view();
        v.setUint8(out, f.kind === "tty" ? 2 : f.node.type);
        v.setUint16(out + 2, f.append ? 1 : 0, true);
        v.setBigUint64(out + 8, ALL, true);
        v.setBigUint64(out + 16, ALL, true);
        return E.OK;
      },
      fd_fdstat_set_flags: (fd, flags) => { const f = fds.get(fd); if (!f) return E.BADF; f.append = !!(flags & 1); return E.OK; },
      fd_fdstat_set_rights: () => E.OK,
      fd_filestat_get: (fd, out) => {
        const f = fds.get(fd);
        if (!f) return E.BADF;
        if (f.kind === "tty") { stat(out, { ino: fd, type: 2, size: 0, mtime: 0 }); return E.OK; }
        stat(out, f.node);
        return E.OK;
      },
      fd_filestat_set_size: (fd, size) => {
        const f = fdNode(fd);
        if (!f || f.node.type !== FILE) return E.BADF;
        const n = Number(size);
        grow(f.node, n);
        if (n > f.node.size) f.node.data.fill(0, f.node.size, n);
        f.node.size = n;
        f.dirty = true;
        return E.OK;
      },
      fd_filestat_set_times: () => E.OK,
      fd_prestat_get: (fd, out) => {
        const f = fds.get(fd);
        if (!f || !f.pre) return E.BADF;
        view().setUint8(out, 0);
        view().setUint32(out + 4, tenc.encode(f.pre).length, true);
        return E.OK;
      },
      fd_prestat_dir_name: (fd, p, n) => {
        const f = fds.get(fd);
        if (!f || !f.pre) return E.BADF;
        bytes().set(tenc.encode(f.pre).subarray(0, n), p);
        return E.OK;
      },
      fd_read: (fd, iovs, n, outp) => {
        const f = fdNode(fd);
        if (!f) { view().setUint32(outp, 0, true); return fd === 0 ? E.OK : E.BADF; }
        if (f.node.type !== FILE) return E.ISDIR;
        let total = 0;
        const v = view();
        for (let i = 0; i < n; i++) {
          const p = v.getUint32(iovs + i * 8, true), len = v.getUint32(iovs + i * 8 + 4, true);
          const avail = Math.max(0, Math.min(len, f.node.size - f.pos));
          bytes().set(f.node.data.subarray(f.pos, f.pos + avail), p);
          f.pos += avail;
          total += avail;
          if (avail < len) break;
        }
        view().setUint32(outp, total, true);
        return E.OK;
      },
      fd_pread: (fd, iovs, n, off, outp) => {
        const f = fdNode(fd);
        if (!f) return E.BADF;
        const save = f.pos;
        f.pos = Number(off);
        const r = api.fd_read(fd, iovs, n, outp);
        f.pos = save;
        return r;
      },
      fd_write: (fd, iovs, n, outp) => {
        const v = view();
        const f = fds.get(fd);
        if (!f) return E.BADF;
        let total = 0;
        for (let i = 0; i < n; i++) {
          const p = v.getUint32(iovs + i * 8, true), len = v.getUint32(iovs + i * 8 + 4, true);
          if (f.kind === "tty") {
            lines[f.out || "log"] += str(p, len);
            let k;
            while ((k = lines[f.out || "log"].indexOf("\n")) >= 0) {
              log(lines[f.out || "log"].slice(0, k), f.out === "err");
              lines[f.out || "log"] = lines[f.out || "log"].slice(k + 1);
            }
          } else {
            const node = f.node;
            if (node.type !== FILE) return E.ISDIR;
            if (f.append) f.pos = node.size;
            grow(node, f.pos + len);
            node.data.set(bytes().subarray(p, p + len), f.pos);
            f.pos += len;
            if (f.pos > node.size) node.size = f.pos;
            node.mtime = Date.now();
            f.dirty = true;
          }
          total += len;
        }
        v.setUint32(outp, total, true);
        return E.OK;
      },
      fd_pwrite: (fd, iovs, n, off, outp) => {
        const f = fdNode(fd);
        if (!f) return E.BADF;
        const save = f.pos;
        f.pos = Number(off);
        const r = api.fd_write(fd, iovs, n, outp);
        f.pos = save;
        return r;
      },
      fd_seek: (fd, off, whence, outp) => {
        const f = fdNode(fd);
        if (!f) return E.SPIPE;
        const o = Number(off);
        const base = whence === 0 ? 0 : whence === 1 ? f.pos : f.node.size;
        const np = base + o;
        if (np < 0) return E.INVAL;
        f.pos = np;
        view().setBigUint64(outp, BigInt(np), true);
        return E.OK;
      },
      fd_tell: (fd, outp) => { const f = fdNode(fd); if (!f) return E.SPIPE; view().setBigUint64(outp, BigInt(f.pos), true); return E.OK; },
      fd_readdir: (fd, buf, len, cookie, outp) => {
        const f = fdNode(fd);
        if (!f || f.node.type !== DIR) return E.NOTDIR;
        const names = [".", "..", ...f.node.kids.keys()];
        const v = view(), b = bytes();
        let used = 0;
        for (let i = Number(cookie); i < names.length; i++) {
          const name = tenc.encode(names[i]);
          const kid = i < 2 ? f.node : f.node.kids.get(names[i]);
          const ent = new Uint8Array(24 + name.length);
          const dv = new DataView(ent.buffer);
          dv.setBigUint64(0, BigInt(i + 1), true);
          dv.setBigUint64(8, BigInt(kid.ino), true);
          dv.setUint32(16, name.length, true);
          dv.setUint8(20, kid.type);
          ent.set(name, 24);
          const take = Math.min(ent.length, len - used);
          b.set(ent.subarray(0, take), buf + used);
          used += take;
          if (used >= len) break;
        }
        v.setUint32(outp, used, true);
        return E.OK;
      },
      fd_renumber: () => E.NOSYS,
      path_create_directory: (fd, p, n) => {
        const f = fdNode(fd);
        if (!f) return E.BADF;
        const { dir, name } = fs.parentOf(str(p, n), f.node);
        if (!dir || dir.type !== DIR) return E.NOENT;
        if (dir.kids.has(name)) return E.EXIST;
        dir.kids.set(name, mkdir());
        return E.OK;
      },
      path_filestat_get: (fd, flags, p, n, out) => {
        const f = fdNode(fd);
        if (!f) return E.BADF;
        const node = fs.lookup(str(p, n), f.node);
        if (!node) return E.NOENT;
        stat(out, node);
        return E.OK;
      },
      path_filestat_set_times: () => E.OK,
      path_link: () => E.NOSYS,
      path_readlink: () => E.INVAL,
      path_symlink: () => E.NOSYS,
      path_open: (fd, dirflags, p, n, oflags, rb, ri, fdflags, outp) => {
        const f = fdNode(fd);
        if (!f) return E.BADF;
        const path = str(p, n);
        let node = fs.lookup(path, f.node);
        if (node && (oflags & 4)) return E.EXIST;               /* EXCL */
        if (!node) {
          if (!(oflags & 1)) return E.NOENT;                     /* CREAT */
          const { dir, name } = fs.parentOf(path, f.node);
          if (!dir || dir.type !== DIR || !name) return E.NOENT;
          node = mkfile();
          dir.kids.set(name, node);
        }
        if ((oflags & 2) && node.type !== DIR) return E.NOTDIR;  /* DIRECTORY */
        if (oflags & 8) { if (node.type !== FILE) return E.ISDIR; node.size = 0; node.mtime = Date.now(); } /* TRUNC */
        const nf = { kind: "node", node, pos: 0, append: !!(fdflags & 1), dirty: !!(oflags & 9) };
        const id = nextFd++;
        fds.set(id, nf);
        view().setUint32(outp, id, true);
        return E.OK;
      },
      path_remove_directory: (fd, p, n) => {
        const f = fdNode(fd);
        if (!f) return E.BADF;
        const { dir, name } = fs.parentOf(str(p, n), f.node);
        const node = dir && dir.kids.get(name);
        if (!node) return E.NOENT;
        if (node.type !== DIR) return E.NOTDIR;
        if (node.kids.size) return E.NOTEMPTY;
        dir.kids.delete(name);
        return E.OK;
      },
      path_rename: (fd, p, n, fd2, p2, n2) => {
        const a = fdNode(fd), b = fdNode(fd2);
        if (!a || !b) return E.BADF;
        const src = fs.parentOf(str(p, n), a.node), dst = fs.parentOf(str(p2, n2), b.node);
        const node = src.dir && src.dir.kids.get(src.name);
        if (!node) return E.NOENT;
        if (!dst.dir || dst.dir.type !== DIR) return E.NOENT;
        src.dir.kids.delete(src.name);
        dst.dir.kids.set(dst.name, node);
        const ap = fs.absPath(node);
        if (ap) fs.changed.add(ap);
        return E.OK;
      },
      path_unlink_file: (fd, p, n) => {
        const f = fdNode(fd);
        if (!f) return E.BADF;
        const path = str(p, n);
        const { dir, name } = fs.parentOf(path, f.node);
        const node = dir && dir.kids.get(name);
        if (!node) return E.NOENT;
        if (node.type === DIR) return E.ISDIR;
        dir.kids.delete(name);
        return E.OK;
      },
      poll_oneoff: (inp, outp, n, neventsp) => {
        /* only used for sleeping; the page never blocks, so wake at once */
        const v = view();
        for (let i = 0; i < n; i++) {
          const ud = v.getBigUint64(inp + i * 48, true);
          v.setBigUint64(outp + i * 32, ud, true);
          v.setUint16(outp + i * 32 + 8, 0, true);
          v.setUint8(outp + i * 32 + 10, v.getUint8(inp + i * 48 + 8));
        }
        v.setUint32(neventsp, n, true);
        return E.OK;
      },
      proc_exit: (code) => { throw new ExitStatus(code); },
      proc_raise: () => E.NOSYS,
      random_get: (p, n) => { crypto.getRandomValues(bytes().subarray(p, p + n)); return E.OK; },
      sched_yield: () => E.OK,
      sock_accept: () => E.NOSYS, sock_recv: () => E.NOSYS, sock_send: () => E.NOSYS, sock_shutdown: () => E.NOSYS,
    };
    return api;
  }

  /* ------------------------------------------------------------- sound */

  class Sound {
    constructor() { this.ctx = null; this.rate = 44100; this.ch = 2; this.next = 0; this.srcs = []; this.paused = false; this.gain = null; }
    wake() {
      if (!this.ctx) {
        const AC = window.AudioContext || window.webkitAudioContext;
        if (!AC) return;
        this.ctx = new AC();
        this.gain = this.ctx.createGain();
        this.gain.connect(this.ctx.destination);
      }
      if (this.ctx.state === "suspended" && !this.paused) this.ctx.resume();
    }
    open(rate, ch) { this.rate = rate; this.ch = ch; this.next = 0; }
    push(i16, frames) {
      if (!this.ctx) return;
      const ch = this.ch;
      const buf = this.ctx.createBuffer(ch, frames, this.rate);
      for (let c = 0; c < ch; c++) {
        const d = buf.getChannelData(c);
        for (let i = 0; i < frames; i++) d[i] = i16[i * ch + c] / 32768;
      }
      const s = this.ctx.createBufferSource();
      s.buffer = buf;
      s.connect(this.gain);
      const now = this.ctx.currentTime;
      if (this.next < now + 0.03) this.next = now + 0.05;
      s.start(this.next);
      this.next += frames / this.rate;
      this.srcs.push(s);
      s.onended = () => { const k = this.srcs.indexOf(s); if (k >= 0) this.srcs.splice(k, 1); };
    }
    queuedMs() {
      if (!this.ctx) return 0;
      if (this.ctx.state !== "running") return 1000; /* not allowed to play yet: don't decode ahead */
      return Math.max(0, (this.next - this.ctx.currentTime) * 1000);
    }
    pause(p) {
      if (!this.ctx || p === this.paused) return;
      this.paused = p;
      if (p) this.ctx.suspend(); else this.ctx.resume();
    }
    stop(now) {
      if (now) { for (const s of this.srcs) { try { s.stop(); } catch (e) {} } this.srcs = []; }
      this.next = 0;
    }
  }

  /* ---------------------------------------------------------- the app */

  class Zappo {
    constructor(opts) {
      this.fs = opts.fs;
      this.canvas = opts.canvas;
      this.ctx2d = this.canvas.getContext("2d", { alpha: false });
      this.image = this.ctx2d.createImageData(640, 480);
      this.px = new Uint32Array(this.image.data.buffer);
      this.sound = opts.sound;
      this.hooks = opts.hooks || {};
      this.log = opts.log || ((s, err) => (err ? console.warn : console.log)("zappo: " + s));
      this.inst = null;
      this.mem = null;
      this.frameMs = [];
    }
    async load(wasmBytes) {
      this.module = await WebAssembly.compile(wasmBytes);
    }
    async start() {
      const getMem = () => this.mem;
      const wasi = makeWasi(this.fs, getMem, this.log);
      const u8 = () => new Uint8Array(this.mem.buffer);
      const dec = new TextDecoder();
      const z = {
        present: (ptr, w, h) => {
          const src = new Uint32Array(this.mem.buffer, ptr, w * h);
          const px = this.px;
          for (let i = 0; i < src.length; i++) {
            const p = src[i];
            px[i] = 0xff000000 | ((p & 0xff) << 16) | (p & 0xff00) | ((p >>> 16) & 0xff);
          }
          this.ctx2d.putImageData(this.image, 0, 0);
          this.presented = true;
        },
        now_ms: () => performance.now(),
        battery: () => (this.hooks.battery ? this.hooks.battery() : 76),
        charging: () => (this.hooks.charging ? this.hooks.charging() : 0),
        brightness: (lv) => this.hooks.brightness && this.hooks.brightness(lv),
        launch: (p, n) => { this.launched = dec.decode(u8().slice(p, p + n)); },
        power: (reboot) => this.hooks.power && this.hooks.power(reboot),
        describe: (p, n) => {
          const t = new TextEncoder().encode((this.hooks.describe ? this.hooks.describe() : "").slice(0, n - 1));
          u8().set(t, p);
          u8()[p + t.length] = 0;
          return t.length;
        },
        audio_open: (rate, ch) => this.sound.open(rate, ch),
        audio_push: (ptr, frames) => this.sound.push(new Int16Array(this.mem.buffer, ptr, frames * this.sound.ch), frames),
        audio_queued_ms: () => Math.round(this.sound.queuedMs()),
        audio_pause: (p) => this.sound.pause(!!p),
        audio_stop: (now) => this.sound.stop(!!now),
      };
      this.inst = await WebAssembly.instantiate(this.module, { wasi_snapshot_preview1: wasi, zappo: z });
      this.mem = this.inst.exports.memory;
      this.inst.exports._initialize();
      this.launched = null;
      const r = this.inst.exports.zx_start();
      if (r !== 0) throw new Error("Zappo could not start (" + r + ")");
      this.running = true;
    }
    /* one animation frame; false once Zappo has finished (see .state) */
    frame() {
      if (!this.running) return false;
      const t0 = performance.now();
      this.presented = false;
      const go = this.inst.exports.zx_frame();
      const dt = performance.now() - t0;
      if (this.presented) { this.frameMs.push(dt); if (this.frameMs.length > 600) this.frameMs.shift(); }
      if (!go) {
        this.state = this.inst.exports.zx_state();
        this.inst.exports.zx_end();
        this.running = false;
      }
      return go;
    }
    key(button, down) { if (this.running) this.inst.exports.zx_key(button, down ? 1 : 0); }
    tab() { return this.running ? this.inst.exports.zx_tab() : -1; }
    musicPlaying() { return this.running ? this.inst.exports.zx_music_playing() : 0; }
  }

  window.ZappoRuntime = { MemFS, Sound, Zappo, ExitStatus };
})();
