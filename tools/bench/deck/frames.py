"""Compares the frames two bench runs wrote with BENCH_SHOT_FLIPS: frames.py DIR_A DIR_B

DIR_A and DIR_B hold flip_N.ppm (N: the guest's flips since the state loaded). For every N both have, it says
whether the pixels are identical, else how many bytes differ and by how much at most. Two runs of one build
are the reference: frames that differ between them are not deterministic, so a difference between builds
there proves nothing.
"""
import glob
import os
import sys


def main():
    a_dir, b_dir = sys.argv[1], sys.argv[2]
    names = sorted(set(map(os.path.basename, glob.glob(a_dir + "/flip_*.ppm"))) & set(map(os.path.basename, glob.glob(b_dir + "/flip_*.ppm"))))
    same = 0
    for name in names:
        a = open(os.path.join(a_dir, name), "rb").read()
        b = open(os.path.join(b_dir, name), "rb").read()
        if a == b:
            same += 1
            continue
        if len(a) != len(b):
            print(f"  {name}: sizes differ")
            continue
        different = sum(x != y for x, y in zip(a, b))
        print(f"  {name}: {different} of {len(a)} bytes differ, by up to {max(abs(x - y) for x, y in zip(a, b))}")
    print(f"{len(names)} frames in common, {same} identical")
    return 0 if names and same == len(names) else 1


if __name__ == "__main__":
    sys.exit(main())
