// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ValhallaNPCSpawner.generated.h"

class AValhallaNPC;
class UArrowComponent;
class UBillboardComponent;
class USkeletalMeshComponent;

/**
 * A placeable NPC spawn point: one spawn point, one NPC.
 *
 * Drop it into a zone's gameplay sublevel (`L_<Zone>_Gameplay`) from the Place
 * Actors panel, pick an **NPC Type** (a `BP_NPC_*` Blueprint from
 * `/Game/Valhalla/NPCs`), and it spawns one instance of that type standing on
 * the ground where the spawn point is, facing where its arrow points. The
 * editor viewport shows a preview of the NPC so the level reads as it will
 * play.
 *
 * **Respawn is EverQuest-style.** When its NPC dies the countdown starts: the
 * corpse lies for CorpseSeconds, then decays, and when the respawn time is up a
 * fresh instance of the type spawns. While its NPC is alive nothing happens.
 * The respawn time is the template's `respawnMs` (set per NPC template in the
 * web editor) unless this spawn point overrides it with RespawnSeconds.
 *
 * Server-only in every meaningful sense: it spawns nothing on a client and is
 * not itself replicated. The NPC it makes is.
 *
 * Build > Map Check lists every spawn point with no NPC type, an unknown
 * template, or its NPC's capsule stuck inside level geometry.
 */
UCLASS(Blueprintable, meta = (DisplayName = "NPC Spawn Point"))
class VALHALLAGAME_API AValhallaNPCSpawner : public AActor
{
	GENERATED_BODY()

public:
	AValhallaNPCSpawner();

	//~ Begin AActor interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
#if WITH_EDITOR
	virtual void PostLoad() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void CheckForErrors() override;
#endif
	//~ End AActor interface

	// ── What to spawn ───────────────────────────────────────────────────

	/**
	 * The NPC type: a Blueprint of AValhallaNPC, e.g. `BP_NPC_Orc`. Its Class
	 * Defaults name the npc-templates.json entry it plays and hold its look.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Spawn", meta = (DisplayName = "NPC Type"))
	TSubclassOf<AValhallaNPC> NPCClass;

	/**
	 * Play a different npc-templates.json entry than the NPC type's own — the
	 * same orc body as a level 5 "Orc Captain", say. Empty uses the type's
	 * Default Template Id.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Spawn", meta = (DisplayName = "Template Override", GetOptions = "GetNPCTemplateOptions"))
	FName TemplateId;

	/** A name for this one NPC ("Bjorn the Trader"), or empty for the type's. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Spawn", meta = (DisplayName = "Name Override"))
	FString DebugLabel;

	// ── Respawn ─────────────────────────────────────────────────────────

	/**
	 * Seconds from this NPC's death to the next one spawning. 0 uses the
	 * template's `respawnMs`, which the web editor sets per NPC template.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Spawn", meta = (DisplayName = "Respawn Time Override", ClampMin = "0.0", Units = "Seconds"))
	float RespawnSeconds = 0.f;

	/** Off for a one-time spawn (the admin API's "Spawn NPC here"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Spawn")
	bool bRespawn = true;

	/**
	 * How long the body lies before it decays, seconds. Always gone before the
	 * next spawn. Loot is a separate bag and is not affected.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Spawn", meta = (ClampMin = "0.5", Units = "Seconds"))
	float CorpseSeconds = 30.f;

	// ── Runtime (server) ────────────────────────────────────────────────

	/** The NPC this spawn point currently owns, alive or a corpse. May be null. */
	AValhallaNPC* GetSpawnedNPC() const { return SpawnedNPC.Get(); }

	/**
	 * The same, as an array. Kept for callers written when a spawner could own
	 * several NPCs (it no longer can).
	 */
	TArray<TWeakObjectPtr<AValhallaNPC>> GetSpawnedNPCs() const;

	/** The template id this spawn point actually plays: the override, else the type's. */
	FName GetEffectiveTemplateId() const;

	/** Respawn seconds after resolving the override against the template. */
	float GetEffectiveRespawnSeconds() const;

	/** Seconds until the next spawn, or 0 when none is pending. */
	float GetSecondsUntilRespawn() const;

	/** Called by AValhallaNPC::Die. Starts the corpse decay and the countdown. */
	void NotifyNPCDied(AValhallaNPC* Npc);

	/**
	 * The admin API's respawn / delete. Clears the corpse (or a live NPC when
	 * bForce) and either spawns a fresh one now or starts the countdown.
	 */
	void RespawnNow();
	void RemoveNPCAndScheduleRespawn();

	/** npc-templates.json ids, for the Template Override dropdown. */
	UFUNCTION()
	TArray<FString> GetNPCTemplateOptions() const;

protected:
	/** Spawn one instance of the type on the ground here. Server only. */
	void SpawnNPC();

	/** Timer body: remove the old corpse if it is still there, then spawn. */
	void HandleRespawnTimer();

	/**
	 * Where the capsule's centre goes: the floor under this spawn point plus
	 * the capsule half-height. The spawn point marks the NPC's *feet*, so
	 * pressing End in the editor to drop it onto the floor is all a designer
	 * has to do.
	 */
	FVector FindGroundedCapsuleCentre(float ScaledHalfHeight) const;

	/** The floor's Z under this spawn point, if a trace finds one. */
	bool TraceFloorZ(double& OutZ) const;

	/** Editor marker, hidden in game. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Spawn")
	TObjectPtr<UBillboardComponent> Marker;

#if WITH_EDITORONLY_DATA
	/** Which way the NPC will face. */
	UPROPERTY()
	TObjectPtr<UArrowComponent> FacingArrow;

	/** Editor-only preview of the NPC type, standing on the floor in its idle. */
	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> PreviewBody;

	/** The active body's separate head (MetaHuman face), a follower of PreviewBody. */
	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> PreviewHead;

	/** The type's armour, followers of PreviewBody. */
	UPROPERTY()
	TArray<TObjectPtr<USkeletalMeshComponent>> PreviewPieces;

	/** Rebuild the preview from NPCClass / TemplateId. */
	void RefreshPreview();
#endif

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<AValhallaNPC> SpawnedNPC;

	FTimerHandle RespawnTimer;
};
