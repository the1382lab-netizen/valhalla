// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaVisibilitySubsystem.h"

#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"
#include "ValhallaCharacter.h"
#include "ValhallaLootBag.h"
#include "ValhallaNPC.h"
#include "ValhallaPartySubsystem.h"
#include "ValhallaPlayerState.h"
#include "ValhallaSpellProjectile.h"
#include "ValhallaZoneAtmosphere.h"
#include "WorldCollision.h"

DEFINE_LOG_CATEGORY(LogValhallaVision);

namespace
{
	/**
	 * `valhalla.VisibilityStats 1` — print a line every StatsIntervalSeconds
	 * with the trace rate and the cache hit rate.
	 *
	 * Off by default and read once per tick, because the thing it measures is
	 * the thing Phase 5 is most likely to get wrong: a cache that stops working
	 * does not break the game, it just quietly multiplies the trace count by
	 * sixty and shows up a month later as a frame time regression.
	 */
	TAutoConsoleVariable<int32> CVarVisibilityStats(
		TEXT("valhalla.VisibilityStats"),
		0,
		TEXT("Dev only. 1 prints the line-of-sight trace and cache-hit rate every few seconds."),
		ECVF_Default);
}

// ─────────────────────────────────────────────────────────────────────────────
//  The 2D polygon — a port of VisibilitySystem.ts, minus the cone
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	/** One ray's closest hit. 1.0's `Intersection`. */
	struct FValhallaRayHit
	{
		FVector2D Point = FVector2D::ZeroVector;
		double Param = 0.0;
		double Angle = 0.0;
	};

	/**
	 * VisibilitySystem.ts:281 `raySegmentIntersect` — with its sign corrected.
	 *
	 * This is the one place in the port that deliberately does not reproduce
	 * 1.0, and it is worth being precise about why.
	 *
	 * Writing O for the ray origin, D for its direction, A for the segment's
	 * first endpoint, S = B - A for the segment, and W = A - O:
	 *
	 *     O + t1*D = A + t2*S
	 *     t1*D - t2*S = W
	 *     crossing with S:  t1 = (W x S) / (D x S)
	 *     crossing with D:  t2 = (W x D) / (D x S)
	 *
	 * where u x v is the 2D scalar cross product u.x*v.y - u.y*v.x. 1.0 writes
	 * both numerators the other way round — `sdx*(a.y-ry) - sdy*(a.x-rx)` is
	 * S.x*W.y - S.y*W.x, which is -(W x S) — so both of its parameters come
	 * out negated. Combined with its own `if (t1 < 0) return null`, that means
	 * 1.0's function rejects every hit in front of the viewer and accepts only
	 * mirrored ones behind it. A ray fired straight at a wall 300 units away
	 * returns null.
	 *
	 * So `VisibilitySystem` never worked. That is consistent with what the rest
	 * of the 1.0 client shows: `FogOfWar.updateVisibility` takes its polygon
	 * from a server message, and nothing in the 1.0 server ever sends one.
	 *
	 * Porting the sign error faithfully would mean shipping a fog of war that
	 * lights the wrong half of the level, so the rule the port follows here is
	 * PLAN.md's: 1.0 is the source of *balance data*, and a cross product is
	 * not balance. Everything above this function — the endpoint rays, the
	 * epsilon pair, the boundary box, the arc, the clamp, the angular sort —
	 * is 1.0's algorithm unchanged, and `Valhalla.Game.Visibility.Polygon`
	 * pins the corrected behaviour.
	 */
	bool RaySegmentIntersect(
		const FVector2D& Origin,
		const FVector2D& Dir,
		const FValhallaVisibilitySegment& Seg,
		FValhallaRayHit& OutHit)
	{
		const FVector2D S = Seg.B - Seg.A;
		const FVector2D W = Seg.A - Origin;

		const double Denominator = Dir.X * S.Y - Dir.Y * S.X;
		if (FMath::Abs(Denominator) < 1e-10)
		{
			// Parallel. 1.0 returned null here too, rather than treating a ray
			// that runs along a wall as a hit at every point of it.
			return false;
		}

		const double T1 = (W.X * S.Y - W.Y * S.X) / Denominator;
		const double T2 = (W.X * Dir.Y - W.Y * Dir.X) / Denominator;

		// t1 >= 0 is "in front of the viewer"; t2 in [0, 1] is "within the
		// segment rather than on its infinite extension".
		if (T1 < 0.0 || T2 < 0.0 || T2 > 1.0)
		{
			return false;
		}

		OutHit.Point = Origin + Dir * T1;
		OutHit.Param = T1;
		return true;
	}
}

