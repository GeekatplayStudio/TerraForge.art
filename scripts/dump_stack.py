"""Read a Windows minidump of TerraForge without a Microsoft debugger.

    python scripts/dump_stack.py <file.dmp> [--all-threads]

Prints the exception record, the loaded modules, and for the faulting
thread a *scanned* stack: every 8-byte word on the stack that points into
a loaded module, as module+RVA. Scanning over-reports (stale return
addresses linger), but the sequence of modules still tells the story —
and frames in geekatplay_studio.exe resolve with resolve_crash.py when the
binary that crashed was built with -g.
"""
import sys

from minidump.minidumpfile import MinidumpFile


def mod_of(mods, addr):
    for m in mods:
        if m.baseaddress <= addr < m.baseaddress + m.size:
            return m
    return None


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    path = sys.argv[1]
    all_threads = "--all-threads" in sys.argv
    md = MinidumpFile.parse(path)
    mods = list(md.modules.modules)
    print(f"== {path}")
    print(f"modules: {len(mods)}")
    for m in mods:
        name = m.name.split("\\")[-1]
        if "geekatplay" in name.lower() or name.lower().startswith(("ucrt", "msvcrt", "libstdc", "libgcc", "kernel32", "ntdll", "libwinpthread")):
            print(f"  {name:32s} base=0x{m.baseaddress:x} size=0x{m.size:x} ts={m.timestamp:#x}")
    exc = md.exception
    tid = None
    if exc and exc.exception_records:
        r = exc.exception_records[0]
        tid = r.ThreadId
        er = r.ExceptionRecord
        addr = er.ExceptionAddress
        m = mod_of(mods, addr)
        where = f"{m.name.split(chr(92))[-1]}+0x{addr - m.baseaddress:x}" if m else "?"
        code = getattr(er.ExceptionCode, "value", er.ExceptionCode)
        print(f"\nexception: code={code if isinstance(code, str) else hex(code)} "
              f"address=0x{addr:x} ({where}) thread={tid}")
    reader = md.get_reader()

    def read(addr, size):
        try:
            buf = reader.get_buffered_reader()
            buf.move(addr)
            return buf.read(size)
        except Exception as e:  # not in the dump
            return None

    for t in md.threads.threads:
        if not all_threads and tid is not None and t.ThreadId != tid:
            continue
        ctx = t.ContextObject
        rip = getattr(ctx, "Rip", None)
        rsp = getattr(ctx, "Rsp", None)
        print(f"\n-- thread {t.ThreadId} rip=0x{rip:x} rsp=0x{rsp:x}" if rip else f"\n-- thread {t.ThreadId}")
        if rip:
            m = mod_of(mods, rip)
            if m:
                print(f"   rip in {m.name.split(chr(92))[-1]}+0x{rip - m.baseaddress:x}")
        stack_top = t.Stack.StartOfMemoryRange
        stack_len = t.Stack.MemoryLocation.DataSize if hasattr(t.Stack, "MemoryLocation") else t.Stack.Size
        start = rsp if rsp else stack_top
        size = min(stack_len - (start - stack_top), 0x4000)
        data = read(start, size)
        if not data:
            print("   (stack memory not in dump)")
            continue
        seen = 0
        for i in range(0, len(data) - 7, 8):
            w = int.from_bytes(data[i:i + 8], "little")
            m = mod_of(mods, w)
            if m:
                name = m.name.split("\\")[-1]
                print(f"   [rsp+0x{i:04x}] {name}+0x{w - m.baseaddress:x}")
                seen += 1
                if seen > 60:
                    print("   ...")
                    break
    return 0


if __name__ == "__main__":
    sys.exit(main())
