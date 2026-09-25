// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ValhallaGameTypes.h"
#include "ValhallaNPCPath.h"
#include "ValhallaNPCPatrolRules.h"
#include "ValhallaTypes.h"
#include "ValhallaNPC.generated.h"

class UStaticMeshComponent;
class USkeletalMesh;
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
 * B-06 1.9a: where a held prop sits on a non-player body, relative to a bone —
 * the same numbers UValhallaVisuals' grip frames use for the player body.
 * An empty Bone means "use the player body's frame".
 */
USTRUCT(BlueprintType)
struct VALHALLAGAME_API FValhallaNPCGripFrame
{
	GENERATED_BODY()

	/** Bone (or socket) the prop rides on, e.g. hand_r. Empty = not overridden. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Grip")
	FName Bone;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Grip")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Grip")
	FRotator Rotation = FRotator::ZeroRotator;

	/** Prop size relative to its modelled size (props are modelled for a 122 cm person). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Grip", meta = (ClampMin = "0.05"))
	float PropScale = 1.f;
};

/**
 * One live enemy or friendly NPC. The port of `NPCState` + `NPCSystem`.
 *
 * **NPC types are Blueprints of this class.** `/Game/Valhalla/NPCs/BP_NPC_*` is
 * the library of pre-built NPCs: each one names the npc-templates.json entry it
 * plays (DefaultTemplateId — stats, behaviour, loot and respawn time stay in the
 * JSON, where the web editor balances them) and carries its own look (the body
 * and armour meshes on its components, scale, tint, a fixed name). An
 * AValhallaNPCSpawner placed in a level spawns one instance of one type. A type
 * dragged straight into a level also works: it plays its template and stands
 * itself back up where it was placed after the template's respawn time.
 *
 * There is no AIController and no behaviour tree here, and that is a decision
 * rather than an omission. `NPCSystem.update` is a flat state machine
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
 *
 * B-16: the chase and the walk home follow a nav-mesh path (FValhallaNPCPath,
 * PlanAndSteer) instead of pushing in a straight line, so NPCs walk round walls
 * and through doorways. Where the ground is clear they still steer straight at
 * the target, so melee spacing and every authored range mean what they did.
 *
 * B-10 part 2: an *idle* layer under all of that. With no target, not walking
 * back and no fight to finish, an NPC whose spawn point gave it one walks a
 * patrol route, follows a leader or roams (TickIdleMovement). It is the lowest
 * layer, so the 1.0 machine above it is untouched: a fight records FightStart
 * wherever the NPC stood on its route, the leash and the walk back return it
 * there, and the idle layer then carries on to the stop it was heading for.
 */
UCLASS()
class VALHALLAGAME_API AValhallaNPC : public ACharacter
{
	GENERATED_BODY()

public:
	AValhallaNPC();

	//~ Begin AActor interface
	virtual void BeginPlay() override;
	/** Swaps in the active body profile (and re-hangs the weapon) before the components start. */
	virtual void PostInitializeComponents() override;
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

	/**
	 * The template's `type` is `npc` rather than `enemy`: a townsperson or a
	 * merchant. Players cannot attack it and it never aggroes. Replicated
	 * because the client's own hostility check (can I auto-attack this?) needs
	 * it and the template itself is server-only.
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|NPC")
	bool bFriendly = false;

	// ── NPC type (set on a BP_NPC_* Blueprint's Class Defaults) ─────────

	/**
	 * Which npc-templates.json entry this NPC type plays. The dropdown is read
	 * from the JSON on disk, so a template added in the web editor shows up
	 * here as soon as it is saved.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|NPC Type", meta = (GetOptions = "GetNPCTemplateOptions"))
	FName DefaultTemplateId;

	/** A fixed name ("Bjorn the Trader"), or empty to use the template's name. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|NPC Type")
	FString NameOverride;

	/**
	 * Put the placeholder chain shirt and full helm on the chest and helm
	 * slots when this type does not name its own. Turn it off for an NPC whose
	 * look is exactly the meshes below — including bare-headed.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|NPC Type|Look")
	bool bWearPlaceholderKit = true;

	/** The placeholder kit's chest and helm (EquipmentMeshPath names), for the B-27 preloader. */
	static const TCHAR* PlaceholderChestName();
	static const TCHAR* PlaceholderHelmName();

