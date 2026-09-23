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
 * Facing (controls rework, 2026-09-22). The mouse no longer aims: 1.0's
 * cursor-driven `aimAngle` is gone. Two things turn a character, and only two:
 *
 *   Walking. bOrientRotationToMovement is on (RotationRate 720 deg/s), so WASD
 *   turns the body towards where it is walking. CharacterMovement predicts the
 *   turn on the owning client, re-simulates it on the server from the same
 *   moves, and ReplicatedMovement carries it to everyone else.
 *
 *   The right-mouse drag. Orbiting the camera also turns the body to face the
 *   way the camera looks (SetFacingYawFromCamera): applied locally at once and
 *   sent to the server through the throttled ServerSetFacingYaw. Only while
 *   standing still — when walking, orient-to-movement wins and the drag only
 *   swings the camera (and with it the WASD basis).
 *
 * Nothing else writes the yaw. In particular the server never turns a player to
 * face a combat target: a swing or a targeted cast at something the player is
 * not facing fails with "You must be facing your target"
 * (UValhallaCombatLibrary::IsFacing).
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
	/** Swaps in the active body profile (and re-hangs the held props) before the components start. */
	virtual void PostInitializeComponents() override;
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

	// ── Facing ──────────────────────────────────────────────────────────

	/**
	 * Turn this character to a world-space yaw because the camera turned — the
	 * right-mouse drag. Owning client only.
	 *
	 * Ignored while the character is walking (acceleration or more than a
	 * crawl of velocity): orient-to-movement owns the yaw then, and writing it
	 * here would fight CharacterMovement every frame. Otherwise applies at once,
	 * so the player never sees their own turn lag, and forwards to the server at
	 * most FacingSendRateHz times a second and only once the yaw has moved
	 * FacingYawDeadzoneDegrees; a send the throttle held back is flushed by
	 * FlushFacingYaw.
	 */
	void SetFacingYawFromCamera(float NewYaw);

	/**
	 * Send a facing yaw the throttle held back. bForce sends the current yaw
	 * even if it is inside the deadzone (the end of a drag). Called every frame
	 * by the owning controller, and with bForce when the drag ends.
	 */
	void FlushFacingYaw(bool bForce = false);

	/** The yaw this character is currently facing. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Facing")
	float GetFacingYaw() const { return GetActorRotation().Yaw; }

	/** True while CharacterMovement is walking this character (it owns the yaw then). */
	bool IsWalkingForFacing() const;

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
	 * The boom's rotation is absolute and never replicated; the WASD basis
	 * (GetCameraWorldYaw) is read from it client-side. The controller follows
	 * each orbit step with SetFacingYawFromCamera, which is what turns the body
	 * with the camera. Pitch is camera-only, and clamped so the camera can
	 * neither go under the floor nor look straight down the character's neck.
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
	/**
	 * Server-side half of SetFacingYawFromCamera. Reliable, although the next
	 * one supersedes it: the server's yaw is what IsFacing reads, so the last
	 * send of a drag must not be lost. The throttle and deadzone keep it to at
	 * most FacingSendRateHz, and only while a drag is actually turning.
	 */
	UFUNCTION(Server, Reliable)
	void ServerSetFacingYaw(float NewYaw);

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

	/** The active body's separate head (MetaHuman face), following the body. Empty for bodies with their own head. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Appearance")
	TObjectPtr<USkeletalMeshComponent> HeadMesh;

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

	/** Yaw change, in degrees, below which the facing RPC is not worth sending. */
	static constexpr float FacingYawDeadzoneDegrees = 2.f;

	/** Ceiling on facing RPCs per second. */
	static constexpr float FacingSendRateHz = 20.f;

	/** Rotation rate of orient-to-movement, degrees per second of yaw. */
	static constexpr float FacingTurnRateDegrees = 720.f;

	/** Below this 2D speed (cm/s), with no input, the character counts as standing still. */
	static constexpr float FacingStillSpeed = 5.f;

private:
	/** The last yaw actually sent to the server. */
	float LastSentFacingYaw = 0.f;

	/** World time of the last facing RPC, for the 20 Hz throttle. */
	double LastFacingSendTime = 0.0;

	/** A camera turn the throttle has not sent yet. */
	bool bFacingSendPending = false;

	/** The class ApplyClassAppearance last ran for, so it is not redone per frame. */
	FName AppliedClassId;

	/**
	 * What each slot was last resolved to, so RefreshEquipmentVisuals can do
	 * nothing when nothing changed. Indexed by (uint8)EValhallaEquipSlot.
	 */
	TArray<FName> AppliedEquipment;
};
