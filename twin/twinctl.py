"""Talk to the running device twin from outside.

  twinctl.py shot OUT.png        screenshot of the twin's 640x480 screen
  twinctl.py sh 'COMMAND'        run a shell command inside the twin
  twinctl.py qmp COMMAND [JSON]  any QEMU monitor command
  twinctl.py quit                switch the twin off
"""
import json
import os
import socket
import sys
import tempfile

RUN = os.environ.get("TWIN_RUN", "/home/claude/twin/run")


class QMP:
    def __init__(self, path=os.path.join(RUN, "qmp.sock")):
        self.s = socket.socket(socket.AF_UNIX)
        self.s.connect(path)
        self.f = self.s.makefile("rwb")
        self._read()                       # greeting
        self.cmd("qmp_capabilities")

    def _read(self):
        while True:
            m = json.loads(self.f.readline())
            if "event" not in m:
                return m

    def cmd(self, name, **args):
        self.f.write(json.dumps({"execute": name, "arguments": args}).encode() + b"\n")
        self.f.flush()
        r = self._read()
        if "error" in r:
            raise RuntimeError("%s: %s" % (name, r["error"]))
        return r.get("return")


class Agent:
    """the twin's helper inside (twin/root/usr/bin/zappo-twin-agent)"""
    def __init__(self, path=os.path.join(RUN, "agent.sock")):
        self.s = socket.socket(socket.AF_UNIX)
        self.s.connect(path)
        self.f = self.s.makefile("rwb")
        self.n = 0

    def ask(self, do, timeout=60, **kw):
        self.n += 1
        kw.update({"id": self.n, "do": do, "timeout": timeout})
        self.f.write(json.dumps(kw).encode() + b"\n")
        self.f.flush()
        self.s.settimeout(timeout + 15)
        while True:
            ans = json.loads(self.f.readline())
            if ans.get("id") == self.n:
                return ans

    def sh(self, cmd, timeout=60):
        a = self.ask("run", cmd=cmd, timeout=timeout)
        return a["rc"], a["out"]


def shot(out):
    from PIL import Image
    tmp = tempfile.mktemp(suffix=".ppm", dir=RUN)
    QMP().cmd("screendump", filename=tmp)
    Image.open(tmp).convert("RGB").save(out)
    os.remove(tmp)
    return out


if __name__ == "__main__":
    a = sys.argv[1:]
    if a[0] == "shot":
        print(shot(a[1]))
    elif a[0] == "sh":
        rc, out = Agent().sh(a[1], int(a[2]) if len(a) > 2 else 60)
        sys.stdout.write(out)
        sys.exit(rc if rc >= 0 else 1)
    elif a[0] == "qmp":
        print(json.dumps(QMP().cmd(a[1], **(json.loads(a[2]) if len(a) > 2 else {})), indent=1))
    elif a[0] == "quit":
        QMP().cmd("quit")
