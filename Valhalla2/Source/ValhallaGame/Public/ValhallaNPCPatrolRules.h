// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ValhallaNPCPatrolRules.generated.h"

/**
 * How an NPC walks the route its spawn point draws (B-10 part 2, B-06 1.9c).
 *
 * The spawn point itself is always stop 0 and its Patrol Points are stops
 * 1..N, so a route of one point is "walk out there and back" in either mode.
 */
UENUM(BlueprintType)
enum class EValhallaPatrolMode : uint8
{
	/** No route: the points are kept but ignored (the NPC stands, or roams if it has a Wander Radius). */
	None,
	/** 0 -> 1 -> ... -> N -> 0 -> 1 ...: round a camp or a mound. */
	Loop,
	/** 0 -> 1 -> ... -> N -> N-1 -> ... -> 0 -> 1 ...: back and forth along a road or a wall. */
	PingPong,
};

/** Which idle movement an NPC has; at most one. Server-side state, not reflected. */
enum class EValhallaNPCIdleMode : uint8
{
	/** Stands at home (walks back there if pushed off it): the pre-B-10 behaviour. */
	None,
	/** Walks FValhallaNPCPatrolSetup::Stops. */
	Route,
	/** Walks behind another spawn point's NPC. */
	Follow,
	/** Walks to random points round home. */
	Wander,
};

/**
 * What a spawn point hands its NPC about idle movement, in world space. Built
 * by AValhallaNPCSpawner at spawn (route points projected onto the nav mesh)
 * and held by value on the NPC, so moving or editing the spawn point in a
 * running game changes nothing until the next spawn.
 */
struct VALHALLAGAME_API FValhallaNPCPatrolSetup
{
	/** World-space stops; [0] is the spawn point (the NPC's home). Fewer than two is no route. */
	TArray<FVector> Stops;

	EValhallaPatrolMode Mode = EValhallaPatrolMode::None;

	/** The pause at each stop is a uniform roll in [PauseMinSeconds, PauseMaxSeconds]. */
	float PauseMinSeconds = 5.f;
	float PauseMaxSeconds = 10.f;

	/** Fraction of the template's move speed while walking a route, following or roaming. */
	float SpeedFraction = 0.5f;

	/** Roam radius round home, cm; 0 = off. Ignored when there is a route or a leader. */
	float WanderRadius = 0.f;
};

/**
 * The decisions an idle NPC's walk is made of: which stop comes next, when a
 * stop is reached, how long it pauses, where a follower stands behind its
 * leader and how fast it walks to get there, where a roam goes without a nav
 * mesh, and whether a spawn cycle is the rare one.
 *
 * AValhallaNPC::TickIdleMovement does the world half (the nav path, the random
 * nav point, the clock) and asks these; Valhalla.Game.NPC.Patrol pins them
 * without a world. Every distance is XY, in cm. The rolls take their random
 * number as a parameter so a test can supply the edges of the range.
 */
struct VALHALLAGAME_API FValhallaNPCPatrolRules
{
	/** A stop counts as reached this close to it, cm (XY). Wider than a path corner (30) so a slow walker does not creep the last few centimetres. */
	static constexpr double ArrivedCm = 35.0;

	/** A follower keeps this far behind its leader, cm: about one body length, the "pair walks together" of the Eldmoor roamers. */
	static constexpr double FollowDistanceCm = 120.0;

	/** A walking follower stops once this close to its spot behind the leader, cm. */
	static constexpr double FollowStopCm = 40.0;

	/**
	 * A standing follower sets off again only once its spot is this far away,
	 * cm. The gap between this and FollowStopCm is what stops a follower
	 * twitching on and off behind a leader that is barely moving.
	 */
	static constexpr double FollowResumeCm = 80.0;

	/** Beyond this far from its spot a follower walks faster than its leader to catch up, cm... */
	static constexpr double FollowCatchUpStartCm = 150.0;

	/** ...reaching its full move speed this far away, cm. */
	static constexpr double FollowCatchUpFullCm = 450.0;

	/**
	 * The stop after Current, for a route of StopCount stops (the spawn point
	 * included). InOutDirection is PingPong's +1 / -1 and is flipped at either
	 * end; Loop ignores it.
	 *
	 * - no stops at all: INDEX_NONE;
	 * - one stop, or Mode None: stay where it is (0, or Current when valid);
	 * - a Current outside the route (the route shrank): start again at 0.
	 */
	static int32 NextStop(EValhallaPatrolMode Mode, int32 StopCount, int32 Current, int32& InOutDirection)
	{
		if (StopCount <= 0)
		{
			return INDEX_NONE;
		}
		if (Current < 0 || Current >= StopCount)
		{
			InOutDirection = 1;
			return 0;
		}
		if (StopCount == 1 || Mode == EValhallaPatrolMode::None)
		{
			return Current;
		}
		if (Mode == EValhallaPatrolMode::Loop)
		{
			return (Current + 1) % StopCount;
		}

		// PingPong: turn round at either end, never stepping off it.
		if (InOutDirection != 1 && InOutDirection != -1)
		{
			InOutDirection = 1;
		}
		const int32 Next = Current + InOutDirection;
		if (Next < 0 || Next >= StopCount)
		{
			InOutDirection = -InOutDirection;
			return Current + InOutDirection;
		}
		return Next;
	}

