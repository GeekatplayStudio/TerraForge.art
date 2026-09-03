"""Turn a TerraForge crash report's module+RVA stack into file:line.

    python scripts/resolve_crash.py [logs/crash_<stamp>.txt]

With no argument the newest crash file under logs/ is used. Frames inside
geekatplay_studio.exe are resolved with addr2line from the same MinGW
toolchain that built it (the build carries -g). The PE image base is read
from the exe header, since addr2line wants a virtual address, not an RVA.
"""
import glob
import os
import re
import shutil
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "geekatplay_studio.exe")


def image_base(path):
    with open(path, "rb") as f:
        f.seek(0x3C)
        pe = struct.unpack("<I", f.read(4))[0]
        f.seek(pe + 4 + 20)  # signature + COFF header -> optional header
        magic = struct.unpack("<H", f.read(2))[0]
        f.seek(pe + 24 + (24 if magic == 0x20B else 28))
        return struct.unpack("<Q" if magic == 0x20B else "<I",
                             f.read(8 if magic == 0x20B else 4))[0]


def find_addr2line():
    exe = shutil.which("addr2line")
    if exe:
        return exe
    for c in glob.glob(os.path.expandvars(
            r"%LOCALAPPDATA%\Microsoft\WinGet\Packages\*WinLibs*\mingw64\bin\addr2line.exe")):
        return c
    return None


def main():
    if len(sys.argv) > 1:
        report = sys.argv[1]
    else:
        files = sorted(glob.glob(os.path.join(ROOT, "logs", "crash_*.txt")))
        if not files:
            print("no crash reports under logs/")
            return 1
        report = files[-1]
    text = open(report, encoding="utf-8", errors="replace").read()
    print(f"== {report}")
    print(text)
    a2l = find_addr2line()
    if not a2l or not os.path.exists(EXE):
        print("(addr2line or the exe not found; raw stack only)")
        return 0
    base = image_base(EXE)
    rvas = re.findall(r"geekatplay_studio\.exe\+0x([0-9a-fA-F]+)", text)
    if not rvas:
        print("(no frames inside geekatplay_studio.exe)")
        return 0
    print("== resolved frames in geekatplay_studio.exe")
    # one call per address: with -i an address expands to several lines
    # (the inlining chain), so a single batched call cannot be paired back
    for rva in rvas:
        out = subprocess.run([a2l, "-e", EXE, "-f", "-C", "-i", "-p",
                              hex(base + int(rva, 16))],
                             capture_output=True, text=True).stdout
        lines = [l.replace(ROOT.replace("\\", "/") + "/", "") for l in out.splitlines()]
        print(f"  +0x{rva}: " + "\n            ".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
