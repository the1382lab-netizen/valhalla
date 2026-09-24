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
      (`ServerSetActionBar`).
- [x] Loot panel: Phase 9's `TryOpenLootBag` (click a bag or its label) opens
      it; take one, Loot All, Close; closes out of reach or when emptied.
- [x] Live layout: `valhalla.ReloadUI`, and a 2 s poll of the file's
      timestamp, rebuild the HUD from the UI Layout editor's saves.
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
- Not persisted: `bAlive`, god/freeze/mute, buffs, cooldowns, and a customised
  action bar (saved as eight empty strings).
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
  wide stride. Sit / emotes / 2H / block / dodge are imported but not wired up.
  No LODs on the pieces.
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
- [ ] **Step 2 — groundwork:** a C++ base class for the Blueprint panels
      (bindable view-model getters, the drag/drop and tooltip plumbing the
      code-built HUD owns today).
- [ ] **Step 3 — Blueprints:** Widget Blueprints for the character /
      inventory panel (uses `ClientStats`, `GetXpFraction`, the weapon fields
      `minDamage` / `maxDamage` / `attackSpeedMs`), then the other panels.
- [ ] **Step 4 — switch-over and cleanup:** the HUD creates the Blueprints,
      the code-built panels and their `ui-config.json` knobs go.

## Backlog

Open features and improvements are tracked in Google Drive, folder
"Valhalla 2.0 Backlog": the index document "Valhalla 2.0 — Backlog"
(https://docs.google.com/document/d/1pGBSZFRmC25Vw6mJIIcS9fPpWIo_ufkfvQ_qNeDPWCk/edit)
links one plan document per item (B-01 ...). Check it before starting new work.

Current index (2026-09-23, supersedes the link above): "Valhalla 2.0 — Backlog"
(https://docs.google.com/document/d/11_6hlP2HgrbNl8QTKk3NRAx1t_sf1rAE9gqlseRyW-A/edit).
