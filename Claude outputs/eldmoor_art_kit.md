# Eldmoor Grasslands art kit (B-06 Phase 1, step 1.2)

Built 2026-09-24 by Opus 5.5 with the Blender MCP. This covers the art gaps listed in `EldmoorDesign.md` section 12. All 28 meshes are new, so they can be placed by hand without replacing anything. Asset list rows A-094 to A-107 are in `eldmoor_asset_rows.csv`.

| | Where |
|---|---|
| Unreal content | `/Game/Valhalla/Environment/Eldmoor/<Name>/StaticMeshes/<Name>` |
| Source script | `Blender assets/scripts/eldmoor_kit.py` (`build_all()`). It re-runs from nothing and uses the wave5_kit helpers. |
| Blender file | `Blender assets/Environment/valhalla_eldmoor_kit.blend` |
| GLB exports | `Import/Environment/Eldmoor/*.glb` |
| Unreal import | `Valhalla2/Saved/ClaudeOps/eld_import_ue.py` (valhalla_tools.import_kit.reimport_meshes + the collision pass below) |
| New materials | `MI_CliffRock` (Poly Haven `rock_face_03`, CC0, texture set `CliffRock`, recorded in `Import/Textures/texture_sets.json`), `MI_CharredTimber` (the Timber set tinted to char), `MI_Shroud` (flat black), `MI_BindGlow` (green-white emissive), `MI_Embers` (orange emissive). All are M_ValhallaPBR instances in `/Game/Valhalla/Materials/Sets`. |
| Review | Level `/Game/Valhalla/Maps/Dev/L_ArtReview_Eldmoor`. Blender renders are in `Valhalla2/Saved/ArtReview/eldmoor/`. |

## Conventions (same as every kit)

- Units are cm. The pivot sits on the ground at the centre of the footprint, except where a row below says otherwise.
- At yaw 0 the front or outside of a piece faces **+Y (south)**. To face a piece east use yaw -90, west use yaw +90, and north use yaw 180.
- **VB_ pieces** get the `VisionBlocker` profile, which blocks movement, the cursor, the camera and line of sight. The fog renderer takes a VB_ piece's footprint from its mesh bounding box (`ValhallaFogRenderer::GatherBlockerSegments`), so every VB_ piece fills its box.
- Nothing a player walks through or into is VB_. The gate, the cleft and the ramp are SM_ pieces with per-triangle ("complex as simple") collision, the same as `SM_KeepGate`. These three names are now in `import_kit.WALKABLE`.
- The line-of-sight trace runs at 90 cm (`ValhallaEyeHeight`). That is why the thicket is 1.3 m tall rather than waist high.
- Cliffs and boulders carry a 10-30 cm skirt below z = 0, so they can sit into sloped landscape without leaving a gap.
- Light-bearing props: `SM_Forge` and `SM_TorchSconce` are now in `fire_lights.FIRES`. Run `fire_lights` on a level after placing them and each one gets a flickering point light. Braziers already existed (`SM_Brazier`, Temple set, already in FIRES).

## 1. Cliffs and crags (A-094 to A-096)

