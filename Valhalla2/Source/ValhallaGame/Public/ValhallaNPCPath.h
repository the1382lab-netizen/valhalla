// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * B-16: the path an NPC is walking, and the arithmetic of walking it.
 *
 * NPCs keep their 1.0 state machine (AValhallaNPC::ServerFixedTick) as the
 * brain; this only changes *which way* the chase and the walk home push. The
 * state machine asks the navigation system for a path at most every
 * `RepathIntervalSeconds`, or sooner when the goal has moved
 * `RepathDistanceCm`, and each fixed step steers at the next corner.
 *
 * There is deliberately no AAIController and no UPathFollowingComponent. A
 * path follower ticks on the frame clock and moves the pawn itself; the NPC
 * brain runs at a fixed 60 Hz and moves it with AddMovementInput. Keeping one
 * clock and one mover is what keeps aggro, leash and attack timing exactly as
 * they were. Possessing the NPC would also change its owner, which the
 * visibility code walks.
 *
 * Plain struct and static functions, so Valhalla.Game.NPC.PathSteering can pin
 * the rules without a world or a nav mesh.
 */
struct VALHALLAGAME_API FValhallaNPCPath
{
	/** Re-plan when the goal has moved this far since the last plan, cm. */
	static constexpr double RepathDistanceCm = 50.0;

	/** ...or when the last plan is this old, seconds. */
	static constexpr double RepathIntervalSeconds = 0.5;

	/** A corner counts as reached this close to it, cm (XY). */
	static constexpr double CornerReachedCm = 30.0;

	/** No progress for this long while trying to move means stuck, seconds. */
	static constexpr double StuckSeconds = 3.0;

	/** Moving at least this far resets the stuck clock, cm (XY). */
	static constexpr double StuckProgressCm = 25.0;

	enum class EMode : uint8
	{
		/** Nothing planned yet, or the plan was thrown away. */
		None,
		/** Clear walkable ground to the goal: steer straight at it, live. */
		Direct,
		/** Follow Points corner by corner, then steer straight at the goal. */
		Path,
		/** No nav data or no path at all: steer straight, as before B-16. */
		Fallback,
	};

	EMode Mode = EMode::None;

	/** The corners to walk, world space. Points[0] is where the plan started. */
	TArray<FVector> Points;

	/** The corner being walked to. */
	int32 NextIndex = 0;

	/** Where the goal was when the plan was made. */
	FVector PlannedGoal = FVector::ZeroVector;

	/** Server time of the last plan. */
	double PlannedAt = -1.0;

	/** True when the nav system could only get part of the way. */
	bool bPartial = false;

	/** Stuck detection: where the NPC was when the clock last reset, and when. */
	FVector StuckAnchor = FVector::ZeroVector;
	double StuckSince = -1.0;

	/** Forget the plan and the stuck clock. After any teleport, death or target change. */
	void Reset()
	{
		*this = FValhallaNPCPath();
	}

	/** Whether the plan is missing, stale or aimed at the wrong place. */
	static bool NeedsReplan(const FValhallaNPCPath& Path, const FVector& Goal, double Now)
	{
		if (Path.Mode == EMode::None || Path.PlannedAt < 0.0)
		{
			return true;
		}
		if (Now - Path.PlannedAt >= RepathIntervalSeconds)
		{
			return true;
		}
		return FVector::DistSquared2D(Path.PlannedGoal, Goal) >= RepathDistanceCm * RepathDistanceCm;
	}

	/** Store a planned path. Fewer than two points is treated as no path. */
	static void SetPath(FValhallaNPCPath& Path, const TArray<FVector>& InPoints, bool bInPartial, const FVector& Goal, double Now)
	{
		Path.PlannedGoal = Goal;
		Path.PlannedAt = Now;
		if (InPoints.Num() < 2)
		{
			Path.Mode = EMode::Fallback;
			Path.Points.Reset();
			Path.NextIndex = 0;
			Path.bPartial = false;
			return;
		}
		Path.Mode = EMode::Path;
		Path.Points = InPoints;
		Path.NextIndex = 1;
		Path.bPartial = bInPartial;
	}

	/** Store a "go straight" decision: Direct when the ground is clear, Fallback when there is no nav data. */
	static void SetStraight(FValhallaNPCPath& Path, EMode InMode, const FVector& Goal, double Now)
	{
		Path.Mode = InMode;
		Path.Points.Reset();
		Path.NextIndex = 0;
		Path.bPartial = false;
		Path.PlannedGoal = Goal;
		Path.PlannedAt = Now;
	}

	/**
	 * The XY direction to push this step: at the next corner of the path, or
	 * straight at the live goal once the corners run out (a partial path
	 * included — that is the pre-B-16 behaviour at the end of what is
	 * walkable). Advances past corners already reached. Zero when From is on
	 * top of the goal.
	 */
	static FVector SteerDirection(FValhallaNPCPath& Path, const FVector& From, const FVector& LiveGoal)
	{
		if (Path.Mode == EMode::Path)
		{
			while (Path.Points.IsValidIndex(Path.NextIndex)
				&& FVector::DistSquared2D(From, Path.Points[Path.NextIndex]) <= CornerReachedCm * CornerReachedCm)
			{
				++Path.NextIndex;
			}

			// The last point is where the goal *was*; past the corners, aim at
			// where it is now.
			if (Path.Points.IsValidIndex(Path.NextIndex) && Path.NextIndex < Path.Points.Num() - 1)
			{
				FVector ToCorner = Path.Points[Path.NextIndex] - From;
				ToCorner.Z = 0.0;
				return ToCorner.GetSafeNormal2D();
			}
		}

		FVector ToGoal = LiveGoal - From;
		ToGoal.Z = 0.0;
		return ToGoal.GetSafeNormal2D();
	}

	/**
	 * Stuck detection, called every step the NPC is *trying* to move. Returns
	 * true once it has moved less than StuckProgressCm for StuckSeconds.
	 */
	static bool UpdateStuck(FValhallaNPCPath& Path, const FVector& Location, double Now)
	{
		if (Path.StuckSince < 0.0 || FVector::DistSquared2D(Location, Path.StuckAnchor) >= StuckProgressCm * StuckProgressCm)
		{
			Path.StuckAnchor = Location;
			Path.StuckSince = Now;
			return false;
		}
		return Now - Path.StuckSince >= StuckSeconds;
	}

	/** Called on steps the NPC is not trying to move (in range, at home). */
	static void ClearStuck(FValhallaNPCPath& Path)
	{
		Path.StuckSince = -1.0;
	}
};
