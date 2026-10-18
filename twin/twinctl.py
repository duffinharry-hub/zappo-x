"""Talk to the running device twin from outside.

  twinctl.py shot OUT.png        screenshot of the twin's 640x480 screen
  twinctl.py sh 'COMMAND'        run a shell command inside the twin
  twinctl.py press a|b|x|y|l1|r1|l2|r2|select|start|menu|up|down|left|right|volup|voldown|power ...
  twinctl.py hold BUTTON MS      hold a button down for MS milliseconds
  twinctl.py stick L|R X Y       move a stick (-1..1 each), 0 0 to let go
  twinctl.py battery N           battery at N %
  twinctl.py charge on|off       plug the charger in / out
  twinctl.py frame OUT.png       the whole handheld: screen and both stick-light rings
  twinctl.py btpad press|down|up BUTTON...   the pretend Bluetooth pad (once paired and connected)
  twinctl.py btpad stick L|R X Y | wake       move its stick / make it reconnect by itself
  twinctl.py bt status           the pretend Bluetooth devices and how they're connected
  twinctl.py audio OUT.wav [S]   the last S seconds (default 10) of what the twin played
  twinctl.py level               how loud the twin is right now (0 = silent)
  twinctl.py qmp COMMAND [JSON]  any QEMU monitor command
  twinctl.py quit                switch the twin off
"""
import json
import os
import socket
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

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
        # ids unique across clients: an answer meant for an earlier client that
        # gave up can still be in the channel, and must not be taken for ours
        self.n = int.from_bytes(os.urandom(4), "big")

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
    elif a[0] == "press":
        ag = Agent()
        for b in a[1:]:
            r = ag.ask("pad", press=b)
            if r["rc"]:
                sys.exit(r["out"])
            time.sleep(0.25)
    elif a[0] == "hold":
        Agent().ask("pad", press=a[1], ms=int(a[2]))
    elif a[0] == "stick":
        print(Agent().ask("pad", stick=a[1], x=float(a[2]), y=float(a[3]))["out"])
    elif a[0] == "battery":
        print(Agent().ask("power", capacity=int(a[1]))["out"])
    elif a[0] == "charge":
        print(Agent().ask("power", charging=1 if a[1] in ("on", "1", "yes") else 0)["out"])
    elif a[0] == "frame":
        import ledview
        from PIL import Image
        tmp = os.path.join(RUN, ".frame-screen.png")
        shot(tmp)
        ledview.frame_image(Image.open(tmp)).save(a[1])
        print(a[1])
    elif a[0] == "btpad":
        ag = Agent()
        if a[1] == "stick":
            r = ag.ask("bt", stick=a[2], x=float(a[3]), y=float(a[4]))
        elif a[1] == "wake":
            r = ag.ask("bt", cmd="wake")
        else:
            for b in a[2:]:
                r = ag.ask("bt", **{a[1]: b})
                if r["rc"]:
                    break
                time.sleep(0.25)
        print(r["out"])
        sys.exit(r["rc"])
    elif a[0] == "bt":
        print(Agent().ask("bt", cmd=a[1] if len(a) > 1 else "status")["out"])
    elif a[0] in ("audio", "level"):
        import audiorec
        audiorec.main(["save"] + a[1:] if a[0] == "audio" else ["level"])
    elif a[0] == "qmp":
        print(json.dumps(QMP().cmd(a[1], **(json.loads(a[2]) if len(a) > 2 else {})), indent=1))
    elif a[0] == "quit":
        QMP().cmd("quit")
