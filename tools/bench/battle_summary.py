"""The frame rate of a battle run (battle_ab.sh): battle_summary.py LOG...

From the first [gfx] interval that draws 9000 or more texture levels, 40 intervals:
the frame rate (mean, median, min, tenth percentile), the VIF1 thread's and the
mesh stage's share of the wall, the longest frame. A paused scene repeats the
same texture count: read the "fps:" and "tex:" lines before comparing.
"""
import re, sys, statistics as st
def parse(path):
    rows=[];cur=None
    for l in open(path,errors="replace"):
        m=re.match(r"\[gfx\] ([0-9.]+) fps",l)
        if m: cur={"fps":float(m.group(1))}; rows.append(cur); continue
        if cur is None: continue
        m=re.search(r"vif1 busy=(\d+)%",l)
        if m and "vif1" not in cur:
            cur["vif1"]=int(m.group(1)); mb=re.search(r"mesh busy=(\d+)%",l); cur["mesh"]=int(mb.group(1)) if mb else 0
        m=re.search(r"texture levels (\d+)",l)
        if m and "tex" not in cur: cur["tex"]=int(m.group(1))
        m=re.search(r"longest frame ([0-9.]+) ms",l)
        if m and "lf" not in cur: cur["lf"]=float(m.group(1))
    return [r for r in rows if "tex" in r and "vif1" in r]
for p in sys.argv[1:]:
    r=parse(p)
    # the battle: from the first interval with tex>=9000 on, 40 intervals
    start=next((i for i,x in enumerate(r) if x["tex"]>=9000),None)
    if start is None: print(p,"no battle; max tex",max((x["tex"] for x in r),default=0)); continue
    w=r[start:start+40]
    f=[x["fps"] for x in w]
    print("%s: n=%d fps mean %.1f median %.1f min %.1f p10 %.1f | vif1 median %d | mesh median %d | tex median %d | longest frame median %.1f max %.1f"%(
      p.split("/")[-1],len(w),st.mean(f),st.median(f),min(f),sorted(f)[len(f)//10],st.median([x["vif1"] for x in w]),st.median([x["mesh"] for x in w]),st.median([x["tex"] for x in w]),st.median([x.get("lf",0) for x in w]),max(x.get("lf",0) for x in w)))
    print("   fps:"," ".join("%d"%x for x in f)); print("   tex:"," ".join(str(x["tex"]) for x in w))
