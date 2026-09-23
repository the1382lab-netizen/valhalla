// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaAnimComponent.h"

#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "ValhallaAnimInstance.h"
#include "ValhallaCharacter.h"
#include "ValhallaDataSubsystem.h"
#include "ValhallaGame.h"
#include "ValhallaPlayerState.h"
#include "ValhallaSkillComponent.h"

namespace
{
	constexpr int32 AnimCount = static_cast<int32>(EValhallaAnim::Death) + 1;

	/** The name an animation is logged under. */
	const TCHAR* AnimName(EValhallaAnim Anim)
	{
		switch (Anim)
		{
		case EValhallaAnim::Idle:   return TEXT("A_Idle");
		case EValhallaAnim::Walk:   return TEXT("A_Walk");
		case EValhallaAnim::Attack: return TEXT("A_Attack");
		case EValhallaAnim::Shoot:  return TEXT("A_Shoot");
		case EValhallaAnim::Cast:   return TEXT("A_Cast");
		case EValhallaAnim::Hit:    return TEXT("A_Hit");
		case EValhallaAnim::Death:  return TEXT("A_Death");
		}
		return TEXT("A_Idle");
	}

	/** True for the event kinds that mean "a blow was struck", hit or not. */
	bool IsSwingOutcome(EValhallaCombatEventKind Kind)
	{
		switch (Kind)
		{
		case EValhallaCombatEventKind::PlayerHit:
		case EValhallaCombatEventKind::NpcHit:
		case EValhallaCombatEventKind::Missed:
		case EValhallaCombatEventKind::Dodged:
		case EValhallaCombatEventKind::Blocked:
			return true;
		default:
			return false;
		}
	}

	/**
	 * Did this outcome come from a swing, as opposed to a spell or a DoT tick?
	 *
	 * An auto-attack is flagged in the data. An NPC's attack carries its
	 * *template* id rather than a skill id (AValhallaNPC::ServerFixedTick passes
	 * TemplateId to ApplyDamage), so an id that is not a skill at all is also a
	 * swing — that is how a Test Enemy's punch animates without inventing a
	 * fake skill row for it.
	 */
	bool IsSwingSource(const UObject* WorldContext, FName SkillId)
	{
		const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContext);
		const UValhallaDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
		if (!Data)
		{
			return false;
		}

		const FValhallaSkillTemplate* Skill = Data->FindSkill(SkillId);
		return Skill ? Skill->bIsAutoAttack : true;
	}
}

UValhallaAnimComponent::UValhallaAnimComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	// Presentation only. Replicating it would be a fifth source of truth that
	// could disagree with the four this reads from — see the class comment.
	SetIsReplicatedByDefault(false);
}

void UValhallaAnimComponent::BeginPlay()
{
	Super::BeginPlay();

	LoadSequences();

	if (AActor* Owner = GetOwner())
	{
		SkillComponent = Owner->FindComponentByClass<UValhallaSkillComponent>();
	}

	// Stand the body up in its idle pose immediately rather than one tick later,
	// so a character never appears in the reference pose even for a frame.
	EnsureAnimInstance();
	SetLocomotion(EValhallaAnim::Idle);
}

void UValhallaAnimComponent::SetBodyMesh(USkeletalMeshComponent* InBodyMesh)
{
	BodyMesh = InBodyMesh;
	EnsureAnimInstance();
}

UValhallaAnimInstance* UValhallaAnimComponent::GetAnimInstance() const
{
	return AnimInstance;
}

void UValhallaAnimComponent::EnsureAnimInstance()
{
	if (!BodyMesh || !BodyMesh->GetSkeletalMeshAsset())
	{
		return;
	}

	LoadSequences();

	if (AnimInstance && AnimInstance->GetSkelMeshComponent() == BodyMesh)
	{
		return;
	}

	// SetAnimInstanceClass on a *native* UAnimInstance subclass is the whole
	// hook-up. The class has no UAnimBlueprintGeneratedClass behind it, so
	// FAnimInstanceProxy takes its "custom root node" path, which is exactly
	// what UValhallaAnimInstance's proxy provides.
	BodyMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	BodyMesh->SetAnimInstanceClass(UValhallaAnimInstance::StaticClass());

	AnimInstance = Cast<UValhallaAnimInstance>(BodyMesh->GetAnimInstance());
	if (!AnimInstance)
	{
		UE_LOG(LogValhallaVisual, Warning,
			TEXT("%s: the body mesh refused UValhallaAnimInstance; the body will hold its reference pose."),
			*GetNameSafe(GetOwner()));
		return;
	}

	bStarted = true;
	AnimInstance->SetLocomotionSequences(
		Sequence(EValhallaAnim::Idle), Sequence(EValhallaAnim::Walk));
	AnimInstance->SetWalking(false);
}

