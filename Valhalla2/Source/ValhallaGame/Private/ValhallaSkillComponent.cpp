// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaSkillComponent.h"

#include "Components/CapsuleComponent.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "ValhallaCharacter.h"
#include "ValhallaCombatLibrary.h"
#include "ValhallaConstants.h"
#include "ValhallaDataSubsystem.h"
#include "ValhallaGame.h"
#include "ValhallaGameMode.h"
#include "ValhallaGameState.h"
#include "ValhallaNPC.h"
#include "ValhallaPlayerController.h"
#include "ValhallaPlayerState.h"
#include "ValhallaSkillHandler.h"
#include "ValhallaStats.h"

namespace
{
	/** skills.json ids the component itself has to name. */
	const FName SkillMeleeAttack(TEXT("melee_attack"));
	const FName SkillRangedAttack(TEXT("ranged_attack"));

	/** SkillSystem.ts:941 — a DoT or HoT ticks once a second, not once a frame. */
	constexpr double BuffTickIntervalSeconds = 1.0;
}

UValhallaSkillComponent::UValhallaSkillComponent()
{
	// Everything here is driven from AValhallaGameState's fixed step, so the
	// component itself never ticks. A per-frame tick would make the auto-attack
	// interval and the DoT cadence depend on the host's frame rate, which is the
	// exact bug the fixed step exists to prevent.
	PrimaryComponentTick.bCanEverTick = false;

	SetIsReplicatedByDefault(true);

	ActionBar.SetNum(ValhallaActionBarSlots);
}

void UValhallaSkillComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UValhallaSkillComponent, CastingSkillId);
	DOREPLIFETIME(UValhallaSkillComponent, CastStartedAt);
	DOREPLIFETIME(UValhallaSkillComponent, CastDurationMs);

	DOREPLIFETIME(UValhallaSkillComponent, bAutoAttacking);
	DOREPLIFETIME(UValhallaSkillComponent, AutoAttackTargetActor);
	DOREPLIFETIME(UValhallaSkillComponent, AutoAttackSkillId);

	// The action bar only matters to the player it belongs to; sending everyone
	// else's loadout to every client would be pure waste.
	DOREPLIFETIME_CONDITION(UValhallaSkillComponent, ActionBar, COND_OwnerOnly);
}

void UValhallaSkillComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		InitializeActionBarFromClass();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Owner accessors
// ─────────────────────────────────────────────────────────────────────────────

AValhallaCharacter* UValhallaSkillComponent::GetValhallaOwner() const
{
	return Cast<AValhallaCharacter>(GetOwner());
}

AValhallaPlayerState* UValhallaSkillComponent::GetValhallaPlayerState() const
{
	const AValhallaCharacter* Character = GetValhallaOwner();
	return Character ? Character->GetValhallaPlayerState() : nullptr;
}

UValhallaDataSubsystem* UValhallaSkillComponent::GetData() const
{
	const AActor* Owner = GetOwner();
	UGameInstance* GameInstance = Owner ? Owner->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
}

