"""Checks the running twin's hardware, one line per part, and says what failed.

  selftest.py            everything
  selftest.py wifi bt    just those (screen pad lights battery wifi sound bt model)

Run it after changing the twin (kernel, start-up scripts, QEMU) and before
trusting a Zappo X test on it.
"""
import json
import math
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import twinctl  # noqa: E402

RUN = twinctl.RUN
ag = twinctl.Agent()


def sh(cmd, timeout=60):
    return ag.sh(cmd, timeout)[1]


def events_while(dev_name, action, wait=4):
    """key events the twin's device `dev_name` produced while action() ran"""
    sh('E=$(grep -A6 "N: Name=\\"%s\\"" /proc/bus/input/devices | grep -o "event[0-9]*" | head -1); '
       '[ -n "$E" ] && (timeout %d cat /dev/input/$E > /tmp/selftest.ev 2>/dev/null &)' % (dev_name, wait + 2))
    time.sleep(1)
    action()
    time.sleep(wait)
    out = sh('python3 -c "import struct;d=open(\'/tmp/selftest.ev\',\'rb\').read();'
             'print([(c,v) for _,_,t,c,v in (struct.unpack(\'qqHHi\',d[i:i+24]) for i in range(0,len(d)-23,24)) if t==1])"')
    try:
        return eval(out.strip(), {})
    except Exception:
        return []


def t_model():
    m = sh("tr -d '\\0' < /proc/device-tree/model").strip()
    return m == "Anbernic RG40XX H", m


def t_screen():
    from PIL import Image
    p = os.path.join(RUN, ".selftest.png")
    for _ in range(8):                  # a moment of black between screens is fine
        twinctl.shot(p)
        im = Image.open(p).convert("L")
        lo, hi = im.getextrema()
        if hi - lo > 40:
            break
        time.sleep(2)
    return im.size == (640, 480) and hi - lo > 40, "%dx%d, contrast %d" % (im.size + (hi - lo,))


def t_pad():
    ev = events_while("Anbernic RG40XX-H Controller", lambda: ag.ask("pad", press="a"))
    return (304, 1) in ev and (304, 0) in ev, "button A: %s" % (ev or "nothing")


def led_state():
    try:
        return json.load(open(os.path.join(RUN, "led-state.json")))
    except (OSError, ValueError):
        return None


def t_lights():
    st = led_state()
    if not st:
        return False, "nothing has been sent to the light controller"
    lit = [c for c in st.get("right", []) + st.get("left", []) if max(c) > 0]
    return st["brightness"] > 0 and (lit or st["mode"] >= 5), \
        "mode %d, brightness %d, %d LEDs lit" % (st["mode"], st["brightness"], len(lit))


def t_battery():
    before = ag.ask("power")["out"]
    ag.ask("power", capacity=12)
    t0 = time.time()
    ok = False
    while time.time() - t0 < 25:
        st = led_state()
        if st and st["mode"] <= 4 and st.get("right"):
            r, g, b = st["right"][0]
            if r > 100 and g > 60 and b < 60:          # yellow: Knulli's low battery
                ok = True
                break
        time.sleep(1)
    cap = before.split("%")[0]
    ag.ask("power", capacity=int(cap) if cap.isdigit() else 80)
    return ok, "at 12%%: %s" % ("lights turned yellow" if ok else "lights did not change")


def t_wifi():
    out = sh("ip -4 addr show wlan0 | grep -o 'inet [0-9.]*'; ping -c1 -W3 192.168.77.1 >/dev/null && echo ping-ok")
    ok = "192.168.77.20" in out and "ping-ok" in out
    return ok, "wlan0 %s" % (" ".join(out.split()) or "has no address")


def t_sound():
    sh('. /etc/profile.d/xdg.sh; . /etc/profile.d/dbus.sh; [ -f /tmp/selftest.wav ] || python3 -c "'
       'import math,struct,wave\nw=wave.open(\'/tmp/selftest.wav\',\'wb\');w.setnchannels(2);w.setsampwidth(2);w.setframerate(48000)\n'
       'w.writeframes(b\'\'.join(struct.pack(\'<hh\',int(9000*math.sin(2*math.pi*660*i/48000)),int(9000*math.sin(2*math.pi*660*i/48000))) for i in range(48000)))\n'
       'w.close()"; timeout 8 paplay /tmp/selftest.wav')
    time.sleep(1)
    import audiorec
    p = os.path.join(RUN, ".selftest.wav")
    audiorec.ask("save %s 4" % p)
    raw = open(p, "rb").read()
    rate, = struct.unpack("<I", raw[24:28])
    d = raw[44:]
    n = len(d) // 4
    if n < rate // 2:
        return False, "nothing came out"
    x = struct.unpack("<%dh" % (n * 2), d[:n * 4])[0::2]
    # strength at 660 Hz against 500 Hz (Goertzel), over the loudest half second
    best = 0, 0
    half = rate // 2
    for s in range(0, max(1, n - half), half // 2):
        seg = x[s:s + half]

        def power(f):
            k = 2 * math.cos(2 * math.pi * f / rate)
            a = b = 0.0
            for v in seg:
                a, b = v + k * a - b, a
            return a * a + b * b - k * a * b
        p660, p500 = power(660), power(500)
        if p660 > best[0]:
            best = p660, p500
    ok = best[0] > 50 * max(best[1], 1)
    return ok, "660 Hz test tone %s" % ("heard" if ok else "not heard")


def t_bt():
    if "Powered: yes" not in sh("bluetoothctl show"):
        sh("batocera-bluetooth enable >/dev/null 2>&1; sleep 4", 90)
    st = ag.ask("bt", cmd="status")["out"]
    if "Zappo Twin Pad" not in st:
        return False, "no pretend devices: %s" % st[:80]
    if "Paired: yes" not in sh("bluetoothctl info 5A:50:54:00:00:01"):
        sh("timeout 70 batocera-bluetooth trust input >/dev/null 2>&1", 100)
    if "connected" not in ag.ask("bt", cmd="status")["out"].split("\n")[0]:
        ag.ask("bt", cmd="wake")
        time.sleep(8)
    ev = events_while("Zappo Twin Pad", lambda: ag.ask("bt", press="a"))
    ok = (0x130, 1) in ev and (0x130, 0) in ev
    return ok, "pad paired and connected, button A %s" % ("works" if ok else "did nothing: %s" % ev)


TESTS = [("model", t_model), ("screen", t_screen), ("pad", t_pad), ("lights", t_lights),
         ("battery", t_battery), ("wifi", t_wifi), ("sound", t_sound), ("bt", t_bt)]


def main(want):
    bad = 0
    for name, fn in TESTS:
        if want and name not in want:
            continue
        try:
            ok, msg = fn()
        except Exception as e:
            ok, msg = False, "error: %s" % e
        bad += not ok
        print("%-8s %s  %s" % (name, "PASS" if ok else "FAIL", msg), flush=True)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
