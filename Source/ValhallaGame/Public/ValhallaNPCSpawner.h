// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ValhallaNPCSpawner.generated.h"

class AValhallaNPC;
class UBillboardComponent;

/**
 * A placeable spawn point. The port of 1.0's `enemy_spawn` map objects.
 *
 * 1.0 read spawn points out of the Tiled object layer and `NPCSystem.spawnZone`
 * turned each one into an NPC. 2.0 has no Tiled import until Phase 3, so until
 * then a spawner is an actor a designer drags into the level — which is a better
 * authoring story anyway, and is what Phase 3's importer will create.
 *
 * Server-only in every meaningful sense: it spawns nothing on a client, and it
 * is not itself replicated. The NPCs it makes are.
 */
UCLASS(Blueprintable)
class VALHALLAGAME_API AValhallaNPCSpawner : public AActor
{
	GENERATED_BODY()

public:
	AValhallaNPCSpawner();

	//~ Begin AActor interface
	virtual void BeginPlay() override;
	//~ End AActor interface

	/** Which entry of npc-templates.json to spawn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Spawner")
	FName TemplateId;

	/** How many. Each gets its own home point inside SpawnRadius. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Spawner", meta = (ClampMin = "0", UIMin = "0", UIMax = "20"))
	int32 Count = 1;

	/**
	 * How far from this actor the group scatters, cm.
	 *
	 * Each NPC's home is a fixed point inside the circle, not a random one per
	 * respawn: an enemy that respawned somewhere new each time would make the
	 * leash anchor meaningless and the encounter unrepeatable.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Spawner", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SpawnRadius = 300.f;

	/**
	 * Overrides the spawned NPC's `DisplayName`, or empty to keep the
	 * template's.
	 *
	 * Test scaffolding, and named so. `npc-templates.json` has three entries and
	 * they are all called things like "New NPC", which is fine in a grey box and
	 * useless in a gate transcript where four NPCs have to be told apart by name
	 * in a `valhalla.DebugListActors` line. A spawner placing more than one NPC
	 * suffixes the index.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Spawner")
	FString DebugLabel;

	/** The NPCs this spawner made. Weak, so a destroyed NPC drops out by itself. */
	const TArray<TWeakObjectPtr<AValhallaNPC>>& GetSpawnedNPCs() const { return SpawnedNPCs; }

protected:
	/** Read the template, place Count NPCs, log what happened. Server only. */
	void SpawnGroup();

	/** Where NPC number Index should live. Deterministic in Index. */
	FVector ComputeHomeLocation(int32 Index) const;

	/** Editor-only marker so the spawner can be found in a grey-box level. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Spawner")
	TObjectPtr<UBillboardComponent> Marker;

private:
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AValhallaNPC>> SpawnedNPCs;
};
