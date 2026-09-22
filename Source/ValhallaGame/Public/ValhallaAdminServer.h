// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ValhallaZoneTypes.h"
#include "ValhallaAdminServer.generated.h"

class FJsonObject;
class IHttpRouter;
struct FHttpRouteHandleInternal;
struct FHttpServerRequest;

/**
 * Whether the admin API is compiled in at all.
 *
 * Set by ValhallaGame.Build.cs: 1 for the Editor and Server targets, 0 for the
 * packaged Game target. This is deliberately *not* `#if !UE_SERVER`, which is
 * the trap this module avoided: `UE_SERVER` is about which of the client and
 * server halves of a shared build you are, and it is 0 in the editor — so
 * `#if !UE_SERVER` would compile the admin API into every shipped *client* and
 * out of the editor, which is exactly backwards. What the API actually needs is
 * "a build a developer runs", which is a target question, and the runtime half
 * of it is authority, which every handler checks.
 */
#ifndef WITH_VALHALLA_ADMIN_API
#define WITH_VALHALLA_ADMIN_API 0
#endif

#if WITH_VALHALLA_ADMIN_API
#include "HttpResultCallback.h"
#include "HttpServerConstants.h"
#include "HttpServerResponse.h"
#endif

VALHALLAGAME_API DECLARE_LOG_CATEGORY_EXTERN(LogValhallaAdmin, Log, All);

// ─────────────────────────────────────────────────────────────────────────────
//  The snapshot — a plain description of the world, with no world in it
// ─────────────────────────────────────────────────────────────────────────────
//
// `GET /api/admin/state` is two jobs: walk the live world, and turn what you
// found into the JSON document the 1.0 dashboard expects. Splitting them at
// this struct is what makes the second one testable — `BuildStateJson` takes
// one of these and nothing else, so `Valhalla.Game.Admin.StateJson` can pin the
// wire format without a world, a net driver or a PIE session. It is the same
// split `ResolveDamage` and its rolls have had since Phase 1a.
//
// Plain structs, not USTRUCTs: nothing here is replicated, held by a UObject or
// seen by Blueprints, and reflecting it would only add a reason for it to drift
// from the TypeScript interface it mirrors.

/** One occupied slot of a loot bag. 1.0: `{ itemId, quantity }`. */
struct VALHALLAGAME_API FValhallaAdminItemStack
{
	FString ItemId;
	int32 Quantity = 0;
};

/** One connected player, as the dashboard draws them. */
struct VALHALLAGAME_API FValhallaAdminPlayerInfo
{
	/** `APlayerState::GetPlayerId()` as a string — 2.0's session id. */
	FString SessionId;
	FString Name;
	FString ClassId;
	int32 Level = 1;
	/** Zone-local centimetres. See UValhallaAdminServer::ToZoneLocalCm. */
	double X = 0.0;
	double Y = 0.0;
	double Hp = 0.0;
	double MaxHp = 0.0;
	double Mana = 0.0;
	double MaxMana = 0.0;
	bool bAlive = true;
};

/** One NPC actor. */
struct VALHALLAGAME_API FValhallaAdminNpcInfo
{
	/** `AActor::GetName()` — 2.0's npc id. Unique within the world and stable. */
	FString Id;
	FString TemplateId;
	FString Name;
	/** "enemy" or "npc", the 1.0 spellings of `NPCTemplate.type`. */
	FString NpcType;
	int32 Level = 1;
	double X = 0.0;
	double Y = 0.0;
	double Hp = 0.0;
	double MaxHp = 0.0;
	bool bAlive = true;
};

/** One loot bag on the ground. */
struct VALHALLAGAME_API FValhallaAdminLootBagInfo
{
	/** `AActor::GetName()`. */
	FString Id;
	double X = 0.0;
	double Y = 0.0;
	TArray<FValhallaAdminItemStack> Items;
};

/** Everything in one zone. */
struct VALHALLAGAME_API FValhallaAdminZoneSnapshot
{
	TArray<FValhallaAdminPlayerInfo> Players;
	TArray<FValhallaAdminNpcInfo> Npcs;
	TArray<FValhallaAdminLootBagInfo> LootBags;
};

/** The whole of `GET /state`, before it is JSON. */
struct VALHALLAGAME_API FValhallaAdminSnapshot
{
	/**
	 * 1.0's `online` — whether there is a game room at all.
	 *
	 * False produces the empty document 1.0 returned when `getActiveGameRoom()`
	 * was null, which is what the dashboard's "server offline" state reads. In
	 * 2.0 that is "this world has no authority", i.e. the API is answering from
	 * a client world, which should not happen but must not lie if it does.
	 */
	bool bOnline = false;

