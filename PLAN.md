# Valhalla 2.0 — Port Plan

Porting the Valhalla 1.0 TypeScript multiplayer game (Colyseus + Phaser) to
Unreal Engine 5.8 / C++20.

**Ground rules for every phase**

- The 1.0 repo at `../../valhalla` stays the single source of balance data.
  2.0 reads `shared/data/*.json` at runtime; it does not duplicate them into
  `.uasset` files, so the two projects cannot drift.
- Game rules live in `ValhallaCore` and depend only on
  Core/CoreUObject/Engine/Json. If a rule needs a `UWorld`, it is in the wrong
  module.
- Every ported formula is checked against a fixture generated from the running
  1.0 server, not against someone's reading of the code.
- 1.0 distances are in pixels. Conversion to Unreal units happens once, in
  Phase 3, and nowhere else.

---

## Phase 0 — Prereqs

- [x] UE 5.8 installed, `Valhalla2` Blueprint project created.
- [x] Visual Studio 2022 with the C++ game development workload.
- [x] 1.0 repo checked out beside the UE project at `../../valhalla`.
- [x] First successful `Build.bat Valhalla2Editor Win64 Development`.
- [x] Editor opens the project with both C++ modules loaded.

## Phase 1 — Data + rules

### Phase 1a — data model, loader, stat math (this pass)

- [x] Project converted to C++: `Valhalla2.Target.cs`, `Valhalla2Editor.Target.cs`,
      `Valhalla2Server.Target.cs`.
- [x] `ValhallaGame` primary game module (minimal, Phase 2 fills it).
- [x] `ValhallaCore` runtime module with `LogValhallaCore`.
- [x] `.uproject` declares both modules and the `ValhallaTools` plugin.
- [x] `ValhallaTypes.h` — StatBlock, ResolvedStats, ClassTemplate, ItemTemplate,
      SkillTemplate, NPCTemplate, LootEntry/LootTable, ZoneConfig, StartingItem,
      plus the string-backed UENUMs and their parsers.
- [x] `ValhallaConstants.h` — every constant from `constants.ts` and `stats.ts`.
- [x] `UValhallaDataSettings` (`DataRoot`, resolved against `FPaths::ProjectDir()`)
      and its `Config/DefaultGame.ini` section.
- [x] `UValhallaDataSubsystem` — loads all 7 JSON files, hand-written field by
      field, never fatal on bad data, counts logged, `Reload()` public.
- [x] `ValhallaStats` — every exported function of `stats.ts`, plus
      `ResolveDamage` as a deterministic mirror of `CombatSystem.applyStatDamage`.
- [x] Automation tests `Valhalla.Core.Stats.*` and `Valhalla.Core.Data.Loads`.
- [x] `Plugins/ValhallaTools` with the `ValhallaDataTools` Python toolset.
- [x] Compile, then run `Automation RunTests Valhalla.` and confirm the fixture
      comparisons actually pass. 15/15 green as of Phase 5.

### Phase 1b — remaining rules

- [x] Skill resolution: cast time, cooldown groups, resource costs, buff
      stacking modes — `UValhallaSkillComponent` and `UValhallaCombatLibrary`,
      pinned by `Valhalla.Game.Skills.Cooldowns` and `.BuffStacking`.
- [x] Loot rolling (weighted entries + drop chance) as a deterministic function
      taking its rolls, like `ResolveDamage` —
      `UValhallaInventoryLibrary::RollLootTable`. Note `weight` is read and
      ignored, because `rollLootTable` (LootBagSystem.ts:140) never consults it.
- [x] XP award and level-up, including the level 20 stat cap vs. level 25 XP cap.
- [x] Equipment stat aggregation: class base + level growth + equipped items —
      `UValhallaInventoryLibrary::ComputeStatsWithEquipment`. The summation is
      ported from GameScene.ts:4285, the only place in 1.0 that ever does it;
      the 1.0 *server* never applied equipment bonuses at all.

## Phase 2 — Netcode core

- [x] Decide the authority model: dedicated server (`Valhalla2Server` target) is
      authoritative for all combat; clients predict movement only.
