"""Summary of one bench run: analyze.py LOG CSV [--skip FRAMES] [--json FILE]

LOG is the game's log (DQ8_GFX_STATS_EVERY=10), CSV its present log. The
measured window is the stretch from the state's load ("[savestate] loaded: ...
pad read L") to the end of the script ("[input] script over: quit at pad read
M"); the first --skip frames (default 120: the pipelines, textures and caches
the scene builds as it starts) are left out of the statistics and reported
apart.

Printed: frame rate, the frame time's percentiles (from the present log's
flips), the 1% low (the frame rate of the slowest 1% of the frames), the
frames over 20 / 25 / 33 ms, the longest frames, the threads' busy shares
and a fingerprint of the scene (meshes drawn each stats interval): two runs
of one scenario must print the same fingerprint, else the scene differed and
their frame rates are not comparable.
"""
import argparse
import csv
import hashlib
import json
import os
import re
import statistics
import sys


def percentile(ordered, p):
    return ordered[min(len(ordered) - 1, round(p / 100 * (len(ordered) - 1)))]


def parse_log(path):
    text = open(path, encoding="utf-8", errors="replace").read().splitlines()
    loaded = quit_at = None
    chunks, current = [], None
    for line in text:
        m = re.match(r"\[savestate\] loaded: .*pad read (\d+)", line)
        if m and loaded is None:
            loaded = int(m.group(1))
            chunks, current = [], None  # intervals before the load are not the scene's
        m = re.match(r"\[input\] script over: quit at pad read (\d+)", line)
        if m:
            quit_at = int(m.group(1))
        if loaded is None:
            continue
        if line.startswith("[native-pipeline] records"):
            current = [line]
            chunks.append(current)
        elif current is not None:
            current.append(line)
    intervals = []
    for chunk in chunks:
        body = "\n".join(chunk)
        fps = re.search(r"\[gfx\] ([0-9.]+) fps \(backend ([0-9]+)% of wall\) over ([0-9]+) frames", body)
        if not fps:
            continue
        busy = re.search(r"vif1 busy=([0-9]+)% gif busy=([0-9]+)% of wall \| main stall ([0-9.]+) ms", body)
        longest = re.search(r"longest frame ([0-9.]+) ms", body)
        meshes = re.search(r"meshes drawn ([0-9]+)", body)
        intervals.append({
            "fps": float(fps.group(1)), "backend": int(fps.group(2)), "frames": int(fps.group(3)),
            "vif1": int(busy.group(1)) if busy else 0, "gif": int(busy.group(2)) if busy else 0,
            "stall": float(busy.group(3)) if busy else 0.0,
            "longest": float(longest.group(1)) if longest else 0.0,
            "meshes": int(meshes.group(1)) if meshes else 0,
        })
    return loaded, quit_at, intervals


def parse_csv(path):
    with open(path, newline="") as f:
        rows = [r for r in csv.DictReader(f) if r["kind"] == "flip"]
    times = [float(r["taken_ms"]) for r in rows]
    return [b - a for a, b in zip(times, times[1:])]


def power(path, seconds):
    """The machine over the last `seconds` of the run (the measured window ends with the game)."""
    try:
        samples = [json.loads(line) for line in open(path) if line.strip()]
    except OSError:
        return {}
    if len(samples) < 4:
        return {}
    samples = samples[:-1]  # the last sample may be taken as the threads end
    end = samples[-1]["t"]
    window = [s for s in samples if s["t"] >= end - seconds]
    if len(window) < 3:
        return {}
    mean = lambda key: statistics.mean(s[key] for s in window if s.get(key) is not None) if any(s.get(key) is not None for s in window) else 0.0
    span = window[-1]["t"] - window[0]["t"] or 1.0
    cores = {}
    # A thread can end before the run does (the game's own at the quit): its last sample counts.
    for name in {n for s in window for n in s["cpu_ticks"]}:
        have = [s for s in window if name in s["cpu_ticks"]]
        used = have[-1]["cpu_ticks"][name] - have[0]["cpu_ticks"][name]
        cores[name] = round(used / os.sysconf("SC_CLK_TCK") / max(have[-1]["t"] - have[0]["t"], 1.0), 2)
    return {"watts": round(mean("watts"), 2), "tctl": round(mean("tctl"), 1), "edge": round(mean("edge"), 1),
            "tctl_end": window[-1].get("tctl") or 0.0, "gpu_busy": round(mean("gpu_busy"), 1), "gpu_mhz": round(mean("gpu_mhz")),
            "cpu_mhz": round(mean("cpu_mhz")), "cores": cores, "cores_total": round(sum(cores.values()), 2)}


