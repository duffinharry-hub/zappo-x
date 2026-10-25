#!/usr/bin/python3
"""Send to Zappo: a small web page on the handheld for sending it files from a
phone or PC on the same Wi-Fi. What arrives goes into /userdata/import, where
Zappo X sorts it (games to their console, songs to Music, films to Videos).

Zappo X starts this while its "Send to Zappo" screen is open and stops it when
you leave, so the handheld only takes files when you ask it to.

  send-to-zappo.py [PORT]      (default 8640)

Writes one line per file received to /tmp/zappo-x-received (Zappo X shows them).
Nothing but the Python standard library; files go to disk as they arrive
(.part until complete), so big films don't need the memory.
"""
import html
import json
import os
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.parse
import zipfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

IMPORT = os.environ.get("ZAPPO_IMPORT", "/userdata/import")
LOG = os.environ.get("ZAPPO_RECEIVED", "/tmp/zappo-x-received")
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8640
HOME = os.environ.get("ZAPPO_HOME", "/userdata/system/zappo-x")   # Zappo X's own folder
UPDATES = os.environ.get("ZAPPO_UPDATES", "/userdata/system/zappo-x-update")  # packages waiting to be installed

PAGE = """<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Send to Zappo</title>
<style>
:root{--bg:#0f1229;--panel:#171b35;--ink:#f3f1e8;--dim:#a2a8c8;--gold:#f5c842;--ok:#5fd38d;--bad:#ff8a7a;color-scheme:dark}
@media (prefers-color-scheme: light){:root{--bg:#eef0f6;--panel:#fff;--ink:#161a33;--dim:#596080;--gold:#94690a;--ok:#1d8a52;--bad:#b3261e;color-scheme:light}}
body{margin:0;background:var(--bg);color:var(--ink);font:17px/1.5 system-ui,-apple-system,"Segoe UI",sans-serif}
main{max-width:640px;margin:0 auto;padding:24px 16px 48px;display:grid;gap:18px}
h1{margin:0;font-size:30px}
h2{margin:18px 0 0;font-size:22px}
a{color:var(--gold)}
code{font-size:15px}
p{margin:0;color:var(--dim)}
.drop{border:2px dashed var(--gold);border-radius:16px;padding:34px 16px;text-align:center;background:var(--panel);cursor:pointer}
.drop.over{background:color-mix(in srgb,var(--gold) 15%,var(--panel))}
.drop b{display:block;font-size:20px;margin-bottom:6px}
.btns{display:flex;gap:10px;flex-wrap:wrap;justify-content:center;margin-top:14px}
button{font:inherit;border:0;border-radius:999px;padding:10px 18px;background:var(--gold);color:#161a33;font-weight:600;cursor:pointer}
button.alt{background:transparent;color:var(--ink);border:1px solid var(--dim)}
ul{list-style:none;margin:0;padding:0;display:grid;gap:8px}
li{background:var(--panel);border-radius:12px;padding:10px 14px;display:grid;gap:6px;min-width:0}
li .n{overflow-wrap:anywhere}
li .s{font-size:14px;color:var(--dim)}
li.ok .s{color:var(--ok)} li.bad .s{color:var(--bad)}
progress{width:100%;height:6px;accent-color:var(--gold)}
</style></head><body><main>
<h1>Send to Zappo</h1>
<p>Games, songs, albums and films go to <b>__NAME__</b>. Zappo X sorts them into the right place when you leave its Send to Zappo screen.</p>
<div class="drop" id="drop"><b>Drop files or folders here</b><span>or choose them</span>
<div class="btns"><button id="pf">Choose files</button><button class="alt" id="pd">Choose a folder</button></div></div>
<input type="file" id="f" multiple hidden><input type="file" id="d" webkitdirectory hidden>
<ul id="list"></ul>
<p id="free"></p>
<h2>Update Zappo X</h2>
<p>Running <b>__VERSION__</b>. Drop a Zappo X package (<code>zappo-x-knulli.tar</code>) here: the handheld checks it and sets it aside, then Zappo X offers to install it (Settings &gt; About).</p>
<div class="drop" id="udrop"><b>Drop the update package here</b><span>or choose it</span>
<div class="btns"><button id="pu">Choose the package</button></div></div>
<input type="file" id="u" accept=".tar" hidden>
<ul id="ulist"></ul>
<h2>Ports and add-ons</h2>
<p>A port for Knulli (a zip with <code>roms/ports/&hellip;</code> inside, like the Halo CE port) is unpacked onto the card and shows up in Zappo X's Ports. A PortMaster port zip goes to PortMaster's <code>autoinstall</code> folder (PortMaster has to be installed first: Ports &gt; Install PortMaster on the handheld).</p>
<div class="drop" id="adrop"><b>Drop a port zip here</b><span>or choose it</span>
<div class="btns"><button id="pa">Choose the zip</button></div></div>
<input type="file" id="a" accept=".zip" hidden>
<ul id="alist"></ul>
<h2>Report</h2>
<p>If something isn't working, <a href="/report" download>download a report</a> (Zappo X's logs, the Wi-Fi log, Knulli's settings with passwords hidden) and send it to whoever is helping you.</p>
</main><script>
const list=document.getElementById('list'),drop=document.getElementById('drop');
const q=[];let busy=false;
function add(file,path){const li=document.createElement('li');li.innerHTML='<span class="n"></span><progress max="1" value="0"></progress><span class="s">Waiting</span>';
li.querySelector('.n').textContent=path;list.prepend(li);q.push({file,path,li});next();}
function next(){if(busy||!q.length)return;busy=true;const it=q.shift(),x=new XMLHttpRequest();
x.open('POST','/upload?name='+encodeURIComponent(it.path));
x.upload.onprogress=e=>{if(e.lengthComputable){it.li.querySelector('progress').value=e.loaded/e.total;it.li.querySelector('.s').textContent=Math.round(100*e.loaded/e.total)+'%';}};
x.onload=()=>{const ok=x.status==200;it.li.className=ok?'ok':'bad';it.li.querySelector('.s').textContent=ok?'Sent':'Not sent: '+x.responseText;it.li.querySelector('progress').value=ok?1:0;busy=false;free();next();};
x.onerror=()=>{it.li.className='bad';it.li.querySelector('.s').textContent='Not sent: the handheld went away';busy=false;next();};
x.send(it.file);}
function walk(entry,path){if(entry.isFile)entry.file(f=>add(f,path+f.name));else if(entry.isDirectory){const r=entry.createReader();
const read=()=>r.readEntries(es=>{if(es.length){es.forEach(e=>walk(e,path+entry.name+'/'));read();}});read();}}
drop.ondragover=e=>{e.preventDefault();drop.classList.add('over');};drop.ondragleave=()=>drop.classList.remove('over');
drop.ondrop=e=>{e.preventDefault();drop.classList.remove('over');const items=[...e.dataTransfer.items];
if(items.length&&items[0].webkitGetAsEntry){items.forEach(i=>{const en=i.webkitGetAsEntry();if(en)walk(en,'');});}else[...e.dataTransfer.files].forEach(f=>add(f,f.name));};
document.getElementById('pf').onclick=e=>{e.stopPropagation();document.getElementById('f').click();};
document.getElementById('pd').onclick=e=>{e.stopPropagation();document.getElementById('d').click();};
drop.onclick=()=>document.getElementById('f').click();
document.getElementById('f').onchange=e=>{[...e.target.files].forEach(f=>add(f,f.name));e.target.value='';};
document.getElementById('d').onchange=e=>{[...e.target.files].forEach(f=>add(f,f.webkitRelativePath||f.name));e.target.value='';};
const ulist=document.getElementById('ulist'),udrop=document.getElementById('udrop');
function upd(file){const li=document.createElement('li');li.innerHTML='<span class="n"></span><progress max="1" value="0"></progress><span class="s">Sending</span>';
li.querySelector('.n').textContent=file.name;ulist.prepend(li);const x=new XMLHttpRequest();x.open('POST','/update?name='+encodeURIComponent(file.name));
x.upload.onprogress=e=>{if(e.lengthComputable)li.querySelector('progress').value=e.loaded/e.total;};
x.onload=()=>{const ok=x.status==200;li.className=ok?'ok':'bad';li.querySelector('.s').textContent=ok?x.responseText:'Not taken: '+x.responseText;li.querySelector('progress').value=ok?1:0;};
x.onerror=()=>{li.className='bad';li.querySelector('.s').textContent='Not sent: the handheld went away';};x.send(file);}
udrop.ondragover=e=>{e.preventDefault();udrop.classList.add('over');};udrop.ondragleave=()=>udrop.classList.remove('over');
udrop.ondrop=e=>{e.preventDefault();udrop.classList.remove('over');[...e.dataTransfer.files].forEach(upd);};
document.getElementById('pu').onclick=e=>{e.stopPropagation();document.getElementById('u').click();};
udrop.onclick=()=>document.getElementById('u').click();
document.getElementById('u').onchange=e=>{[...e.target.files].forEach(upd);e.target.value='';};
const alist=document.getElementById('alist'),adrop=document.getElementById('adrop');
function addon(file){const li=document.createElement('li');li.innerHTML='<span class="n"></span><progress max="1" value="0"></progress><span class="s">Sending</span>';
li.querySelector('.n').textContent=file.name;alist.prepend(li);const x=new XMLHttpRequest();x.open('POST','/addon?name='+encodeURIComponent(file.name));
x.upload.onprogress=e=>{if(e.lengthComputable){li.querySelector('progress').value=e.loaded/e.total;if(e.loaded==e.total)li.querySelector('.s').textContent='Unpacking on the handheld...';}};
x.onload=()=>{const ok=x.status==200;li.className=ok?'ok':'bad';li.querySelector('.s').textContent=ok?x.responseText:'Not taken: '+x.responseText;li.querySelector('progress').value=ok?1:0;};
x.onerror=()=>{li.className='bad';li.querySelector('.s').textContent='Not sent: the handheld went away';};x.send(file);}
adrop.ondragover=e=>{e.preventDefault();adrop.classList.add('over');};adrop.ondragleave=()=>adrop.classList.remove('over');
adrop.ondrop=e=>{e.preventDefault();adrop.classList.remove('over');[...e.dataTransfer.files].forEach(addon);};
document.getElementById('pa').onclick=e=>{e.stopPropagation();document.getElementById('a').click();};
adrop.onclick=()=>document.getElementById('a').click();
document.getElementById('a').onchange=e=>{[...e.target.files].forEach(addon);e.target.value='';};
function free(){fetch('/status').then(r=>r.json()).then(s=>{document.getElementById('free').textContent=(s.free_gb).toFixed(1)+' GB free on the card';}).catch(()=>{});}
free();
</script></body></html>"""


