// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ValhallaZoneEntry.generated.h"

class UArrowComponent;

/**
 * Where a traveller from another zone comes out. 1.0's `zone_entry` overlay
 * point, as an actor.
 *
 * The 1.0 arrangement was indirect by necessity: `MapManager.mergeOverlay`
 * (MapManager.ts:136) had to *open the destination zone's overlay file* and
 * search it for a `zone_entry` whose `fromZone` matched the zone it was
 * merging, because the server only ever had one zone's tile grid in hand at a
 * time. 2.0 hosts every zone in one world, so the portal can simply be told
 * the id of the entry it leads to and the subsystem can look the actor up.
 *
 * `FromZoneId` is kept anyway, and for the reason 1.0 had it: it documents
 * which direction this entry is for, and it lets the loader check that a
 * portal claiming to lead here is coming from the zone this entry expects.
 * A portal pointing at an entry meant for somewhere else is a level-authoring
 * mistake that is otherwise invisible until a player walks through it.
 *
 * Not replicated. Arrival is a server-side teleport of the pawn; the client
 * learns about it the way it learns about any other movement.
 */
UCLASS(Blueprintable)
class VALHALLAGAME_API AValhallaZoneEntry : public AActor
{
	GENERATED_BODY()

public:
	AValhallaZoneEntry();

	/** Unique within the zone. What a portal's `TargetEntryId` names. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Zones")
	FName EntryId;

	/** Which zone arrivals here are expected to be coming from. 1.0's `fromZone`. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Zones")
	FName FromZoneId;

	/** Where and which way the arriving pawn is placed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Zones")
	TObjectPtr<UArrowComponent> Arrow;

	/** The entry with this id in a world, or null. */
	static AValhallaZoneEntry* Find(const UWorld* World, FName InEntryId);
};
