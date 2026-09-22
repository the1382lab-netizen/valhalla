// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaAnimInstance.h"

#include "Animation/AnimSequence.h"

// ─────────────────────────────────────────────────────────────────────────────
//  The proxy: the node tree itself
// ─────────────────────────────────────────────────────────────────────────────

void FValhallaAnimInstanceProxy::Initialize(UAnimInstance* InAnimInstance)
{
	FAnimInstanceProxy::Initialize(InAnimInstance);

	IdlePlayer.SetLoopAnimation(true);
	WalkPlayer.SetLoopAnimation(true);
	ActionPlayer.SetLoopAnimation(false);

	LocomotionBlend.A.SetLinkNode(&IdlePlayer);
	LocomotionBlend.B.SetLinkNode(&WalkPlayer);
	LocomotionBlend.Alpha = 0.f;

	ActionBlend.A.SetLinkNode(&LocomotionBlend);
	ActionBlend.B.SetLinkNode(&ActionPlayer);
	ActionBlend.Alpha = 0.f;

	// A zero-weight child is not updated, so the walk cycle freezes while the
	// character stands still and resumes from the frame it stopped on. That is
	// what an AnimBlueprint does too, and it is the behaviour we want: a walk
	// that restarts from frame 0 on every step-off reads as a stutter.
	//
	// The links are set *after* the base Initialize, which is where the engine
	// takes GetCustomRootNode()'s answer, and *before* UAnimInstance's
	// InitializeRootNode walks the tree. Both orderings matter and neither is
	// obvious, which is why this is the only place either happens.
}

void FValhallaAnimInstanceProxy::GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes)
{
	OutNodes.Add(&IdlePlayer);
	OutNodes.Add(&WalkPlayer);
	OutNodes.Add(&LocomotionBlend);
	OutNodes.Add(&ActionPlayer);
	OutNodes.Add(&ActionBlend);
}

// ─────────────────────────────────────────────────────────────────────────────
//  The instance: game-thread state
// ─────────────────────────────────────────────────────────────────────────────

FAnimInstanceProxy* UValhallaAnimInstance::CreateAnimInstanceProxy()
{
	return new FValhallaAnimInstanceProxy(this);
}

void UValhallaAnimInstance::SetLocomotionSequences(UAnimSequence* Idle, UAnimSequence* Walk)
{
	IdleSequence = Idle;
	WalkSequence = Walk;
}

void UValhallaAnimInstance::PlayAction(UAnimSequence* Sequence, bool bLoop)
{
	if (!Sequence)
	{
		return;
	}

	ActionSequence = Sequence;
	bActionLoop = bLoop;
	bActionRestartPending = true;
	ActionTarget = 1.f;
}

void UValhallaAnimInstance::StopAction()
{
	ActionTarget = 0.f;
}

float UValhallaAnimInstance::StepAlpha(float Current, float Target, float BlendSeconds, float DeltaSeconds)
{
	if (BlendSeconds <= 0.f)
	{
		return Target;
	}

	// Constant rate rather than an exponential ease. An exponential never
	// actually arrives, so a "finished" blend sits at 0.98 forever and the
	// clip it was blending out of keeps contributing a sliver of pose.
	const float Step = DeltaSeconds / BlendSeconds;
	return FMath::Clamp(
		Current + FMath::Clamp(Target - Current, -Step, Step),
		0.f, 1.f);
}

void UValhallaAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	WalkAlpha = StepAlpha(WalkAlpha, WalkTarget, LocomotionBlendSeconds, DeltaSeconds);
	ActionAlpha = StepAlpha(
		ActionAlpha, ActionTarget,
		ActionTarget > ActionAlpha ? ActionBlendInSeconds : ActionBlendOutSeconds,
		DeltaSeconds);

	// This is the AnimBlueprint "write your variables in Event Blueprint Update
	// Animation" slot: the game thread writes the proxy's fields here, before
	// the parallel evaluation task that reads them is kicked off.
	FValhallaAnimInstanceProxy& Proxy = GetProxyOnGameThread<FValhallaAnimInstanceProxy>();

	Proxy.IdlePlayer.SetSequence(IdleSequence);
	Proxy.WalkPlayer.SetSequence(WalkSequence);
	Proxy.LocomotionBlend.Alpha = WalkAlpha;

	if (bActionRestartPending)
	{
		bActionRestartPending = false;
		Proxy.ActionPlayer.SetSequence(ActionSequence);
		Proxy.ActionPlayer.SetLoopAnimation(bActionLoop);

		// Rewind explicitly. Assigning a new sequence does not reset the
		// player's time accumulator, so a 0.667 s swing started while the 2.0 s
		// idle was at t = 1.2 s would begin already past its own end — the same
		// bug Phase 4c's Start() had to work around, in a different mechanism.
		Proxy.ActionPlayer.SetAccumulatedTime(0.f);
	}

	Proxy.ActionBlend.Alpha = ActionAlpha;
}
