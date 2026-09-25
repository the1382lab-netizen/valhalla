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
- [x] The action bar is saved and loaded with the character: the game mode puts
      the saved bar on the pawn at spawn and saves the pawn's bar back (2026-09-24,
      see "Action bar — empty by default" below).

## Phase 8 — Polish

- [x] The editor proxy (`editor/src/server.ts`) sends
      `Authorization: Bearer <VALHALLA_SERVER_SECRET>` to 2568, and
      `bAdminApiRequireSecret` is on everywhere (Phase 9).
- [ ] Front-end polish: the two screens are functional and plain. A character
      row is a tinted button with one line of text; there is no portrait, no
      class art, no keyboard navigation and no "remember me".
- [ ] Audio, and the pass over skill telegraphs.

### Phase 8a — VFX and animation (done, in baseline `fe9d055`)

- [x] Niagara systems `NS_Slash/Bolt/Impact/AoERing/Cone/Heal/BuffAura/Debuff`
      (`build_vfx.py`, `build_vfx_materials.py`), chosen per skill by
      `UValhallaVfxLibrary` and spawned client-side from combat events by
      `UValhallaVfxSubsystem`; `AValhallaSpellProjectile` for fireball.
- [x] `UValhallaAnimInstance` (two-way blend idle/walk + one-shot attack, cast,
      hit, death) and the toon/outline materials.
- [x] Automation test `Valhalla.Game.Vfx.Mapping`.

### Phase 8b — the UMG game HUD (done)

`UValhallaGameHUDWidget` (Source/ValhallaGame, ~3,000 lines) is one
`UUserWidget` built from `WidgetTree` in C++ — no Blueprint assets, like the
7b front end — and laid out from `ui-config.json` through the typed
`FValhallaUIConfig` (ValhallaCore; every field defaulted, so a missing section
never blanks the HUD). `AValhallaHUD` creates it for the local player and keeps
only the world-anchored, clickable "Loot (n)" bag labels (Phase 9); the Phase 2
canvas HUD is behind `valhalla.DebugHud 1`.

- [x] Vitals (`hud.*`), action bar with generated skill icons (category-tinted
      tile + `iconAbbrev`) and a cooldown sweep (`actionBar.*`), cast bar
      (`castBar.*`), target frame with buff tokens, party frame with an
      Accept / Decline invite prompt (`ClientPartyInvite`), nameplates
      (`nameplates.*`), floating combat text, death overlay (`deathOverlay.*`).
- [x] Combat log (50 lines, the 1.0 wording) with the 11 right-click filters.
- [x] Chat box (`chat.*`): Enter opens it (UI-only input, so WASD does not walk
      while typing), `/g /world /p /w <name> /invite /accept /decline /leave`
      (`ValhallaChatCommands`, pure and tested), Tab cycles the channel, idle
      lines fade. Kept clear of the action bar on narrow viewports.
- [x] Character + inventory panel (I; B too until B-07; `inventory.*`): 1.0 item icons
      imported to `/Game/Valhalla/UI/Icons/Items` (`import_ui_icons.py`, 68
      textures, check-before-create), tooltips, click to equip/unequip, drag to
      move/equip, right-click drop with a confirm.
- [x] Skills pane (K): drag, or click a skill then click a slot
      (`ServerSetActionBar`). Drag a skill off the bar, or right-click it, to
      remove it (2026-09-24).
- [x] Loot panel: Phase 9's `TryOpenLootBag` (click a bag or its label) opens
      it; take one, Loot All, Close; closes out of reach or when emptied.
- [x] Live layout: `valhalla.ReloadUI`, and a 2 s poll of the file's
      timestamp, rebuild the HUD from the UI Layout editor's saves.
- B-07 (2026-09-24): the layout now lives in the WBP_GameHUD Widget Blueprint
  (Project Settings > Valhalla > UI > Game HUD Class); the code-built panels and
  most of `ui-config.json` are gone (see `## B-07`).
      `valhalla.UI [@class] <verb>` drives a client's HUD for scripted gates.
- [x] **Instant casts lost their events.** Every combat event was its own
      unreliable `NetMulticast`; the engine sends at most
      `net.MaxRPCPerNetUpdate` (2) calls of one unreliable RPC per actor net
      update, so an instant skill's third event onward (its `skillEffect`,
      the one the VFX and the log need) never reached a client. Events are
      now queued and flushed once per server frame as one reliable
      `MulticastCombatEvents(TArray)`, in order. The client mirrors cooldowns
      from `skillEffect` for the action bar sweep; the game state raises
      `OnCombatEvent` for the HUD.
- [x] VFX: saturated palette with the sprite glow 2.5 -> 1.6; a fireball's cast
      event no longer draws a second impact (`SpawnsProjectileActor`).
- [x] Tests `Valhalla.Game.UI.ConfigParse`, `Valhalla.Game.UI.ChatCommands`.
      25/25 green.
