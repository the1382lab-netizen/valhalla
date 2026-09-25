// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaVfxLibrary.h"

#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/CoreMisc.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "ValhallaAnimComponent.h"
#include "ValhallaCharacter.h"
#include "ValhallaDataSubsystem.h"
#include "ValhallaPlayerState.h"
#include "ValhallaTypes.h"

DEFINE_LOG_CATEGORY(LogValhallaVfx);

namespace
{
	/** The two skills that put an AValhallaSpellProjectile in the air. */
	const FName SkillFireball(TEXT("wizard_fireball"));
	const FName SkillMagicMissile(TEXT("wizard_magic_missile"));

	/** `scalingStat` values, as skills.json spells them. */
	const FName StatStrength(TEXT("strength"));
	const FName StatDexterity(TEXT("dexterity"));
	const FName StatIntelligence(TEXT("intelligence"));
	const FName StatWisdom(TEXT("wisdom"));

	/**
	 * The four-entry palette.
	 *
	 * Phase 8b: saturated, and only modestly over-bright. 8a's values
	 * (1.6/0.42/0.08 and friends) were tuned under a 2.5x sprite glow, which
	 * together pushed every channel past 1 after the tonemapper and read as a
	 * near-white wash whatever the tint. With M_ValhallaVfxSprite's glow at 1.6
	 * the dominant channel lands at ~1.6-1.9 and the others stay low, so a
	 * wizard's bolt reads violet and a heal reads gold.
	 */
	const FLinearColor RedOrange(1.15f, 0.20f, 0.02f, 1.f);
	const FLinearColor Green(0.06f, 1.05f, 0.12f, 1.f);
	const FLinearColor BlueViolet(0.38f, 0.10f, 1.20f, 1.f);
	const FLinearColor Gold(1.15f, 0.70f, 0.05f, 1.f);

	/** A bow's arrow is wood and fletching, not magic. */
	const FLinearColor ArrowBrown(0.85f, 0.5f, 0.22f, 1.f);

	/** What an attack with no skill row at all (an NPC's punch) is drawn in. */
	const FLinearColor NeutralSteel(0.75f, 0.8f, 0.95f, 1.f);

	const FName ColorParameter(TEXT("Color"));
	const FName RadiusParameter(TEXT("Radius"));

	/** The mesh a WeaponHand plan hangs off, or null. */
	USkeletalMeshComponent* FindBodyMesh(AActor* Actor)
	{
		if (!Actor)
		{
			return nullptr;
		}
		if (const AValhallaCharacter* Character = Cast<AValhallaCharacter>(Actor))
		{
			return Character->GetBodyMesh();
		}
		return Actor->FindComponentByClass<USkeletalMeshComponent>();
	}

