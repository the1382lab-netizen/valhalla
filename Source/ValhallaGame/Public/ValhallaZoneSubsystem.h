// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ValhallaZoneTypes.h"
#include "ValhallaZoneSubsystem.generated.h"

class AValhallaPortal;
class AValhallaZoneEntry;
class AValhallaZoneVolume;
class APawn;

VALHALLAGAME_API DECLARE_LOG_CATEGORY_EXTERN(LogValhallaZones, Log, All);

/**
 * Every zone in the world, where each one is, and who is in which.
 *
 * ## One world, every zone
 *
 * 1.0 ran one Colyseus room that held every player and swapped the *map* under
 * them: `MapManager.loadZone` parsed a Tiled file on demand, `player.zoneId`
 * said which one you were on, and the systems that had to be zone-aware — chat
 * `general`, party XP splits, NPC ticking — compared that string. There was
 * exactly one process and one shared clock.
 *
 * 2.0 keeps that shape exactly. `L_World` is a persistent level whose only
 * contents are lighting and a post-process volume; `L_Grasslands` and
 * `L_Desert` are always-loaded streaming sublevels, the desert offset by
 * +40000 cm on X so the two 4096 cm zones cannot touch. One server, one
 * `AValhallaGameState`, one `ServerFixedTick`, every zone resident. Nothing
 * streams in or out during play, so a zone change is a teleport and not a
 * level load — which is why it costs a frame rather than a loading screen, and
 * why an NPC in the desert keeps ticking while every player is in the
 * grasslands, as 1.0's did.
 *
 * The alternative — a server per zone, or level streaming keyed on the local
 * player — was rejected for the reason 1.0's design implies: party members in
 * different zones still have to be in each other's party, and a whisper still
 * has to arrive. A cross-process message bus to reproduce what a shared
 * `TArray<APlayerState*>` already does would be a lot of machinery to buy
 * nothing at this scale.
 *
 * ## Where a zone's facts come from
 *
 * `AValhallaZoneVolume` actors, discovered on `OnWorldBeginPlay`. The volume
 * carries the id, the display name, the box and the default spawn, and because
 * it is a level actor its box is in *world* space with the streaming offset
 * already applied. No zone coordinate is ever written down twice.
 *
 * ## `PlayerState::ZoneId`
 *
 * Derived, never assigned by hand except by a portal. `UpdatePlayerZones` is a
 * step of `AValhallaGameState::ServerFixedTick`: for each player state it box-
 * tests the pawn against the zone it was last in, and only when that fails
 * does it search the list. So the common case is one float compare and the
 * uncommon one is a loop over two. A player who walks out of every zone keeps
 * the id they had, which is deliberate: falling back to "no zone" would drop
 * them out of `general` chat and out of their party's XP split because they
 * stood on a hill the volume did not quite cover.
 */