	/** Whether a route drives this NPC at all: a mode, and somewhere to go besides home. */
	static bool HasRoute(EValhallaPatrolMode Mode, int32 StopCount)
	{
		return Mode != EValhallaPatrolMode::None && StopCount >= 2;
	}

	/** At the stop (XY). Height is ignored: the stop is on the floor, the NPC's origin is its capsule centre. */
	static bool HasArrived(const FVector& From, const FVector& Stop, double Tolerance = ArrivedCm)
	{
		return FVector::DistSquared2D(From, Stop) <= Tolerance * Tolerance;
	}

	/**
	 * How long to stand at a stop: uniform in [Min, Max] from Random01 in
	 * [0, 1]. A negative Min counts as 0 and a Max below Min as Min, so a
	 * half-edited spawn point pauses for a fixed time rather than for a
	 * negative one.
	 */
	static double RollPause(double MinSeconds, double MaxSeconds, double Random01)
	{
		const double Lo = FMath::Max(0.0, MinSeconds);
		const double Hi = FMath::Max(Lo, MaxSeconds);
		return Lo + (Hi - Lo) * FMath::Clamp(Random01, 0.0, 1.0);
	}

	/** Where a follower stands: Distance behind the leader along the way it faces, at the leader's height. */
	static FVector FollowSpot(const FVector& LeaderLocation, double LeaderYawDegrees, double Distance = FollowDistanceCm)
	{
		const double Yaw = FMath::DegreesToRadians(LeaderYawDegrees);
		return FVector(LeaderLocation.X - FMath::Cos(Yaw) * Distance, LeaderLocation.Y - FMath::Sin(Yaw) * Distance, LeaderLocation.Z);
	}

	/**
	 * Whether a follower walks this step. A walking one stops within
	 * FollowStopCm of its spot; a standing one sets off again beyond
	 * FollowResumeCm.
	 */
	static bool FollowerShouldMove(double DistanceToSpot, bool bWasMoving)
	{
		return bWasMoving ? DistanceToSpot > FollowStopCm : DistanceToSpot > FollowResumeCm;
	}

	/**
	 * The input scale a follower walks at: its leader's pace (LeaderFraction,
	 * the leader's walking speed as a fraction of the follower's own move
	 * speed) while it keeps up, rising linearly to full speed as it falls
	 * between FollowCatchUpStartCm and FollowCatchUpFullCm behind. Clamped to
	 * [0.1, 1].
	 */
	static double FollowSpeedScale(double DistanceToSpot, double LeaderFraction)
	{
		const double Base = FMath::Clamp(LeaderFraction, 0.1, 1.0);
		const double T = FMath::Clamp((DistanceToSpot - FollowCatchUpStartCm) / (FollowCatchUpFullCm - FollowCatchUpStartCm), 0.0, 1.0);
		return FMath::Lerp(Base, 1.0, T);
	}

	/**
	 * A roam goal with no nav mesh (L_GreyBox, tests): uniform over the disc
	 * of Radius round Home, from two numbers in [0, 1]. The square root keeps
	 * it uniform by area rather than bunched at the centre.
	 */
	static FVector WanderPoint(const FVector& Home, double Radius, double RandomAngle01, double RandomDistance01)
	{
		const double Angle = FMath::Clamp(RandomAngle01, 0.0, 1.0) * 2.0 * UE_DOUBLE_PI;
		const double Dist = FMath::Max(0.0, Radius) * FMath::Sqrt(FMath::Clamp(RandomDistance01, 0.0, 1.0));
		return FVector(Home.X + FMath::Cos(Angle) * Dist, Home.Y + FMath::Sin(Angle) * Dist, Home.Z);
	}

	/**
	 * Whether this spawn cycle is the rare one: Random01 in [0, 1) below the
	 * chance. A chance of 0 (or less) never is and 1 (or more) always is.
	 */
	static bool RollsRare(double Chance, double Random01)
	{
		if (Chance <= 0.0)
		{
			return false;
		}
		if (Chance >= 1.0)
		{
			return true;
		}
		return Random01 < Chance;
	}
};