	/** Zone id -> its contents. Zones with nothing in them are still listed. */
	TMap<FString, FValhallaAdminZoneSnapshot> Zones;
};

// ─────────────────────────────────────────────────────────────────────────────

/**
 * The 1.0 admin REST API (`server/src/routes/admin.ts`), served by the running
 * 2.0 game server so the existing React editor's Live Dashboard keeps working.
 *
 * ## Why the same contract, verbatim
 *
 * The dashboard is a finished piece of software that already knows how to draw
 * a zone, place an NPC by right-clicking a map and teleport a player. Changing
 * its wire format would mean porting it too, for no gain: the shapes below are
 * `admin.ts`'s, field for field and status code for status code, and where 2.0
 * has nothing to say (1.0's `spriteColor`, `spriteSize`) the field is simply
 * absent rather than invented.
 *
 * Two routes are new, and both exist because 2.0 has something 1.0 did not: a
 * running process that has already read the JSON. `POST /reload-data` re-reads
 * the seven data files and re-resolves everything that cached them;
 * `POST /reload-overlays` re-reads `maps/overlays-2.0/*.json` and rebuilds the
 * spawners. Together they are what lets a designer save in the editor and see
 * the change without leaving PIE.
 *
 * ## Coordinates
 *
 * 1.0's x/y were map pixels with the map's origin at the top-left. 2.0's are
 * *zone-local centimetres*: the zone volume's min corner is (0, 0) and a
 * centimetre is a 1.0 pixel (PLAN.md, Phase 3). Everything crossing this
 * boundary is converted, in both directions, by `ToZoneLocalCm` and
 * `FromZoneLocalCm` and by nothing else — which matters because `L_Desert` is
 * loaded at world X = +40000, so a desert coordinate that skipped the
 * conversion would be forty metres of nonsense rather than an obvious error.
 *
 * ## Threading
 *
 * Every handler runs on the game thread. That is not something this class
 * arranges — `FHttpServerModule` is an `FTSTickerObjectBase`, so it reads
 * sockets and dispatches handlers from its tick, which is the game thread's —
 * but it is something the handlers rely on: they walk `TActorIterator`, destroy
 * actors and teleport pawns, none of which would survive being called from a
 * socket thread. If the engine ever moves that dispatch, every handler here
 * needs an `AsyncTask(ENamedThreads::GameThread, ...)` hop and this comment
 * needs deleting.
 *
 * ## Security
 *
 * None, on purpose, and only two things make that acceptable: the listener
 * binds to loopback (`FHttpServerConfig`'s `DefaultBindAddress` is
 * `localhost`), and the API is compiled out of a packaged Game target
 * (`WITH_VALHALLA_ADMIN_API`). Phase 7 adds a token — see `Authorize` for the
 * one place a check goes.
 */
UCLASS()
class VALHALLAGAME_API UValhallaAdminServer : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UValhallaAdminServer();
	virtual ~UValhallaAdminServer() override;

	//~ Begin USubsystem interface
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	//~ End USubsystem interface

	// ── Lifetime ────────────────────────────────────────────────────────

	/**
	 * Bind every route and start listening. Called by `AValhallaGameMode::InitGame`.
	 *
	 * Refuses, with a log line and no error, when the API is compiled out, when
	 * `UValhallaDataSettings::bEnableAdminApi` is false, or when this world has
	 * no authority — a listen-server client world gets a subsystem too, and
	 * exactly one of the two must own the port.
	 */
	void Start();

	/** Unbind every route and stop listening. Idempotent. */
	void Stop();

	/** True between a successful Start and a Stop. */
	bool IsRunning() const { return bRunning; }

	/** The port Start bound, or 0. */
	int32 GetBoundPort() const { return BoundPort; }

	/** `UValhallaDataSettings::AdminApiPort`, or 2568 if the settings are missing. */
	static int32 GetConfiguredPort();

	// ── The snapshot ────────────────────────────────────────────────────

	/**
	 * Walk the world into a snapshot. Game thread, authority only.
	 *
	 * Every zone the zone subsystem knows about is listed even when it is empty,
	 * which is one deliberate difference from 1.0: `admin.ts` built its `zones`
	 * map lazily from the entities it found, so a zone nobody was standing in
	 * did not exist as far as the dashboard was concerned and its map could not
	 * be opened. Listing them makes the zone picker stable.
	 *
	 * An NPC or bag outside every zone box is filed under "unknown", which is
	 * the fallback 1.0 used for an entity with no `zoneId`.
	 */
	void BuildSnapshot(FValhallaAdminSnapshot& OutSnapshot) const;

	/**
	 * The snapshot as the document `GET /api/admin/state` returns.
	 *
	 * Pure, static and world-free — see the snapshot section's comment. Numbers
	 * that 1.0 rounded (`Math.round`) are rounded here, because the dashboard
	 * prints them straight into a tooltip.
	 */
	static TSharedRef<FJsonObject> BuildStateJson(const FValhallaAdminSnapshot& Snapshot);

	// ── Coordinates ─────────────────────────────────────────────────────

	/**
	 * World position -> zone-local centimetres, the API's coordinate system.
	 *
	 * Thin by design: the arithmetic is `FValhallaZoneDef::ToZoneLocal`, which
	 * is where it has been since Phase 3, and this exists so that the admin
	 * layer has one named place to point at (and one for
	 * `Valhalla.Game.Admin.CmConversion` to test) rather than subtracting
	 * `Bounds.Min` in eight handlers.
	 */
	static FVector2D ToZoneLocalCm(const FValhallaZoneDef& Zone, const FVector& WorldLocation);

	/**
	 * Zone-local centimetres -> world position. The exact inverse of the above
	 * in XY; Z is the zone box's floor plus `ZOffsetCm`, because the API is 2D
	 * and something has to choose a height.
	 */
	static FVector FromZoneLocalCm(const FValhallaZoneDef& Zone, double LocalX, double LocalY, double ZOffsetCm);

	/**
	 * How far above a zone's floor an admin-placed thing is put, cm.
	 *
	 * The same 8 cm `UValhallaZoneSubsystem::SpawnFromOverlayPoint` lifts an
	 * overlay spawner by, for the same reason: a capsule or a bag whose origin
	 * is exactly on the floor plane starts the frame intersecting it.
	 */
	static constexpr double PlacementZOffsetCm = 8.0;

