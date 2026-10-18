"""Builds the Zappo X progress page from docs/progress.json and ZAPPO-X-PLAN.md.

usage: progress.py OUT_DIR
writes OUT_DIR/progress.html          (page content, published as the online artifact)
       OUT_DIR/Zappo X - Progress.html (the same page as a full HTML file for the PC)"""
import datetime
import html
import json
import math
import os
import re
import sys

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
E = html.escape


def md(text):
    """just enough Markdown for the plan: headings, lists, bold, code, rules, paragraphs"""
    out, inlist, para = [], False, []

    def inline(s):
        s = E(s)
        s = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", s)
        return re.sub(r"`([^`]+)`", r"<code>\1</code>", s)

    def flush():
        nonlocal para
        if para:
            out.append("<p>" + inline(" ".join(para)) + "</p>")
            para = []
    for line in text.splitlines():
        s = line.rstrip()
        m = re.match(r"^(\s*)[-*] (.*)", s)
        if m:
            flush()
            if not inlist:
                out.append("<ul>")
                inlist = True
            out.append("<li%s>%s</li>" % (' class="sub"' if len(m.group(1)) >= 2 else "", inline(m.group(2))))
            continue
        if inlist and s.startswith("  ") and s.strip():
            out[-1] = out[-1][:-5] + " " + inline(s.strip()) + "</li>"
            continue
        if inlist:
            out.append("</ul>")
            inlist = False
        if not s.strip():
            flush()
            continue
        h = re.match(r"^(#{1,3}) (.*)", s)
        if h:
            flush()
            n = len(h.group(1)) + 2
            out.append("<h%d>%s</h%d>" % (n, inline(h.group(2)), n))
            continue
        if s.strip() == "---":
            flush()
            continue
        if re.match(r"^\d+\. ", s):
            flush()
            out.append("<p>" + inline(s) + "</p>")
            continue
        para.append(s.strip())
    flush()
    if inlist:
        out.append("</ul>")
    return "\n".join(out)


def ring(lit, colour="var(--gold-fill)", size=40, pulse=False, label=""):
    """the RG40XX H's stick light: 8 LEDs round the stick; `lit` of them on"""
    dots = []
    for i in range(8):
        a = -math.pi / 2 + i * math.pi / 4
        x, y = 20 + 13 * math.cos(a), 20 + 13 * math.sin(a)
        on = i < lit
        dots.append('<circle cx="%.2f" cy="%.2f" r="3.6" class="%s" style="%s"/>'
                    % (x, y, "led on" if on else "led", ("fill:%s;color:%s" % (colour, colour)) if on else ""))
    return ('<svg class="ring%s" width="%d" height="%d" viewBox="0 0 40 40" role="img" aria-label="%s">'
            '<circle cx="20" cy="20" r="18.5" class="housing"/><circle cx="20" cy="20" r="7.5" class="stick"/>%s</svg>'
            % (" pulse" if pulse else "", size, size, E(label), "".join(dots)))


