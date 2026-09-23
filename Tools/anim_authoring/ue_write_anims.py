# Run inside the Unreal editor (py "<path>/ue_write_anims.py") after `python author.py`:
# writes out/*.json (hand-authored stance poses and attacks) into AnimSequences.
import os, sys, json, glob, traceback, unreal
H = os.path.dirname(os.path.abspath(__file__)); sys.path.append(H)
import importlib, qm, skel, ue_io
for m in (qm, skel, ue_io): importlib.reload(m)
AN = "/Game/Valhalla/Characters/MetaHuman/Animations/"
out = []
only = open(os.path.join(H, "mh_write_only.txt")).read().split() if os.path.exists(os.path.join(H, "mh_write_only.txt")) else None
try:
    for f in sorted(glob.glob(os.path.join(H, "out", "*.json"))):
        name = os.path.splitext(os.path.basename(f))[0]
        if name in ("grips", "report") or (only and name not in only): continue
        d = json.load(open(f))
        frames = []
        for loc in d["frames"]:
            p = skel.Pose(d["names"], [0]*len(d["names"]), loc); frames.append(p)
        a = ue_io.write_anim(AN + name, frames, fps=d["fps"])
        out.append("%s len=%.2f frames=%d" % (name, a.get_play_length(), len(frames)))
except Exception:
    out.append(traceback.format_exc())
open(os.path.join(H, "mh_write.txt"), "w").write("\n".join(out))
