"""Samples the machine while a bench run goes: sample.py PID OUT [INTERVAL=0.5]

One JSON line per sample until PID ends: the time, the APU's power (amdgpu's
power1_average, watts), its temperatures (k10temp Tctl, amdgpu edge), the GPU's
busy share and clock, the mean CPU clock, and the CPU time each thread name of
the game has used so far (clock ticks, from /proc/PID/task/*/stat). analyze.py
reads it for the measured window.
"""
import glob
import json
import os
import re
import sys
import time


def hwmon(name):
    for path in glob.glob("/sys/class/hwmon/hwmon*"):
        try:
            if open(path + "/name").read().strip() == name:
                return path
        except OSError:
            pass
    return None


def read(path, scale=1.0):
    try:
        return float(open(path).read().split()[0]) * scale
    except (OSError, ValueError, IndexError):
        return None


def gpu_clock(card):
    try:
        for line in open(card + "/pp_dpm_sclk"):
            if line.rstrip().endswith("*"):
                return float(re.search(r"(\d+)Mhz", line).group(1))
    except (OSError, AttributeError):
        pass
    return None


def threads(pid):
    used = {}
    for stat in glob.glob(f"/proc/{pid}/task/*/stat"):
        try:
            text = open(stat).read()
        except OSError:
            continue
        name = text[text.index("(") + 1:text.rindex(")")]
        fields = text[text.rindex(")") + 2:].split()
        used[name] = used.get(name, 0) + int(fields[11]) + int(fields[12])  # utime + stime
    return used


def main():
    pid, out = int(sys.argv[1]), sys.argv[2]
    interval = float(sys.argv[3]) if len(sys.argv) > 3 else 0.5
    amd, k10 = hwmon("amdgpu"), hwmon("k10temp")
    card = os.path.dirname(glob.glob("/sys/class/drm/card*/device/gpu_busy_percent")[0]) if glob.glob("/sys/class/drm/card*/device/gpu_busy_percent") else None
    start = time.time()
    with open(out, "w") as f:
        while os.path.exists(f"/proc/{pid}"):
            mhz = []
            for path in glob.glob("/sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_cur_freq"):
                v = read(path, 1e-3)
                if v:
                    mhz.append(v)
            sample = {
                "t": round(time.time() - start, 2),
                "watts": read(amd + "/power1_average", 1e-6) if amd else None,
                "tctl": read(k10 + "/temp1_input", 1e-3) if k10 else None,
                "edge": read(amd + "/temp1_input", 1e-3) if amd else None,
                "gpu_busy": read(card + "/gpu_busy_percent") if card else None,
                "gpu_mhz": gpu_clock(card) if card else None,
                "cpu_mhz": round(sum(mhz) / len(mhz), 0) if mhz else None,
                "cpu_ticks": threads(pid),
            }
            f.write(json.dumps(sample) + "\n")
            f.flush()
            time.sleep(interval)


if __name__ == "__main__":
    main()