CSS = """
<style>
/* layout: one calm column; each milestone is a row led by an RG40XX H stick-light ring showing how far it has got */
:root{
  --bg:#eef0f6; --panel:#ffffff; --ink:#161a33; --dim:#596080; --line:#d9dcea;
  --gold:#94690a; --gold-fill:#f2bf2f; --ok:#1d8a52; --now:#c45f0a; --off:#c9cde0;
  --housing:#1b1f38; --stick:#2c3150;
  --display:"Fredoka","Trebuchet MS",system-ui,sans-serif;
  --body:"Figtree",system-ui,-apple-system,"Segoe UI",sans-serif;
  --mono:"JetBrains Mono",ui-monospace,Consolas,monospace;
}
@media (prefers-color-scheme: dark){:root:not([data-theme="light"]){
  --bg:#0f1229; --panel:#171b35; --ink:#f3f1e8; --dim:#a2a8c8; --line:#292e4f;
  --gold:#f5c842; --gold-fill:#f5c842; --ok:#5fd38d; --now:#ff9a3c; --off:#3a4066;
  --housing:#0a0c1c; --stick:#22274a; color-scheme:dark}}
:root[data-theme="dark"]{
  --bg:#0f1229; --panel:#171b35; --ink:#f3f1e8; --dim:#a2a8c8; --line:#292e4f;
  --gold:#f5c842; --gold-fill:#f5c842; --ok:#5fd38d; --now:#ff9a3c; --off:#3a4066;
  --housing:#0a0c1c; --stick:#22274a; color-scheme:dark}
body{background:var(--bg);color:var(--ink);font:16px/1.55 var(--body);
  padding-left:max(16px,env(safe-area-inset-left,0px));padding-right:max(16px,env(safe-area-inset-right,0px))}
main{max-width:820px;margin:0 auto;padding-block:28px 64px;display:grid;gap:28px}
h1,h2{font-family:var(--display);font-weight:600;text-wrap:balance;margin:0;line-height:1.15}
h1{font-size:44px;letter-spacing:.5px}
h2{font-size:22px}
.eyebrow{font:600 12px/1.2 var(--mono);letter-spacing:.12em;text-transform:uppercase;color:var(--dim)}
header{display:flex;gap:20px;align-items:center;flex-wrap:wrap}
header .t{display:grid;gap:4px;min-width:0;flex:1}
header .meta{color:var(--dim);font-variant-numeric:tabular-nums}
.ring .housing{fill:var(--housing)} .ring .stick{fill:var(--stick)}
.ring .led{fill:var(--off)} .ring .led.on{filter:drop-shadow(0 0 2.2px currentColor)}
@media (prefers-reduced-motion: no-preference){.ring.pulse .led.on{animation:glow 2.4s ease-in-out infinite}}
@keyframes glow{50%{opacity:.55}}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:18px 20px;display:grid;gap:10px;min-width:0}
.now p{margin:0}
.checks{list-style:none;margin:0;padding:0;display:grid;gap:8px}
.checks li{display:grid;grid-template-columns:22px 1fr;gap:10px;align-items:start;min-width:0;overflow-wrap:anywhere}
.box{width:18px;height:18px;border-radius:5px;border:2px solid var(--dim);margin-top:3px;display:grid;place-items:center}
.done .box{background:var(--ok);border-color:var(--ok)}
.done .box::after{content:"";width:5px;height:9px;border:solid var(--panel);border-width:0 2px 2px 0;transform:translate(0,-1px) rotate(45deg)}
.done .txt{color:var(--dim)}
.ms{display:grid;gap:12px}
.m{display:grid;grid-template-columns:48px 1fr;gap:14px;background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:16px 18px}
.m .body{display:grid;gap:8px;min-width:0}
.m .hd{display:flex;gap:10px;align-items:baseline;flex-wrap:wrap}
.m .id{font:600 13px var(--mono);color:var(--gold)}
.m .title{font-weight:650;font-size:17px;flex:1;min-width:0}
.pill{font:600 11px/1 var(--mono);letter-spacing:.1em;text-transform:uppercase;padding:5px 8px;border-radius:999px;border:1px solid currentColor}
.pill.done{color:var(--ok)} .pill.now{color:var(--now)} .pill.next{color:var(--dim)}
.m.now{border-color:var(--now)}
.secs{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px}
@media (max-width:620px){.secs{grid-template-columns:repeat(2,minmax(0,1fr))}}
.sec{background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:14px;display:grid;grid-template-columns:40px 1fr;gap:12px;align-items:center;min-width:0}
.sec b{display:block} .sec span{color:var(--dim);font-size:14px}
details{background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:14px 20px}
summary{cursor:pointer;font-weight:650}
summary:focus-visible{outline:2px solid var(--gold);outline-offset:4px;border-radius:4px}
details .plan{max-width:68ch}
details h3,details h4,details h5{font-family:var(--display);font-weight:600;margin:22px 0 6px}
details li.sub{margin-left:18px;list-style:circle}
code{font-family:var(--mono);font-size:.88em;background:var(--bg);padding:1px 5px;border-radius:5px;overflow-wrap:anywhere}
.foot{color:var(--dim);font-size:14px}
figure{margin:0;display:grid;gap:8px}
figure img{width:100%;height:auto;border-radius:10px;display:block}
figcaption{color:var(--dim);font-size:14px}
</style>
"""


