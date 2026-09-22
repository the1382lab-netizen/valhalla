// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ValhallaGameTypes.h"
#include "ValhallaTypes.h"
#include "ValhallaNPC.generated.h"

class UStaticMeshComponent;
class USkeletalMeshComponent;
class UMaterialInstanceDynamic;
class AValhallaNPCSpawner;
class UValhallaAnimComponent;

/**
 * A minimal replicated view of one debuff, for the client's target pane.
 * The port of `NpcBuffInfo` (schema/NPCState.ts:10) — and, like it, deliberately
 * three fields: a client may see that a poison is ticking and when it ends, and
 * nothing else about it.
 */
USTRUCT(BlueprintType)
struct VALHALLAGAME_API FValhallaNPCBuffInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPC")
	FName SkillId;

	/** Server time in seconds when it drops off. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPC")
	double ExpiresAt = 0.0;

	/** 0 when this is not a DoT. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPC")
	float DotDamagePerSec = 0.f;
};

/**
 * One live enemy or friendly NPC. The port of `NPCState` + `NPCSystem`.
 *
 * There is no AIController, no behaviour tree and no navmesh here, and that is
 * a decision rather than an omission. `NPCSystem.update` is a flat state machine
 * — respawn, aggro, chase, leash, attack — that runs for every NPC in one pass
 * at a fixed 60 Hz, and its behaviour is the balance data. Rebuilding it as a
 * behaviour tree would mean the 2.0 enemies no longer demonstrably match the 1.0
 * ones, which is the only property of this port that matters. So the state
 * machine is ported line for line and ticked from AValhallaGameState's fixed
 * step, in the same place in the same order as `GameRoom.update` step 6.
 * Phase 4 may put a behaviour tree on top; it will have this to check against.
 *
 * The one thing that is not a port: movement goes through CharacterMovement
 * rather than 1.0's raw `x += dx * speed * dt`, so NPCs collide with the world
 * and with each other. `moveSpeed` becomes MaxWalkSpeed and the numbers carry
 * over unchanged, because a 1.0 pixel is a 2.0 centimetre.
 */
UCLASS()
class VALHALLAGAME_API AValhallaNPC : public ACharacter
{
	GENERATED_BODY()

public:
	AValhallaNPC();

	//~ Begin AActor interface
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/**
	 * Phase 5's anti-cheat boundary: a client is never sent an NPC it
	 * cannot see. See UValhallaVisibilitySubsystem::IsRelevantForViewer.
	 */
	virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override;
	//~ End AActor interface

	// ── Replicated state (the port of the synced half of NPCState) ──────

	/** NPCState.ts:42 `hp`. */
	UPROPERTY(ReplicatedUsing = OnRep_Hp, BlueprintReadOnly, Category = "Valhalla|NPC")
	float Hp = 0.f;

	/** NPCState.ts:44 `maxHp`. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|NPC")
	float MaxHp = 0.f;

	/** NPCState.ts:46 `level`. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|NPC")
	int32 Level = 1;

	/** NPCState.ts:33 `name`. What the HUD draws over its head. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|NPC")
	FString DisplayName;

	/** NPCState.ts:48 `alive`. */
	UPROPERTY(ReplicatedUsing = OnRep_Alive, BlueprintReadOnly, Category = "Valhalla|NPC")
	bool bAlive = true;

	/** NPCState.ts:31 `templateId`. Clients need it to look the template up. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|NPC")
	FName TemplateId;

	/** NPCState.ts:57 `syncedBuffs`. The client's target-pane debuff pills. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|NPC")
	TArray<FValhallaNPCBuffInfo> SyncedBuffs;

	// ── Server API ──────────────────────────────────────────────────────

	/**
	 * Stand this NPC up from a template at a home position. Server only.
	 * @param Spawner The actor that owns it; also its leash anchor. May be null.
	 */
	void InitializeFromTemplate(const FValhallaNPCTemplate& Template, const FVector& InHomeLocation, AValhallaNPCSpawner* InSpawner);

	/**
	 * NPCSystem.ts:111 `update`, for one NPC, one fixed step.
	 * Driven by AValhallaGameState::ServerFixedTick — never by an actor tick, so
	 * the chase distances are frame-rate independent exactly as 1.0's were.
	 */
	void ServerFixedTick(float FixedDeltaSeconds, double Now);

	/**
	 * NPCSystem.ts:279 `damageNPC`.
	 *
	 * This is the *only* thing that puts a player on an NPC's threat table:
	 * damage dealt is threat generated. Proximity can pick a first target, but it
	 * cannot out-rank someone who has actually hit it. Returns the xp reward when
	 * this hit killed it, 0 otherwise.
	 */
	int32 ApplyDamageFromAttacker(AActor* Attacker, int32 Damage, double Now, bool& bOutDied);

	/**
	 * NPCSystem.ts:306 `tauntNpc` — add threat and switch target immediately.
	 * The switch is unconditional: a taunt that only added threat would be a
	 * suggestion, and Taunt is not a suggestion.
	 */
	void AddThreat(AActor* Player, float BonusThreat, bool bForceTarget);

	/** Apply or refresh a buff on this NPC, keeping SyncedBuffs in step. */
	void ApplyNPCBuff(const FValhallaActiveBuff& Buff);

	/** Kill it now, start the respawn timer, and emit npcDied. */
	void Die(AActor* Killer, double Now);

