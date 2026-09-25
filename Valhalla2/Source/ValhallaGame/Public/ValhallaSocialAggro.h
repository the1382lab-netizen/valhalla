// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * B-10: social aggro — who answers when an NPC calls for help.
 *
 * An NPC whose template has `canSocialAggro` calls for help the moment it
 * picks up a target (AValhallaNPC::CallForHelp). This is the rule each other
 * NPC is held to; the world queries (distance, the line-of-sight trace) are
 * done by the caller and passed in, so Valhalla.Game.NPC.SocialAggro can pin
 * the rule without a world.
 *
 * Chaining is not in here on purpose: an NPC that answers picks up a target
 * the ordinary way, and calls its own neighbours the next fixed step if its
 * own template has `canSocialAggro`. Each NPC calls at most once per fight,
 * and an NPC already fighting never answers, so a chain always ends.
 */
struct VALHALLAGAME_API FValhallaSocialAggro
{
	/** What the caller knows about one other NPC. */
	struct FCandidate
	{
		/** Its template's social group (the template id when the JSON leaves it blank). */
		FName Group;
		bool bAlive = true;
		/** CanEverAggro(): an enemy (or canAggro) and not friendly. */
		bool bCanAggro = true;
		/** Already has a target or anything on its threat table. */
		bool bEngaged = false;
		/** Squared XY distance to the caller, cm². */
		double DistanceSq = 0.0;
		/** No sight blocker (VisionBlocker channel) between the two. */
		bool bLineOfSight = true;
	};

	/** Whether this NPC joins a fight the caller (in CallerGroup, calling over CallerRange cm) started. */
	static bool ShouldAnswer(FName CallerGroup, double CallerRange, const FCandidate& Candidate)
	{
		return !CallerGroup.IsNone()
			&& Candidate.Group == CallerGroup
			&& Candidate.bAlive
			&& Candidate.bCanAggro
			&& !Candidate.bEngaged
			&& CallerRange > 0.0
			&& Candidate.DistanceSq <= CallerRange * CallerRange
			&& Candidate.bLineOfSight;
	}
};