- [x] `AValhallaGameMode` / `AValhallaGameState` / `AValhallaPlayerState`.
- [x] Replicated character stats driven by `FValhallaResolvedStats` — the vitals
      and vision range only. The resolved stat block is server-only on purpose;
      see AValhallaPlayerState's class comment.
- [x] Server RPCs for the 1.0 message set (`MessageType` in `types.ts`): input,
      cast skill, auto-attack start/stop, inventory actions, chat, party.
- [x] Combat events as multicast RPCs (hit, miss, dodge, block, died, respawned).
- [x] Server-side RNG feeding `ResolveDamage`, with the rolls logged so a
      contested hit can be replayed exactly — `FValhallaDamageRolls`.
- [x] Invulnerability window (`Valhalla::InvulnerabilityMs`) enforced server-side.

## Phase 3 — World

- [x] Pixel -> Unreal unit conversion, defined once: 1.0 pixels and 2.0
      centimetres are 1:1, so a 64 px tile is a 64 cm tile and a 64 x 64 map is
      4096 cm square. `build_zone.py`'s `TILE`/`N`/`ZONE_CM` are the only place
      it is written down.
- [x] One persistent world hosting every zone, as 1.0's single Colyseus room
      did. `L_World` carries the lighting and nothing else; `L_Grasslands` and
      `L_Desert` are always-loaded streaming sublevels, the desert at +40000 cm
      on X. One server, one `AValhallaGameState`, one `ServerFixedTick`, both
      zones resident. See `build_world.py`'s docstring for the three
      alternatives this was chosen over.
- [x] `AValhallaZoneVolume` + `UValhallaZoneSubsystem`: zone bounds, display
      name and default spawn discovered from level actors rather than config,
      because a streaming sublevel's offset is not in any config file.
      `PlayerState::ZoneId` is derived from the volume the pawn is in, as step 8
      of the fixed tick.
- [x] Zone connections and travel: `AValhallaPortal` + `AValhallaZoneEntry`,
      server-side overlap, velocity zeroed, 1 s cooldown — the ping-pong guard
      1.0's `checkZoneTransitions` never had. Announced as a `zoneChange`
      combat event.
- [x] Overlay-driven spawning. `maps/overlays-2.0/<zone>.json` is 1.0's overlay
      in zone-local centimetres with the portal's destination split into
      `targetZone` + `targetEntry`. Enemy and NPC spawns are *created* from it;
      portals, entries and player spawns are *validated* against the placed
      actors. `valhalla.ReloadOverlays` re-reads it without leaving PIE.
- [x] Levels built by reproducible Python (`build_grasslands.py`,
      `build_desert.py`): floors as one `AValhallaTileField` per tile mesh,
      everything that can block sight as individual actors — see
      `AValhallaTileField`'s class comment for why that split is forced rather
      than chosen.
- [x] Top-down captures for the Phase 6 editor: `capture_zone_topdown` writes
      `maps/thumbs/<zone>.png` (2048², orthographic) and `<zone>.json` with the
      five numbers needed to turn a click into a coordinate.
- [x] Material consolidation: 92 Interchange per-mesh copies collapsed to 39
      canonical `M_ValhallaToon` instances under
      `/Game/Valhalla/Environment/Materials`.
- [ ] Tiled map import (`TiledMapParser.ts`). **Not done, and deliberately.**
      The 2.0 zones are Unreal levels built by script from the Blender kits;
      importing 1.0's isometric Tiled tile grids would produce a second,
      worse description of the same zones. What the 1.0 maps still own — enemy
      groups, portals, spawn points — comes across as the overlay, which is the
      part a designer actually edits. If a 1.0 map ever has to be traced, the
      reference is still `TiledMapParser.ts`.
- [ ] Navigation mesh generation. The NPCs move by the ported flat state
      machine and direct steering, as 1.0's did, so nothing needs a navmesh
      yet; Phase 4d's behaviour trees are what will.

**What Phase 3 owes Phase 5.** Line of sight is already automatic for any level
the importer builds, provided it keeps three conventions:

1. **Name blockers `VB_`.** A static mesh whose asset name starts with `VB_`
   gets the `VisionBlocker` collision profile at import (`import_gltf_batch`),
   and anything placed from it blocks sight. Nothing else does: the channel's
   default response is Ignore, so floors, trees and rocks are transparent
   without being touched. Re-running `apply_vision_blocker_profile` fixes a
   level that was built before a re-import.