	/**
	 * NPCSystem.ts:124 — stand it back up at home with full hp and no threat.
	 *
	 * Extracted from `ServerFixedTick` rather than duplicated so the admin API's
	 * `respawn-npc` and the timer take exactly the same path: an NPC an admin
	 * stood up early must be indistinguishable from one whose timer expired, or
	 * the dashboard becomes a way to produce states the game itself cannot.
	 * Does nothing to an NPC that is already alive.
	 */
	void Respawn();

	/**
	 * Re-read this NPC's template after a data hot reload, keeping it where it
	 * is and keeping it alive.
	 *
	 * The template is held by value (see the `Template` member), so a reload of
	 * npc-templates.json is invisible to every NPC already standing until this
	 * runs. Current hp is carried across as a *fraction* of max: an edit that
	 * doubles a template's hp should not leave the NPC on the field at half a
	 * bar, and an edit that halves it must not leave it above its own maximum.
	 * Position, threat table and buffs are untouched — this is a stat edit, not
	 * a respawn.
	 */
	void ReapplyTemplate(const FValhallaNPCTemplate& NewTemplate);

	// ── Queries ─────────────────────────────────────────────────────────

	/** The loaded template. Null if the id was unknown when it spawned. */
	const FValhallaNPCTemplate& GetTemplate() const { return Template; }

	/** True while it is up. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|NPC")
	bool IsAlive() const { return bAlive; }

	/** Where it will run home to. */
	FVector GetHomeLocation() const { return HomeLocation; }

	/** Who it is currently trying to kill. Null when it is idle. */
	AActor* GetAggroTarget() const { return AggroTarget.Get(); }

protected:
	/** Drop it into the held last frame of `A_Death`, or stand it back up. */
	UFUNCTION()
	void OnRep_Alive();

	/** Client-side hook so a nameplate can flash on a hit. */
	UFUNCTION()
	void OnRep_Hp();

	/** NPCSystem.ts:493 `updateAggro` — prune the threat table, then pick the top of it. */
	void UpdateAggro(double Now);

	/** NPCSystem.ts:406 `tickNpcBuffs` — expire, then tick DoTs once a second. */
	void TickBuffs(double Now);

	/** Push ActiveBuffs into the replicated SyncedBuffs view. */
	void SyncBuffsToReplicatedView();

	/** Teleport home, full heal, drop all threat. The port of the leash reset. */
	void ResetToHome();

	/**
	 * Read `spriteSize` and `spriteColor` onto the body, and put the fixed kit
	 * on. Runs on every end from BeginPlay; idempotent.
	 */
	void ApplyAppearance();

	/** The animated body — the same rig and the same skeleton a player uses. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Appearance")
	TObjectPtr<USkeletalMeshComponent> BodyMesh;

	/** `SK_chest_priests_chain`, a follower of BodyMesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Appearance")
	TObjectPtr<USkeletalMeshComponent> ChestMesh;

	/** `SK_helm_iron_full`, a follower of BodyMesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Appearance")
	TObjectPtr<USkeletalMeshComponent> HelmMesh;

	/** Idle / Walk / attack / hit / death. Identical driving to a player's. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Appearance")
	TObjectPtr<UValhallaAnimComponent> AnimComponent;

	/** `spriteColor` on the body's skin slot. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TintMaterial;

	/** Capsule radius, cm. Matches the player's so ranges read the same both ways. */
	static constexpr float CapsuleRadius = 30.f;

	/** Capsule half-height, cm. The art is 120 cm tall, so this is exact. */
	static constexpr float CapsuleHalfHeight = 60.f;

	/**
	 * NPCSystem.ts:177 — an NPC stops closing at 30 units and attacks from there.
	 *
	 * Measured surface to surface: the two capsules' radii are added to this (and
	 * to the template's attackRange) before the comparison, because 1.0's
	 * entities were points that could occupy the same spot and 2.0's are capsules
	 * that cannot. See the chase block in ServerFixedTick.
	 */
	static constexpr float StopChaseDistance = 30.f;

	/** NPCSystem.ts:241 — the walk home is 2/3 of the chase speed. */
	static constexpr float ReturnSpeedFraction = 0.66f;

private:
	/** The template this NPC was built from. Copied, not referenced. */
	FValhallaNPCTemplate Template;

	/** NPCSystem `spawnX/spawnY` — the leash anchor and the respawn point. */
	FVector HomeLocation = FVector::ZeroVector;

	/** The spawner that made it. Weak: deleting a spawner must not delete its NPCs mid-frame. */
	TWeakObjectPtr<AValhallaNPCSpawner> Spawner;

	/** NPCSystem `aggroTarget`. */
	TWeakObjectPtr<AActor> AggroTarget;

	/** NPCSystem `threatTable` — cumulative damage per player. */
	TMap<TWeakObjectPtr<AActor>, float> ThreatTable;

	/** NPCSystem `respawnAt`, in server-time seconds. 0 while alive. */
	double RespawnAt = 0.0;

	/** NPCSystem `lastAttackTime`, in server-time seconds. */
	double LastAttackTime = 0.0;

	/** Server-only buff list. SyncedBuffs is the client's censored view of it. */
	TArray<FValhallaActiveBuff> ActiveBuffs;

	/** Who last damaged it, so a DoT kill still credits someone. */
	TWeakObjectPtr<AActor> LastAttacker;

	/** Resolved once from the template so the leash check is not a branch per tick. */
	float LeashRange = 600.f;
};
