// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "ValhallaGameTypes.h"
#include "ValhallaGameState.generated.h"

/**
 * The port of the parts of `GameRoom` that are world state rather than rules:
 * the authoritative clock and the zone every connected player is in.
 *
 * It also owns the fixed-rate server step. GameRoom.update ran on Colyseus's
 * `setSimulationInterval` at SERVER_TICK_RATE (60 Hz); an Unreal actor ticks
 * once per rendered frame, which on a listen server is whatever the GPU
 * manages. Accumulating into fixed Valhalla::ServerTickMs steps keeps every
 * ported per-tick rate (regen now; DoT/HoT and cast progression in Phase 2b)
 * numerically identical to 1.0 regardless of frame rate.
 */
UCLASS()
class VALHALLAGAME_API AValhallaGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AValhallaGameState();

	//~ Begin AActor interface
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;
	//~ End AActor interface

	/**
	 * Seconds since the match began, authored by the server.
	 *
	 * This is the clock every timestamp in the game is measured against —
	 * skill cooldown expiry, buff expiry, the invulnerability window. 1.0 used
	 * Date.now() milliseconds, which no client could trust; this replicates, so
	 * a client can render a cooldown sweep against the same number the server
	 * will judge the cast with.
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|World")
	double ServerTime = 0.0;

	/** The zone this server instance is hosting. Phase 3 makes it meaningful. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|World")
	FName ZoneId = TEXT("grasslands");

	/** ServerTime, for anything timing itself against the authoritative clock. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|World")
	double GetServerTime() const { return ServerTime; }

	// ── Combat events ───────────────────────────────────────────────────

	/**
	 * Tell every client that something happened. The 2.0 replacement for
	 * GameRoom's `this.broadcast(...)` calls (GameRoom.ts:939-1141).
	 *
	 * Unreliable on purpose: these are presentation, not state. A dropped
	 * floating damage number is invisible; a reliable channel backed up behind
	 * fourteen of them in a busy fight is not. Everything that must be true on
	 * the client already replicates as a property.
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastCombatEvent(const FValhallaCombatEvent& Event);

	/**
	 * The last few events, newest last, with the time each arrived. Client-side
	 * presentation state — the HUD reads it to draw floating numbers and a
	 * combat log. Never authoritative and never read by a rule.
	 */
	const TArray<TPair<double, FValhallaCombatEvent>>& GetRecentEvents() const { return RecentEvents; }

	/** Record an event locally. Called by the multicast and by caster-private RPCs. */
	void RecordCombatEvent(const FValhallaCombatEvent& Event);

	/** How long a floating number stays up, seconds. */
	static constexpr float CombatEventLifetimeSeconds = 1.f;

	// ── Phase 6b: data hot reload ───────────────────────────────────────

	/**
	 * How often the watcher stats the files it is watching, seconds.
	 *
	 * Two, because that is slow enough to be free — twelve `stat` calls every
	 * two seconds is nothing next to a 60 Hz fixed tick — and fast enough that
	 * a designer who hits Save in the browser sees the change before they have
	 * finished tabbing back to the game. A file watcher (IDirectoryWatcher)
	 * would be event-driven and is deliberately not used: it needs the editor's
	 * DirectoryWatcher module, which a packaged dedicated server does not have,
	 * and polling twelve paths is the kind of cost that never needs optimising.
	 */
	static constexpr double DataWatchIntervalSeconds = 2.0;

protected:
	/**
	 * One fixed 1/60 s step of the server simulation, in `GameRoom.update`'s
	 * order: spell projectiles, then skills and auto-attacks, then respawns,
	 * then NPCs. The order is load-bearing — an NPC that moves before the
	 * projectile step would dodge a fireball that had already reached it.
	 */
	virtual void ServerFixedTick(float FixedDeltaSeconds);

#if !UE_BUILD_SHIPPING
	/**
	 * Phase 6's "file watcher on DataRoot", as a poll.
	 *
	 * Stats the seven files `UValhallaDataSubsystem` reads and every
	 * `maps/overlays-2.0/*.json`, and when a timestamp moves runs the same
	 * thing the matching HTTP route runs — `AValhallaGameMode::ReloadGameData`
	 * for the data files, `UValhallaZoneSubsystem::ReloadOverlays` for the
	 * overlays. Two front doors, one implementation, as ever.
	 *
	 * The first pass only records timestamps: a server that hot-reloaded
	 * everything two seconds after booting because it had nothing to compare
	 * against would be indistinguishable from a real edit, and would do it on
	 * every launch.
	 *
	 * Server only, non-shipping only, and off entirely when
	 * `valhalla.DataHotReload` is 0.
	 */
	void TickDataWatcher(float DeltaSeconds);

	/** Timestamps as of the last poll, by absolute path. Empty until the first. */
	TMap<FString, FDateTime> WatchedTimestamps;

	/** Real seconds since the last poll. Not the fixed-step accumulator. */
	double DataWatchAccumulator = 0.0;

	/** False until the first poll has recorded a baseline. */
	bool bDataWatchBaselineTaken = false;
#endif

private:
	/** Leftover real time not yet consumed by a fixed step. */
	double TickAccumulator = 0.0;

	/** See GetRecentEvents. Trimmed by age every time one is added. */
	TArray<TPair<double, FValhallaCombatEvent>> RecentEvents;

	/** Hard cap on RecentEvents, in case a fight outpaces the trim. */
	static constexpr int32 MaxRecentEvents = 64;
};
