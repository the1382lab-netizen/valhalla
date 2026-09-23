# Retarget the Manny template clips we use onto the MetaHuman base; root-lock them.
import os, traceback, unreal
d = os.path.dirname(os.path.abspath(__file__))
log = []
def w(m):
    log.append(str(m)); open(os.path.join(d, "manny_retarget.txt"), "w").write("\n".join(log))
RT = "/Game/Valhalla/Characters/MetaHuman/Retarget"
AN = "/Game/Valhalla/Characters/MetaHuman/Animations"
MA = "/Game/Characters/Mannequins/Anims/"
CLIPS = ["Unarmed/MM_Idle", "Unarmed/Walk/MF_Unarmed_Walk_Fwd", "Unarmed/Jog/MF_Unarmed_Jog_Fwd",
         "Unarmed/Attack/MM_Attack_01", "Unarmed/Attack/MM_Attack_02", "Unarmed/Attack/MM_Attack_03", "Unarmed/Attack/MM_ChargedAttack",
         "Rifle/HitReact/MM_HitReact_Front_Lgt_01", "Rifle/HitReact/MM_HitReact_Front_Med_01",
         "Death/MM_Death_Front_01", "Death/MM_Death_Back_01"]
at = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary
try:
    smesh = unreal.load_asset("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple")
    mmesh = unreal.load_asset("/Game/Valhalla/Characters/MetaHuman/Build/Medium/MHC_ValhallaBase/Body/SKM_MHC_ValhallaBase_BodyMesh")
    srig = unreal.load_asset(RT + "/IK_Manny") or at.create_asset("IK_Manny", RT, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    sc = unreal.IKRigController.get_controller(srig)
    sc.set_skeletal_mesh(smesh)
    sc.apply_auto_generated_retarget_definition()
    w("manny chains: %s root=%s" % ([str(c.chain_name) for c in sc.get_retarget_chains()], sc.get_retarget_root()))
    mrig = unreal.load_asset(RT + "/IK_MH_ValhallaBase")
    rtg = unreal.load_asset(RT + "/RTG_Manny_to_MH") or at.create_asset("RTG_Manny_to_MH", RT, unreal.IKRetargeter, unreal.IKRetargetFactory())
    c = unreal.IKRetargeterController.get_controller(rtg)
    c.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, srig)
    c.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, mrig)
    c.set_preview_mesh(unreal.RetargetSourceOrTarget.SOURCE, smesh)
    c.set_preview_mesh(unreal.RetargetSourceOrTarget.TARGET, mmesh)
    if c.get_num_retarget_ops() == 0:
        c.add_default_ops()
    c.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.SOURCE, srig)
    c.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET, mrig)
    for i in range(c.get_num_retarget_ops()):
        c.run_op_initial_setup(i)
    c.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
    c.auto_align_all_bones(unreal.RetargetSourceOrTarget.TARGET)
    w("ops: %s" % [str(c.get_op_name(i)) for i in range(c.get_num_retarget_ops())])
    eal.save_loaded_asset(srig, False); eal.save_loaded_asset(rtg, False)
    anims = [eal.find_asset_data(MA + n) for n in CLIPS]
    res = unreal.IKRetargetBatchOperation.duplicate_and_retarget(anims, smesh, mmesh, rtg, search="", replace="", prefix="MH_", suffix="", target_path=AN, use_source_path=False, include_referenced_assets=False, overwrite_existing_files=True)
    for ad in res:
        a = ad.get_asset()
        if isinstance(a, unreal.AnimSequence):
            a.set_editor_property("force_root_lock", True)
            eal.save_loaded_asset(a, False)
            w("  %s len=%.2f additive=%s skel=%s" % (a.get_name(), a.get_play_length(), a.get_editor_property("additive_anim_type"), a.get_editor_property("skeleton").get_name()))
    w("done")
except Exception:
    w(traceback.format_exc())
