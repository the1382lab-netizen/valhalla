# Eldmoor Grasslands - zone design (B-06, Phase 1, step 1.1)

Status: **for Kevin's approval**, 2026-09-24. Nothing here is built yet. Companion files: `eldmoor_layout.svg` / `.png` (the one-page sketch) and `eldmoor_layout.json` (the same data in zone-local cm, for a blockout script).

Coordinates in this document are **zone-local tiles** unless marked cm: 1 tile = 64 cm, tile (0,0) is the north-west corner, +X is east, +Y is south (the same convention as `maps/thumbs/*.json`). Multiply by 64 for cm. The zone is **143 x 143 tiles = 9152 x 9152 cm (91.5 m square)**, about 5x the ground area of Grasslands (4096 x 4096 cm).

## Decisions (Kevin, 2026-09-24)

The design was approved on 2026-09-24 with these answers; everything below is updated to match.

1. **Size:** 143 x 143 tiles stands; the 55-80 s portal-to-keep walk is fine.
2. **Levels:** Eldmoor 4-10; Greyfell about 8-12 with the named at the end.
3. **Names:** all accepted as proposed (zone, regions, keep, the eight named enemies).
4. **Desert link: yes.** A second portal from the Scorched Desert arrives in Burnt Steadings, at the empty Miller's Stead in the north-east corner (section 4).
5. **Terrain:** sculpted Unreal Landscape as proposed.
6. **Undercroft:** a Landscape hole under the great hall with a **ramp** down, not stairs (section 7, ramp spec).
7. **Castellan Vane:** 20% rare per 25-30 min cycle.
8. **Thornwood thickets block line of sight**; each clearing is a reveal. Thicket placements are in section 6 and the VB_ thicket mesh stays on the art-gap list.
9. **Harrow's Rest is the bind / respawn point**, and its guards will later fight enemies that follow players in; guard posts and wall patrols are laid out for that (section 5).
10. **Vendors** become backlog item B-22; the roster keeps its future vendor types.
11. **Fog:** 10 m clear to 16 m fully fogged (editable in the web editor). **Firelight shows through the fog as a glow** to about 24 m, so camps can be spotted from a distance; section 9 lists the beacons, the deliberately cold camps, and confirms nothing lights the hidden cleft.
12. Section 14 now lists only what is still open (nothing).

**Blockout review, 2026-09-24** (`EldmoorBlockoutReview.md`): the blockout matches this design. Accepted as built and folded into the data: entry ids prefixed with their zone; the postern tower at (50,16) with the postern door at (50,19); the hall fireplace at (80,5); the undercroft made by sinking the terrain under the hall to +1 m instead of a painted hole; the cleft trigger inside the passage at (6,25.2). Corrected on the design side: the cleft screen boulders now overlap in front of the mouth (section 8).

## 1. Pitch

Eldmoor was the Ashvane family's march: a river valley of sheep downs and dark woods below a rocky ridge, with the family keep on the rise at the head of the valley. The old lord died without an heir, the garrison was never paid, and the keep's castellan, **Ordric Vane**, simply kept it. His **Ashvane Renegades** now tax the roads, burn the farms that won't feed them, and let bandits and poachers work the lower valley for a cut. A cult that digs in the old barrow for the Drowned Crown of the valley's first king has been left alone because Vane finds them useful. The only law left is **Harrow's Rest**, a palisaded outpost on a knoll just inside the zone, held by a stubborn captain and a handful of guards and tradespeople who will not leave. Players arrive at level 4 by the road from Grasslands, work up the valley camp by camp, and end at levels 8-10 clearing the keep room by room, down to its cellar. Somewhere under the ridge, where the outpost guard says the rocks breathe cold air, is the way into Greyfell Cave.

Names (all original): the zone **Eldmoor Grasslands** (zone id `grasslands_v2`, unchanged); the river **Mistwater**; the outpost **Harrow's Rest**; the castle **Ashvane Keep**; the ridge and crag **Greyfell Highlands / Greyfell Tor**; the ruins **Kingsbarrow**; the woods **Thornwood**; the farms **Burnt Steadings**. Named enemies: **Merrik Sallow, the Poacher-King** (Thornwood, 7), **Hierophant Selwen Marr** (Kingsbarrow, 7), **Grimald Hask** (Highlands, 8), **Sergeant-at-Arms Kell Draven** (keep courtyard, 9), **Chaplain Mael Corvane** (keep chapel, 9), **Gatewarden Brann Coll** (postern tower, 9), **Warden Hesk** (undercroft, 10) and **Castellan Ordric Vane** (great hall, 10, rare).

## 2. Scale rules used throughout