double ValhallaVisibility::NormaliseAngle(double Angle)
{
	while (Angle > UE_DOUBLE_PI)
	{
		Angle -= 2.0 * UE_DOUBLE_PI;
	}
	while (Angle < -UE_DOUBLE_PI)
	{
		Angle += 2.0 * UE_DOUBLE_PI;
	}
	return Angle;
}

TArray<FVector2D> ValhallaVisibility::ComputeVisibilityPolygon(
	const FVector2D& Origin,
	TArrayView<const FValhallaVisibilitySegment> Segments,
	double Radius,
	int32 ArcRays)
{
	TArray<FVector2D> Polygon;
	if (Radius <= 0.0)
	{
		return Polygon;
	}

	// ── 1. Gather nearby wall segments (VisibilitySystem.ts:139) ─────────
	//
	// 1.5 * radius rather than radius: a segment with both ends outside the
	// circle can still cross it, and dropping it would open a hole in a long
	// wall the viewer is standing beside.
	TArray<FValhallaVisibilitySegment> All;
	All.Reserve(Segments.Num() + 4);

	const double ExtRadiusSq = FMath::Square(Radius * 1.5);
	for (const FValhallaVisibilitySegment& Segment : Segments)
	{
		if (FVector2D::DistSquared(Segment.A, Origin) > ExtRadiusSq &&
			FVector2D::DistSquared(Segment.B, Origin) > ExtRadiusSq)
		{
			continue;
		}
		All.Add(Segment);
	}

	// ── 2. A box at the vision radius (VisibilitySystem.ts:156) ──────────
	//
	// So that every ray hits *something*. Without it a ray fired across open
	// ground has no intersection and the polygon has a hole in it.
	const double Left = Origin.X - Radius;
	const double Right = Origin.X + Radius;
	const double Top = Origin.Y - Radius;
	const double Bottom = Origin.Y + Radius;

	All.Emplace(FVector2D(Left, Top), FVector2D(Right, Top));
	All.Emplace(FVector2D(Right, Top), FVector2D(Right, Bottom));
	All.Emplace(FVector2D(Right, Bottom), FVector2D(Left, Bottom));
	All.Emplace(FVector2D(Left, Bottom), FVector2D(Left, Top));

	// ── 3. Every endpoint angle, plus the arc (VisibilitySystem.ts:177) ──
	TSet<double> UniqueAngles;
	UniqueAngles.Reserve(All.Num() * 2 + ArcRays);

	for (const FValhallaVisibilitySegment& Segment : All)
	{
		UniqueAngles.Add(FMath::Atan2(Segment.A.Y - Origin.Y, Segment.A.X - Origin.X));
		UniqueAngles.Add(FMath::Atan2(Segment.B.Y - Origin.Y, Segment.B.X - Origin.X));
	}

	const int32 Steps = FMath::Max(1, ArcRays);
	for (int32 Index = 0; Index < Steps; ++Index)
	{
		// Half-open, so the ray at -PI is not also emitted at +PI.
		UniqueAngles.Add(NormaliseAngle(-UE_DOUBLE_PI + (2.0 * UE_DOUBLE_PI * Index) / Steps));
	}

	// ── 4. Three rays per angle (VisibilitySystem.ts:201) ────────────────
	//
	// The epsilon pair is the whole trick: a ray *at* a corner stops on the
	// wall, and the two beside it slip past on either side, which is what puts
	// a vertex on the far geometry and opens the shadow behind the corner.
	TArray<double> Rays;
	Rays.Reserve(UniqueAngles.Num() * 3);
	for (const double Angle : UniqueAngles)
	{
		Rays.Add(Angle - RayEpsilon);
		Rays.Add(Angle);
		Rays.Add(Angle + RayEpsilon);
	}

	// ── 5. Closest hit per ray, clamped to the radius ────────────────────
	TArray<FValhallaRayHit> Hits;
	Hits.Reserve(Rays.Num());

	for (const double Angle : Rays)
	{
		const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));

		FValhallaRayHit Closest;
		bool bFound = false;

		for (const FValhallaVisibilitySegment& Segment : All)
		{
			FValhallaRayHit Hit;
			if (!RaySegmentIntersect(Origin, Direction, Segment, Hit))
			{
				continue;
			}
			if (!bFound || Hit.Param < Closest.Param)
			{
				Closest = Hit;
				bFound = true;
			}
		}

		FVector2D Point = bFound ? Closest.Point : Origin + Direction * Radius;

		const double Distance = FVector2D::Distance(Point, Origin);
		if (Distance > Radius)
		{
			Point = Origin + (Point - Origin) / Distance * Radius;
		}

		FValhallaRayHit Result;
		Result.Point = Point;
		Result.Param = Distance;
		Result.Angle = NormaliseAngle(Angle);
		Hits.Add(Result);
	}

	// ── 6. Sort by angle (VisibilitySystem.ts:268) ───────────────────────
	//
	// 1.0 sorted relative to the aim direction because its polygon was a wedge
	// that had to start and end at the cone edges. A full circle has no start,
	// so the sort is on the absolute angle and the polygon closes on itself.
	Hits.Sort([](const FValhallaRayHit& A, const FValhallaRayHit& B)
	{
		return A.Angle < B.Angle;
	});

	Polygon.Reserve(Hits.Num());
	for (const FValhallaRayHit& Hit : Hits)
	{
		Polygon.Add(Hit.Point);
	}
	return Polygon;
}

