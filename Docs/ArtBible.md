# Valhalla 2.0 — Art Bible

B-15 Wave 0, 2026-09-22. The rules every new or replaced asset follows. The
plan and the asset list are in Google Drive (B-15); this file is the
reference that lives next to the code.

## 1. Direction

**A modern Neverwinter Nights remaster.** Classic D&D high fantasy seen from an
isometric camera: timber-frame and stone towns, keeps, taverns, heraldry, plate
and robes, an earthy palette. It is drawn with physically based materials, real
light and shadow and light atmospheric haze, not flat colour and outlines.

- **References:** Neverwinter Nights (2002), NWN2, NWN Enhanced Edition.
  Kevin's picks are in `Docs/reference/` (7 images, 2026-09-22). What they say:
  - **Local light is half the mood.** Torches, braziers, lanterns and magic
    glow pool warm (or coloured) light against cooler ambient, especially at
    dusk and indoors. Every light-bearing prop (lamp post, brazier, torch
    sconce) gets a real light, not just an emissive.
  - **Rich armour and cloth.** Plate with gold or silver trim and engraving,
    blue and violet enamel, scale and chain with coloured edging, belts,
    buckles and layered straps. Characters are the most saturated things on
    screen.
  - **Patterned stone underfoot.** Plazas and halls use laid patterns
    (circular cobble fans, mosaics, bordered flagstones), not a flat repeat.
    Use decal or inlay meshes over the tiling ground material.
  - **Dense set dressing.** Tents, barrels, crates, sacks, banners and weapon
    racks fill every camp and square.
- **In the spirit of, never copied.** No NWN or Forgotten Realms assets, names,
  logos or heraldry. Our own banners and sigils.
- **Everything is made by Opus 5.5 driving Blender through the Blender MCP
  connector**, as scripts that can be re-run. CC0 textures (Poly Haven,
  ambientCG) are used wherever they beat procedural ones. The connector's AI
  shape generators (Hyper3D, Hunyuan3D) and Sketchfab are **off**.
- **Humans only for now**, built so other races can be added later (section 9).

## 2. World scale (do not change)

Gameplay is tuned to the existing scale: capsules, attack and aggro ranges,
footprints, line of sight, fog of war and the web editor's map overlays all
assume it. **1 Unreal unit = 1 cm.**

| Thing | Size | Notes |
|---|---|---|
| Human character | **122 cm** tall | capsule radius 30, half-height 60 |
| Ground grid | **64 cm** tiles | AValhallaTileField instances |
| Wall module (VB_) | 64 wide × **180** tall × 25 thick | pivot bottom centre |
| Door wall module | 128 wide × 180 | opening ≥ 1.1 × character height |
| Roof module | 128 × 128 footprint | |

Props are built at **about 0.68 × real-world size** (122 cm ÷ 180 cm), so they
sit right next to a 122 cm person. For example:

| Prop | Game size (cm) | Real-world equivalent |
|---|---|---|
| Barrel | ⌀ 50, h 70 | 0.75 × 1.0 m |
| Crate | 52 × 52 × 50 | |
| Lamp post | h 222 | |
| Market stall | 136 × 96 × 190 | |
| Well | 100 × 86 × 160 | |
| Fence section | 64 wide × 90 | one grid tile |
| Sword | 62 long | 0.9 m arming sword |
| Staff | 150 | taller than the character, as a staff is |

**Trees and tall things:** readability from the camera beats realism. Keep trees
at 2.2–5 m game scale and canopies above head height, so they never hide a
fight. Check them from the gameplay camera.

**Pivots:** at ground level, centred on the footprint. **Axes:** Blender
Z-up, facing −Y; glTF export converts to Y-up; the Unreal importers handle the
rest (see `import_gltf_batch` / `import_character_glb`).

## 3. The camera decides what matters

The game is seen from above at an isometric angle, as NWN was:

