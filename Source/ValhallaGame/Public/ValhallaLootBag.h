// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ValhallaInventoryTypes.h"
#include "ValhallaLootBag.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class USphereComponent;

/**
 * A bag of loot on the ground. The port of `LootBagState`
 * (server/src/schema/LootBagState.ts:15) and the entity half of
 * `LootBagSystem`.
 *
 * 1.0 kept bags in a `MapSchema` on the room state and synced them to everyone
 * in the zone; 2.0 makes each one an actor, because Unreal already has
 * relevancy, lifetime and spatial queries and a parallel entity map would be
 * fighting all three. The consequences are worth naming:
 *
 *   - Despawn is `SetLifeSpan`, not a sweep in the room update
 *     (LootBagSystem.ts:113). The 1.0 sweep also removed *empty* bags, which
 *     here is a Destroy the moment the last slot is taken.
 *   - `bag.zoneId` is gone. Every actor is in the world of its own zone, so the
 *     zone test in `canPlayerReachBag` (LootBagSystem.ts:169) is satisfied by
 *     the actor existing at all. Phase 3 makes that literally true.
 *
 * Everything else — the 80 cm merge radius, the 200 cm reach, the five minute
 * life, the 18 slot cap — is the 1.0 number in 1.0's own units, because 1.0
 * pixels and Unreal centimetres are the same number (PLAN.md, Phase 3).
 */
UCLASS()
class VALHALLAGAME_API AValhallaLootBag : public AActor
{
	GENERATED_BODY()

public:
	AValhallaLootBag();

	//~ Begin AActor interface
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/**
	 * Phase 5's anti-cheat boundary: a client is never sent a loot bag it
	 * cannot see. See UValhallaVisibilitySubsystem::IsRelevantForViewer.
	 */
	virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override;
	//~ End AActor interface

	/**
	 * What is in the bag. Replicated to everyone: a bag on the ground is public,
	 * and 1.0 put it in the room state for the same reason.
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|Loot")
	TArray<FValhallaBagSlot> Items;

	/**
	 * Whose corpse or whose drop this came from, for the HUD label.
	 *
	 * Not an ownership rule: 1.0 had no loot ownership, no rolls and no timer
	 * before a bag went free-for-all, and 2.0 does not invent one. Phase 7 is
	 * where a real loot-rights rule would go, and this field is where it would
	 * start.
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|Loot")
	FString OwnerName;

	/**
	 * AValhallaGameState::GetServerTime() at which this bag was created.
	 *
	 * A merge into an existing bag deliberately does *not* refresh it
	 * (LootBagSystem.ts:39 adds items and returns without touching `_createdAt`),
	 * so a busy spot does not accumulate an immortal pile.
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|Loot")
	double CreatedAt = 0.0;

	/** How many slots are occupied. Drives the "Loot (n)" label. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Loot")
	int32 GetSlotCount() const { return Items.Num(); }

	/**
	 * LootBagSystem.ts:168 `canPlayerReachBag`, minus the zone test — see the
	 * class comment. 2D, because a player standing on a rise above a bag is
	 * still standing next to it.
	 */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Loot")
	bool IsWithinReach(const AActor* Player) const;

	/**
	 * Put items in, stacking where the template allows. Server only.
	 * LootBagSystem.ts:200 `addItemsToBag`.
	 */
	void AddItems(const TArray<FValhallaBagSlot>& NewItems);

	/**
	 * Destroy the bag if nothing is left in it.
	 * LootBagSystem.ts:118 — an empty bag is removed on the very next sweep, so
	 * there is no observable state in which one exists.
	 */
	void DestroyIfEmpty();

	/**
	 * LootBagSystem.ts:178 `findNearbyBag` — an existing bag within
	 * Valhalla::LootBagMergeRange of a point that still has room, or null.
	 *
	 * Static because the caller (AValhallaGameMode::OnNPCKilled) is asking "is
	 * there one of these near here", which is a question about the world rather
	 * than about any particular bag.
	 */
	static AValhallaLootBag* FindNearbyBag(UWorld* World, const FVector& Location);

	/** Spawn a bag, or merge into a nearby one. LootBagSystem.ts:27 `spawnBag`. Server only. */
	static AValhallaLootBag* SpawnOrMerge(UWorld* World, const FVector& Location, const TArray<FValhallaBagSlot>& NewItems, const FString& InOwnerName);

	/** How high above the ground the proxy sits, cm. Keeps the sphere out of the floor. */
	static constexpr float GroundOffset = 24.f;

	/**
	 * The `Interact` trace channel (DefaultEngine.ini, ECC_GameTraceChannel2).
	 * Only loot bags block it, so the loot click can never be eaten by an NPC,
	 * a corpse, a prop or the player's own capsule standing in front of the bag.
	 */
	static constexpr ECollisionChannel InteractChannel = ECC_GameTraceChannel2;

	/** Radius of the invisible click target around the bag, cm. Generous on purpose. */
	static constexpr float ClickRadius = 55.f;

private:
	/** The grey-box body: a small brown sphere. Phase 4 gives it a real mesh. */
	UPROPERTY(VisibleAnywhere, Category = "Valhalla|Loot")
	TObjectPtr<UStaticMeshComponent> ProxyMesh;

	/** The generous, invisible click target on the Interact channel. */
	UPROPERTY(VisibleAnywhere, Category = "Valhalla|Loot")
	TObjectPtr<USphereComponent> ClickTarget;

	/** Tint for ProxyMesh, so the sphere is not whatever colour the kit shipped. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ProxyMaterial;
};