def main():
    ap = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    ap.add_argument("log")
    ap.add_argument("csv")
    ap.add_argument("--skip", type=int, default=120)
    ap.add_argument("--json")
    ap.add_argument("--sample", help="sample.py's file: power, temperatures, clocks and CPU time by thread")
    args = ap.parse_args()
    loaded, quit_at, intervals = parse_log(args.log)
    if loaded is None:
        print("no state was loaded: see", args.log)
        return 1
    window = (quit_at - loaded) if quit_at is not None else None
    deltas = parse_csv(args.csv)
    if window is not None:
        deltas = deltas[-window:]
    steady = deltas[args.skip:]
    skipped_intervals = args.skip // 10
    steady_intervals = intervals[skipped_intervals:]
    if quit_at is None:
        print("warning: the script did not end (the run was cut): partial window")
    if len(steady) < 60:
        print("too few frames measured:", len(steady))
        return 1
    ordered = sorted(steady)
    fps = 1000.0 * len(steady) / sum(steady)
    slow1 = ordered[int(len(ordered) * 0.99):]
    result = {
        "frames": len(steady), "fps": round(fps, 2),
        "ft_p50": round(percentile(ordered, 50), 2), "ft_p95": round(percentile(ordered, 95), 2),
        "ft_p99": round(percentile(ordered, 99), 2), "ft_max": round(ordered[-1], 2),
        "low1_fps": round(1000.0 / statistics.mean(slow1), 2),
        "over20": sum(d > 20 for d in steady), "over25": sum(d > 25 for d in steady), "over33": sum(d > 33.4 for d in steady),
        "warmup_max": round(max(deltas[: args.skip]), 1) if deltas[: args.skip] else 0.0,
        "backend": round(statistics.mean(i["backend"] for i in steady_intervals), 1) if steady_intervals else 0,
        "vif1": round(statistics.mean(i["vif1"] for i in steady_intervals), 1) if steady_intervals else 0,
        "gif": round(statistics.mean(i["gif"] for i in steady_intervals), 1) if steady_intervals else 0,
        "stall": round(statistics.mean(i["stall"] for i in steady_intervals), 2) if steady_intervals else 0,
    }
    meshes = [i["meshes"] for i in steady_intervals]
    result["fingerprint"] = hashlib.sha1(",".join(map(str, meshes)).encode()).hexdigest()[:10]
    result["meshes_mean"] = round(statistics.mean(meshes)) if meshes else 0
    result["intervals"] = steady_intervals
    if args.sample:
        result.update(power(args.sample, sum(steady) / 1000.0))
    result["window"] = [loaded, quit_at]
    print(f"{args.log.rsplit('/', 1)[-1]}: {len(steady)} frames after {args.skip} of warm-up (pad reads {loaded} to {quit_at})")
    print(f"  fps {result['fps']:6.2f}   frame time ms: p50 {result['ft_p50']:5.2f}  p95 {result['ft_p95']:5.2f}  p99 {result['ft_p99']:5.2f}  max {result['ft_max']:6.2f}   1% low {result['low1_fps']:5.1f} fps")
    print(f"  frames over 20 / 25 / 33 ms: {result['over20']} / {result['over25']} / {result['over33']}   longest in the warm-up: {result['warmup_max']} ms")
    print(f"  GS backend {result['backend']}% of wall, VIF1 {result['vif1']}%, GIF {result['gif']}%, main stall {result['stall']} ms")
    print(f"  scene: fingerprint {result['fingerprint']}, {result['meshes_mean']} meshes per 10 frames")
    if "watts" in result:
        top = sorted(result["cores"].items(), key=lambda kv: -kv[1])[:5]
        print(f"  APU {result['watts']} W, Tctl {result['tctl']} C (end {result['tctl_end']}), GPU {result['gpu_busy']}% at {result['gpu_mhz']} MHz, "
              f"CPU {result['cpu_mhz']} MHz; cores used {result['cores_total']}: " + ", ".join(f"{k} {v}" for k, v in top))
    if args.json or True:
        with open(args.json or (args.log.rsplit(".", 1)[0] + ".json"), "w") as f:
            json.dump(result, f)
    return 0


if __name__ == "__main__":
    sys.exit(main())