protected:
#if WITH_VALHALLA_ADMIN_API
	// ── Route handlers ──────────────────────────────────────────────────
	//
	// Each returns true and invokes OnComplete exactly once, which is the
	// FHttpRequestHandler contract (returning false means "never completing").

	bool HandleState(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSpawnNpc(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleDropItem(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleKickPlayer(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleTeleportPlayer(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleKillNpc(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleRespawnNpc(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleDeleteNpc(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleReloadOverlays(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleReloadData(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/**
	 * Phase 7's hook, and today a no-op that returns true.
	 *
	 * Every handler calls this first, which is the point — ten handlers each
	 * remembering to check is nine chances to forget.
	 *
	 * **Phase 7b filled it in.** When `UValhallaDataSettings::bAdminApiRequireSecret`
	 * is set, a request must carry `Authorization: Bearer <ServerSecret>`; the
	 * comparison is constant-time over the whole secret, because a byte
	 * compare that returns early tells a caller how much of its guess was
	 * right. The flag defaults *off* in the Editor and *on* in a Server build,
	 * for the reason the setting's own comment gives: the 1.0 React dashboard
	 * reaches 2568 through `editor/src/server.ts`'s proxy, and that proxy does
	 * not yet send the header. Teaching it to is Phase 8's, and until then
	 * turning the flag on in the Editor turns the dashboard off.
	 */
	bool Authorize(const FHttpServerRequest& Request, TUniquePtr<FHttpServerResponse>& OutResponse) const;

	/** Bind one route, remember its handle, and log it. False if the path was taken. */
	bool BindRoute(const FString& Path, uint16 Verbs, bool (UValhallaAdminServer::*Handler)(const FHttpServerRequest&, const FHttpResultCallback&));
#endif // WITH_VALHALLA_ADMIN_API

	/**
	 * The one instance in this process that owns the port, if any.
	 *
	 * There is exactly one admin API per process, and it is whichever world
	 * claimed it first. The case this exists for is not theoretical and is not
	 * rare: `kick-player` sends a PIE client back to a standalone world of its
	 * own, that world has a game mode, and its `InitGame` calls `Start` — so
	 * without this, kicking somebody prints ten "failed to bind route" errors
	 * and leaves a second subsystem believing it is serving.
	 *
	 * Weak, so a world that ends releases the claim without anything having to
	 * remember to.
	 */
	static TWeakObjectPtr<UValhallaAdminServer> ActiveInstance;

private:
#if WITH_VALHALLA_ADMIN_API
	/** The router for BoundPort. Shared with anything else on the same port. */
	TSharedPtr<IHttpRouter> Router;

	/** Every handle BindRoute produced, so Stop can give them all back. */
	TArray<TSharedPtr<const FHttpRouteHandleInternal>> RouteHandles;
#endif

	/** See IsRunning. */
	bool bRunning = false;

	/** See GetBoundPort. */
	int32 BoundPort = 0;
};
