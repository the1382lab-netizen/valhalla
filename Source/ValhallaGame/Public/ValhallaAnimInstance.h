// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNodes/AnimNode_TwoWayBlend.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "ValhallaAnimInstance.generated.h"

class UAnimSequence;

/**
 * A three-node blend tree, built in C++, with no AnimBlueprint asset.
 *
 * ## Why this exists
 *
 * Phase 4c drove the body with `USkeletalMeshComponent::PlayAnimation`, which
 * is single-node playback: one clip, no blending, no layering. Idle to Walk was
 * a cut and a swing replaced the walk cycle outright. Phase 4c's own class
 * comment named the two honest ways out — an AnimBlueprint asset, or something
 * that evaluates a real node tree — and said Phase 8 would pick one.
 *
 * This is the second. An AnimBlueprint would have meant a `.uasset` holding the
 * state machine, which is exactly the thing this project has refused everywhere
 * else: a binary file whose contents cannot be reviewed in a diff and which has
 * to be kept in step with the C++ that drives it by hand.
 *
 * ## How it works
 *
 * A plain `UAnimInstance` subclass evaluates to the reference pose, because the
 * anim *graph* lives on a UAnimBlueprintGeneratedClass and a native class has
 * none. But `FAnimInstanceProxy` supports a **custom root node**: set
 * `RootNode` to a node you own and list your nodes from `GetCustomNodes`, and
 * the engine initialises, updates, caches bones for and evaluates that tree
 * exactly as if it had come from a graph. This is not a trick — it is the
 * mechanism `FAnimSequencerInstanceProxy` uses to blend Sequencer tracks, in
 * `AnimGraphRuntime`, for the same reason.
 *
 * The tree is four nodes:
 *
 *     ActionBlend (A) <- LocomotionBlend (A) <- IdlePlayer
 *                                       (B) <- WalkPlayer
 *                 (B) <- ActionPlayer
 *
 * `LocomotionBlend.Alpha` crossfades Idle into Walk over
 * LocomotionBlendSeconds. `ActionBlend.Alpha` layers a one-shot — a swing, a
 * cast, a flinch, a death — over whatever locomotion is doing, so a character
 * who swings while walking keeps walking. Both alphas are interpolated on the
 * game thread in NativeUpdateAnimation and pushed into the proxy there, which
 * is where an AnimBlueprint's own variables would be written from.
 *
 * ## What still is not here
 *
 * There is no upper-body bone mask, so an action blends over the *whole* body
 * rather than over the arms only. That needs a `UBlendProfile` or a
 * `LayeredBoneBlend` with a bone list, and a bone list is a content decision
 * about this particular rig — Phase 8b's, not this pass's.
 */
USTRUCT()
struct VALHALLAGAME_API FValhallaAnimInstanceProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

	FValhallaAnimInstanceProxy() = default;

	explicit FValhallaAnimInstanceProxy(UAnimInstance* InAnimInstance)
		: FAnimInstanceProxy(InAnimInstance)
	{
	}

	//~ Begin FAnimInstanceProxy interface
	/** Link the tree. The engine has already taken its root by this point. */
	virtual void Initialize(UAnimInstance* InAnimInstance) override;

	/**
	 * The root of the tree.
	 *
	 * This is the supported hook: `FAnimInstanceProxy::Initialize` assigns its
	 * private `RootNode` from here whenever there is no
	 * UAnimBlueprintGeneratedClass behind the instance, which for a native
	 * UAnimInstance subclass is always. Everything downstream — initialisation,
	 * bone caching, update and evaluation — then walks this node as if it had
	 * come out of a compiled anim graph.
	 */
	virtual FAnimNode_Base* GetCustomRootNode() override { return &ActionBlend; }

	/**
	 * Every node in the tree, so the engine can run its per-node
	 * initialisation. Without this the nodes are never told which anim
	 * instance they belong to and the tree evaluates to the reference pose.
	 */
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override;
	//~ End FAnimInstanceProxy interface

	/** `A_Idle`, looping. */
	FAnimNode_SequencePlayer_Standalone IdlePlayer;

	/** `A_Walk`, looping. */
	FAnimNode_SequencePlayer_Standalone WalkPlayer;

	/** Idle (A) into Walk (B). */
	FAnimNode_TwoWayBlend LocomotionBlend;

	/** The one-shot: a swing, a cast, a flinch, or the death pose. */
	FAnimNode_SequencePlayer_Standalone ActionPlayer;

	/** Locomotion (A) under the action (B). */
	FAnimNode_TwoWayBlend ActionBlend;
};

/**
 * The game-thread face of the blend tree.
 *
 * Owns the sequences (so the garbage collector can see them), owns the two
 * alphas and their interpolation, and is the only thing UValhallaAnimComponent
 * talks to. It has no opinion about *when* anything plays; that is still the
 * component's state machine, unchanged from Phase 4c.
 */
UCLASS()
class VALHALLAGAME_API UValhallaAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	//~ Begin UAnimInstance interface
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	//~ End UAnimInstance interface

	/** The two looping clips the locomotion blend crossfades between. */
	void SetLocomotionSequences(UAnimSequence* Idle, UAnimSequence* Walk);

	/** 0 for standing still, 1 for walking. Reached over LocomotionBlendSeconds. */
	void SetWalking(bool bWalking) { WalkTarget = bWalking ? 1.f : 0.f; }

	/**
	 * Layer a clip over the locomotion, from its first frame.
	 *
	 * Always restarts, even when the same clip is already playing: two swings
	 * in a row are two swings, and a fast weapon that skipped the second would
	 * look slow.
	 */
	void PlayAction(UAnimSequence* Sequence, bool bLoop);

	/** Blend the action back out over ActionBlendOutSeconds. */
	void StopAction();

	/** True while the action layer has any weight at all. */
	bool IsActionBlended() const { return ActionAlpha > KINDA_SMALL_NUMBER; }

	/** 0..1 through the Idle -> Walk crossfade. For the gate log and tests. */
	float GetWalkAlpha() const { return WalkAlpha; }

	/** 0..1 of the action layer's weight. For the gate log and tests. */
	float GetActionAlpha() const { return ActionAlpha; }

	// ── Tuning ──────────────────────────────────────────────────────────

	/** Idle <-> Walk crossfade, seconds. The Phase 8a brief's 0.15. */
	static constexpr float LocomotionBlendSeconds = 0.15f;

	/**
	 * How fast an action takes over, seconds.
	 *
	 * Much shorter than the locomotion blend on purpose: a swing that eases in
	 * over 0.15 s has visibly not started when the damage number appears, and a
	 * hit reaction that eases in does not read as a flinch at all.
	 */
	static constexpr float ActionBlendInSeconds = 0.06f;

	/** How fast it hands the body back, seconds. */
	static constexpr float ActionBlendOutSeconds = 0.12f;

protected:
	/** Move Current toward Target at 1/BlendSeconds per second. */
	static float StepAlpha(float Current, float Target, float BlendSeconds, float DeltaSeconds);

private:
	/** Hard references, so the sequences cannot be collected under the proxy. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> IdleSequence;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> WalkSequence;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> ActionSequence;

	float WalkAlpha = 0.f;
	float WalkTarget = 0.f;

	float ActionAlpha = 0.f;
	float ActionTarget = 0.f;

	bool bActionLoop = false;

	/** Set by PlayAction, consumed by the next NativeUpdateAnimation. */
	bool bActionRestartPending = false;
};