	/** The data subsystem, or null outside a world. */
	const UValhallaDataSubsystem* FindData(const UObject* WorldContext)
	{
		const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContext);
		return GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  The mapping
// ─────────────────────────────────────────────────────────────────────────────

const TCHAR* UValhallaVfxLibrary::SystemName(EValhallaVfx System)
{
	switch (System)
	{
	case EValhallaVfx::Bolt:     return TEXT("NS_Bolt");
	case EValhallaVfx::Impact:   return TEXT("NS_Impact");
	case EValhallaVfx::Slash:    return TEXT("NS_Slash");
	case EValhallaVfx::Heal:     return TEXT("NS_Heal");
	case EValhallaVfx::BuffAura: return TEXT("NS_BuffAura");
	case EValhallaVfx::Debuff:   return TEXT("NS_Debuff");
	case EValhallaVfx::AoERing:  return TEXT("NS_AoERing");
	case EValhallaVfx::Cone:     return TEXT("NS_Cone");
	default:                     return TEXT("none");
	}
}

FString UValhallaVfxLibrary::SystemPath(EValhallaVfx System)
{
	if (System == EValhallaVfx::None)
	{
		return FString();
	}
	const FString Name(SystemName(System));
	return FString::Printf(TEXT("%s/%s.%s"), VfxRoot(), *Name, *Name);
}

FLinearColor UValhallaVfxLibrary::FromPackedColor(int32 Packed)
{
	const FColor Srgb(
		static_cast<uint8>((Packed >> 16) & 0xFF),
		static_cast<uint8>((Packed >> 8) & 0xFF),
		static_cast<uint8>(Packed & 0xFF));
	return FLinearColor::FromSRGBColor(Srgb);
}

bool UValhallaVfxLibrary::IsProjectileSkill(const FValhallaSkillTemplate& Skill)
{
	// `bHasProjectile` is the data's own answer and is checked first, even
	// though no 1.0 skill currently sets it: a designer adding a `projectile`
	// block in skills.json should get a bolt without editing C++. The two named
	// ids are the skills the handler registry actually gives a projectile to.
	return Skill.bHasProjectile || Skill.Id == SkillFireball || Skill.Id == SkillMagicMissile;
}

bool UValhallaVfxLibrary::SpawnsProjectileActor(const FValhallaSkillTemplate& Skill)
{
	return IsProjectileSkill(Skill) && Skill.Id != SkillMagicMissile;
}

FLinearColor UValhallaVfxLibrary::ColorForSkill(const FValhallaSkillTemplate& Skill, int32 ClassColorPacked)
{
	if (Skill.ScalingStat == StatStrength)
	{
		return RedOrange;
	}
	if (Skill.ScalingStat == StatDexterity)
	{
		return Green;
	}
	if (Skill.ScalingStat == StatIntelligence)
	{
		return BlueViolet;
	}
	if (Skill.ScalingStat == StatWisdom)
	{
		return Gold;
	}

	// stamina, or nothing at all. The caster's class colour is the fallback,
	// and it is over-brightened to the same degree as the palette so a
	// stamina skill does not read as a dim version of everything else.
	if (ClassColorPacked != 0)
	{
		return FromPackedColor(ClassColorPacked) * 1.2f;
	}
	return NeutralSteel;
}

FValhallaVfxPlan UValhallaVfxLibrary::ResolveForSkill(
	const FValhallaSkillTemplate& Skill,
	int32 ClassColorPacked,
	EValhallaAttackCycle WeaponCycle)
{
	FValhallaVfxPlan Plan;
	Plan.Color = ColorForSkill(Skill, ClassColorPacked);

	// ── 1. An auto-attack is the weapon, not the spell ───────────────────
	if (Skill.bIsAutoAttack)
	{
		if (WeaponCycle == EValhallaAttackCycle::Melee)
		{
			Plan.System = EValhallaVfx::Slash;
			Plan.Attach = EValhallaVfxAttach::ActorFeet;
			Plan.Radius = 95.f;
			return Plan;
		}

		Plan.System = EValhallaVfx::Bolt;
		Plan.Attach = EValhallaVfxAttach::WeaponHand;
		Plan.Radius = ArrowBoltRadius;
		if (WeaponCycle == EValhallaAttackCycle::Shoot)
		{
			// An arrow is not a spell and should not be class-coloured.
			Plan.Color = ArrowBrown;
		}
		return Plan;
	}

	// ── 2. Anything that flies ───────────────────────────────────────────
	if (IsProjectileSkill(Skill))
	{
		Plan.System = EValhallaVfx::Bolt;
		Plan.Attach = EValhallaVfxAttach::WeaponHand;
		Plan.Radius = Skill.ProjectileRadius > 0.f ? Skill.ProjectileRadius : DefaultBoltRadius;
		return Plan;
	}

	// ── 3. Category, where the category is the whole point ───────────────
	if (Skill.Category == EValhallaSkillCategory::Healing)
	{
		Plan.System = EValhallaVfx::Heal;
		Plan.Attach = EValhallaVfxAttach::ActorFeet;
		Plan.Radius = 40.f;
		return Plan;
	}

	if (Skill.Category == EValhallaSkillCategory::Buff
		|| Skill.Category == EValhallaSkillCategory::Defensive)
	{
		Plan.System = EValhallaVfx::BuffAura;
		Plan.Attach = EValhallaVfxAttach::ActorFeet;
		Plan.Radius = AuraRadius;
		Plan.bLooping = true;
		if (Skill.Category == EValhallaSkillCategory::Defensive)
		{
			// A ward reads as gold whatever stat drives it — shield_of_faith
			// and mana_shield should look like the same kind of thing.
			Plan.Color = Gold;
		}
		return Plan;
	}

	// ── 4. Shape, where the shape is the information ─────────────────────
	if (Skill.TargetType == EValhallaSkillTargetType::Cone)
	{
		Plan.System = EValhallaVfx::Cone;
		Plan.Attach = EValhallaVfxAttach::ActorFeet;
		Plan.Radius = Skill.Range > 0.f ? Skill.Range : DefaultAoeRadius;
		return Plan;
	}

	if (Skill.TargetType == EValhallaSkillTargetType::AoeGround
		|| Skill.TargetType == EValhallaSkillTargetType::AoeSelf)
	{
		Plan.System = EValhallaVfx::AoERing;
		Plan.Attach = Skill.TargetType == EValhallaSkillTargetType::AoeSelf
			? EValhallaVfxAttach::ActorFeet
			: EValhallaVfxAttach::WorldLocation;
		Plan.Radius = Skill.AoeRadius > 0.f ? Skill.AoeRadius : DefaultAoeRadius;
		return Plan;
	}

	// ── 5. What is left ──────────────────────────────────────────────────
	if (Skill.Category == EValhallaSkillCategory::Debuff)
	{
		Plan.System = EValhallaVfx::Debuff;
		Plan.Attach = EValhallaVfxAttach::ActorFeet;
		Plan.Radius = 35.f;
		return Plan;
	}

	if (Skill.Category == EValhallaSkillCategory::Utility
		&& (Skill.TargetType == EValhallaSkillTargetType::Self
			|| Skill.TargetType == EValhallaSkillTargetType::SingleAlly))
	{
		// Blink and Cure Ailment: a friendly gesture, not a blow.
		Plan.System = EValhallaVfx::BuffAura;
		Plan.Attach = EValhallaVfxAttach::ActorFeet;
		Plan.Radius = AuraRadius;
		Plan.bLooping = false;
		return Plan;
	}

	Plan.System = EValhallaVfx::Impact;
	Plan.Attach = EValhallaVfxAttach::WorldLocation;
	Plan.Radius = 70.f;
	return Plan;
}

FValhallaVfxPlan UValhallaVfxLibrary::ResolveForSkillId(const UObject* WorldContext, FName SkillId, AActor* Caster)
{
	const UValhallaDataSubsystem* Data = FindData(WorldContext);
	const FValhallaSkillTemplate* Skill = Data ? Data->FindSkill(SkillId) : nullptr;

	int32 ClassColor = 0;
	if (const AValhallaCharacter* Character = Cast<AValhallaCharacter>(Caster))
	{
		if (const AValhallaPlayerState* PlayerState = Character->GetValhallaPlayerState())
		{
			ClassColor = Data ? Data->GetClassColor(PlayerState->ClassId) : 0;
		}
	}

	if (!Skill)
	{
		// Not a skill at all — an NPC's attack carries its template id. The
		// only sensible reading is "somebody hit something", which is a swing.
		FValhallaVfxPlan Plan;
		Plan.System = EValhallaVfx::Slash;
		Plan.Attach = EValhallaVfxAttach::ActorFeet;
		Plan.Color = ClassColor != 0 ? FromPackedColor(ClassColor) * 1.2f : NeutralSteel;
		Plan.Radius = 95.f;
		return Plan;
	}

	return ResolveForSkill(*Skill, ClassColor, CycleFor(Caster));
}

EValhallaAttackCycle UValhallaVfxLibrary::CycleFor(AActor* Actor)
{
	if (const UValhallaAnimComponent* Anim = UValhallaAnimComponent::Find(Actor))
	{
		return Anim->GetAttackCycle();
	}
	return EValhallaAttackCycle::Melee;
}

// ─────────────────────────────────────────────────────────────────────────────
//  The runtime
// ─────────────────────────────────────────────────────────────────────────────

bool UValhallaVfxSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	// A dedicated server has no viewer, so it has nothing to draw for. The
	// check is here rather than at each spawn because "there is no object" is
	// a stronger guarantee than "every call site remembered the guard".
	if (IsRunningDedicatedServer())
	{
		return false;
	}