def latest(p):
    """the newest picture from the twin, inlined (the page is one file)"""
    import base64
    lt = p.get("latest")
    if not lt:
        return ""
    data = open(os.path.join(R, lt["image"]), "rb").read()
    kind = "jpeg" if lt["image"].endswith(".jpg") else "gif" if lt["image"].endswith(".gif") else "png"
    return ('<section class="panel"><div class="eyebrow">Latest from the twin</div><figure>'
            '<img src="data:image/%s;base64,%s" alt="%s"><figcaption>%s</figcaption></figure></section>'
            % (kind, base64.b64encode(data).decode(), E(lt["caption"]), E(lt["caption"])))


def build(p, plan, when):
    ms = p["milestones"]
    first_open = next((i for i, m in enumerate(ms) if not all(n["done"] for n in m["notes"])), len(ms))
    done_count = sum(all(n["done"] for n in m["notes"]) for m in ms)
    rows = []
    for i, m in enumerate(ms):
        steps = m["notes"]
        k = sum(n["done"] for n in steps)
        state = "done" if k == len(steps) else ("now" if i == first_open or k > 0 else "next")
        lit = round(8 * k / len(steps))
        colour = {"done": "var(--ok)", "now": "var(--now)"}.get(state, "var(--gold-fill)")
        items = "".join('<li class="%s"><span class="box" aria-hidden="true"></span><span class="txt">%s</span></li>'
                        % ("done" if n["done"] else "", E(n["text"])) for n in steps)
        rows.append('<article class="m %s">%s<div class="body"><div class="hd"><span class="id">%s</span>'
                    '<span class="title">%s</span><span class="pill %s">%s</span></div><ul class="checks">%s</ul></div></article>'
                    % (state, ring(lit, colour, 48, state == "now", "%d of %d steps done" % (k, len(steps))), E(m["id"]),
                       E(m["title"]), state, {"done": "Done", "now": "In progress", "next": "Next"}[state], items))
    you = "".join('<li class="%s"><span class="box" aria-hidden="true"></span><span class="txt">%s</span></li>'
                  % ("done" if n["done"] else "", E(n["text"])) for n in p.get("you", []))
    secs = "".join('<div class="sec">%s<div><b>%s</b><span>%s Zappo &middot; %s</span></div></div>'
                   % (ring(8, s["colour"], 40, False, "%s lights" % s["name"]), E(s["name"]), E(s["zappo"]), E(s["motion"]))
                   for s in p.get("sections", []))
    return ("""<title>Zappo X Progress</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="preconnect" href="https://fonts.googleapis.com"><link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Fredoka:wght@500;600&family=Figtree:wght@400;600;700&family=JetBrains+Mono:wght@500;600&display=swap">
""" + CSS + """
<main>
<header>%s<div class="t"><div class="eyebrow">Anbernic RG40XX H &middot; H700</div><h1>Zappo X</h1>
<div class="meta">%d of %d milestones done &middot; updated %s</div></div></header>
<section class="panel now"><div class="eyebrow">Right now</div><p>%s</p></section>
%s
<section class="panel"><div class="eyebrow">Waiting on you</div><ul class="checks">%s</ul></section>
<section class="ms"><h2>Milestones</h2>%s</section>
<section class="ms"><h2>Sections and their Zappos</h2><div class="secs">%s</div>
<p class="foot">Each section gets its own animated background, and the stick lights take its colour as you move between them.</p></section>
<details><summary>The full plan</summary><div class="plan">%s</div></details>
</main>""" % (ring(done_count, "var(--gold-fill)", 76, False, "%d of %d milestones done" % (done_count, len(ms))),
              done_count, len(ms), E(when), E(p.get("now", "")), latest(p), you, "".join(rows), secs, md(plan)))


def main(out_dir):
    p = json.load(open(os.path.join(R, "docs", "progress.json")))
    plan = open(os.path.join(R, "ZAPPO-X-PLAN.md")).read()
    when = datetime.datetime.now().strftime("%d %b %Y, %H:%M")
    page = build(p, plan, when)
    os.makedirs(out_dir, exist_ok=True)
    open(os.path.join(out_dir, "progress.html"), "w").write(page)
    full = ('<!doctype html><html lang="en"><head><meta charset="utf-8">'
            '<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover"></head><body>'
            + page + "</body></html>")
    open(os.path.join(out_dir, "Zappo X - Progress.html"), "w").write(full)


if __name__ == "__main__":
    main(sys.argv[1])