bool ValhallaVisibility::IsPointInPolygon(TArrayView<const FVector2D> Polygon, const FVector2D& Point)
{
	bool bInside = false;
	const int32 Count = Polygon.Num();

	for (int32 I = 0, J = Count - 1; I < Count; J = I++)
	{
		const FVector2D& Vi = Polygon[I];
		const FVector2D& Vj = Polygon[J];

		const bool bStraddles = (Vi.Y > Point.Y) != (Vj.Y > Point.Y);
		if (!bStraddles)
		{
			continue;
		}

		const double Crossing = (Vj.X - Vi.X) * (Point.Y - Vi.Y) / (Vj.Y - Vi.Y) + Vi.X;
		if (Point.X < Crossing)
		{
			bInside = !bInside;
		}
	}

	return bInside;
}

// ─────────────────────────────────────────────────────────────────────────────
//  The subsystem
// ─────────────────────────────────────────────────────────────────────────────

bool UValhallaVisibilitySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Game worlds only. An editor world has no connections to cull for and no
	// pawn to draw fog around, and the fog renderer's polygon builder is a free
	// function that does not need this object at all.
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld();
}

void UValhallaVisibilitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UWorld* World = GetWorld();
	LastPruneTime = World ? World->GetTimeSeconds() : 0.0;
	LastStatsTime = LastPruneTime;

	UE_LOG(LogValhallaVision, Log,
		TEXT("visibility subsystem up in %s (netmode %d): cache %d ticks, prune %.1f s"),
		World ? *World->GetName() : TEXT("?"),
		World ? static_cast<int32>(World->GetNetMode()) : -1,
		CacheTicks, CachePruneIntervalSeconds);
}

void UValhallaVisibilitySubsystem::Deinitialize()
{
	Cache.Empty();
	Super::Deinitialize();
}

