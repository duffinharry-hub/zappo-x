#!/usr/bin/python3
"""Zappo X: is there a newer release on GitHub? Downloads it if so.

  zappo-update-check.py <current version, e.g. "X 0.1.5 (hash, date)"> <updates folder>

Looks at the public repository's releases for the newest tag "ZappoX-<version>"
carrying zappo-x-knulli.tar, compares the version with the one running, and
if it is newer downloads the package into the updates folder (checked against
the SHA-256 GitHub records for the file), where Zappo X offers to install it.
Prints one line: UPTODATE|<ver>, READY|<ver>, or ERR|<why>. Standard library only.
"""
import hashlib
import json
import os
import re
import sys
import urllib.request

REPO = "duffinharry-hub/zappo-x"
ASSET = "zappo-x-knulli.tar"


def vtuple(v):
    m = re.search(r"(\d+(?:\.\d+)+)", v or "")
    return tuple(int(x) for x in m.group(1).split(".")) if m else (0,)


def fetch(url, timeout=30):
    req = urllib.request.Request(url, headers={"User-Agent": "ZappoX-updater", "Accept": "application/vnd.github+json"})
    return urllib.request.urlopen(req, timeout=timeout)


def main():
    cur = sys.argv[1] if len(sys.argv) > 1 else ""
    dest_dir = sys.argv[2] if len(sys.argv) > 2 else "/userdata/system/zappo-x-update"
    try:
        rels = json.load(fetch("https://api.github.com/repos/%s/releases?per_page=30" % REPO))
    except Exception as e:  # no network, GitHub down, odd answer
        print("ERR|Couldn't reach GitHub (%s)" % (getattr(e, "reason", None) or e.__class__.__name__))
        return 1
    best = None
    for r in rels:
        tag = r.get("tag_name", "")
        if not tag.startswith("ZappoX-") or r.get("draft") or r.get("prerelease"):
            continue
        for a in r.get("assets", []):
            if a.get("name") == ASSET:
                cand = (vtuple(tag), tag[7:], a["browser_download_url"], a.get("digest") or "", int(a.get("size") or 0))
                if best is None or cand[0] > best[0]:
                    best = cand
    if best is None:
        print("ERR|No Zappo X release found on GitHub yet")
        return 1
    _, ver, url, digest, size = best
    if vtuple(ver) <= vtuple(cur):
        print("UPTODATE|%s" % ver)
        return 0
    os.makedirs(dest_dir, exist_ok=True)
    part = os.path.join(dest_dir, ASSET + ".part")
    h = hashlib.sha256()
    got = 0
    try:
        with fetch(url, timeout=60) as r, open(part, "wb") as f:
            while True:
                chunk = r.read(1 << 20)
                if not chunk:
                    break
                f.write(chunk)
                h.update(chunk)
                got += len(chunk)
    except Exception as e:
        try:
            os.remove(part)
        except OSError:
            pass
        print("ERR|The download stopped (%s)" % (getattr(e, "reason", None) or e.__class__.__name__))
        return 1
    if size and got != size:
        os.remove(part)
        print("ERR|The download was cut short")
        return 1
    if digest.startswith("sha256:") and h.hexdigest() != digest[7:]:
        os.remove(part)
        print("ERR|The download didn't match GitHub's checksum")
        return 1
    final = os.path.join(dest_dir, ASSET)
    os.replace(part, final)
    with open(final + ".version", "w") as f:
        f.write("X %s\n" % ver)
    if hasattr(os, "sync"):
        os.sync()
    print("READY|%s" % ver)
    return 0


if __name__ == "__main__":
    sys.exit(main())