- **Tops and silhouettes first.** Roofs, the tops of walls, stalls, barrels and
  shoulders are what the player sees most. Spend detail there.
- **Value contrast between ground and actors.** Ground is mid-to-dark and
  muted. Characters, NPCs, loot and interactive props must read lighter, more
  saturated or both, at a glance.
- **No eye-level-only detail.** Carvings under eaves or on the underside of a
  table are never seen.

## 4. Palette

Earthy ground and buildings, rich accents. Environment base materials stay at
**≤ 45% saturation**. Accents go higher: banners, awnings, heraldry, enamel,
armour trim, magic light (up to ~70%). Characters and their gear are the
richest colours on screen (section 3).

| Role | Hex |
|---|---|
| Timber (dark oak) | `#4A3526` |
| Plaster / daub | `#D9CDB4` |
| Thatch (weathered straw) | `#A68B55` |
| Stone (grey-brown) | `#7A746A` |
| Cobble | `#6E6A62` |
| Grass (muted) | `#5E7A3A` |
| Dirt path | `#7A6146` |
| Sand / sandstone | `#C2A878` |
| Iron | `#5A5E63` |
| Leather | `#6B4A30` |
| Accent: heraldic red | `#8C2F2A` |
| Accent: royal blue | `#2F4A7A` |
| Accent: forest green | `#3E5B3A` |
| Accent: gold trim | `#B08D3C` |

The first-pass saturated oranges and greens (paths, grass strips, thatch) are
the main thing Wave 1 fixes.

## 5. Materials and textures

**One master: `M_ValhallaPBR`** (`/Game/Valhalla/Materials`, built by
`valhalla_tools/build_pbr.py`). Every surface is an instance of it. It replaced
`M_ValhallaToon` in Wave 0; all 53 first-pass instances were re-parented with
roughness and metallic set by family (`reparent_to_toon()` is the rollback).

| Parameter | Use |
|---|---|
| `UseTextures` (switch) | off: flat `BaseColor`; on: texture set below |
| `BaseColorMap` / `NormalMap` / `ORMMap` | the texture set; ORM = R ambient occlusion, G roughness, B metallic |
| `BaseColor` | tint (multiplies the map). NPC skin tint writes this at runtime |
| `WorldAlignedUV` (switch) + `TextureWorldSize` | ground and floors: top-down world UVs, seamless across 64 cm tiles |
| `UVScale` | tiling for mesh UVs |
| `Roughness` / `Metallic` / `Specular` | untextured surfaces |
| `RoughnessScale` / `MetallicScale` / `AOStrength` | adjust the ORM channels |
| `MacroVariation` / `MacroScale` | large world-space brightness variation, breaks up repeats |
| `Grime` / `GrimeColor` | dirt in recesses (driven by AO) |
| `UseVertexColor`, `EmissiveColor` / `EmissiveStrength` | painted hair, glows |

**Texture sets** live in `Import/Textures/<SetName>/`:
`T_<Set>_BC.png` (sRGB), `T_<Set>_N.png` (**DirectX** normal),
`T_<Set>_ORM.png` (linear). The pipeline:

1. In Blender: `download_polyhaven_asset(id, "textures", "2k")`, then
   `valhalla_textures.export_texture_set(id, "<SetName>")`
   (`Blender assets/scripts/`). It flips Poly Haven's OpenGL normal to DirectX
   and packs ORM.
2. In Unreal: `import_texture_sets.import_set("<SetName>")` then
   `make_instance(...)` creates `/Game/Valhalla/Materials/Sets/MI_<SetName>`.

**Texel density (mid-range PC target):**

| Asset | Density | Typical map size |
|---|---|---|
| Ground, walls, buildings | 512 px per game-metre | 2K tiling sets |
| Props | 512–1024 px/m | 1K–2K (trim sheets shared per kit) |
| Character body | | 2K |
| Armour piece | | 1K (2K for chest) |
| Weapon, shield | | 1K |