	const UWorld* World = Cast<UWorld>(Outer);
	if (World && World->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}

	// Editor preview and inactive worlds never tick gameplay.
	return World == nullptr || World->IsGameWorld();
}

void UValhallaVfxSubsystem::Deinitialize()
{
	for (const FActiveAura& Aura : Auras)
	{
		if (UNiagaraComponent* Component = Aura.Component.Get())
		{
			Component->DestroyComponent();
		}
	}
	Auras.Reset();

	for (const FTimedEffect& Effect : Timed)
	{
		if (UNiagaraComponent* Component = Effect.Component.Get())
		{
			Component->DestroyComponent();
		}
	}
	Timed.Reset();

	Systems.Reset();

	Super::Deinitialize();
}

TStatId UValhallaVfxSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UValhallaVfxSubsystem, STATGROUP_Tickables);
}

void UValhallaVfxSubsystem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	for (int32 Index = Timed.Num() - 1; Index >= 0; --Index)
	{
		FTimedEffect& Effect = Timed[Index];

		UNiagaraComponent* Component = Effect.Component.Get();
		if (!Component)
		{
			Timed.RemoveAt(Index);
			continue;
		}

		Effect.RemainingSeconds -= DeltaSeconds;
		if (Effect.RemainingSeconds > 0.f)
		{
			continue;
		}

		// Deactivate, not destroy: the particles already in the air run out
		// their own lives, and bAutoDestroy collects the component once the
		// last of them has gone.
		Component->Deactivate();
		Component->SetAutoDestroy(true);
		Timed.RemoveAt(Index);
	}
}