| Fact | Value | Consequence |
|---|---|---|
| Tile | 64 cm | all bounds below are tile rectangles |
| Zone | 143 x 143 tiles, 91.5 m | 60-80 s to cross on foot; about ten screens wide |
| Screen | about 10 m across at default zoom | a camp (radius 2-3 m) plus its pull lane fits on one screen |
| Fog (design assumption) | clear 10 m, fully fogged by 16 m | anything that must stay hidden is more than 16 m from where players walk, or behind a vision blocker |
| Walking speed | 110-160 cm/s | portal to keep gate by road is about 87 m of road (measured along the polylines): 55-80 s (the plan's 3-4 minutes does not fit a 91 m zone; see open questions) |
| Outpost safe radius | **12 m** (19 tiles) from the palisade, not the plan's 40 m | 40 m would cover almost half the zone; 12 m keeps the whole knoll quiet and still leaves the first camps 14-17 m out. No enemy spawn point and no roamer path inside it (checked by script). |
| Camp footprint | radius 3-4 tiles (2-2.5 m), spawn points 1-2 tiles apart | |
| Group size | 3-6 players, forced grouping from level 4 | every camp past C2 assumes a group |

## 3. Regions


| # | Region | Tile bounds | Level | Terrain and elevation | Mood |
|---|---|---|---|---|---|
| 1 | **Southern Downs** | x 0-92, y 96-143 (92 x 47 tiles = 58.9 x 30.1 m) | 4-5 | +1 to +3 m rolling; outpost knoll +4 m | Open, windy, sheep-cropped grass, scattered rocks and single trees. The road out of the starter zones. |
| 2 | **Mistwater Vale (river valley)** | x 0-100, y 48-96 (100 x 48 tiles = 64.0 x 30.7 m) | 5-6 | 0 m at the river, banks +0.5 m, floor within 10 tiles of the river; low local mist | Cold, damp, reed banks, the stone bridge and its outlaw toll camp; mist pockets in the low ground. |
| 3 | **Thornwood** | x 92-143, y 84-143 (51 x 59 tiles = 32.6 x 37.8 m) | 5-7 | +1 to +2 m hummocky, no cliffs | Dense wood, fallen logs, stumps, ferns; game trails only; poachers and outlaws, later wolves. |
| 4 | **Kingsbarrow (old barrow ruins)** | x 100-143, y 48-84 (43 x 36 tiles = 27.5 x 23.0 m) | 6-7 | Long low mound +3 m (x104-136, y52-80); crypt hollow -1 m at (130,58) | Broken walls, leaning columns, a headless king statue; cultists of the Drowned Crown keep vigil. |
| 5 | **Greyfell Highlands** | x 0-50, y 0-48 (50 x 48 tiles = 32.0 x 30.7 m) | 7-8 | Ridge +3 to +4 m; Greyfell Tor +7 m plateau (0-26,0-26) with cliffs on its S and E faces; scree pocket (0-26,26-50) +1 to +3 m | Bare rock, dead trees, wind; brigands on the shelf; the hidden Greyfell cleft under the tor. |
| 6 | **Ashvane Keep and its approach** | x 50-94, y 0-48 (44 x 48 tiles = 28.2 x 30.7 m) | 8-10 | Plateau +4 m (y0-40) sloping down to +1 m at y48 (glacis); joins the ridge at the postern | The renegades' stronghold on the rise; banners, braziers, patrols on the glacis. |
| 7 | **Burnt Steadings** | x 94-143, y 0-48 (49 x 48 tiles = 31.4 x 30.7 m) | 8-9 | +1.5 to +2.5 m, gentle slope down to the river | Torched farmsteads, fences, blackened beams; renegade foraging parties. The Desert portal lands at the empty Miller's Stead in the north-east corner. |

The seven rectangles tile the zone exactly (Thornwood is drawn after the vale, so the strip x 92-100, y 84-96 is wood). Region names are working names; the pitch above gives the reasons for each.

**Terrain proposal (sculpted Landscape, modest heights).** Base ground 0 m is the river. The Southern Downs roll +1 to +3 m with the outpost knoll at +4 m (radius 20 tiles around (50,112), gentle ramps at both gates). Mistwater Vale is the flat floor within about 10 tiles of the river, with 0.5 m banks (river-set bank strips). Thornwood is hummocky +1 to +2 m with no cliffs. Kingsbarrow is one long oval mound, +3 m, x 104-136 / y 52-80, with a -1 m crypt hollow at (130,58). The Greyfell Highlands rise from +2 m at the vale to a +4 m ridge (x 26-50, y 6-26) that has a cliff on its north side (drops off the zone edge) and a 1.5 m scarp along its south side (y 28) above Hask's Hold, which sits on a +3 m shelf. **Greyfell Tor** is a +7 m crag, x 0-26 / y 0-26, with vertical cliff faces on its south and east sides; it is not walkable (a landmark, and the roof over the hidden cleft). Below it the scree pocket (x 0-26, y 26-50) slopes from +3 m at the cliff foot to +1 m. Ashvane Keep stands on a +4 m plateau (x 46-98, y 0-40) that slopes down as a grassy glacis to +1 m at y 48; the plateau's west edge meets the ridge at the postern, so the highland path arrives at keep level. The Burnt Steadings are +1.5 to +2.5 m, sloping to the river. Zone edges: the north edge of the keep plateau and the tor are cliffs to nothing; the other edges are a tree line (grassland trees A/B, dense) with a VB collision wall behind it.

## 4. Roads, portals, starts

**Roads (1 tile wide, dirt-road tiles) and tracks (half a tile, worn grass / dirt blend):**


- `road_main_south` (road) - Warden's Road (portal to outpost): (80,143) -> (80,138) -> (80,128) -> (72,120) -> (64,112)
- `road_outpost` (road) - Outpost street: (64,112) -> (50,112) -> (50,100)
- `road_main_north` (road) - Warden's Road (outpost to keep): (50,100) -> (52,90) -> (54,80) -> (56,70) -> (56,67) -> (56,61) -> (58,52) -> (62,44) -> (68,38) -> (72,36) -> (72,34)
- `road_barrow` (road) - Barrow Road: (80,128) -> (92,122) -> (102,114) -> (108,102) -> (110,88) -> (112,76)
- `path_barrow_footbridge` (track) - Barrow to footbridge: (112,76) -> (110,62) -> (108,52) -> (108,46) -> (108,40) -> (112,30) -> (116,22)
- `path_steadings_keep` (track) - Steadings to keep gate: (116,22) -> (104,22) -> (96,30) -> (88,38) -> (76,38) -> (72,36)
- `track_west` (track) - West track (Shepherd's Hollow): (72,120) -> (56,130) -> (40,134) -> (24,134) -> (14,124)
- `track_highland` (track) - Highland path (bridge to postern): (56,61) -> (48,58) -> (40,54) -> (34,48) -> (30,40) -> (32,32) -> (38,26) -> (46,22) -> (50,20)
- `trail_thornwood_a` (trail) - Game trail to Sallow's camp: (102,114) -> (112,112) -> (116,110)
- `trail_thornwood_b` (trail) - Game trail to Deadfall: (92,122) -> (104,128) -> (116,134) -> (123,136)
- `trail_steadings` (trail) - Cart track to the foragers: (116,22) -> (114,17) -> (113,14)
- `track_landing` (track) - River track (Desert landing to the footbridge): (135,10) -> (134,16) -> (128,24) -> (120,30) -> (114,36) -> (108,40)

**River Mistwater:** centreline (0,74) -> (14,76) -> (28,72) -> (42,66) -> (56,64) -> (70,60) -> (84,54) -> (98,46) -> (112,42) -> (126,36) -> (143,30), 5 tiles (3.2 m) wide between bank strips, so the 3.84 m humped stone bridge spans it with a bank on each end. Crossings: **stone bridge** at (56,64), N-S, span (56,61)-(56,67), on Warden's Road; **plank footbridge** at (108,43), N-S, span (108,40)-(108,46), on the barrow track. The river is not fordable anywhere else (bank collision): the two crossings are the two chokepoints between the level 4-7 south and the level 7-10 north.

**Portal from Grasslands.** Grasslands' town road runs north out of the map at x 2048 and there is nothing on that edge, so: on the **Grasslands side** a rune-stone portal `portal_to_eldmoor` at **(2048, 96) cm on the north edge**, at the end of the north road (the existing Desert portal is on the east edge at (4000, 864), no clash), with the zone entry `grasslands_from_eldmoor` at (2048, 380) cm on the road between the two northern houses. On the **Eldmoor side** the portal `portal_to_grasslands` sits on the south edge at tile (80,139) = (5120,8896) cm, with the entry `eldmoor_from_grasslands` at (80,132) = (5120,8448) cm (4.5 m north of the portal trigger, so arriving players do not bounce back). Player starts (the four the scaffold makes): (78,131), (82,131), (78,135), (82,135). Suggested world offset for step 1.0: Eldmoor north of Grasslands (X 0, Y about -30,000 to -40,000 world) so the geography matches the portal edges.

**Entry ids** are prefixed with the zone the entry stands in (`eldmoor_from_grasslands`, `grasslands_from_eldmoor`, `eldmoor_from_desert`, `desert_from_eldmoor`, and in Phase 2 `greyfell_from_eldmoor`, `eldmoor_from_greyfell`, `eldmoor_from_greyfell_lair`), because zone entries are looked up by id across the whole world and the starter zones already use `entry_from_grasslands` / `entry_from_desert`. Step 1.7 uses these ids in both overlays.

**Portal from the Scorched Desert (decision 4).** The Desert's east-west road (y 864) runs edge to edge; its west end holds the Grasslands portal (96,864) and entry (480,864), so the east end is free. On the **Desert side**: portal `portal_to_eldmoor` at **(3968, 864) cm on the east edge**, entry `desert_from_eldmoor` at (3616, 864) cm on the road (at the east end of the Desert's E-W road; its Grasslands portal (96,864) and entry (480,864) are on the west edge, no clash; a small rock stands near (4000,820) and may need nudging). On the **Eldmoor side**: portal `portal_to_desert` at tile (139,8) = (8896,512) cm on the east edge of Burnt Steadings, in the yard of **Miller's Stead**, a burnt and empty farmstead (ruins set, x 130-136 / y 1-6); entry `eldmoor_from_desert` at (135,10) = (8640,640) cm, 4 tiles inside the portal so nobody bounces back; a signpost at (133,12) reads "Harrow's Rest: west along the river, over the footbridge, then south by the Barrow Road. Do not go north." The **river track** (`track_landing`) leaves the stead and hugs the north bank to the footbridge's north end: (135,10) -> (134,16) -> (128,24) -> (120,30) -> (114,36) -> (108,40), then the barrow track south and the Barrow Road west to the fork and the outpost (about 100 m, 65-90 s).

*Design consequence.* Level-4 players from the Desert land in an 8-9 area. Handled four ways: (1) the landing is the zone's quiet corner: nothing spawns or patrols within 12 m of the entry (checked by script: C10 is 14.7 m away, R7's nearest point 14.4 m), so a player who stands still is safe; (2) the only things they can see are the signpost, the ruin, and two glows on the edge of the fog: C10's cooking fire to the west (a warning) and the Lantern Tower far to the south-west (a landmark); (3) the river track keeps the river on their left and the Steadings' camp and patrol on their right, with C10 moved west to (112,10) so it lies off the track, and R7 crossing the track only once, at (112,32) above the footbridge, with a lantern that shows it coming; (4) the walk to Harrow's Rest passes C4 (5-6, 8 tiles off the track, its fire visible) and the barrow (6-7, camp on top of the mound, off the track), both avoidable by staying on the track. It is a real EverQuest-style run through a higher zone: dangerous if you dawdle, safe if you keep moving, and the fires tell you where not to go.

