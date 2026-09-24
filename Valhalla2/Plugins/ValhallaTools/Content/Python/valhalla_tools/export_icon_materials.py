"""B-15 Wave 4: export the material slots and parameters of every weapon,
shield and armour piece to ``Import/UI/icon_materials.json``, so the Blender
icon studio (``Blender assets/scripts/wave4_icons.py``) can rebuild the same
materials the game uses. Re-run after changing a material instance:

    py "<project>/Plugins/ValhallaTools/Content/Python/valhalla_tools/export_icon_materials.py"
"""
import os, json, traceback, unreal
_project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUT = os.path.normpath(os.path.join(_project, "..", "Import", "UI", "icon_materials.json"))
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
out = {"meshes": {}, "materials": {}}
def dump(m):
    name = m.get_name()
    if name in out["materials"]: return name
    e = {"path": m.get_path_name(), "class": m.get_class().get_name()}
    try:
        base = m
        while isinstance(base, unreal.MaterialInstance) and base.get_editor_property("parent"):
            base = base.get_editor_property("parent")
        e["master"] = base.get_name()
        if isinstance(m, unreal.MaterialInstance):
            e["scalars"] = {str(n): MEL.get_material_instance_scalar_parameter_value(m, n) for n in MEL.get_scalar_parameter_names(m)}
            e["vectors"] = {}
            for n in MEL.get_vector_parameter_names(m):
                c = MEL.get_material_instance_vector_parameter_value(m, n); e["vectors"][str(n)] = [c.r, c.g, c.b, c.a]
            e["textures"] = {}
            for n in MEL.get_texture_parameter_names(m):
                t = MEL.get_material_instance_texture_parameter_value(m, n)
                e["textures"][str(n)] = t.get_path_name() if t else None
            e["switches"] = {str(n): MEL.get_material_instance_static_switch_parameter_value(m, n) for n in MEL.get_static_switch_parameter_names(m)}
    except Exception:
        e["error"] = traceback.format_exc()
    out["materials"][name] = e
    return name
try:
    folders = ["/Game/Valhalla/Characters/Weapons", "/Game/Valhalla/Characters/MetaHuman/Equipment"]
    for f in folders:
        for p in EAL.list_assets(f, recursive=False):
            a = EAL.load_asset(p.split(".")[0])
            if isinstance(a, unreal.StaticMesh):
                out["meshes"][a.get_name()] = [[str(s.material_slot_name), dump(s.material_interface) if s.material_interface else None] for s in a.get_editor_property("static_materials")]
            elif isinstance(a, unreal.SkeletalMesh):
                out["meshes"][a.get_name()] = [[str(s.material_slot_name), dump(s.material_interface) if s.material_interface else None] for s in a.get_editor_property("materials")]
except Exception:
    out["error"] = traceback.format_exc()
open(OUT, "w").write(json.dumps(out, indent=1))
unreal.log("export_icon_materials: {} meshes, {} materials -> {}".format(len(out["meshes"]), len(out["materials"]), OUT))