2. **Drop one `AValhallaFogBounds` per level**, sized to the playable area.
   Without it the fog falls back to the bounds of every `VB_`/`SM_` static mesh
   plus ten per cent, which works but is a guess; and the box is stretched over
   1024 texels, so an oversized one costs resolution.
3. **Nothing else.** Relevancy culling is on the actor classes, not on the
   level, and the fog renderer is spawned by the player controller. A new level
   needs no Phase 5 wiring at all beyond the two points above.

## Phase 4 — Characters

- [x] Character actor + movement tuned to `ClassTemplate.baseSpeed`.
- [x] glTF asset pipeline. `import_gltf_batch` handles the environment kits;
      characters go through `import_character_glb`, which drives Interchange
      with an override pipeline so all 20 skeletal meshes and 7 animations
      bind to one `SK_Valhalla_Skeleton` — `import_gltf_batch` cannot, because
      an automated `AssetImportTask` mints a fresh skeleton per source file.
- [x] Animation: idle / walk / melee / ranged / cast / hit / death, chosen by
      the equipped weapon's `weaponStyle` rather than by class, as 1.0 did.
      `UValhallaAnimComponent`, driven by velocity, the replicated cast state
      and the combat events — so simulated proxies and NPCs animate too.
- [x] Equipment visuals — the 1.0 paperdoll layers are seven skinned follower
      components on `SetLeaderPoseComponent(BodyMesh)` plus two socketed
      props, keyed on `ItemTemplate.spriteId` exactly as 1.0 keyed its sprites.
- [x] NPC spawning from `npc-templates.json` — `AValhallaNPCSpawner`. NPCs use
      the same rig and animation driving as players, with a fixed worn kit.
- [ ] Behaviour trees per `behaviorType` (Phase 4d). The ported flat state
      machine in `AValhallaNPC::ServerFixedTick` is what runs today, and it is
      what a behaviour tree will have to be checked against.

## Phase 5 — Line of sight

- [x] Dedicated `VisionBlocker` trace channel and collision profile, declared in
      `Config/DefaultEngine.ini`. The channel's default response is Ignore, so
      only the `VB_` meshes block it and nothing else needed editing. This
      closes the TODO `import_gltf_batch` had been carrying since Phase 4.
- [x] Per-class vision range from `FValhallaClassTemplate::VisionRange`, read
      off the viewer's own `AValhallaPlayerState` — 1200 warrior/cleric/shaman/
      wizard, 1350 rogue, 1800 ranger.
- [x] Server-side visibility culling. `UValhallaVisibilitySubsystem::IsVisibleFrom`
      is a 2D range test plus a `VisionBlocker` line trace eye to eye, cached
      per (viewer, target) pair for `CacheTicks` server ticks; `IsNetRelevantFor`
      on `AValhallaCharacter`, `AValhallaNPC`, `AValhallaSpellProjectile` and
      `AValhallaLootBag` asks it and nothing else. Owner, self and party
      members' pawns are unconditional.
- [x] Fog of war presentation on the client. `AValhallaFogRenderer` rebuilds the
      1.0 visibility polygon each frame from the `VisionBlocker` geometry around
      the pawn, draws it into two world-space masks, and `PP_Fog` reads world XY
      back out of the scene depth to dim the scene to 1.0 / 0.5 / 0.05.
- [x] `valhalla.DebugListActors` — what each PIE client world was actually sent.
      The proof that culling works, because a client world contains exactly the
      actors that were replicated into it.

**Known and accepted.** `AValhallaPlayerState` is always relevant (Unreal sends
every player state to every client), so character names, levels and HP bars leak
for players whose pawns are culled. `RelevantTimeout=0.5` is net-driver-global,
so a culled actor lingers on a client for up to half a second before its channel
closes. A projectile that flies out of line of sight mid-flight stops updating
where it is; its detonation still arrives as a multicast combat event.

## Phase 6 — Editor continuity