TStatId UValhallaVisibilitySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UValhallaVisibilitySubsystem, STATGROUP_Tickables);
}

void UValhallaVisibilitySubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Incremented before the frame's replication pass, which runs in the
	// network phase of the world tick, after tickables. So every relevancy
	// question asked this frame sees the same counter.
	++TickCounter;

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;

	if (Now - LastPruneTime >= CachePruneIntervalSeconds)
	{
		PruneCache();
		LastPruneTime = Now;
	}

	if (CVarVisibilityStats.GetValueOnGameThread() != 0 && Now - LastStatsTime >= StatsIntervalSeconds)
	{
		ReportStats(Now);
		LastStatsTime = Now;
	}
}

void UValhallaVisibilitySubsystem::PruneCache()
{
	const int32 Before = Cache.Num();

	for (auto It = Cache.CreateIterator(); It; ++It)
	{
		// An entry whose key has gone stale — the actor was destroyed — is
		// swept out here too, because its expiry ticks over like any other and
		// nothing ever looks it up again.
		if (It.Value().ExpiryTick <= TickCounter)
		{
			It.RemoveCurrent();
		}
	}

	UE_LOG(LogValhallaVision, VeryVerbose, TEXT("cache pruned %d -> %d"), Before, Cache.Num());
}

void UValhallaVisibilitySubsystem::ReportStats(double Now)
{
	const double Window = FMath::Max(Now - LastStatsTime, UE_DOUBLE_SMALL_NUMBER);
	const int64 Answers = WindowTraces + WindowCacheHits;
	const double HitRate = Answers > 0 ? (100.0 * WindowCacheHits) / Answers : 0.0;

	UE_LOG(LogValhallaVision, Log,
		TEXT("valhalla.VisibilityStats: %lld traces in %.1f s (%.1f traces/s), %lld cache hits (%.1f %%), ")
		TEXT("%lld out of range, %d cached pairs"),
		WindowTraces, Window, WindowTraces / Window,
		WindowCacheHits, HitRate, WindowRangeRejects, Cache.Num());

	WindowTraces = 0;
	WindowCacheHits = 0;
	WindowRangeRejects = 0;
}

UValhallaVisibilitySubsystem* UValhallaVisibilitySubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	return World ? World->GetSubsystem<UValhallaVisibilitySubsystem>() : nullptr;
}

float UValhallaVisibilitySubsystem::GetVisionRangeFor(const AActor* Viewer)
{
	if (const APawn* Pawn = Cast<APawn>(Viewer))
	{
		if (const AValhallaPlayerState* State = Pawn->GetPlayerState<AValhallaPlayerState>())
		{
			const float ClassRange = State->VisionRange > 0.f ? State->VisionRange : DefaultVisionRange;

			// B-06: a zone's atmosphere can cap it (`netRelevancyRadiusCm`), so
			// a player in Eldmoor's mist is not sent the camp 30 m away that
			// the fog would hide anyway. A cap only ever lowers the range, and
			// a zone without one leaves it exactly as it was. This is a player's
			// own zone (the replicated ZoneId), and NPCs have no player state,
			// so an NPC's view — and with it aggro — is never touched.
			const float ZoneCap = ValhallaAtmosphere::GetRelevancyCapCm(Pawn, State->ZoneId);
			return ZoneCap > 0.f ? FMath::Min(ClassRange, ZoneCap) : ClassRange;
		}
	}
	return DefaultVisionRange;
}

bool UValhallaVisibilitySubsystem::IsVisibleFrom(const AActor* Viewer, const AActor* Target)
{
	const UWorld* World = GetWorld();
	return IsVisibleFrom(Viewer, Target, World ? World->GetTimeSeconds() : 0.0);
}

