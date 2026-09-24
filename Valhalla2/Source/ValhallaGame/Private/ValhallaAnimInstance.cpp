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

	// Stance layer: three branch-filtered static poses over the action blend.
	GripRightPlayer.SetLoopAnimation(true);
	GripLeftPlayer.SetLoopAnimation(true);
	ShieldArmPlayer.SetLoopAnimation(true);
	HandLayer.BasePose.SetLinkNode(&ActionBlend);
	if (HandLayer.BlendPoses.Num() == 0)
	{
		HandLayer.AddPose();
		HandLayer.AddPose();
		HandLayer.AddPose();
	}
	HandLayer.BlendPoses[0].SetLinkNode(&GripRightPlayer);
	HandLayer.BlendPoses[1].SetLinkNode(&GripLeftPlayer);
	HandLayer.BlendPoses[2].SetLinkNode(&ShieldArmPlayer);
	auto Fingers = [](FInputBlendPose& Layer, const TCHAR* Side)
	{
		Layer.BranchFilters.Reset();
		for (const TCHAR* Root : { TEXT("thumb_01"), TEXT("index_metacarpal"), TEXT("middle_metacarpal"), TEXT("ring_metacarpal"), TEXT("pinky_metacarpal") })
		{
			FBranchFilter Filter;
			Filter.BoneName = FName(FString::Printf(TEXT("%s_%s"), Root, Side));
			Filter.BlendDepth = 0;
			Layer.BranchFilters.Add(Filter);
		}
	};
	Fingers(HandLayer.LayerSetup[0], TEXT("r"));
	Fingers(HandLayer.LayerSetup[1], TEXT("l"));
	{
		FBranchFilter Arm;
		Arm.BoneName = TEXT("upperarm_l");
		Arm.BlendDepth = 0;
		HandLayer.LayerSetup[2].BranchFilters = { Arm };
	}
	for (float& Weight : HandLayer.BlendWeights)
	{
		Weight = 0.f;
	}

	AdditivePlayer.SetLoopAnimation(false);
	AdditiveLayer.Base.SetLinkNode(&HandLayer);
	AdditiveLayer.Additive.SetLinkNode(&AdditivePlayer);
	AdditiveLayer.Alpha = 0.f;

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
	OutNodes.Add(&GripRightPlayer);
	OutNodes.Add(&GripLeftPlayer);
	OutNodes.Add(&ShieldArmPlayer);
	OutNodes.Add(&HandLayer);
	OutNodes.Add(&AdditivePlayer);
	OutNodes.Add(&AdditiveLayer);
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

void UValhallaAnimInstance::SetStancePoses(UAnimSequence* GripRight, UAnimSequence* GripLeft, UAnimSequence* ShieldArm)
{
	StancePoses = { GripRight, GripLeft, ShieldArm };
}

void UValhallaAnimInstance::PlayAction(UAnimSequence* Sequence, bool bLoop, float BlendInSeconds)
{
	if (!Sequence)
	{
		return;
	}

	// An additive clip is a delta, not a pose. It goes on top of whatever the
	// body is already doing, never through the action blend (where it would
	// collapse every bone onto the root).
	if (Sequence->IsValidAdditive())
	{
		AdditiveSequence = Sequence;
		bAdditiveRestartPending = true;
		AdditiveTarget = 1.f;
		return;
	}

	ActionSequence = Sequence;
	bActionLoop = bLoop;
	ActionBlendIn = BlendInSeconds >= 0.f ? BlendInSeconds : ActionBlendInSeconds;
	bActionRestartPending = true;
	ActionTarget = 1.f;
}

void UValhallaAnimInstance::StopAction(float BlendOutSeconds)
{
	ActionBlendOut = BlendOutSeconds >= 0.f ? BlendOutSeconds : ActionBlendOutSeconds;
	ActionTarget = 0.f;
	AdditiveTarget = 0.f;
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
		ActionTarget > ActionAlpha ? ActionBlendIn : ActionBlendOut,
		DeltaSeconds);

	// This is the AnimBlueprint "write your variables in Event Blueprint Update
	// Animation" slot: the game thread writes the proxy's fields here, before
	// the parallel evaluation task that reads them is kicked off.
	FValhallaAnimInstanceProxy& Proxy = GetProxyOnGameThread<FValhallaAnimInstanceProxy>();

	Proxy.IdlePlayer.SetSequence(IdleSequence);
	Proxy.WalkPlayer.SetSequence(WalkSequence);
	Proxy.WalkPlayer.SetPlayRate(WalkPlayRate);

	// Stance layers: a layer with no pose asset stays at zero whatever the target.
	FAnimNode_SequencePlayer_Standalone* StancePlayers[3] = { &Proxy.GripRightPlayer, &Proxy.GripLeftPlayer, &Proxy.ShieldArmPlayer };
	for (int32 Layer = 0; Layer < 3; ++Layer)
	{
		UAnimSequence* Pose = StancePoses.IsValidIndex(Layer) ? StancePoses[Layer].Get() : nullptr;
		StanceAlpha[Layer] = StepAlpha(StanceAlpha[Layer], Pose ? StanceTarget[Layer] : 0.f, StanceBlendSeconds, DeltaSeconds);
		StancePlayers[Layer]->SetSequence(Pose);
		if (Proxy.HandLayer.BlendWeights.IsValidIndex(Layer))
		{
			Proxy.HandLayer.BlendWeights[Layer] = StanceAlpha[Layer];
		}
	}
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

	AdditiveAlpha = StepAlpha(
		AdditiveAlpha, AdditiveTarget,
		AdditiveTarget > AdditiveAlpha ? ActionBlendInSeconds : ActionBlendOutSeconds,
		DeltaSeconds);
	if (bAdditiveRestartPending)
	{
		bAdditiveRestartPending = false;
		Proxy.AdditivePlayer.SetSequence(AdditiveSequence);
		Proxy.AdditivePlayer.SetAccumulatedTime(0.f);
	}
	Proxy.AdditiveLayer.Alpha = AdditiveAlpha;
}
