// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "ValhallaVisibilitySubsystem.generated.h"

/** Line of sight, relevancy culling and the client's fog of war. */
VALHALLAGAME_API DECLARE_LOG_CATEGORY_EXTERN(LogValhallaVision, Log, All);

/**
 * The channel every line-of-sight question is asked on.
 *
 * A dedicated trace channel rather than ECC_Visibility, because the two
 * questions are different and the answers must be allowed to differ: the mouse
 * cursor should hit a tree, and a tree should not hide an enemy behind it
 * (Phase 5 decision 6 — players see through trunks). Only meshes carrying the
 * `VisionBlocker` collision *profile* block it, and the channel's default
 * response is Ignore, which is what makes every other profile in the project —
 * BlockAll floors, the character capsules, the props — transparent to it
 * without being touched.
 *
 * Declared in `Config/DefaultEngine.ini` under `[/Script/Engine.CollisionProfile]`.
 * The two must agree: if the ini entry is ever renumbered this constant moves
 * with it, and nothing else in the project names ECC_GameTraceChannel1.
 */
inline constexpr ECollisionChannel ValhallaVisionBlockerChannel = ECC_GameTraceChannel1;

/**
 * How far above an actor's origin the line-of-sight trace starts and ends, cm.
 *
 * Both ends are raised by the same amount, so the trace is a horizontal line
 * roughly at head height of a 120 cm character standing on the floor. Tracing
 * origin to origin instead would run along the ground and be stopped by the
 * first kerb; tracing eye to *feet* would let a player see an enemy's toes
 * around a corner and call that vision.
 */
inline constexpr float ValhallaEyeHeight = 90.f;

/** One 2D wall edge, in world XY centimetres. */
struct FValhallaVisibilitySegment
{
	FVector2D A = FVector2D::ZeroVector;
	FVector2D B = FVector2D::ZeroVector;

	FValhallaVisibilitySegment() = default;
	FValhallaVisibilitySegment(const FVector2D& InA, const FVector2D& InB)
		: A(InA), B(InB)
	{
	}
};

/**
 * The 1.0 visibility polygon, ported.
 *
 * `VisibilitySystem.computeVisibilityPolygon` (server/src/systems/VisibilitySystem.ts:124)
 * with the cone removed: 2.0's camera is a top-down isometric one that shows
 * the character's whole surroundings, so a 180-degree wedge that swung with the
 * cursor would mean half the screen went dark whenever the player looked away
 * from it. Everything else is the 1.0 algorithm step for step — rays to every
 * segment endpoint at the angle and at the angle plus and minus an epsilon (so
 * a ray can slip past a corner and light what is behind it), a fixed number of
 * rays around the arc so the open-field boundary is a circle rather than a
 * square, closest hit wins, results clamped to the vision radius and sorted by
 * angle.
 *
 * Deliberately free functions on plain 2D data with no world, no actor and no
 * subsystem: that is what lets `Valhalla.Game.Visibility.Polygon` run them in
 * the commandlet context, and it is the same discipline the Phase 1 stat math
 * and the Phase 2b combat rules are held to.
 */
namespace ValhallaVisibility
{
	/** 1.0's `epsilon` (VisibilitySystem.ts:202). The peek-around-a-corner offset. */
	inline constexpr double RayEpsilon = 1e-5;

	/**
	 * 1.0's `arcSteps`, quadrupled because 1.0 only ever swept a 180-degree cone
	 * with them and this sweeps the full circle. 64 rays put a vertex every 5.6
	 * degrees, which at a 1800 cm ranger radius is a 176 cm chord — under three
	 * tiles, and invisible once the fog edge is blurred.
	 */
	inline constexpr int32 DefaultArcRays = 64;

	/** Fold an angle into [-PI, PI]. VisibilitySystem.ts:33 `normaliseAngle`. */
	VALHALLAGAME_API double NormaliseAngle(double Angle);

