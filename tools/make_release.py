#!/usr/bin/env python3
"""Build the AmiDrive release for the Amiga: a drawer with the installer, the program,
the icons and the documentation, and (with --lha) an LhA archive made on the Amiga.

  python tools/make_release.py            -> build/release/AmiDrive-<ver>/ (+ .info)
  python tools/make_release.py --lha      also: upload to RAM: of the Amiga (AmiAgent),
                                          pack with lha there, download
                                          releases/amiga/AmiDrive-<ver>.lha

Run "make" first (build/amiga/AmiDrive). Texts are written for the Amiga: Polish in
ISO-8859-2, LF line ends. Needs Pillow; --lha needs AmiAgent (tools/amiga_remote.py).

Archive layout:
  AmiDrive-<ver>/                    drawer (icon AmiDrive-<ver>.info next to it)
    Install-AmiDrive(.info)          Installer script, default tool Installer
    AmiDrive(.info)                  the program
    Docs/(.info)                     AmiDrive.txt, AmiDrive_en.txt, *_release*.txt (+icons),
                                     AmiDrive-opis.pdf
    WBStartup/AmiDrive(.info)        autostart icon template (default tool set by the installer)
"""

import argparse
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import make_icon  # noqa: E402

PROG_TOOLTYPES = ["(LOCAL=Work:OneDrive)", "(REMOTE=/Amiga)", "(INTERVAL=3)", "(UPLOAD=YES)",
                  "(CHARSET=LATIN2)", "(APPICON=YES)", "(ONCE)", "(WINDOW)", "DONOTWAIT"]
DOCS = ["AmiDrive.txt", "AmiDrive_en.txt", "AmiDrive_release.txt", "AmiDrive_release_en.txt"]
VIEWER = "SYS:Utilities/MultiView"


def version():
    text = open(os.path.join(ROOT, "src", "amidrive.h"), encoding="latin-1").read()
    return re.search(r'AMIDRIVE_VERSION\s+"([^"]+)"', text).group(1)


def amiga_text(src, dst):
    """UTF-8 -> ISO-8859-2 (Polish letters), LF line ends"""
    text = open(src, encoding="utf-8").read().replace("\r\n", "\n")
    open(dst, "wb").write(text.encode("iso-8859-2"))


def build(ver):
    name = f"AmiDrive-{ver}"
    top = os.path.join(ROOT, "build", "release")
    out = os.path.join(top, name)
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(os.path.join(out, "Docs"))
    os.makedirs(os.path.join(out, "WBStartup"))

    cloud, doc, drawer = make_icon.draw(), make_icon.draw_doc(), make_icon.draw_drawer()
    make_icon.save_icon(os.path.join(top, name + ".info"), drawer, drawer=True)

    shutil.copy(os.path.join(ROOT, "build", "amiga", "AmiDrive"), os.path.join(out, "AmiDrive"))
    make_icon.save_icon(os.path.join(out, "AmiDrive.info"), cloud, PROG_TOOLTYPES)

    amiga_text(os.path.join(ROOT, "install", "Install-AmiDrive"), os.path.join(out, "Install-AmiDrive"))
    make_icon.save_icon(os.path.join(out, "Install-AmiDrive.info"), make_icon.draw_installer(),
                        ["APPNAME=AmiDrive", "MINUSER=AVERAGE", "DEFUSER=AVERAGE"], "Installer")

    make_icon.save_icon(os.path.join(out, "Docs.info"), drawer, drawer=True)
    for d in DOCS:
        amiga_text(os.path.join(ROOT, "docs", "release", "amiga", d), os.path.join(out, "Docs", d))
        make_icon.save_icon(os.path.join(out, "Docs", d + ".info"), doc, default_tool=VIEWER)
    shutil.copy(os.path.join(ROOT, "docs", "AmiDrive-opis.pdf"), os.path.join(out, "Docs"))

    with open(os.path.join(out, "WBStartup", "AmiDrive"), "wb") as f:
        f.write(b"AmiDrive autostart: the icon starts AmiDrive in the background\n"
                b"when Workbench starts. Delete AmiDrive and AmiDrive.info here to\n"
                b"switch it off.\n")
    make_icon.save_icon(os.path.join(out, "WBStartup", "AmiDrive.info"), cloud, ["DONOTWAIT"],
                        "Work:AmiDrive/AmiDrive")
    print(out)
    return top, name


def lha(top, name, ver):
    """pack on the Amiga: upload to RAM:AmiDrive-release/, lha -r a, download"""
    remote = [sys.executable, os.path.join(ROOT, "tools", "amiga_remote.py")]
    stage = "RAM:AmiDrive-release"

    def run(*args):
        env = dict(os.environ, PYTHONIOENCODING="latin-1")
        r = subprocess.run(remote + list(args), capture_output=True, encoding="latin-1", env=env)
        print("".join(c for c in r.stdout.strip() if c.isprintable() or c == "\n"))
        if r.returncode:
            raise SystemExit(r.stderr)
        return r.stdout

    run("run", f"Delete {stage} ALL QUIET")
    run("run", f"MakeDir {stage}")
    for base, dirs, files in os.walk(top):
        rel = os.path.relpath(base, top).replace("\\", "/")
        dest = stage if rel == "." else f"{stage}/{rel}"
        for d in dirs:
            run("run", f"MakeDir \"{dest}/{d}\"")
        for f in files:
            if f.endswith(".lha"):
                continue
            run("put", os.path.join(base, f), f"{dest}/{f}")
    archive = f"RAM:{name}.lha"
    run("run", f"Delete {archive} QUIET")
    # "home/" before the pattern: paths in the archive relative to the staging drawer
    out = run("run", f"lha -r -a -N a {archive} {stage}/ #?")
    os.makedirs(os.path.join(ROOT, "releases", "amiga"), exist_ok=True)
    local = os.path.join(ROOT, "releases", "amiga", f"{name}.lha")
    run("get", archive, local)
    print(local, os.path.getsize(local), "bytes")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lha", action="store_true", help="pack with lha on the Amiga (AmiAgent)")
    args = ap.parse_args()
    ver = version()
    top, name = build(ver)
    if args.lha:
        lha(top, name, ver)


if __name__ == "__main__":
    main()