**Signposts:** at the portal ("Harrow's Rest - keep to the road"), at the Barrow Road fork (80,128) ("W: Harrow's Rest / N: Warden's Road, Ashvane Keep / E: Kingsbarrow"), outside the outpost's east gate (66,115), and at the bridge's south end ("Ashvane Keep - travellers are taxed"). No signpost anywhere in the Highlands.

**Suggested bind / default spawn:** Harrow's Rest, tile (50,112) = (3200, 7168) cm, replacing the placeholder `defaultSpawn` in zones.json (open question 5).

## 5. Harrow's Rest (the outpost)

A palisade (art gap: palisade wall modules and a gate) on the knoll, **x 36-64, y 100-124 (28 x 24 tiles = 17.9 x 15.4 m)**, at +4 m. Two gates: **east gate** at x 64, y 110-114 (4 tiles, 2.56 m; Warden's Road arrives here) and **north gate** at y 100, x 48-52 (the road to the bridge and the keep). Inside, a cobbled street from the east gate to the well to the north gate; the rest is packed dirt and grass. Lamp posts flank both gates and the well (real warm lights, the outpost's glow through the fog). Everything is single-storey for the top-down camera; the tavern kit is two-storey on the outside but only its ground floor is used, and its roof hides when a player is inside.

**Buildings (tile footprints):**

| Building | Footprint | Kit | Notes |
|---|---|---|---|
| The Broken Spur (tavern kit) | x 38-47, y 102-109 (9 x 7 tiles = 5.8 x 4.5 m) | tavern | two-storey kit, only the ground floor is used; roof hides inside |
| Trader's house (town kit) | x 52-60, y 102-108 (8 x 6 tiles = 5.1 x 3.8 m) | town | timber walls, thatch, one door on the south side |
| Smithy lean-to | x 38-45, y 114-120 (7 x 6 tiles = 4.5 x 3.8 m) | town | open south side; forge + anvil are an art gap |
| Provisioner's stall | x 49-51, y 116-118 (2 x 2 tiles = 1.3 x 1.3 m) | town | market stall |
| Captain's bell tent | x 56-59, y 111-114 (3 x 3 tiles = 1.9 x 1.9 m) | camp |  |
| Guard A-frame tent | x 57-59, y 117-119 (2 x 2 tiles = 1.3 x 1.3 m) | camp |  |
| Guard A-frame tent | x 60-62, y 117-119 (2 x 2 tiles = 1.3 x 1.3 m) | camp |  |

**Props:** well at (50,111); campfire at (56,121); log_seat at (55,122); log_seat at (58,122); supply_pile at (61,121); weapon_rack at (46,115); lamp_post at (62,109); lamp_post at (62,115); lamp_post at (47,101); lamp_post at (53,101); lamp_post at (52,110); signpost at (66,115); brazier at (65,109) (gate brazier, real light); brazier at (47,99) (gate brazier, real light); bind_stone at (48,110) (rune-stone variant without a portal trigger); barrel at (61,104); crate at (61,106); picket_fence at (38,110) (6 sections along the tavern yard, y=110, x38-44). Plus barrels, crates and sacks around the trader's door, a picket-fenced yard behind the tavern, and the Harrow banner (a grey tower on green; our own heraldry) over the east gate.

**Friendly NPC roster (10).** All `hostility: friendly` (new field on NPC Types: friendly / neutral / hostile), `behaviorType: stationary`, cannot be attacked, never aggro. Vendor types are what they will sell once trading exists.

| Id | Name | Role | Future vendor | Tile | Hail line |
|---|---|---|---|---|---|
| `npc_harrow_captain` | Captain Bessa Harrow | Outpost Captain | none (later: quests / bind) | (52,113) | "Harrow's Rest holds because nobody has told it to fall yet. Stay inside the stakes after dark, and don't cross the bridge alone." |
| `npc_harrow_guard_tam` | Tam Pellow | Guard, east gate | none | (65,111) | "Road's yours as far as the bridge. Past the bridge, it's theirs." |
| `npc_harrow_guard_iselle` | Iselle Dunmore | Guard, north gate | none | (51,102) | "Mind the mist in the vale. Things stand in it and don't move until you're close." |
| `npc_harrow_guard_orrin` | Orrin Blackwater | Guard, west wall | none | (38,111) | "Some nights you can see the Lantern Tower's brazier from here, up on the keep. That's Vane's men laughing at us." |
| `npc_harrow_guard_wyn` | Wyn Cotter | Guard, south wall | none | (46,122) | "Chased a brigand runner up under the ridge once. There's cold air coming out of the rocks under the tor, cold as a cellar. I didn't stay to learn why." |
| `npc_harrow_smith` | Hedda Stroud | Blacksmith | weapons, repair | (40,117) | "Iron's dear this far from a town. Bring me back what the renegades drop and we'll talk." |
| `npc_harrow_armourer` | Pol Garrick | Armourer | armour, shields | (44,117) | "Ashvane plate, if you can pry it off them. I can make it fit you." |
| `npc_harrow_provisioner` | Mother Ada Quill | Provisioner | food, drink, bandages, torches | (50,119) | "Bread, salt pork, bandages. Nothing fancy, nothing poisoned, which is more than the cultists can say." |
| `npc_harrow_trader` | Corwen Ashby | General Trader | general goods; buys anything | (56,105) | "I buy anything that isn't still bleeding. Barrow trinkets fetch the best price, if you're brave." |
| `npc_harrow_innkeeper` | Maud Fenwick | Innkeeper, The Broken Spur | drink; later: room / bind | (42,106) | "A bed's a bed, even one this close to the vale. Don't track the mist in with you." |

**Bind and respawn (decision 9).** Harrow's Rest is the Eldmoor bind point: a bind stone (a rune-stone variant with no portal trigger) at (48,110) beside the well; respawn and `zones.json` `defaultSpawn` at (50,112) = (3200,7168) cm. A death anywhere in the zone returns you to the knoll, 20 m from the Grasslands portal and on the road to everything.

**Guard posts and patrols (decision 9, for the later "guards fight" feature).** Each gate has a guard post two tiles inside it with a brazier (real light) and a weapon rack, so a player chased home runs through the gate and the guard meets the pursuer in the gateway, a one-gate chokepoint. Two guards walk the walls and converge on either gate; the captain holds the centre. Guards are level 12 (above the zone) so nothing in Eldmoor can farm them, and they leash back to their posts.

- `npc_harrow_guard_tam`: gate post at (65,111). stands outside the east gate by its brazier; later: engages anything hostile that comes within 10 tiles of the gate, fights in the gateway, leashes to the post
- `npc_harrow_guard_iselle`: gate post at (51,102). inside the north gate by its brazier; same rule
- `npc_harrow_guard_orrin`: wall patrol at (38,111), patrol (38,104) -> (38,120). walks the inside of the west palisade, 20 s each way; later: converges on either gate
- `npc_harrow_guard_wyn`: wall patrol at (46,122), patrol (40,122) -> (60,122). walks the inside of the south palisade; same
- `npc_harrow_captain`: command post at (52,113). by the well at the centre, weapon rack beside her; later: joins any fight inside the stakes

Wyn Cotter's line is the hint for the hidden Greyfell cleft. Orrin Blackwater's line names the Lantern Tower, the zone's landmark. **Safe radius:** no enemy spawn point or roamer path within 12 m of the palisade (the nearest are C1 at 16 m, R1 at 14 m and R3 at 12.8 m).

## 6. Camps

All camps are humanoid until Wave 6 creatures exist. Each camp is a clearing or ruin with camp props (tents, fire, bedrolls, weapon rack, supply pile) so its fire glow is the first thing a player sees through the fog. Spawn points sit 1-2 tiles apart around the fire; "linked" means the spawns share social aggro (assist range 4 tiles); everything else is a single pull. Respawn timers are EQ-style: the whole camp cycles on the same timer, named placeholders take longer. Level bands and colours match the sketch.