	/**
	 * The polygon visible from Origin, in world XY, sorted by angle.
	 *
	 * @param Segments  Wall edges. Anything further than 1.5 * Radius from the
	 *                  origin at both ends is dropped, exactly as 1.0 does.
	 * @param Radius    The viewer's vision range, cm.
	 * @param ArcRays   Rays spread evenly around the circle. See DefaultArcRays.
	 */
	VALHALLAGAME_API TArray<FVector2D> ComputeVisibilityPolygon(
		const FVector2D& Origin,
		TArrayView<const FValhallaVisibilitySegment> Segments,
		double Radius,
		int32 ArcRays = DefaultArcRays);

	/** VisibilitySystem.ts:311 `isPointVisible` — even-odd ray casting. */
	VALHALLAGAME_API bool IsPointInPolygon(TArrayView<const FVector2D> Polygon, const FVector2D& Point);
}

/**
 * One ordered (viewer, target) pair.
 *
 * Ordered, because visibility is not symmetric: the range used is the
 * *viewer's*, so a ranger can see a warrior who cannot see them back.
 *
 * FObjectKey rather than a raw pointer, because an entry can outlive the actor
 * it names — an NPC dies with an answer about it still in the map — and a
 * freshly spawned actor can be handed the recycled index. An FObjectKey
 * compares the index and the serial number, so the recycled object is a
 * different key and simply misses the cache.
 */
struct FValhallaVisibilityKey
{
	FObjectKey Viewer;
	FObjectKey Target;

	FValhallaVisibilityKey() = default;

	FValhallaVisibilityKey(const UObject* InViewer, const UObject* InTarget)
		: Viewer(InViewer)
		, Target(InTarget)
	{
	}

	bool operator==(const FValhallaVisibilityKey& Other) const
	{
		return Viewer == Other.Viewer && Target == Other.Target;
	}
};

FORCEINLINE uint32 GetTypeHash(const FValhallaVisibilityKey& Key)
{
	return HashCombine(GetTypeHash(Key.Viewer), GetTypeHash(Key.Target));
}

/** One cached answer. Small enough to live by value in the map. */
struct FValhallaVisibilityCacheEntry
{
	/** The subsystem tick at which this answer stops being trusted. */
	uint64 ExpiryTick = 0;

	bool bVisible = false;
};

/**
 * The server's authoritative line-of-sight oracle, and the client's source of
 * the same geometry for the fog.
 *
 * This is an anti-cheat boundary, not a rendering nicety (PLAN.md, Phase 5).
 * `IsNetRelevantFor` on the four replicated gameplay actors asks this and
 * nothing else, so an actor a player cannot see is never put on that player's
 * connection at all — its position, its name and its health never leave the
 * server. A modified client cannot draw what it was not sent.
 *
 * Two things make that affordable. The distance test comes first and is a
 * squared 2D compare, so the overwhelming majority of pairs never trace. The
 * survivors are cached per ordered (viewer, target) pair for CacheTicks server
 * ticks, keyed on FObjectKey rather than on a raw pointer so that an actor
 * destroyed while its answer is still in the map cannot be resurrected by a
 * stale key — an FObjectKey compares the object's index *and* its serial
 * number, and a recycled index gets a new serial.
 */
