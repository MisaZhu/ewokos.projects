#!/usr/bin/env python3
"""QMP driver for headless EwokOS verification: screendump + input events."""
import json
import socket
import sys
import time

SOCK = "/tmp/wechat-qmp"


class QMP:
    def __init__(self, path=SOCK):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.connect(path)
        self.f = self.s.makefile("rb")
        self._read()  # greeting
        self.cmd("qmp_capabilities")

    def _read(self):
        line = self.f.readline()
        return json.loads(line) if line else None

    def cmd(self, execute, **args):
        msg = {"execute": execute}
        if args:
            msg["arguments"] = args
        self.s.sendall(json.dumps(msg).encode() + b"\n")
        while True:
            r = self._read()
            if r is None:
                raise RuntimeError("qmp closed")
            if "event" in r:
                continue
            if "error" in r:
                raise RuntimeError(r["error"])
            return r.get("return")

    def screenshot(self, path):
        self.cmd("screendump", filename=path)

    def mouse_abs(self, x, y):
        # virtio-tablet absolute coords are in [0, 0x7fff]
        self.cmd("input-send-event", events=[
            {"type": "abs", "data": {"axis": "x", "value": int(x * 0x7fff)}},
            {"type": "abs", "data": {"axis": "y", "value": int(y * 0x7fff)}},
        ])

    def click(self, x, y):
        self.mouse_abs(x, y)
        time.sleep(0.15)
        self.cmd("input-send-event", events=[
            {"type": "btn", "data": {"button": "left", "down": True}}])
        time.sleep(0.1)
        self.cmd("input-send-event", events=[
            {"type": "btn", "data": {"button": "left", "down": False}}])

    def key(self, qcode, down=True):
        self.cmd("input-send-event", events=[
            {"type": "key", "data": {"key": {"type": "qcode", "data": qcode},
                                     "down": down}}])

    def type_str(self, text):
        shift_map = {
            '!': ('1', True), '@': ('2', True), '#': ('3', True),
            '$': ('4', True), '%': ('5', True), '^': ('6', True),
            '&': ('7', True), '*': ('8', True), '(': ('9', True),
            ')': ('0', True), '_': ('minus', True), '+': ('equal', True),
            ':': ('semicolon', True), '"': ('apostrophe', True),
            '<': ('comma', True), '>': ('dot', True), '?': ('slash', True),
            '{': ('bracket_left', True), '}': ('bracket_right', True),
            '|': ('backslash', True), '~': ('grave_accent', True),
        }
        for ch in text:
            if ch == ' ':
                self.key('spc'); self.key('spc', False)
            elif ch == '\n':
                self.key('ret'); self.key('ret', False)
            elif ch.isupper():
                self.key('shift')
                self.key(ch.lower())
                self.key(ch.lower(), False)
                self.key('shift', False)
            elif ch in shift_map:
                k, _ = shift_map[ch]
                self.key('shift')
                self.key(k)
                self.key(k, False)
                self.key('shift', False)
            else:
                name = {'-': 'minus', '=': 'equal', '[': 'bracket_left',
                        ']': 'bracket_right', ';': 'semicolon',
                        "'": 'apostrophe', ',': 'comma', '.': 'dot',
                        '/': 'slash', '\\': 'backslash',
                        '`': 'grave_accent'}.get(ch, ch)
                self.key(name)
                self.key(name, False)
            time.sleep(0.03)


def ppm_to_png(ppm_path, png_path):
    sys.path.insert(0, "/home/misa/work/ewokos/projects/wechat/tools")
    from make_assets import png_write
    with open(ppm_path, "rb") as f:
        data = f.read()
    # P6 header: magic, whitespace/comments, w h, maxval
    idx = 0
    header = []
    while len(header) < 4:
        nl = data.index(b"\n", idx)
        line = data[idx:nl]
        idx = nl + 1
        if line.startswith(b"#"):
            continue
        header.extend(line.split())
    assert header[0] == b"P6"
    w, h, maxv = int(header[1]), int(header[2]), int(header[3])
    px = bytearray(w * h * 4)
    raw = data[idx:]
    for i in range(w * h):
        px[i * 4] = raw[i * 3]
        px[i * 4 + 1] = raw[i * 3 + 1]
        px[i * 4 + 2] = raw[i * 3 + 2]
        px[i * 4 + 3] = 255
    png_write(png_path, w, h, px)
    return w, h


if __name__ == "__main__":
    q = QMP()
    op = sys.argv[1]
    if op == "shot":
        out = sys.argv[2]
        q.screenshot("/tmp/wechat-shot.ppm")
        time.sleep(0.3)
        w, h = ppm_to_png("/tmp/wechat-shot.ppm", out)
        print(f"saved {out} {w}x{h}")
    elif op == "click":
        q.click(float(sys.argv[2]), float(sys.argv[3]))
        print("clicked", sys.argv[2], sys.argv[3])
    elif op == "move":
        q.mouse_abs(float(sys.argv[2]), float(sys.argv[3]))
    elif op == "type":
        q.type_str(sys.argv[2])
    elif op == "key":
        q.key(sys.argv[2])
        q.key(sys.argv[2], False)