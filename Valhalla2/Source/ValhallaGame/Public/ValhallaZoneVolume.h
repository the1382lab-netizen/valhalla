// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ValhallaZoneTypes.h"
#include "ValhallaZoneVolume.generated.h"

class UArrowComponent;
class UBoxComponent;

/**
 * "This rectangle of the world is the zone `grasslands`."
 *
 * Every zone fact 2.0 needs at runtime is derived from this actor, and that is
 * deliberate. 1.0 could keep its zone bounds in `zones.json` because a zone was
 * a tile grid whose size was in the map file. A 2.0 zone is a streaming
 * sublevel loaded at an offset the config file does not know, so the only
 * honest place for the bounds is inside the level, moving with it.
 *
 * What reads it:
 *
 *   - `UValhallaZoneSubsystem` builds its `FValhallaZoneDef` list from these,
 *     which makes `GetZoneAt` a box test and `PlayerState::ZoneId` a lookup.
 *   - The overlay loader treats `Box->Bounds.Min` as the zone's origin, so an
 *     overlay coordinate is zone-local centimetres and survives the level
 *     being moved.
 *   - `AValhallaFogRenderer` stretches its masks over the box of whichever
 *     zone the pawn is in. A zone volume therefore *replaces*
 *     `AValhallaFogBounds` for a level that has one, which is why
 *     `build_grasslands.py` and `build_desert.py` place no fog bounds actor.
 *
 * Not replicated, and it does not need to be: a level actor exists in every
 * client's copy of the level, so the client's fog and the server's zone lookup
 * read the same box without a byte crossing the wire.
 */
UCLASS(Blueprintable)
class VALHALLAGAME_API AValhallaZoneVolume : public AActor
{
	GENERATED_BODY()

public:
	AValhallaZoneVolume();

	//~ Begin AActor interface
	virtual void OnConstruction(const FTransform& Transform) override;
	//~ End AActor interface

	/** `zones.json` key. A volume with none is ignored, loudly. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Zones")
	FName ZoneId;

	/** `ZoneConfig.name`. Cosmetic; the id is what rules compare. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Zones")
	FString DisplayName;

	/**
	 * Half-size of the zone, cm.
	 *
	 * An EditAnywhere property pushed onto the box in OnConstruction, for the
	 * same reason `AValhallaFogBounds::Extent` is one: it is a single
	 * `set_editor_property` from the Python level builders and it survives a
	 * re-save, which a default subobject's own extent does not.
	 *
	 * A 64 x 64 tile zone is 4096 cm square, so the half-size is 2048.
	 *
	 * The box's **minimum Z is the zone floor**, and the overlay loader relies
	 * on it: an overlay coordinate is (x, y) only, and the Z it gets is
	 * `Bounds.Min.Z`. So a builder places the volume at
	 * `z = FLOOR_TOP + Extent.Z`, which puts the bottom face exactly on the
	 * floor tiles' top surface and makes an overlay point at (0, 0) land on the
	 * zone's south-west corner tile rather than several metres under it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Zones")
	FVector Extent = FVector(2048.f, 2048.f, 500.f);

	/** The zone rectangle. Query-only: nothing collides with a zone. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Zones")
	TObjectPtr<UBoxComponent> Box;

	/**
	 * `ZoneConfig.defaultSpawn`, as a thing you can see and drag.
	 *
	 * An arrow rather than two numbers because the yaw matters — a player who
	 * logs in facing a wall has a worse first second than one who logs in
	 * facing the town — and because a spawn point you can only edit as a pair
	 * of floats is a spawn point nobody ever moves.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Zones")
	TObjectPtr<UArrowComponent> DefaultSpawn;

	/** World-space box. Includes the streaming level's offset. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Zones")
	FBox GetZoneBounds() const;

	/** Build the runtime definition this volume stands for. */
	FValhallaZoneDef ToZoneDef() const;

	/** Every zone volume in a world, in no particular order. */
	static void FindAll(const UWorld* World, TArray<AValhallaZoneVolume*>& OutVolumes);

	/** The volume for a zone id, or null. */
	static AValhallaZoneVolume* Find(const UWorld* World, FName InZoneId);
};