| Asset | Footprint X x Y x Z (cm) | Pivot / notes | Collision | Tris | Serves |
|---|---|---|---|---|---|
| `VB_CliffFace_Tall` | 256 x 128 x 700 | Front face on local +Y. The top meets the landscape at 700 along the back (-Y) edge. The two ends share one rock profile, so modules butt in any order, and the ledges (horizontal strata at fixed heights) run on across joints. | VisionBlocker, box | 2,978 | Greyfell Tor's south and east faces (7 m). The tor's S face runs along y = 26 tiles, x 0-26; its E face along x = 26, y 0-26. That is about 7 modules per face plus a corner. |
| `VB_CliffFace_Mid` | 256 x 128 x 400 | As above | VisionBlocker, box | 1,928 | The ridge's north cliff (x 26-50, off the zone edge) and the keep plateau's north edge (4 m) |
| `VB_CliffFace_Low` | 256 x 96 x 180 | As above. 180 cm high so the landscape can bury the foot of a 1.5 m scarp. | VisionBlocker, box | 1,256 | The ridge's 1.5 m south scarp at y 28 above Hask's Hold, and anywhere else a 1-1.5 m step is needed |
| `VB_CliffCorner` | 128 x 128 x 700 | A buttress of stacked granite blocks. It turns a cliff run through 90 degrees or ends one. Scale Z to 0.57 beside Mid modules and 0.26 beside Low ones. | VisionBlocker, box | 4,390 | The tor's SE corner at (26,26), and the ends of the ridge runs |
| `SM_CliffCleft` | 256 x 128 x 700 | A Tall module with the **cave cleft** at its foot. The opening is 150 cm wide by about 225 cm tall, centred at local X 0, and closes to a crack up to 370 cm. The passage runs from the mouth (local Y +64) back to a black `MI_Shroud` wall at local Y -45. The floor is at z 0 (`MI_CaveFloor`). **Keep the landscape at cliff-foot height under this footprint.** Otherwise the tor's slope comes up through the passage floor, so flatten it or paint a hole. Put the hidden-portal trigger in the passage, about local (0, +10, 60). | BlockAll, per-triangle (walk in) | 3,006 | The unmarked Greyfell cleft at tile (6,27), trigger at (6,26), facing south (yaw 0), at the west end of the tor's S face. Screen it with the two `VB_BoulderLarge_A` at (3,30) and (9,31) as in section 8. |
| `VB_BoulderLarge_A` | 237 x 162 x 220 | A split granite block leaning on two smaller ones. Mossy tops. | VisionBlocker, 26-DOP hull | 480 | The cleft screen boulders (3,30) and (9,31) |
| `VB_BoulderLarge_B` | 179 x 118 x 160 | A lower, longer block | VisionBlocker, 26-DOP hull | 400 | Hask's line-of-sight rock at (31,42), shelf dressing, scree |
| `VB_RockOutcrop` | 237 x 170 x 142 | A flat top at about 125 cm that an NPC can stand on | VisionBlocker, 26-DOP hull | 480 | The crossbowman's rock at Hask's Hold (25,34) |

## 2. Palisade (A-097)

Sharpened logs, 2.4 m tall, with two back rails on the inside. At yaw 0 the outside faces south (+Y) and the rails are on the -Y side.

| Asset | Footprint (cm) | Pivot / notes | Collision | Tris | Serves |
|---|---|---|---|---|---|
| `VB_PalisadeWall` | 64 x 26 x 240 | One tile, centred on the wall line | VisionBlocker, box | 350 | Harrow's Rest palisade, x 36-64 / y 100-124 |
| `VB_PalisadeWall_Long` | 256 x 26 x 242 | Four tiles, for the long runs | VisionBlocker, box | 1,286 | The same |
| `VB_PalisadeCorner` | 35 x 35 x 275 | A heavy lashed post on the corner point where two runs meet | VisionBlocker, box | 126 | The four palisade corners |
| `SM_PalisadeGate` | 389 x 160 x 305 | **Replaces 6 tiles of wall.** The middle 4 tiles are the opening: 256 cm clear between the posts and 251 cm clear under the double log lintel. Both leaves stand open, swung inward to local -Y, so the footprint runs Y -142 to +18. Open state only. SM_, not VB_: a VB box would fog out the gateway. The posts hide little. | BlockAll, per-triangle (walk through) | 1,446 | The east gate (x 64, y 110-114): yaw -90, centred on (64, 112). The north gate (y 100, x 48-52): yaw 180, centred on (50, 100). The Harrow banner can hang on the lintel. |

## 3. Thornwood thicket (A-098)

| Asset | Footprint (cm) | Pivot / notes | Collision | Tris | Serves |
|---|---|---|---|---|---|
| `VB_Thicket_A` | 128 x 128 x 133 | Dense bush: leaf clumps (MI_LeafMass core, MI_LeafCluster cards) over a fern skirt. It fills its 2 x 2 tile box. | VisionBlocker, box: **blocks movement and sight** | 2,008 | TH1-TH3 rings, TH6 corridor clusters, and corners of the belts |
| `VB_Thicket_B` | 256 x 128 x 138 | The 4 x 2 tile version | VisionBlocker, box | 4,096 | TH4 and TH5 belts (2 tiles wide), long ring segments |

The thicket blocks movement as well as sight, as decision 8 asks. The corridors and clearings only work if players can't push through. Rotate and mirror placements freely: the clumps are random, so repeats don't read.

## 4. Smithy, stables, courtyard and keep pieces (A-099 to A-103, A-107)