void UValhallaAnimComponent::LoadSequences()
{
	if (bLoaded)
	{
		return;
	}
	bLoaded = true;

	Sequences.SetNum(AnimCount);
	for (int32 Index = 0; Index < AnimCount; ++Index)
	{
		const EValhallaAnim Anim = static_cast<EValhallaAnim>(Index);
		const FString Path = UValhallaVisuals::AnimPath(Anim);
		UAnimSequence* Loaded = LoadObject<UAnimSequence>(nullptr, *Path);
		Sequences[Index] = Loaded;

		if (!Loaded)
		{
			UE_LOG(LogValhallaVisual, Warning, TEXT("missing animation %s"), *Path);
		}
	}
}

UAnimSequence* UValhallaAnimComponent::Sequence(EValhallaAnim Anim) const
{
	const int32 Index = static_cast<int32>(Anim);
	return Sequences.IsValidIndex(Index) ? Sequences[Index].Get() : nullptr;
}

FString UValhallaAnimComponent::DescribeCurrentAnim() const
{
	return AnimName(CurrentAnim);
}

FString UValhallaAnimComponent::DescribeBlend() const
{
	if (!AnimInstance)
	{
		return FString(AnimName(CurrentAnim));
	}

	const float Walk = AnimInstance->GetWalkAlpha();
	const float Action = AnimInstance->GetActionAlpha();

	FString Locomotion = FString::Printf(TEXT("%s %.2f + %s %.2f"),
		AnimName(EValhallaAnim::Idle), 1.f - Walk,
		AnimName(EValhallaAnim::Walk), Walk);

	if (Action > KINDA_SMALL_NUMBER)
	{
		Locomotion = FString::Printf(TEXT("(%s) x%.2f | %s %.2f"),
			*Locomotion, 1.f - Action, AnimName(CurrentAnim), Action);
	}
	return Locomotion;
}

void UValhallaAnimComponent::SetLocomotion(EValhallaAnim Anim)
{
	EnsureAnimInstance();
	if (!AnimInstance)
	{
		return;
	}

	LocomotionAnim = Anim;
	AnimInstance->SetWalking(Anim == EValhallaAnim::Walk);

	// CurrentAnim names whatever the body most visibly is. While no action is
	// layered that is the locomotion state; an action overwrites it and
	// IsActionFinished hands it back.
	if (!bActionPlaying && !bHoldingCast && !bDead)
	{
		CurrentAnim = Anim;
	}
}

void UValhallaAnimComponent::PlayAction(EValhallaAnim Anim)
{
	if (bDead)
	{
		return;
	}

	EnsureAnimInstance();

	UAnimSequence* Asset = Sequence(Anim);
	if (!AnimInstance || !Asset)
	{
		return;
	}

	bHoldingCast = false;
	bActionPlaying = true;

	// Always restarts: two swings in a row are two swings, and skipping the
	// second because it matches the first would make a fast weapon look slow.
	CurrentAnim = Anim;
	AnimInstance->PlayAction(Asset, /*bLoop=*/false);

	// The deadline is kept here rather than read back off the node's own time.
	// The sequence length is a fixed property of the asset and the play rate is
	// 1, so this is exact; and the action layer has to start blending *out*
	// before the clip ends, or the last frame would pop back to locomotion.
	const UWorld* World = GetWorld();
	ActionEndTime = (World ? World->GetTimeSeconds() : 0.0)
		+ FMath::Max(0.f, Asset->GetPlayLength() - UValhallaAnimInstance::ActionBlendOutSeconds);
}

void UValhallaAnimComponent::PlaySwing()
{
	PlayAction(UValhallaVisuals::AnimForAttackCycle(AttackCycle));
}

void UValhallaAnimComponent::PlayHitReaction()
{
	if (bDead || bActionPlaying || bHoldingCast)
	{
		return;
	}
	PlayAction(EValhallaAnim::Hit);
}

void UValhallaAnimComponent::SetDead(bool bInDead)
{
	if (bDead == bInDead)
	{
		return;
	}

	bDead = bInDead;
	bActionPlaying = false;
	bHoldingCast = false;

	EnsureAnimInstance();
	if (!AnimInstance)
	{
		return;
	}

	if (bDead)
	{
		// A non-looping sequence player clamps on its last frame and holds it,
		// which is exactly the corpse we want: no ragdoll, no physics asset,
		// and nothing to fall through the floor. The action layer is left at
		// full weight rather than blended out, so the body stays down.
		CurrentAnim = EValhallaAnim::Death;
		AnimInstance->PlayAction(Sequence(EValhallaAnim::Death), /*bLoop=*/false);
	}
	else
	{
		// Respawn hands the body back to locomotion, which starts at idle.
		AnimInstance->StopAction();
		SetLocomotion(EValhallaAnim::Idle);
		CurrentAnim = EValhallaAnim::Idle;
	}
}

