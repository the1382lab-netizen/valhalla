import unreal, os
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "sit_ground.txt")
P = unreal.AnimPoseExtensions; W = unreal.AnimPoseSpaces.WORLD; LS = unreal.AnimPoseSpaces.LOCAL
mesh = unreal.load_asset("/Game/Valhalla/Characters/MetaHuman/Build/Medium/MHC_ValhallaBase/Body/SKM_MHC_ValhallaBase_BodyMesh")
om = unreal.AnimPoseEvaluationOptions(); om.set_editor_property("optional_skeletal_mesh", mesh)
oraw = unreal.AnimPoseEvaluationOptions()
g = lambda p, n: P.get_bone_pose(p, n, W).translation
low = lambda p: min(g(p, b).z for b in ("ball_l", "ball_r", "foot_l", "foot_r"))
s = unreal.load_asset("/Game/Valhalla/Characters/MetaHuman/Animations/MH_Sit")
nf = unreal.AnimationLibrary.get_num_frames(s); ln = s.get_play_length()
ts = [ln * i / nf for i in range(nf + 1)]
raw0 = P.get_bone_pose(P.get_anim_pose_at_time(s, 0, oraw), "pelvis", LS).translation.z
k = g(P.get_anim_pose_at_time(s, 0, om), "pelvis").z / raw0
base = low(P.get_anim_pose_at_time(s, 0, om))
pos = []; rot = []; scl = []; L = ["frames=%d len=%.2f k=%.4f base=%.2f" % (nf, ln, k, base)]
for t in ts:
    lift = max(0.0, low(P.get_anim_pose_at_time(s, t, om)) - base)
    tr = P.get_bone_pose(P.get_anim_pose_at_time(s, t, oraw), "pelvis", LS)
    v = tr.translation; pos.append(unreal.Vector(v.x, v.y, v.z - lift / k)); rot.append(tr.rotation); scl.append(unreal.Vector(1, 1, 1))
c = s.controller
c.open_bracket(unreal.Text("ground sit"), False)
c.set_bone_track_keys("pelvis", pos, rot, scl, False)
c.close_bracket(False)
unreal.EditorAssetLibrary.save_loaded_asset(s, False)
for t in ts[::4] + [ln]:
    p = P.get_anim_pose_at_time(s, t, om); L.append("t=%.2f pelvis=%.1f lowest=%.1f" % (t, g(p, "pelvis").z, low(p)))
open(OUT, "w").write("\n".join(L))
