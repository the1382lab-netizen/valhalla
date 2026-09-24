<!-- Copy of the B-06 section of Valhalla2/PLAN.md. -->

## B-06 Phase 1 prerequisites — zone atmosphere, friendly NPCs, 160-tile scaffold (2026-09-24)

Technical groundwork for Eldmoor Grasslands (`grasslands_v2`) and, later,
Greyfell Cave (`cave_dungeon`). No level work: the Eldmoor layout is separate.

- [x] **Zone atmosphere as data.** Optional `atmosphere` object per zone in
      `shared/data/zones.json` (`ZoneAtmosphere` in `shared/src/maps.ts`; its rules in
      `validateZoneAtmosphere` in `shared/src/validation.ts`, used by B-13's
      `validateGameData`, the loader and the Zones page; `FValhallaAtmosphereProfile` in `ValhallaTypes.h`,
      parsed in `ValhallaDataSubsystem.cpp`). Fields: `visionClearRadiusCm`,
      `visionFadeWidthCm`, `fogColor` (#rrggbb), `heightFogDensity`,
      `heightFogStartCm`, `sunIntensityScale`, `skyLightIntensityScale`,
      `gradeTint` (#rrggbb), `cameraMaxArmCm`, `netRelevancyRadiusCm`,
      `firelightGlow`, `firelightRangeCm`, `notes`.
      **A missing object or field means today's look exactly.** Grasslands and
      Desert have no profile and do not change.
- [x] **Web editor:** new **World → Zones** page (`editor/src/components/editors/zones/ZoneEditor.tsx`):
      name, default spawn, and every atmosphere field (empty = default). Saves
      `zones.json`; the Validation page checks the atmosphere too. Hot reload is
      the existing `valhalla.DataHotReload` path: the server reloads, bumps the
      data version, clients reload, and the client re-reads the profile every
      frame, so a saved change blends in within about a second.
- [x] **Client (`AValhallaZoneAtmosphere`, new).** Spawned by
      `AValhallaFogRenderer` (client-only, never on a dedicated server). It
      records L_World's own values on the first tick (Sun, Fill, SkyLight,
      ExponentialHeightFog, the global post-process gain), follows the local
      pawn's zone (the same local zone test the fog of war uses), and blends
      over 1 s (smoothstep) to the zone's profile: height fog density/start/
      colour, sun intensity, sky light + fill intensity, a colour-gain tint on
      the player camera, and PP_Fog's new vision-fog parameters. Leaving for a
      zone without a profile blends back to the recorded values and switches
      the camera override off.
- [x] **Vision fog** is a new term in **PP_Fog** (`build_fog.py`): the pixel's
      ground distance from `VisionCentre` (the player), clear inside
      `VisionClearRadius`, smoothstep over `VisionFadeWidth`, then
      `VisionFogColor` (divided by eye adaptation so the designer's colour is
      what shows). `VisionFogStrength` defaults to 0, so the regenerated
      material is identical to today's until a zone asks for fog. The LOS fog of
      war stays underneath; walls still block sight.
- [x] **Firelight through the fog.** Beacon lights — actors tagged
      `ValhallaFireLight` (fire_lights.py: campfires, cooking fires, braziers,
      fireplaces), `ValhallaLampLight` (lamp_lights.py: lamp posts) or
      `ValhallaBeacon` (anything placed by hand, e.g. a torch) — are drawn by
      `AValhallaFogRenderer` into a third world-space mask, `GlowMask`: one
      additive soft disc per light, its attenuation radius wide, in its colour,
      weighted by sqrt(candelas / 70) (candles and the altar fall under the 0.3
      cut). Rebuilt on a zone change and every 2 s, only while a zone has the
      firelight on. PP_Fog thins the vision fog over the glow (up to 70 %) and
      adds the glow's colour on top, both scaled by the fog's alpha and faded
      out beyond `FirelightRange` — so a camp reads as a warm glow through the
      mist from a distance, and nothing changes inside the clear radius. Per
      zone: `firelightGlow` (1 default, 0 off) and `firelightRangeCm` (default
      1.5 x the fully fogged distance, 24 m in Eldmoor), both on the Zones page.
      The actor carrying a light (an NPC) is still hidden; only tagged level
      lights glow.
- [x] **Camera:** a zone's `cameraMaxArmCm` clamps the boom (500–2600). No
      clamp in zones without one, so `valhalla.DebugCameraDistance` still works there.
- [x] **Hide beyond the fog.** Client: NPCs, other players and loot bags
      further than clear + fade are hidden (checked every 0.1 s; loot labels and
      the debug-HUD plates skip hidden actors). Server:
      `UValhallaVisibilitySubsystem::GetVisionRangeFor` caps a player's vision
      range with the zone's `netRelevancyRadiusCm` (never raises it), so those
      actors stop replicating. The LOS polygon uses the same capped range. This
      is the per-zone relevancy radius; `NetCullDistanceSquared` stays 1e12 on
      purpose (a 3D radius from the view point, applied before
      `IsNetRelevantFor`, which would also drop party members). Targeting:
      `AValhallaPlayerState::SetTargetActor` refuses a target beyond the fog.
      NPC aggro is unaffected (NPCs have no player state and aggro does not use LOS).
- [x] **Friendly NPCs** already existed: template `type: "npc"` →
      `AValhallaNPC::bFriendly` (replicated) → `AreHostile` false, no aggro by
      default. No separate hostility field was added (it would duplicate
      `type`). Tightened: a friendly NPC never aggroes even with `canAggro:
      true` (`CanEverAggro`), takes no damage (`ApplyDamageFromAttacker`), and
      cannot be the target of a SingleEnemy skill (`ValidateCast` and the
      fire-time check now use `AreHostile`). New optional `role` string on
      templates (TS, C++ `FValhallaNPCTemplate::Role`, NPC editor field shown
      for friendly NPCs); nothing reads it yet. The NPC editor's Type now reads
      "Enemy (hostile)" / "NPC (friendly)". Neutral NPCs are not supported.
- [x] **Nameplates (UMG HUD):** friendly NPCs get a green name and bar; NPCs
      and players hidden by the vision fog get no plate; the target frame no
      longer shows a friendly NPC's name in enemy red.
- [x] **scaffold_zone:** `MAX_TILES` 128 → 160 (Eldmoor is 143 tiles, 9152 cm;
      at 160 tiles the fog masks are 10 cm a texel).

**Starting values (in zones.json, marked in `notes`, not playtested).** From the
camera: boom 1500 cm at −45° pitch, 35° horizontal FOV, 16:9. The ground seen
runs from 3.2 m behind the player to 4.6 m ahead, 9.5 m across, so the far
screen corners are about **7.4 m** from the player. Corner distance scales with
the boom (about 0.49 × boom length at −45°), so the 2600 maximum reaches
about 12.8 m; pitching the camera up to −15° would show 70+ m, which the vision
fog (sky pixels count as far) and the height fog cover.

| | clear → fully fogged | camera max | relevancy | other |
|---|---|---|---|---|
| Eldmoor (`grasslands_v2`) | 10 m → 16 m | 2400 (corners ≈ 11.9 m, inside the fade) | 1700 | fog #8c978a, height fog 0.03 from 1600, sun ×0.6, sky ×0.85, tint #eef3ee |
| Greyfell (`cave_dungeon`) | 4 m → 7 m | 1500 (default view, corners ≈ 7.4 m) | 800 | fog #0b0f18, height fog 0.05 from 1000, sun ×0, sky ×0, tint #c8d2e6 |

Relevancy is capped by the class vision range as well (1200 most classes,
1350 rogue, 1800 ranger), so in Eldmoor most classes stop receiving actors at
12 m (about a quarter fogged) and a ranger at 17 m.

**On Windows (Kevin):**

1. Build the editor target (`Valhalla2Editor`, Development Editor) — new files
   `ValhallaZoneAtmosphere.h/.cpp`, so a full build, not Live Coding.
2. Regenerate PP_Fog: `py "import valhalla_tools.build_fog as f; f.build_fog()"`
   (adds the vision-fog parameters; with no profile it looks the same as before).
3. Tests: `Valhalla.` automation tests should be unchanged.
4. Manual checks (PIE, listen server + one client):
   - Enter Grasslands: no visual change; `valhalla.DebugCameraDistance 5000` still works.
   - Web editor → Zones → Grasslands → Custom atmosphere, e.g. clear 400, fade
     300, fog #202830, sun 0.3, sky 0.5, tint #c0d0ff, camera 1500,
     relevancy 800; Save. Within about a second: mist closes in, light dims,
     the camera pulls in. Untick Custom atmosphere, Save: it blends back.
   - With that profile: an NPC further than 7 m is not drawn and cannot be
     clicked, its nameplate goes with it, a far bag's loot label is gone, and
     `valhalla.DebugListActors` on the client lists nothing beyond 8 m.
   - A friendly test NPC (`type: npc`, e.g. `merchant_bjorn`, optionally with
     `canAggro` ticked): walking up does not aggro it; auto-attack and a
     SingleEnemy spell on it say "Invalid target"; an AoE does not hurt it.
   - Firelight: with that profile, walk 10-15 m from Bjorn's market lamp
     posts or a campfire: a warm glow shows through the mist where the fire
     is; set Firelight glow to 0 and Save: it goes; 2: brighter.
   - Leave the Grasslands profile off again before committing `zones.json`.

### B-06 follow-up — vision is a scale on the class range, not a cap (2026-09-24)

Kevin's call: the zone cap (`netRelevancyRadiusCm`, a min with the class
range) took the ranger's advantage away in fog zones. Now one effective range
per player drives everything:

- **Data:** `visionScale` (turns the vision fog on; absent = no fog, class
  range unchanged), `visionClearFraction` (default 0.625), `relevancyMarginCm`
  (default 100). `visionClearRadiusCm`, `visionFadeWidthCm` and
  `netRelevancyRadiusCm` are retired (the validator warns, the game logs and
  ignores them).
- **Formula** (`ValhallaAtmosphere::ResolveVision` in C++,
  `resolveZoneVision` in `shared/src/maps.ts`): effective = class
  `visionRange` x `visionScale` (fully fogged, hide and target limit);
  clear = effective x `visionClearFraction`; relevancy = effective +
  `relevancyMarginCm` (server line of sight and net relevancy, and the fog
  renderer's lit polygon). Firelight range defaults to 1.5 x effective.
- **Hooks:** `ValhallaAtmosphere::GetBaseVisionRange(PlayerState)` is the one
  place buff and race vision modifiers (B-20) multiply in;
  `ValhallaAtmosphere::ResolveCameraMaxArm(Profile, PlayerState)` is where a
  per-class camera modifier would go (the camera limit stays per zone).
- **Values:** Eldmoor 1.3333 / 0.625 / 100 — a 12 m class is clear to 10 m,
  fully fogged at 16 m, sent to 17 m (as before); a rogue 11.3 / 18 / 19 m; a
  ranger 15 / 24 / 25 m. Greyfell 0.5833 / 0.571 / 100 — 12 m class 4 / 7 /
  8 m; ranger 6 / 10.5 / 11.5 m. Grasslands and the Desert have no profile and
  are unchanged.
- **Web editor:** the Zones page has the three fields and a per-class preview
  table (clear / fully fogged / sent to) computed with `resolveZoneVision`.