UValhallaVfxSubsystem* UValhallaVfxSubsystem::Find(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UValhallaVfxSubsystem>() : nullptr;
}

void UValhallaVfxSubsystem::DispatchCombatEvent(const UObject* WorldContext, const FValhallaCombatEvent& Event)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Valhalla_Vfx_Dispatch);
	if (UValhallaVfxSubsystem* Subsystem = Find(WorldContext))
	{
		Subsystem->HandleCombatEvent(Event);
	}
}

UNiagaraSystem* UValhallaVfxSubsystem::GetSystem(EValhallaVfx System)
{
	const int32 Index = static_cast<int32>(System);
	if (Index <= 0)
	{
		return nullptr;
	}

	if (Systems.Num() <= Index)
	{
		Systems.SetNum(static_cast<int32>(EValhallaVfx::Cone) + 1);
	}

	if (!Systems[Index])
	{
		const FString Path = UValhallaVfxLibrary::SystemPath(System);
		Systems[Index] = LoadObject<UNiagaraSystem>(nullptr, *Path);

		if (!Systems[Index] && !ReportedMissing.Contains(static_cast<uint8>(Index)))
		{
			ReportedMissing.Add(static_cast<uint8>(Index));
			UE_LOG(LogValhallaVfx, Warning, TEXT("missing Niagara system %s"), *Path);
		}
	}

	return Systems[Index];
}

