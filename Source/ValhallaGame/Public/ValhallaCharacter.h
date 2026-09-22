// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ValhallaCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class UMaterialInstanceDynamic;
class AValhallaPlayerState;
class UValhallaSkillComponent;
class UValhallaAnimComponent;
enum class EValhallaEquipSlot : uint8;

/**
 * The player's body. One per connected client, server-spawned.
 *
 * Facing is aim, not motion. 1.0 stored `aimAngle` on the schema and let the
 * sprite face the cursor while the character strafed in any direction; that is
 * reproduced here by turning off both of Unreal's usual rotation sources
 * (bOrientRotationToMovement and bUseControllerRotationYaw) and driving the
 * actor yaw from the owning client's cursor instead. Movement itself is
 * ordinary CharacterMovement: client-predicted, server-corrected.
 *
 * The body is the 1.0 paperdoll, rebuilt in three dimensions. 1.0 drew a stack
 * of sprite layers in a fixed order — body, legs, chest, boots, gloves, helm,
 * hair, weapon — and 2.0 draws the same stack as one animated body plus seven
 * skinned followers and two rigid props, with the same layer identities and
 * the same `spriteId` keying the same art. Every follower runs
 * SetLeaderPoseComponent(BodyMesh), so there is exactly one pose evaluated per
 * character however much they are wearing, and a piece of armour physically
 * cannot separate from the body that is wearing it.
 */
UCLASS()
class VALHALLAGAME_API AValhallaCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AValhallaCharacter();

	//~ Begin AActor interface
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * Phase 5's anti-cheat boundary: a client is never sent a character it
	 * cannot see. Self, owner and party members are unconditional; everything
	 * else is a line-of-sight trace. See
	 * UValhallaVisibilitySubsystem::IsRelevantForViewer.
	 */
	virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override;
	//~ End AActor interface

	//~ Begin APawn interface
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	//~ End APawn interface

	// ── Aim ─────────────────────────────────────────────────────────────

	/**
	 * Point this character at a world-space yaw.
	 *
	 * Called every frame by the owning AValhallaPlayerController with the yaw
	 * from the cursor. Applies immediately so the local player never sees their
	 * own aim lag, then forwards to the server at most AimSendRateHz times a
	 * second and only once the yaw has moved AimYawDeadzoneDegrees. The server
	 * sets the actor rotation, which reaches the other clients through
	 * ReplicatedMovement.
	 */
	void UpdateAimYaw(float NewAimYaw);

	/** The yaw this character is currently facing. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Aim")
	float GetAimYaw() const { return GetActorRotation().Yaw; }

	// ── Class ───────────────────────────────────────────────────────────

	/**
	 * Pull MaxWalkSpeed and the skin tint out of classes.json for whatever
	 * class the PlayerState says this character is.
	 *
	 * Runs on the server and on every client, driven by the replicated ClassId,
	 * because MaxWalkSpeed is not a replicated property: both ends have to
	 * derive it from the same data or client prediction fights the server.
	 */
	void ApplyClassAppearance();

	// ── Paperdoll ───────────────────────────────────────────────────────

	/**
	 * Re-resolve all nine equipment slots onto the mesh components.
	 *
	 * Runs on BeginPlay and on the OnRep of every `Equip*` field, so a client
	 * sees another player's gear change as soon as the field arrives and
	 * without an extra RPC. Cheap and idempotent: a slot whose item has not
	 * changed since the last pass is skipped before anything is loaded.
	 *
	 * Assets are loaded synchronously on first use, not streamed. There are
	 * twenty-two of them, they are a few hundred kilobytes each, and an async
	 * load would mean a character standing naked for a frame or two every time
	 * somebody equipped something.
	 */
	void RefreshEquipmentVisuals();

	/** The leader mesh. Every follower copies its pose; the anim driver plays it. */
	USkeletalMeshComponent* GetBodyMesh() const { return BodyMesh; }

	/** The animation state machine. Never null. */
	UValhallaAnimComponent* GetAnimComponent() const { return AnimComponent; }

	/** True while the owning PlayerState says this character is up. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Vitals")
	bool IsAlive() const;

	/** Typed accessor for the owning player state. Null before it replicates. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Vitals")
	AValhallaPlayerState* GetValhallaPlayerState() const;

	// ── Skills ──────────────────────────────────────────────────────────

	/** Casting, cooldowns, buffs and the auto-attack loop. Never null. */
	UValhallaSkillComponent* GetSkillComponent() const { return SkillComponent; }

	/**
	 * Drop or stand up a body.
	 *
	 * Phase 2b hid the actor outright, because a rock has no death pose. Now
	 * there is one: `A_Death` plays once and holds its last frame, so the
	 * corpse lies where it fell and is still there to loot. Collision and
	 * movement go off; the mesh stays visible.
	 *
	 * Still not a ragdoll, and deliberately so — the imported meshes have no
	 * physics asset (the import pipeline is told not to generate one), and an
	 * authored death animation is also the only version of a death that looks
	 * the same on every client.
	 *
	 * Called on the server by AValhallaGameMode, and on every client by
	 * OnRep_DeathPresentation, so the two ends agree without an extra RPC.
	 */
	void SetDeathPresentation(bool bDead);

	// ── Camera ──────────────────────────────────────────────────────────

	/** The isometric boom. The controller reads its yaw to rotate WASD. */
	USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	/** The isometric camera. */
	UCameraComponent* GetTopDownCamera() const { return TopDownCamera; }

	/** World yaw of the camera, i.e. what "screen up" means in world space. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Camera")
	float GetCameraWorldYaw() const;

	/**
	 * Orbit the camera around the character — the right-mouse drag.
	 *
	 * Local presentation only: the boom's rotation is absolute and never
	 * replicated, and the one thing gameplay reads from it (the WASD basis, via
	 * GetCameraWorldYaw) is already client-side. Pitch is clamped so the camera
	 * can neither go under the floor nor look straight down the character's neck.
	 */
	void AddCameraOrbit(float DeltaYawDegrees, float DeltaPitchDegrees);

	/** Mouse wheel. Positive steps pull the camera out, negative push it in. */
	void AddCameraZoom(float Steps);

	/** Camera limits. The defaults (-45 pitch, 1500 arm) are the 1.0 isometric view. */
	static constexpr float CameraPitchMin = -80.f;
	static constexpr float CameraPitchMax = -15.f;
	static constexpr float CameraArmMin = 500.f;
	static constexpr float CameraArmMax = 2600.f;
	static constexpr float CameraArmStep = 150.f;

