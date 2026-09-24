<!--
  B-06 Phase 1 technical prerequisites. Written as a PLAN.md section, ready to
  append to Valhalla2/PLAN.md above "## Backlog". It is a separate file only
  because PLAN.md was being edited by the B-21 session at the time (2026-09-24).
-->

## B-06 Phase 1 prerequisites — zone atmosphere, friendly NPCs, 160-tile scaffold (2026-09-24)

Technical groundwork for Eldmoor Grasslands (`grasslands_v2`) and, later,
Greyfell Cave (`cave_dungeon`). No level work: the Eldmoor layout is separate.

- [x] **Zone atmosphere as data.** Optional `atmosphere` object per zone in
      `shared/data/zones.json` (`ZoneAtmosphere` in `shared/src/maps.ts`, with
      `validateZoneAtmosphere`; `FValhallaZoneAtmosphere` in `ValhallaTypes.h`,
      parsed in `ValhallaDataSubsystem.cpp`). Fields: `visionClearRadiusCm`,
      `visionFadeWidthCm`, `fogColor` (#rrggbb), `heightFogDensity`,
      `heightFogStartCm`, `sunIntensityScale`, `skyLightIntensityScale`,
      `gradeTint` (#rrggbb), `cameraMaxArmCm`, `netRelevancyRadiusCm`, `notes`.
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
- [ ] **Friendly nameplate colour in the UMG HUD — not done.** The plates are
      drawn by `UValhallaGameHUDWidget::TickWorldLayer`, which the B-21 session
      was editing. The change: skip `It->IsHidden()` actors, and pass
      `!It->bFriendly` instead of `true` as `bHostile` for NPC plates (and use
      `AreHostile` instead of `IsNpcTarget` for the target-frame name colour).
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
     clicked, its nameplate goes within a few seconds (the UMG plates do not
     check hidden yet, see above), a far bag's loot label is gone, and
     `valhalla.DebugListActors` on the client lists nothing beyond 8 m.
   - A friendly test NPC (`type: npc`, e.g. `merchant_bjorn`, optionally with
     `canAggro` ticked): walking up does not aggro it; auto-attack and a
     SingleEnemy spell on it say "Invalid target"; an AoE does not hurt it.
   - Leave the Grasslands profile off again before committing `zones.json`.
