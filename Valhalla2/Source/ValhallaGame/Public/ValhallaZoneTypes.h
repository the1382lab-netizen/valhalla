// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ValhallaZoneTypes.generated.h"

class AValhallaZoneVolume;

/**
 * One zone, as the running world knows it.
 *
 * 1.0 had a zone per Colyseus room-loaded map: `zones.json` named the map file
 * and a default spawn, and `MapManager.loadZone` turned that into a tile grid.
 * 2.0 keeps the same four fields and drops the grid, because a UE level *is*
 * the map: the bounds come from an `AValhallaZoneVolume` placed in the level
 * and the default spawn from that volume's arrow.
 *
 * The bounds are world space, already including whatever offset the streaming
 * level was loaded at. That is the whole point of discovering them from a
 * placed actor rather than from a config file: `L_Desert` is authored around
 * its own origin and loaded at X = +40000, and nothing in this struct has to
 * know that.
 */
USTRUCT(BlueprintType)
struct VALHALLAGAME_API FValhallaZoneDef
{
	GENERATED_BODY()

	/** `zones.json` key — `grasslands`, `desert`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Zones")
	FName ZoneId;

	/** `ZoneConfig.name` — "Grasslands", "Scorched Desert". */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Zones")
	FString DisplayName;

	/** The volume's box in world space. Also the fog bounds for this zone. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Zones")
	FBox Bounds = FBox(ForceInit);

	/** `ZoneConfig.defaultSpawn`, world space, at the height the arrow sits at. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Zones")
	FVector DefaultSpawn = FVector::ZeroVector;

	/** Which way a player arriving at DefaultSpawn faces. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Zones")
	float DefaultSpawnYaw = 0.f;

	/** The actor this came from. Weak: the level owns it, not us. */
	TWeakObjectPtr<AValhallaZoneVolume> Volume;

	bool IsValid() const { return !ZoneId.IsNone() && Bounds.IsValid != 0; }

	/**
	 * XY-only containment.
	 *
	 * Z is ignored on purpose. A zone volume is authored as a flat box a couple
	 * of metres tall over a flat level, and a player standing on a roof or
	 * knocked into the air is still in the zone — as they were in 1.0, where
	 * there was no Z at all.
	 */
	bool Contains2D(const FVector& Point) const
	{
		return Bounds.IsValid != 0
			&& Point.X >= Bounds.Min.X && Point.X <= Bounds.Max.X
			&& Point.Y >= Bounds.Min.Y && Point.Y <= Bounds.Max.Y;
	}

	/** The zone-local coordinate system: centimetres from the box's min corner. */
	FVector2D ToZoneLocal(const FVector& World) const
	{
		return FVector2D(World.X - Bounds.Min.X, World.Y - Bounds.Min.Y);
	}

	/** Inverse of ToZoneLocal. Z is the bounds' floor plus ZOffset. */
	FVector FromZoneLocal(double LocalX, double LocalY, double ZOffset = 0.0) const
	{
		return FVector(Bounds.Min.X + LocalX, Bounds.Min.Y + LocalY, Bounds.Min.Z + ZOffset);
	}

	/** The XY rectangle, for the fog renderer. */
	FBox2D GetBounds2D() const
	{
		return FBox2D(FVector2D(Bounds.Min.X, Bounds.Min.Y), FVector2D(Bounds.Max.X, Bounds.Max.Y));
	}
};