protected:
	/** Server-side half of UpdateAimYaw. Unreliable: the next one supersedes it. */
	UFUNCTION(Server, Unreliable)
	void ServerSetAimYaw(float NewAimYaw);

	/** Boom: fixed length, no lag, no collision probe — this is an RTS camera. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	/** Narrow FOV so the projection reads as isometric without being orthographic. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Camera")
	TObjectPtr<UCameraComponent> TopDownCamera;

	// ── Paperdoll components ────────────────────────────────────────────
	//
	// One leader and nine dependents, in the 1.0 layer order. The seven
	// skeletal followers are all bound to the same skeleton as the body — the
	// import pipeline guarantees that and character_import.verify() checks it —
	// which is the precondition for SetLeaderPoseComponent: a follower on a
	// different skeleton silently renders its own reference pose instead, and
	// the failure looks like armour floating beside a walking character.

	/**
	 * The animated body. This *is* ACharacter::Mesh, not a second component
	 * beside it: everything in the engine that reasons about a character's mesh
	 * (the crouch offset, the base movement, the navmesh agent) looks at
	 * GetMesh(), and a parallel mesh would be invisible to all of it.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Appearance")
	TObjectPtr<USkeletalMeshComponent> BodyMesh;

	/** Hair or a bald cap. Hidden entirely when a helm is worn. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Appearance")
	TObjectPtr<USkeletalMeshComponent> HairMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Appearance")
	TObjectPtr<USkeletalMeshComponent> HelmMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Appearance")
	TObjectPtr<USkeletalMeshComponent> ChestMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Appearance")
	TObjectPtr<USkeletalMeshComponent> LegsMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Appearance")
	TObjectPtr<USkeletalMeshComponent> GlovesMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Appearance")
	TObjectPtr<USkeletalMeshComponent> BootsMesh;

	/** The cloak slot. Skinned, so it follows the spine rather than hanging rigid. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Appearance")
	TObjectPtr<USkeletalMeshComponent> BackMesh;

	/** Held in the right hand, on `socket_weapon_r`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Appearance")
	TObjectPtr<UStaticMeshComponent> WeaponMesh;

	/** Held in the left hand, on `socket_offhand_l`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Appearance")
	TObjectPtr<UStaticMeshComponent> OffhandMesh;

	/** Skin tint from the class's `bodyId`, applied to the body's skin slot. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SkinMaterial;

	/** Idle / Walk / swing / cast / hit / death. See UValhallaAnimComponent. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Appearance")
	TObjectPtr<UValhallaAnimComponent> AnimComponent;

	/** The port of SkillSystem, per character. See UValhallaSkillComponent. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Skills")
	TObjectPtr<UValhallaSkillComponent> SkillComponent;

	/** Assign one slot's art, and log the assignment. Returns true if it changed. */
	bool ApplySlotVisual(EValhallaEquipSlot Slot, FName ItemId);

	/**
	 * Replicated so a client hides a corpse without waiting for the player
	 * state's bAlive to arrive on its own, slower, channel.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_DeathPresentation)
	bool bDeathPresentation = false;

	UFUNCTION()
	void OnRep_DeathPresentation();

	// ── Tuning ──────────────────────────────────────────────────────────

	/** Capsule radius, cm. 1.0's PLAYER_COLLISION_RADIUS was 20 px; 30 fits the kit's 64 cm tiles. */
	static constexpr float CapsuleRadius = 30.f;

	/** Capsule half-height, cm — a 120 cm character, which is what the art measures. */
	static constexpr float CapsuleHalfHeight = 60.f;

	/** Yaw change, in degrees, below which the aim RPC is not worth sending. */
	static constexpr float AimYawDeadzoneDegrees = 2.f;

	/** Ceiling on aim RPCs per second. */
	static constexpr float AimSendRateHz = 20.f;

private:
	/** The last yaw actually sent to the server. */
	float LastSentAimYaw = 0.f;

	/** World time of the last aim RPC, for the 20 Hz throttle. */
	double LastAimSendTime = 0.0;

	/** The class ApplyClassAppearance last ran for, so it is not redone per frame. */
	FName AppliedClassId;

	/**
	 * What each slot was last resolved to, so RefreshEquipmentVisuals can do
	 * nothing when nothing changed. Indexed by (uint8)EValhallaEquipSlot.
	 */
	TArray<FName> AppliedEquipment;
};
