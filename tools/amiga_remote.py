#!/usr/bin/env python3
"""Remote control of an Amiga running AmiAgent (amiagent/AmiAgent.c).

Connection settings come from (later wins): the file C:/amiga-dev/amiagent.cfg
(or --config), the environment (AMIAGENT_HOST, AMIAGENT_PORT, AMIAGENT_PASSWORD)
and the command line (--host, --port, --password). The config file has lines
"host = 192.168.1.50", "port = 6800", "password = ...".

  python tools/amiga_remote.py ping
  python tools/amiga_remote.py put build/amiga/AmiDrive Work:AmiDrive/AmiDrive
  python tools/amiga_remote.py get S:User-Startup user-startup.txt
  python tools/amiga_remote.py run "Work:AmiDrive/AmiDrive TEST"
  python tools/amiga_remote.py start "Work:AmiDrive/AmiDrive LOCAL=Work:OneDrive REMOTE=/Amiga"
  python tools/amiga_remote.py stop AmiDrive
  python tools/amiga_remote.py titles
  python tools/amiga_remote.py shot screen.png [x y w h]
  python tools/amiga_remote.py key ctrl [ms]          (or a raw key code: key 0x63)
      --window "Title" activates that window first
  python tools/amiga_remote.py activate "AmiDrive"    activate a window by the start of its title
  python tools/amiga_remote.py deploy [--remote-dir Work:AmiDrive] [--args "..."]
      stop the running program, upload build/amiga/AmiDrive, start it again
  python tools/amiga_remote.py push [--remote-dir Work:AmiDrive]
      upload build/amiga/AmiDrive only (then use "run" for CLI tests)
"""

import argparse
import os
import socket
import sys
import time

DEFAULT_CONFIG = "C:/amiga-dev/amiagent.cfg"
HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.join(HERE, "..", "build", "amiga", "AmiDrive")
REMOTE_DIR = "Work:AmiDrive"

# Amiga raw key codes
KEYS = {
    "esc": 0x45, "space": 0x40, "return": 0x44, "enter": 0x43, "tab": 0x42, "backspace": 0x41,
    "up": 0x4C, "down": 0x4D, "right": 0x4E, "left": 0x4F,
    "ctrl": 0x63, "lshift": 0x60, "rshift": 0x61, "capslock": 0x62,
    "lalt": 0x64, "ralt": 0x65, "lamiga": 0x66, "ramiga": 0x67, "del": 0x46, "help": 0x5F,
    "f1": 0x50, "f2": 0x51, "f3": 0x52, "f4": 0x53, "f5": 0x54,
    "f6": 0x55, "f7": 0x56, "f8": 0x57, "f9": 0x58, "f10": 0x59,
    "1": 0x01, "2": 0x02, "3": 0x03, "4": 0x04, "5": 0x05, "6": 0x06, "7": 0x07, "8": 0x08,
    "9": 0x09, "0": 0x0A,
    "q": 0x10, "w": 0x11, "e": 0x12, "r": 0x13, "t": 0x14, "y": 0x15, "u": 0x16, "i": 0x17,
    "o": 0x18, "p": 0x19, "a": 0x20, "s": 0x21, "d": 0x22, "f": 0x23, "g": 0x24, "h": 0x25,
    "j": 0x26, "k": 0x27, "l": 0x28, "z": 0x31, "x": 0x32, "c": 0x33, "v": 0x34, "b": 0x35,
    "n": 0x36, "m": 0x37,
}


class AgentError(Exception):
    pass


class Agent:
    def __init__(self, host, port, password, timeout=30):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.buf = b""
        reply = self.command(f"AUTH {password}")
        if not reply.startswith("OK"):
            raise AgentError("authentication failed")

    def _line(self):
        while b"\n" not in self.buf:
            data = self.sock.recv(65536)
            if not data:
                raise AgentError("connection closed")
            self.buf += data
        line, self.buf = self.buf.split(b"\n", 1)
        return line.decode("latin-1").rstrip("\r")

    def _exact(self, n):
        while len(self.buf) < n:
            data = self.sock.recv(max(65536, n - len(self.buf)))
            if not data:
                raise AgentError("connection closed")
            self.buf += data
        data, self.buf = self.buf[:n], self.buf[n:]
        return data

    def command(self, line, payload=None):
        self.sock.sendall(line.encode("latin-1") + b"\n")
        if payload is not None:
            self.sock.sendall(payload)
        reply = self._line()
        if reply.startswith("ERR"):
            raise AgentError(reply[4:])
        return reply

    def put(self, data, remote):
        self.command(f"PUT {len(data)} {remote}", data)

    def get(self, remote):
        size = int(self.command(f"GET {remote}").split()[1])
        return self._exact(size)

    def run(self, cmd):
        _, rc, size = self.command(f"RUN {cmd}").split()
        return int(rc), self._exact(int(size)).decode("latin-1")

    def start(self, cmd):
        self.command(f"START {cmd}")

    def stop(self, name):
        return int(self.command(f"STOP {name}").split()[1])

    def titles(self):
        size = int(self.command("TITLES").split()[1])
        lines = self._exact(size).decode("latin-1").splitlines()
        result = []
        for l in lines:
            if "|" in l:
                screen, window = l.split("|", 1)
                active = window.startswith("*")
                result.append((screen, window[1:] if active else window, active))
        return result

    def shot(self, area=None):
        cmd = "SHOT" + ("" if not area else " " + " ".join(str(v) for v in area))
        _, w, h, size = self.command(cmd).split()
        return int(w), int(h), self._exact(int(size))

    def activate(self, prefix):
        self.command(f"ACTIVATE {prefix}")

    def key(self, code, ms=100):
        self.command(f"KEY {code} {ms}")

    def close(self):
        try:
            self.sock.sendall(b"QUIT\n")
        except OSError:
            pass
        self.sock.close()