| Id | Name | Region | Centre tile (cm) | Level | Spawns | NPC archetypes (suggested template ids) | Respawn | Pull notes |
|---|---|---|---|---|---|---|---|---|
| **C1** | Wood's-Edge Bandits | southern_downs | (98,128) = (6272,8192) cm | 4-5 | 3 | bandit_cutpurse x2, bandit_lookout x1 | 6 min | First camp out of the portal. Lookout stands on the road side at (95,126); pull him to the Barrow Road with a bow or spell, the two cutpurses stay put (social range 4 tiles). Add risk: R1 footpad walks past every ~90 s. |
| **C2** | Shepherd's Hollow | southern_downs | (8,114) = (512,7296) cm | 4-5 | 4 | bandit_cutpurse x2, bandit_archer x1, bandit_thug x1 | 6 min | A dip in the western downs with a ruined shepherd's hut (ruins set). The archer stands on the hut wall and sees the track end at (14,124): pull from behind the boulder at (14,118) to break her line of sight. Thug (lvl 5) is the boss; cutpurses run (flee at 20% hp) toward R2's track. |
| **C3** | Mistwater Toll (ford camp) | mistwater_vale | (48,72) = (3072,4608) cm | 5-6 | 4 | outlaw_footpad x2, outlaw_bowman x1, outlaw_ringleader x1 | 8 min | Outlaws 'taxing' the stone bridge from the south bank, 8 tiles west of the road. Pull single footpads from the road bend at (54,80); the ringleader and bowman are linked (pull as a pair). Fight on the road, not the bank: the mist pocket hides adds. R3 courier is the roaming add. |
| **C4** | Footbridge Poachers | mistwater_vale | (98,54) = (6272,3456) cm | 5-6 | 4 | poacher x2, poacher_bowman x1, poacher_houndmaster x1 | 8 min | South bank west of the plank footbridge. Approached from the barrow track at (108,52). The houndmaster (lvl 6) gets a hound when creatures arrive (placeholder: a second poacher). Chokepoint: the footbridge itself, single file. Do not fight on the north bank: R7 patrol crosses the river track at (112,32). |
| **C5** | Sallow's Camp (Poacher-King) | thornwood | (120,108) = (7680,6912) cm | 6-7 | 5 | poacher x2, poacher_bowman x1, poacher_skinner x1, poacher_captain / Merrik Sallow x1 | 10 min, named slot 20 min, 25% Merrik Sallow, the Poacher-King | Clearing ringed by thicket TH1 (blocks sight) and fallen logs, one 3-tile gap on the west at (114,110) where the game trail ends. From the trail you see only the fire glow; step into the gap to see the fire and the captain/Merrik with the two poachers (linked). The skinner at the racks on the north side is a single pull once you are in the gap. Bowman on a stump at (123,105) adds if you fight inside the ring: pull to the gap and fall back down the trail, where the thicket breaks his line of sight. |
| **C6** | Deadfall Hideout | thornwood | (130,136) = (8320,8704) cm | 5-6 | 4 | outlaw_footpad x2, outlaw_bowman x1, outlaw_ringleader x1 | 8 min | Dead-end hollow in the south-east corner under a fallen giant (dead tree + logs), walled by thicket TH2 with one 3-tile gap on the west at (123,136) where the trail ends. No fire: you see nothing until you are in the gap. Footpads sleep on bedrolls (slow to aggro: 6-tile range); the ringleader paces the gap and is the natural first pull. R4 tracker may wander in behind you. |
| **C7** | Kingsbarrow Vigil (cultists) | kingsbarrow | (118,66) = (7552,4224) cm | 6-7 | 5 | cultist_acolyte x2, cultist_zealot x1, cultist_bloodpriest x1, barrow hierophant / Selwen Marr x1 | 10 min, named slot 20 min, 25% Hierophant Selwen Marr | Atop the mound among broken columns; the Barrow Road ends 8 tiles south. Acolytes kneel at the statue on the south edge (single pulls). The bloodpriest (caster, heals) stands with the hierophant at the altar stone: pull the pair through the half-arch at (114,70), which is a one-wide chokepoint. R5 procession (x2) circles the mound every ~3 min: watch the west face. |
| **C8** | Hask's Hold (brigands) | greyfell_highlands | (28,38) = (1792,2432) cm | 7-8 | 5 | brigand x2, brigand_crossbowman x1, brigand_bruiser x1, brigand chief / Grimald Hask x1 | 12 min, named slot 25 min, 30% Grimald Hask | A shelf under the ridge scarp, reached by the highland path from (34,48). Crossbowman on the boulder at (25,34) covers the path: pull him first from the bend at (30,44) (line of sight is broken by the rock at (31,42)). Brigands and bruiser are linked with the chief; fight on the path below the shelf so the scarp blocks the lookouts' sight. |
| **C9** | Ridge Cairn (lookouts) | greyfell_highlands | (42,24) = (2688,1536) cm | 7-8 | 3 | brigand_lookout x2, brigand_crossbowman x1 | 12 min | Cairn on the ridge top beside the path to the postern. Lookouts have a long aggro range (10 tiles) but no linking: pull them one by one from (36,28). Killing them opens the quiet way to the Postern Tower chamber (A6). |
| **C10** | Steadings Foragers | burnt_steadings | (112,10) = (7168,640) cm | 8-9 | 4 | renegade_footman x2, renegade_archer x1, renegade_sergeant x1 | 12 min | Renegades loading a cart by the cooking fire in a burnt farmyard (picket fence, one gate on the south at (113,14), the cart track from (116,22)). Footmen are linked with the sergeant (lvl 9); the archer stands apart at the well (108,7) and is the single pull. Fight at the fence gate. R7 patrol passes (116,22) every ~2 min: the last pull before the keep. 16.6 m from the Desert landing, so only its fire glow is visible from there. |

**Creature placeholders (marked, empty until Wave 6):**

- **P1** Thornwood wolf den (placeholder) - thornwood, centre (100,100) = (6400,6400) cm, level 5-6, 3 spawn points. Wave 6 creatures; until then leave empty (or 3 poachers).
- **P2** Barrow crypt spiders (placeholder) - kingsbarrow, centre (130,58) = (8320,3712) cm, level 7, 3 spawn points. Wave 6; the crypt hollow is a -1 m dip with rubble and a broken statue.

### Roamers

Roamers walk their path end to end (or loop) at walking speed, pause 5-10 s at each vertex, and are the adds that keep camps honest. Pairs walk together and are linked.

| Id | Name | Level | Spawns | Region | Path (tiles) | Respawn | Note |
|---|---|---|---|---|---|---|---|
| **R1** | a wayside footpad | 4 | 1 | southern_downs | (86,130) -> (92,122) -> (102,114) -> (92,122) | 8 min | walks the Barrow Road between the fork and the wood's edge, past C1 |
| **R2** | a bandit scout | 5 | 1 | southern_downs | (20,138) -> (10,128) -> (6,110) | 8 min | far west track, back and forth; never within 12 m of the palisade |
| **R3** | an outlaw courier | 5-6 | 1 | mistwater_vale | (53,80) -> (56,70) -> (70,66) -> (56,70) | 10 min | south bank only, never crosses the bridge |
| **R4** | a poacher tracker | 6 | 1 | thornwood | (102,114) -> (112,112) -> (114,110) -> (104,128) -> (116,134) | 10 min | walks both game trails, never enters the clearings |
| **R5** | a cultist procession | 7 | 2 | kingsbarrow | (106,80) -> (130,82) -> (134,58) -> (108,56) | 12 min | pair, loop around the mound, chanting |
| **R6** | a brigand runner | 7-8 | 1 | greyfell_highlands | (40,54) -> (30,40) -> (38,26) -> (48,22) | 12 min | highland path, hold to postern |
| **R7** | a renegade patrol | 8-9 | 2 | ashvane_rise | (112,32) -> (116,22) -> (104,22) -> (100,32) -> (90,41) -> (76,40) | 15 min | pair with a lantern (a moving light), crosses the river track above the footbridge and walks to the keep gate |
| **R8** | a highland scavenger | 7 | 1 | greyfell_highlands | (10,52) -> (20,44) -> (28,50) -> (16,56) | 12 min | picks over the scree; the only reason to wander below the tor |

### Thornwood thickets (decision 8)