UNiagaraComponent* UValhallaVfxSubsystem::Spawn(
	const FValhallaVfxPlan& Plan, FName SkillId, AActor* Anchor, const FVector& Location)
{
	if (!Plan.IsValid())
	{
		return nullptr;
	}

	UNiagaraSystem* System = GetSystem(Plan.System);
	if (!System)
	{
		return nullptr;
	}

	UNiagaraComponent* Component = nullptr;

	// bAutoActivate is false on every path below and the component is activated
	// by hand once its user parameters are set. A system that activates first
	// spawns its opening frame of particles against the *default* colour, which
	// on a 0.25 s effect is a visible fraction of the whole thing.
	const bool bAutoDestroy = !Plan.bLooping;

	// A body with no weapon socket — an NPC rig that failed to import — falls
	// back to the root rather than dropping the effect on the floor.
	USkeletalMeshComponent* Mesh = Plan.Attach == EValhallaVfxAttach::WeaponHand
		? FindBodyMesh(Anchor) : nullptr;
	const bool bHandSocket = Mesh && Mesh->DoesSocketExist(UValhallaVisuals::WeaponSocket());

	if (bHandSocket)
	{
		Component = UNiagaraFunctionLibrary::SpawnSystemAttached(
			System, Mesh, UValhallaVisuals::WeaponSocket(),
			FVector::ZeroVector, FRotator::ZeroRotator,
			EAttachLocation::SnapToTarget, bAutoDestroy, /*bAutoActivate=*/false);
	}
	else if (Plan.Attach != EValhallaVfxAttach::WorldLocation)
	{
		USceneComponent* Root = Anchor ? Anchor->GetRootComponent() : nullptr;
		if (!Root)
		{
			return nullptr;
		}

		// The slash arc is authored starting at the shape's local +X; rotating
		// it back by half its sweep is what puts it in front of the attacker
		// rather than off their right shoulder.
		const FRotator Relative = Plan.System == EValhallaVfx::Slash
			? FRotator(0.f, UValhallaVfxLibrary::SlashArcYaw, 0.f)
			: FRotator::ZeroRotator;

		// Feet, not centre: the capsule's origin is its middle, and every
		// ground ring in the set is drawn flat on the floor — a few
		// centimetres above it, or it z-fights the tile and vanishes.
		const FVector Offset(0.f, 0.f,
			UValhallaVisuals::MeshZOffset + UValhallaVfxLibrary::GroundLift);

		Component = UNiagaraFunctionLibrary::SpawnSystemAttached(
			System, Root, NAME_None, Offset, Relative,
			EAttachLocation::KeepRelativeOffset, bAutoDestroy, /*bAutoActivate=*/false);
	}
	else
	{
		Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this, System, Location, FRotator::ZeroRotator, FVector::OneVector,
			bAutoDestroy, /*bAutoActivate=*/false);
	}

	if (!Component)
	{
		return nullptr;
	}

	Component->SetVariableLinearColor(ColorParameter, Plan.Color);
	Component->SetVariableFloat(RadiusParameter, Plan.Radius);
	Component->Activate(/*bReset=*/true);

	UE_LOG(LogValhallaVfx, Log,
		TEXT("%s -> %s color=(%.2f,%.2f,%.2f) radius=%.0f attach=%d"),
		*SkillId.ToString(), UValhallaVfxLibrary::SystemName(Plan.System),
		Plan.Color.R, Plan.Color.G, Plan.Color.B, Plan.Radius,
		static_cast<int32>(Plan.Attach));

	return Component;
}

void UValhallaVfxSubsystem::SpawnTimed(
	const FValhallaVfxPlan& Plan, FName SkillId, AActor* Anchor, const FVector& Location, float Seconds)
{
	// NS_Bolt loops forever, because the thing that usually carries it is a
	// projectile actor that decides when it stops. Used as a muzzle flash for a
	// spell that has no projectile, something else has to end it — see Tick.
	FValhallaVfxPlan Held = Plan;
	Held.bLooping = true;

	if (UNiagaraComponent* Component = Spawn(Held, SkillId, Anchor, Location))
	{
		FTimedEffect Effect;
		Effect.Component = Component;
		Effect.RemainingSeconds = Seconds;
		Timed.Add(Effect);
	}
}

void UValhallaVfxSubsystem::StartAura(const FValhallaVfxPlan& Plan, FName SkillId, AActor* Target)
{
	if (!Target)
	{
		return;
	}

	// Re-applying a buff refreshes its aura rather than stacking a second one:
	// two identical rings at the same radius read as one brighter ring, and the
	// second buffRemoved would have nothing to stop.
	StopAura(SkillId, Target);

	UNiagaraComponent* Component = Spawn(Plan, SkillId, Target, Target->GetActorLocation());
	if (!Component)
	{
		return;
	}

	FActiveAura Aura;
	Aura.Target = Target;
	Aura.SkillId = SkillId;
	Aura.Component = Component;
	Auras.Add(Aura);
}

void UValhallaVfxSubsystem::StopAura(FName SkillId, AActor* Target)
{
	for (int32 Index = Auras.Num() - 1; Index >= 0; --Index)
	{
		const FActiveAura& Aura = Auras[Index];

		// A dead entry is swept here rather than on a tick: the only thing that
		// ever asks is a buffRemoved, and a buff that outlived its target is
		// exactly the case this has to be robust to.
		const bool bStale = !Aura.Target.IsValid() || !Aura.Component.IsValid();
		const bool bMatch = Aura.Target.Get() == Target
			&& (SkillId.IsNone() || Aura.SkillId == SkillId);

		if (!bStale && !bMatch)
		{
			continue;
		}

		if (UNiagaraComponent* Component = Aura.Component.Get())
		{
			// Deactivate rather than destroy outright, so the motes already in
			// the air finish their lives instead of vanishing mid-flight.
			Component->Deactivate();
			Component->SetAutoDestroy(true);
		}
		Auras.RemoveAt(Index);
	}
}