bool UValhallaVisibilitySubsystem::IsVisibleFrom(const AActor* Viewer, const AActor* Target, double /*Now*/)
{
	if (!Viewer || !Target)
	{
		return false;
	}
	if (Viewer == Target)
	{
		return true;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const FVector ViewerLocation = Viewer->GetActorLocation();
	const FVector TargetLocation = Target->GetActorLocation();

	// ── Range, in 2D ────────────────────────────────────────────────────
	//
	// First because it is a squared compare against a replicated float and it
	// rejects most pairs in a level of any size. Deliberately not cached: the
	// distance between two moving actors changes every tick, and caching it
	// would be caching the cheap half of the answer.
	const float Range = GetVisionRangeFor(Viewer);
	if (FVector::DistSquared2D(ViewerLocation, TargetLocation) > static_cast<double>(Range) * Range)
	{
		++WindowRangeRejects;
		return false;
	}

	// ── The cached answer ───────────────────────────────────────────────
	const FValhallaVisibilityKey Key(Viewer, Target);
	if (const FValhallaVisibilityCacheEntry* Entry = Cache.Find(Key))
	{
		if (Entry->ExpiryTick > TickCounter)
		{
			++WindowCacheHits;
			return Entry->bVisible;
		}
	}

	// ── The trace ───────────────────────────────────────────────────────
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ValhallaLineOfSight), /*bTraceComplex*/ false);
	Params.AddIgnoredActor(Viewer);
	Params.AddIgnoredActor(Target);

	const FVector Start = ViewerLocation + FVector(0.f, 0.f, ValhallaEyeHeight);
	const FVector End = TargetLocation + FVector(0.f, 0.f, ValhallaEyeHeight);

	++WindowTraces;
	++TotalTraces;

	const bool bBlocked = World->LineTraceTestByChannel(Start, End, ValhallaVisionBlockerChannel, Params);
	const bool bVisible = !bBlocked;

	FValhallaVisibilityCacheEntry NewEntry;
	NewEntry.ExpiryTick = TickCounter + static_cast<uint64>(FMath::Max(1, CacheTicks));
	NewEntry.bVisible = bVisible;
	Cache.Add(Key, NewEntry);

	return bVisible;
}

bool UValhallaVisibilitySubsystem::IsRelevantForViewer(const AActor* Self, const AActor* RealViewer, const AActor* ViewTarget)
{
	if (!Self)
	{
		return false;
	}

	// ── 1. Self and owner ───────────────────────────────────────────────
	if (Self == RealViewer || Self == ViewTarget)
	{
		return true;
	}
	for (const AActor* Owner = Self->GetOwner(); Owner; Owner = Owner->GetOwner())
	{
		if (Owner == RealViewer || Owner == ViewTarget)
		{
			return true;
		}
	}

	// Who is looking. RealViewer is the connection's player controller and
	// ViewTarget is normally its pawn, but during a respawn the view target can
	// briefly be the controller itself, so the pawn is looked up either way.
	const AActor* ViewerPawn = Cast<APawn>(ViewTarget);
	if (!ViewerPawn)
	{
		if (const APlayerController* Controller = Cast<APlayerController>(RealViewer))
		{
			ViewerPawn = Controller->GetPawn();
		}
	}
	if (!ViewerPawn)
	{
		// No body to see with yet. Fail open — see the header.
		return true;
	}

	// ── 2. Party members' pawns ─────────────────────────────────────────
	//
	// Pawns only, and only against another pawn: an NPC does not become
	// relevant by standing next to someone's party member, which is the whole
	// of gate (d).
	if (Self->IsA<AValhallaCharacter>())
	{
		const APawn* SelfPawn = Cast<APawn>(Self);
		const APawn* OtherPawn = Cast<APawn>(ViewerPawn);
		if (SelfPawn && OtherPawn)
		{
			const AValhallaPlayerState* SelfState = SelfPawn->GetPlayerState<AValhallaPlayerState>();
			const AValhallaPlayerState* ViewerState = OtherPawn->GetPlayerState<AValhallaPlayerState>();
			if (SelfState && ViewerState)
			{
				if (const UValhallaPartySubsystem* Party = UValhallaPartySubsystem::Get(Self))
				{
					if (Party->AreInSameParty(SelfState, ViewerState))
					{
						return true;
					}
				}
			}
		}
	}

	// ── 3. Line of sight ────────────────────────────────────────────────
	UValhallaVisibilitySubsystem* Subsystem = Get(Self);
	if (!Subsystem)
	{
		return true;
	}

	const UWorld* World = Self->GetWorld();
	return Subsystem->IsVisibleFrom(ViewerPawn, Self, World ? World->GetTimeSeconds() : 0.0);
}