- [x] Keep the 1.0 web editor as the authoring tool; 2.0 only ever reads.
- [x] **Phase 6b — the admin API.** `UValhallaAdminServer`, a world subsystem
      in `ValhallaGame` serving the 1.0 `server/src/routes/admin.ts` contract
      verbatim on loopback port 2568 (`ValhallaDataSettings.AdminApiPort`), so
      the existing React Live Dashboard drives the 2.0 server unchanged.
      Compiled into the Editor and Server targets only
      (`WITH_VALHALLA_ADMIN_API`), started by `AValhallaGameMode::InitGame` and
      stopped on `EndPlay`, one instance per process. Coordinates cross the
      boundary as **zone-local centimetres** — the zone volume's min corner is
      (0, 0) — converted by `ToZoneLocalCm`/`FromZoneLocalCm` and nowhere else,
      which is what keeps `L_Desert`'s +40000 cm streaming offset out of the
      wire format. `sessionId` is `APlayerState::GetPlayerId()` as a string and
      `npcId` is the NPC actor's `GetName()`.
- [x] Watcher on `DataRoot` calling `UValhallaDataSubsystem::Reload()` —
      `AValhallaGameState::TickDataWatcher`, a 2 s poll of the seven data files'
      and every overlay's mtime rather than an `IDirectoryWatcher`, because a
      packaged dedicated server does not have the editor's DirectoryWatcher
      module. `valhalla.DataHotReload`, default on in non-shipping. A data
      change runs `AValhallaGameMode::ReloadGameData`, which re-resolves every
      logged-in player (Phase 2c's `RecomputeStats`, which now also re-reads
      `visionRange`) and re-reads every live NPC's template; an overlay change
      runs `UValhallaZoneSubsystem::ReloadOverlays`. Both are the same functions
      the `reload-data` / `reload-overlays` routes call.
- [x] `FValhallaItemTemplate::MeshId` — optional JSON `meshId`, and
      `GetArtId()` (meshId else spriteId) as the single rule every art lookup
      in `ValhallaVisuals` goes through. 1.0's sprite grouping was right for
      sprites and is wrong for meshes; this is how an item opts out without
      editing the field the 1.0 client still reads.
- [ ] Editor utility to diff loaded data against the files on disk.
- [ ] Validation commandlet wrapping `validate_data_json` for CI.

**Done in Phase 7b.** `UValhallaAdminServer::Authorize` now requires
`Authorization: Bearer <UValhallaDataSettings::ServerSecret>` when
`bAdminApiRequireSecret` is set, comparing in constant time. The flag defaults
*off* in the Editor and *on* in a Server build, because the 1.0 React dashboard
reaches 2568 through `editor/src/server.ts`'s proxy and that proxy does not
send the header yet — teaching it to is Phase 8's, and it is the one thing
that stops the flag being on everywhere.

## Phase 7 — Backend

### Phase 7a — the server-to-server routes (the 1.0 repo's side)

- [x] `server/src/routes/internal.ts`, guarded by `X-Server-Secret`:
      `GET /api/health`, `POST /api/auth/verify`,
      `GET /api/characters/:id/load?userId=`, `PUT /api/characters/:id/save`.
      Mounted at `/api` *before* the JWT-protected `charactersRouter` so that
      `/api/characters/:id/load` is not swallowed by it.

### Phase 7b — the 2.0 client and server (this pass)

- [x] Account and character persistence — **1.0's server kept**, not replaced.
      The 1.0 repo is the source of truth for data and an account is data; two
      services would mean two user tables and a decision about which one a
      player's characters really live in. A 1.0 client and a 2.0 client now log
      into the same account and see the same characters.
- [x] `UValhallaBackendSubsystem` — a GameInstance subsystem over
      `FHttpModule`, split by audience rather than by instance: the client half
      (`Register`/`Login`/`ListCharacters`/`CreateCharacter`/`DeleteCharacter`)
      authenticates with the player's bearer token, the server half
      (`Verify`/`LoadCharacter`/`SaveCharacter`/`Health`) with
      `X-Server-Secret`. 10 s timeouts, every call logged at Verbose, passwords
      never logged and tokens only as their first eight characters.
