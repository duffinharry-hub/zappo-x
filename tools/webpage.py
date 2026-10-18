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
os.makedirs(out, exist_ok=True)
open(os.path.join(out, "index.html"), "w").write(page.replace("/*__RUNTIME__*/", rt))
print(os.path.join(out, "index.html"))