| Asset | Footprint (cm) | Pivot / notes | Collision | Tris | Serves |
|---|---|---|---|---|---|
| `SM_Forge` | 207 x 89 x 236 | A stone hearth with glowing coals and a small flame. The hood and chimney are at the back (-Y), the bellows on the west end and the quench tub on the east end. Light from `fire_lights` (40 cd, 500 cm). | BlockAll, 26-DOP hull | 890 | Harrow's Rest smithy lean-to (x 38-45, y 114-120), with the open side south. The keep's smithy corner at the east end of the outer bailey. |
| `SM_Anvil` | 61 x 45 x 64 | An anvil on a log block, with a hammer on the face | BlockAll, 26-DOP hull | 310 | Both smithies, next to the forge |
| `SM_HayBale` | 63 x 42 x 37 | Stacks and scatters | BlockAll, box | 68 | The stables corner (west end of the outer bailey), farmyards |
| `SM_StrawPile` | 141 x 127 x 50 | A loose heap with a half-buried bale | BlockAll, 26-DOP hull | 468 | The same |
| `SM_TrainingPost` | 75 x 64 x 154 | A pell: a post on crossed feet with a sackcloth torso and an arm | BlockAll, 26-DOP hull | 332 | A2 inner courtyard sparring spots (66,16), (68,18), (78,16). It replaces the stump fallback. |
| `SM_CellBars` | 128 x 10 x 180 | An iron grille the width of a door module, with a locked barred door on its +X half. It blocks feet, not sight. | BlockAll, box | 320 | The undercroft gaol cell at the west end of A7 |
| `SM_StoneRamp` | 640 x 128 x 312 | **Pivot at hall-floor level (z 0)** at the footprint centre. The top edge is at local +X (z 0) and the foot at local -X (z -300), a 25.1 degree slope. It is a solid masonry wedge with a 12 cm kerb along its open +Y (south) side. | BlockAll, per-triangle (walkable slope) | 20 | The undercroft ramp: yaw 0, centre at tile (71, 6), i.e. the slot x 66-76 / y 5-7 = (4544, 384) cm, z = hall floor. The top is at (76,6) and the foot at (66,6). |
| `SM_KeepParapetLow` | 128 x 42 x 90 | A 90 cm parapet on the keep curtain's 128 cm module | BlockAll, box (stops feet, not sight) | 100 | The south edge of the ramp slot in the great hall (5 modules, x 66-76) |
| `SM_TorchSconce` | 13 x 21 x 55 | **Pivot at the wall fixing.** The torch stands out along +Y, so at yaw 0 it goes on a south-facing wall. Hang it at about 150 cm. Light from `fire_lights` (14 cd, 420 cm, shadowed). | none | 217 | Undercroft torches (A7 is lit by torches only), keep interiors |

## 5. Burnt Steadings, foragers and the outpost (A-104 to A-106)

| Asset | Footprint (cm) | Pivot / notes | Collision | Tris | Serves |
|---|---|---|---|---|---|
| `VB_ScorchedWall` | 128 x 26 x 185 | A burnt timber-frame wall: sill, three charred posts (one snapped), a broken head beam, burnt-off boards and a daub fragment. The top line is ragged. | VisionBlocker, box | 320 | Burnt Steadings farmsteads, Miller's Stead (x 130-136, y 1-6), C10's burnt farmyard |
| `SM_ScorchedPost` | 44 x 20 x 172 | A lone charred post with a snapped brace. The pivot is at the post, and the brace reaches to local +X. | BlockAll, 26-DOP hull | 72 | Corners and gaps of the burnt houses, fence lines |
| `SM_ScorchedBeams` | 199 x 162 x 49 | Fallen charred beams and boards on an ash bed | BlockAll, 26-DOP hull | 228 | The collapsed insides of the burnt houses |
| `SM_Cart` | 233 x 112 x 83 | A two-wheeled farm cart at rest on its shafts (the shafts point to local +X), loaded with sacks | BlockAll, 26-DOP hull | 1,936 | C10 Steadings Foragers ("loading a cart"), the stables corner |
| `SM_BindStone` | 132 x 132 x 126 | The SM_PortalMarker rune-stone with a green-white glow instead of the portal glow. It stands on a round flagstone dais ringed by five low stones and has **no trigger**. | BlockAll, 26-DOP hull | 1,052 | Harrow's Rest bind point at (48,110) |

## What section 12 lists that is not here, and why

- **Brazier (section 12.5):** it already exists. `SM_Brazier` in `/Game/Valhalla/Environment/Temple` has a real light through `fire_lights`. Use it for the gate braziers, the keep gate and the Lantern Tower.
- **Mist volume material (section 12.6):** this is engineering, not art (the design says so). It belongs to the fog work (`build_fog.py`), not to this kit.
- **Stair-down pieces:** not needed. Decision 6 chose the ramp, which is `SM_StoneRamp`.
- **Keep tower variant and wall-walk piece (section 12.7):** optional in the design and not needed for the approved layout. Not built.
- **Iron-bar cell front:** built as `SM_CellBars`, rather than reusing the portcullis inside `SM_KeepGate`, which cannot be pulled out as a separate mesh.
