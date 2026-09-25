// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ValhallaTypes.h"

/**
 * The decisions an NPC's fight is made of, one fixed step at a time: whether
 * to keep closing, whether this step's swing (or shot) may go off, when the
 * leash sends it back, and when a returning NPC can be pulled again.
 *
 * AValhallaNPC::ServerFixedTick does the world queries (distances, the facing
 * cone, the sight trace) and asks these; Valhalla.Game.NPC.CombatRules pins
 * them without a world. Every distance is XY, in cm, with the two capsules'
 * radii already added where the rule says "range" (see the chase block in
 * ServerFixedTick for why).
 */
struct VALHALLAGAME_API FValhallaNPCCombatRules
{
	/**
	 * A ranged NPC that is still walking in closes to this fraction of its
	 * range before it stops, so a target that steps back a little is still in
	 * range and the NPC is not stopping and starting on the edge.
	 */
	static constexpr double RangedCloseInFraction = 0.9;

	/**
	 * A returning (leashed) NPC can be pulled again, by a hit or by walking
	 * into its aggro range, once it is back within this fraction of its leash
	 * range. Beyond that it would only leash again on its next step.
	 */
	static constexpr double RepullLeashFraction = 0.75;

	/** A return has ended once the NPC is this close to the spot it left from, cm. */
	static constexpr double ReturnArrivedCm = 2.0;

	/**
	 * Whether the NPC keeps moving toward its target this step.
	 *
	 * Melee closes to StopDistance. Ranged closes until the target is in range
	 * *and* in sight, then holds (bWasHolding) until the target leaves its full
	 * range or its sight; it never walks in to point-blank range on its own.
	 */
	static bool ShouldChase(EValhallaNPCAttackType Type, double Distance, double StopDistance, double AttackRange,
		bool bInSight, bool bWasHolding)
	{
		if (Type != EValhallaNPCAttackType::Ranged)
		{
			return Distance > StopDistance;
		}
		if (!bInSight)
		{
			return true;
		}
		return bWasHolding
			? Distance > AttackRange
			: Distance > AttackRange * RangedCloseInFraction;
	}

	/**
	 * Whether this step's attack goes off. The same gate a player's auto-attack
	 * has: in range, facing the target (UValhallaCombatLibrary::IsFacing) and
	 * off cooldown; a ranged shot also needs a clear line of sight. A step that
	 * fails only on facing does not spend the attack, so it lands the moment
	 * the NPC has turned.
	 */
	static bool CanAttack(double Distance, double AttackRange, bool bInSight, bool bFacing, double Now, double ReadyAt)
	{
		return Distance <= AttackRange && bInSight && bFacing && Now >= ReadyAt;
	}

	/** Led too far from where the fight started: give up and walk back. */
	static bool IsBeyondLeash(double DistanceFromStart, double LeashRange)
	{
		return DistanceFromStart > LeashRange;
	}

	/** A returning NPC this far from where the fight started can take a new target. */
	static bool CanRepull(double DistanceFromStart, double LeashRange)
	{
		return DistanceFromStart <= LeashRange * RepullLeashFraction;
	}
};
