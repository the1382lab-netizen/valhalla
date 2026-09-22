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

	/** The overlay's coordinate system: centimetres from the box's min corner. */
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

/**
 * The `type` field of an overlay spawn point. The 1.0 spellings, unchanged —
 * `MapManager.mergeOverlay` partitions on exactly these five strings and
 * grepping both repos for `zone_entry` should find both ends.
 */
UENUM(BlueprintType)
enum class EValhallaOverlayPointType : uint8
{
	/** Where players arrive when they log into this zone. */
	PlayerSpawn		UMETA(DisplayName = "player_spawn"),
	/** An `AValhallaNPCSpawner`; `TemplateId` names the npc-templates.json row. */
	EnemySpawn		UMETA(DisplayName = "enemy_spawn"),
	/** The same thing for a friendly. 2.0 spawns both through the same actor. */
	NpcSpawn		UMETA(DisplayName = "npc_spawn"),
	/** A way out: `TargetZone` + `TargetEntry`. */
	Portal			UMETA(DisplayName = "portal"),
	/** A way in: `EntryId` is this point's own `Id`, `FromZone` says from where. */
	ZoneEntry		UMETA(DisplayName = "zone_entry"),
	/** Parsed but unrecognised. Kept so the loader can name it in a warning. */
	Unknown			UMETA(DisplayName = "unknown"),
};

/**
 * One entry of an overlay file's `spawnPoints` array.
 *
 * The 2.0 overlay is the 1.0 overlay with two changes and nothing else:
 * coordinates are zone-local *centimetres* rather than map pixels, and the
 * portal's destination is two explicit fields (`targetZone`, `targetEntry`)
 * rather than `templateId` doing double duty. 1.0's `templateId` meant "the
 * NPC template" on an enemy spawn and "the target zone" on a portal, which is
 * the kind of thing that works until somebody names a zone after a monster.
 *
 * `width`/`height` are gone too. A 1.0 portal was a trigger *rect* in the
 * overlay because the 1.0 editor drew it with `fillRect`; a 2.0 portal is an
 * `AValhallaPortal` actor whose box is authored in the level, so the overlay
 * only needs to say where it is, to check that one is there.
 */
USTRUCT(BlueprintType)
struct VALHALLAGAME_API FValhallaOverlayPoint
{
	GENERATED_BODY()

	/** Stable id. Unique within the file; used in every log line about it. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Zones")
	FName Id;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Zones")
	EValhallaOverlayPointType Type = EValhallaOverlayPointType::Unknown;

	/** Zone-local centimetres from the zone volume's min corner. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Zones")
	double X = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Zones")
	double Y = 0.0;

	/** npc-templates.json id, on an enemy_spawn / npc_spawn. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Zones")
	FName TemplateId;

	/** How many NPCs; straight onto `AValhallaNPCSpawner::Count`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Zones")
	int32 Count = 1;

	/** Scatter radius in cm; onto `AValhallaNPCSpawner::SpawnRadius`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Zones")
	double Radius = 300.0;

	/** On a portal: which zone it leads to. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Zones")
	FName TargetZone;

	/** On a portal: the `zone_entry` id in that zone to arrive at. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Zones")
	FName TargetEntry;

	/** On a zone_entry: which zone arrivals come from. 1.0's `fromZone`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Zones")
	FName FromZone;

	/** Human label, for logs and for the Phase 6 editor. Never read by a rule. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Zones")
	FString Label;
};

/** A parsed `maps/overlays-2.0/<zone>.json`. */
USTRUCT(BlueprintType)
struct VALHALLAGAME_API FValhallaZoneOverlay
{
	GENERATED_BODY()

	/** The file's `version`. "2.0" is the only one this loader accepts. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Zones")
	FString Version;

	/** The file's `units`. "cm" is the only one this loader accepts. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Zones")
	FString Units;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Zones")
	FName ZoneId;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Zones")
	TArray<FValhallaOverlayPoint> SpawnPoints;

	/** Every point of one type, in file order. */
	TArray<const FValhallaOverlayPoint*> PointsOfType(EValhallaOverlayPointType Wanted) const
	{
		TArray<const FValhallaOverlayPoint*> Result;
		for (const FValhallaOverlayPoint& Point : SpawnPoints)
		{
			if (Point.Type == Wanted)
			{
				Result.Add(&Point);
			}
		}
		return Result;
	}
};