	/** Armour this type wears, from /Game/Valhalla/Characters/Equipment. Empty = none (or the placeholder). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|NPC Type|Look")
	TSoftObjectPtr<USkeletalMesh> ChestMeshAsset;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|NPC Type|Look")
	TSoftObjectPtr<USkeletalMesh> HelmMeshAsset;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|NPC Type|Look")
	TSoftObjectPtr<USkeletalMesh> LegsMeshAsset;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|NPC Type|Look")
	TSoftObjectPtr<USkeletalMesh> BootsMeshAsset;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|NPC Type|Look")
	TSoftObjectPtr<USkeletalMesh> GlovesMeshAsset;

	/** Body scale. 0 uses the template's `spriteSize`. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|NPC Type|Look", meta = (ClampMin = "0.0", UIMax = "3.0"))
	float ScaleOverride = 0.f;

	/** Skin tint. Leave alpha at 0 to use the template's `spriteColor`. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|NPC Type|Look")
	FLinearColor TintOverride = FLinearColor(0.f, 0.f, 0.f, 0.f);

	// ── Body (B-06 1.9a: a non-player body, e.g. the goblin) ────────────

	/**
	 * A body of its own instead of the player body. When set, the NPC draws
	 * this mesh, has no head, hair or armour pieces (the mesh is its own
	 * outfit), plays the clips in AnimFolderOverride and holds its weapon with
	 * the grip frames below. The mesh must face +Y with its feet at the origin,
	 * like the player body.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|NPC Type|Body")
	TSoftObjectPtr<USkeletalMesh> BodyMeshOverride;

	/**
	 * Folder of the game's clips retargeted onto that body's skeleton, same
	 * names as the player body's (e.g. /Game/Valhalla/Characters/Goblin/Animations).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|NPC Type|Body")
	FString AnimFolderOverride;

	/**
	 * Scales the override body to the game's 122 cm person (122 / the mesh's
	 * own height in cm). Its real size then comes from ScaleOverride or the
	 * template's spriteSize, which also scale the capsule. 0 = 1.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|NPC Type|Body", meta = (ClampMin = "0.0"))
	float BodyMeshScale = 0.f;

	/** Sword, mace, dagger, totem on the override body. Empty bone = the player body's frame. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|NPC Type|Body")
	FValhallaNPCGripFrame OneHandGrip;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|NPC Type|Body")
	FValhallaNPCGripFrame StaffGrip;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|NPC Type|Body")
	FValhallaNPCGripFrame BowGrip;

	/** True when this type uses BodyMeshOverride. */
	bool HasBodyOverride() const { return !BodyMeshOverride.IsNull(); }

	/** BodyMesh's scale before spriteSize: the override's BodyMeshScale, else the active body's. */
	float GetBaseBodyScale() const;

	/** npc-templates.json ids, for the DefaultTemplateId dropdown. */
	UFUNCTION()
	TArray<FString> GetNPCTemplateOptions() const;

	/**
	 * Every npc-templates.json id on disk, sorted. Works in the editor, where
	 * there is no game instance and so no UValhallaDataSubsystem; the tables are
	 * cached and re-read when the file changes.
	 */
	static TArray<FString> ReadTemplateIdsFromDisk();

	/** One template read from disk, for editor previews. False if unknown. */
	static bool ReadTemplateFromDisk(FName TemplateId, FValhallaNPCTemplate& OutTemplate);

	/** The body scale this type plays a template at: ScaleOverride, else spriteSize. */
	float ResolveBodyScale(const FValhallaNPCTemplate& ForTemplate) const;

	/**
	 * The armour meshes this NPC type wears — chest, helm, legs, boots, gloves,
	 * each null when empty — placeholder kit included. Called on a class
	 * default object for the spawn point's editor preview, so it never touches
	 * a live component.
	 */
	void GetAppearanceMeshes(USkeletalMesh*& OutBody, TArray<USkeletalMesh*>& OutPieces) const;

	// ── Server API ──────────────────────────────────────────────────────

	/**
	 * Stand this NPC up from a template at a home position. Server only.
	 * @param Spawner The actor that owns it; also its leash anchor. May be null.
	 */
	void InitializeFromTemplate(const FValhallaNPCTemplate& Template, const FVector& InHomeLocation, AValhallaNPCSpawner* InSpawner);

	/** The spawn point that owns it, or null for a hand-placed or admin NPC. */
	AValhallaNPCSpawner* GetSpawner() const { return Spawner.Get(); }

