// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ValhallaGameTypes.h"
#include "ValhallaVisuals.h"
#include "ValhallaAnimComponent.generated.h"

class UAnimSequence;
class USkeletalMeshComponent;
class UValhallaAnimInstance;
class UValhallaSkillComponent;

/** The locomotion cycle a body is in, from its ground speed (UValhallaAnimComponent::ChooseGait). */
enum class EValhallaGait : uint8
{
	Idle,
	Walk,
	Jog,
};

/** "Idle", "Walk", "Jog". For the log. */
VALHALLAGAME_API const TCHAR* LexToString(EValhallaGait Gait);

/**
 * The animation state machine: five states, driving a blended node tree.
 *
 * WHY THIS IS A COMPONENT AND NOT THE UAnimInstance ITSELF
 *
 * The *state* — what should be playing and why — reads from the owner's
 * velocity, the replicated cast state, the combat events and the death events.
 * None of that belongs on an anim instance, which is a thing owned by a mesh
 * and recreated whenever the mesh's anim class changes. Keeping the machine on
 * the actor means one code path that is identical for the player, for a
 * simulated proxy of another player, and for an NPC.
 *
 * What changed in Phase 8a is what it drives. Phase 4c drove
 * `USkeletalMeshComponent::PlayAnimation` — single-node playback, one clip at a
 * time, no blending: Idle to Walk was a cut and a swing replaced the walk cycle
 * outright. It now drives UValhallaAnimInstance, a native UAnimInstance whose
 * proxy owns a real four-node blend tree (see that class's comment for why a
 * native anim instance *can* evaluate a tree and how). The component's public
 * surface is unchanged; PlaySwing, PlayAction, PlayHitReaction and SetDead mean
 * exactly what they meant, and now blend.
 *
 * WHERE THE STATE COMES FROM
 *
 * Nothing here is replicated, because nothing here needs to be. Every input is
 * already on the wire for another reason:
 *
 *   locomotion  the owner's velocity, which is replicated movement
 *   casting     UValhallaSkillComponent::CastingSkillId, replicated for the
 *               cast bar — polled, because the *state* is what matters and a
 *               dropped unreliable event would leave a cast pose stuck on
 *   swings      the hit/miss combat events, which are already multicast
 *   death       the death/respawn combat events
 *
 * so a simulated proxy of a wizard two hundred metres away animates from the
 * same four sources the local player does, and an NPC animates from the last
 * two. Adding a replicated "current animation" byte would be a fifth source of
 * truth that could disagree with the other four.
 */