UCLASS(Config = Game)
class VALHALLAGAME_API UValhallaVisibilitySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem interface
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem interface

	//~ Begin FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject interface

	/** The subsystem for a world, or null. */
	static UValhallaVisibilitySubsystem* Get(const UObject* WorldContextObject);

	/**
	 * Can Viewer see Target right now?
	 *
	 * True when the two are within the *viewer's* vision range measured in 2D —
	 * a ranger on a rise does not see further than a ranger on the flat — and
	 * nothing carrying the VisionBlocker profile stands between their eyes.
	 *
	 * @param Now  The world time the caller is reasoning about, used only to
	 *             drive the once-a-second prune. Answers expire in ticks, not
	 *             seconds, because relevancy is evaluated per server frame.
	 */
	bool IsVisibleFrom(const AActor* Viewer, const AActor* Target, double Now);

	/** IsVisibleFrom against this world's current time. */
	bool IsVisibleFrom(const AActor* Viewer, const AActor* Target);

	/**
	 * The shared body of every `IsNetRelevantFor` override in the project.
	 *
	 * Three rules, in order:
	 *
	 *   1. A player is always sent themselves, their own pawn and anything they
	 *      own. Culling a client's own character would make them unable to play.
	 *   2. A party member's pawn is always sent. 1.0 had no such rule because it
	 *      had no culling; 2.0 needs one, because a party whose members vanish
	 *      from each other's screens behind every wall cannot be a party. Note
	 *      that this is the pawn only — the NPC standing next to a party member
	 *      is still culled, which is what gate (d) checks.
	 *   3. Everything else is line of sight.
	 *
	 * Fails *open* — relevant — whenever it cannot answer: no world, no
	 * subsystem, no viewer pawn yet. A missing answer that hides an actor would
	 * look like a replication bug and be debugged as one; a missing answer that
	 * shows one looks like Phase 4.
	 */
	static bool IsRelevantForViewer(const AActor* Self, const AActor* RealViewer, const AActor* ViewTarget);

	/**
	 * The vision range of whoever is doing the looking, cm.
	 *
	 * `AValhallaPlayerState::VisionRange`, which Phase 2c copied out of
	 * `classes.json` — 1200 for most, 1350 rogue, 1800 ranger. An actor with no
	 * player state (an NPC asking about a player, say) gets the 1200 default
	 * rather than infinite sight.
	 *
	 * B-06: capped by the player's zone's `atmosphere.netRelevancyRadiusCm`
	 * when the zone sets one (never raised). This is the per-zone relevancy
	 * radius. It is applied here rather than through NetCullDistanceSquared,
	 * which every culled actor sets to 1e12 on purpose (see AValhallaCharacter's
	 * constructor): that one is a 3D radius from the connection's view point,
	 * applied before IsNetRelevantFor, and would cull party members too.
	 */
	static float GetVisionRangeFor(const AActor* Viewer);

	/** The class default when `classes.json` has nothing to say. */
	static constexpr float DefaultVisionRange = 1200.f;

	/**
	 * How many server ticks one traced answer is trusted for.
	 *
	 * Four at 60 Hz is 67 ms, which is under one net update at the 20 Hz an NPC
	 * replicates at, so caching cannot make an actor appear or disappear a whole
	 * update late. Raising it trades responsiveness at a corner for traces.
	 */
	UPROPERTY(Config)
	int32 CacheTicks = 4;

	/** How often the expired half of the cache is swept out, seconds. */
	UPROPERTY(Config)
	float CachePruneIntervalSeconds = 1.f;

	/** How often `valhalla.VisibilityStats 1` prints, seconds. */
	UPROPERTY(Config)
	float StatsIntervalSeconds = 5.f;

	/** Traces run since the process started. Read by the tests and the stats line. */
	int64 GetTotalTraces() const { return TotalTraces; }

private:
	/** Drop every entry whose answer has expired. */
	void PruneCache();

	/** The `valhalla.VisibilityStats 1` line. */
	void ReportStats(double Now);

	TMap<FValhallaVisibilityKey, FValhallaVisibilityCacheEntry> Cache;

	/** Ticks since Initialize. The unit CacheTicks is measured in. */
	uint64 TickCounter = 0;

	/** World time of the last prune and the last stats line. */
	double LastPruneTime = 0.0;
	double LastStatsTime = 0.0;

	/** Since the last stats line. */
	int64 WindowTraces = 0;
	int64 WindowCacheHits = 0;
	int64 WindowRangeRejects = 0;

	int64 TotalTraces = 0;
};