	/** True once InitializeFromTemplate has run. */
	bool IsInitialized() const { return bInitializedFromTemplate; }

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
	 * Whether this NPC ever builds threat and fights: its template's
	 * `canAggro`, defaulting to true for enemies — and never for a friendly
	 * NPC (B-06), whatever the template says.
	 */
	bool CanEverAggro() const;

	/**
	 * NPCSystem.ts:306 `tauntNpc` — add threat and switch target immediately.
	 * The switch is unconditional: a taunt that only added threat would be a
	 * suggestion, and Taunt is not a suggestion.
	 */
	void AddThreat(AActor* Player, float BonusThreat, bool bForceTarget);

	/** An attack on this NPC missed or was dodged: the attacker still draws aggro (1 threat). Server only. */
	void NotifyAttackAvoided(AActor* Attacker);

	/** Apply or refresh a buff on this NPC, keeping SyncedBuffs in step. */
	void ApplyNPCBuff(const FValhallaActiveBuff& Buff);

	/**
	 * Kill it now and emit npcDied. A spawn point's NPC tells its spawn point,
	 * which lets the corpse decay and spawns a fresh instance when its respawn
	 * time is up; a hand-placed NPC starts its own timer and stands back up.
	 */
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

	/** Move the leash anchor / respawn point. Server only. */
	void SetHomeLocation(const FVector& InHomeLocation) { HomeLocation = InHomeLocation; }

	/** Who it is currently trying to kill. Null when it is idle. */
	AActor* GetAggroTarget() const { return AggroTarget.Get(); }

	// ── Idle movement (B-10 part 2) ─────────────────────────────────────

	/**
	 * Give it a patrol route, a leader or a roam radius (one of them, in that
	 * order of precedence), and start from stop 0 with no pause pending.
	 * Called by its spawn point right after it spawns; server only.
	 * @param Leader The spawn point whose NPC this one follows, or null.
	 */
	void ConfigureIdleMovement(const FValhallaNPCPatrolSetup& Setup, AValhallaNPCSpawner* Leader);

	/** Its idle walking pace, cm/s: the template's move speed times the setup's speed fraction. What a follower matches. */
	float GetIdleWalkSpeed() const;

	/** The stop it is walking to (or pausing at); INDEX_NONE without a route. For logs and tests. */
	int32 GetPatrolTargetIndex() const { return IdleMode == EValhallaNPCIdleMode::Route ? PatrolTargetIndex : INDEX_NONE; }

	/** Which idle movement it has. */
	EValhallaNPCIdleMode GetIdleMode() const { return IdleMode; }

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

	/**
	 * The leash (and the chase give-up): drop the target and all threat and
	 * start walking back to where the fight started. No teleport and no heal
	 * yet; FinishReturn heals it when it gets there. Kevin, 2026-09-24: a
	 * leashed NPC walks back, and can be pulled again on the way.
	 * @param GaveUpOn The target it could not reach, or null for a leash.
	 *                 Walking near it again does not re-pull the NPC on this
	 *                 return (a hit still does), or a stuck NPC would give up,
	 *                 re-aggro and give up again every few seconds.
	 */
	void StartReturn(const TCHAR* Why, AActor* GaveUpOn);

	/** Back where the fight started: full heal, the return and the fight are over. */
	void FinishReturn();

	/** Turn in place toward Point at the movement component's turn rate. Server only. */
	void TurnToward(const FVector& Point, float DeltaSeconds);

	/** No sight blocker (VisionBlocker channel, eye height) between this NPC and Target. */
	bool HasLineOfSightTo(const AActor* Target) const;

	/**
	 * Read `spriteSize` and `spriteColor` onto the body, and put the fixed kit
	 * on. Runs on every end from BeginPlay; idempotent.
	 */
	void ApplyAppearance();

	/**
	 * Scale the capsule with the body. Before this, `spriteSize` scaled only the
	 * mesh, so anything above 1.0 stood its feet below the capsule — and the
	 * floor. Server and client both run it; the capsule is not replicated.
	 */
	void ApplyBodyScale();

	/** The template's scale, replicated so clients size the capsule the same. */
	UPROPERTY(ReplicatedUsing = OnRep_BodyScale)
	float BodyScale = 1.f;

	UFUNCTION()
	void OnRep_BodyScale();

	/** The template's tint as packed 0xRRGGBB, replicated for the same reason. */
	UPROPERTY(ReplicatedUsing = OnRep_BodyScale)
	int32 BodyColor = 0;