- [ ] Left for 8c: vendor / dialogue windows (data is parsed only), a
      minimap, keybinding UI, locked (level-gated) skills can still be put on
      the bar (they show dimmed and fail on use), skill range is centre to
      centre while melee reach is surface to surface (Shield Bash, range 64,
      is "Out of range" at an NPC's own attack distance), audio.

## Phase 9 — Live-play fixes (Kevin's list, 2026-09-22)

The 1.0 Phaser client is **retired**. The 1.0 Node server stays as the
account/character backend, and the web editor stays as the data and live-ops
tool; neither needs a Colyseus game room any more.

- [x] **Backend reads the editor's JSON at startup.** `DataManager` used to
      initialize only when a 1.0 game room opened, which 2.0 never does, so
      the internal routes validated saves against the hard-coded
      `ITEM_CATALOG` and refused (400) any character carrying an editor-only
      item. Now `initializeAndWatch()` runs in `index.ts` and reloads on every
      `shared/data/*.json` change, keeping the old data if a file is caught
      half-written. `server/scripts/smoke-internal.ts` pins it.
- [x] **NPCs are placed in Unreal.** NPC *types* are Blueprints of
      `AValhallaNPC` under `/Game/Valhalla/NPCs` (`BP_NPC_*`): Class Defaults
      name the npc-templates.json entry (stats, behaviour, loot, respawn time
      stay in the JSON) and the look (armour meshes, scale, tint, fixed name).
      `AValhallaNPCSpawner` ("NPC Spawn Point" in Place Actors) spawns **one**
      instance of one type, on the floor under the marker, facing its arrow,
      with an editor preview of the NPC and Map Check warnings for a missing
      type/template or a capsule inside geometry.
- [x] **EverQuest respawn.** Death starts the spawn point's countdown
      (template `respawnMs`, edited in seconds in the NPC editor, or the spawn
      point's Respawn Time Override); the corpse decays first and a fresh
      instance spawns. A type dragged straight into a level also works and
      stands itself back up on the template's timer.
- [x] **Spawn points live in gameplay sublevels** (`L_Grasslands_Gameplay`,
      `L_Desert_Gameplay`, streamed at their zone's offset) that
      `build_world.py` creates once and never clears. `npc_setup.py` created the
      first types and turned every overlay `enemy_spawn`/`npc_spawn` into
      individual spawn points, moved clear of geometry; the overlay loader now
      ignores those entries with a warning. The Map Objects page no longer
      places NPCs.
- [x] **Friendly NPCs.** Template `type: "npc"` is not hostile (cannot be
      attacked, never aggroes) — replicated as `bFriendly`. Bjorn the Trader
      has his own template (`merchant_bjorn`) and type. Vendors/dialogue are
      still parsed only.
- [x] **Body scale fix.** `spriteSize` scales the capsule with the mesh (it
      used to bury anything above 1.0), and scale/tint replicate so clients
      draw what the server has.
- [x] **Controls.** Right mouse button = hold and drag to orbit the camera
      (pitch clamped), wheel = zoom. Right click no longer attacks or loots.
      (Superseded in part by the Controls rework below: the drag now also
      turns the body, and the mouse no longer aims.)
      Auto-attacks are the action bar's skills, and each is its own kind:
      Auto Melee (`melee_attack`) and Auto Ranged (`ranged_attack`, needs a
      ranged weapon) — `ServerStartAutoAttackWith`.
- [x] **Loot.** Bags have a generous click sphere on a dedicated `Interact`
      trace channel (ECC_GameTraceChannel2) that nothing else blocks; their
      "Loot (n)" label is drawn over the world and is itself clickable, so an
      NPC or corpse in front of a bag no longer hides it. Clicking opens a loot
      window (take one item, Loot All, close); out of reach says "You are too
      far away to loot that."
- [x] **Admin API: MMO actions.** `/player-action` (set-vitals, heal, kill,
      resurrect, give-item, remove-item, set-level, grant-xp,
      teleport-to-player, unstuck, freeze, god-mode, reset-cooldowns,
      clear-buffs, message, mute, save), `/player-inspect`, `/broadcast`,
      `/spawn-point-action`; `kick-player` takes a reason; `/state` lists spawn
      points and admin flags. Every POST is appended to
      `Saved/Logs/ValhallaAdminAudit.log`.
- [x] **Live Dashboard.** Errors are shown; zone tabs come from the server;
      a player list grouped by zone with the full right-click admin menu,
      inspect window and broadcast; spawn points drawn with their countdown.
- [x] **Melee reach fix.** Player auto-attack range is now measured surface to
      surface (both capsule radii), like the NPC side; before, an NPC standing
      at its own attack distance was always just out of the player's reach.
      Pressing an auto-attack with a different enemy selected switches to it.
- [x] **Admin ids are world-unique.** Actors in a gameplay sublevel are
      reported as `<Level>.<Name>` (both sublevels have a
      `ValhallaNPCSpawner_0`).
- [x] **Unity builds off** for both modules: once the tree was committed,
      UBT's adaptive unity merged files that each define the same helper in an
      anonymous namespace.
- [x] **Ban / suspend an account.** Backend: `users.banned_until` /
      `ban_reason` / `banned_by` (migrated on start); login, `/api/auth/verify`
      and every JWT route answer 403 with the end date and reason; internal
      `GET /api/accounts/bans`, `POST /api/accounts/ban|unban` (X-Server-Secret).
      UE: `UValhallaBackendSubsystem::BanAccount/UnbanAccount/ListBans`,
      player-action `ban` (the player's account, then kicks every connection
      on it) and `/account-action` (ban / unban / list-bans by username, for
      offline accounts). `/state` players carry `account` and `userId`.
      Dashboard: "Ban / suspend account…" on a player, and a "Bans…" dialog.
      Smoke test covers it (81 assertions); e2e checked through the admin API.
- [x] **Clients reload data when the server does.** `AValhallaGameState::
      DataVersion` (replicated) is bumped by `ReloadGameData`; its OnRep reloads
      the client's data subsystem, redraws every paperdoll and prints "Game data
      updated by the server." Clients read their own `DataRoot`, so a remote
      client needs the same data files.

**Where it lives (from `git diff fe9d055`, merged with 8b in one tree).**
Controls: `AValhallaPlayerController` (IA_CameraOrbit/Look/Zoom replace
IA_SecondaryClick; `TryOpenLootBag`, `CloseLootWindow`, the Interact-channel
bag trace; admin freeze and mute honoured client/server side) and
`AValhallaCharacter::AddCameraOrbit/AddCameraZoom`. Auto-attack kinds:
`UValhallaSkillComponent::ServerStartAutoAttackWith`. NPCs:
`AValhallaNPC` (template/type/look, `bFriendly`, scale + tint replication,
respawn), `AValhallaNPCSpawner` (single spawn, editor preview, Map Check),
`UValhallaZoneSubsystem` (overlay spawns ignored), `build_world.py` /
`build_zone.py` / `npc_setup.py`, `BP_NPC_*`, the `_Gameplay` sublevels.
Loot: `AValhallaLootBag::InteractChannel` (+ `Interact` trace channel in
DefaultEngine.ini); its window is now the 8b UMG loot panel. Admin:
`UValhallaAdminServer` (+~900 lines), `UValhallaBackendSubsystem` ban routes,
`AValhallaPlayerState` admin flags, god mode in
`UValhallaCombatLibrary::ApplyDamage`, `AValhallaGameMode` data reload ->
`AValhallaGameState::DataVersion`. Both modules `bUseUnity = false`.

- [ ] Delete the retired 1.0 code paths (Colyseus `GameRoom`, the Tiled/legacy
      map editor, sprite-sheet fields). **On hold** (Kevin, 2026-09-22) — the
      Node backend and the web editor stay regardless; decide what counts as
      retired first. Snapshot commit `3630d75` in the 1.0 repo precedes it.

## Controls rework (Kevin, 2026-09-22)

Kevin: "Moving your mouse shouldn't re-position the character. Moving with WASD
will point your character in a new direction, or holding down mouse and moving
the camera will also point you in a new direction." And: a player who is not
facing the NPC they hit or cast on is not turned; the hit/cast fails with
"You must be facing your target".

Why: the Phase 2 port kept 1.0's cursor `aimAngle` — the body faced the mouse
every frame — which in a 3D third-person view read as the character twitching
whenever the mouse moved, and made facing meaningless (the server auto-turned
players to their auto-attack target anyway).

- [x] **The mouse does not aim.** `UpdateAimFromCursor` / `UpdateAimYaw` are
      gone. The cursor stays visible for click-to-target and loot (Phase 9,
      unchanged). It is read once, at the key press, for an `aoeGround` skill
      only (`AValhallaPlayerController::SampleCursorGroundPoint`); every other
      skill is sent the caster's own position.
- [x] **WASD turns the body.** `bOrientRotationToMovement = true`,
      `RotationRate = (0, 720, 0)`; movement stays camera-relative (W walks
      away from the camera). CharacterMovement predicts and replicates it.
- [x] **The right-mouse drag turns camera and body together.** Each orbit step
      turns the boom, then sets the body's yaw to the boom's
      (`OrbitCameraBy` -> `AValhallaCharacter::SetFacingYawFromCamera`); pitch
      is camera-only. Sent by `ServerSetFacingYaw` (was `ServerSetAimYaw`;
      2 degree deadzone, 20 Hz, now Reliable because the server's yaw is what
      the facing rule reads, plus a forced send when the drag ends).
      **Who wins:** while the character is walking (acceleration, or more than
      5 cm/s), orient-to-movement owns the yaw and the drag only turns the view
      — which bends the walk with the new WASD basis. Standing still, the drag
      owns it. Pending moves are flushed before each facing send so a late move
      cannot turn the server's body back towards the walk.
- [x] **Facing rule, no auto-turn.** `UValhallaCombatLibrary::IsFacing(Actor,
      Target, HalfAngleDeg = Valhalla::FacingHalfAngleDegrees)` — 2D, the
      target within 60 degrees either side of the actor's yaw (inclusive); a
      target under 1 cm away counts as faced. Applied on the server to:
      auto-attack swings (melee, ranged and the casters' magical basic — the
      swing is skipped, the loop stays on, the timer is not advanced so the
      swing lands the moment the player turns; a `skillFailed` with reason
      `notFacing` at most once per `NotFacingMessageIntervalSeconds` = 2 s) and
      `ValidateCast` for `singleEnemy` and `singleAlly`-at-someone-else
      (refused before any cooldown, resource or cast bar), re-checked when a
      timed cast completes. Self, `aoeSelf`, `aoeGround` and `cone` are exempt
      (a cone is already measured off the caster's yaw, ±45). The 1.0
      auto-face in `TickAutoAttack` (SkillSystem.ts:510) is removed; nothing on
      the server turns a player for combat. NPCs are unchanged.
- [x] **HUD.** `FValhallaCombatEvent::Reason` (new) carries `notFacing`; the
      combat log prints the server's sentence as is, and a "Not facing" floater
      rises over the player like miss / dodge.
- [x] If an `aoeGround` press finds no cursor over the world (outside the
      viewport, above the horizon) it aims at the selected target, else the
      caster.
- [x] Gate helpers: `valhalla.DebugFacing` (every player's yaw in every PIE
      world, camera yaw for the local one); `valhalla.UI @class orbit <deg>`
      (the drag, through the same `OrbitCameraBy`), `walk <w|a|s|d> <sec>`
      (holds a key through HandleMove's body, `ApplyMoveInput`) and
      `press <slot> <x> <y>` (the key press with the aoeGround cursor read at a
      viewport pixel). Slate key presses do not reach PIE input and typing in
      the editor console takes the cursor off the viewport, so these three are
      the gate's hands; type them into the editor console.
- [x] Test `Valhalla.Game.Combat.Facing`. 26/26.

**Gate (2026-09-22, L_World, PIE_Client, 2 clients, one process, warrior +
wizard). Pass.** (a) Slate hovers swept the warrior's in-game cursor across
the viewport for 5.7 s ((527,48) → (1164,15) …): yaw -45.0 before and after,
server and client. (b) Camera -45: S → 135, A → -135, W → -45, D → 45 (client
and server agree). Drag 90° while walking W: at the end of the drag the body
was still -45 and orient-to-movement then turned it to 45. (c) orbit 90: camera
-45 → 45 and body -45 → 45 on the client, the server and the other client.
(d) Test Enemy directly behind, Auto Melee on: no swings for 27 s and
`You must be facing your target` at 40.6, 42.9, 44.9, 46.9 … s; turned to face
it (orbit -135) at 07.280 and the first swing landed at 07.315, then every
3.2 s. (e) Magic Missile (`singleEnemy`; Fireball is `aoeGround` in
skills.json, so exempt) at an enemy behind: refused, mana 150 → 150, cooldown
0, no cast, "Not facing" floater; after facing it: cast, hit 76, mana 110,
cooldown 2.5 s. (f) Fireball `press 2 400 300`: independent deprojection of
that pixel (2183.7, 1206.8); `spellImpact … at (2183.71, 1206.75)`, cast with
the point 110° off the wizard's facing. (g) 26/26; no ensure, no new error
lines. Screenshots in `outputs/controls/`.

## Phase 8c gate — first playable (independent verifier, 2026-09-22)

Exit criterion: "four-player party in grasslands completes a fight, loot,
level-up and zone change with no server errors". Run from `L_FrontEnd`,
PIE_Standalone, separate server, one process, **4 clients**, throwaway accounts
`pt_0115690_1..4` (characters Ptwar/Ptcle/Ptran/Ptwiz5690, deleted afterwards;
the backend has no route to delete users). **Verdict: pass, after one fix
(below) and with the known issues listed.**

| # | Item | Result |
|---|------|--------|
| 1 | 4 in `L_World` grasslands, right class + starting gear | Pass. `Login:` lines for all four. Only the wizard gets gear, because only rogue and wizard have `startingItems` in classes.json (data gap, not code). |
| 2 | Party 4/4 via `/invite` x3 + `/accept` x3 | Pass. `partyUpdate received: party 1 with 4 member(s)` on all 4 clients; screenshot `02`. |
| 3 | Fight, floating text + log, NPC deaths, loot, 4-way XP | Pass. Auto-attacks plus Shield Bash, Aimed Shot, Magic Missile and Fireball, Smite, and Minor Heal on the warrior (`heal 39 Ptcle5690 -> Ptwar5690`). 4 kills, each `awardKillXP party 1: … split 4 ways = 27 each`. Warrior and ranger looted by clicking the loot panel. The cleric and wizard clicks never reached their panels (see Known issues), so they used `valhalla.DebugLootNearest`. |
| 4 | Level-up | Pass, from kills alone: `levelUp Ptwar5690 -> level 2` (all four). |
| 5 | Zone change, party frame, chat scoping, LoS | Pass. `zoneChange Ptran5690: grasslands -> desert`, and the same for Ptwiz5690. `general` reached 2 clients per zone; `party` reached 4. In the desert, `DebugListActors` shows 3 NPCs for the wizard (1200 cm vision) and 6 for the ranger (1800), out of 21 in the zone. |
| 6 | Persistence across a PIE restart | **Failed first, passed after the fix.** Level 2, xp 8, zone, position, inventory and equipment all came back. `valhalla.SaveNow` does not work in this PIE recipe; autosave and the admin `save` action were used instead. |
| 7 | Hot reload of skills.json | Pass. `valhalla.DataHotReload: …skills.json changed` then `4 players re-resolved, 39 NPCs updated`. The file was restored byte-identical (md5 `71b9eafb…` before and after). |
| 8 | `GET /api/admin/state` | Pass. With the bearer secret: 2 players in grasslands and 2 in desert (4 in grasslands earlier). Without it: 401. |
| 9 | Server health | Partial. No `Error:`, ensure or `LogNet: Warning` apart from the spawn failure the fix removes. The fixed tick was 99.9 % of wall clock with the editor in the foreground, but 88 % over the whole run: while the editor sat throttled in the background at about 3 fps, the 0.25 s catch-up clamp dropped time. Visibility: 14–29 traces/s. |
| 10 | Automation | Pass, 25/25. |

**Fix made (gate-blocking).** `AValhallaGameMode::SpawnLoadedPawn` spawned at
floor + `PlacementZOffsetCm` (8 cm), which puts the capsule centre inside the
floor, so `SpawnActor failed because of collision` refused 3 of 4 saved
positions and those players were left with no pawn. It now adds the pawn's
capsule half-height, and falls back to `RestartPlayer` if the spot is still
blocked. It was applied with Live Coding; the on-disk DLL needs a normal
`Build.bat` before the next editor start.

### Known issues (from the 8c run and review)

- `ValhallaLevelTools.run_console_command` (Python) runs every **client**
  Server RPC locally. Editor scripting sets `GAllowActorScriptExecutionInEditor`,
  so `valhalla.UI … press/say` driven that way never reaches the server. Type
  those commands into the editor console instead. The docstring's claim that it
  is "not a different code path" is wrong for client commands.
- `valhalla.SaveNow` does not use `AuthorityWorldFor`, so it fails from the
  editor console when the server is a separate PIE world.
- A cleric cannot target a party member except by clicking their body; party
  frame rows are not clickable targets.
- Loot-panel clicks through SlateInspector reached 2 of 4 PIE windows only;
  not diagnosed.
- A loot bag's 5-minute lifespan is not extended when a second kill merges
  into it, and its despawn is not logged.
- ~~The `Join request` log line prints the full JWT~~ (trimmed to 8 characters
  in B-04). The engine's `LogNet: Browse` / `Login request` lines still print
  the URL; game code can't change them.
- Combat events go to every client as one reliable multicast from the game
  state, with no relevancy filter and no size cap (`ValhallaGameState.cpp:372`).
  That leaks fight positions past LoS, and a lagging client risks reliable-buffer
  overflow.
- ~~The admin API sends `Access-Control-Allow-Origin: *`, and nothing refuses
  the public `dev-server-secret` on a Server build.~~ Fixed in B-04
  (`AdminApiAllowedOrigins`; servers outside the editor exit on the dev secret).
- Damage paths outside the pipeline:
  - Magic Missile and Backstab skip `ResolveDamage`, i-frames and god mode.
  - Player DoTs ignore god mode and shield.
  - Single-enemy skills do not check `bFriendly`.
  - The i-frame map `GInvulnerableUntil` is global and never pruned.
- Not persisted: `bAlive`, god/freeze/mute, buffs and cooldowns. (The action bar
  is, since 2026-09-24.)
- Relevancy fails open while a joiner has no pawn, which is the whole async load.
- Warrior, cleric and ranger have no `startingItems`. `M_Rope` and
  `M_Cloth_Blue` lack the SkeletalMesh usage flag. The front end logs
  `SpawnActor failed because no class was specified` once per client.

## Phase 10 — One repo, and data for remote clients (2026-09-22)

- [x] **The two folders are one repo.** `Valhalla 2.0/` is the git root:
      `Valhalla2/` (this Unreal project, same path as before), `shared/`
      (`shared/data` is still the single source of game data), `server/`,
      `editor/`, `maps/` (`overlays-2.0`, `thumbs`), `Import/`,
      `Blender assets/`, `Tools/` (`fixtures`, `unreal-mcp-bridge`).
      `DataRoot` is now `../shared/data`. The old `valhalla/` checkout is
      retired; its last state is the tag `archive/1.0-final` (branch `archive`; `dev` is now the working branch).
- [x] **Retired 1.0 code not carried over:** the Phaser client, the Colyseus
      game room (the backend is plain Express now), the Tiled/legacy map
      editor, the Zone editor, the paperdoll preview and the sprite-sheet
      fields, 1.0 sprites, Tiled maps and 1.0 overlays, `tools/mapgen`,
      `tools/paperdoll-gen`. Kept because Unreal uses them: `spriteId`,
      `spriteColor`, `spriteSize`, `bodyId`, and `inventoryIcon` (the icon
      PNGs moved to `Import/UI/Icons`).
- [x] **B-03, game data for packaged and remote clients.** Backend
      `GET /api/data/manifest` (`dataVersion` 0 plus a SHA-1 per file) and
      `GET /api/data/<file>`. A client that can't see `shared/data` (a
      packaged build, or `valhalla.Data.ForceDownload 1`) reads
      `Saved/Data`, seeded from `Content/Data` (staged by
      `stage_game_data.py`, packaged via `DirectoriesToAlwaysStageAsNonUFS`),
      and `UValhallaBackendSubsystem::SyncGameData` brings it up to date when
      the front end opens and on every server data reload. Files are written
      only after every changed one downloaded and matched its hash.

## Phase 11 — Weapon damage rolls; casters melee (2026-09-22)

- [x] **Weapons roll damage.** Items take `minDamage` / `maxDamage`; each
      auto-attack adds a uniform roll in that range to the base constant before
      strength scaling (`Valhalla::Stats::RollWeaponDamage`). A 1.0 flat
      `attackDamage` still works as a fixed range; unarmed rolls 1–3. Ranges:
      dagger 2–5, bone totem 2–6, staff 3–7, mace 3–8, bow 3–8, sword 4–9.
      Before this no melee weapon had any damage (only the bow's 5), so a
      weapon only changed damage through its stat bonus, which the floor often
      swallowed, and every non-crit hit on a target was identical.
- [x] **Casters melee.** Wizard, cleric and shaman auto-attack with their
      weapon like everyone else (EverQuest-style); their magic is their spells.
      1.0's magical basic (`isRangedMagic`, 450 cm) is gone from the
      auto-attack, and a staff now plays the attack swing rather than A_Cast.
      Checked in PIE: cleric unarmed 17–19, with the Iron Mace 21–25.
- [x] **NPC melee rolls too.** Monsters fight unarmed, so their templates
      take the same `minDamage` / `maxDamage` roll (falling back to the flat
      `damage`). Test Enemy 7–13, Tough Guy 30–50; editable in the NPC editor.
      Checked in PIE: Test Enemy raw hits 7.3, 7.7, 8.1, 10.7, 10.9.
- [x] **NPC weapons.** An NPC template can name a `weaponId` from items.json
      (web editor: Weapon dropdown listing every weapon-slot item, with its
      damage range; the validation panel flags a missing or non-weapon id).
      The NPC holds the item's mesh in `socket_weapon_r`, its weaponStyle
      picks the attack animation, and each hit adds the weapon's roll on top
      of the NPC's own (`Valhalla::Stats::RollNPCMeleeDamage`). The weapon's
      stat bonuses and attack speed do not apply to NPCs. Test Enemy carries
      the Iron Sword: 7–13 + 4–9 = 11–22; PIE hits 12.6–20.0.
- [x] **NPCs chase again.** NPCs have no controller, and CharacterMovement
      drops AddMovementInput on an uncontrolled pawn unless
      `bRunPhysicsWithNoController` is set, so every chase and walk-home step
      was ignored. Set in the constructor; checked in PIE (a Test Enemy walked
      213 cm to reach a player 3 m away). A miss or dodge on an NPC now adds 1
      threat. Straight-line chasing still gets stuck on walls: pathfinding is
      backlog item B-16.

## Phase 12 — B-15 art overhaul, Wave 0: style foundation (2026-09-22)

Direction: a modern Neverwinter Nights remaster. Decisions: mid-range PC target
(GTX 1660 class), golden area = Bjorn's market square, creatures stay in Wave 6.
All art by Opus + Blender MCP; CC0 textures; AI generators off.

- [x] **Art bible** — `Docs/ArtBible.md`: world scale is fixed (122 cm
      character, 64 cm grid, 180 cm walls; props ~0.68 × real), palette,
      material parameters, texel density and triangle budgets, naming,
      per-asset checklist.
- [x] **M_ValhallaPBR** — `valhalla_tools/build_pbr.py`. Keeps BaseColor /
      UseVertexColor / Emissive* (runtime tint and hair still work); adds a
      texture set (BC / DirectX normal / ORM), world-aligned UVs for ground,
      grime and macro variation. All 53 M_ValhallaToon instances re-parented
      with roughness/metallic by family (`reparent_to_toon()` rolls back). A
      live master is changed with `upgrade_pbr()`: `delete_all_material_expressions`
      on a master in use crashes the editor (`!IsRooted()`).
- [x] **Lighting and post** — `valhalla_tools/lighting_remaster.py` (`LOOK`),
      applied to L_World in place and used by `build_world.py`: warm sun, Lumen
      sky fill, faint cool fill, haze, gentle grade. Outline retired
      (`valhalla.Visual.Outline 1` brings it back). Removed the hand-placed
      DirectionalLights in L_Grasslands / L_Desert that doubled every shadow.
- [x] **CC0 texture pipeline** — Blender MCP Poly Haven on;
      `Blender assets/scripts/valhalla_textures.py` → `Import/Textures/<Set>/`
      → `valhalla_tools/import_texture_sets.py` → `MI_<Set>`. Proven with
      CobbleFloor (Poly Haven cobblestone_floor_001; previewed, applied in Wave 1).
- [x] **unreal-mcp bridge** — reconnects after an editor restart (stale
      session 404), no longer recurses on a dropped connection, and times out a
      call Unreal never answers at 50 s. Tested against a mock server.
- Review shots: `Saved/ArtReview/wave0/` (before, lighting, textured-cobble preview).

## Phase 13 — B-15 art overhaul, Wave 1: town and grassland kit, weapons (2026-09-22)

Every Wave 1 mesh is rebuilt in Blender by script (`Blender assets/scripts/wave1_*.py`,
shared helpers in `valhalla_kit.py`), exported to the same `Import/` glb and
re-imported in place by `valhalla_tools/import_kit.py` (same asset path, pivot,
footprint and collision profile; materials bound by slot name = MI name).
CC0 textures only (Poly Haven, ambientCG); sources in `Import/Textures/texture_sets.json`
(the PNGs are gitignored — re-download with the manifest).

- [x] **Ground** — grass / dirt / stone / cobble / wood tiles; `M_ValhallaGroundBlend`
      (dirt with a noisy grass verge by vertex colour); `road_lanes.fix_open_world()`
      turned 121 road tiles so the verge faces away from the road.
      `build_grasslands._road_yaw` does not apply the lane rule yet: a rebuild
      of L_Grasslands must run `road_lanes.fix_open_world()` afterwards.
- [x] **Buildings** — timber-frame house walls (plain / window / door), thatch
      roof, stone walls (straight / corner / end). Bounds identical; VisionBlocker kept.
- [x] **Props** — barrel, crate, sack/loot bag, fence, signpost, lamp post,
      market stall, well. `lamp_lights.add_all()` puts a warm, shadowless
      PointLight on every lamp post.
- [x] **Weapons** — sword, staff, bow, mace, buckler, kite shield, plus new
      dagger and bone totem (`items.json` meshId `dagger_iron`, `totem_bone`).
      `import_kit.reimport_weapons()`; `character_import._pipeline()` now works
      on UE 5.8 (EditorAssetLibrary no longer loads /Interchange content).
- [x] **Nature** — trees A/B (bark tubes + leaf-mass cores + leaf cards), rock,
      water. New masters `M_ValhallaFoliage` (masked, Two Sided Foliage, edge-on
      card fade, per-tree tint) and `M_ValhallaWater` (panning ripple normals).
- [x] **Perf check** — 30 animated bodies + the square's NPCs at Bjorn's
      market, game camera: ~20 ms/frame (49 fps) in editor PIE on the dev PC
      (PIE includes editor overhead; not yet measured on a GTX 1660).
- Known: market stall and well use 7 material slots (trim sheet later); the
  thatch ridge-roll end cap UVs stretch close up; tree wind not done; pond
  edges follow the tile grid.
- Review shots: `Saved/ArtReview/wave1/` (`final_*`, `weapons_close_0`, `perf30_0`).

## Phase 14 — B-15 art overhaul, Wave 2: characters (2026-09-23)

Built on Fable 5.1 at Kevin's request, in three reviewed stages. Scripts:
`Blender assets/scripts/wave2_body.py`, `wave2_hair.py`, `wave2_body_textures.py`,
`wave2_armour.py`, `wave2_ambientcg.py`, `wave2_anims.py`, `wave2_review.py`.

- [x] **Body + hair** — organic body (shells → voxel remesh → scripted sculpt →
      QuadriFlow, seam welded), scripted weights on the unchanged
      SK_Valhalla_Skeleton; procedural SkinBase / HairStrands sets; runtime skin
      tint and hair vertex colour unchanged. Race_Stocky / Race_Slender shape keys
      live in the .blend/glb (Unreal import has morph targets off).
- [x] **Armour** — all 16 SK_ pieces rebuilt, fitted to the body by ray casts,
      CC0 ambientCG Leather028 / Chainmail004 / Fabric032 / Metal009. Equipment
      materials are bound like `w2b_equipment_import.py`, NOT by
      `consolidate_materials()` (it only knows the first-pass names).
- [x] **Animations** — the 7 existing actions re-keyed (same names/semantics,
      Walk matched to 122 cm/s); 8 new ones imported but not wired up:
      A_Run, A_Sit, A_Emote_Wave/Cheer/Bow, A_Attack2H, A_Block, A_Dodge.
      Hooking them up (EValhallaAnim entries, run blend, /sit and emote commands,
      TwoHand attack cycle, Block/Dodge on combat events) is a gameplay task.
- `character_import.py`: `_fix_redirectors` survives UE 5.8,
  `build_material_instances` uses M_ValhallaPBR, `ANIMATIONS` has 15 entries.
- Known: tabard, robe skirt and cloak are stiff (no cloth sim); plate reads dark
  in the grass light; hair clumps look scaly close up; no LODs.
- Review shots: `Saved/ArtReview/wave2/` (`stageA_*`, `stageB_*`, `stageC_*`).

## Phase 14b — B-15 Wave 2 on the MetaHuman body (2026-09-23)

Kevin replaced the Fable 5.1 body with a MetaHuman base (Paragon Gideon was tried and
archived). The Wave 2 art was then redone for it; everything the Fable body used stays
in place for `valhalla.Visual.BodyProfile 0`.

- [x] **Body** — `MHC_ValhallaBase` (MetaHumanCharacter plugin, Optimized/Medium),
      in underwear, drawn at 122/180.3. The face follows the body by leader pose.
      Content lives in `/Game/Valhalla/Characters/MetaHuman/`.
- [x] **Animations** — idle, walk and jog were retargeted from the UE5 Mannequin
      (IK_Manny -> IK_MH_ValhallaBase; the Mannequins are archived outside the repo).
      Hand-keyed in `Tools/anim_authoring`:
      - stance poses (grip R / L, shield arm) and per-weapon attacks
      - bow draw-hold-release (skill field `castAnimation: "bow"`)
      - open-hand and staff casts
      - A-030: sit, wave, cheer, bow, 2H chop, block, dodge (`author_extra.py`).
      Hit reacts play on an additive layer.
- [x] **Armour (16) and hair (2)** — re-fitted by script to the MetaHuman.
      - Scripts: `Blender assets/scripts/wave2mh_base.py`, `wave2mh_armour.py`,
        `wave2mh_hair.py`, `wave2mh_review.py`.
      - Source files: `Import/Characters/MetaHuman/{Equipment,Hair}/*.fbx` and
        `Blender assets/Characters/valhalla_mh_equipment.blend`.
      - Assets: `/Game/Valhalla/Characters/MetaHuman/{Equipment,Hair}`, imported
        onto `metahuman_base_skel` by `Saved/ClaudeOps/mh_equipment_import.py`.
      - Method: same designs as the Fable pieces. Heights go through `Z()`
        (landmark to landmark), offsets through `O()`.
      - Cloth `drape()`s over the body's outer envelope; hands, feet and hips are
        shells of the MetaHuman's own skin.
      - Weights are transferred from the MetaHuman body and face; the helm and hat
        are rigid on `head`.
      - 868–2,960 tris per piece; hair 4.3k / 5.0k.
      - SK_Hood_Bald_Cap is not needed (the MetaHuman head is its own scalp).
- [x] **A-030 wired** —
      - Chat `/sit` (toggles), `/stand`, `/wave`, `/cheer`, `/bow` (`ValhallaChat::Parse` →
        `ServerEmote`; also parsed server-side in `TryHandleChatCommand`).
      - `AValhallaCharacter::bSitting` replicates; the body holds `MH_Sit`. The server
        stands a sitter up on movement, cast, auto-attack start or death
        (`UValhallaSkillComponent`).
      - Emotes go out on `MulticastEmote` (unreliable) and are dropped when the
        character moves; refused while casting.
      - The defender of a blocked or dodged blow plays `MH_Block` / `MH_Dodge`,
        never over a swing, a cast or a sit.
      - weaponStyle `greatsword` → `MH_Attack_2H`, two-handed (hides the shield).
        No greatsword item exists yet.
      - Chat parse tests cover the verbs.
      - Sitting's regen and the casters' Meditate skill: backlog B-18.
- [x] **Fixes (2026-09-24)** —
      - Body shrank during authored clips (attacks, casts, block, dodge, emotes, sit):
        they carried `metahuman_base_skel`'s smaller bone lengths with the body mesh as
        retarget source. Retarget source cleared (`ue_io.write_anim` now does this), so
        the body maps them to its own proportions like the locomotion; `MH_Sit`
        re-grounded (`Tools/anim_authoring/sit_ground.py`).
      - Enter didn't reopen chat after sending a line until the screen was clicked: the
        text box cleared keyboard focus after commit, and the hand-back went to Slate's
        "game viewport", which in a two-client PIE is the other window. `ChatInput` no
        longer clears focus on commit, and `CloseChat` focuses this player's own
        viewport (`GetWorld()->GetGameViewport()`). Checked with real key presses in
        both PIE clients.
- [x] **Code** —
      - `UValhallaVisuals::SkinnedArtRoot / EquipmentMeshPath / HairMeshPath` pick
        the active body's folder.
      - `PieceForActiveBody` swaps a piece authored for the other body for its
        same-named rebuild. NPC Blueprints and spawner previews that still name
        `Characters/Equipment/SK_*` get the MetaHuman piece.
- Before using the round trip again, know this: Blender FBX (armature object `root`,
  primary Y / secondary X) back onto `metahuman_base_skel` keeps every skinned bone's
  reference pose. That matters because a leader-pose follower skins against its
  own reference pose. Checked with `Saved/ClaudeOps/mh_refcheck.py`.
- Hair vertex colour must be exported LINEAR: `M_Hair` multiplies it in raw.
- Known: capes and skirts are stiff, with no cloth sim. The ranger's shoulder cape
  reads as a wide collar at rest. The robe skirt stretches between the legs on a
  wide stride.   No LODs on the pieces.
- Review shots: `Saved/ArtReview/wave2mh/` (`pie1_*`, `sheet_*`).

## Phase 15 — Hosting a test for other people (2026-09-23)

- [x] **Packaged clients use the public address.** New `PublicBackendUrl`
      (`https://184.96.133.165`) and `PublicGameServerAddress`
      (`184.96.133.165:7777`) apply only in a cooked non-server build
      (`UValhallaDataSettings::IsPackagedClient`); the editor, PIE and servers
      keep the loopback `BackendUrl` / `GameServerAddress`.
      `-ValhallaBackendUrl=` and `-ValhallaGameServer=` override both.
- [x] **The server secret no longer ships with the client.** `ServerSecret` is
      gone from `DefaultGame.ini` (every ini is packaged). The game server
      resolves it with `GetServerSecret()`: `-ValhallaServerSecret=`, env
      `VALHALLA_SERVER_SECRET`, `secrets.local.env` at the repo root, the legacy
      ini value (warns), then the dev default in editor builds only. Always
      empty in a packaged client.
- [x] **Backend production secrets.** `secrets.local.env` (gitignored) holds
      `NODE_ENV=production`, `HOST=127.0.0.1`, `JWT_SECRET` and
      `VALHALLA_SERVER_SECRET`; `server/src/loadEnv.ts` and
      `editor/src/loadEnv.ts` read it. Production refuses to start without a
      real `JWT_SECRET` and disables the internal routes for the dev secret. The
      editor API server binds 127.0.0.1. Smoke 89/89 in both modes.
- [x] **HTTPS for logins.** Caddy (`deploy/Caddyfile`) terminates TLS on 443
      with a Let's Encrypt IP certificate (6-day `shortlived` profile) and
      proxies to the loopback backend. `deploy/start-*.bat` and
      `deploy/README.md` cover the session; the game server runs as
      `UnrealEditor.exe -server` (a `Valhalla2Server` build needs a
      source-built engine).
- [ ] Not verified yet: a packaged client end to end (TLS to the IP cert
      through UE's bundled CA list, travel to the public game server).
- [ ] The game connection (UDP 7777) is unencrypted and carries the JWT in the
      join URL. (The `Join request` log line is trimmed since B-04.)
- Going live: [deploy/GOING_LIVE.md](../deploy/GOING_LIVE.md) (B-04).

## B-05 — hand-edited level protection (2026-09-23)

- [x] **Marker.** `maps/handedited.json` (next to `overlays-2.0/`) lists the
      hand-edited levels (`L_World`, `Zones/L_Grasslands`, `Zones/L_Desert`)
      and overlays (`grasslands`, `desert`). Edit it to protect or release one.
- [x] **The rebuild tools refuse them.** `build_world_levels` /
      `build_world.build_all`, `build_grasslands.build` / `build_desert.build`
      (refuse when the open level is marked), `ZoneBuilder.write_overlay` and
      `npc_setup.migrate_overlay_spawns` skip every marked level/overlay, name
      it in `skipped` / `message`, and still build anything unmarked (a new
      zone). With everything marked the MCP call changes nothing: verified by
      `.umap` mtimes and overlay md5s before/after.
- [x] **To force** (retired by B-19; no longer exists): `build_world_levels(force=True)` (or `build(force=True)`).
      Only when Kevin asks for his hand edits to be discarded. It first copies
      the old `.umap` / `_BuiltData.uasset` / overlay JSONs to
      `Saved/LevelBackups/<YYYYMMDD-HHMMSS>/`, paths relative to the repo root,
      and logs the folder. Logic and self-check:
      `valhalla_tools/level_protection.py` (`self_check()`, 13 checks).

## B-14 — Phase 9 testing follow-ups (2026-09-23)

Run from `L_FrontEnd`, PIE Standalone, separate server, one process, 2 clients,
`valhalla.AutoLogin` with throwaway accounts `b14t_2140_a` / `b14t_2140_b`
(characters Bwar2140 / Bwiz2140, deleted afterwards; the backend has no route
to delete users). Admin calls went through the editor's `/api/admin/*` proxy,
the same routes the Live Dashboard uses.

- [ ] **Right-drag orbit — hand check for Kevin.** In code: pitch clamped to
      `CameraPitchMin = -80` .. `CameraPitchMax = -15` degrees
      (`AValhallaCharacter::AddCameraOrbit`); `BeginCameraOrbit` stores the
      cursor position and hides it, `EndCameraOrbit` shows it again and
      `SetMouseLocation`s it back to where the drag started.
- [x] **Ban a logged-in real account.** Fix first: a kick's reason never
      reached the player (the engine's `ClientWasKicked` is empty, and a
      banned auto-login fell through to "register" and hid the ban). Now
      `AValhallaPlayerController::ClientWasKicked` keeps the sentence in
      `UValhallaBackendSubsystem` (`SetDisconnectNotice`), the front end shows
      it on the login screen after the disconnect and skips the auto-login, and
      `AutoLogin` reports a 403 login instead of registering
      (`FValhallaAuthSession::HttpStatus`). Result, `player-action ban`, 1440
      min, reason "B-14 ban test (throwaway account)": `kicked by the server
      (Bwar2140): This account is suspended until 2026-09-25 02:14 UTC. Reason:
      B-14 ban test (throwaway account)`, the same text on the client's login
      screen; login 403 with it (HTTP and a fresh PIE: `AutoLogin stopped: This
      account is suspended until …`); `account-action unban` → `login ok
      'b14t_2140_a'` and in-world again.
- [x] **Two real clients through the front end.** `DebugListActors` on each
      client lists both characters; `/state` shows both accounts (userId 15,
      16); `reload-data` → `data version 1 from the server: client reload ok`
      twice; portal to the desert and back for both (`zoneChange … grasslands
      -> desert`, `desert -> grasslands`) with `saved char=39/40 (zone
      change)` and `(admin)`.
- [x] **Level streaming.** Fresh server: grasslands 18 NPCs (18/18 spawn
      points, all `L_Grasslands_Gameplay`), desert 21 (21/21,
      `L_Desert_Gameplay`), 39 in total.
- [x] **Slow automated tests.** Editor Preferences → Performance → "Use Less
      CPU when in Background" is now off. `run_valhalla_tests` turns it off for
      the run if it is on and restores it when the log says the queue is empty.
      Full `Valhalla.` suite from a background editor: 619 s before (600 s of
      `FWaitForInteractiveFrameRate` at 3 fps, then "Giving up"), 11 s after
      through the tool with the throttle on (wait passed at 51 fps in 5 s),
      0.6 s through `RunTestsByFilter "StartsWith:Valhalla."`. 26/27:
      `Valhalla.Core.Data.MeshIdFallback` fails on items.json (iron_dagger and
      bone_totem now author a meshId) — test data, not this change.
- [x] **`NetCullDistanceSquared`** is set through `SetNetCullDistanceSquared()`
      in the character, NPC, loot bag and spell projectile constructors (all
      four direct writes in `Source/`). Rebuild: no C4996 or other compiler
      warnings.
- [x] "Testing multiplayer in PIE" note in `deploy/README.md`.
- Known: `FrontEndClientCounter` is static, so a second PIE run deals the
      `valhalla.AutoLogin` entries in the other order (client 3 got entry 1).
      Harmless for two throwaway accounts.

## B-19 — zone rebuild tools retired; `scaffold_zone` for new zones (2026-09-23)

`L_World`, `L_Grasslands` and `L_Desert` are hand-edited, so the from-scratch
builders were only a way to destroy them. Kevin's call: keep the generator as
a scaffold for new zones only.

- [x] **Retired.** The MCP tool `ValhallaLevelTools.build_world_levels` is
      gone, and with it every `force` path. `build_world.build_all()` refuses
      unconditionally; `build_world._open_empty_level` refuses any level that
      exists (it no longer empties one); `build_grasslands.build()` /
      `build_desert.build()` refuse their own level and any marked level, and
      otherwise only lay the old layout out under a new `zone_id` into a new
      empty level. The scripts stay on disk as history.
- [x] **New zone:** `ValhallaLevelTools.scaffold_zone(zone_id, theme="grassland",
      size_tiles=64)` (`valhalla_tools/scaffold_zone.py`, built on
      `build_zone.ZoneBuilder`). Themes `grassland`, `desert`, `town` (cave
      later); 8–128 tiles. Creates `Zones/L_<ZoneId>` (theme floor, zone
      volume, fog bounds, four tagged player starts), an empty
      `Zones/L_<ZoneId>_Gameplay` for NPC Spawn Points, and
      `maps/overlays-2.0/<zone_id>.json`; registers each in
      `maps/handedited.json` as soon as it exists; reopens the level that was
      open. Refuses (creates nothing) if either level, the overlay or a marker
      entry exists, or a map is unsaved. No `force`.
- [x] **Hand steps after a scaffold:** open `L_World`, Levels panel, add
      `L_<ZoneId>` and `L_<ZoneId>_Gameplay` as always-loaded streaming
      sub-levels at one offset clear of the other zones (grasslands X=0,
      desert X=+40000 cm), save; then a `zones.json` entry and portals.
- [x] **Recovering an old layout:** git history for the `.umap`;
      `Valhalla2/Saved/LevelBackups/<timestamp>/` (B-05-era copies); or
      `build_grasslands.build(zone_id="grasslands_v1")` /
      `build_desert.build(zone_id=...)` with a new empty level open.
- [x] **Marker** now also lists `L_Grasslands_Gameplay` / `L_Desert_Gameplay`.
      `level_protection.register()` adds entries; nothing removes them (hand
      edit only). Backups kept; no tool calls them now. `self_check()` 15/15.
      `npc_setup.migrate_overlay_spawns(force)` is unchanged (overlay
      migration, not a rebuild).
- [x] **Verified:** `scaffold_zone("b19test", "grassland", 16)` created both
      levels and the overlay and registered them; a second call refused with
      all six reasons; test levels, overlay and marker entries then removed.
      `Valhalla.` tests 26/27 (only the known `MeshIdFallback`).

## B-04 — Production secrets and exposure (2026-09-23)

Checklist for a session with outside players: [deploy/GOING_LIVE.md](../deploy/GOING_LIVE.md).

- [x] **CORS allow-list.** Backend (`server/src/middleware/cors.ts`):
      `CORS_ORIGINS`, default `http://localhost:5180,http://127.0.0.1:5180`.
      An allowed origin gets itself echoed with credentials; a foreign Origin
      gets 403 (preflight or not); no Origin (UE client/server) passes.
      `X-Server-Secret` is no longer offered to browsers. The web editor's API
      server (`editor/src/server.ts`, which writes data files and forwards
      admin calls *with the secret*) was `cors()` = any origin; it now uses the
      same list. Admin API: `AdminApiAllowedOrigins` in `UValhallaDataSettings`
      (default the two editor origins) replaces `*`; foreign Origin 403, CORS
      headers added by the route wrapper for allowed ones.
- [x] **Rate limits** (`server/src/middleware/rateLimit.ts`, in-memory sliding
      window, no new dependency): login and register 10/min per IP, `POST
      /api/characters` 5/min, 429 + `Retry-After`. `X-Forwarded-For` (last
      entry) is believed only from loopback (Caddy). Health, data and internal
      routes unlimited. `RATE_LIMIT_WINDOW_MS` exists for the smoke test.
- [x] **Servers refuse the dev secret.** `AValhallaGameMode::InitGame`: a
      dedicated server outside the editor (`UnrealEditor.exe -server`
      included: `GIsEditor` is false there) or a listen server in a non-editor
      build that resolves `dev-server-secret` **or no secret** logs `FATAL: …`
      and calls `FPlatformMisc::RequestExitWithStatus(false, 1)`; the admin API
      is not started and PreLogin refuses joins until the exit. Editor and PIE
      keep the dev default. Resolution order unchanged, now a pure
      `UValhallaDataSettings::ResolveServerSecret` covered by
      `Valhalla.Game.Security.ServerSecret`.
- [x] **Tokens in logs.** `Join request` now logs `RedactJoinOptions(...)`
      (every `token=` cut to 8 characters); the other Valhalla lines already
      used `RedactToken`. Not fixable from game code: the engine's `LogNet:
      Browse` (client) and `LogNet: Login request` (server) lines print the full
      URL. Backend: no `console.*` prints a token or password.
- [x] **Client secret audit:** `python Tools/audit_client_secrets.py [folder]`
      (dev literal, `X-Server-Secret`, and the real secret values read from
      `secrets.local.env` and never printed). `Valhalla2/Config` +
      `shared/data` (no staged `Content/Data`, no `Saved/StagedBuilds` exist
      yet): 11 files, 0 findings. Packaged client not audited: none exists.
- [x] **Tests:** `server/scripts/smoke-security.ts` 30/30;
      `smoke-internal.ts` 89/89 (its `spawn('npx')` failed on Windows with
      ENOENT; it now runs `node --import tsx`).
- [ ] **Old account database in the git history — Kevin to decide.**
      `server/valhalla.db` (and a copy under
      `node_modules/@valhalla/.server-ryxSsj3C/`) was committed from
      `8230e4cd` (2026-02-16) to `3630d752` / `361b5dc4`: 14 distinct versions,
      reachable only from `origin/archive` and tag `archive/1.0-final` (not
      `dev`/`main`). **The GitHub repo `the1382lab-netizen/valhalla` is public.**
      Contents: at most 14 users (`alpha`, `beta`, `insidious`,
      `insidious0`–`3`, `salvo`, `probe_tmp`, `pt_0115690_1`–`4`, `ue_bantest`);
      all 58 hashes are bcrypt `$2b$10$`; no plaintext passwords; the names are
      handles and test accounts, not real people's names. **All 14 still exist
      in today's `server/valhalla.db` with the same hashes**, so their password
      hashes are public now.
      - Option A, leave it: cost nothing. Risk: offline guessing of weak
        passwords at bcrypt cost 10. Mitigate either way by deleting those
        accounts or changing their passwords before going live (and never
        reusing a real password on them).
      - Option B, purge: `git filter-repo --path server/valhalla.db --path
        node_modules/@valhalla/.server-ryxSsj3C/valhalla.db --invert-paths` on
        a fresh mirror clone, then force-push every branch and the tag. Cost:
        every commit hash from 2026-02-16 onward changes on `archive` and the
        tag (and on `dev`/`main` only if they share that history, which they
        do not); every clone (this one included, with its uncommitted work)
        must re-clone or hard-reset; old hashes in PLAN.md and docs go stale;
        GitHub keeps the old objects reachable through caches and forks until
        support purges them, so the hashes must be treated as leaked anyway.

## Phase 16 — B-15 art overhaul, Wave 3: the Desert (2026-09-24)

Every Desert mesh and the portal marker rebuilt in Blender by script
(`Blender assets/scripts/wave3_desert.py`, `.blend` in `Blender assets/Environment/valhalla_wave3_desert.blend`),
exported to the same `Import/` glb and re-imported in place with
`import_kit.reimport_meshes` (`Saved/ClaudeOps/w3_import_ue.py`). No level edits.

- [x] **Textures** — CC0: ambientCG Ground080 `DesertSand`, Ground097 `SandRipples`;
      Poly Haven dry_ground_01 `CrackedEarth`, sandstone_blocks_05 `SandstoneBlocks`,
      old_sandstone_02 `SandstoneRock`, palm_bark `PalmBark`. Procedural:
      `PalmFrond` (`wave3_palm_texture.py`), `Cactus` (`wave3_cactus_texture.py`).
      Poly Haven sets now come from `wave3_polyhaven.py` (plain Python, `nor_dx` + `arm`
      as shipped); all recorded in `Import/Textures/texture_sets.json`.
- [x] **Ground** — sand A/B/C and cracked earth on `M_ValhallaGroundBlend`
      (`MI_SandBlend`, `MI_CrackedBlend`), vertex-colour patches faded out at every
      edge; first-pass tops kept (7.3 / 6.2 cm). The tile skirt has its own rim
      vertices, so smooth top normals no longer bend at the edges.
- [x] **Dunes** — every dune in L_Desert is an actor on top of a sand tile (scaled
      1.1–1.6, diagonal chains), so `SM_Dune` is now only a round mound 0.96 m across
      with its rim buried; chains merge into ridges.
- [x] **Walls** — ashlar sandstone, bounds identical, VisionBlocker kept.
- [x] **Nature** — boulder (bedded sandstone), saguaro and barrel cacti, date palm
      (`MI_PalmFrond` on `M_ValhallaFoliage`).
- [x] **Portal marker** — rune-stone obelisk on an octagonal plinth; `M_PortalGlow`
      toned down (EmissiveStrength 4 → 1.6).
- Known: palm wind not done; wall block joints don't line up across module seams;
  the ruins' floor inside the Desert walls is the grassland stone floor.
- Review shots: `Saved/ArtReview/wave3/` (`before_*`, `v*_*`, `final_*`).

## Phase 17 — B-15 art overhaul, Wave 4: UI art (2026-09-24)

- [x] **Item icons (A-059)** — `Blender assets/scripts/wave4_icons.py`: an icon
      studio (ortho camera, warm key / cool rim / fill, transparent film) that
      imports the model the game uses (weapon/shield glb, MetaHuman armour FBX), rebuilds
      its materials from `Import/UI/icon_materials.json` (written by
      `valhalla_tools/export_icon_materials.py` from the live MIs), poses it by
      category and renders 256 px; `wave4_icon_finish.py` (Pillow) makes the 128 px PNG
      with outline and shadow. 30 icons = every item in items.json; 8 new props
      (rings, potions, rat tail, coins) modelled in the studio; items.json
      `inventoryIcon` set for the dagger, totem and those 6.
- [x] **Skill icons (A-060)** — `wave4_skill_icons.py`: SDF emblems shaded as metal
      or glow on the skill's `iconColor`; 41 in `Import/UI/Icons/Skills/<id>.png`.
      `SetSkillIcon(..., FindSkillIcon(id))`; no icon → the old code tile.
- [x] **HUD frames (A-061)** — `wave4_ui_frames.py`: nine-slice `T_UI_Panel`,
      `T_UI_Slot`, `T_UI_Button`, `T_UI_BarFrame`, `T_UI_BarFill` in `Import/UI/Frames`.
      `MakePanel` (not the chat panel), `MakeCell` (`SetFrameArt`), `MakeButton`,
      `MakeBar` and the combat log use them via `ValhallaHudArt::BoxBrush`.
      `FindUiTexture` / `LoadUiTexture` load the imported asset, else the PNG on disk.
- [x] **Import** — `import_ui_icons.py` now handles Icons, Icons/Skills and Frames,
      re-imports a PNG newer than its asset, and sets bilinear + mips on 128 px icons
      (nearest, no mips on the 32 px 1.0 icons).
- Known: 46 of the 1.0 item icons belong to no current item and stay pixel art;
  panels are ~20 px wider with the trim (layout positions unchanged).
- Review shots: `Saved/ArtReview/wave4/`.

## Phase 18 — B-15 art overhaul, Wave 5: more environment (2026-09-24)

New assets only; nothing in the hand-edited levels changed. Built by
`Blender assets/scripts/wave5_kit.py` (`build_set(name)` / `build_all()`, review
renders via `wave5_review.py`), exported to `Import/Environment/<Set>/`, imported to
`/Game/Valhalla/Environment/<Set>/` by `import_kit.reimport_meshes` (81 meshes, all
slots bound, VB_ pieces on the VisionBlocker profile).

- [x] **Tavern (A-064)** — 64 cm wall / window / door (128) / corner, stone ground
      floor with a jettied timber upper storey (3.6 m), slate gable roof (128 x 128),
      chimney, hanging sign, plank floor.
- [x] **Temple (A-065)** — limestone wall and pilaster wall (2.4 m), fluted column,
      altar, steps, mosaic floor, brazier, a hooded statue.
- [x] **Keep (A-066)** — 50 cm curtain walls in 1.28 m modules (plain, arrow slit),
      corner pier, round tower (2.16 m, 4.8 m), gatehouse with raised portcullis
      (SM_, walk-through), banner. Outside = +Y in Unreal.
- [x] **Ruins (A-067)** — block-laid broken walls (ragged tops, every block supported),
      corner, low wall, snapped and fallen columns, half arch, broken statue, rubble.
- [x] **Bridge and river bank (A-068)** — humped stone bridge (3.84 m), plank footbridge,
      bank edge strip and corner (land +Y, over the tile line), reeds, stepping stones.
- [x] **Cave (A-069)** — rock walls 1.28 x 0.64 x 2.4 m (and 1.4 m) with ledged strata,
      pillar to hide joints / turn corners, ceiling lip (overhangs 0.9 m, no roof for the
      top-down camera), floor tile, stalagmites, stalactites, glowing crystals, rubble.
- [x] **Interior props (A-071)** — bar, table, chair, bench, bed, shelf, books, candle,
      chest, keg, fireplace, table clutter.
- [x] **Camp props (A-072)** — A-frame and bell tents, campfire, cooking fire, bedroll,
      weapon rack, log seat, supply pile.
- [x] **Foliage variety (A-073)** — two bushes, grass tuft, two flower patches, fern,
      dead tree, stump, fallen log. Card materials on M_ValhallaFoliage.
- [x] **Desert variety (A-074)** — bones, horned skull, sandstone ruin wall and column,
      nomad awning, pottery.
- Textures: 12 Poly Haven CC0 sets (SlateRoof, KeepStone, TempleStone, Marble,
  TempleFloor, RuinStone, MossyRock, OakPlanks, DarkPlanks, LogBark, CaveRock,
  CaveFloor) + 5 procedural foliage cards (`wave5_foliage_textures.py`); flat MIs
  MI_Crystal (emissive), MI_Wax, MI_Pottery, MI_Bread. Script: `Saved/ClaudeOps/w5_import_ue.py`.
- Cards, bank strips, banner, sign, ceiling lip, stalactites and table dressing are NoCollision.
- Review level: `/Game/Valhalla/Maps/Dev/L_ArtReview_Wave5` (L_World lighting LOOK, every
  piece in vignettes; `Saved/ClaudeOps/w5_showcase.py` rebuilds it). Shots: `Saved/ArtReview/wave5/`
  (`vp_*.png` from the editor viewport; the SceneCapture shots render the water black).
- [x] **Moving fire** — campfires, cooking fire, braziers, fireplace, candles and the altar
      candles carry their flames as five crossed cards (`wave5_kit.flame`) on `MI_Fire`
      (`M_ValhallaFire`, `valhalla_tools/build_fire.py`): unlit, masked + dithered, a teardrop
      eaten and bent by rising 3D value noise in object space (offset by the actor's position
      so neighbours differ). Fire sections cast no shadow and do not collide.
      `valhalla_tools/fire_lights.add_all()` attaches a movable warm PointLight to each fire
      prop in the open level with a flicker light function (`MI_FireFlicker_A/B/C`, three
      phases); tagged `ValhallaFireLight`, `remove_all()` undoes it. Run it after placing fires,
      like `lamp_lights`. Review GIFs: `Saved/ArtReview/wave5/fire/`.
- [x] **Walk-through pieces** — `import_kit.WALKABLE` (SM_KeepGate, SM_RuinArch, both
      bridges, SM_TempleSteps) get complex-as-simple collision on import; the importer's
      simple collision had made the gate and bridges solid boxes. Stone bridge widened to 80 cm
      between parapets (capsule is 60). Checked with 30 x 60 capsule sweeps on the Pawn profile
      (`Saved/ClaudeOps/w5_walk_test.py`): gate, arch and both bridges clear, keep and tavern
      walls still block. The gate has no closed state yet.
- Known: wall pieces are box-UV'd per piece, so the stone shows a faint seam every
  64 cm; cave walls show a straight joint where two pieces meet (use a pillar).

## B-07 — HUD → Widget Blueprint, step 1: stats, XP, keybinds (2026-09-24)

Groundwork for moving the code-built HUD (`UValhallaGameHUDWidget`) to Widget
Blueprints: give the client the numbers a character sheet needs, and tidy the
panel keys. The code-built panel shows them in the meantime.

- [x] **Owner-only resolved stats.** `AValhallaPlayerState::ClientStats`
      (`FValhallaResolvedStats`, `Replicated`, `COND_OwnerOnly`, private,
      `BlueprintReadOnly` via `AllowPrivateAccess`; C++ reads
      `GetClientStats()`). The server copies `Stats` into it at the end of
      `RecomputeStats`, in `InitializeFromClass` and in `CopyProperties`, and
      never reads it back; `Stats` stays private and unreplicated. Class
      comment updated: the owner may know its own dodge/block/crit, nobody
      else may.
- [x] **XP to next level.** `GetXpToNextLevel()` (`XpRequiredForLevel(Level)`,
      `INDEX_NONE` at the cap) and `GetXpFraction()` (Xp / needed, clamped
      0–1, 1 at the cap), both `BlueprintPure` over the replicated Level/Xp,
      plus static `XpToNextLevelFor` / `XpFractionFor` for tests.
- [x] **Keybinds.** B no longer opens the panel: `IA_Character` removed
      (member, action, mapping, binding); I alone opens the combined
      Character + Inventory panel, titled "Character  (I)". Input log line
      and comments updated.
- [x] **Character panel (interim, code-built).** The "Gear:" bonus sum is
      replaced by the resolved stats: "Level n   XP x / y (z%)" (or "XP
      max") over a thin XP bar (`MakeBar`, from `GetXpFraction`); HP; Mana or
      Energy; STR STA DEX INT WIS; Phys Def, Phys Resist, Spell Resist (flat
      ratings, not percentages); Block / Dodge / Crit % and crit multiplier;
      main-hand weapon damage min–max and swing (base and with DEX), "—"
      with no weapon; Speed; Vision. Item tooltips add Physical defense and
      Block / Dodge / Crit chance / Crit damage as percentages.
- [x] **Block-rate data.** Checked: `shared/data/items.json` already holds
      `blockRating` 0.03 / 0.05 (fixed when the data moved into this repo),
      and every other rate bonus (`dodgeRating` 0.01) is a decimal; no data
      change needed. The stale "known data bug" note on
      `UValhallaInventoryLibrary::ComputeStatsWithEquipment` is rewritten.
      The item editor's steps for rate fields are 0.01; the stats fixtures
      (`Tools/fixtures/generate.ts`) do not read items.json, so nothing to
      regenerate.
- [x] **Tests:** `Valhalla.Game.PlayerState.XpToNextLevel`,
      `Valhalla.Game.PlayerState.ClientStatsOwnerOnly` (reads the CDO's
      lifetime props) and `Valhalla.Core.Data.StatBonusRates` (every
      `critChance` / `critDamage` / `blockRating` / `dodgeRating` bonus in
      [0, 1]). Not yet built or run: the new replicated UPROPERTY needs a
      full rebuild with the editor closed (not Live Coding).
- [x] **Step 2 — groundwork (2026-09-24):** the C++ HUD can be laid out by a
      Widget Blueprint child while C++ keeps filling and running it; with no
      Blueprint nothing changes. Not yet built or run (the editor was open
      with other work); Kevin rebuilds.
      - `UValhallaHUDSlotWidget` and the new `UValhallaHUDBarWidget` are
        `Blueprintable` with `BindWidgetOptional` parts (cell: `Sizer`
        `Frame` `Fill` `Stack` `Icon` `SkillTile` `Abbrev` `CooldownSizer`
        `CooldownFill` `CooldownBar` `CooldownText` `KeyLabel` `QuantityText`
        `SelectionRing`; bar: `Frame` `Background` `Sizer` `FillSizer` `Fill`
        `FillBar` `OverlaySizer` `OverlayFill` `OverlayBar` `Label`). No
        designer tree: they build themselves in code as before (in
        `NativeOnInitialized`; PreConstruct runs after the Slate tree is
        made). A designer tree keeps its look; parts it lacks are hidden
        stand-ins, so `SetIcon` / `SetCooldown` / `SetFraction` / `SetLabel`
        ... work either way. Every `MakeBar` (HP, mana/energy, cast, target,
        party, XP, and the pooled nameplates) is now a bar widget; `FBar` is
        gone.
      - `UValhallaGameHUDWidget` binds designer panels by name: required
        (`BindWidget`) `VitalsPanel` `HpBar` `ManaBar` `ActionBarRow`
        `ChatPanel` `ChatScroll` `ChatInput`; optional the rest (target,
        party + invite, combat log, loot, skills, character / equipment / XP /
        stats, inventory + `InventoryGrid` (a Uniform Grid Panel), tooltip,
        drop confirm, death). `SlotWidgetClass` / `BarWidgetClass` pick the
        cell and bar classes C++ makes. `BuildAll` is `BindDesignerPanels` +
        `PopulateDesignerPanels` for a complete designer tree (a subclass, a
        Canvas Panel root, every required widget), else the code build; an
        incomplete tree logs what is missing and falls back to code. The
        code build assigns the same members, and the cells, rows and tokens
        are made by shared `Add*` helpers for both paths. Designer buttons
        are `UValhallaHUDButton` with `Action` (now editable) set; the combat
        log's right-click filter menu and the nameplate / floater layer stay
        C++'s.
      - Click-through: after binding, layout panels and panels with nothing
        clickable in them become SelfHitTestInvisible, whatever the designer
        left; a border holding buttons / inputs / cells keeps eating clicks.
      - `UValhallaUISettings` (Project Settings > Valhalla > UI > Game HUD
        Class, empty = the code HUD) and `valhalla.HudClass <path>` for the
        next HUD; `AValhallaHUD::BeginPlay` creates that class.
      - Editor tools (`ValhallaUITools`, `valhalla_tools/hud_blueprints.py`):
        `create_hud_blueprints` (WBP_HUDSlot / WBP_HUDBar / WBP_GameHUD in
        `/Game/Valhalla/UI/HUD`, check-before-create, empty),
        `layout_hud_from_config` (WBP_GameHUD's tree from ui-config.json with
        the binding names), `describe_hud_blueprint`. Python drives the
        engine's `UMGToolSet` (`AddWidget`, `RenameWidget`,
        `CompileWidgetBlueprint`, ...) through `call_method`: 5.8's Python
        cannot touch `UWidgetBlueprint::WidgetTree` directly, and no C++
        editor module was needed.
      - Test `Valhalla.Game.UI.HudBlueprintGroundwork`: the setting defaults
        empty and resolves to the C++ HUD, the C++ class never lays out from
        a Blueprint, every panel name is a widget property with the right
        Bind meta and type, the bar clamps its fraction.
- [x] **Step 3 — Blueprints (2026-09-24):** the three Widget Blueprints
      exist, are laid out, compile clean and drive the HUD in PIE through
      `valhalla.HudClass` (the project setting is unchanged: step 4).
      - Assets in `/Game/Valhalla/UI/HUD`: `WBP_GameHUD` (99 widgets, Canvas
        Panel root `HUDRoot`, all 7 required and all 36 optional bindings,
        no fallbacks), `WBP_HUDBar` (Frame > Background > Sizer > Layers:
        FillSizer/Fill, OverlaySizer/OverlayFill, Label), `WBP_HUDSlot`
        (Sizer > Frame > Fill > Stack: Icon, SkillTile, Abbrev,
        CooldownSizer/CooldownFill, CooldownText, KeyLabel, QuantityText).
        Made with `ValhallaUITools.create_hud_blueprints` and
        `layout_hud_from_config` (now lays out all three, by parent class);
        `CompileWidgetBlueprint` true for each, no compile errors in the log
        after the final build.
      - Layout: every panel at the code build's canvas anchor, alignment and
        offset (vitals bottom left, action bar and cast bar bottom centre,
        target top centre, party (12,12), invite, combat log 320x200 top
        right, chat right of the vitals, loot (220,-40), skills (16,-40) 300
        wide, character + inventory centred, tooltip on the root canvas,
        240 max), from `ui-config.json`. Bars are `WBP_HUDBar` instances in a
        Size Box of the code bar's framed size, `BarWidth` = the fill width.
        The B-15 Wave 4 frame art (`T_UI_Panel` / `_Button` / `_BarFrame` /
        `_BarFill` / `_Slot`) is baked into the designer brushes with the
        code's margins and paddings (C++ gives a designer tree none); flat
        ui-config colours if those textures are not imported.
      - Character panel (new): "Character  (I)", `EquipmentPanel` (C++ adds
        the 9 rows), `CharacterLevel`, a 200 x 10 `XpBar`, `CharacterStats`
        (left-aligned, wrapping) in a box with room for 16 lines at 7 pt.
      - PIE (L_World, offline copy, 2 clients) with `valhalla.HudClass
        /Game/Valhalla/UI/HUD/WBP_GameHUD.WBP_GameHUD_C`: log `game HUD:
        layout from the Widget Blueprint WBP_GameHUD_C.` and `HUD built
        (WBP_GameHUD_C) ...` for both clients, no `has no '...' widget`
        warnings. Vitals, action bar, combat log shown; the I and K keys
        (SlateInspector key presses) open Character + Inventory with the
        stats and XP bar, and Skills; Enter opens chat, a typed line is sent
        (`chat send [general] ...`) and fades. Esc in PIE stops the session
        (editor binding), so close was checked with `valhalla.UI close`
        (`CloseTopmost`). Screenshots, code HUD and Blueprint HUD side by
        side: `Saved/ClaudeOps/b07/`.
      - Not wired: WBP_GameHUD's `SlotWidgetClass` / `BarWidgetClass` stay
        the C++ classes. `Setup` does not size a designer tree, and C++ makes
        cells at 44 / 48 / 26 px and bars at 160x8 (party) and 60x4
        (nameplates), so one designer size would be wrong for most of them.
        Follow-up (C++): let `Setup` size a designer `Sizer` (and set
        `BarWidth`), then `hud_blueprints.wire_cell_classes(True, True)`.
      - Known: the designer chat keeps its 360 px width (no `TickLayout`
        narrowing), so on a viewport under ~1450 units it runs under the
        action bar, and it grows to `chat.height` rather than jumping to it.
      - Tools: `hud_blueprints.py` gains `layout_blueprint` (dispatch),
        `layout_bar`, `layout_slot`, `wire_cell_classes`, the frame-art
        brushes, and fixes (a replace now really removes the old root —
        UMGToolSet reports BindWidget-named widgets as inherited; widget-less
        GetWidgets entries are ignored; compile failures are reported, not
        raised). `ui_tools.py` reloads `hud_blueprints` on every call (takes
        effect at the next editor start). Console commands in PIE:
        `ValhallaLevelTools.run_console_command` already existed.
- [x] **Step 4 — switch-over and cleanup (2026-09-24):** the game HUD is
      WBP_GameHUD by project setting, the code-built layout and its
      `ui-config.json` knobs are gone.
      - Setting: `DefaultGame.ini` `[/Script/ValhallaGame.ValhallaUISettings]`
        `GameHUDClass=/Game/Valhalla/UI/HUD/WBP_GameHUD.WBP_GameHUD_C`, plus
        `+DirectoriesToAlwaysCook=(Path="/Game/Valhalla/UI/HUD")` (the setting
        is a soft reference the cooker does not follow). `valhalla.HudClass`
        still overrides it. Empty or unloadable: an error naming the setting,
        and the bare C++ class, which has no layout (hidden stand-ins for every
        panel; only nameplates and floating text show; chat cannot open).
      - Deleted: `BuildAll`'s code branch and `BuildVitals` `BuildActionBar`
        `BuildCastBar` `BuildTargetFrame` `BuildPartyFrame` `BuildCombatLog`
        `BuildChat` `BuildLootPanel` `BuildSkillsPane` `BuildInventoryPanel`
        `BuildTooltip` `BuildDropConfirm` `BuildDeathOverlay` and the code
        branch of `Rebuild`; `ValhallaGameHUDWidget.cpp` 482 lines out, 223 in
        (4,130 -> 3,814), the header 50 out. Kept: `BuildWorldLayer`
        (nameplates, floaters), the combat-log filter menu (`MakePanel`,
        `MakeButton`, `Place`, `MakeText` serve it), the `Add*` cell / row
        helpers, and the slot / bar widgets' code-built fallback trees.
      - Setup sizes a designer tree: `UValhallaHUDSlotWidget::Setup` sets the
        designer `Sizer` (action 44, inventory / loot 48, skills 36, equipment
        26), new `UValhallaHUDBarWidget::SetBarSize` the `Sizer` and
        `BarWidth` of the bars C++ makes (party 160x8, nameplates 60x4; the
        bars placed in WBP_GameHUD keep the designer's size).
        `hud_blueprints.wire_cell_classes(True, True)` then set WBP_GameHUD's
        `SlotWidgetClass` / `BarWidgetClass` to WBP_HUDSlot / WBP_HUDBar
        (compiled, saved).
      - Chat on the designer path: `TickLayout` narrows ChatPanel's Size Box
        to the gap beside the centred action bar, or stacks the chat above
        VitalsPanel when the gap is under 220 units (only for bottom-left
        anchored chat and vitals); the designer's position and width are read
        once. Opening the chat jumps the Size Box to its Max Desired Height
        (170), closing clears it.
      - ui-config.json trimmed to `chat.maxMessages` / `chat.visibleLines`,
        `inventory.cols` / `rows` and `nameplates` (version 1.1.0), in all four
        places (the file, `shared/src/ui-config.ts`, `FValhallaUIConfig`, the
        editor's UI Layout page). Removed: `hud` (hp / mana / energy bars,
        classText), `actionBar`, `castBar`, `deathOverlay`, chat sizes /
        paddings / colours / font, inventory slot sizes / gaps / panel dims /
        colours. What C++ still needed from them at runtime (cell sizes, HP
        high / mid / low, mana, energy, cast bar, cooldown, key label,
        highlight, label / value text, chat font and channel colours, the open
        chat's background) is now `UValhallaGameHUDWidget` "Valhalla|HUD Style"
        properties with the old values as defaults, i.e. WBP_GameHUD's Class
        Defaults. `chat.visibleLines` is now read (idle lines, was a fixed 8).
        A pre-B-07 file still parses; the old sections are ignored.
      - Editor: the UI Layout page (which wrote its own, never-read schema:
        `hpMana`, `deathScreen`, ...) is rewritten on the real schema: three
        tabs (inventory grid, chat lines, nameplates with a preview) and a note
        that the layout lives in WBP_GameHUD. `tsc --noEmit` clean in
        `shared/` and `editor/`.
      - Tests: `Valhalla.Game.UI.ConfigParse` (three sections, the old
        sections absent from the file, a partial and a pre-B-07 file),
        `HudBlueprintGroundwork` (the setting names WBP_GameHUD and resolves
        to `WBP_GameHUD_C`), `Valhalla.Core.Data.Loads` (the raw file has the
        three sections and no `hud`). Full `Valhalla.` run: all pass but the
        known `Valhalla.Core.Data.MeshIdFallback`.
      - PIE (L_World, 2 standalone clients, no console override): `game HUD
        class /Game/Valhalla/UI/HUD/WBP_GameHUD.WBP_GameHUD_C (Project
        Settings > Valhalla > UI)`, `game HUD: layout from the Widget Blueprint
        WBP_GameHUD_C.`, `HUD built (WBP_GameHUD_C) ... cells WBP_HUDSlot_C,
        bars WBP_HUDBar_C` for both clients. I (character + inventory, stats,
        XP bar, 48 px inventory and 26 px equipment slots), K (skills, 36 px),
        Enter + a typed line (`chat send [general] ...`), the chat jumping to
        full height and narrowed beside the action bar, `valhalla.ReloadUI`
        rebuilding both HUDs. Shots `Saved/ClaudeOps/b07/10_`..`14_`.
      - Not exercised: target frame, party, loot, tooltip, drop confirm and
        death overlay (the standalone clients have no server; no `valhalla.UI`
        verb reaches them there), and the chat's stack-above-the-vitals
        branch (the 646 px client still had room beside the bar). Known: the
        class line is wider than the HP bar and runs under the open chat's
        left edge (the code HUD had the same geometry).
      - Depends on another session's uncommitted B-15 Wave 4 frame textures
        (`/Game/Valhalla/UI/Frames/T_UI_*`), which the Blueprints reference;
        they are not in this commit. Until they are committed a clean checkout
        shows the Blueprints' frames without art.

## B-21 — In-game options menu and HUD customization, steps 1–6 (2026-09-24)

Kevin's decisions: settings are per character and synced through the backend
(saved when changed, loaded at login); the UI is locked by default; Escape
opens the options menu when nothing else is open; a cog button bottom right is
the other way in. Steps 1–2 are the foundation (settings model, sync, layout
apply); steps 3–5 the edit mode, the menu and live style; step 6 verified the
backend sync end to end (real account, front end, two characters).

- [x] **Settings model (ValhallaCore).** `FValhallaUserUISettings`
      (`ValhallaUserUISettings.h`, BlueprintType): `Version` 1, `UpdatedAt`
      (ISO 8601 UTC, stamped on every change), `bLocked` (true), `UiScale`
      (1, 0.5–2), `PanelOpacity` (1, 0.2–1), `Panels` (key ->
      `FValhallaPanelLayout`: AnchorMin/Max, Alignment, Position, Size (0 =
      designer's), Scale, bVisible, bSet), `Colours` (HUD Style property name
      -> colour), `ChatFontSize` / `ChatVisibleLines` (0 = default),
      `bChatTimestamps`, `LogFilters` (filter key -> shown; the HUD's own
      `TMap<FName, bool>` shape), `bShowNpcNameplates`,
      `bShowPlayerNameplates`, `bFloatingCombatText`, `NameplateFontSize`.
      Hand-written JSON (format in the header): flat object, vectors `[x, y]`,
      colours sRGB `"#rrggbbaa"`, sorted keys (stable bytes). Missing fields
      keep defaults, wrong types are skipped with a warning, values clamp, text
      that is not an object loads the defaults, unknown fields are kept and
      written back, a newer `Version` loads what it knows and keeps its number.
      `ResetSection` (All / Layout / Style / Chat / Nameplates).
- [x] **Backend.** Table `character_settings (character_id PK -> characters.id
      ON DELETE CASCADE, ui_json TEXT, updated_at TEXT)`, created with the
      other tables (`CREATE TABLE IF NOT EXISTS` is the migration).
      `GET /api/characters/:id/settings` -> `{ui, updatedAt}` (404 when nothing
      is saved, `noSettings: true`; 404 for another user's or no character),
      `PUT` `{ui}` -> `{ok, updatedAt}` (400 not an object, 413 over 64 KB),
      player JWT (charactersRouter), 60 per minute per IP for GET + PUT
      together (`settingsLimiter`). `services/SettingsService.ts`.
      `deleteCharacter` deletes the row explicitly: found while testing that
      sql.js's `export()` (every `saveToDisk`) resets `PRAGMA foreign_keys`,
      so no `ON DELETE CASCADE` in this backend fires (`character_action_bar`
      rows are left behind too; not fixed here). CORS needed nothing (PUT was
      already allowed). README section and `scripts/smoke-settings.ts`.
- [x] **Client HTTP and the session.** The JWT lived only on
      `AValhallaFrontEndController::Session`, which the ClientTravel into the
      world destroys, so the in-world client had no token. `EnterWorld` now
      hands token, user id and character id to
      `UValhallaBackendSubsystem::SetPlayerSession` (per game instance, survives
      the travel, memory only, logged redacted); the front end's BeginPlay and
      LogOut clear it. New `GetCharacterSettings` / `PutCharacterSettings`.
      Chosen over relaying through the game server (server RPC -> internal
      route with X-Server-Secret): the routes are the player's own, the backend
      checks ownership against the token, no RPC payload limits or server
      plumbing, and a token that expires (24 h) only means that session's
      changes stay in the disk cache until the next login pushes them.
- [x] **`UValhallaUserSettingsSubsystem`** (GameInstance, not on a dedicated
      server): `Get()`, `Mutate(lambda)` / `Set` (sanitize, stamp, dirty,
      broadcast `OnChanged`), debounced `Save()` 2 s after the last change
      (FTSTicker), immediate on `ResetToDefaults`, `FlushPendingSave` (HUD
      destruct), `FlushAndForget` (front end opens), `Deinitialize`.
      `LoadForCharacter(id)`: disk cache `Saved/UI/settings_<id>.json`
      (`settings_offline.json` for id 0: offline PIE) applies at once, then
      GET; newer `UpdatedAt` wins (backend on a tie), a newer or never-sent
      local copy is PUT back, unreachable / 401 keeps the cache. Cache written
      beside + moved over on every save. Character id: the backend session's
      (0 without one).
- [x] **Movable panels.** `UValhallaGameHUDWidget::GetMovablePanels()`: Vitals
      (VitalsPanel), ActionBar (ActionBarRow, moves ActionBarFrame), CastBar
      (CastBar, moves CastBarSize), TargetFrame, Party, CombatLog (flowing:
      CombatLogSize width + height), Chat (flowing: ChatSize width + max /
      open height), Loot, Skills (flowing: SkillsSize width + max height),
      Character, Inventory; scaled otherwise. Hideable: the always-on parts
      (not loot, skills, character, inventory; chat shows while typing).
- [x] **WBP_GameHUD split.** `hud_blueprints.py`: CharacterPanel and
      InventoryPanel are their own root-canvas children ((0.5,0.5) /
      (1,0.5) / (-102,-30) and (0.5,0.5) / (0,0.5) / (-94,-30), from the fixed
      widths 256 and 452 and the 8 px gap; both min height 504, the tallest
      class's character column, as the Horizontal Box made them one height).
      Re-laid out with `layout_hud_from_config(replace=True)` (98 widgets,
      compiled, saved; the cell / bar classes survived, no re-wire needed),
      re-saved with AssetTools. PIE: the character panel's left edge is at
      x 360 as the pair's was; screenshots 00 (before) and 02 (after) match.
- [x] **Layout apply.** `CaptureDesignerDefaults` (once, after
      PopulateDesignerPanels so click-through has settled visibility): each
      panel's canvas slot, render transform, Size Box values and background
      brush colours. `ApplyUserLayout` (every build and `OnChanged`): bSet
      entries' anchors, alignment, position, Size (flowing), Scale and
      visibility (`EnforceUserHiddenPanels` after the Tick code); nothing set
      puts back exactly the designer's. Chat: `ChatDesignPosition` / width /
      open height follow the applied values, and `TickLayout`'s narrow-screen
      fix-up is skipped once the player has placed the chat (it measures at
      the applied render scale otherwise). `ApplyUserStyle` is a placeholder.
      `Size` means a flowing panel's content box (not the canvas slot with
      auto-size off): the chat's idle-grows / open-jumps behaviour lives on
      that box.
- [x] **UiScale / PanelOpacity.** UiScale multiplies each movable panel's
      position and its render scale (about its alignment point): for
      point-anchored canvas children that is exactly what a DPI change does,
      without touching the project DPI curve, the front end or the editor;
      text stays sharp (Slate rasterizes at the scale), hit-testing follows
      the transform. PanelOpacity scales the background borders' brush alpha
      (the panel frame and the combat log's well; the open chat's
      background), so text and icons stay opaque.
- [x] **Console.** `valhalla.UI settings` (the JSON), `resetlayout`, `panels`
      (each movable panel's slot and drawn geometry), and dev verbs until edit
      mode: `movepanel <Key> <x> <y>`, `hidepanel` / `showpanel <Key>`,
      `uiscale <s>`, `opacity <a>`.
- [x] **Tests.** `Valhalla.Core.UISettings.RoundTrip` / `.Defaults` /
      `.ForwardVersion`; `Valhalla.Game.UI.MovablePanels` (the list against
      the BindWidget members, the Size Boxes, `ResolvePanelLayout` with empty
      and set entries and UiScale, `ApplyUserLayout` on a HUD without a layout
      a no-op, WBP_GameHUD's tree: every panel its own canvas child, no
      InventoryPair). Full `Valhalla.` run: 36 tests, all pass but the known
      `Valhalla.Core.Data.MeshIdFallback`. Backend: `smoke-settings.ts` 37
      assertions (401, GET 404 -> PUT -> GET, replace, ownership 404, bad ids,
      400s, 413 at 70 and 200 KB, 429 at 61, delete removes the row);
      `smoke-security` and `smoke-internal` still pass; `tsc --noEmit` clean.
- [x] **PIE (L_World, offline, 2 clients).** HUD unchanged
      (`Saved/ClaudeOps/b21/01_layout_baseline`), `valhalla.UI settings`
      prints the JSON, `movepanel Chat 300 -8` wrote
      `Saved/UI/settings_offline.json` 2 s later, a new PIE put the chat at
      (300, -8) (`03`, `04` open), `uiscale 1.3` + `opacity 0.4` (`05`),
      `resetlayout` put everything back (chat at 232, narrowed again). The
      backend path was not exercised in PIE (no backend running there).
- [x] **Step 3 — edit mode (2026-09-24).** `bLocked` false (the menu's Lock
      box, `valhalla.UI lock 0|1`) puts a `UValhallaPanelEditOverlay` over
      every movable panel on the root canvas (Z 55, the drawn rectangle,
      render scale included, re-measured every tick): the Highlight colour at
      50 % as a 1.5 px outline over a 6 % wash, the panel key top left, a
      14 x 14 grip bottom right. Relocking removes them (`SetEditMode`).
      - Drag: a left press on the overlay body -> `BeginPanelDrag` (mouse
        captured), `UpdatePanelDrag` on every move, `EndPanelDrag` on release
        (or capture lost). While it runs the panel is pinned by its drawn
        top-left (anchor / alignment / pivot 0), moved live, snapped to a
        4 px grid and to the canvas edges within 8 px, kept on the canvas
        (`SnapPanelPosition`). Two ticks after release (Slate has laid it
        out) `CommitPanelDrag` measures it, re-anchors it to the nearest of
        the nine anchor points per axis (near edge / centre / far edge,
        whichever the panel's own edge or centre is closest to; a tie goes to
        the centre: `ChooseAnchor`), turns the rectangle into Position at
        UiScale 1 (`AnchorLayoutForRect`: the drawn alignment point = anchor x
        canvas + Position x UiScale, whatever the render scale) and `Mutate`s
        the panel's layout (bSet; a flowing panel also stores its box as it
        is, so a move never resizes the TickLayout-narrowed chat). A press
        that never moves 3 units puts it back.
      - Resize (the grip, a 18 px hit area): flowing panels set their Size
        Box (combat log width + height; chat and skills width + max height),
        min 160 x 80 / 200 x 60 / 220 x 120, on the 4 px grid; scaled panels
        change Scale 0.5 - 2 (steps of 0.05) uniformly, growing from the
        pinned top-left (the corner opposite the grip). Unlocked, max-height
        panels (chat, skills) show at their full height so the whole box can
        be placed.
      - Hidden panels (by the player, or the game: no target, no party, no
        cast, closed windows) are shown at 40 % opacity while unlocked
        (`TickEditMode`, `IsPanelWantedByGame`) and go back to their state on
        relock.
      - What wins: unlocked, the overlay takes presses on the panel, so a
        drag on its cells or scroll boxes moves the panel (no cell drag and
        drop, no tooltips while unlocked; the mouse wheel is passed to the
        scroll box under the cursor). Each tick the overlay body stops
        hit-testing while the cursor is over one of the panel's buttons,
        check boxes, sliders, spin / text / combo boxes
        (`IsInteractiveChild`), so those still work. Right clicks fall
        through (the combat log's filter menu). Locked there are no overlays:
        cells, drag and drop and everything else are exactly as before.
      - Console: `valhalla.UI lock [0|1]`, `valhalla.UI dragtest <Key> <dx>
        <dy> [resize]` (press / move / release through the same functions the
        mouse handlers call); `movepanel` still sets a position directly.
- [x] **Step 4 — options menu (2026-09-24).** `UValhallaOptionsMenuWidget`
      (logic, `ValhallaOptionsMenuWidget.h/.cpp`) and `WBP_OptionsMenu`
      (look, `/Game/Valhalla/UI/HUD`, 131 widgets, made by the new
      `hud_blueprints.layout_options_menu` / tool
      `ValhallaUITools.layout_options_menu`, check-before-create; the HUD's
      new `OptionsMenuClass` defaults to the C++ class and the tool sets
      WBP_GameHUD's to WBP_OptionsMenu). Every widget is BindWidgetOptional
      (`GetOptionalWidgetNames`; one warning lists what a tree lacks):
      frame `Tabs` (widget switcher, 5 pages), `CloseButton`,
      `LayoutTabButton` `ColoursTabButton` `ChatLogTabButton`
      `NameplatesTabButton` `ControlsTabButton`; Layout: `LockCheck`,
      `UiScaleSlider`/`UiScaleText` (0.5 - 2), `OpacitySlider`/`OpacityText`
      (0.2 - 1), `ShowVitalsCheck` `ShowActionBarCheck` `ShowCastBarCheck`
      `ShowTargetFrameCheck` `ShowPartyCheck` `ShowCombatLogCheck`
      `ShowChatCheck`, `ResetLayoutButton` (panels, UI scale, opacity; not the
      lock); Colours: `ColourList` (C++ adds name / swatch / Edit per colour),
      `ColourEditor` with `EditTitle`, `PresetGrid` (12 swatches, C++),
      `HueSlider` `SaturationSlider` `ValueSlider` (sRGB HSV), `EditSwatch`,
      `ColourOkButton` `ColourCancelButton`, `ResetColoursButton`; Chat & log:
      `ChatFontSizeSlider`/`Text` (6 - 14 pt), `ChatLinesSlider`/`Text`
      (4 - 20), `TimestampsCheck`, `LogFilterList` (C++: a check box per
      combat-log filter), `ResetChatButton`; Nameplates:
      `NpcNameplatesCheck` `PlayerNameplatesCheck` `FloatingTextCheck`
      `NameplateFontSlider`/`Text` (8 - 20 px), `ResetNameplatesButton`;
      Controls: `ControlsText` (from the player controller's mapping context:
      `GetInputMappingContext`, grouped per action, 1 - 8 as one line). No
      Skills show switch: like the loot, character and inventory windows it
      is opened by a key (`GetMovablePanels` has it not hideable). Every
      control `Mutate`s (saved 2 s later) and the HUD re-applies at once; the
      HUD then `SyncFromSettings` the menu. Buttons, check boxes and sliders
      are not focusable (WASD keeps walking).
      - Wiring: `OpenOptions` / `CloseOptions` / `ToggleOptions`; made once,
        centred on the root canvas (Z 60), collapsed when closed, input mode
        unchanged (GameAndUI), its border eats clicks and the wheel.
        `CloseTopmost` closes the menu first (its colour editor before it);
        `HandleEscape`: nothing closed -> `OpenOptions`. Escape reaching the
        HUD while a menu control has focus closes it too. The cog:
        `OptionsButton` (UValhallaHUDButton, new `EValhallaHUDButton::Options`,
        BindWidgetOptional) at (1,1) / (1,1) / (-12,-12), 36 x 36, Z 9, the
        `T_UI_Cog` image as its brush (hover 1.25, pressed 0.75), tooltip
        "Options (Esc)". `valhalla.UI options` toggles it.
      - Art: `T_UI_Cog` 64 x 64, an 8-tooth bronze cog in the frame palette
        (T_UI_Button's #b8974e / #8a6f33 / #614d22, dark #1c150a outline, a
        dark well in the hub), drawn with numpy in Blender (no Pillow there),
        4x supersampled, source `Import/UI/Frames/T_UI_Cog.png` (with the
        other `T_UI_*` sources, so FindUiTexture's disk fallback finds it,
        not `Import/UI/Icons`, which imports to Icons/Items); imported alone
        to `/Game/Valhalla/UI/Frames/T_UI_Cog` with import_ui_icons.py's frame
        settings (bilinear, no mips, UI group; `Saved/ClaudeOps/b21/import_cog.py`).
        WBP_GameHUD re-laid out (`layout_hud_from_config(replace=True)`, 99
        widgets, compiled, saved; cell / bar classes kept: WBP_HUDSlot_C /
        WBP_HUDBar_C).
- [x] **Step 5 — live style (2026-09-24).** `ApplyUserStyle` records the
      overrides (`ColourOverrides`, only keys of `GetStyleColourKeys`) and the
      HUD reads colours through `Effective*Colour()` / `ChatColour` /
      `HpColourFor` (override, else the "Valhalla|HUD Style" default; the
      properties themselves are never written). 16 keys: HpHigh HpMid HpLow
      Mana Energy CastBar Highlight KeyLabel Label Value ChatGeneral ChatWorld
      ChatWhisper ChatParty ChatSystem and the new `PanelTintColour` (white;
      multiplies the movable panels' background brushes, alpha x
      PanelOpacity: `ApplyPanelBackgrounds`). Live, no rebuild: bars and
      nameplates next tick, cells' highlight (`SetHighlightColour`) and key
      labels, party names, cast bar, edit outlines at once, the skills rows
      rebuilt when next shown; the chat is redrawn when its font size
      (6 - 24 clamp), idle lines, timestamps ("[hh:mm] " from the local
      arrival time; lines from before the HUD have none) or channel colours
      change. `LogFilters` are the combat log's filters (restored on load;
      `ToggleLogFilter` / new `SetLogFilter` save them). NPC / player
      nameplate and floating-text switches in `TickWorldLayer` /
      `SpawnFloater`; nameplate font size restyles the pooled plates
      (`RestyleNameplates`, no Rebuild). ApplyUserLayout logs only when its
      summary changes (a slider drag applies every frame).
- [x] **Tests (steps 3-5).** `Valhalla.Game.UI.OptionsMenu` (Options enum
      value after the old ones, OptionsButton / OptionsMenuClass, every menu
      name a BindWidgetOptional member of the kind its name says, show
      switches name hideable panels, 12 presets, the controls text from a
      mapping context, WBP_OptionsMenu's tree has every name and 5 tabs,
      WBP_GameHUD names it and has the cog with Action = Options),
      `Valhalla.Game.UI.StyleColours` (16 keys, each a HUD Style
      FLinearColor; CDO defaults; override wins for its key only, is dropped
      with the setting; unknown keys ignored; log filters restored),
      `Valhalla.Game.UI.EditModeMaths` (ChooseAnchor's 9 cases + tie, snap:
      grid / edges / on canvas, AnchorLayoutForRect round trip through
      ResolvePanelLayout at UI scale 1 / 1.5 / 0.75, a bottom-right panel 12
      px from its corner on a bigger window, IsInteractiveChild). Full
      `Valhalla.` run: 39 tests, all pass but the known
      `Valhalla.Core.Data.MeshIdFallback`.
- [x] **PIE / game (L_World, offline, 2 clients; `Saved/ClaudeOps/b21/10_`..`25_`).**
      Cog bottom right (`10`); a real click on it (SlateInspector) opens the
      menu (`11`); every tab renders (`12`-`15`); the colour editor, preset
      red, OK: HP bar red, opacity slider dragged to 20 % (`16`, `17`), both
      saved to `settings_offline.json`; unlock: outlines, grips and ghosts
      (`18`); a real mouse drag (SlateInspector Drag) moved the combat log
      from (1085, 12) to the top edge, re-anchored top centre, saved (`19`);
      `dragtest Chat 120 -60 resize`: 392 x 112, anchored bottom left (`20`);
      the PIE window resized 646 x 520 -> 1000 x 640 with the Win32 API: the
      panels kept their anchors (`21`); colours reset, relocked: overlays and
      ghosts gone (`22`); a new PIE loaded it all (`23`); combat log hidden,
      Misses filtered, timestamps and 14 pt chat (`24`: "[12:25] [G]
      Player1: ..."). Escape in PIE stops the session (the editor's binding),
      so Escape was checked in a standalone `-game` window with real
      WM_KEYDOWNs: nothing open -> the menu opens (`25`), Escape closes it,
      I then Escape closes the inventory (not the menu), Escape again opens
      it. The offline settings file was put back to its defaults afterwards
      (the run's copy: `b21/settings_offline_after_pie.json`).
      - Not exercised: a real press on the resize grip (no Slate ref for it;
        the grip goes through the same BeginPanelDrag as `dragtest`), the
        backend path (step 6), inventory drag and drop while locked
        (unchanged code; no items offline), nameplate font / switches on
        screen (only through the settings file), a player-hidden panel's
        ghost (the same code as the game-hidden ghosts shown in `18`).
      - Known: two PIE clients share `settings_offline.json` (character 0);
        the last to save wins. The tab bar's "current tab" tint is faint on
        the bronze plates.
- [x] **Step 6 — verification with the backend (2026-09-24).** Run as B-14
      did: `npm run dev:server` (repo root; `secrets.local.env` applied, so
      production mode on 127.0.0.1:2567, `server/valhalla.db` kept), PIE from
      `L_FrontEnd`, Play Standalone, Launch Separate Server, Run Under One
      Process, **1 client** for the run (put back to 2 afterwards),
      `valhalla.AutoLogin` with the throwaway account `b21tester` (userId 17,
      registered through `POST /api/auth/register`; characters `Bsyncwar`
      warrior id 41 and `Bsyncwiz` wizard id 42 through `POST
      /api/characters`; account and characters left in the dev db, password
      only in the gitignored `Saved/ClaudeOps/b21/creds.json`). Each run went
      front end -> login -> character select -> Enter World -> the PIE
      server's token verify -> in world. Screenshots `Saved/ClaudeOps/b21/30_`..`35_`,
      API responses `31_api_get_*.json`, `34_api_get_*.json`, `36_*.json`.
      - Load at login, nothing saved: `UI settings: character 41 from the
        defaults.` then `settings get char=41 : 404` -> `character 41 has none
        saved; the defaults.` (`30`).
      - Save on change: a real click (SlateInspector) on the cog, then on the
        menu's Combat log box (hidden), then `valhalla.UI movepanel Chat 300
        -8` and `opacity 0.5`: 2 s later each time `saved .../Saved/UI/settings_41.json
        (updated 2026-09-24T18:40:55.111Z)` and `settings put char=41 : 200
        ok` (`31`). `GET /api/characters/41/settings` (the account's JWT):
        200, `PanelOpacity` 0.5, `Panels.Chat.Position` [300, -8],
        `Panels.CombatLog.bVisible` false, `UpdatedAt` as logged; 42 still 404.
      - Cache deleted (moved to `b21/32_settings_41_cache_moved_away.json`),
        new PIE as Bsyncwar: `from the defaults` (no file) -> `settings get
        char=41 : 200 ok` -> `character 41 from the backend (updated
        …18:40:55.111Z)` -> `player layout applied (2 panel(s) placed, UI scale
        1.00, panel opacity 0.50)`, the cache file written again; the menu
        shows Combat log unticked and 50 % (`32`).
      - Second character: Bsyncwiz got its own defaults (`character 42 from the
        defaults`, 404, 0 panels, opacity 1.00; `33`); `uiscale 1.3` +
        `movepanel CombatLog -12 240` -> `settings_42.json` + `put char=42 :
        200` (`34`); the GETs then: 42 UiScale 1.3 with only CombatLog placed,
        41 unchanged (same `UpdatedAt`, opacity 0.5).
      - Back to Bsyncwar: `from .../settings_41.json (updated …18:40:55.111Z)`,
        2 panels / 0.50 at once, then the GET tie -> `from the backend` (same
        value); HUD as in `31` (`35`).
      - Newer local copy (the token-expired case): `settings_41.json` edited
        offline (`bChatTimestamps` true, `UpdatedAt` 18:43:45) -> login:
        `character 41's local copy (2026-09-24T18:43:45.000Z) is newer than the
        backend's (2026-09-24T18:40:55.111Z); sending it.` + `put char=41 :
        200 ok`; the GET then has the timestamps flag and 18:43:45. No token:
        401.
      - No fixes were needed. `Valhalla.` suite afterwards: 39 tests, 38 pass,
        the known `Valhalla.Core.Data.MeshIdFallback` fails. Backend stopped,
        the test caches moved out of `Saved/UI` (`b21/37_final_*`),
        `settings_offline.json` unchanged, `valhalla.AutoLogin` cleared,
        `L_World` reopened.
      - Not driven: typing a login into the front end's own text boxes
        (AutoLogin runs the same login / select / Enter World calls); two
        characters in one client session (there is no in-world "back to
        character select", so every switch is a new login, which is what a
        player does); a backend that is down mid-session (the code keeps the
        cache; the newer-local path above is what the next login does).
      - Known limits: two offline PIE clients share `settings_offline.json`
        (character 0; last save wins). A token that expires mid-session (24 h)
        makes the PUTs 401; changes stay in the cache file and the next login
        sends them (the newer-local rule above). Character
        delete leaves `character_action_bar` rows (pre-existing, found in step
        1: sql.js `export()` resets `PRAGMA foreign_keys`, so no `ON DELETE
        CASCADE` fires; `character_settings` is deleted explicitly). The
        `valhalla.AutoLogin` cvar echo puts the dev password in the local
        editor log (`Saved/Logs`, gitignored).

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

## B-06 Phase 1, steps 1.4-1.5 — Eldmoor Grasslands level, Landscape and blockout (2026-09-24)

- [x] **Levels:** `scaffold_zone("grasslands_v2", "grassland", 143)` made `Zones/L_GrasslandsV2` and
      `Zones/L_GrasslandsV2_Gameplay` (both in `maps/handedited.json`); streamed into `L_World`
      always-loaded at **X = +80000 cm** (zone volume world X 80000-89152, Y 0-9152). The scaffold's
      floor tiles were removed (the Landscape is the floor); display name "Eldmoor Grasslands".
- [x] **Landscape:** 5 x 5 components of 63 quads (1 section), **316 x 316 vertices, 32 cm a quad**
      (10080 cm; the zone plus 4.5 m of border), Z scale 10 (0.078 cm a step), actor at
      (79552, -448, 0). Heights from `valhalla_tools/eldmoor_terrain.py` (pure Python, reads
      `eldmoor_layout.json`; outputs committed in `Docs/Zones/Eldmoor/terrain/`): river channel -0.7 m,
      banks +0.5 m, vale floor, downs +1..+3 m, knoll and keep plateau flat +4 m, glacis ~28 deg,
      Kingsbarrow mound, ridge +4 m with the 1.2 m scarp (a 12 deg ramp where the highland path
      crosses), scree +3 -> +1 m, Greyfell Tor +7 m with one-quad S/E faces, cliffs to nothing beyond
      the north edge. Material `/Game/Valhalla/Materials/Landscape/M_EldmoorLandscape`
      (`eldmoor_landscape.build_material()`): weight-blended Grass / Moss / Dirt / Rock on the kit's
      texture sets and sizes, macro variation, Landscape Visibility Mask on Opacity Mask. Layer infos
      in `Zones/L_GrasslandsV2_sharedassets/`. Imported through Landscape mode > New > Import from File
      (no Python API creates Landscape components); **never give the `__LANDSCAPE_VISIBILITY__` layer a
      file or a layer info in that dialog** — in 5.8 that asserts in
      `LandscapeEditLayerTargetTypeState.cpp:182` and takes the editor down.
- [x] **Undercroft:** the Landscape under x 61-77 / y 5-13 is sunk to the cellar floor (+1 m, 3 m under
      the hall) instead of a painted hole: the room walls (keep modules, BlockAll so the fog does not
      draw them through the hall), stone floor, `SM_StoneRamp` (verified walkable both ways),
      `SM_KeepParapetLow`, cell bars and torches stand in the pit and the hall floor covers it. The
      material already carries the visibility mask, so a hole can be painted later (Landscape mode >
      Sculpt > Visibility, small brush) without changing anything else.
- [x] **Blockout:** `valhalla_tools/build_eldmoor_blockout.py` — `build()` places everything from
      `eldmoor_layout.json` (refuses if the level already has an actor tagged `EldmoorBlockout`),
      `add_neighbour_portals()` adds the Grasslands / Desert ends, `light_fires()` lights this level's
      fires, braziers, forges, torches and lamp posts only. About 1,100 actors + 1,000 instances
      (trees, scatter, bank strips, floors in `AValhallaTileField`s). Uses the Eldmoor art kit
      everywhere it exists (cliffs, SM_CliffCleft, palisade and gates, thickets, forge, hay, training
      posts, cell bars, ramp, parapet, scorched timber, cart, bind stone, torches).
      Placeholders/markers: `PH_Portal_GreyfellCleft` (TriggerBox, no portal, no glow),
      `PH_Mist_M1..M6` (TargetPoints scaled to the pocket radii; the mist volume is fog work),
      `PH_CreatureCamp_P1/P2`, `GuardPost_*` / `GuardPatrol_*`. River: pawn-only `InvisibleWall`
      bank walls, gaps only at the two bridges. Zone edges: pawn-only walls + tree line.
- [x] **Portals:** Grasslands (2048, 96) <-> Eldmoor (5120, 8896), Desert (3968, 864) <-> Eldmoor
      (8896, 512), SM_PortalMarker rune-stones, entries 4.5 m inside; the Grasslands / Desert ends are
      in their `_Gameplay` levels. **Entry ids are zone-prefixed** (`eldmoor_from_grasslands`,
      `eldmoor_from_desert`, `grasslands_from_eldmoor`, `desert_from_eldmoor`): entries are found by
      id across the whole world, and the layout's `entry_from_*` names collided with the existing
      Grasslands / Desert entries. Overlays and `zones.json` (`defaultSpawn` = Harrow's Rest, 3200,
      7168 = the bind point; the four starts are at the Grasslands arrival) updated.
- [x] **Fog / nav:** `FogBounds_grasslands_v2` covers the zone (z -400..1100 for the tor and cleft);
      `NavMeshBounds_grasslands_v2` over the zone for B-16 (nav not built).
- [x] **Verified in PIE** (standalone clients): Grasslands -> Eldmoor -> Grasslands, Desert -> Eldmoor
      -> Desert all arrive at the right entries; the zone atmosphere blends in on entry; glacis ->
      gate -> bailey -> inner gate, the knoll road to the east gate, the highland path up the scarp
      gap and the undercroft ramp (down to +1.7 m and back) are walkable; the tor's S and E faces
      stop a walking player; the river cannot be forded off the bridges; the cleft passage can be
      entered from the cliff foot. Line of sight (Visibility traces filtered to the VisionBlocker
      profile): blocked by TH1, TH4, the palisade, the keep curtain and the tor face; clear through
      TH1's gap and across open downs.
- Deviations: postern tower moved from y 20 to y 16 so the postern door (y 18-20) can open beside it;
  the hall fireplace is at (80, 5), because the design's (76, 5) is the top of the ramp; the entry ids
  above; no roof-hide / hall-floor-hide exists in code yet (the design assumes it), so a player in
  the undercroft is under the hall floor.

- [x] **Spawn height (C++):** `UValhallaZoneSubsystem::FindStandingZ` traces straight down, finds the
      Landscape, and stands the capsule on the highest walkable surface within +3.2 m / -1 m of it that
      the capsule fits on (a floor, a bridge deck, the great hall over the undercroft), else on the
      Landscape; no Landscape under the point (the flat tile zones) changes nothing.
      `InitializeJoiningPlayer`'s saved position (only 2D, so on Eldmoor's +4 m knoll "zone floor +
      constant" was inside the hill) and `valhalla.DebugTeleport` (kept the old height) use it.
      Verified with DebugTeleport in PIE: knoll 404, inside the Broken Spur 406 (not the roof at 762),
      stone bridge deck 115 (not the river bed at -70), keep courtyard 400, scree 285, tor top 697.
      The saved-position login was not re-run end to end (it needs a backend login); it calls the same
      function. The admin teleport route still uses the default spawn's height. `Valhalla.` tests: all
      pass except the known `MeshIdFallback`.

## Zone overlay JSON retired; zone actors live only in Unreal (2026-09-24)

Kevin's decision (B-06): Unreal-placed actors are the only source of truth for portals, zone
entries, player starts and NPC Spawn Points. `maps/overlays-2.0/*.json` is deleted, along with
everything that read or wrote it. The game had already stopped using it at runtime (login,
respawn, portals and NPCs all read actors); what remained were warnings, the web editor's
Map Objects page and the Live Dashboard's portal/entry/start markers. Doors: open archways
for now; clickable doors are B-23.

- **Game server.** `UValhallaZoneSubsystem`: `LoadOverlays` / `ReloadOverlays` /
  `ParseOverlay` / `ValidateOverlayPoint`, the overlay types in `ValhallaZoneTypes.h`,
  `valhalla.ReloadOverlays` and the `DataHotReload` overlay watch are gone. New
  `CheckPlacedActors()` (server, at `OnWorldBeginPlay`, and `valhalla.CheckZones`) warns when a
  portal's target zone or entry does not exist or the entry stands in another zone, when two
  entries share an id, and when a zone has no tagged PlayerStart.
- **Admin API.** `/reload-overlays` is removed. `GET /state` now gives each zone `portals`
  (`targetZoneId`, `targetEntryId`), `zoneEntries` (`entryId`, `fromZoneId`, `yaw`),
  `playerStarts` (`tag`, `yaw`) and `zone` (display name, size, default spawn), all zone-local cm.
  `Valhalla.Game.Admin.StateJson` covers the new fields; `Valhalla.Game.Zones.OverlayParse` is
  deleted.
- **Web editor.** The Live Dashboard draws portals, entries and starts from `/state`; the
  Teleport dialog defaults to the target zone's first tagged start, else its default spawn. The
  Map Objects page, `/api/overlays2/*` and the `reload-overlays` proxy route are deleted.
- **Validator.** `export_unreal_refs.py` also exports zone volumes, portals, zone entries and
  player starts; `npm run validate` checks them against `zones.json` (same rules as
  `CheckPlacedActors`). The overlay checks and the `maps` category are gone; the pre-commit hook
  no longer watches `maps/overlays-2.0`. Re-export `maps/unreal-refs.json` from `L_World` to
  turn the new checks on.
- **Tools.** `scaffold_zone` and the retired builders no longer write overlays (a zone "exists"
  when a zone volume with its id is loaded, or its levels exist); `handedited.json` lists levels
  only; `npc_setup.migrate_overlay_spawns` (one-time, long done) is deleted.

## Action bar — empty by default, drag to place, drag off to remove (2026-09-24)

Kevin: a new character starts with a blank action bar and fills it from the
skills pane (K); a newly unlocked skill is dragged on the same way; a skill is
removed by dragging it off the bar. The bar used to be pre-filled with the
first eight of `classSkills[classId]` and was never saved.

- **Empty by default.** `UValhallaSkillComponent::InitializeActionBarFromClass`
  (server, once the class is known) now only empties the eight slots. Nothing is
  added automatically on level-up.
- **Saved with the character.** `AValhallaGameMode::SpawnLoadedPawn` puts the
  backend's saved bar on the pawn (`ApplySavedActionBar`); `SaveCharacterFor`
  reads it back (`GetActionBarForSave`: eight ids, `""` for an empty slot) into
  the session before every save (autosave, logout). A saved entry that no longer
  passes the rule below (a skill removed from the data, another class's) is left
  empty with a warning.
- **The rule, server-side.** `CanPlaceOnActionBar`: one of the class's skills
  (`classSkills[classId]`) or a cross-class one (`classId` null, like
  `melee_attack`), and unlocked (`level >= levelRequired`).
  `ServerSetActionBar` refuses anything else and the slot keeps what it had
  (it used to clear it); clearing a slot is always allowed. The static
  `CanPlaceOnActionBar(Skill, ClassSkills, Level)` and `BuildActionBarFromSave`
  are the pure parts (`Valhalla.Game.ActionBar.*`).
- **HUD.** The skills pane lists the class's skills in pane order; a locked one
  is dimmed with "(Lv N)" and dragging or clicking it onto the bar is refused on
  the client too, with a system line "You must be level N to use <skill>."
  Dragging a bar skill onto another bar cell swaps (onto an empty one, moves);
  let go on the cell it came from or in a gap between cells, nothing changes;
  let go anywhere else — another HUD cell (skills pane, inventory), a panel, the
  chat, the world — and it comes off the bar (`UValhallaHUDDragOperation`:
  UMG's `DragCancelled` when no widget took the drop, and `HandleSlotDropped`
  for an action cell dropped on a non-action cell). Right-click also clears a
  slot. Keys 1–8 on an empty slot do nothing.

## B-16 — NPC pathfinding: nav mesh and path steering (B-06 step 1.8, 2026-09-24)

NPCs walk round walls, houses and through doorways instead of pushing in a straight line. The
1.0 state machine stays the brain (aggro, leash, attack, walk home, all unchanged); only the
direction the chase and the walk home push changes.

- **No AI controller** (a change from the B-16 doc). A path follower would move NPCs on the frame
  clock outside the fixed 60 Hz step, and possessing them changes their owner, which the
  visibility code walks. Instead `AValhallaNPC::PlanAndSteer` asks the nav mesh directly:
  - every 0.5 s, or when the goal has moved 50 cm, it re-plans: a nav raycast first; clear
    ground means steer straight at the live target (exactly the old behaviour, so melee spacing
    and attack range are exact); otherwise `FindPathSync` (partial paths allowed);
  - each fixed step it steers at the next corner (`FValhallaNPCPath`, reached within 30 cm), then
    at the live goal once the corners run out;
  - no nav data (L_GreyBox, tests) or no path: steer straight, as before.
- **Stuck = warp home.** A chasing NPC more than attack range + 150 cm from its target that moves
  less than 25 cm in 3 s gives up (`ResetToHome`: teleport, heal, clear threat), EverQuest style;
  one walking home that is stuck teleports the rest of the way. Crowding at melee range never
  counts.
- **Nav data.** NavMeshBoundsVolumes in L_Grasslands, L_Desert and L_GrasslandsV2 (each covers
  the zone volume plus 2 m). RecastNavMesh-Default (L_World): agent radius 30, height 120 (the
  character capsule), step 45, slope 44, **cell size 5 cm** (19 cm closed the 1 m archways:
  erosion plus rasterisation ate the whole opening), **runtime generation Dynamic**, so the
  editor rebuilds edited tiles itself and the "NAVMESH NEEDS TO BE REBUILT" message is gone.
  DefaultEngine.ini carries the same agent (`SupportedAgents`) and RecastNavMesh defaults.
- **Tests.** `Valhalla.Game.NPC.PathSteering` pins re-planning, corner following, the live goal
  at the end, direct/fallback steering and the stuck clock. Suite: all pass except the known
  `MeshIdFallback`.
- **PIE checks (standalone, Grasslands town):** a Test Enemy aggroed next to a building, player
  moved 3 m away behind it: the NPC walked the 9–11 m path round the building and stopped at
  89–90 cm and attacked, twice (two buildings). After the player died it walked home round the
  same building to within 2 cm. Nav queries through all 12 Eldmoor archways and gates
  (palisade gate, keep gates, keep/tavern/house arches) find straight, complete paths.
- **Not done here:** patrol routes and roaming (B-10 / population), crowd avoidance (NPCs still
  just collide), the L_LoSTest doorway check (the town checks cover the same case).

## B-10 part 1 — Social aggro (2026-09-24)

Kevin: an NPC that can aggro gets a second checkbox, Social Aggro. When it picks up a target it
calls for help, and similar NPCs nearby join the fight.

- **Data** (npc-templates.json, all optional): `canSocialAggro` (off by default), `socialGroup`
  (who answers; blank means this template only) and `socialRange` (how far the call reaches;
  blank or 0 means the aggro range). Loaded into FValhallaNPCTemplate; `npm run validate` errors on
  a negative range and warns when the box is ticked on a friendly NPC or one with Can Aggro off.
- **Web editor, NPCs page:** a Social Aggro checkbox under Can Aggro (shown only when Can Aggro
  is ticked), with Social Group and Social Range showing their defaults as placeholders.
- **Game server** (AValhallaNPC::CallForHelp / JoinFight, rule in FValhallaSocialAggro):
  - the first fixed step an NPC has a target (proximity, a hit, a miss or a taunt), it calls once
    per fight, if its template has the box ticked;
  - an NPC answers when it is in the same social group, alive, can aggro, is not already fighting,
    is within the caller's social range (XY) and can see the caller: a VisionBlocker trace at eye
    height, so walls, cliffs and the TH thickets stop the call;
  - it joins with 1 threat, like a proximity pull, so whoever hits it takes it over;
  - chaining (Kevin's choice): an NPC that joins calls its own neighbours if its own box is ticked.
    Chains end because an NPC already fighting never answers and each NPC calls once per fight
    (reset when it loses its target, leashes, dies or respawns).
- **Tests:** `Valhalla.Game.NPC.SocialAggro` pins the answer rule (group, range edge, sight,
  already fighting, dead, can't aggro, zero range). PIE, with Test Enemy (Bandit) temporarily set to
  `canSocialAggro`, group `bandits`, range 400 (npc-templates.json restored afterwards):
  - chain: the player pulled Bandit A; Bandit B 250 cm away answered A; Bandit C 600 cm from A but
    350 cm from B answered B;
  - wall: a Bandit 135 cm from the pulled one but on the far side of a wall did not answer.
- No template has social aggro switched on yet; that is for 1.9 population.

## NPC leash walk-back, facing, ranged auto-attack (2026-09-24)

Kevin, before B-06 1.9: a leashed NPC should walk back, not teleport; an NPC should not hit a
player behind it (the player rule); NPCs get a melee or ranged auto-attack.

- **Leash walks back** (`AValhallaNPC::StartReturn` / `FinishReturn`, replaces `ResetToHome`):
  - the NPC remembers where the fight started (`FightStart`, its own position the step it first
    took a target; its home spot until patrols move). The leash measures from there;
  - past the leash it drops its target and threat and walks back along a nav path at full speed.
    The B-16 stuck-chase give-up does the same (settles B-25's open question: walk, don't warp);
  - Kevin's choices: it can be **pulled again on the way** (a hit, its aggro range, a call for
    help), and it **heals on arrival**, so one re-pulled halfway is still hurt. A re-pull keeps the
    original FightStart, so a fight cannot be dragged further out one leash at a time;
  - it can only be re-pulled once back within 75% of its leash range; beyond that it would leash
    again on its next step. A stuck give-up also ignores proximity from the player it gave up on
    until it is back (a hit still re-pulls), so it doesn't give up, re-aggro and give up again;
  - the one teleport left: a walk back that makes no progress for 3 s warps to the spot;
  - a fight that simply ends (target dead) still strolls back at 2/3 speed, no heal.
- **Facing**: an NPC's attack needs `UValhallaCombatLibrary::IsFacing` (the players' 60° cone). An
  NPC standing its ground turns in place toward its target at its turn rate (540°/s); the attack
  timer waits, so the hit lands the moment it faces. Stationary NPCs with a target now turn and
  fight from where they stand (they still never chase or leash).
- **Melee or ranged** (`attackType` in npc-templates.json, `melee` by default):
  - ranged: a blank or 0 `attackRange` means 600 (the player's Ranged Attack); the NPC walks in
    until the target is within 90% of its range *and* in sight (VisionBlocker trace at eye
    height, as social aggro), then holds and shoots until the target leaves its range or sight;
    point-blank it keeps shooting. Same damage roll and pipeline as a melee hit, landing
    instantly like the player's Ranged Attack (no visible arrow yet; B-09);
  - web editor: an Auto Attack dropdown (Melee / Ranged) next to Attack Speed; the Attack Range
    hint shows the default for the type. `npm run validate` errors on an unknown type and warns
    on a ranged NPC without a ranged weapon or a melee NPC with a bow (it warns today for Wood's
    Edge Bandit 3, which carries a short bow).
- **Rules and tests**: the decisions live in `FValhallaNPCCombatRules` (chase or hold, attack
  gate, leash, re-pull); `Valhalla.Game.NPC.CombatRules` pins them. Suite: all pass except the
  known `MeshIdFallback`.
- **PIE checks** (standalone, Grasslands Test Enemy bandits; for the ranged runs the template was
  temporarily set to ranged + short bow + aggro 300, npc-templates.json restored byte-identical):
  - leash: HP set to 20, player pulled it then jumped 16 m away; it chased 12 m, leashed, walked
    back (largest move in one frame 10 cm, i.e. no teleport), healed to 200 on arrival, stopped
    0.2 cm from its spot;
  - re-pull: the player stepped in front of it 6.7 m from its spot on the way back; it took the
    player again at 20/200 HP and attacked;
  - facing: an NPC turned away with the player 80 cm behind it turned at 540°/s and its first hit
    landed as the player entered its 60° cone (about 0.15 s), not before;
  - ranged: shot at 66 cm (point blank), then with the player 4.5 m away it turned and held its
    spot, shooting every 4 s; with the player 1.7 m away behind a building wall it never shot and
    walked the nav path round instead.
- **Seen in the wall run (for B-25):** a player standing behind a wall whose way round is longer
  than the leash gets a loop: chase, leash, walk back, re-pulled by proximity, chase again. That is
  the re-pull-by-proximity rule working as chosen; B-25's "give up when not getting closer" is the
  place to decide whether a leash should also stop proximity re-pulls by the same player.

## Web editor — template IDs (2026-09-24)

- NPC, Item, Skill and Loot Table editors show the id at the top of the details panel (monospace, Copy button) and as a list column; search matches ids.
- New / Copy asks for a name and derives a snake_case id (editable, must match `^[a-z][a-z0-9_]*$` and be unused), so no more `npc_<timestamp>` ids; Cancel creates nothing.
- The id can be renamed in place while nothing in the shared data references it (the field lists what does); NPC renames also need the `BP_NPC_*` Default Template Id changed in Unreal. Shared bits: `editor/src/components/shared/TemplateId.tsx`.

## Per-class base auto-attack damage; ranged scales with Dexterity (2026-09-24)

Kevin wants player melee scaled down, tunable per class in the web editor; a fuller damage
redesign may come later.

- **Data:** `classes.json` classes can carry `baseMeleeDamage` and `baseRangedDamage`; a class
  without them uses the old constants, 10 and 8, and the web editor shows those defaults and writes
  the fields on the next Save of that class. Optional everywhere:
  the TS type (`shared/src/classes.ts`), the C++ loader (`FValhallaClassTemplate::BaseMeleeDamage`
  / `BaseRangedDamage`, falling back to `Valhalla::BaseMeleeDamage` / `BaseRangedDamage`).
- **Web editor:** Classes → Properties has "Base Melee Damage" and "Base Ranged Damage" under the
  attack speeds. `npm run validate`: error if either is negative or not a number; warning if melee
  is 0, or if a class with a ranged attack speed has ranged damage 0.
- **Formula** (`UValhallaSkillComponent`, player auto-attack only):
  melee = class `baseMeleeDamage` + weapon roll + Strength × 0.4;
  ranged = class `baseRangedDamage` + bow roll + **Dexterity** × 0.4 (was Strength).
  `StrengthDamageScaling` 0.8 → 0.4 and new `DexterityDamageScaling` 0.4 in
  `ValhallaConstants.h`, mirrored as `STRENGTH_DAMAGE_SCALING` / `DEXTERITY_DAMAGE_SCALING` +
  `computeRangedPhysicalDamage` in `shared/src/stats.ts`. The base is read from the class at swing
  time, so a web-editor Save + data reload applies on the next swing. Unchanged: NPC melee and
  ranged (their own template ranges), skills (`SkillStatScaling`), spell damage.
- **Tests:** `Tools/fixtures/stats_fixtures.json` `physicalDamage` expectations rewritten for 0.4
  (the other sections untouched); `Valhalla.Core.Stats.DamageRoll` gains the Dexterity and
  class-default cases; `Valhalla.Core.Data.Loads` checks the two fields were read. TS: `tsc` clean
  for shared/server/editor, `npm run validate` 0 errors, `npm run smoke` 134/134.
- **Built and tested:** full editor build (editor closed; `FValhallaClassTemplate` gained two
  UPROPERTYs), then `Valhalla.` headless: 42 passed, 1 failed — the known
  `Valhalla.Core.Data.MeshIdFallback` (iron_dagger / bone_totem author a meshId), unrelated.
  Not yet seen in PIE: a swing's damage against the numbers above.

## Backlog

Open features and improvements are tracked in Google Drive, folder
"Valhalla 2.0 Backlog": the index document "Valhalla 2.0 — Backlog"
(https://docs.google.com/document/d/1MmGert5h6aDJx1TSdeO-gpdU7zDGqMaKnNZjBVI_6aA/edit)
links one plan document per item (B-01 ...). Check it before starting new work.

Current index (2026-09-24, supersedes the links above): "Valhalla 2.0 — Backlog"
(https://docs.google.com/document/d/1swd398GJccpi1PJWxTR9hlbcAhifoV5_0A1iU2GSgKQ/edit).
B-15 plan: https://docs.google.com/document/d/1rPej3Xn6kOdMX3fuJzbg3geO9o5SqFzcIMrrSG5EXe8/edit;
asset list: https://docs.google.com/spreadsheets/d/1O0EMLM5b__j9z3URoybK_i6beZUKANp7vziid2nIoxc/edit
(Docs/B-15 Art asset list.xlsx is the repo copy).

Decision (Kevin, 2026-09-24): B-15 Wave 6 (creatures) is on hold. The first creatures
can be free Unreal assets; the classes get built out first (backlog B-20) before more
NPCs and creatures are added.
