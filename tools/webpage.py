"""Builds the browser preview page: web/page.html with the runtime inlined
(artifact pages may only load scripts from a few CDNs), next to zappo.wasm
and the card in build/web.   usage: webpage.py OUT_DIR [--native]
(--native: the page zappo-tablet serves, drawing what it streams)"""
import os
import sys

out = sys.argv[1]
native = len(sys.argv) > 2 and sys.argv[2] == "--native"   # served by zappo-tablet
page = open("web/page.html").read()
rt = open("web/zappo-runtime.js").read()
assert "/*__RUNTIME__*/" in page
if native:
    rt = "window.ZAPPO_NATIVE = true;\n" + rt
    # the tablet: Zappo X as an app (home-screen icon, full screen, landscape)
    head = ('<link rel="manifest" href="manifest.webmanifest">\n<meta name="theme-color" content="#0b0e18">\n'
            '<meta name="mobile-web-app-capable" content="yes">\n<link rel="icon" href="icon-192.png">\n')
    assert "</head>" in page or "<title>" in page
    page = page.replace("<title>", head + "<title>", 1)
os.makedirs(out, exist_ok=True)
if native:
    import shutil
    for f in os.listdir("web/app"):
        shutil.copyfile(os.path.join("web/app", f), os.path.join(out, f))
open(os.path.join(out, "index.html"), "w").write(page.replace("/*__RUNTIME__*/", rt))
print(os.path.join(out, "index.html"))
