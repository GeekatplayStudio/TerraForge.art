"""Is the picture the same?

A geometry change that removes 98% of the triangles at distance is only
allowed if nobody can see it. This puts a number on that: the fraction of
pixels that differ at all, and the worst single-channel difference.
"""
import os
import struct
import sys
import zlib


def read_png(path):
    """Enough PNG to read an 8-bit RGB/RGBA image. No dependency to install."""
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    pos, idat, w = 8, b"", 0
    h = bit = colour = 0
    while pos < len(data):
        ln = struct.unpack(">I", data[pos:pos + 4])[0]
        typ = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            w, h, bit, colour = struct.unpack(">IIBB", body[:10])
        elif typ == b"IDAT":
            idat += body
        elif typ == b"IEND":
            break
        pos += 12 + ln
    assert bit == 8, f"{path}: {bit}-bit"
    ch = {0: 1, 2: 3, 4: 2, 6: 4}[colour]
    raw = zlib.decompress(idat)
    stride = w * ch
    out = bytearray(h * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if f == 1:
            for i in range(ch, stride):
                line[i] = (line[i] + line[i - ch]) & 0xFF
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif f == 3:
            for i in range(stride):
                a = line[i - ch] if i >= ch else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif f == 4:
            for i in range(stride):
                a = line[i - ch] if i >= ch else 0
                b = prev[i]
                c = prev[i - ch] if i >= ch else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return w, h, ch, bytes(out)


def compare(a_path, b_path):
    wa, ha, ca, a = read_png(a_path)
    wb, hb, cb, b = read_png(b_path)
    if (wa, ha) != (wb, hb):
        return None
    n = min(len(a), len(b))
    diff_px, worst = 0, 0
    stride = wa * ca
    for y in range(ha):
        ra = a[y * stride:(y + 1) * stride]
        rb = b[y * stride:(y + 1) * stride]
        for x in range(0, stride, ca):
            d = max(abs(ra[x] - rb[x]), abs(ra[x + 1] - rb[x + 1]),
                    abs(ra[x + 2] - rb[x + 2]))
            if d > 2:
                diff_px += 1
            worst = max(worst, d)
    return wa * ha, diff_px, worst


base = os.getcwd()
A, B = os.path.join(base, "shots_" + sys.argv[1]), \
       os.path.join(base, "shots_" + sys.argv[2])
print(f"{'view':10s} {'differing':>12s} {'of':>10s} {'%':>7s} {'worst':>6s}")
for name in ("ground", "low", "mid", "high", "orbital", "top"):
    pa, pb = os.path.join(A, name + ".png"), os.path.join(B, name + ".png")
    if not (os.path.exists(pa) and os.path.exists(pb)):
        print(f"{name:10s} missing")
        continue
    r = compare(pa, pb)
    if r is None:
        print(f"{name:10s} different sizes")
        continue
    total, diff, worst = r
    print(f"{name:10s} {diff:>12,} {total:>10,} {100.0*diff/total:>6.2f}% "
          f"{worst:>6d}")