void UValhallaVfxSubsystem::HandleSwingOutcome(const FValhallaCombatEvent& Event)
{
	const UValhallaDataSubsystem* Data = FindData(this);
	const FValhallaSkillTemplate* Skill = Data ? Data->FindSkill(Event.SkillId) : nullptr;

	// An auto-attack is flagged in the data; an id that is not a skill at all
	// is an NPC's template id, which is also a swing. Anything else is a spell
	// or a DoT tick, and its effect was already drawn when it was cast.
	const bool bIsSwing = Skill ? Skill->bIsAutoAttack : true;
	if (!bIsSwing)
	{
		return;
	}

	AActor* Attacker = Event.Instigator;
	if (Attacker)
	{
		const FValhallaVfxPlan Plan = UValhallaVfxLibrary::ResolveForSkillId(this, Event.SkillId, Attacker);
		if (Plan.System == EValhallaVfx::Bolt)
		{
			// A bow or a staff auto-attack has no projectile actor, so the bolt
			// is a muzzle flash that has to be stopped by hand.
			SpawnTimed(Plan, Event.SkillId, Attacker, Attacker->GetActorLocation(),
				UValhallaVfxLibrary::InstantBoltSeconds);
		}
		else
		{
			Spawn(Plan, Event.SkillId, Attacker, Attacker->GetActorLocation());
		}
	}

	// A blow that actually landed also flashes on whatever it landed on.
	if (Event.Target && !Event.bHeal && Event.Amount > 0.f
		&& (Event.Kind == EValhallaCombatEventKind::PlayerHit
			|| Event.Kind == EValhallaCombatEventKind::NpcHit
			|| Event.Kind == EValhallaCombatEventKind::Blocked))
	{
		FValhallaVfxPlan Hit = UValhallaVfxLibrary::ResolveForSkillId(this, Event.SkillId, Attacker);
		Hit.System = EValhallaVfx::Impact;
		Hit.Attach = EValhallaVfxAttach::WorldLocation;
		Hit.Radius = 45.f;
		Spawn(Hit, Event.SkillId, Event.Target, Event.Target->GetActorLocation());
	}
}

