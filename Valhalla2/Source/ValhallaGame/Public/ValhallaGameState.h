// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "ValhallaGameTypes.h"
#include "ValhallaGameState.generated.h"

class APlayerState;

/**
 * B-27 Phase 3: how one combat event reaches one player.
 *
 *   Guaranteed — the player is part of it, or a party member in the same zone
 *                is: the combat log is written from these, so they go on the
 *                reliable batch.
 *   Seen       — it names an actor that exists on the player's client (the
 *                replication system has it on their connection): the swing,
 *                the flinch and the spell effect, on the unreliable batch.
 *   Skip       — anything else. A fight the player cannot see costs them nothing.
 */
enum class EValhallaCombatEventRoute : uint8
{
	Skip,
	Seen,
	Guaranteed,
};

/** The router's view of one player receiving events. Server only; plain data so the rule is testable. */
struct FValhallaCombatEventViewer
{
	const AActor* Pawn = nullptr;
	const APlayerState* PlayerState = nullptr;
	FName ZoneId;
	/** 0: no party. */
	int32 PartyId = 0;
	/** B-24's loading screen: only the player's own and their party's guaranteed events. */
	bool bInTransit = false;
};

/** The router's view of one actor an event names (its Target or its Instigator). */
struct FValhallaCombatEventActor
{
	const AActor* Actor = nullptr;
	/** The actor's player, null for an NPC. */
	const APlayerState* PlayerState = nullptr;
	/** The actor's player's zone and party (None / 0 for an NPC). */
	FName ZoneId;
	int32 PartyId = 0;
	/** The actor is on the viewer's client. Per viewer. */
	bool bOnClient = false;
};

/**
 * The port of the parts of `GameRoom` that are world state rather than rules:
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
	 * Phase 8b: queued, then sent once per server frame as one *reliable*
	 * batch (MulticastCombatEvents). Until 8b every event was its own
	 * unreliable multicast, and Unreal sends an actor's unreliable multicasts
	 * only when that actor replicates — for this game state 10 times a second
	 * — and drops every call to the same RPC past `net.MaxRPCPerNetUpdate`
	 * (default 2) within one of those windows. An instant cast raises
	 * skillStarted, the handler's own events and skillEffect in the same
	 * frame, so its third event onward never left the server; that is why
	 * frost nova, mana shield and every warrior skill had no client-side
	 * skillEffect while a 2.5 s fireball, whose events are 2.5 s apart, did.
	 * One reliable batch per frame is one RPC call per frame, is never
	 * dropped, and keeps the events in the order the server raised them.
	 *
	 * B-27 Phase 3: no longer a multicast. FlushCombatEvents records the batch
	 * on the server once and builds one batch per remote player
	 * (RouteCombatEvent): the events they must have on the reliable
	 * AValhallaPlayerController::ClientCombatEvents, the ones they can see on
	 * the unreliable ClientCombatEventsSeen, nothing from fights they cannot
	 * see. Each is still one RPC per frame, in the order the server raised them.
	 *
	 * Called on a client (a listen-less standalone test), records locally.
	 */
	void QueueCombatEvent(const FValhallaCombatEvent& Event);

	/**
	 * B-27 Phase 3: where one event goes for one player. Pure; tested
	 * (Valhalla.Game.CombatPerf.Routing). `EventZone` is the zone at the event's
	 * Location, used only when it names no actor at all.
	 */
	static EValhallaCombatEventRoute RouteCombatEvent(const FValhallaCombatEventViewer& Viewer,
		const FValhallaCombatEventActor& Target, const FValhallaCombatEventActor& Instigator, FName EventZone);

	/** Client: a batch from the server (either RPC). Records every event, counts them for the CSV profiler. */
	void ReceiveCombatEvents(const TArray<FValhallaCombatEvent>& Events);

	/** Fired on every end for every recorded event. The HUD's combat log and floaters listen here. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnValhallaCombatEvent, const FValhallaCombatEvent&);
	FOnValhallaCombatEvent OnCombatEvent;

	/**
	 * The last few events, newest last, with the time each arrived. Client-side
	 * presentation state — the HUD reads it to draw floating numbers and a
	 * combat log. Never authoritative and never read by a rule.
	 */
	const TArray<TPair<double, FValhallaCombatEvent>>& GetRecentEvents() const { return RecentEvents; }

	/** Record an event locally. Called by the flush on the server, the batch RPCs and caster-private RPCs. */
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

	/**
	 * Bumped by the server every time `AValhallaGameMode::ReloadGameData`
	 * succeeds, so connected clients reload their own copy of the data too.
	 *
	 * The server's reload only ever touched the server's game instance: a
	 * client kept the item names, skill numbers and equipment meshes it loaded
	 * at startup until it restarted, so a tooltip or a paperdoll disagreed with
	 * the server after every edit. Clients read the same files from their own
	 * data root (`UValhallaDataSettings::DataRoot`), which on a dev machine is
	 * the same folder the server reads.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_DataVersion)
	int32 DataVersion = 0;

	/** Server: announce a successful data reload to every client. */
	void BumpDataVersion();

protected:
	/** Client: reload the data subsystem and redraw what depends on it. */
	UFUNCTION()
	void OnRep_DataVersion();

	/** The reload and redraw half of OnRep_DataVersion, after any download. */
	void ApplyClientDataReload();

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
	 * Stats the files `UValhallaDataSubsystem` reads, and when a timestamp
	 * moves runs the same thing the `/reload-data` HTTP route runs —
	 * `AValhallaGameMode::ReloadGameData`. Two front doors, one
	 * implementation, as ever.
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

	/** Events raised this frame, sent by FlushCombatEvents at the end of Tick. Server only. */
	TArray<FValhallaCombatEvent> PendingCombatEvents;

	/** Record PendingCombatEvents here and send each remote player their batch. */
	void FlushCombatEvents();

	/** B-27 Phase 3: each remote player's guaranteed and seen batches (RouteCombatEvent). */
	void RouteCombatEvents(const TArray<FValhallaCombatEvent>& Batch);

	/**
	 * Client only: mirror a cooldown locally when our own skillEffect arrives.
	 *
	 * The cooldown table is server-only by design (AValhallaPlayerState's class
	 * comment), so a client's action bar had no way to draw a sweep. The
	 * skillEffect event says exactly when a cast completed, which is when the
	 * server started its cooldown; ApplyCooldown on the client's otherwise
	 * unused copy of the table puts the same expiry there, one network trip
	 * late. It is presentation only — the server never reads the client's copy.
	 */
	void MirrorCooldownFromEvent(const FValhallaCombatEvent& Event);

	/** Hard cap on RecentEvents, in case a fight outpaces the trim. */
	static constexpr int32 MaxRecentEvents = 64;
};