UCLASS(ClassGroup = (Valhalla), meta = (BlueprintSpawnableComponent))
class VALHALLAGAME_API UValhallaAnimComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UValhallaAnimComponent();

	//~ Begin UActorComponent interface
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent interface

	/**
	 * The mesh this drives. Followers are not touched: they copy the leader's
	 * pose through SetLeaderPoseComponent and have no animation of their own.
	 */
	void SetBodyMesh(USkeletalMeshComponent* InBodyMesh);

	/**
	 * B-06 1.9a: load every clip from this folder instead of the active body's
	 * (a non-player body, e.g. /Game/Valhalla/Characters/Goblin/Animations).
	 * The folder holds the same clip names, retargeted onto that body's
	 * skeleton. Empty = the active body's own folder. Call before BeginPlay;
	 * a later call reloads the clips.
	 */
	void SetAnimFolderOverride(const FString& InFolder);

	/**
	 * Which cycle a swing plays, from the equipped weapon's `weaponStyle`.
	 * Set by the owner whenever the weapon slot changes.
	 */
	void SetAttackCycle(EValhallaAttackCycle InCycle) { AttackCycle = InCycle; }

	/**
	 * Which cycle this body's weapon swings. Read by UValhallaVfxLibrary so the
	 * swing effect and the swing animation cannot disagree.
	 */
	EValhallaAttackCycle GetAttackCycle() const { return AttackCycle; }

	/** A swing, a shot or a cast gesture, depending on the weapon. One shot. */
	void PlaySwing();

	/**
	 * What the hands hold, set by the owner whenever the weapon or offhand
	 * slot changes: the melee auto-attack to play (UValhallaVisuals::
	 * AttackAnimForWeapon) and which stance layers to lay over everything —
	 * right fist around a weapon, left fist around a bow, left arm carrying a
	 * shield.
	 */
	void SetWeaponLoadout(EValhallaAnim InAttackAnim, bool bInGripRight, bool bInGripLeft, bool bInShieldArm);

	/** One shot of a named animation, interrupting whatever action is running. */
	void PlayAction(EValhallaAnim Anim, float BlendInSeconds = -1.f);

	/**
	 * A flinch. Refused while an action or a cast is running, because a player
	 * being beaten on while casting should look like they are casting, not like
	 * they are being juggled — and because interrupting the swing that is
	 * mid-flight reads as the attacker's swing failing.
	 */
	void PlayHitReaction();

	/**
	 * A chat emote (/wave /cheer /bow). Plays over whatever the body is doing
	 * unless it is dead or holding a cast, and is dropped the moment the
	 * character starts to move.
	 */
	void PlayEmote(EValhallaAnim Emote);

	/**
	 * The defender's side of a blocked or dodged blow. Replaces a hit reaction
	 * or an emote, never a swing, a cast or a sit.
	 */
	void PlayDefense(EValhallaAnim Defense);

	/**
	 * /sit and standing up. Sitting plays A_Sit and holds its last frame on the
	 * action layer (as a corpse holds A_Death); standing blends it out.
	 * Driven by AValhallaCharacter's replicated bSitting.
	 */
	void SetSitting(bool bInSitting);
	bool IsSitting() const { return bSitting; }

	/** Fall over and stay down, or get back up. Idempotent. */
	void SetDead(bool bInDead);

	/** True while a one-shot action is still running. */
	bool IsPlayingAction() const { return bActionPlaying; }

	/** What is on screen right now. For the log and for tests. */
	EValhallaAnim GetCurrentAnim() const { return CurrentAnim; }

	/** The asset name of the current animation, e.g. `A_Cast`. For the gate log. */
	FString DescribeCurrentAnim() const;

	/**
	 * `A_Walk 0.42 + A_Attack 1.00` — what is actually on the body right now,
	 * with both blend weights. This is the line the Phase 8a gate reads to
	 * prove the crossfade is a crossfade and not a cut.
	 */
	FString DescribeBlend() const;

	/** The blend tree this drives. Null before the mesh has one. */
	UValhallaAnimInstance* GetAnimInstance() const;

	// ── Event routing ───────────────────────────────────────────────────

	/**
	 * Turn one combat event into animation on the actors it names.
	 *
	 * Called from AValhallaGameState::RecordCombatEvent, which is the single
	 * funnel every combat event passes through on every end — server, listen
	 * host and every client — so this needs no separate replication and cannot
	 * drift from what the combat log says happened.
	 */
	static void DispatchCombatEvent(const UObject* WorldContext, const FValhallaCombatEvent& Event);

	/** The anim component on an actor, or null. */
	static UValhallaAnimComponent* Find(AActor* Actor);

	// ── Gait ────────────────────────────────────────────────────────────

	/**
	 * The locomotion cycle for a ground speed, cm/s (Project Settings >
	 * Valhalla > Locomotion supplies the three numbers):
	 *
	 *   Speed < MovingSpeed                       Idle
	 *   otherwise, from Idle or Walk              Jog if Speed >= JogSpeed, else Walk
	 *   otherwise, from Jog                       Walk only once Speed < JogSpeed - Hysteresis
	 *
	 * so exactly JogSpeed jogs, and a jog slowing through the threshold keeps
	 * jogging for Hysteresis cm/s before it walks. Pure; pinned by
	 * Valhalla.Game.Anim.Gait.
	 */
	static EValhallaGait ChooseGait(float Speed, EValhallaGait Previous, float MovingSpeed, float JogSpeed, float Hysteresis);

	/** The gait the body is in now. */
	EValhallaGait GetGait() const { return Gait; }

