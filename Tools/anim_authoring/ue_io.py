"""Unreal-side helpers: read an AnimPose into a skel.Pose, write frames into an AnimSequence."""
import os, sys, json, unreal
HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path: sys.path.append(HERE)
from qm import *
import skel
P = unreal.AnimPoseExtensions
W = unreal.AnimPoseSpaces.WORLD; L = unreal.AnimPoseSpaces.LOCAL

def _t(tr): return ((tr.translation.x, tr.translation.y, tr.translation.z), (tr.rotation.x, tr.rotation.y, tr.rotation.z, tr.rotation.w))

def parents_for(names, pose):
    cache = os.path.join(HERE, "parents.json")
    if os.path.exists(cache):
        d = json.load(open(cache))
        if d["names"] == names: return d["parents"]
    world = [_t(P.get_bone_pose(pose, n, W)) for n in names]
    local = [_t(P.get_bone_pose(pose, n, L)) for n in names]
    parents = []
    for i in range(len(names)):
        if i == 0: parents.append(-1); continue
        best, be = -1, 1e9
        lt, lq = local[i]
        for j in range(i):
            pt, pq = world[j]
            p = v_add(pt, q_rot(pq, lt))
            e = v_len(v_sub(p, world[i][0]))
            if e < be:
                q = q_mul(pq, lq); dq = abs(sum(a*b for a, b in zip(q, world[i][1])))
                if dq > 0.9999: best, be = j, e
        parents.append(best)
    json.dump({"names": names, "parents": parents}, open(cache, "w"))
    return parents

BODY = "/Game/Valhalla/Characters/MetaHuman/Build/Medium/MHC_ValhallaBase/Body/SKM_MHC_ValhallaBase_BodyMesh"
def eval_opts():
    o = unreal.AnimPoseEvaluationOptions()
    try:
        o.set_editor_property("optional_skeletal_mesh", unreal.load_asset(BODY))
    except Exception:
        pass
    return o

def read_pose(anim, t=0.0):
    pose = P.get_anim_pose_at_time(anim, t, eval_opts())
    names = [str(n) for n in P.get_bone_names(pose)]
    local = [_t(P.get_bone_pose(pose, n, L)) for n in names]
    par = parents_for(names, pose)
    sp = skel.Pose(names, par, local); sp.fk()
    return sp, pose

def write_anim(path, frames, fps=30, root_lock=True, template="/Game/Valhalla/Characters/MetaHuman/Animations/MH_MM_Idle"):
    """frames: list of skel.Pose (same bone list). Creates/overwrites an AnimSequence at path."""
    eal = unreal.EditorAssetLibrary
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    a = eal.duplicate_asset(template, path)
    c = a.controller
    c.open_bracket(unreal.Text("valhalla keyed anim"), False)
    c.remove_all_bone_tracks(False)
    c.remove_all_curves_of_type(unreal.RawCurveTrackTypes.RCT_FLOAT, False)
    c.set_frame_rate(unreal.FrameRate(fps, 1), False)
    n = max(2, len(frames))
    c.set_number_of_frames(unreal.FrameNumber(n - 1), False)
    names = frames[0].names
    for bi, b in enumerate(names):
        pos = []; rot = []; scl = []
        for f in (frames if len(frames) > 1 else frames*2):
            t, q = f.local[bi]
            pos.append(unreal.Vector(*t)); rot.append(unreal.Quat(*q)); scl.append(unreal.Vector(1, 1, 1))
        c.add_bone_track(b, False)
        c.set_bone_track_keys(b, pos, rot, scl, False)
    c.close_bracket(False)
    a.set_editor_property("force_root_lock", root_lock)
    eal.save_loaded_asset(a, False)
    return a