def load_settings(args):
    cfg = {}
    path = args.config or DEFAULT_CONFIG
    if os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            if "=" in line and not line.strip().startswith("#"):
                k, v = line.split("=", 1)
                cfg[k.strip().lower()] = v.strip()
    host = args.host or os.environ.get("AMIAGENT_HOST") or cfg.get("host")
    port = int(args.port or os.environ.get("AMIAGENT_PORT") or cfg.get("port") or 6800)
    password = args.password or os.environ.get("AMIAGENT_PASSWORD") or cfg.get("password")
    if not host or not password:
        sys.exit(f"set host and password in {path}, the environment or on the command line")
    return host, port, password


def save_png(path, w, h, rgb):
    try:
        from PIL import Image
        Image.frombytes("RGB", (w, h), rgb).save(path)
    except ImportError:
        with open(os.path.splitext(path)[0] + ".ppm", "wb") as f:
            f.write(b"P6\n%d %d\n255\n" % (w, h) + rgb)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--config")
    ap.add_argument("--host")
    ap.add_argument("--port")
    ap.add_argument("--password")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("ping")
    p = sub.add_parser("put"); p.add_argument("local"); p.add_argument("remote")
    p = sub.add_parser("get"); p.add_argument("remote"); p.add_argument("local")
    p = sub.add_parser("run"); p.add_argument("command")
    p = sub.add_parser("start"); p.add_argument("command")
    p = sub.add_parser("stop"); p.add_argument("name")
    sub.add_parser("titles")
    p = sub.add_parser("shot"); p.add_argument("out"); p.add_argument("area", nargs="*", type=int)
    p = sub.add_parser("key"); p.add_argument("key"); p.add_argument("ms", nargs="?", type=int, default=100)
    p.add_argument("--window", default="", help="activate the window whose title starts so first ('' = none)")
    p = sub.add_parser("activate"); p.add_argument("prefix")
    p = sub.add_parser("deploy")
    p.add_argument("--remote-dir", default=REMOTE_DIR)
    p.add_argument("--args", default="")
    p.add_argument("--local", default=GAME)
    p = sub.add_parser("push")
    p.add_argument("--remote-dir", default=REMOTE_DIR)
    p.add_argument("--local", default=GAME)
    p = sub.add_parser("stats"); p.add_argument("--wait", type=float, default=0)
    args = ap.parse_args()

    agent = Agent(*load_settings(args))
    try:
        if args.cmd == "ping":
            print(agent.command("PING"))
        elif args.cmd == "put":
            agent.put(open(args.local, "rb").read(), args.remote)
            print(f"{args.local} -> {args.remote}")
        elif args.cmd == "get":
            open(args.local, "wb").write(agent.get(args.remote))
            print(f"{args.remote} -> {args.local}")
        elif args.cmd == "run":
            rc, out = agent.run(args.command)
            sys.stdout.write(out)
            print(f"[rc {rc}]")
        elif args.cmd == "start":
            agent.start(args.command)
            print("started")
        elif args.cmd == "stop":
            print(f"Ctrl-C sent to {agent.stop(args.name)} process(es)")
        elif args.cmd == "titles":
            for screen, window, active in agent.titles():
                print(f"{screen} | {'*' if active else ' '} {window}")
        elif args.cmd == "shot":
            w, h, rgb = agent.shot(args.area or None)
            save_png(args.out, w, h, rgb)
            print(f"{w}x{h} -> {args.out}")
        elif args.cmd == "key":
            code = KEYS[args.key.lower()] if args.key.lower() in KEYS else int(args.key, 0)
            prefixes = [args.window] if args.window else []
            for prefix in prefixes:
                try:
                    agent.activate(prefix)
                    time.sleep(0.2)
                    break
                except AgentError:
                    pass                               # no such window: try the next one
            agent.key(code, args.ms)
        elif args.cmd == "activate":
            agent.activate(args.prefix)
        elif args.cmd == "deploy":
            name = os.path.basename(args.local)
            remote = args.remote_dir + ("" if args.remote_dir.endswith((":", "/")) else "/") + name
            n = agent.stop(name)
            if n:
                time.sleep(1.5)                       # let it close its window and AHI
            agent.put(open(args.local, "rb").read(), remote)
            agent.start(f"{remote} {args.args}".strip())
            print(f"stopped {n}, uploaded {os.path.getsize(args.local)} bytes to {remote}, started")
        elif args.cmd == "push":
            name = os.path.basename(args.local)
            remote = args.remote_dir + ("" if args.remote_dir.endswith((":", "/")) else "/") + name
            agent.put(open(args.local, "rb").read(), remote)
            print(f"uploaded {os.path.getsize(args.local)} bytes to {remote}")
        elif args.cmd == "stats":
            if args.wait:
                time.sleep(args.wait)
            found = [w for _, w, _ in agent.titles() if w.startswith("AmiDrive")]
            print(found[0] if found else 'no window with an "AmiDrive" title')
    except AgentError as e:
        sys.exit(f"agent: {e}")
    finally:
        agent.close()


if __name__ == "__main__":
    main()