No 4K textures without a reason written down here.

## 6. Budgets (mid-range PC: GTX 1660 / RX 5600 class, 1080p, 60 fps)

| Asset | Triangles | Material slots |
|---|---|---|
| Character body (with head, hands) | 8–12k | ≤ 2 |
| Armour piece | 1–3k | ≤ 2 |
| Fully equipped character | ≤ 25k | |
| Weapon / shield | 1–3k | ≤ 2 |
| Small prop (crate, barrel) | 0.5–2k | ≤ 2 |
| Medium prop (stall, well) | 2–6k | ≤ 3 |
| Building module | 1–5k | ≤ 3 |
| Tree (with leaf cards) | 5–15k | ≤ 2 |
| Creature | 6–15k | ≤ 2 |

Static environment meshes may use Nanite, but must still meet these budgets so
the kit works without it. Skinned meshes need 2–3 LODs.

**Rendering:** Lumen global illumination and reflections, virtual shadow maps,
Substrate on. Wave 1 ends with a performance check: 30 characters and NPCs on
screen in the market square at 1080p.

## 7. Lighting and post

`valhalla_tools/lighting_remaster.py` holds the look (the `LOOK` table) and is
used both to edit L_World in place and by the world rebuild tool:

- a warm late-afternoon sun from the south-west (pitch −48°, soft 1.2° source);
- a real-time sky light plus a faint cool, shadowless fill;
- height-fog haze that starts past the gameplay camera (1800 cm);
- a gentle grade: slightly warm gain, 0.95 saturation, 1.06 contrast, light
  bloom and vignette.
- **Local lights come with their props** (from Wave 1): lamp posts, torches and
  braziers carry warm point or spot lights (about 2700 K), shadowed only where
  it matters. A dusk variant of `LOOK` (low sun, stronger local light) is a
  Wave 1 option once the lamp posts light.

**No outlines.** The Phase 4c black outline is retired. `valhalla.Visual.Outline 1`
brings it back on the player camera for comparison. There is one sun, in
L_World: zone sublevels never carry their own directional light.

## 8. Naming

| Prefix | Asset |
|---|---|
| `SM_` | static mesh |
| `SK_` | skeletal mesh (body, hair, armour on SK_Valhalla_Skeleton) |
| `VB_` | vision blocker: walls that block line of sight and fog |
| `T_` | texture (`_BC`, `_N`, `_ORM` suffixes) |
| `M_` / `MI_` | master material / material instance |
| `A_` | animation |
| `UCX_` | custom collision (in the .glb, next to its mesh) |

**Replacements keep the asset name, path, pivot and footprint** of what they
replace. Every level and every item's `meshId`/`spriteId` then picks up the new
art with no rebuild. **`VB_` walls keep their exact footprint and collision.**
New assets get new names and are placed by hand (and B-05 protects hand work
from the rebuild tool).

## 9. Room for other races

- One humanoid skeleton (`SK_Valhalla_Skeleton`) for every humanoid: players,
  NPCs, goblins and skeletons. Races are proportion variants (bone scale), so
  all animations are shared.
- The body is a base mesh plus shape keys. A race is new proportions and a new
  head, not a new rig.
- Armour is authored on the human base with matching shape keys, so it follows
  a race's proportions instead of being remodelled.

## 10. Per-asset checklist

1. **Brief:** purpose, reference, size and footprint (copied from the old asset
   for a replacement), triangle and texture budget from section 6.
2. **Build** in Blender by script (`Blender assets/scripts/`), materials from a
   texture set or procedural, a viewport screenshot for review.
3. **Game-ready:** scale and pivot (section 2), collision, LODs or Nanite, the
   exact name, rig and skin for characters.
4. **Export and import:** `.glb` into `Import/`, the Unreal importers, material
   instances of M_ValhallaPBR.
5. **Check** in Bjorn's market square (the golden area) and in Play mode for
   anything animated; update the B-15 asset list.