	/**
	 * The template's `weaponId` (an items.json weapon), replicated so every
	 * machine draws the same prop and plays the same attack. NAME_None: unarmed.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_BodyScale)
	FName WeaponId;

	/** The weapon id WeaponMesh currently shows, so a reapply only reloads on change. */
	FName AppliedWeaponId;

	/** Draws WeaponId into WeaponMesh and sets the attack cycle it implies. */
	void ApplyWeaponVisual();

	/** B-06 1.9a: put the override body on BodyMesh (no head). False when this type has none. */
	bool ApplyBodyOverride();

	/** B-06 1.9a: hang WeaponMesh on the override body with this type's frame for Grip. */
	void AttachWeaponToOverrideBody(int32 GripIndex);

	/** The animated body — the same rig and the same skeleton a player uses. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Appearance")
	TObjectPtr<USkeletalMeshComponent> BodyMesh;

	/** The active body's separate head (MetaHuman face), following the body. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Appearance")
	TObjectPtr<USkeletalMeshComponent> HeadMesh;

	/** `SK_chest_priests_chain`, a follower of BodyMesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Appearance")
	TObjectPtr<USkeletalMeshComponent> ChestMesh;

	/** `SK_helm_iron_full`, a follower of BodyMesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Appearance")
	TObjectPtr<USkeletalMeshComponent> HelmMesh;

	/** Legs armour, a follower of BodyMesh. Set from LegsMeshAsset. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Appearance")
	TObjectPtr<USkeletalMeshComponent> LegsMesh;

	/** Boots, a follower of BodyMesh. Set from BootsMeshAsset. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Appearance")
	TObjectPtr<USkeletalMeshComponent> BootsMesh;

	/** Gloves, a follower of BodyMesh. Set from GlovesMeshAsset. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Appearance")
	TObjectPtr<USkeletalMeshComponent> GlovesMesh;

	/** The template's weapon, in the right hand (socket_weapon_r), like a player's. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Appearance")
	TObjectPtr<UStaticMeshComponent> WeaponMesh;

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

	/**
	 * NPCSystem.ts:241 — the walk home after a fight ends (target dead or gone)
	 * is 2/3 of the chase speed. A leashed NPC walks back at full speed.
	 */
	static constexpr float ReturnSpeedFraction = 0.66f;

private:
	/** The template this NPC was built from. Copied, not referenced. */
	FValhallaNPCTemplate Template;

	/** NPCSystem `spawnX/spawnY` — the respawn point, and where an idle NPC stands (a fight's leash measures from FightStart). */
	FVector HomeLocation = FVector::ZeroVector;

	/** The spawner that made it. Weak: deleting a spawner must not delete its NPCs mid-frame. */
	TWeakObjectPtr<AValhallaNPCSpawner> Spawner;

	/** NPCSystem `aggroTarget`. */
	TWeakObjectPtr<AActor> AggroTarget;

	/** NPCSystem `threatTable` — cumulative damage per player. */
	TMap<TWeakObjectPtr<AActor>, float> ThreatTable;

	/** NPCSystem `respawnAt`, in server-time seconds. 0 while alive. */
	double RespawnAt = 0.0;

	/** See IsInitialized. */
	bool bInitializedFromTemplate = false;

	/** NPCSystem `lastAttackTime`, in server-time seconds. */
	double LastAttackTime = 0.0;

	/** Server-only buff list. SyncedBuffs is the client's censored view of it. */
	TArray<FValhallaActiveBuff> ActiveBuffs;

	/** Who last damaged it, so a DoT kill still credits someone. */
	TWeakObjectPtr<AActor> LastAttacker;

	/** Resolved once from the template so the leash check is not a branch per tick. */
	float LeashRange = 600.f;

	/** B-16: the path being walked by the chase or the walk home. Server only. */
	FValhallaNPCPath MovePath;

	/**
	 * B-16: the XY direction to push this step toward Goal — along a nav-mesh
	 * path round obstacles, or straight at it when the ground between is clear
	 * or there is no nav data. Re-plans at most every 0.5 s, or when Goal has
	 * moved 50 cm. Server only.
	 */
	FVector PlanAndSteer(const FVector& Goal, double Now);

	/** B-16: a chasing NPC this close to its target is never "stuck" (crowding at melee range is normal), cm beyond attack range. */
	static constexpr double StuckIgnoreNearCm = 150.0;