void UValhallaAnimComponent::TickCastHold()
{
	if (!SkillComponent)
	{
		return;
	}

	const bool bCasting = !SkillComponent->CastingSkillId.IsNone();

	if (bCasting && !bHoldingCast)
	{
		bHoldingCast = true;
		bActionPlaying = false;
		CurrentAnim = EValhallaAnim::Cast;
		EnsureAnimInstance();
		if (AnimInstance)
		{
			// Looped for the duration of the cast: the cast bar and the pose
			// end together, whatever the sequence's own length is. It is the
			// action *layer* that loops, so a caster who walks while casting
			// still has legs.
			AnimInstance->PlayAction(Sequence(EValhallaAnim::Cast), /*bLoop=*/true);
		}
	}
	else if (!bCasting && bHoldingCast)
	{
		bHoldingCast = false;
		if (AnimInstance)
		{
			AnimInstance->StopAction();
		}
	}
}

bool UValhallaAnimComponent::IsActionFinished() const
{
	// The node's own accumulated time is deliberately not consulted. It lives
	// on the proxy, is written by the worker thread, and would mean trusting a
	// second mechanism to agree with the first. The clip length is a fixed
	// property of the asset and the play rate is 1, so a deadline taken when
	// the action started is exact and has nothing to disagree with.
	const UWorld* World = GetWorld();
	return !World || World->GetTimeSeconds() >= ActionEndTime;
}

void UValhallaAnimComponent::TickLocomotion()
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	const float Speed = Owner->GetVelocity().Size2D();
	SetLocomotion(Speed > WalkSpeedThreshold ? EValhallaAnim::Walk : EValhallaAnim::Idle);
}

void UValhallaAnimComponent::TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaSeconds, TickType, ThisTickFunction);

	if (!BodyMesh || !BodyMesh->GetSkeletalMeshAsset())
	{
		return;
	}

	if (!AnimInstance)
	{
		// The mesh is assigned after the component's BeginPlay on a client, so
		// the first few ticks are where the instance actually gets created.
		EnsureAnimInstance();
		if (!AnimInstance)
		{
			return;
		}
	}

	// A corpse is a held pose and nothing else; locomotion is not ticked,
	// because a dead body's velocity is not a thing it should react to.
	if (bDead)
	{
		return;
	}

	// The action layer retires itself. Phase 4c returned here, because the
	// action *replaced* the locomotion clip; now it sits on top of it, so the
	// legs keep being updated either way — which is the whole point of the
	// layering and is what lets a character swing while walking.
	if (bActionPlaying && IsActionFinished())
	{
		bActionPlaying = false;
		AnimInstance->StopAction();
	}

	// The cast pose is held open from the replicated cast state, and it
	// outranks a finished action but not a running one: an instant skill fired
	// mid-cast has already interrupted the cast.
	if (!bActionPlaying)
	{
		TickCastHold();
	}

	TickLocomotion();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Event routing
// ─────────────────────────────────────────────────────────────────────────────

UValhallaAnimComponent* UValhallaAnimComponent::Find(AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UValhallaAnimComponent>() : nullptr;
}

void UValhallaAnimComponent::DispatchCombatEvent(const UObject* WorldContext, const FValhallaCombatEvent& Event)
{
	UValhallaAnimComponent* TargetAnim = Find(Event.Target);
	UValhallaAnimComponent* InstigatorAnim = Find(Event.Instigator);

	switch (Event.Kind)
	{
	case EValhallaCombatEventKind::SkillStarted:
		// A timed cast is held open by TickCastHold from the replicated cast
		// state; only the instant skills need a one-shot here, and Amount is
		// the cast time in ms, so zero is exactly "instant".
		if (InstigatorAnim && Event.Amount <= 0.f)
		{
			InstigatorAnim->PlayAction(EValhallaAnim::Cast);
		}
		break;

	case EValhallaCombatEventKind::PlayerHit:
	case EValhallaCombatEventKind::NpcHit:
	case EValhallaCombatEventKind::Missed:
	case EValhallaCombatEventKind::Dodged:
	case EValhallaCombatEventKind::Blocked:
		if (InstigatorAnim && IsSwingSource(WorldContext, Event.SkillId))
		{
			InstigatorAnim->PlaySwing();
		}
		// A heal is not a blow, and neither is a tick that did nothing.
		if (TargetAnim && IsSwingOutcome(Event.Kind) && !Event.bHeal && Event.Amount > 0.f)
		{
			TargetAnim->PlayHitReaction();
		}
		break;

	case EValhallaCombatEventKind::PlayerDied:
	case EValhallaCombatEventKind::NpcDied:
		if (TargetAnim)
		{
			TargetAnim->SetDead(true);
		}
		break;

	case EValhallaCombatEventKind::PlayerRespawned:
		if (TargetAnim)
		{
			TargetAnim->SetDead(false);
		}
		break;

	default:
		break;
	}
}
