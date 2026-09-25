"""B-06 step 1.9: the NPC Type Blueprints Eldmoor's population wears.

One Blueprint per *look*, not per template: the spawn point picks the template
(and so the weapon, stats and loot), the Type only decides what the body
wears. Kevin's direction (2026-09-24): leather outside Ashvane Keep, plate or
chainmail inside it, goblins at the camps. All armour is MetaHuman armour
(the only body the game uses; PieceForActiveBody resolves the folder).

Re-runnable: a Type that exists is updated in place, never duplicated.

    import valhalla_tools.make_eldmoor_npc_types as m; m.run()
"""

import unreal

FOLDER = "/Game/Valhalla/NPCs/Eldmoor"
TEMPLATE_BP = "/Game/Valhalla/NPCs/BP_NPC_TestEnemy"
EQ = "/Game/Valhalla/Characters/MetaHuman/Equipment/"

LEATHER = dict(chest="SK_chest_travelers_jerkin", legs="SK_legs_travelers", boots="SK_boots_ranger",
               gloves="SK_gloves_ranger_bracers")
CHAIN = dict(chest="SK_chest_priests_chain", legs="SK_legs_travelers", boots="SK_boots_iron_sabatons",
             gloves="SK_gloves_iron_gauntlets")
PLATE = dict(chest="SK_chest_iron_cuirass", legs="SK_legs_iron_plate", boots="SK_boots_iron_sabatons",
             gloves="SK_gloves_iron_gauntlets")

# look id -> (Blueprint name, pieces, helm, default template)
LOOKS = {
    "leather":       ("BP_NPC_Eld_Leather", LEATHER, "SK_hood_scout", "bandit_cutpurse"),
    "leather_bare":  ("BP_NPC_Eld_LeatherBare", LEATHER, None, "bandit_thug"),
    "leather_named": ("BP_NPC_Eld_LeatherNamed", dict(LEATHER, boots="SK_boots_iron_sabatons", gloves="SK_gloves_iron_gauntlets"),
                      None, "poacher_captain"),
    "chain":         ("BP_NPC_Eld_Chain", CHAIN, "SK_helm_iron_full", "renegade_footman"),
    "chain_caster":  ("BP_NPC_Eld_ChainCaster", CHAIN, None, "renegade_steward"),
    "plate":         ("BP_NPC_Eld_Plate", PLATE, "SK_helm_iron_full", "renegade_hallguard"),
    "plate_named":   ("BP_NPC_Eld_PlateNamed", PLATE, None, "named_kell_draven"),
    "outpost_guard": ("BP_NPC_Eld_OutpostGuard", CHAIN, "SK_helm_iron_full", "npc_harrow_guard_tam"),
    "townsfolk":     ("BP_NPC_Eld_Townsfolk", dict(chest="SK_chest_travelers_jerkin", legs="SK_legs_travelers",
                                                   boots="SK_boots_ranger", gloves=None), None, "npc_harrow_trader"),
    "goblin":        ("BP_NPC_Eld_Goblin", None, None, "goblin_scrapper_c2"),
}

# Goblin: grips measured on SK_Goblin by the retarget pass (Saved/ClaudeOps/goblin/goblin_grips.json).
GOBLIN_HEIGHT_CM = 112.5
GOBLIN_GRIPS = {
    "one_hand_grip": ("hand_r", (-5.284, 1.597, 1.301), (34.579, 5.858, 86.673)),
    "staff_grip":    ("hand_r", (-5.284, 1.597, 1.301), (78.538, 105.109, 16.616)),
    "bow_grip":      ("hand_l", (7.158, -2.988, -3.113), (-27.627, -99.952, -177.588)),
}
GOBLIN_PROP_SCALE = 0.85


def _asset(name):
    return unreal.load_asset(EQ + name) if name else None


def _ensure_bp(name):
    path = FOLDER + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path), False
    unreal.EditorAssetLibrary.make_directory(FOLDER)
    return unreal.EditorAssetLibrary.duplicate_asset(TEMPLATE_BP, path), True


def _grip(bone, loc, rot, scale):
    g = unreal.ValhallaNPCGripFrame()
    g.set_editor_property("bone", bone)
    g.set_editor_property("location", unreal.Vector(*loc))
    g.set_editor_property("rotation", unreal.Rotator(pitch=rot[0], yaw=rot[1], roll=rot[2]))
    g.set_editor_property("prop_scale", scale)
    return g


def run():
    report = []
    for look, (name, pieces, helm, template) in LOOKS.items():
        bp, created = _ensure_bp(name)
        cdo = unreal.get_default_object(bp.generated_class())
        cdo.set_editor_property("default_template_id", template)
        cdo.set_editor_property("wear_placeholder_kit", False)
        cdo.set_editor_property("name_override", "")
        if look == "goblin":
            for slot in ("chest", "helm", "legs", "boots", "gloves"):
                cdo.set_editor_property(slot + "_mesh_asset", None)
            cdo.set_editor_property("body_mesh_override",
                                    unreal.load_asset("/Game/Fab/Stylized_Goblin_Minion_-_Free/SK_Goblin"))
            cdo.set_editor_property("anim_folder_override", "/Game/Valhalla/Characters/Goblin/Animations")
            cdo.set_editor_property("body_mesh_scale", 122.0 / GOBLIN_HEIGHT_CM)
            for prop, (bone, loc, rot) in GOBLIN_GRIPS.items():
                cdo.set_editor_property(prop, _grip(bone, loc, rot, GOBLIN_PROP_SCALE))
        else:
            cdo.set_editor_property("chest_mesh_asset", _asset(pieces.get("chest")))
            cdo.set_editor_property("legs_mesh_asset", _asset(pieces.get("legs")))
            cdo.set_editor_property("boots_mesh_asset", _asset(pieces.get("boots")))
            cdo.set_editor_property("gloves_mesh_asset", _asset(pieces.get("gloves")))
            cdo.set_editor_property("helm_mesh_asset", _asset(helm))
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        unreal.EditorAssetLibrary.save_asset(FOLDER + "/" + name)
        report.append("%s %s (%s)" % ("created" if created else "updated", name, look))
    return report


def class_for_look(look):
    """The generated class of the Type for a look id (used by the placement script)."""
    name = LOOKS[look][0]
    return unreal.load_class(None, "%s/%s.%s_C" % (FOLDER, name, name))