UCLASS()
class VALHALLAGAME_API UValhallaZoneSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem interface
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	//~ End USubsystem interface

	//~ Begin UWorldSubsystem interface
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	//~ End UWorldSubsystem interface

	// ── The zone list ───────────────────────────────────────────────────

	/**
	 * (Re)build the zone list from the `AValhallaZoneVolume` actors present.
	 *
	 * Called from `OnWorldBeginPlay` and again by `valhalla.ReloadOverlays`.
	 * Cheap and idempotent; the list is two entries.
	 */
	void DiscoverZones();

	/** Every zone, in discovery order. */
	const TArray<FValhallaZoneDef>& GetZones() const { return Zones; }

	/**
	 * The zone containing a world point, or null.
	 *
	 * XY only — see `FValhallaZoneDef::Contains2D`. Zones do not overlap by
	 * construction (they are 4096 cm boxes 40000 cm apart), so the first hit is
	 * the only hit and the order of the list does not matter.
	 */
	const FValhallaZoneDef* GetZoneAt(const FVector& WorldLocation) const;

	/** The zone with this id, or null. */
	const FValhallaZoneDef* FindZone(FName InZoneId) const;

	/**
	 * Where a player logging in should stand: the default zone's default spawn.
	 *
	 * Returns false when the world has no zone volumes at all, which is the
	 * case in `L_GreyBox` and `L_LoSTest` — both of which keep working, because
	 * the game mode falls back to the engine's own `ChoosePlayerStart`.
	 */
	bool GetDefaultSpawn(FName InZoneId, FVector& OutLocation, float& OutYaw) const;

	// ── Travel ──────────────────────────────────────────────────────────

	/**
	 * Take a pawn through a portal: teleport, re-zone, announce.
	 *
	 * The order matters and is 1.0's (`checkZoneTransitions`, GameRoom.ts:851):
	 * resolve the arrival point first, then move, then write `zoneId`, then
	 * tell everyone. Writing the id before the move would leave a one-frame
	 * window in which a `general` message went to the wrong zone.
	 *
	 * Velocity is zeroed. 1.0's players had no momentum to carry, but a UE
	 * character does, and arriving in the desert still running at 400 cm/s in
	 * the direction of a portal you can no longer see is how a player ends up
	 * back where they started.
	 *
	 * Returns false when the travel did not happen — no target zone, an entry
	 * id that names nothing, or the cooldown.
	 */
	bool TravelThroughPortal(APawn* Pawn, const AValhallaPortal* Portal);

	/** The same thing addressed by ids, for `valhalla.DebugTravel` and tests. */
	bool TravelToZone(APawn* Pawn, FName TargetZoneId, FName TargetEntryId);

	/**
	 * How long after a zone change a pawn is deaf to portals, seconds.
	 *
	 * 1.0 had no equivalent and did not need one only by luck: nothing in its
	 * four maps put an arrival point inside a return portal. One second is long
	 * enough to walk clear of a 192 cm trigger at every class's speed and short
	 * enough that a player who genuinely wants to turn round and go straight
	 * back can.
	 */
	static constexpr double TravelCooldownSeconds = 1.0;

	// ── Zone tracking ───────────────────────────────────────────────────

	/**
	 * Push every player's pawn position back into `PlayerState::ZoneId`.
	 * A step of `AValhallaGameState::ServerFixedTick`. Server only.
	 */
	void UpdatePlayerZones();

	// ── Overlays ────────────────────────────────────────────────────────

	/**
	 * Load `maps/overlays-2.0/<zone>.json` for every discovered zone, spawn
	 * what it asks for, and check the rest against what the level already has.
	 *
	 * Spawners are *created* from the overlay: an `enemy_spawn` or `npc_spawn`
	 * point becomes an `AValhallaNPCSpawner` at that zone-local coordinate. So
	 * enemy placement is data, editable in the Phase 6 editor without opening
	 * Unreal, exactly as it was in 1.0.
	 *
	 * Portals, entries and player spawns are *validated*, not created, because
	 * all three have to exist as level actors anyway — a portal needs a mesh
	 * and a box, a `PlayerStart` needs to be somewhere the navmesh reaches —
	 * and a second, invisible copy spawned from JSON beside the authored one is
	 * worse than a warning. Every mismatch is logged with both coordinates.
	 *
	 * Server only, and it does nothing at all on a client.
	 */
	void LoadOverlays();

	/** Destroy the spawners this subsystem made, then `LoadOverlays` again. */
	void ReloadOverlays();

	/** `<DataRoot>/../../maps/overlays-2.0`, absolute. */
	static FString GetOverlayDirectory();

	/** The overlay file for a zone, absolute. */
	static FString GetOverlayPath(FName InZoneId);

	/**
	 * Parse an overlay document. The whole of the 2.0 format lives here.
	 *
	 * Static and string-in, so `Valhalla.Game.Zones.OverlayParse` can pin the
	 * format without a world, a file or an editor — the same reason
	 * `ResolveDamage` takes its rolls as an argument.
	 *
	 * Never fatal on bad data, matching `UValhallaDataSubsystem`: an
	 * unparseable point is skipped with a line in `OutErrors` and the rest of
	 * the file still loads. Returns false only when the document is not an
	 * object, when `version` is not "2.0", or when `units` is present and is
	 * not "cm" — three cases where continuing would silently misplace
	 * everything.
	 */
	static bool ParseOverlay(const FString& JsonText, FValhallaZoneOverlay& OutOverlay, TArray<FString>& OutErrors);

	/** How many spawners the last `LoadOverlays` created. For the gate log. */
	int32 GetSpawnedSpawnerCount() const { return CreatedSpawners.Num(); }

protected:
	/** Turn one overlay point into an `AValhallaNPCSpawner`. */
	void SpawnFromOverlayPoint(const FValhallaZoneDef& Zone, const FValhallaOverlayPoint& Point);

	/** Check a portal / zone_entry / player_spawn point against the level. */
	void ValidateOverlayPoint(const FValhallaZoneDef& Zone, const FValhallaOverlayPoint& Point);

	/** Where a portal leads, honouring the entry-then-default-spawn precedence. */
	bool ResolveArrival(FName TargetZoneId, FName TargetEntryId, FVector& OutLocation, float& OutYaw) const;

private:
	/** Discovered zones. Rebuilt by `DiscoverZones`, never edited piecemeal. */
	TArray<FValhallaZoneDef> Zones;

	/** Last zone-change time per pawn, against `UWorld::GetTimeSeconds`. */
	TMap<TWeakObjectPtr<APawn>, double> LastTravelTime;

	/** Spawners this subsystem created, so `ReloadOverlays` can undo itself. */
	TArray<TWeakObjectPtr<AActor>> CreatedSpawners;

	/** True once `LoadOverlays` has run, so a reload knows it is a reload. */
	bool bOverlaysLoaded = false;
};
