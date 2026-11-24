"""Compare bench runs: compare.py BASE_LABEL NEW_LABEL [SCENARIO...] [--runs DIR]

Reads the .json files `analyze.py` wrote in DIR/BASE_LABEL and DIR/NEW_LABEL
(DIR is $BENCH_HOME/runs), takes for each scenario the median of its runs, and
prints the change in frame rate, 1% low, p99 frame time and frames over 25 ms.
It says first whether the scenes were the same (fingerprints), since frame
rates of two different scenes do not compare; and the spread of each side's
repeated runs, to tell a change from noise: a difference smaller than the
runs' own spread is not one.
"""
import argparse
import glob
import json
import os
import statistics


def load(directory):
    runs = {}
    for path in sorted(glob.glob(os.path.join(directory, "*.json"))):
        scenario = os.path.basename(path).split(".")[0]
        runs.setdefault(scenario, []).append(json.load(open(path)))
    return runs


def similarity(a, b, tolerance=0.02):
    """Share of the stats intervals whose meshes drawn agree within a tolerance, two runs' scenes
    being the same when almost all do (a field scene can differ by a few per cent in one stretch
    from one run to the next: something in the game still follows the host's timing)."""
    pairs = list(zip((i["meshes"] for i in a["intervals"]), (i["meshes"] for i in b["intervals"])))
    return sum(abs(x - y) <= tolerance * max(x, y, 1) for x, y in pairs) / max(len(pairs), 1)


def med(runs, key):
    return statistics.median(r[key] for r in runs)


def spread(runs, key):
    values = [r[key] for r in runs]
    return max(values) - min(values)


def paired(b, n, key):
    """Run i of one side against run i of the other (ab.sh alternates them, so a pair saw the same machine)."""
    deltas = [y[key] - x[key] for x, y in zip(b, n)]
    return deltas


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("base")
    ap.add_argument("new")
    ap.add_argument("scenarios", nargs="*")
    ap.add_argument("--runs", default=os.path.join(os.environ.get("BENCH_HOME", "/run/media/deck/SD/Recomp/RogueBench"), "runs"))
    args = ap.parse_args()
    base, new = load(os.path.join(args.runs, args.base)), load(os.path.join(args.runs, args.new))
    for scenario in args.scenarios or sorted(set(base) & set(new)):
        if scenario not in base or scenario not in new:
            print(f"{scenario}: missing on one side")
            continue
        b, n = base[scenario], new[scenario]
        every = b + n
        worst = min(similarity(every[0], r) for r in every)
        exact = len({r["fingerprint"] for r in every}) == 1
        verdict = "identical" if exact else (f"alike ({worst:.0%} of the intervals within 2% at worst)" if worst >= 0.9
                                               else f"DIFFERENT ({worst:.0%} of the intervals within 2% at worst): not comparable")
        print(f"== {scenario}: {len(b)} run(s) vs {len(n)}; scene {verdict}")
        for key, label, better in (("fps", "fps", 1), ("low1_fps", "1% low fps", 1), ("ft_p99", "p99 frame ms", -1),
                                   ("over25", "frames > 25 ms", -1), ("backend", "GS backend %", -1), ("vif1", "VIF1 %", -1)):
            mb, mn = med(b, key), med(n, key)
            noise = max(spread(b, key), spread(n, key))
            delta = mn - mb
            verdict = "within noise" if abs(delta) <= noise else ("better" if delta * better > 0 else "WORSE")
            print(f"  {label:15s} {mb:8.2f} -> {mn:8.2f}  ({delta:+.2f}; runs spread {noise:.2f})  {verdict}")
        if len(b) == len(n) and len(b) > 1:
            d = paired(b, n, "fps")
            favour = sum(x > 0 for x in d)
            print(f"  paired fps deltas {', '.join(f'{x:+.2f}' for x in d)}: mean {statistics.mean(d):+.2f}, "
                  f"the new side ahead in {favour} of {len(d)} pairs (only meaningful for runs alternated by ab.sh)")


if __name__ == "__main__":
    main()