	/**
	 * Where the current fight started: the NPC's own position the step it first
	 * took a target. The leash is measured from here and a leashed NPC walks
	 * back here. It stays set through a re-pull on the way back (so a fight
	 * cannot be dragged further out one leash at a time) and is cleared when
	 * the NPC gets back, dies or respawns. For a patrolling, following or
	 * roaming NPC (B-10 part 2) it is wherever on its walk it was pulled, so
	 * after the fight it resumes the walk from there.
	 */
	FVector FightStart = FVector::ZeroVector;
	bool bHasFightStart = false;

	/** Leashed or gave up, and walking back to FightStart. Server only. */
	bool bReturning = false;

	/** The target a stuck chase gave up on; see StartReturn. */
	TWeakObjectPtr<AActor> GaveUpOn;

	/** A ranged NPC standing its ground (in range and in sight); see FValhallaNPCCombatRules::ShouldChase. */
	bool bHoldingPosition = false;

	/**
	 * B-10 social aggro: true once this NPC has called for help in the current
	 * fight. Cleared when it has no target any more, leashes, dies or respawns.
	 * One call per fight is what makes chains finite.
	 */
	bool bCalledForHelp = false;

	/**
	 * B-10: call same-group NPCs within the template's social range that can
	 * see this one and are not already fighting; each that answers takes
	 * Target (1 threat, like a proximity pull). Server only. Returns how many
	 * joined.
	 */
	int32 CallForHelp(AActor* Target);

	/** B-10: answer another NPC's call — take Target as if it had walked into aggro range. */
	void JoinFight(AActor* Target, const AValhallaNPC* Caller);

	// ── B-10 part 2: idle movement (server only) ────────────────────────

	/** Route, Follow, Wander or None; set by ConfigureIdleMovement. */
	EValhallaNPCIdleMode IdleMode = EValhallaNPCIdleMode::None;

	/** The spawn point's setup, world space, held by value. */
	FValhallaNPCPatrolSetup Patrol;

	/** The spawn point whose NPC this one follows (Follow mode). Weak, like Spawner. */
	TWeakObjectPtr<AValhallaNPCSpawner> FollowLeader;

	/** The route stop being walked to, or paused at; see FValhallaNPCPatrolRules::NextStop. */
	int32 PatrolTargetIndex = 0;

	/** PingPong's direction along the route, +1 or -1. */
	int32 PatrolDirection = 1;

	/** Server time the current pause at a stop (or a roam goal) ends; standing still until then. */
	double IdlePauseUntil = 0.0;

	/** Wander: the goal being walked to, when bHasWanderGoal. */
	FVector WanderGoal = FVector::ZeroVector;
	bool bHasWanderGoal = false;

	/** Follow: walking (true) or standing behind the leader; the hysteresis in FollowerShouldMove. */
	bool bFollowWalking = false;

	/** Back to stop 0, no pause, no roam goal. Respawn and ConfigureIdleMovement. */
	void ResetIdleState();

	/**
	 * The idle layer, one fixed step: only called with no target, not
	 * returning and no FightStart pending (see the walk-back block in
	 * ServerFixedTick). Returns false when this NPC has no idle movement, so
	 * the caller does the old stand-at-home.
	 */
	bool TickIdleMovement(float FixedDeltaSeconds, double Now);

	void TickPatrolRoute(double Now);
	void TickFollow(float FixedDeltaSeconds, double Now);
	void TickWander(double Now);

	/**
	 * One idle step toward Goal along a nav path at InputScale of the move
	 * speed (slowing over the last half metre). Returns true when the stuck
	 * clock ran out: the caller skips the stop or picks another goal, it does
	 * not warp.
	 */
	bool IdleWalkToward(const FVector& Goal, float InputScale, double Now);

	/** Stand still this step: clear the stuck clock and any plan. */
	void IdleStand();

	/** Pick a roam goal round home: a random reachable nav point, or FValhallaNPCPatrolRules::WanderPoint without nav. */
	bool PickWanderGoal(FVector& OutGoal) const;

public:
	/** B-10: the template's social group (the template id when blank). */
	FName GetSocialGroup() const { return Template.SocialGroup.IsNone() ? TemplateId : Template.SocialGroup; }

	/** B-10: whether this NPC has a target or anyone on its threat table. */
	bool IsEngaged() const { return AggroTarget.IsValid() || ThreatTable.Num() > 0; }

	/** Leashed and walking back to where its fight started. */
	bool IsReturning() const { return bReturning; }
};