void UValhallaVfxSubsystem::HandleCombatEvent(const FValhallaCombatEvent& Event)
{
	switch (Event.Kind)
	{
	// ── A cast went off ──────────────────────────────────────────────────
	case EValhallaCombatEventKind::SkillEffect:
	{
		FValhallaVfxPlan Plan = UValhallaVfxLibrary::ResolveForSkillId(this, Event.SkillId, Event.Instigator);
		if (!Plan.IsValid())
		{
			return;
		}

		// A looping aura and a debuff cloud are started by the buff events, not
		// by the cast: they have to live exactly as long as the buff does, and
		// buffApplied is the only event that says how long that is.
		if (Plan.System == EValhallaVfx::BuffAura && Plan.bLooping)
		{
			return;
		}
		if (Plan.System == EValhallaVfx::Debuff)
		{
			return;
		}

		switch (Plan.System)
		{
		case EValhallaVfx::Heal:
			// At the *receiver's* feet. A cleric healing a warrior across the
			// room should sparkle on the warrior.
			Spawn(Plan, Event.SkillId, Event.Target ? Event.Target : Event.Instigator, Event.Location);
			break;

		case EValhallaVfx::Bolt:
		{
			// Magic Missile does not spawn a projectile actor, so its bolt is a
			// flash at the hand. Fireball's bolt *is* the projectile and this
			// one is the flash that launches it; both are timed out.
			SpawnTimed(Plan, Event.SkillId, Event.Instigator, Event.Location,
				UValhallaVfxLibrary::InstantBoltSeconds);

			// Magic Missile never misses and never detonates, so nothing else
			// would ever mark where it landed. A fireball *does* detonate: its
			// spellImpact draws the burst where it actually lands, so drawing
			// one on the selected target at cast time as well was a second,
			// wrong impact (Phase 8a's leftover).
			const UValhallaDataSubsystem* Data = FindData(this);
			const FValhallaSkillTemplate* Skill = Data ? Data->FindSkill(Event.SkillId) : nullptr;
			const bool bHasProjectileActor = Skill && UValhallaVfxLibrary::SpawnsProjectileActor(*Skill);
			if (Event.Target && Event.Target != Event.Instigator && !bHasProjectileActor)
			{
				FValhallaVfxPlan Hit = Plan;
				Hit.System = EValhallaVfx::Impact;
				Hit.Attach = EValhallaVfxAttach::WorldLocation;
				Hit.Radius = 55.f;
				Spawn(Hit, Event.SkillId, Event.Target, Event.Target->GetActorLocation());
			}
			break;
		}

		case EValhallaVfx::AoERing:
			if (Plan.Attach == EValhallaVfxAttach::WorldLocation)
			{
				Spawn(Plan, Event.SkillId, nullptr, Event.Location);
			}
			else
			{
				Spawn(Plan, Event.SkillId, Event.Instigator, Event.Location);
			}
			break;

		case EValhallaVfx::Slash:
		case EValhallaVfx::Cone:
		case EValhallaVfx::BuffAura:
			Spawn(Plan, Event.SkillId, Event.Instigator, Event.Location);
			break;

		case EValhallaVfx::Impact:
		default:
			Spawn(Plan, Event.SkillId, Event.Target,
				Event.Target ? Event.Target->GetActorLocation() : FVector(Event.Location));
			break;
		}
		break;
	}

	// ── A projectile detonated ───────────────────────────────────────────
	case EValhallaCombatEventKind::SpellImpact:
	{
		FValhallaVfxPlan Plan = UValhallaVfxLibrary::ResolveForSkillId(this, Event.SkillId, Event.Instigator);

		FValhallaVfxPlan Burst = Plan;
		Burst.System = EValhallaVfx::Impact;
		Burst.Attach = EValhallaVfxAttach::WorldLocation;
		Burst.bLooping = false;
		Burst.Radius = Event.Amount > 0.f ? Event.Amount : 70.f;
		Spawn(Burst, Event.SkillId, nullptr, Event.Location);

		// The blast radius is carried on the event, so the ring is the truth
		// about how far the damage reached rather than a guess at it.
		if (Event.Amount > 0.f)
		{
			FValhallaVfxPlan Ring = Plan;
			Ring.System = EValhallaVfx::AoERing;
			Ring.Attach = EValhallaVfxAttach::WorldLocation;
			Ring.bLooping = false;
			Ring.Radius = Event.Amount;
			Spawn(Ring, Event.SkillId, nullptr, Event.Location);
		}
		break;
	}

	// ── A buff went on or came off ───────────────────────────────────────
	case EValhallaCombatEventKind::BuffApplied:
	{
		const FValhallaVfxPlan Plan = UValhallaVfxLibrary::ResolveForSkillId(this, Event.SkillId, Event.Instigator);
		if (!Plan.IsValid() || !Event.Target)
		{
			return;
		}

		if (Plan.bLooping)
		{
			StartAura(Plan, Event.SkillId, Event.Target);
		}
		else if (Plan.System == EValhallaVfx::Debuff)
		{
			Spawn(Plan, Event.SkillId, Event.Target, Event.Target->GetActorLocation());
		}
		break;
	}

	case EValhallaCombatEventKind::BuffRemoved:
		StopAura(Event.SkillId, Event.Target);
		break;

	// ── A blow ───────────────────────────────────────────────────────────
	case EValhallaCombatEventKind::PlayerHit:
	case EValhallaCombatEventKind::NpcHit:
	case EValhallaCombatEventKind::Missed:
	case EValhallaCombatEventKind::Dodged:
	case EValhallaCombatEventKind::Blocked:
		HandleSwingOutcome(Event);
		break;

	// ── A corpse carries no buffs ────────────────────────────────────────
	case EValhallaCombatEventKind::PlayerDied:
	case EValhallaCombatEventKind::NpcDied:
		StopAura(NAME_None, Event.Target);
		break;

	default:
		break;
	}
}
