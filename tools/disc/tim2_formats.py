#!/usr/bin/env python3
"""Count the image formats of the TIM2 pictures in the disc's files (headers only).

Usage: python3 -I tools/disc/tim2_formats.py disc/

Nothing is copied from the disc: only the counts are printed. Use it to see which
texture formats the native producer (src/native_producer.cpp) has to take; a format
it refuses is logged as "[texture-refused]". Only TIM2 pictures are read: textures
inside other containers are not seen.
"""
import mmap, os, struct, sys, collections

IMG = {1: "CT16", 2: "CT24", 3: "CT32", 4: "T4", 5: "T8"}
root = sys.argv[1]
total = collections.Counter()
combos = collections.Counter()
bad = collections.Counter()

# What the native producer takes (src/native_producer.cpp: texture(), palette()):
# images CT32, CT24, CT16, T8, T4 up to 4096; palettes CT32 or CT16 of 16 or 256 entries.
def problem(img, cfmt, layout, ccols, w, h):
    if img not in ("CT32", "CT24", "CT16", "T8", "T4"):
        return "image format %s" % img
    if w == 0 or h == 0 or w > 4096 or h > 4096:
        return "size %dx%d" % (w, h)
    if img in ("T8", "T4"):
        if cfmt not in ("CT32", "CT16"):
            return "%s with palette %s" % (img, cfmt)
        if ccols not in (16, 256):
            return "%s with %d palette entries" % (img, ccols)
        if layout == "CSM2":
            return "%s with a CSM2 palette" % img
    return None
for dirpath, _, names in os.walk(root):
    if "/mc" in dirpath or "MOVIE" in dirpath:
        continue
    for n in sorted(names):
        if n.upper().endswith(".MVI"):
            continue
        path = os.path.join(dirpath, n)
        if os.path.getsize(path) < 64:
            continue
        counts = collections.Counter()
        with open(path, "rb") as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as m:
            at = m.find(b"TIM2")
            while at != -1:
                if at + 0x40 <= len(m):
                    version, fmt, pics = struct.unpack_from("<BBH", m, at + 4)
                    if version in (3, 4) and fmt in (0, 1) and 0 < pics < 64:
                        head = at + (0x10 if fmt == 0 else 0x80)
                        for _ in range(pics):
                            if head + 48 > len(m):
                                break
                            tot, clut, size, hsize, ccols = struct.unpack_from("<IIIHH", m, head)
                            pfmt, mips, ctype, itype, w, h = struct.unpack_from("<BBBBHH", m, head + 0x10)
                            if tot == 0 or hsize < 48 or hsize > 0x200:
                                break
                            img = IMG.get(itype & 0x3F, "?%d" % itype)
                            counts[img] += 1
                            cfmt = {0: "none", 1: "CT16", 2: "CT24", 3: "CT32"}.get(ctype & 0x3F, "?%d" % (ctype & 0x3F))
                            layout = "CSM2" if ctype & 0x80 else "CSM1"
                            combos[(img, cfmt if clut else "none", layout, ccols, mips)] += 1
                            why = problem(img, cfmt if clut else "none", layout, ccols, w, h)
                            if why:
                                bad[why] += 1
                            head += tot
                at = m.find(b"TIM2", at + 4)
        if counts:
            print(path, dict(counts))
            total.update(counts)
print("TOTAL", dict(total))
print("COMBINATIONS (image, palette, layout, entries, mipmaps): count")
for k, v in sorted(combos.items(), key=lambda kv: -kv[1]):
    print("  ", k, v)
print("NOT TAKEN BY THE PRODUCER:", dict(bad) if bad else "none")
