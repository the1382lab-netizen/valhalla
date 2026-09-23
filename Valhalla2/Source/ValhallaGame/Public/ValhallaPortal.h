// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ValhallaPortal.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/**
 * A way out of a zone. The port of 1.0's `ZoneConnection` + its trigger rect
 * (`MapManager.ts:160`, `GameRoom.checkZoneTransitions`).
 *
 * 1.0 tested the trigger every server tick, in `checkZoneTransitions`, as an
 * inclusive AABB point test of `player.x/y` against a rect that came out of the
 * overlay file. 2.0 uses a real overlap on a box component instead, which is
 * the same test done by the physics scene, and takes the pawn's capsule into
 * account rather than a single point — so walking along the edge of a portal
 * triggers it, as a player expects, instead of requiring the pawn's origin to
 * cross the line.
 *
 * Server-only behaviour. The overlap fires on a client too (the box is not
 * replicated, but the level actor is there), and `HandleOverlap` returns
 * immediately without authority. Nothing about a zone change is a client's to
 * decide; the client finds out because its pawn moved and its `ZoneId` changed.
 *
 * The 1.0 ping-pong hazard is real and is handled here. 1.0 had no cooldown at
 * all — its only guard was `break; // Only one portal per tick` — so a
 * destination entry point that happened to land inside a portal leading back
 * would bounce a player between two zones every tick forever. 2.0's cooldown
 * lives on `UValhallaZoneSubsystem` rather than on the portal, because the
 * portal that sent you and the portal that would send you back are two
 * different actors and a per-actor cooldown would not see the loop.
 */
UCLASS(Blueprintable)
class VALHALLAGAME_API AValhallaPortal : public AActor
{
	GENERATED_BODY()

public:
	AValhallaPortal();

	//~ Begin AActor interface
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	//~ End AActor interface

	/** `zones.json` key of the zone this leads to. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Zones")
	FName TargetZoneId;

	/**
	 * The `AValhallaZoneEntry::EntryId` in that zone to arrive at.
	 *
	 * Empty falls back to the target zone's `AValhallaZoneVolume` default
	 * spawn, which is the same precedence 1.0 used: matching `zone_entry`
	 * first, `zones.json` `defaultSpawn` second (MapManager.ts:134-147).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Zones")
	FName TargetEntryId;

	/** Half-size of the trigger, cm. One tile is 64, so 96 is a tile and a half. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Zones")
	FVector TriggerExtent = FVector(96.f, 96.f, 120.f);

	/** The trigger. Overlaps pawns and nothing else. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Zones")
	TObjectPtr<UBoxComponent> Trigger;

	/** `SM_PortalMarker`, so the player can see where they are walking. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Zones")
	TObjectPtr<UStaticMeshComponent> Marker;

	/** `/Game/Valhalla/Props/SM_PortalMarker/StaticMeshes/SM_PortalMarker`. */
	static constexpr const TCHAR* MarkerMeshPath =
		TEXT("/Game/Valhalla/Props/SM_PortalMarker/StaticMeshes/SM_PortalMarker");

protected:
	/** Bound to the trigger. Asks the zone subsystem to do the travelling. */
	UFUNCTION()
	void HandleOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);
};