- [x] Character creation applying `ClassTemplate.startingItems` — `POST
      /api/characters` does it, exactly as 1.0's `CharacterService.createCharacter`
      always did. `AValhallaGameMode::InitializeJoiningPlayer`'s
      `GrantStartingItems` call is now **skipped for any joiner with a verified
      token**, and the load's `bNeedsStartingItems` (no inventory AND no
      equipment AND level 1) is the only thing that can re-grant it. That is
      the line Phase 2c said Phase 7 would replace, and this is the replacement.
- [x] Inventory and equipment persistence, through one
      `UValhallaBackendSubsystem::BuildSaveData`. Saved on `Logout`, on a 30 s
      per-player timer (`Valhalla::SaveIntervalMs`, constants.ts:62) and on a
      zone change — the last because a crash between a portal and the next
      autosave would otherwise bring a character back in the zone it left at a
      position local to the zone it arrived in.
- [x] Session flow: login -> character select -> zone join. `L_FrontEnd` is a
      client-only map (`GameDefaultMap`; `ServerDefaultMap` stays `L_World`)
      carrying `AValhallaFrontEndGameMode` as its World Settings override, and
      its UI is built from `WidgetTree` in C++ with no Blueprint assets — a
      login screen is the one screen where a silent change to what a field is
      bound to is a security bug, and a `.uasset` cannot be reviewed in a diff.
- [x] Server join: `PreLoginAsync` -> `Verify` -> reject with the backend's own
      reason, then `PostLogin` -> `LoadCharacter` with the pawn spawn deferred
      (`HandleStartingNewPlayer_Implementation` returns early while a load is
      pending) and finally `RestartPlayerAtTransform` at the saved position.
      Spawning first and teleporting on arrival would put a character in the
      grasslands for a whole round trip, visible to everyone.
- [x] Automation tests `Valhalla.Game.Backend.SaveDataJson` and
      `.ApplyLoaded`. 22/22 green.

**Two things that bite, both written down where they bite.** Unreal's ini
parser swallows `//` comments (`FParse::ELineExtendedFlags::SwallowDoubleSlashComments`),
so an unquoted `BackendUrl=http://host:port` arrives in C++ as `"http:"` and
every request then fails exactly like a backend that is down; the ini quotes
the value and `GetResolvedBackendUrl` repairs and warns about it anyway. And
`UWorld::ServerTravel` with `bAbsolute=true` discards the base URL, so the
front-end map's redirect to `L_World` moved a PIE server off
`ULevelEditorPlaySettings::ServerPort` onto the default 7777 while every client
kept knocking on 17777 — the travel is relative for that reason.

**Play-In-Editor.** Open `L_FrontEnd`, set Play Net Mode to *Standalone* with
2 clients, *Launch Separate Server* on and *Run Under One Process* on. The two
clients start on the front end and the server — which PIE also hands
`L_FrontEnd` — travels itself to `L_World` on its first tick.
`valhalla.AutoLogin` is a comma-separated list of `user:pass:charname[:class]`
dealt to front-end clients in start order, exactly as `valhalla.ClassAssignment`
deals classes and for the same reason: every PIE client in one process shares
one set of cvars. `valhalla.AutoLoginNow <entry>` does one client on demand,
and `valhalla.FrontEnd.PieServerAddress` is where the travel goes in PIE.

- [ ] Character *deletion* of the account itself. The backend has no route for
      it and 2.0 does not invent one.
- [ ] The action bar is carried through a load and a save untouched; Phase 8
      owns it. Dropping it here would have silently wiped a bar the 1.0 client
      still renders.

## Phase 8 — Polish

- [ ] The editor proxy (`editor/src/server.ts`) must send
      `Authorization: Bearer <ServerSecret>` to 2568, so
      `bAdminApiRequireSecret` can be turned on in the Editor too. Until then
      the flag is off there and on in Server builds — see Phase 6b's note.
- [ ] Front-end polish: the two screens are functional and plain. A character
      row is a tinted button with one line of text; there is no portrait, no
      class art, no keyboard navigation and no "remember me".
- [ ] UMG HUD driven by `ui-config.json` (kept raw by the loader for exactly
      this reason).
- [ ] Action bar, cast bar, nameplates, chat, inventory, death overlay — the
      seven sections `ui-config.json` already describes.
- [ ] Damage numbers, hit/miss/dodge/block feedback.
- [ ] Audio, VFX, and the pass over skill telegraphs.