// ─────────────────────────────────────────────────────────────────────────────
//  valhalla.DebugListActors — the gate's proof
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	/** "warrior 'Player1'", or "no player state" — how a client world is identified. */
	FString DescribeLocalPlayer(UWorld* World)
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* Controller = It->Get();
			if (!Controller || !Controller->IsLocalController())
			{
				continue;
			}
			if (const AValhallaPlayerState* State = Controller->GetPlayerState<AValhallaPlayerState>())
			{
				return FString::Printf(TEXT("%s '%s'"), *State->ClassId.ToString(), *State->CharacterName);
			}
			return TEXT("no player state");
		}
		return TEXT("no local controller");
	}

	/** One line per actor of a class, or a single "none" line. */
	template <typename TActor>
	void ListClass(UWorld* World, const TCHAR* ClassLabel, TFunctionRef<FString(const TActor&)> Describe)
	{
		TArray<FString> Lines;
		for (TActorIterator<TActor> It(World); It; ++It)
		{
			const TActor& Actor = **It;
			const FVector Location = Actor.GetActorLocation();
			Lines.Add(FString::Printf(TEXT("%s @ (%.0f, %.0f)"), *Describe(Actor), Location.X, Location.Y));
		}

		if (Lines.Num() == 0)
		{
			UE_LOG(LogValhallaVision, Log, TEXT("    %s x0"), ClassLabel);
			return;
		}

		UE_LOG(LogValhallaVision, Log, TEXT("    %s x%d: %s"), ClassLabel, Lines.Num(), *FString::Join(Lines, TEXT(", ")));
	}

	/**
	 * `valhalla.DebugListActors` — what each PIE *client* actually holds.
	 *
	 * This is the one command in the project that deliberately does not run on
	 * the server. Every other debug command is careful to act with authority,
	 * because it drives a rule. This one is asking the opposite question: what
	 * did the server decide to *send*? The only honest place to ask it is the
	 * client's own world, because that world contains exactly the actors that
	 * were replicated into it and nothing else — which is precisely what
	 * relevancy culling is supposed to control.
	 *
	 * So it walks GEngine->GetWorldContexts() rather than taking the console's
	 * world: under "run under one process" PIE, each client is its own
	 * FWorldContext with its own UWorld, and the editor console is bound to one
	 * of them. Listing all of them in one call is what lets the ranger's view
	 * and the warrior's view be compared in a single log excerpt.
	 */
	void ValhallaDebugListActors()
	{
		if (!GEngine)
		{
			return;
		}

		int32 ClientWorlds = 0;

		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World || !World->IsGameWorld() || World->GetNetMode() != NM_Client)
			{
				continue;
			}

			++ClientWorlds;

			UE_LOG(LogValhallaVision, Log,
				TEXT("valhalla.DebugListActors: client world '%s' (PIE instance %d, %s)"),
				*World->GetName(), Context.PIEInstance, *DescribeLocalPlayer(World));

			ListClass<AValhallaCharacter>(World, TEXT("AValhallaCharacter"),
				[](const AValhallaCharacter& Actor) -> FString
				{
					const AValhallaPlayerState* State = Actor.GetValhallaPlayerState();
					return State
						? FString::Printf(TEXT("%s[%s]"), *State->CharacterName, *State->ClassId.ToString())
						: Actor.GetName();
				});

			ListClass<AValhallaNPC>(World, TEXT("AValhallaNPC"),
				[](const AValhallaNPC& Actor) -> FString
				{
					return Actor.DisplayName.IsEmpty() ? Actor.GetName() : Actor.DisplayName;
				});

			ListClass<AValhallaSpellProjectile>(World, TEXT("AValhallaSpellProjectile"),
				[](const AValhallaSpellProjectile& Actor) -> FString
				{
					return Actor.SkillId.IsNone() ? Actor.GetName() : Actor.SkillId.ToString();
				});

			ListClass<AValhallaLootBag>(World, TEXT("AValhallaLootBag"),
				[](const AValhallaLootBag& Actor) -> FString
				{
					return FString::Printf(TEXT("%s(%d)"), *Actor.OwnerName, Actor.GetSlotCount());
				});
		}

		if (ClientWorlds == 0)
		{
			UE_LOG(LogValhallaVision, Warning,
				TEXT("valhalla.DebugListActors: no PIE client world. Play as a listen server with at least one client, ")
				TEXT("with 'Run Under One Process' on, or there is nothing on this machine to list."));
		}
	}

	FAutoConsoleCommand GDebugListActorsCommand(
		TEXT("valhalla.DebugListActors"),
		TEXT("Dev only. valhalla.DebugListActors — list the Valhalla actors each PIE client world was actually sent."),
		FConsoleCommandDelegate::CreateStatic(&ValhallaDebugListActors));

	/**
	 * `valhalla.DebugCameraDistance <cm> [fov]` — pull every local camera back.
	 *
	 * The shipping camera is a 1500 cm boom at 35 degrees, which shows about
	 * 1200 cm of ground: the fog edge at a warrior's 1200 cm vision range sits
	 * exactly off the bottom of the screen, and a ranger's 1800 is nowhere near
	 * it. That is the right camera for playing and the wrong one for *looking
	 * at* what Phase 5 does, so this pulls the boom back for a screenshot.
	 *
	 * Presentation only, and therefore — unlike every other `valhalla.Debug*`
	 * command — it deliberately runs on the *client*. A camera boom is not
	 * replicated, so setting it on the server's copy of a remote player's pawn
	 * would change nothing that anybody sees. It walks the world contexts for
	 * the same reason valhalla.DebugListActors does: under one-process PIE each
	 * client is its own world, and the editor console is bound to one of them.
	 */
	void ValhallaDebugCameraDistance(const TArray<FString>& Args)
	{
		if (Args.Num() < 1 || !GEngine)
		{
			UE_LOG(LogValhallaVision, Warning,
				TEXT("valhalla.DebugCameraDistance needs a boom length in cm, e.g. 'valhalla.DebugCameraDistance 5000 60'."));
			return;
		}

		const float Distance = FCString::Atof(*Args[0]);
		const float Fov = Args.IsValidIndex(1) ? FCString::Atof(*Args[1]) : 0.f;

		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World || !World->IsGameWorld())
			{
				continue;
			}

			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				APlayerController* Controller = It->Get();
				if (!Controller || !Controller->IsLocalController())
				{
					continue;
				}

				AValhallaCharacter* Pawn = Cast<AValhallaCharacter>(Controller->GetPawn());
				if (!Pawn)
				{
					continue;
				}

				if (USpringArmComponent* Boom = Pawn->GetCameraBoom())
				{
					Boom->TargetArmLength = Distance;
				}
				if (Fov > 0.f)
				{
					if (UCameraComponent* Camera = Pawn->GetTopDownCamera())
					{
						Camera->SetFieldOfView(Fov);
					}
				}

				UE_LOG(LogValhallaVision, Log,
					TEXT("valhalla.DebugCameraDistance: %s in %s -> boom %.0f cm, fov %.0f"),
					*Pawn->GetName(), *World->GetName(), Distance, Fov);
			}
		}
	}

	FAutoConsoleCommand GDebugCameraDistanceCommand(
		TEXT("valhalla.DebugCameraDistance"),
		TEXT("Dev only. valhalla.DebugCameraDistance <cm> [fov] — pull every local camera boom back, to see the fog edge."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ValhallaDebugCameraDistance));
}