protected:
	/** Point the locomotion blend at Idle or Walk. */
	void SetLocomotion(EValhallaAnim Anim);

	/**
	 * Put the mesh on UValhallaAnimInstance and hand it the two looping clips.
	 * Idempotent, and cheap after the first call.
	 */
	void EnsureAnimInstance();

	/**
	 * Idle, walk or jog, from the owner's ground speed (ChooseGait), and the
	 * cycle's play rate matched to that speed.
	 */
	void TickLocomotion();

	/** Poll the skill component's replicated cast state. */
	void TickCastHold();

	/**
	 * Has the current one-shot run for its clip's full length?
	 *
	 * Compares the world clock against a deadline taken when the action
	 * started; see the implementation for why neither of the two obvious
	 * alternatives works.
	 */
	bool IsActionFinished() const;

	/** The sequence for a logical animation, or null if it failed to load. */
	UAnimSequence* Sequence(EValhallaAnim Anim) const;

	/** Load all seven sequences. Cheap after the first character in a session. */
	void LoadSequences();

	/** Push the stance flags to the anim instance (all off while dead). */
	void ApplyStance();

	/** The cast being held ended by interruption: lower, do not release. */
	void NoteCastInterrupted();

private:
	/** The leader mesh. Weak in spirit — it is a sibling component, not owned. */
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> BodyMesh;

	/** Index is (uint8)EValhallaAnim. Null entries are a missing asset, logged once. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimSequence>> Sequences;

	/** The owner's skill component, if it has one. NPCs do not. */
	UPROPERTY(Transient)
	TObjectPtr<UValhallaSkillComponent> SkillComponent;

	/** The blend tree. Created by EnsureAnimInstance on the body mesh. */
	UPROPERTY(Transient)
	TObjectPtr<UValhallaAnimInstance> AnimInstance;

	/** The locomotion state, Idle or Walk. */
	EValhallaAnim LocomotionAnim = EValhallaAnim::Idle;

	/** What the action layer last started, or the locomotion state if none. */
	EValhallaAnim CurrentAnim = EValhallaAnim::Idle;

	/** Which swing the equipped weapon produces. */
	EValhallaAttackCycle AttackCycle = EValhallaAttackCycle::Melee;

	EValhallaAnim WeaponAttackAnim = EValhallaAnim::Attack;
	bool bStanceGripRight = false;
	bool bStanceGripLeft = false;
	bool bStanceShieldArm = false;

	/** The locomotion cycle, from ChooseGait. Walk and Jog both drive the blend's moving slot. */
	EValhallaGait Gait = EValhallaGait::Idle;

	/** What the current cast hold is, so its end plays the matching release. */
	enum class EHeldCast : uint8 { None, Spell, Staff, Bow };
	EHeldCast HeldCast = EHeldCast::None;
	bool bCastInterrupted = false;

	/** True between PlayAction and the sequence running out. */
	bool bActionPlaying = false;

	/** World time the current one-shot ends. Only meaningful while bActionPlaying. */
	double ActionEndTime = 0.0;

	/** True while the cast pose is held open for a timed cast. */
	bool bHoldingCast = false;

	/** True from the death event until the respawn one. */
	bool bDead = false;

	/** See SetSitting. */
	bool bSitting = false;

	/** The running action is an emote: movement cancels it. */
	bool bEmoteAction = false;

	/** So a failed load is complained about once rather than every frame. */
	bool bLoaded = false;

	/** True once the mesh has been given its anim instance. */
	bool bStarted = false;

	/** See SetAnimFolderOverride. */
	FString AnimFolderOverride;
};