UValhallaSkillHandlerRegistry* UValhallaSkillComponent::GetRegistry() const
{
	const AActor* Owner = GetOwner();
	UGameInstance* GameInstance = Owner ? Owner->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UValhallaSkillHandlerRegistry>() : nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Action bar
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaSkillComponent::InitializeActionBarFromClass()
{
	const AValhallaPlayerState* PlayerState = GetValhallaPlayerState();
	const UValhallaDataSubsystem* Data = GetData();
	if (!PlayerState || !Data)
	{
		return;
	}

	// Kevin, 2026-09-24: a new character starts with an empty bar and fills it
	// from the skills pane. A returning character's saved bar goes on after
	// this (AValhallaGameMode::SpawnLoadedPawn -> ApplySavedActionBar).
	ActionBar.Reset();
	ActionBar.SetNum(ValhallaActionBarSlots);

	UE_LOG(LogValhallaGame, Log, TEXT("%s action bar: empty"), *PlayerState->CharacterName);
}

bool UValhallaSkillComponent::CanPlaceOnActionBar(FName SkillId) const
{
	const AValhallaPlayerState* PlayerState = GetValhallaPlayerState();
	const UValhallaDataSubsystem* Data = GetData();
	if (!PlayerState || !Data || SkillId.IsNone())
	{
		return false;
	}
	return CanPlaceOnActionBar(Data->FindSkill(SkillId), Data->GetClassSkills(PlayerState->ClassId), PlayerState->Level);
}

bool UValhallaSkillComponent::CanPlaceOnActionBar(const FValhallaSkillTemplate* Skill, const TArray<FName>& ClassSkills, int32 Level)
{
	if (!Skill || Skill->Id.IsNone())
	{
		return false;
	}
	// GameRoom.ts:277 — a class skill or a cross-class skill (classId null,
	// like melee_attack), and nothing else. And only once it is unlocked.
	const bool bOwnSkill = Skill->ClassId.IsNone() || ClassSkills.Contains(Skill->Id);
	return bOwnSkill && Level >= Skill->LevelRequired;
}

TArray<FName> UValhallaSkillComponent::BuildActionBarFromSave(const TArray<FString>& Saved, TFunctionRef<bool(FName)> CanPlace)
{
	TArray<FName> Bar;
	Bar.SetNum(ValhallaActionBarSlots);
	for (int32 Index = 0; Index < Saved.Num() && Index < ValhallaActionBarSlots; ++Index)
	{
		if (Saved[Index].IsEmpty())
		{
			continue;
		}
		const FName SkillId(*Saved[Index]);
		if (CanPlace(SkillId))
		{
			Bar[Index] = SkillId;
		}
	}
	return Bar;
}

void UValhallaSkillComponent::ApplySavedActionBar(const TArray<FString>& Saved)
{
	const AValhallaPlayerState* PlayerState = GetValhallaPlayerState();
	ActionBar = BuildActionBarFromSave(Saved, [this](FName SkillId) { return CanPlaceOnActionBar(SkillId); });
	for (int32 Index = 0; Index < Saved.Num() && Index < ValhallaActionBarSlots; ++Index)
	{
		if (!Saved[Index].IsEmpty() && ActionBar[Index].IsNone())
		{
			UE_LOG(LogValhallaGame, Warning, TEXT("%s's saved action bar slot %d holds '%s', which it cannot use; left empty."),
				PlayerState ? *PlayerState->CharacterName : TEXT("?"), Index + 1, *Saved[Index]);
		}
	}
	UE_LOG(LogValhallaGame, Log, TEXT("%s action bar (saved): %s"),
		PlayerState ? *PlayerState->CharacterName : TEXT("?"),
		*FString::JoinBy(ActionBar, TEXT(", "), [](const FName& Id) { return Id.IsNone() ? TEXT("-") : Id.ToString(); }));
}

TArray<FString> UValhallaSkillComponent::GetActionBarForSave() const
{
	TArray<FString> Out;
	Out.SetNum(ValhallaActionBarSlots);
	for (int32 Index = 0; Index < ActionBar.Num() && Index < ValhallaActionBarSlots; ++Index)
	{
		Out[Index] = ActionBar[Index].IsNone() ? FString() : ActionBar[Index].ToString();
	}
	return Out;
}

FName UValhallaSkillComponent::GetSlotSkillId(int32 Slot) const
{
	const int32 Index = Slot - 1;
	return ActionBar.IsValidIndex(Index) ? ActionBar[Index] : NAME_None;
}

void UValhallaSkillComponent::ServerSetActionBar_Implementation(int32 Slot, FName SkillId)
{
	const int32 Index = Slot - 1;
	if (!ActionBar.IsValidIndex(Index))
	{
		return;
	}

	const AValhallaPlayerState* PlayerState = GetValhallaPlayerState();
	const UValhallaDataSubsystem* Data = GetData();
	if (!PlayerState || !Data)
	{
		return;
	}

	// Clearing a slot is always allowed.
	if (SkillId.IsNone())
	{
		ActionBar[Index] = NAME_None;
		return;
	}

	// A skill the character may not use (another class's, or not unlocked yet)
	// is refused and the slot keeps what it had.
	if (CanPlaceOnActionBar(SkillId))
	{
		ActionBar[Index] = SkillId;
	}
	else
	{
		UE_LOG(LogValhallaGame, Warning, TEXT("%s asked for '%s' in slot %d; not an unlocked skill of class '%s' (level %d)."),
			*PlayerState->CharacterName, *SkillId.ToString(), Slot, *PlayerState->ClassId.ToString(), PlayerState->Level);
	}
}

void UValhallaSkillComponent::CastFromActionBar(int32 Slot, AActor* Target, const FVector& AimPoint)
{
	const FName SkillId = GetSlotSkillId(Slot);
	if (SkillId.IsNone())
	{
		return;
	}

	const UValhallaDataSubsystem* Data = GetData();
	const FValhallaSkillTemplate* Skill = Data ? Data->FindSkill(SkillId) : nullptr;
	if (!Skill)
	{
		return;
	}

	// GameScene.ts:731 — an auto-attack skill on the bar toggles the loop rather
	// than casting. Each auto-attack button is its own kind: pressing Auto Melee
	// while it runs stops it, and pressing Auto Ranged while melee runs switches
	// to ranged on the same target instead of stopping.
	if (Skill->bIsAutoAttack)
	{
		// Same button on the same target (or no new target) stops; the same
		// button with a different enemy selected switches to it.
		if (bAutoAttacking && AutoAttackSkillId == SkillId && (!Target || Target == AutoAttackTargetActor))
		{
			ServerStopAutoAttack();
		}
		else
		{
			AActor* AttackTarget = Target ? Target : AutoAttackTargetActor.Get();
			ServerStartAutoAttackWith(AttackTarget, SkillId);
		}
		return;
	}

	ServerCastSkill(SkillId, Target, AimPoint);
}

float UValhallaSkillComponent::GetSlotCooldownRemaining(int32 Slot) const
{
	const FName SkillId = GetSlotSkillId(Slot);
	const AValhallaPlayerState* PlayerState = GetValhallaPlayerState();
	const UValhallaDataSubsystem* Data = GetData();

	if (SkillId.IsNone() || !PlayerState || !Data)
	{
		return 0.f;
	}

	const FValhallaSkillTemplate* Skill = Data->FindSkill(SkillId);
	if (!Skill)
	{
		return 0.f;
	}

	const double Now = UValhallaCombatLibrary::GetServerTime(this);
	FName Blocking;
	return static_cast<float>(UValhallaCombatLibrary::GetCooldownRemaining(
		PlayerState->GetSkillCooldownExpiry(), *Skill, Data->GetClassSkills(PlayerState->ClassId),
		[Data](FName Id) { return Data->FindSkill(Id); }, Now, Blocking));
}

float UValhallaSkillComponent::GetCastProgress() const
{
	if (CastingSkillId.IsNone() || CastDurationMs <= 0.f)
	{
		return 0.f;
	}

	const double Now = UValhallaCombatLibrary::GetServerTime(this);
	return FMath::Clamp(static_cast<float>((Now - CastStartedAt) / (CastDurationMs / 1000.0)), 0.f, 1.f);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Validation — SkillSystem.ts:684
// ─────────────────────────────────────────────────────────────────────────────

bool UValhallaSkillComponent::RequiresFacing(const FValhallaSkillTemplate& Skill, const AActor* Target) const
{
	// Only the kinds aimed at one other actor. A singleAlly heal on yourself
	// needs no facing; self / aoeSelf / aoeGround are not aimed at an actor;
	// a cone already hits only what is in front (its arc is off the yaw).
	switch (Skill.TargetType)
	{
	case EValhallaSkillTargetType::SingleEnemy:
		return Target != nullptr;
	case EValhallaSkillTargetType::SingleAlly:
		return Target != nullptr && Target != GetOwner();
	default:
		return false;
	}
}

FString UValhallaSkillComponent::ValidateCast(const FValhallaSkillTemplate& Skill, AActor* Target, const FVector& TargetLocation, double Now,
	FName* OutReasonCode) const
{
	const AValhallaCharacter* Character = GetValhallaOwner();
	const AValhallaPlayerState* PlayerState = GetValhallaPlayerState();
	const UValhallaDataSubsystem* Data = GetData();

	if (!Character || !PlayerState || !Data)
	{
		return TEXT("Not ready");
	}

	// ── Alive ────────────────────────────────────────────────────────────
	if (!PlayerState->IsAlive())
	{
		return TEXT("You are dead");
	}

	// ── Class ────────────────────────────────────────────────────────────
	// A null classId in skills.json means every class may use it (melee_attack).
	if (!Skill.ClassId.IsNone())
	{
		const TArray<FName> ClassSkills = Data->GetClassSkills(PlayerState->ClassId);
		if (!ClassSkills.Contains(Skill.Id))
		{
			return TEXT("Your class cannot use this skill");
		}
	}

	// ── Level ────────────────────────────────────────────────────────────
	if (PlayerState->Level < Skill.LevelRequired)
	{
		return FString::Printf(TEXT("Requires level %d"), Skill.LevelRequired);
	}

	// ── Already casting ──────────────────────────────────────────────────
	if (!CastingSkillId.IsNone())
	{
		return TEXT("Already casting");
	}

	// ── Cooldown, including the group ────────────────────────────────────
	{
		FName Blocking;
		const double Remaining = UValhallaCombatLibrary::GetCooldownRemaining(
			PlayerState->GetSkillCooldownExpiry(), Skill, Data->GetClassSkills(PlayerState->ClassId),
			[Data](FName Id) { return Data->FindSkill(Id); }, Now, Blocking);

		if (Remaining > 0.0)
		{
			if (Blocking != Skill.Id)
			{
				const FValhallaSkillTemplate* Other = Data->FindSkill(Blocking);
				return FString::Printf(TEXT("On cooldown (%ds) — shared with %s"),
					FMath::CeilToInt(Remaining), Other ? *Other->Name : *Blocking.ToString());
			}
			return FString::Printf(TEXT("On cooldown (%ds)"), FMath::CeilToInt(Remaining));
		}
	}

	// ── Resource ─────────────────────────────────────────────────────────
	if (Skill.ResourceType == EValhallaResourceType::Mana && PlayerState->Mana < Skill.ResourceCost)
	{
		return TEXT("Not enough mana");
	}
	if (Skill.ResourceType == EValhallaResourceType::Energy && PlayerState->Energy < Skill.ResourceCost)
	{
		return TEXT("Not enough energy");
	}

	const FVector CasterLocation = Character->GetActorLocation();

	// ── Ground-targeted range ────────────────────────────────────────────
	if (Skill.TargetType == EValhallaSkillTargetType::AoeGround)
	{
		if (Skill.Range > 0.f)
		{
			// 1.0 ranges are pixels; a 1.0 pixel is a 2.0 centimetre, so the
			// number carries over with no conversion. See ApplyClassAppearance.
			FVector ToPoint = TargetLocation - CasterLocation;
			ToPoint.Z = 0.0;
			if (ToPoint.SizeSquared2D() > static_cast<double>(Skill.Range) * Skill.Range)
			{
				return TEXT("Out of range");
			}
		}
	}

	// ── Single-target rules ──────────────────────────────────────────────
	if (Skill.TargetType == EValhallaSkillTargetType::SingleEnemy || Skill.TargetType == EValhallaSkillTargetType::SingleAlly)
	{
		if (!Target)
		{
			return TEXT("No target selected");
		}
		if (!UValhallaCombatLibrary::IsAliveTarget(Target))
		{
			return TEXT("Target is dead");
		}

		// devlog_changes.txt 2026-02-20 — the target *class* is enforced, not just
		// its existence. Minor Heal cast at an enemy used to silently heal the
		// caster; now it is refused here, on the server, where it cannot be
		// bypassed by a client that skips the matching check in GameScene.
		const bool bTargetIsNpc = UValhallaCombatLibrary::IsNpcTarget(Target);
		if (Skill.TargetType == EValhallaSkillTargetType::SingleAlly && bTargetIsNpc)
		{
			return TEXT("Invalid target");
		}
		// B-06: "an NPC" is not enough for an enemy skill — a friendly NPC (a
		// vendor, a guard) is an NPC nobody may attack. AreHostile is the same
		// test the auto-attack and every AoE already use.
		if (Skill.TargetType == EValhallaSkillTargetType::SingleEnemy && !UValhallaCombatLibrary::AreHostile(Character, Target))
		{
			return TEXT("Invalid target");
		}
	}

	// ── Range to a selected target ───────────────────────────────────────
	if (Target && Skill.Range > 0.f && Skill.TargetType != EValhallaSkillTargetType::AoeGround)
	{
		FVector ToTarget = Target->GetActorLocation() - CasterLocation;
		ToTarget.Z = 0.0;
		if (ToTarget.SizeSquared2D() > static_cast<double>(Skill.Range) * Skill.Range)
		{
			return TEXT("Out of range");
		}
	}

	// ── Facing (controls rework) ─────────────────────────────────────────
	// Last, so "Out of range" and the rest win: they are the more useful news.
	// Refused before anything is spent — no cooldown, no resource, no cast bar
	// — and the server does not turn the caster to make it pass.
	if (RequiresFacing(Skill, Target) && !UValhallaCombatLibrary::IsFacing(Character, Target))
	{
		if (OutReasonCode)
		{
			*OutReasonCode = UValhallaCombatLibrary::NotFacingReason();
		}
		return UValhallaCombatLibrary::NotFacingText();
	}

	return FString();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Casting — SkillSystem.ts:176 / 832 / 863
// ─────────────────────────────────────────────────────────────────────────────

FValhallaSkillContext UValhallaSkillComponent::MakeContext(const FValhallaSkillTemplate& Skill, AActor* Target, const FVector& TargetLocation, double Now) const
{
	FValhallaSkillContext Context;
	Context.Caster = GetOwner();
	Context.Target = Target;
	Context.AimPoint = TargetLocation;
	Context.ServerTime = Now;
	Context.Skill = Skill;
	Context.World = GetWorld();
	return Context;
}

void UValhallaSkillComponent::SendSkillFailed(const FString& Reason, FName ReasonCode, FName SkillId) const
{
	const AValhallaCharacter* Character = GetValhallaOwner();
	AValhallaPlayerController* Controller = Character ? Cast<AValhallaPlayerController>(Character->GetController()) : nullptr;
	if (!Controller)
	{
		return;
	}

	FValhallaCombatEvent Event;
	Event.Kind = EValhallaCombatEventKind::SkillFailed;
	Event.Target = GetOwner();
	Event.Instigator = GetOwner();
	Event.SkillId = SkillId;
	Event.Text = Reason;
	Event.Reason = ReasonCode;
	Event.Location = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;

	if (!ReasonCode.IsNone())
	{
		UE_LOG(LogValhallaGame, Log, TEXT("%s: skillFailed [%s] %s%s"),
			*UValhallaCombatLibrary::GetDisplayName(GetOwner()), *ReasonCode.ToString(), *Reason,
			SkillId.IsNone() ? TEXT("") : *FString::Printf(TEXT(" ('%s')"), *SkillId.ToString()));
	}

	// castFailed is the one event 1.0 sent only to the caster
	// (GameRoom.ts:992). Broadcasting it would tell every client in the zone
	// exactly which spells someone has on cooldown.
	Controller->ClientOnCombatEvent(Event);
}

void UValhallaSkillComponent::ServerCastSkill_Implementation(FName SkillId, AActor* Target, FVector TargetLocation)
{
	// EQ: casting stands you up.
	if (AValhallaCharacter* Sitter = GetValhallaOwner())
	{
		Sitter->SetSitting(false);
	}

	const UValhallaDataSubsystem* Data = GetData();
	const FValhallaSkillTemplate* Skill = Data ? Data->FindSkill(SkillId) : nullptr;
	if (!Skill)
	{
		SendSkillFailed(TEXT("Unknown skill"));
		return;
	}

	const double Now = UValhallaCombatLibrary::GetServerTime(this);

	FName FailCode;
	const FString FailReason = ValidateCast(*Skill, Target, TargetLocation, Now, &FailCode);
	if (!FailReason.IsEmpty())
	{
		UE_LOG(LogValhallaGame, Verbose, TEXT("%s: cast of '%s' refused — %s"),
			*UValhallaCombatLibrary::GetDisplayName(GetOwner()), *SkillId.ToString(), *FailReason);
		SendSkillFailed(FailReason, FailCode, SkillId);
		return;
	}

	// SkillSystem.ts:215 — a zero cast time is not a very short cast, it is a
	// different code path with no cast bar and nothing to interrupt.
	if (Skill->CastTimeMs <= 0.f)
	{
		FValhallaCombatEvent Started;
		Started.Kind = EValhallaCombatEventKind::SkillStarted;
		Started.Target = GetOwner();
		Started.Instigator = GetOwner();
		Started.SkillId = SkillId;
		Started.Amount = 0.f;
		UValhallaCombatLibrary::BroadcastCombatEvent(this, Started);

		CompleteCast(*Skill, Target, TargetLocation, Now);
		return;
	}

	StartTimedCast(*Skill, Target, TargetLocation, Now);
}

void UValhallaSkillComponent::StartTimedCast(const FValhallaSkillTemplate& Skill, AActor* Target, const FVector& TargetLocation, double Now)
{
	CastingSkillId = Skill.Id;
	CastStartedAt = Now;
	CastDurationMs = Skill.CastTimeMs;

	PendingCastTarget = Target;
	PendingCastLocation = TargetLocation;
	MovementDuringCastSeconds = 0.f;

	FValhallaCombatEvent Started;
	Started.Kind = EValhallaCombatEventKind::SkillStarted;
	Started.Target = GetOwner();
	Started.Instigator = GetOwner();
	Started.SkillId = Skill.Id;
	Started.Amount = Skill.CastTimeMs;
	UValhallaCombatLibrary::BroadcastCombatEvent(this, Started);

	UE_LOG(LogValhallaGame, Log, TEXT("%s began casting '%s' (%.0f ms)."),
		*UValhallaCombatLibrary::GetDisplayName(GetOwner()), *Skill.Id.ToString(), Skill.CastTimeMs);
}

void UValhallaSkillComponent::ClearCastingState()
{
	CastingSkillId = NAME_None;
	CastStartedAt = 0.0;
	CastDurationMs = 0.f;
	PendingCastTarget = nullptr;
	PendingCastLocation = FVector::ZeroVector;
	MovementDuringCastSeconds = 0.f;
}

void UValhallaSkillComponent::ServerCancelCast_Implementation()
{
	if (CastingSkillId.IsNone())
	{
		return;
	}

	FValhallaCombatEvent Event;
	Event.Kind = EValhallaCombatEventKind::SkillInterrupted;
	Event.Target = GetOwner();
	Event.Instigator = GetOwner();
	Event.SkillId = CastingSkillId;
	UValhallaCombatLibrary::BroadcastCombatEvent(this, Event);

	UE_LOG(LogValhallaGame, Log, TEXT("%s cancelled '%s'."),
		*UValhallaCombatLibrary::GetDisplayName(GetOwner()), *CastingSkillId.ToString());

	// No refund and no cooldown: a cancelled cast never reached CompleteCast, so
	// it never spent anything. That is 1.0's behaviour and it is deliberate —
	// interrupting is a cost in time, not in mana.
	ClearCastingState();
}

void UValhallaSkillComponent::DeductResource(const FValhallaSkillTemplate& Skill)
{
	AValhallaPlayerState* PlayerState = GetValhallaPlayerState();
	if (!PlayerState)
	{
		return;
	}

	if (Skill.ResourceType == EValhallaResourceType::Mana)
	{
		PlayerState->Mana = FMath::Max(0.f, PlayerState->Mana - Skill.ResourceCost);
	}
	else if (Skill.ResourceType == EValhallaResourceType::Energy)
	{
		PlayerState->Energy = FMath::Max(0.f, PlayerState->Energy - Skill.ResourceCost);
	}
}

void UValhallaSkillComponent::CompleteCast(const FValhallaSkillTemplate& Skill, AActor* Target, const FVector& TargetLocation, double Now)
{
	AValhallaPlayerState* PlayerState = GetValhallaPlayerState();
	const UValhallaDataSubsystem* Data = GetData();
	UValhallaSkillHandlerRegistry* Registry = GetRegistry();

	if (!PlayerState || !Data || !Registry)
	{
		return;
	}

	// SkillSystem.ts:872 — resource then cooldown then effect, in that order. A
	// handler that throws must still have cost the caster their mana.
	DeductResource(Skill);

	UValhallaCombatLibrary::ApplyCooldown(
		PlayerState->GetSkillCooldownExpiry(), Skill, Data->GetClassSkills(PlayerState->ClassId),
		[Data](FName Id) { return Data->FindSkill(Id); }, Now);

	Registry->Execute(MakeContext(Skill, Target, TargetLocation, Now));

	FValhallaCombatEvent Event;
	Event.Kind = EValhallaCombatEventKind::SkillEffect;
	Event.Target = Target ? Target : GetOwner();
	Event.Instigator = GetOwner();
	Event.SkillId = Skill.Id;
	Event.Location = Target ? Target->GetActorLocation() : TargetLocation;
	UValhallaCombatLibrary::BroadcastCombatEvent(this, Event);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Auto-attack — SkillSystem.ts:352 / 434
// ─────────────────────────────────────────────────────────────────────────────

FName UValhallaSkillComponent::ResolveAutoAttackSkillId() const
{
	const AValhallaPlayerState* PlayerState = GetValhallaPlayerState();
	if (!PlayerState)
	{
		return SkillMeleeAttack;
	}

	// stats.ts:216 — only the ranger has a ranged basic attack.
	if (!Valhalla::Stats::HasRangedAttack(PlayerState->ClassId))
	{
		return SkillMeleeAttack;
	}

	// SkillSystem.ts:378 — and only with a bow in hand. `ranged_attack` carries
	// `requiresWeapon: true` (skills.ts:182), and the weapon has to be one whose
	// template says `isRangedWeapon`. A ranger who has dropped their bow swings
	// it like everyone else rather than firing arrows from an empty hand.
	//
	// The skill's own flag is honoured rather than hard-coding the rule, so a
	// designer who marks a second skill `requiresWeapon` gets the same gate.
	const UValhallaDataSubsystem* Data = GetData();
	const FValhallaSkillTemplate* RangedSkill = Data ? Data->FindSkill(SkillRangedAttack) : nullptr;

	if (RangedSkill && RangedSkill->bRequiresWeapon)
	{
		const FValhallaItemTemplate* Weapon = PlayerState->GetEquippedWeapon();
		if (!Weapon || !Weapon->bIsRangedWeapon)
		{
			return SkillMeleeAttack;
		}
	}

	return SkillRangedAttack;
}

float UValhallaSkillComponent::GetAutoAttackIntervalMs() const
{
	const AValhallaPlayerState* PlayerState = GetValhallaPlayerState();
	const UValhallaDataSubsystem* Data = GetData();
	if (!PlayerState || !Data)
	{
		return FallbackMeleeAttackSpeedMs;
	}

	const FValhallaClassTemplate* ClassTemplate = Data->FindClass(PlayerState->ClassId);
	// The running loop's own skill when there is one — a ranger who chose Auto
	// Melee swings at melee speed even with a bow in hand.
	const FName ActiveSkill = (bAutoAttacking && !AutoAttackSkillId.IsNone()) ? AutoAttackSkillId : ResolveAutoAttackSkillId();
	const bool bRanged = ActiveSkill == SkillRangedAttack;

	// SkillSystem.ts:499 — `weapon?.attackSpeedMs ?? classTemplate?.baseXSpeedMs
	// ?? <hard fallback>`, read right to left.
	float BaseMs = bRanged ? FallbackRangedAttackSpeedMs : FallbackMeleeAttackSpeedMs;

	if (ClassTemplate)
	{
		const float FromClass = bRanged ? ClassTemplate->BaseRangedAttackSpeedMs : ClassTemplate->BaseMeleeAttackSpeedMs;
		if (FromClass > 0.f)
		{
			BaseMs = FromClass;
		}
	}

	// The weapon wins when it names a speed, and it names one for every weapon in
	// items.json. `bHasAttackSpeed` is the 2.0 spelling of the difference between
	// `attackSpeedMs: 0` and the field being absent, which `??` gets for free in
	// TypeScript and a float does not.
	//
	// This is what makes weapons matter to the *feel* of a class rather than only
	// to its stat sheet: a wizard's class base is 4800 ms and an Oak Staff is
	// 2200, so picking the staff up more than doubles their swing rate.
	if (const FValhallaItemTemplate* Weapon = PlayerState->GetEquippedWeapon())
	{
		if (Weapon->bHasAttackSpeed && Weapon->AttackSpeedMs > 0.f)
		{
			BaseMs = Weapon->AttackSpeedMs;
		}
	}

	// stats.ts:184 — dexterity buys up to 40% off, and no more.
	return static_cast<float>(Valhalla::Stats::ComputeAutoAttackSpeed(BaseMs, PlayerState->GetStats().Dexterity));
}

void UValhallaSkillComponent::ServerStartAutoAttack_Implementation(AActor* Target)
{
	ServerStartAutoAttackWith_Implementation(Target, ResolveAutoAttackSkillId());
}

FString UValhallaSkillComponent::GetAutoAttackBlocker(FName SkillId) const
{
	const AValhallaPlayerState* PlayerState = GetValhallaPlayerState();
	const UValhallaDataSubsystem* Data = GetData();
	if (!PlayerState || !Data)
	{
		return TEXT("Not ready");
	}

	const FValhallaSkillTemplate* Skill = Data->FindSkill(SkillId);
	if (!Skill || !Skill->bIsAutoAttack)
	{
		return TEXT("Not an auto-attack");
	}

	// A null classId means every class (melee_attack); otherwise it must match.
	if (!Skill->ClassId.IsNone() && Skill->ClassId != PlayerState->ClassId)
	{
		return TEXT("Your class cannot use that attack");
	}

	if (SkillId == SkillRangedAttack && !Valhalla::Stats::HasRangedAttack(PlayerState->ClassId))
	{
		return TEXT("Your class has no ranged attack");
	}

	if (Skill->bRequiresWeapon)
	{
		const FValhallaItemTemplate* Weapon = PlayerState->GetEquippedWeapon();
		if (!Weapon)
		{
			return TEXT("You need a weapon equipped");
		}
		if (SkillId == SkillRangedAttack && !Weapon->bIsRangedWeapon)
		{
			return TEXT("You need a ranged weapon equipped");
		}
	}

	return FString();
}

void UValhallaSkillComponent::ServerStartAutoAttackWith_Implementation(AActor* Target, FName SkillId)
{
	const AValhallaPlayerState* PlayerState = GetValhallaPlayerState();
	if (!PlayerState || !PlayerState->IsAlive())
	{
		SendSkillFailed(TEXT("You are dead"));
		return;
	}

	if (!UValhallaCombatLibrary::IsAliveTarget(Target) || !UValhallaCombatLibrary::AreHostile(GetOwner(), Target))
	{
		SendSkillFailed(TEXT("Invalid target"));
		return;
	}

	const FString Blocker = GetAutoAttackBlocker(SkillId);
	if (!Blocker.IsEmpty())
	{
		SendSkillFailed(Blocker);
		return;
	}

	StartAutoAttackInternal(Target, SkillId);
}

void UValhallaSkillComponent::StartAutoAttackInternal(AActor* Target, FName SkillId)
{
	if (AValhallaCharacter* Sitter = GetValhallaOwner())
	{
		Sitter->SetSitting(false);
	}

	// SkillSystem.ts:396 — starting an auto-attack drops any cast in progress.
	if (!CastingSkillId.IsNone())
	{
		ServerCancelCast();
	}

	bAutoAttacking = true;
	AutoAttackTargetActor = Target;
	AutoAttackSkillId = SkillId;

	// SkillSystem.ts:405 — the first swing lands immediately, so turning the
	// attack on feels like attacking rather than like waiting.
	NextAutoAttackAt = UValhallaCombatLibrary::GetServerTime(this);

	FValhallaCombatEvent Event;
	Event.Kind = EValhallaCombatEventKind::AutoAttackStarted;
	Event.Target = Target;
	Event.Instigator = GetOwner();
	Event.SkillId = AutoAttackSkillId;
	UValhallaCombatLibrary::BroadcastCombatEvent(this, Event);

	UE_LOG(LogValhallaGame, Log, TEXT("%s started auto-attacking %s with '%s' every %.0f ms."),
		*UValhallaCombatLibrary::GetDisplayName(GetOwner()),
		*UValhallaCombatLibrary::GetDisplayName(Target),
		*AutoAttackSkillId.ToString(), GetAutoAttackIntervalMs());
}

void UValhallaSkillComponent::ServerStopAutoAttack_Implementation()
{
	if (!bAutoAttacking)
	{
		return;
	}

	bAutoAttacking = false;
	AutoAttackTargetActor = nullptr;
	AutoAttackSkillId = NAME_None;
	NextAutoAttackAt = 0.0;

	FValhallaCombatEvent Event;
	Event.Kind = EValhallaCombatEventKind::AutoAttackStopped;
	Event.Instigator = GetOwner();
	Event.Target = GetOwner();
	UValhallaCombatLibrary::BroadcastCombatEvent(this, Event);

	UE_LOG(LogValhallaGame, Log, TEXT("%s stopped auto-attacking."), *UValhallaCombatLibrary::GetDisplayName(GetOwner()));
}

void UValhallaSkillComponent::TickAutoAttack(double Now)
{
	if (!bAutoAttacking)
	{
		return;
	}

	AValhallaCharacter* Character = GetValhallaOwner();
	const AValhallaPlayerState* PlayerState = GetValhallaPlayerState();
	const UValhallaDataSubsystem* Data = GetData();

	if (!Character || !PlayerState || !Data)
	{
		return;
	}

	// SkillSystem.ts:446 — a dead attacker stops; so does one whose target died
	// or vanished. These are stops, not pauses.
	if (!PlayerState->IsAlive() || !UValhallaCombatLibrary::IsAliveTarget(AutoAttackTargetActor))
	{
		ServerStopAutoAttack();
		return;
	}

	const FValhallaSkillTemplate* Skill = Data->FindSkill(AutoAttackSkillId);
	if (!Skill)
	{
		ServerStopAutoAttack();
		return;
	}

	AActor* Target = AutoAttackTargetActor;
	FVector ToTarget = Target->GetActorLocation() - Character->GetActorLocation();
	ToTarget.Z = 0.0;
	const double Distance = ToTarget.Size2D();

	// Every class melees with its weapon, EverQuest-style (Kevin, 2026-09-22):
	// casters' magic comes from their spells, not from the auto-attack. 1.0's
	// magical basic for wizard / cleric / shaman (stats.ts:208) is gone.
	const bool bRangedPhysical = AutoAttackSkillId == SkillRangedAttack;

	// Measured surface to surface, the way AValhallaNPC measures its own reach:
	// 1.0's entities were points, 2.0's are capsules that cannot overlap. With
	// only one radius added here, an NPC standing at *its* attack distance
	// (30 cm + both radii) was always just outside the player's melee reach,
	// and Auto Melee never landed on anything that stood still.
	auto CapsuleRadiusOf = [](const AActor* Actor) -> double
	{
		const UCapsuleComponent* Capsule = Actor ? Actor->FindComponentByClass<UCapsuleComponent>() : nullptr;
		return Capsule ? Capsule->GetScaledCapsuleRadius() : Valhalla::PlayerCollisionRadius;
	};
	const double CapsuleGap = CapsuleRadiusOf(Character) + CapsuleRadiusOf(Target);

	double MaxRange = Valhalla::MeleeRange + CapsuleGap;
	if (bRangedPhysical)
	{
		MaxRange = Skill->Range + CapsuleGap;
	}

	// SkillSystem.ts:475 — out of range is a pause, not a stop. The player may
	// well be walking back in, and cancelling their attack for them is rude.
	if (Distance > MaxRange)
	{
		return;
	}

	// SkillSystem.ts:482 — auto-attacks pause during a cast, and the timer is
	// reset so the swing lands the instant the cast finishes rather than being
	// owed several swings at once.
	if (!CastingSkillId.IsNone())
	{
		NextAutoAttackAt = Now;
		return;
	}

	if (Now < NextAutoAttackAt)
	{
		return;
	}

	// Facing (controls rework). 1.0 auto-faced the target here
	// (SkillSystem.ts:510); 2.0 never turns a player from the server. A swing
	// at something the player is not facing is skipped — the loop stays on,
	// and the timer is not advanced, so the swing lands the moment they turn
	// to face it — and the player is told, at most once per 2 s.
	if (!UValhallaCombatLibrary::IsFacing(Character, Target))
	{
		if (Now - LastNotFacingMessageAt >= Valhalla::NotFacingMessageIntervalSeconds)
		{
			LastNotFacingMessageAt = Now;
			SendSkillFailed(UValhallaCombatLibrary::NotFacingText(), UValhallaCombatLibrary::NotFacingReason(), AutoAttackSkillId);
		}
		return;
	}
	LastNotFacingMessageAt = -1.0e9;

	const FValhallaResolvedStats& Stats = PlayerState->GetStats();

	// The weapon's damage roll (2.0): a uniform roll in the weapon's
	// [minDamage, maxDamage] — or its flat 1.0 `attackDamage` when it has no
	// range, or 1-3 with no weapon at all — added to the base constant
	// *before* the stat scaling, where SkillSystem.ts:513 added attackDamage.
	double WeaponMin = Valhalla::UnarmedMinDamage;
	double WeaponMax = Valhalla::UnarmedMaxDamage;
	if (const FValhallaItemTemplate* Weapon = PlayerState->GetEquippedWeapon())
	{
		float Min = 0.f, Max = 0.f;
		Weapon->GetDamageRange(Min, Max);
		WeaponMin = Min;
		WeaponMax = Max;
	}
	const double WeaponRoll = Valhalla::Stats::RollWeaponDamage(WeaponMin, WeaponMax, FMath::FRand());

	// The flat base is per class (classes.json `baseMeleeDamage` /
	// `baseRangedDamage`, 2026-09-24), read at swing time so a web-editor save
	// plus a data reload applies on the next swing. The constants are only the
	// fallback when the class cannot be found.
	double BaseDamage = bRangedPhysical ? Valhalla::BaseRangedDamage : Valhalla::BaseMeleeDamage;
	if (const FValhallaClassTemplate* ClassTemplate = Data->FindClass(PlayerState->ClassId))
	{
		BaseDamage = bRangedPhysical ? ClassTemplate->BaseRangedDamage : ClassTemplate->BaseMeleeDamage;
	}

	// Melee scales with Strength, ranged with Dexterity (Kevin, 2026-09-24).
	const double RawDamage = bRangedPhysical
		? Valhalla::Stats::ComputeRangedPhysicalDamage(BaseDamage + WeaponRoll, Stats.Dexterity)
		: Valhalla::Stats::ComputePhysicalDamage(BaseDamage + WeaponRoll, Stats.Strength);

	UValhallaCombatLibrary::ApplyDamage(Character, Target, RawDamage, /*bMagical=*/false, AutoAttackSkillId);

	const float IntervalMs = GetAutoAttackIntervalMs();
	NextAutoAttackAt = Now + IntervalMs / 1000.0;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Buffs — SkillSystem.ts:923
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaSkillComponent::TickBuffs(double Now)
{
	AValhallaCharacter* Character = GetValhallaOwner();
	AValhallaPlayerState* PlayerState = GetValhallaPlayerState();
	UValhallaSkillHandlerRegistry* Registry = GetRegistry();

	if (!Character || !PlayerState || !Registry)
	{
		return;
	}

	TArray<FValhallaActiveBuff>& Buffs = PlayerState->GetActiveBuffs();

	for (int32 Index = Buffs.Num() - 1; Index >= 0; --Index)
	{
		FValhallaActiveBuff& Buff = Buffs[Index];

		// ── Expiry ───────────────────────────────────────────────────────
		if (Now >= Buff.ExpiresAt)
		{
			// SkillSystem.ts:931 — the cleanup runs before the buff is dropped,
			// because it usually needs to read what the buff left behind.
			Registry->RunBuffCleanup(Character, Buff);

			FValhallaCombatEvent Event;
			Event.Kind = EValhallaCombatEventKind::BuffRemoved;
			Event.Target = Character;
			Event.SkillId = Buff.SkillId;
			UValhallaCombatLibrary::BroadcastCombatEvent(this, Event);

			Buffs.RemoveAt(Index);
			continue;
		}

		const int32 Stacks = FMath::Max(1, Buff.Stacks);

		// ── DoT ──────────────────────────────────────────────────────────
		if (Buff.DotDamagePerSec > 0.f && (Now - Buff.LastTickAt) >= BuffTickIntervalSeconds)
		{
			// Advance by exactly one interval rather than to Now: a hitch must
			// not swallow a tick, and a caught-up server must not fire three at
			// once. NPCSystem.ts:428 does the same thing for the same reason.
			Buff.LastTickAt += BuffTickIntervalSeconds;

			const int32 Damage = FMath::RoundToInt32(static_cast<double>(Buff.DotDamagePerSec) * Stacks);
			PlayerState->Hp = FMath::Max(0.f, PlayerState->Hp - Damage);

			FValhallaCombatEvent Event;
			Event.Kind = EValhallaCombatEventKind::PlayerHit;
			Event.Target = Character;
			Event.Instigator = Buff.Caster.Get();
			Event.SkillId = Buff.SkillId;
			Event.Amount = static_cast<float>(Damage);
			Event.RemainingHp = PlayerState->Hp;
			Event.Location = Character->GetActorLocation();
			UValhallaCombatLibrary::BroadcastCombatEvent(this, Event);

			UE_LOG(LogValhallaCombat, Log, TEXT("dot tick %d on %s from '%s' (x%d stacks)"),
				Damage, *PlayerState->CharacterName, *Buff.SkillId.ToString(), Stacks);

			if (PlayerState->Hp <= 0.f)
			{
				if (AValhallaGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AValhallaGameMode>() : nullptr)
				{
					GameMode->HandlePlayerDeath(Character, Buff.Caster.Get());
				}
				return;
			}
		}

		// ── HoT ──────────────────────────────────────────────────────────
		if (Buff.HotHealPerSec > 0.f && (Now - Buff.LastTickAt) >= BuffTickIntervalSeconds)
		{
			Buff.LastTickAt += BuffTickIntervalSeconds;

			const int32 Heal = FMath::RoundToInt32(static_cast<double>(Buff.HotHealPerSec) * Stacks);
			PlayerState->Hp = FMath::Min(PlayerState->MaxHp, PlayerState->Hp + Heal);

			FValhallaCombatEvent Event;
			Event.Kind = EValhallaCombatEventKind::SkillEffect;
			Event.Target = Character;
			Event.Instigator = Buff.Caster.Get();
			Event.SkillId = Buff.SkillId;
			Event.Amount = static_cast<float>(Heal);
			Event.bHeal = true;
			Event.RemainingHp = PlayerState->Hp;
			Event.Location = Character->GetActorLocation();
			UValhallaCombatLibrary::BroadcastCombatEvent(this, Event);
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  The fixed tick
// ─────────────────────────────────────────────────────────────────────────────

bool UValhallaSkillComponent::AccumulateMovementInterrupt(float FixedDeltaSeconds)
{
	const AValhallaCharacter* Character = GetValhallaOwner();
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return false;
	}

	// GetCurrentAcceleration is what the client's replayed move *asked* for, not
	// where the capsule ended up, so it is zero for a character being pushed,
	// sliding down a slope or settling onto the floor — all of which move a
	// position and none of which are the player choosing to walk.
	const FVector Acceleration = Movement->GetCurrentAcceleration();
	if (Acceleration.SizeSquared2D() <= MovementInterruptAccelThreshold * MovementInterruptAccelThreshold)
	{
		return false;
	}

	MovementDuringCastSeconds += FixedDeltaSeconds;
	return MovementDuringCastSeconds > MovementInterruptSeconds;
}

void UValhallaSkillComponent::ServerFixedTick(float FixedDeltaSeconds, double Now)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	AValhallaPlayerState* PlayerState = GetValhallaPlayerState();
	const UValhallaDataSubsystem* Data = GetData();
	if (!PlayerState || !Data)
	{
		return;
	}

	// ── Sitting: walking off (or dying) stands you up ────────────────────
	if (AValhallaCharacter* Sitter = GetValhallaOwner(); Sitter && Sitter->IsSitting())
	{
		const UCharacterMovementComponent* Movement = Sitter->GetCharacterMovement();
		const bool bMoving = Movement
			&& Movement->GetCurrentAcceleration().SizeSquared2D() > MovementInterruptAccelThreshold * MovementInterruptAccelThreshold;
		if (bMoving || !PlayerState->IsAlive())
		{
			Sitter->SetSitting(false);
		}
	}

	// ── Cast progression (SkillSystem.ts:263) ────────────────────────────
	if (!CastingSkillId.IsNone())
	{
		if (!PlayerState->IsAlive())
		{
			ClearCastingState();
		}
		else if (AccumulateMovementInterrupt(FixedDeltaSeconds))
		{
			const FName Interrupted = CastingSkillId;

			FValhallaCombatEvent Event;
			Event.Kind = EValhallaCombatEventKind::SkillInterrupted;
			Event.Target = GetOwner();
			Event.Instigator = GetOwner();
			Event.SkillId = Interrupted;
			UValhallaCombatLibrary::BroadcastCombatEvent(this, Event);

			UE_LOG(LogValhallaGame, Log, TEXT("%s: '%s' interrupted after %.0f ms of movement."),
				*PlayerState->CharacterName, *Interrupted.ToString(), MovementDuringCastSeconds * 1000.f);

			ClearCastingState();
		}
		else if (Now >= CastStartedAt + CastDurationMs / 1000.0)
		{
			const FName FinishedId = CastingSkillId;
			AActor* Target = PendingCastTarget;
			const FVector Location = PendingCastLocation;

			// Clear first: CompleteCast runs handlers that may start another cast,
			// and ValidateCast's "Already casting" check would refuse it.
			ClearCastingState();

			if (const FValhallaSkillTemplate* Skill = Data->FindSkill(FinishedId))
			{
				// SkillSystem.ts:292 — re-validate the target at fire time. It may
				// have died, left, or (with the 2026-02-20 fix) been the wrong kind
				// of thing all along.
				const bool bNeedsTarget = Skill->TargetType == EValhallaSkillTargetType::SingleEnemy
					|| Skill->TargetType == EValhallaSkillTargetType::SingleAlly;

				if (bNeedsTarget)
				{
					if (!UValhallaCombatLibrary::IsAliveTarget(Target))
					{
						SendSkillFailed(TEXT("Target is no longer valid"));
						return;
					}

					const bool bTargetIsNpc = UValhallaCombatLibrary::IsNpcTarget(Target);
					if ((Skill->TargetType == EValhallaSkillTargetType::SingleAlly && bTargetIsNpc)
						|| (Skill->TargetType == EValhallaSkillTargetType::SingleEnemy && !UValhallaCombatLibrary::AreHostile(GetOwner(), Target)))
					{
						SendSkillFailed(TEXT("Invalid target"));
						return;
					}
				}

				// Facing, again at fire time: the caster may have dragged the
				// camera round mid-cast, or the target walked behind them. The
				// cast fails before CompleteCast, so nothing is spent.
				if (RequiresFacing(*Skill, Target) && !UValhallaCombatLibrary::IsFacing(GetOwner(), Target))
				{
					SendSkillFailed(UValhallaCombatLibrary::NotFacingText(), UValhallaCombatLibrary::NotFacingReason(), FinishedId);
					return;
				}

				CompleteCast(*Skill, Target, Location, Now);
			}
		}
	}

	// ── Buff ticking (SkillSystem.ts:340) ────────────────────────────────
	TickBuffs(Now);

	// ── Auto-attack (GameRoom.update step 4b) ────────────────────────────
	TickAutoAttack(Now);
}