Thickets are clusters of the VB_ thicket mesh (art gap): waist-high dense bush that blocks line of sight and movement, drawn as dark green bands on the sketch. They turn Thornwood into corridors and rooms: the road and trails are open, every clearing is walled with one gap, and the gap is the pull lane. Fire glow (section 9) still leaks over a thicket, so a lit camp announces itself and a cold camp does not.

- **TH1 Sallow's ring** (ring): centred (120,108), radii 7 x 6 tiles, gap 3 tiles wide facing west at (114,110). the game trail ends at the gap; pull lane runs west down the trail
- **TH2 Deadfall wall** (ring): centred (130,136), radii 7 x 6 tiles, gap 3 tiles wide facing west at (123,136). clipped by the zone edge; the hollow is a dead end
- **TH3 Wolf den ring (P1)** (ring): centred (100,100), radii 6 x 5 tiles, gap 3 tiles wide facing east at (106,100). gap faces the Barrow Road; empty until Wave 6
- **TH4 Wood's west edge** (belt): (93,84) -> (93,116), 2 tiles wide, no gaps. an unbroken wall of thicket between the vale/downs and the wood; the only ways in are the Barrow Road and the trails
- **TH5 Wood's north edge** (belt): (96,85) -> (143,85), 2 tiles wide, gaps: 5 tiles at (110,85) (the Barrow Road passes). separates Thornwood from Kingsbarrow
- **TH6 Trail corridors**: thicket clusters 2 tiles either side of both game trails (trail_thornwood_a and _b), broken every 6-8 tiles by ferns and logs, so each trail is a green corridor and each clearing a reveal; the road itself is never walled

**Pull check with thickets.** C5: the puller stands in TH1's west gap (114,110), sees the fire and the three linked NPCs at it, pulls, and backs 4 tiles down the trail; the bowman on the stump loses line of sight the moment the puller leaves the gap, so he only adds if the group fights inside the ring. The skinner is pulled from the gap after the fire group is down. C6: TH2's gap is the only opening; the ringleader paces it and is pulled from the trail end; the sleepers have a 6-tile aggro range and the gap is 7 tiles from their bedrolls, so they stay asleep until pulled one by one from inside the gap. P1: TH3's gap faces the road, so the future den is pulled from the Barrow Road at (108,102). R4 walks the trails but never enters a ring. TH4 and TH5 have no effect on pulls; they stop players cutting across the wood and seeing the vale from inside it.

### Suggested enemy NPC templates (new rows in `npc-templates.json`)

One template per archetype and level; `type: enemy`, `canAggro: true`, `behaviorType: patrol` for roamers and bailey patrols, `stationary` for camp spawns. Stats scale from the existing "Tough Guy" (level 2, 800 hp, 30-50 damage) along the class curves in classes.json; numbers are for step 1.9 to tune.