def safe_path(name):
    """a relative path inside IMPORT: no .., no absolute paths, no hidden names"""
    parts = []
    for p in name.replace("\\", "/").split("/"):
        p = p.strip()
        if not p or p in (".", "..") or p.startswith("."):
            continue
        parts.append("".join(c for c in p if c >= " " and c not in '<>:"|?*'))
    return os.path.join(*parts) if parts else None


def free_name(path):
    """path, or "name (2).ext" when something is already there"""
    if not os.path.exists(path):
        return path
    stem, ext = os.path.splitext(path)
    n = 2
    while os.path.exists("%s (%d)%s" % (stem, n, ext)):
        n += 1
    return "%s (%d)%s" % (stem, n, ext)


class Handler(BaseHTTPRequestHandler):
    server_version = "SendToZappo/1"

    def log_message(self, *a):
        pass

    def reply(self, code, body, kind="text/plain; charset=utf-8"):
        b = body.encode() if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(b)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(b)

    def do_GET(self):
        if self.path.startswith("/status"):
            u = shutil.disk_usage(IMPORT if os.path.isdir(IMPORT) else "/")
            return self.reply(200, json.dumps({"free_gb": u.free / 1e9}), "application/json")
        if self.path in ("/", "/index.html"):
            name = html.escape(os.environ.get("ZAPPO_NAME", "the handheld"))
            try:
                ver = open(os.path.join(HOME, "VERSION")).read().strip()
            except OSError:
                ver = "?"
            return self.reply(200, PAGE.replace("__NAME__", name).replace("__VERSION__", html.escape(ver)), "text/html; charset=utf-8")
        if self.path.startswith("/report"):
            script = os.path.join(HOME, "zappo-report.sh")
            try:
                body = subprocess.run(["sh", script], capture_output=True, timeout=60).stdout
            except (OSError, subprocess.TimeoutExpired) as e:
                return self.reply(500, "couldn't make the report: %s" % e)
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Disposition", 'attachment; filename="zappo-x-report-%s.txt"' % time.strftime("%Y%m%d-%H%M"))
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        self.reply(404, "not here")

    def receive(self, length, part):
        """the request body to a file; True when it all arrived"""
        left = length
        with open(part, "wb") as f:
            while left > 0:
                chunk = self.rfile.read(min(left, 1 << 20))
                if not chunk:
                    break
                f.write(chunk)
                left -= len(chunk)
        return left == 0

    def do_update(self, u):
        """a Zappo X package: checked, then kept in UPDATES for Zappo X to offer"""
        try:
            length = int(self.headers.get("Content-Length", "-1"))
        except ValueError:
            length = -1
        if length < 0 or length > 200 << 20:
            return self.reply(400, "that doesn't look like a Zappo X package")
        os.makedirs(UPDATES, exist_ok=True)
        if shutil.disk_usage(UPDATES).free < length * 3 + 64 * 1024 * 1024:
            return self.reply(507, "not enough space on the card")
        part = os.path.join(UPDATES, ".incoming-%d.part" % os.getpid())
        try:
            whole = self.receive(length, part)
            if not whole:
                os.remove(part)
                return self.reply(400, "the file didn't arrive whole")
            with tarfile.open(part) as t:
                names = set(n.lstrip("./") for n in t.getnames())
                if "install.sh" not in names or "zappo-x/zappo-x" not in names:
                    os.remove(part)
                    return self.reply(400, "that's not a Zappo X package (no install.sh / zappo-x inside)")
                if any(n.startswith("/") or ".." in n.split("/") for n in names):
                    os.remove(part)
                    return self.reply(400, "that package has paths it shouldn't")
                try:
                    ver = t.extractfile("./zappo-x/VERSION" if "./zappo-x/VERSION" in t.getnames() else "zappo-x/VERSION").read().decode().strip()
                except (KeyError, AttributeError, OSError):
                    ver = "unknown version"
            dest = os.path.join(UPDATES, "zappo-x-knulli.tar")
            os.replace(part, dest)
            with open(dest + ".version", "w") as f:
                f.write(ver + "\n")
            os.sync() if hasattr(os, "sync") else None
        except tarfile.TarError:
            try:
                os.remove(part)
            except OSError:
                pass
            return self.reply(400, "that's not a Zappo X package (zappo-x-knulli.tar)")
        except OSError as e:
            try:
                os.remove(part)
            except OSError:
                pass
            return self.reply(400, "couldn't take it: %s" % e.strerror)
        self.reply(200, "Taken: %s is waiting. On the handheld: Settings > About > Install the update." % ver)

    def do_addon(self, u):
        """a port zip: a Knulli port (roms/ports/... inside) is unpacked onto the
        card; a PortMaster port (Name.sh + Name/port.json at the top) goes to
        PortMaster's autoinstall folder. Nothing already on the card is overwritten
        except the port's own files (its saves and settings stay)."""
        name = safe_path(urllib.parse.parse_qs(u.query).get("name", [""])[0]) or "port.zip"
        try:
            length = int(self.headers.get("Content-Length", "-1"))
        except ValueError:
            length = -1
        if length < 0 or length > 2000 << 20:
            return self.reply(400, "that doesn't look like a port zip")
        share = os.path.dirname(IMPORT) if os.path.isdir(os.path.dirname(IMPORT)) else "/userdata"
        if shutil.disk_usage(share).free < length * 3 + 256 * 1024 * 1024:
            return self.reply(507, "not enough space on the card")
        os.makedirs(UPDATES, exist_ok=True)
        part = os.path.join(UPDATES, ".addon-%d.part" % os.getpid())
        try:
            if not self.receive(length, part):
                os.remove(part)
                return self.reply(400, "the file didn't arrive whole")
            with zipfile.ZipFile(part) as z:
                names = [n for n in z.namelist() if not n.endswith("/")]
                if any(n.startswith("/") or ".." in n.split("/") for n in names):
                    os.remove(part)
                    return self.reply(400, "that zip has paths it shouldn't")
                ports_sh = [n for n in names if n.startswith("roms/ports/") and n.count("/") == 2 and n.endswith(".sh")]
                top_sh = [n for n in names if n.count("/") == 0 and n.endswith(".sh")]
                if ports_sh:
                    # a Knulli port: everything under roms/ goes onto the share partition
                    done = 0
                    for n in names:
                        if not n.startswith("roms/"):
                            continue
                        dest = os.path.join(share, n)
                        os.makedirs(os.path.dirname(dest), exist_ok=True)
                        with z.open(n) as src, open(dest, "wb") as dst:
                            shutil.copyfileobj(src, dst, 1 << 20)
                        if n.endswith((".sh", "/halo")) or (z.getinfo(n).external_attr >> 16) & 0o111:
                            os.chmod(dest, 0o755)
                        done += 1
                    os.sync() if hasattr(os, "sync") else None
                    os.remove(part)
                    what = ", ".join(os.path.basename(p)[:-3] for p in ports_sh)
                    extra = ""
                    if any("/halo/" in n for n in names):
                        extra = " Halo needs your Xbox disc image: send the .iso through the box at the top of this page and Zappo X puts it in ports/halo."
                    return self.reply(200, "Installed: %s (%d files). It shows up in Ports.%s" % (what, done, extra))
                if top_sh and any(n.endswith("/port.json") for n in names):
                    pm = os.path.join(share, "roms", "ports", "PortMaster")
                    if not os.path.isdir(pm):
                        os.remove(part)
                        return self.reply(400, "that's a PortMaster port, and PortMaster isn't installed yet: on the handheld, Ports > Install PortMaster, then send it again")
                    auto = os.path.join(pm, "autoinstall")
                    os.makedirs(auto, exist_ok=True)
                    dest = os.path.join(auto, os.path.basename(name))
                    os.replace(part, dest)
                    os.sync() if hasattr(os, "sync") else None
                    return self.reply(200, "Taken for PortMaster: start PortMaster on the handheld (Ports) and it installs %s." % os.path.basename(name))
            os.remove(part)
            return self.reply(400, "that zip isn't a Knulli port (no roms/ports/*.sh) or a PortMaster port (no port.json)")
        except zipfile.BadZipFile:
            try:
                os.remove(part)
            except OSError:
                pass
            return self.reply(400, "that's not a zip file")
        except OSError as e:
            try:
                os.remove(part)
            except OSError:
                pass
            return self.reply(500, "couldn't install it: %s" % (e.strerror or e))

    def do_POST(self):
        u = urllib.parse.urlparse(self.path)
        if u.path == "/update":
            return self.do_update(u)
        if u.path == "/addon":
            return self.do_addon(u)
        if u.path != "/upload":
            return self.reply(404, "not here")
        name = safe_path(urllib.parse.parse_qs(u.query).get("name", [""])[0])
        try:
            length = int(self.headers.get("Content-Length", "-1"))
        except ValueError:
            length = -1
        if not name or length < 0:
            return self.reply(400, "a file with a name, please")
        if shutil.disk_usage(IMPORT).free < length + 64 * 1024 * 1024:
            return self.reply(507, "not enough space on the card")
        dest = free_name(os.path.join(IMPORT, name))
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        part = os.path.join(IMPORT, ".incoming", "%d-%s.part" % (os.getpid(), os.path.basename(dest)))
        os.makedirs(os.path.dirname(part), exist_ok=True)
        left = length
        try:
            with open(part, "wb") as f:
                while left > 0:
                    chunk = self.rfile.read(min(left, 1 << 20))
                    if not chunk:
                        break
                    f.write(chunk)
                    left -= len(chunk)
            if left:
                os.remove(part)
                return self.reply(400, "the file didn't arrive whole")
            os.replace(part, dest)
        except OSError as e:
            try:
                os.remove(part)
            except OSError:
                pass
            return self.reply(500, "couldn't save it: %s" % e.strerror)
        with open(LOG, "a") as f:
            f.write("%d\t%d\t%s\n" % (time.time(), length, os.path.relpath(dest, IMPORT)))
        self.reply(200, "ok")


def main():
    os.makedirs(IMPORT, exist_ok=True)
    ThreadingHTTPServer.allow_reuse_address = True
    srv = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    srv.serve_forever()


if __name__ == "__main__":
    main()