| Level | Templates |
|---|---|
| 4 | `bandit_cutpurse`, `bandit_lookout`, `wayside_footpad` (R1) |
| 5 | `bandit_archer`, `bandit_thug`, `bandit_scout` (R2), `outlaw_footpad`, `poacher` |
| 6 | `outlaw_bowman`, `outlaw_ringleader`, `outlaw_courier` (R3), `poacher_bowman`, `poacher_houndmaster`, `poacher_skinner`, `poacher_tracker` (R4), `poacher_captain` (Sallow's placeholder), `cultist_acolyte`, `barrow_hierophant` (Marr's placeholder) |
| 7 | `named_merrik_sallow`, `cultist_zealot`, `cultist_bloodpriest` (healer), `named_selwen_marr` (caster), `cultist_procession` (R5), `brigand`, `brigand_lookout`, `brigand_chief` (Hask's placeholder), `highland_scavenger` (R8) |
| 8 | `brigand_crossbowman`, `brigand_bruiser`, `brigand_runner` (R6), `named_grimald_hask`, `renegade_footman`, `renegade_archer`, `renegade_quartermaster`, `renegade_penitent`, `renegade_patrol` (R7) |
| 9 | `renegade_sergeant`, `renegade_corporal`, `renegade_hallguard`, `renegade_steward` (healer), `renegade_lieutenant` (Vane's placeholder), `renegade_armsmaster`, `renegade_signaller`, `ashvane_zealot`, `named_kell_draven`, `named_mael_corvane` (caster), `named_brann_coll` |
| 10 | `renegade_gaoler`, `ashvane_alchemist` (AoE caster), `named_warden_hesk`, `named_ordric_vane` (rare, best loot table) |

## 7. Ashvane Keep

**Footprint:** outer curtain wall x 50-94, y 4-34 (44 x 30 tiles = 28.2 x 19.2 m) on the +4 m plateau, keep kit: 50 cm curtain modules (1.28 m = 2 tiles each; the 44 x 30 tile outline is a whole number of modules on every side), corner piers, round towers (2.16 m across, 4.8 m tall) at the four corners and flanking the gatehouse, plus a mid-wall tower on the west side at (50,16). **Gatehouse** centred (72,34) on the south wall, opening x 70-74, portcullis raised and walk-through (no closed state yet). **Inner wall** at y 24 from x 51 to 93 with a gate at x 70-74 (a 128 cm door module in a curtain run, or a second gatehouse): the second chokepoint. **Postern** door in the west curtain at y 18-20 (a half-width keep gate centred (50,19)), just south of the postern tower at (50,16), reached by the highland path; it opens into the Postern Tower chamber (51-54, 18-21) and the muster yard beside the barracks. The keep's banner is a black hound on ash grey (our own). Braziers with real lights at both gates and on the Lantern Tower; the great hall's fireplace is the only other big light.

Interiors are single-storey and the roofs hide when a player is inside (per building, local player only). The undercroft is the one deliberate exception: a stone ramp down (decision 6).

**Tower rooms:** the 2.16 m round towers have an interior of about 1.2 m, too small for a room, so each "tower room" is a 3 x 3 tile (1.9 m) stone chamber built from keep-kit modules against the tower on the inside of the wall: the NW Armoury (51-54, 5-8), the NE Lantern Tower chamber (90-93, 5-8) and the Postern Tower chamber (51-54, 18-21). The other towers are solid.


| Area | Tile bounds | Level | Spawns | Contents | Spawn points (template, tile) | Patrol | Pull notes |
|---|---|---|---|---|---|---|---|
| **A1 Gatehouse and outer bailey** | x 51-93, y 25-33 (42 x 8 tiles = 26.9 x 5.1 m) | 8 | 6 | Gatehouse; stables corner at the west end (supply piles, barrels, crates; hay is an art gap); campfire with log seats at (60,29); well at (84,29); smithy corner at the east end (weapon racks; forge is an art gap); banners on the inner wall. | renegade_footman (gate guard) (70,31); renegade_footman (gate guard) (74,31); renegade_footman (bailey patrol, pair) (54,29); renegade_archer (bailey patrol, pair) (55,29); renegade_footman (sentry) (56,27); renegade_archer (sentry) (88,27) | (54,29) -> (90,29) | Gate guards are linked and stand in the gatehouse arch: pull the pair to the glacis at (72,40). The bailey patrol pair walks the full length every ~40 s; wait for it at the east end, then take the west sentry alone. |
| **A2 Inner courtyard (training yard)** | x 62-82, y 12-24 (20 x 12 tiles = 12.8 x 7.7 m) | 8-9 | 5 | Inner gate; weapon racks along the south wall; training posts (art gap: use stumps); supply pile at (80,22); a second campfire; doors to barracks (62,14), great hall (72,12), chapel (82,18). | renegade_footman (sparring) (66,16); renegade_footman (sparring) (68,18); renegade_footman (sparring) (78,16); Sergeant-at-Arms Kell Draven (9, always up) (72,15); renegade_quartermaster (8) (80,22) | - | The inner gate (70-74,24) is a one-and-a-half-tile chokepoint: pull sparring pairs through it into the bailey. Kell Draven (9) links to all three sparring footmen: kill them first from the gate, then Draven alone. |
| **A3 Barracks (west range)** | x 51-62, y 5-17 (11 x 12 tiles = 7.0 x 7.7 m) | 8-9 | 6 | 8 beds along the west wall, chests at their feet, a long table with candles, weapon rack, fireplace on the north wall, kegs. Door from the courtyard at (62,14). The muster yard (51-62,17-24) outside it is open ground with the postern tower. | renegade_footman (off duty, sitting) (54,8); renegade_footman (off duty, sleeping) (53,12); renegade_footman (off duty, sleeping) (53,15); renegade_footman (at table) (58,10); renegade_archer (at table) (59,12); renegade_corporal (9) (56,6) | - | Sleeping soldiers have a 3-tile aggro range and are not linked: single pulls to the door. The corporal and the two at the table are linked (pull of three): do it from the muster yard with the door as the chokepoint. |
| **A4 Great hall (north range)** | x 62-90, y 5-12 (28 x 7 tiles = 17.9 x 4.5 m) | 9-10 | 5 | Two long tables with benches, fireplace on the north wall at (80,5) (east of the ramp slot), dais at the east end (86,8) with the Castellan's chair and the Ashvane banner (a black hound on ash-grey), a ramp slot along the north wall (x 66-76, y 5-7) descending west into the undercroft, with a 90 cm parapet on its south edge. Door from the courtyard at (72,12). | renegade_hallguard (9) (72,10); renegade_hallguard (9) (78,10); renegade_steward (9, caster) (68,8); renegade_archer (9) (84,6); a renegade lieutenant (9) / Castellan Ordric Vane (10, 20%, 25-30 min) (86,8) | - | The hall door is the chokepoint. Hall guards are linked to each other; the steward heals. Pull the guards through the door into the courtyard, then the steward, then the archer, then the lieutenant/Castellan alone on the dais. Vane hits hard and calls no help: a level 8-9 group of five can take him once the hall is clear. |
| **A5 Chapel (east range, temple kit)** | x 82-93, y 12-24 (11 x 12 tiles = 7.0 x 7.7 m) | 9 | 4 | Two rows of limestone columns, altar on the north wall at (87,14) with steps, candles, a broken statue (the old lord's saint, defaced), books. Door from the courtyard at (82,18). | Chaplain Mael Corvane (9, named, always up, 15 min) (87,15); ashvane_zealot (9) (85,20); ashvane_zealot (9) (89,20); renegade_penitent (8, kneeling) (87,22) | - | The penitent by the door is a single pull. The zealots are linked with the Chaplain (caster, roots and nukes): pull all three through the door and interrupt the Chaplain in the doorway, where the columns break his line of sight to the group. |
| **A6 Tower rooms (ground floor)** | three 3x3 chambers, see above | 9 | 3 | The 2.16 m round towers are too small for rooms, so each tower room is a 3x3-tile stone chamber (keep-kit modules) built against the tower on the inside: NW Armoury (weapon racks, chests) at (51-54,5-8); NE Lantern Tower (stair up is fake, brazier on top is the zone landmark) at (90-93,5-8); Postern Tower chamber at (51-54,18-21) with the postern door. | renegade_armsmaster (9) (52,6); renegade_signaller (9) (92,6); Gatewarden Brann Coll (9, always up) (52,19) | - | Each is a solo pull from its doorway. The Gatewarden is the first fight if you come in by the postern: his chamber opens both to the highland path and to the muster yard, so the barracks (A3) hears the fight if it runs long (link range 6 tiles). |
| **A7 Undercroft (cellar, via ramp)** | x 62-76, y 5-12 (14 x 7 tiles = 9.0 x 4.5 m) | 10 | 4 | Under the west half of the great hall, floor 3 m below the hall, reached by the ramp (top (76,6), foot (66,6), see the ramp spec). Kegs, crates, chests, a gaol cell with iron bars (art gap) at the west end, a strongroom with the Castellan's chest at the south-east, candles, kegs stacked under the high end of the ramp, and a bricked-up arch on the west wall (Phase-2 flavour: the old lords dug toward the ridge). Dark: torch light only. | Warden Hesk (10, named, always up, 20 min) (64,9); renegade_gaoler (10) (68,8); renegade_gaoler (10) (70,10); ashvane_alchemist (10, caster) (74,10) | - | The ramp is a 128 cm wide chokepoint and the only way out. Gaolers are linked and stand at its foot; the alchemist throws oil (AoE). Pull the gaolers up the ramp into the hall; the Warden and alchemist stand at the strongroom and must be fought below. The first 'dungeon' fight of the game. |

**Keep total: 33 spawn points** (A1 6, A2 5, A3 6, A4 5, A5 4, A6 3, A7 4). Level 8 at the gate, 8-9 in the yard and barracks, 9 in the chapel and towers, 9-10 in the hall, 10 in the cellar. Outside the walls, R7 (a renegade patrol pair, 8-9) walks the glacis and the Steadings track, so the approach is never quiet.

**Named officer and rare spawn.** The dais spawn point in the great hall (86,8) normally spawns *a renegade lieutenant* (9). On each respawn (25-30 min) there is a 20% chance it spawns **Castellan Ordric Vane** (10) instead, with his own loot table (a keep-themed rare item: the Ashvane signet, a hound-crested shield, a castellan's sword). He does not call for help and does not leave the hall. The Chaplain, the Gatewarden, Kell Draven and Warden Hesk are always-up named NPCs with better-than-normal drops; only Vane is rare.

**Patrol routes inside the keep:** the bailey pair (54,29) -> (90,29) and back (about 40 s each way); everything else is stationary. When B-16 gives us a nav mesh, a second route can go bailey -> inner gate -> courtyard -> bailey.

**Undercroft ramp and Landscape hole (decision 6).**

| Item | Spec |
|---|---|
| Ramp top | tile (76,6) at hall-floor level, east end of the slot |
| Ramp foot | tile (66,6) at -300 cm (cellar floor) |
| Run / drop / slope | 640 cm / 300 cm / **25.1 deg** (under the 35 deg limit; CharacterMovement default walkable angle is 44.8 deg) |
| Width | 2 tiles = 128 cm (y 5-7), a door-module width; the cellar's only way in or out |
| Slot in the hall floor | x 66-76, y 5-7 (10 x 2 tiles = 6.4 x 1.3 m) |
| Landscape hole (visibility mask) | x 61-77, y 5-13 (16 x 8 tiles = 10.2 x 5.1 m) |
| Undercroft room | x 62-76, y 5-12 (14 x 7 tiles = 9.0 x 4.5 m), floor at -300 cm, walls 360 cm (two stacked curtain modules) |

- Ramp: a straight stone ramp along the hall's north wall, top at tile (76,6) at hall-floor level, foot at (66,6) at -300 cm. Run 10 tiles = 640 cm, drop 300 cm: slope 25.1 deg (tan = 0.47), well under the 35 deg limit and under CharacterMovement's default walkable floor angle (44.8 deg). Width 2 tiles = 128 cm (y 5-7), the same as a door module; it is the undercroft's only way in or out.
- Slot in the hall floor: x 66-76, y 5-7 (640 x 128 cm). Its south edge carries a 90 cm stone parapet (a short variant of the 50 cm curtain module, minor art gap) so nobody walks off it; its north edge is the hall's north wall.
- Landscape hole: x 61-77, y 5-13 (16 x 8 tiles = 1024 x 512 cm), painted with the visibility mask. That is the room (62-76, 5-12) plus one tile of margin east, west and south; the north edge coincides with the hall's north wall line, where the undercroft's north wall stacks directly under the outer curtain wall.
- Room: floor mesh (stone floor tiles) at -300 cm, walls from keep curtain modules stacked two high (360 cm) standing on the undercroft floor in the one-tile margin, so no Landscape edge is ever visible. The great hall's floor mesh is the undercroft's ceiling: it overlaps the hole by at least one tile on every side and hides for the local player while they are below (the same roof-hide rule as a building). The hall's stone walls sit on solid ground outside the hole.
- Under the ramp's high end (x 70-76, y 5-7) the clearance is under 122 cm, so that strip is filled with kegs and crates and is not walkable. Everything else in the room has 300 cm headroom.
- The plateau is +4 m, so the cellar floor is still +1 m above the river; no water table, no drainage hole needed. Nav mesh (B-16): the ramp is a normal walkable slope; NPCs chase up and down it.

## 8. The hidden Greyfell cleft

- **Where:** the cleft mouth is at tile (6,27) (384,1728 cm), the portal trigger just inside at (6,25); it faces south. A narrow cleft at the foot of Greyfell Tor's southern cliff face, at the far west end where the cliff meets the zone edge. It opens south into the scree pocket.
- **Screen (props, tile):** boulder (large, VB_BoulderLarge_A, yaw 15) (4,29); boulder (large, VB_BoulderLarge_A, yaw -15) (8,29); dead tree (2,34); dead tree (12,33); fallen log (5,33); fallen log (10,34); bush (4,31); bush (8,32); fern clumps (6,32); stump (11,29); rock (small) x6 scree (8,36).
- **Approach:** From the scree pocket: climb north-west from the highland path's bend at (34,48) across open scree with no path, past the scavenger's ground, to the cliff foot at (12,32); walk WEST along the cliff foot through the 140 cm passage between the cliff face (front at y 26) and boulder B at (8,29.5); the cleft is then in front of you at (6,27). The two boulders (each 3.7 tiles wide, yawed 15 and -15 degrees so their ends overlap by at least 20 cm) stand shoulder to shoulder 3 tiles south of the mouth, so from the south there is no line of sight into it at the 90 cm trace height; the logs, bushes and dead trees south of them hide the boulders' shape. The trigger is inside the passage at (6,25.2), 60 cm up.
- **Why it is hard to find:** No path or signpost leads there; it is 17 m from the nearest path point (30,40) and 15.5 m from Hask's Hold, both beyond the 10-16 m fog radius; the tor cliff and boulders block line of sight; a dense mist pocket sits over the scree; nothing glows; and the only reason to wander there is R8, a single scavenger. The hint is Wyn Cotter's hail line at the outpost.
- **Not on any player map.** The dashboard capture (step 1.10) should be taken with the boulders and dead trees in place, so even the top-down thumbnail does not show a gap; the SVG marks it only as "unmarked cleft (section 8)".
- **Phase 2:** The trigger just inside the cleft is a hidden-portal variant (rune-stone marker off) to cave_dungeon entry 'greyfell_from_eldmoor' (entry ids are prefixed with the zone they stand in, see section 4). Greyfell's tier-1 entrance tunnels begin here; the optional lair exit returns players to (14,46) below the scree. Testers use the admin teleport.

## 9. Sightlines and fog

Design assumption: clear to about 10 m, fully fogged at 16 m (the B-06 plan's starting values of 25/40 m are wider than the engineering brief; the layout works with either, but the hidden entrance distances above are computed for 16 m).

**Mist pockets (local fog volumes, denser than the zone fog):**


- **M1** River bottom (west): ellipse centred (30,70), radii 34 x 9 tiles, medium - both banks, Mistwater Toll camp sits in it
- **M2** River bottom (bridge to footbridge): ellipse centred (80,56), radii 26 x 8 tiles, medium
- **M3** Scree pocket under the tor: ellipse centred (13,40), radii 13 x 12 tiles, dense - hides the cleft further
- **M4** Thornwood hollow: ellipse centred (100,100), radii 8 x 7 tiles, dense - wolf placeholder den
- **M5** Barrow crypt hollow: ellipse centred (130,58), radii 6 x 5 tiles, dense
- **M6** Deadfall: ellipse centred (130,136), radii 7 x 6 tiles, medium

**Views deliberately blocked (vision blockers, VB_ collision):**

- Greyfell Tor's south and east cliffs (VB cliff meshes): the cleft cannot be seen from the ridge path or Hask's Hold.
- The ridge scarp above Hask's Hold: the lookouts at the cairn cannot see the shelf.
- The outpost palisade (VB): the vale and the west track are hidden from inside.
- The keep's curtain walls and inner wall (VB): the courtyard is hidden from the bailey and the glacis.
- Thornwood thickets (VB bush clusters, art gap): each camp is a clearing you only see from its trail end.
- Kingsbarrow's broken walls (VB): the altar is hidden from the road end.

**Landmarks that should read through the fog:**

- Lantern Tower brazier (Ashvane Keep NE tower) at (94,4) - warm glow readable at ~1.5x fog range (engineering ask)
- Greyfell Tor silhouette at (13,13) - +7 m crag, unwalkable; visible as a dark mass from the vale
- Outpost lamp posts and campfire at (50,112) - glow from the road
- Kingsbarrow headless king statue at (116,72) - on the mound's south edge above the road end
- Camp fires at every camp - glow visible before the NPCs are

**Firelight through the fog (decision 11).** Fires, braziers, cooking fires, torches and lamp posts show through the fog as a soft glow to about **24 m** (1.5x the fully-fogged radius) even though the meshes under them are fogged out, like a lamp seen through real mist. Only light-bearing props do this; NPCs, nameplates and loot stay hidden beyond the fog. The zone is lit deliberately: **beacons** are camps and places you are meant to find by their glow, **cold camps** are the ones meant to surprise you, and nothing at all glows near the hidden cleft.

*Beacons (intended):*

- **F_C1** C1 Wood's-Edge Bandits at (98,128): campfire. the first beacon: visible from the portal 21 tiles (13.5 m) away
- **F_C3** C3 Mistwater Toll at (48,72): campfire. the toll fire on the south bank, seen from the road bend
- **F_C4** C4 Footbridge Poachers at (98,54): cooking fire. seen from the barrow track and from the river track
- **F_C5** C5 Sallow's Camp at (120,108): campfire (large). the glow leaks over thicket TH1; the thicket hides the people, not the light
- **F_C7** C7 Kingsbarrow Vigil at (118,66): altar brazier + candles. cold blue-white cult light, not orange: reads as 'something wrong' from the road end
- **F_C10** C10 Steadings Foragers at (112,10): cooking fire. the warning glow seen from the Desert landing (16.6 m)
- **F_KEEP_GATE** Ashvane Keep gatehouse at (72,35): two braziers. seen from the bridge's north end up the glacis
- **F_KEEP_LANTERN** Lantern Tower (NE tower top) at (94,4): brazier. the zone landmark; readable from the Steadings, the footbridge and the river track
- **F_R7** R7 renegade patrol: hand lantern (moving). a moving glow on the glacis and the river track: the patrol announces itself
- **F_OUTPOST** Harrow's Rest at (50,112): 5 lamp posts, 2 gate braziers, 1 campfire. the safe glow: seen from the portal road and from the bridge

*Lights that are not beacons (inside walls or interiors, blocked by VB walls):* A1 outer bailey (campfire); A2 courtyard (campfire); A4 great hall (fireplace).

*Deliberately cold (no fire, no light):*

- **C2 Shepherd's Hollow**: cold camp: bandits hiding in a ruined hut; the archer on the wall is the surprise
- **C6 Deadfall Hideout**: cold camp: sleeping outlaws in a dark hollow behind thicket TH2
- **C8 Hask's Hold**: cold camp: brigands do not light fires under the ridge; only R6 and the path lead there
- **C9 Ridge Cairn**: cold camp: lookouts show no light
- **R8 highland scavenger**: carries no lantern
- **The whole of the Greyfell Highlands west of x 34 and the scree pocket**: no fire, torch, lamp or glowing prop of any kind: the nearest visible light to the cleft is the keep gate brazier at about 42 m (the bailey campfire at 35 m is inside the curtain wall), far beyond the 24 m glow range, and the tor's cliff and the ridge stand between

*Hidden cleft check:* the nearest light of any kind to the cleft at (6,27) is the bailey campfire at about 35 m, inside the curtain wall; the nearest beacon is the Mistwater Toll fire at about 39 m, across the river and below the ridge, then the keep gate brazier at 42 m. All are far beyond the 24 m glow range, and the tor and the ridge stand between. Nothing in the Highlands west of x 34 carries a light, and the scavenger R8 has no lantern. Scripted check: `eldgen.py` prints the nearest-light and nearest-beacon distances on every run.

**Silhouettes at ten screens wide.** With 10 m per screen, players never see a region at once; they read it from its ground material, its props and the shape of its edges. So each region has one unmistakable floor and one prop family: sheep-cropped grass and single trees (Downs), reeds and bank strips (Vale), trees packed to 1 per 3 tiles with logs and ferns (Thornwood), broken walls and columns on a mound (Kingsbarrow), bare rock with dead trees (Highlands), stone walls and banners (Keep), scorched timber and fences (Steadings).

## 10. Spawn summary


| Region | Camps (spawns) | Roamers (spawns) | Keep areas (spawns) | Live spawn points | Level | Placeholders |
|---|---|---|---|---|---|---|
| Southern Downs | C1 (3), C2 (4) | R1 (1), R2 (1) | - | **9** | 4-5 | - |
| Mistwater Vale (river valley) | C3 (4), C4 (4) | R3 (1) | - | **9** | 5-6 | - |
| Thornwood | C5 (5), C6 (4) | R4 (1) | - | **10** | 5-7 | P1 (3) |
| Kingsbarrow (old barrow ruins) | C7 (5) | R5 (2) | - | **7** | 6-7 | P2 (3) |
| Greyfell Highlands | C8 (5), C9 (3) | R6 (1), R8 (1) | - | **10** | 7-8 | - |
| Ashvane Keep and its approach | - | R7 (2) | A1 (6), A2 (5), A3 (6), A4 (5), A5 (4), A6 (3), A7 (4) | **35** | 8-10 | - |
| Burnt Steadings | C10 (4) | - | - | **4** | 8-9 | - |
| **Total** | **41** | **10** | **33** | **84** | 4-10 | 6 |

**By level band (outside the keep):** 4-5: 8, 5-6: 14, 6-7: 11, 7-8: 12, 8-9: 6 (51). **Inside the keep:** 8: 6, 8-9: 11, 9: 7, 9-10: 5, 10: 4 (33). Total 84.

84 live spawn points plus 6 creature placeholders = 90, inside the plan's 80-100. Camps: 41 spawns in 10 camps (3-5 each). Roamers: 10 spawns on 8 paths. Keep: 33.

## 11. A level-4 group's first hour

1. **0:00** Three to five level-4 players step through the Grasslands portal and arrive at (80,132) on Warden's Road with the portal stone behind them and a signpost ahead. They can see about 10 m: road, grass, a bandit fire glowing faintly to the north-east. R1 (a wayside footpad, 4) walks past on the Barrow Road: first kill.
2. **0:03** They follow the road 20 m to Harrow's Rest, hail everyone (ten lines of lore, one of them Wyn Cotter's hint), and note the safe knoll. Nothing aggroes within 12 m of the stakes.
3. **0:06** Back down the road to **C1 Wood's-Edge Bandits** (4-5, 3 spawns): the lookout pulls alone to the road, then the two cutpurses. Six-minute respawn means they can camp it for a couple of cycles while people learn to pull.
4. **0:20** West along the track to **C2 Shepherd's Hollow** (4-5, 4 spawns) with an archer on a wall and a level-5 thug: the first fight that needs a tank and a healer. R2 wanders in as the add. Most of the group hits level 5 here.
5. **0:35** North through the gate and down into the vale. The mist thickens; the stone bridge and the outlaws' toll fire at **C3 Mistwater Toll** (5-6, 4 spawns) appear together. Single footpads from the road bend, then the ringleader and bowman as a pair; R3 the courier is the add. This is the first "real" group camp and probably where the hour ends.
6. **Next session:** across the bridge is level 7+ (R6 on the highland path, Hask's Hold); they should feel it and turn back. The intended order is C4 and Thornwood (5-7) via the Barrow Road, Kingsbarrow (6-7), the Highlands (7-8), the Steadings by the footbridge (8-9), then the keep gate at 8 and the undercroft at 10.

Rough pacing by level: 4-5 in the Downs (C1, C2, R1, R2); 5-6 at the crossings and in the near wood (C3, C4, C6, R3, P1); 6-7 in Thornwood's heart and the barrow (C5, C7, R4, R5, P2); 7-8 in the Highlands (C8, C9, R6, R8); 8-9 in the Steadings and the bailey (C10, R7, A1, A2, A3); 9-10 inside the keep (A4-A7).

## 12. Art gaps (must be modelled; not in any kit)

1. **Cliff and crag meshes** for Greyfell Tor's faces (7 m), the ridge's north cliff and south scarp (1.5-4 m), and the plateau's north edge; plus 2-3 large rock outcrops / boulders (1.5-2.5 m) for the cleft screen, Hask's crossbowman's rock and the shelf. All `VB_` (block sight).
2. **Cave mouth / cleft** piece: a narrow dark opening in a cliff-foot module, 1.5 m wide, 2.2 m tall, with an interior black shroud so the trigger is not visible.
3. **Palisade wall modules** (64 cm sections, 2.4 m tall, sharpened logs) with corner posts and a **gate** (2.56 m opening, open state only), `VB_`.
4. **Forge and anvil** (outpost smithy and keep smithy corner); **hay bale / straw pile** (stables corner); **training post / pell** (courtyard; stumps as fallback); **iron-bar cell front** (undercroft; the gatehouse portcullis mesh could be reused); **stone ramp** piece for the undercroft (640 x 128 cm, 25 deg); **scorched timber wall / beam variants** for the Burnt Steadings (or use the ruins set and blackened decals); a **cart** for the foragers' yard; a **VB bush thicket** cluster for Thornwood's clearings.
5. **Short curtain-wall variant (90 cm parapet)** for the ramp slot's south edge; a **brazier** prop with a real light (gate posts, keep gate, Lantern Tower) if the lamp post is the only light-bearing prop in the kits; a **bind stone** variant of the rune-stone portal marker (no trigger, different glow).
6. **Mist volume** material (engineering rather than art) and the fog-glow bleed for fires and braziers.
7. Optional: a 4 m "keep tower" variant if Kevin prefers real tower rooms to the 3 x 3 chambers; a wall-walk piece for patrols on the walls (not needed for the design above).

## 13. Deviations from the B-06 plan, and why

- **Outpost safe radius 12 m instead of 40 m.** The zone is 91 m across; 40 m from a 18 x 15 m palisade would sterilise most of the south half. 12 m keeps the knoll quiet and still puts the first camps 14-17 m out.
- **Portal-to-keep walk of 55-80 s, not 3-4 minutes.** That is what 87 m of road at 110-160 cm/s gives. The plan's time was written before the extents were measured; 3-4 minutes would need a road three times as long, either a zone about 3x wider on each side (roughly 45x Grasslands' area, not 5x) or a road that winds far more than the terrain justifies. Kevin accepted the shorter walk (decision 1); the fog, not distance, is what makes the zone feel big.
- **Tower rooms as 3 x 3 chambers** against the towers, because the kit's 2.16 m towers cannot hold a room.
- **A second (east) approach to the keep** by the footbridge and the Steadings, and a **postern back door** from the Highlands, so the keep has three ways in and the zone forms a loop instead of a single road.
- **Burnt Steadings** is an added region (the plan's seven rows were six regions plus the outpost) so the north-east corner, north of the river, is not empty; it carries the 8-9 band between the Highlands and the keep.
- **Fog designed for 10-16 m** rather than the plan's 25/40 m starting values; confirmed by decision 11.
- **The Desert portal lands in Burnt Steadings** (decision 4), not on the Downs as the plan's optional link implied, so the zone has a back door at its hard end.
- **Undercroft by ramp** (decision 6) instead of the plan's stairs.

All of the above were approved on 2026-09-24 (see Decisions at the top).

## 14. Open questions

None. Every question from the first draft was answered on 2026-09-24 (see Decisions at the top). The next steps are 1.2 (art gaps, section 12) and 1.3 (zone atmosphere), then the blockout from `eldmoor_layout.json`.

## 15. Files

- `Docs/Zones/Eldmoor/EldmoorDesign.md` - this document.
- `Docs/Zones/Eldmoor/eldmoor_layout.svg` and `eldmoor_layout.png` - the sketch, 1 tile = 6 px, tile numbers on the frame.
- `Docs/Zones/Eldmoor/eldmoor_layout.json` - regions, roads, river, portals, starts, outpost (buildings, props, NPC roster), camps, placeholders, roamers, thickets, fires and cold camps, guard posts, bind stone, keep areas with every spawn point and the undercroft ramp spec, both portals (Desert side included), hidden entrance, mist pockets and landmarks, in zone-local cm (tile values kept alongside), for a blockout script.
