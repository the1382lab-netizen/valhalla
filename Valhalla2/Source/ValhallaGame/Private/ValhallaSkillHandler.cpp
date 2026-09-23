// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaSkillHandler.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "ValhallaCharacter.h"
#include "ValhallaCombatLibrary.h"
#include "ValhallaConstants.h"
#include "ValhallaDataSubsystem.h"
#include "ValhallaGame.h"
#include "ValhallaGameMode.h"
#include "ValhallaNPC.h"
#include "ValhallaPlayerState.h"
#include "ValhallaSpellProjectile.h"
#include "ValhallaStats.h"

namespace
{
	/** The skill ids that get a hand-written handler. The 2.0 handlers/index.ts. */
	const FName SkillFireball(TEXT("wizard_fireball"));
	const FName SkillMagicMissile(TEXT("wizard_magic_missile"));
	const FName SkillTaunt(TEXT("warrior_taunt"));
	const FName SkillShieldOfFaith(TEXT("cleric_shield_of_faith"));
	const FName SkillBackstab(TEXT("rogue_backstab"));
	const FName SkillPoisonBlade(TEXT("rogue_poison_blade"));

	/** SkillEffectHandler.ts:102 — every scaled effect multiplies the stat by this. */
	constexpr double SkillStatScaling = 0.8;

	/** SkillEffectHandler.ts:147 — except healing, which scales a shade harder. */
	constexpr double HealStatScaling = 0.9;

	/** SkillEffectHandler.ts:295 — a cone is ±45°. */
	constexpr double ConeHalfAngleDegrees = 45.0;

	/** The resolved stat block behind an actor, or a zeroed one. */
	FValhallaResolvedStats GetCasterStats(const AActor* Caster)
	{
		if (const AValhallaCharacter* Character = Cast<AValhallaCharacter>(Caster))
		{
			if (const AValhallaPlayerState* PlayerState = Character->GetValhallaPlayerState())
			{
				return PlayerState->GetStats();
			}
		}
		return FValhallaResolvedStats();
	}

	/** Write HP directly, for the one handler that must not run the hit pipeline. */
	void ApplyUnavoidableDamage(AActor* Attacker, AActor* Target, int32 FinalDamage, bool bCrit, FName SkillId, double Now)
	{
		if (!Target || FinalDamage <= 0)
		{
			return;
		}

		bool bDied = false;

		if (AValhallaNPC* Npc = Cast<AValhallaNPC>(Target))
		{
			Npc->ApplyDamageFromAttacker(Attacker, FinalDamage, Now, bDied);
		}
		else if (AValhallaCharacter* Character = Cast<AValhallaCharacter>(Target))
		{
			if (AValhallaPlayerState* PlayerState = Character->GetValhallaPlayerState())
			{
				double Absorbed = 0.0;
				double RemainingShield = 0.0;
				const double ToHp = Valhalla::Stats::ApplyShieldAbsorption(FinalDamage, PlayerState->ShieldHp, Absorbed, RemainingShield);

				PlayerState->ShieldHp = static_cast<float>(RemainingShield);
				PlayerState->Hp = FMath::Max(0.f, PlayerState->Hp - static_cast<float>(ToHp));
				bDied = PlayerState->Hp <= 0.f;
			}
		}

		FValhallaCombatEvent Event;
		Event.Kind = UValhallaCombatLibrary::IsNpcTarget(Target)
			? EValhallaCombatEventKind::NpcHit
			: EValhallaCombatEventKind::PlayerHit;
		Event.Target = Target;
		Event.Instigator = Attacker;
		Event.SkillId = SkillId;
		Event.Amount = static_cast<float>(FinalDamage);
		Event.bCrit = bCrit;
		Event.RemainingHp = static_cast<float>(UValhallaCombatLibrary::DescribeCombatant(Target).Hp);
		Event.Location = Target->GetActorLocation();
		UValhallaCombatLibrary::BroadcastCombatEvent(Target, Event);

		UE_LOG(LogValhallaCombat, Log, TEXT("rolls hit=n/a dodge=n/a crit=%s block=n/a -> hit damage=%d  %s -> %s  skill=%s (unavoidable)"),
			bCrit ? TEXT("yes") : TEXT("no"), FinalDamage,
			*UValhallaCombatLibrary::GetDisplayName(Attacker),
			*UValhallaCombatLibrary::GetDisplayName(Target),
			*SkillId.ToString());

		if (bDied && !UValhallaCombatLibrary::IsNpcTarget(Target))
		{
			if (UWorld* World = Target->GetWorld())
			{
				if (AValhallaGameMode* GameMode = World->GetAuthGameMode<AValhallaGameMode>())
				{
					GameMode->HandlePlayerDeath(Cast<AValhallaCharacter>(Target), Attacker);
				}
			}
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Shared helpers
// ─────────────────────────────────────────────────────────────────────────────

double UValhallaSkillHandler::RollBaseDamage(const FValhallaSkillTemplate& Skill)
{
	if (!Skill.bHasBaseDamage)
	{
		return 0.0;
	}

	// skills.json has at least one entry with baseDamage [25, 5] — max below min
	// (wizard_magic_missile). 1.0's `min + rand * (max - min)` quietly produced a
	// value in [5, 25] for it, so the same expression is used rather than a
	// "corrected" Min/Max: the goal is the 1.0 number, not the tidy one.
	const double Min = Skill.BaseDamageMin;
	const double Max = Skill.BaseDamageMax;
	return Min + FMath::FRand() * (Max - Min);
}

double UValhallaSkillHandler::RollBaseHealing(const FValhallaSkillTemplate& Skill)
{
	if (!Skill.bHasBaseHealing)
	{
		return 0.0;
	}

	const double Min = Skill.BaseHealingMin;
	const double Max = Skill.BaseHealingMax;
	return Min + FMath::FRand() * (Max - Min);
}

double UValhallaSkillHandler::GetScalingStat(const AActor* Caster, const FValhallaSkillTemplate& Skill)
{
	return UValhallaCombatLibrary::GetStatValue(GetCasterStats(Caster), Skill.ScalingStat);
}

void UValhallaSkillHandler::GetCritStats(const AActor* Caster, double& OutCritChance, double& OutCritDamage)
{
	const FValhallaCombatant Info = UValhallaCombatLibrary::DescribeCombatant(Caster);

	// The `?? 0.05` / `?? 0.5` fallbacks all over the 1.0 handlers existed
	// because `stats` could be null before a character finished loading. The
	// same can happen here between PostLogin and the first replication tick.
	OutCritChance = Info.bValid ? Info.CritChance : 0.05;
	OutCritDamage = Info.bValid ? Info.CritDamage : 0.5;
}

void UValhallaSkillHandler::ApplyBuffToTarget(AActor* Target, const FValhallaActiveBuff& Buff, const FValhallaSkillTemplate& Skill)
{
	if (!Target)
	{
		return;
	}

	if (AValhallaNPC* Npc = Cast<AValhallaNPC>(Target))
	{
		// SkillEffectHandler.ts:213 `applyNpcBuff` — NPCs are always replace.
		Npc->ApplyNPCBuff(Buff);
		return;
	}

	if (AValhallaCharacter* Character = Cast<AValhallaCharacter>(Target))
	{
		if (AValhallaPlayerState* PlayerState = Character->GetValhallaPlayerState())
		{
			UValhallaCombatLibrary::ApplyBuff(PlayerState->GetActiveBuffs(), Buff, Skill.StackingMode, Skill.MaxStacks);
		}
	}
}

void UValhallaSkillHandler::BroadcastBuffApplied(const FValhallaSkillContext& Context, AActor* Target, float DurationMs)
{
	FValhallaCombatEvent Event;
	Event.Kind = EValhallaCombatEventKind::BuffApplied;
	Event.Target = Target;
	Event.Instigator = Context.Caster;
	Event.SkillId = Context.Skill.Id;
	Event.Amount = DurationMs;
	Event.Location = Target ? Target->GetActorLocation() : Context.AimPoint;
	UValhallaCombatLibrary::BroadcastCombatEvent(Context.World, Event);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Default handler — SkillEffectHandler.ts:83
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaDefaultSkillHandler::GatherAffectedTargets(const FValhallaSkillContext& Context, TArray<AActor*>& OutTargets)
{
	const FValhallaSkillTemplate& Skill = Context.Skill;
	AActor* Caster = Context.Caster;
	UWorld* World = Context.World;

	switch (Skill.TargetType)
	{
	case EValhallaSkillTargetType::SingleEnemy:
	case EValhallaSkillTargetType::SingleAlly:
		// SkillEffectHandler.ts:273 — a single-target skill never hits its caster,
		// even if the client somehow selected them.
		if (Context.Target && Context.Target != Caster && UValhallaCombatLibrary::IsAliveTarget(Context.Target))
		{
			OutTargets.Add(Context.Target);
		}
		return;

	case EValhallaSkillTargetType::AoeGround:
		// SkillEffectHandler.ts:313 — nothing. A ground skill's damage happens
		// when its projectile lands, which is somewhere else entirely.
		return;

	case EValhallaSkillTargetType::AoeSelf:
	case EValhallaSkillTargetType::Cone:
	{
		if (!World || !Caster)
		{
			return;
		}

		const FVector Origin = Caster->GetActorLocation();
		const double RangeSq = static_cast<double>(Skill.Range) * Skill.Range;
		const double CasterYaw = Caster->GetActorRotation().Yaw;

		// 1.0 swept its player and NPC maps. 2.0 sweeps the actor iterators for
		// the same two kinds of thing, and applies the same hostility rule.
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Candidate = *It;
			if (Candidate == Caster || !UValhallaCombatLibrary::IsAliveTarget(Candidate))
			{
				continue;
			}
			if (!UValhallaCombatLibrary::AreHostile(Caster, Candidate))
			{
				continue;
			}

			const FVector Delta = Candidate->GetActorLocation() - Origin;
			if (Delta.SizeSquared2D() > RangeSq)
			{
				continue;
			}

			if (Skill.TargetType == EValhallaSkillTargetType::Cone)
			{
				// SkillEffectHandler.ts:303 — the arc is measured off the caster's
				// aim, which in 2.0 is the actor yaw (see AValhallaCharacter).
				const double AngleToTarget = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
				if (FMath::Abs(FRotator::NormalizeAxis(AngleToTarget - CasterYaw)) > ConeHalfAngleDegrees)
				{
					continue;
				}
			}

			OutTargets.Add(Candidate);
		}
		return;
	}

	default:
		if (Context.Target)
		{
			OutTargets.Add(Context.Target);
		}
		return;
	}
}

void UValhallaDefaultSkillHandler::Execute(const FValhallaSkillContext& Context)
{
	const FValhallaSkillTemplate& Skill = Context.Skill;
	AActor* Caster = Context.Caster;

	double CritChance = 0.05;
	double CritDamage = 0.5;
	GetCritStats(Caster, CritChance, CritDamage);

	// ── Damage skills (SkillEffectHandler.ts:94) ─────────────────────────
	if (Skill.bHasBaseDamage)
	{
		TArray<AActor*> Targets;
		GatherAffectedTargets(Context, Targets);

		for (AActor* Target : Targets)
		{
			if (!UValhallaCombatLibrary::IsAliveTarget(Target))
			{
				continue;
			}

			const double Scaled = RollBaseDamage(Skill) + GetScalingStat(Caster, Skill) * SkillStatScaling;

			// 1.0 rolled its own crit here and then dealt the damage raw. 2.0
			// hands the scaled number to ApplyDamage, which rolls the crit (and
			// the hit, dodge and block 1.0 skipped for skills) inside the one
			// pipeline. The scaling is identical; the mitigation is now honest.
			UValhallaCombatLibrary::ApplyDamage(Caster, Target, Scaled,
				Skill.Category == EValhallaSkillCategory::Offensive && Skill.ResourceType == EValhallaResourceType::Mana,
				Skill.Id);

			// SkillEffectHandler.ts:123 — a damage skill that also names a DoT
			// leaves one behind on whatever survived.
			if (Skill.DotDamagePerSec > 0.f && Skill.BuffDurationMs > 0.f && UValhallaCombatLibrary::IsAliveTarget(Target))
			{
				FValhallaActiveBuff Buff;
				Buff.SkillId = Skill.Id;
				Buff.Caster = Caster;
				Buff.AppliedAt = Context.ServerTime;
				Buff.LastTickAt = Context.ServerTime;
				Buff.ExpiresAt = Context.ServerTime + Skill.BuffDurationMs / 1000.0;
				Buff.DotDamagePerSec = Skill.DotDamagePerSec;

				ApplyBuffToTarget(Target, Buff, Skill);
				BroadcastBuffApplied(Context, Target, Skill.BuffDurationMs);
			}
		}
	}

	// ── Healing skills (SkillEffectHandler.ts:138) ───────────────────────
	if (Skill.bHasBaseHealing)
	{
		// devlog_changes.txt 2026-02-20: a singleAlly heal with no target, or an
		// NPC target, fizzles. It used to silently heal the caster instead, which
		// looked like the spell working and was the worst kind of bug.
		const bool bFizzle = Skill.TargetType == EValhallaSkillTargetType::SingleAlly
			&& (!Context.Target || UValhallaCombatLibrary::IsNpcTarget(Context.Target));

		if (!bFizzle)
		{
			AActor* HealTarget = (Context.Target && !UValhallaCombatLibrary::IsNpcTarget(Context.Target))
				? Context.Target.Get()
				: Caster;

			if (UValhallaCombatLibrary::IsAliveTarget(HealTarget))
			{
				const double ScaledHeal = RollBaseHealing(Skill) + GetScalingStat(Caster, Skill) * HealStatScaling;
				UValhallaCombatLibrary::ApplyHealing(Caster, HealTarget, FMath::RoundToDouble(ScaledHeal), Skill.Id);
			}

			// SkillEffectHandler.ts:154 — the HoT rides on the same target.
			if (Skill.HotHealPerSec > 0.f && Skill.BuffDurationMs > 0.f)
			{
				FValhallaActiveBuff Buff;
				Buff.SkillId = Skill.Id;
				Buff.Caster = Caster;
				Buff.AppliedAt = Context.ServerTime;
				Buff.LastTickAt = Context.ServerTime;
				Buff.ExpiresAt = Context.ServerTime + Skill.BuffDurationMs / 1000.0;
				Buff.HotHealPerSec = Skill.HotHealPerSec;

				ApplyBuffToTarget(HealTarget, Buff, Skill);
				BroadcastBuffApplied(Context, HealTarget, Skill.BuffDurationMs);
			}
		}
	}

	// ── Buff / debuff-only skills (SkillEffectHandler.ts:169) ────────────
	if (!Skill.bHasBaseDamage && !Skill.bHasBaseHealing && Skill.BuffDurationMs > 0.f)
	{
		AActor* BuffTarget = Caster;
		if (Skill.Category == EValhallaSkillCategory::Debuff)
		{
			BuffTarget = Context.Target ? Context.Target.Get() : Caster;
		}
		else if (Skill.TargetType != EValhallaSkillTargetType::Self)
		{
			BuffTarget = Context.Target ? Context.Target.Get() : Caster;
		}

		FValhallaActiveBuff Buff;
		Buff.SkillId = Skill.Id;
		Buff.Caster = Caster;
		Buff.AppliedAt = Context.ServerTime;
		Buff.LastTickAt = Context.ServerTime;
		Buff.ExpiresAt = Context.ServerTime + Skill.BuffDurationMs / 1000.0;
		Buff.DotDamagePerSec = Skill.DotDamagePerSec;
		Buff.HotHealPerSec = Skill.HotHealPerSec;

		ApplyBuffToTarget(BuffTarget, Buff, Skill);
		BroadcastBuffApplied(Context, BuffTarget, Skill.BuffDurationMs);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Fireball — fireballHandler.ts
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaFireballHandler::Execute(const FValhallaSkillContext& Context)
{
	UWorld* World = Context.World;
	if (!World || !Context.Caster)
	{
		return;
	}

	const FValhallaSkillTemplate& Skill = Context.Skill;

	// fireballHandler.ts:40 — rolled now, carried by the projectile. A fireball
	// already in the air is not affected by anything that happens to its caster.
	const double ScaledDamage = RollBaseDamage(Skill) + GetScalingStat(Context.Caster, Skill) * SkillStatScaling;

	const double ProjSpeed = Skill.bHasProjectile && Skill.ProjectileSpeed > 0.f
		? Skill.ProjectileSpeed
		: Valhalla::FireballProjectileSpeed;
	const double ProjRadius = Skill.bHasProjectile && Skill.ProjectileRadius > 0.f
		? Skill.ProjectileRadius
		: Valhalla::FireballProjectileRadius;
	const double BlastRadius = Skill.AoeRadius > 0.f ? Skill.AoeRadius : Valhalla::FireballAoeRadius;

	// fireballHandler.ts:49 — spawn clear of the caster's own capsule so the
	// projectile does not immediately detonate on the person who cast it.
	const FVector CasterLocation = Context.Caster->GetActorLocation();
	FVector ToTarget = Context.AimPoint - CasterLocation;
	ToTarget.Z = 0.0;

	const FVector Direction = ToTarget.IsNearlyZero()
		? Context.Caster->GetActorForwardVector()
		: ToTarget.GetSafeNormal();

	const double SpawnOffset = Valhalla::PlayerCollisionRadius + ProjRadius + 2.0;
	const FVector SpawnLocation = CasterLocation + Direction * SpawnOffset;

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Context.Caster;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AValhallaSpellProjectile* Projectile = World->SpawnActor<AValhallaSpellProjectile>(
		AValhallaSpellProjectile::StaticClass(), SpawnLocation, Direction.Rotation(), SpawnParams);

	if (!Projectile)
	{
		UE_LOG(LogValhallaGame, Warning, TEXT("Fireball: projectile failed to spawn at %s."), *SpawnLocation.ToCompactString());
		return;
	}

	// Aim at the caster's own height, not the raw ground point: the whole game is
	// played on one plane and a projectile that dips to the floor would clip it.
	FVector AimAtHeight = Context.AimPoint;
	AimAtHeight.Z = CasterLocation.Z;

	Projectile->InitializeProjectile(Context.Caster, Skill.Id, AimAtHeight, ProjSpeed, ProjRadius, ScaledDamage, BlastRadius);

	UE_LOG(LogValhallaCombat, Log, TEXT("%s cast %s: projectile spawned at %s -> %s speed=%.0f damage=%.1f aoe=%.0f"),
		*UValhallaCombatLibrary::GetDisplayName(Context.Caster), *Skill.Id.ToString(),
		*SpawnLocation.ToCompactString(), *AimAtHeight.ToCompactString(),
		ProjSpeed, ScaledDamage, BlastRadius);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Magic Missile — magicMissileHandler.ts
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaMagicMissileHandler::Execute(const FValhallaSkillContext& Context)
{
	AActor* Target = Context.Target;
	if (!Target || Target == Context.Caster || !UValhallaCombatLibrary::IsAliveTarget(Target))
	{
		return;
	}

	const FValhallaSkillTemplate& Skill = Context.Skill;
	const double Scaled = RollBaseDamage(Skill) + GetScalingStat(Context.Caster, Skill) * SkillStatScaling;

	double CritChance = 0.05;
	double CritDamage = 0.5;
	GetCritStats(Context.Caster, CritChance, CritDamage);

	// magicMissileHandler.ts:36 — the crit roll is the *only* roll. No hit roll,
	// no dodge roll: "always hits" is the spell, and routing it through
	// ApplyDamage would quietly give it a 15% failure rate.
	const bool bCrit = FMath::FRand() < CritChance;
	const double CritMultiplier = bCrit ? 1.0 + CritDamage : 1.0;
	const int32 FinalDamage = FMath::RoundToInt32(Scaled * CritMultiplier);

	ApplyUnavoidableDamage(Context.Caster, Target, FinalDamage, bCrit, Skill.Id, Context.ServerTime);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Taunt — tauntHandler.ts
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaTauntHandler::Execute(const FValhallaSkillContext& Context)
{
	AValhallaNPC* Npc = Cast<AValhallaNPC>(Context.Target);
	if (!Npc || !Npc->IsAlive())
	{
		return;
	}

	const FValhallaCombatant CasterInfo = UValhallaCombatLibrary::DescribeCombatant(Context.Caster);
	const float BonusThreat = BaseThreat + ThreatPerLevel * CasterInfo.Level;

	// bForceTarget: a taunt that only added threat would lose to a rogue who had
	// already out-damaged the tank, which is exactly the situation taunt exists
	// to fix. tauntHandler.ts/NPCSystem.tauntNpc:312 sets aggroTarget outright.
	Npc->AddThreat(Context.Caster, BonusThreat, /*bForceTarget=*/true);

	const float DurationMs = Context.Skill.BuffDurationMs > 0.f ? Context.Skill.BuffDurationMs : 6000.f;
	BroadcastBuffApplied(Context, Npc, DurationMs);

	UE_LOG(LogValhallaCombat, Log, TEXT("%s taunted %s for %.0f threat."),
		*CasterInfo.DisplayName, *UValhallaCombatLibrary::GetDisplayName(Npc), BonusThreat);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Shield of Faith — shieldOfFaithHandler.ts
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaShieldOfFaithHandler::Execute(const FValhallaSkillContext& Context)
{
	// shieldOfFaithHandler.ts:31 — an NPC target falls back to the caster, so a
	// cleric who fumbles the click still shields themselves rather than fizzling.
	AActor* ShieldTarget = (Context.Target && !UValhallaCombatLibrary::IsNpcTarget(Context.Target))
		? Context.Target.Get()
		: Context.Caster.Get();

	AValhallaCharacter* Character = Cast<AValhallaCharacter>(ShieldTarget);
	AValhallaPlayerState* PlayerState = Character ? Character->GetValhallaPlayerState() : nullptr;
	if (!PlayerState || !PlayerState->IsAlive())
	{
		return;
	}

	// The wisdom is the *caster's*, not the target's: a cleric's shield is as
	// strong on a warrior as on themselves.
	const double Wisdom = UValhallaCombatLibrary::GetStatValue(GetCasterStats(Context.Caster), TEXT("wisdom"));
	const int32 ShieldAmount = FMath::RoundToInt32(BaseShield + Wisdom * ShieldPerWisdom);

	// Refresh semantics: a second cast overwrites rather than adding.
	PlayerState->ShieldHp = static_cast<float>(ShieldAmount);

	const float DurationMs = Context.Skill.BuffDurationMs > 0.f ? Context.Skill.BuffDurationMs : DefaultDurationMs;

	FValhallaActiveBuff Buff;
	Buff.SkillId = Context.Skill.Id;
	Buff.Caster = Context.Caster;
	Buff.AppliedAt = Context.ServerTime;
	Buff.LastTickAt = Context.ServerTime;
	Buff.ExpiresAt = Context.ServerTime + DurationMs / 1000.0;

	ApplyBuffToTarget(ShieldTarget, Buff, Context.Skill);
	BroadcastBuffApplied(Context, ShieldTarget, DurationMs);

	UE_LOG(LogValhallaCombat, Log, TEXT("%s shielded %s for %d (wisdom %.0f), %.0f ms."),
		*UValhallaCombatLibrary::GetDisplayName(Context.Caster),
		*PlayerState->CharacterName, ShieldAmount, Wisdom, DurationMs);
}

void UValhallaShieldOfFaithHandler::OnBuffExpired(AActor* Target, const FValhallaActiveBuff& Buff)
{
	// shieldOfFaithHandler.ts:58 — without this the absorb outlives the buff and
	// never goes away, because nothing else in the game ever writes ShieldHp down.
	if (AValhallaCharacter* Character = Cast<AValhallaCharacter>(Target))
	{
		if (AValhallaPlayerState* PlayerState = Character->GetValhallaPlayerState())
		{
			PlayerState->ShieldHp = 0.f;
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Backstab — backstabHandler.ts
// ─────────────────────────────────────────────────────────────────────────────

bool UValhallaBackstabHandler::IsAttackingFromBehind(const AActor* Caster, const AActor* Target)
{
	if (!Caster || !Target)
	{
		return false;
	}

	// backstabHandler.ts:37 — the angle from the *target* to the attacker,
	// compared against the direction opposite the target's facing.
	const FVector ToAttacker = Caster->GetActorLocation() - Target->GetActorLocation();
	if (ToAttacker.IsNearlyZero())
	{
		return false;
	}

	const double AngleToAttacker = FMath::RadiansToDegrees(FMath::Atan2(ToAttacker.Y, ToAttacker.X));
	const double TargetBack = Target->GetActorRotation().Yaw + 180.0;

	return FMath::Abs(FRotator::NormalizeAxis(AngleToAttacker - TargetBack)) <= BehindHalfArcDegrees;
}

void UValhallaBackstabHandler::Execute(const FValhallaSkillContext& Context)
{
	AActor* Target = Context.Target;
	if (!Target || !UValhallaCombatLibrary::IsAliveTarget(Target))
	{
		return;
	}

	const FValhallaSkillTemplate& Skill = Context.Skill;
	const bool bBehind = IsAttackingFromBehind(Context.Caster, Target);

	const double Scaled = RollBaseDamage(Skill) + GetScalingStat(Context.Caster, Skill) * SkillStatScaling;

	double CritChance = 0.05;
	double CritDamage = 0.5;
	GetCritStats(Context.Caster, CritChance, CritDamage);

	// backstabHandler.ts:65 — both bonuses, and both only from behind.
	const double DamageMultiplier = bBehind ? BehindDamageMultiplier : 1.0;
	const double EffectiveCritChance = CritChance + (bBehind ? BehindCritBonus : 0.0);

	const bool bCrit = FMath::FRand() < EffectiveCritChance;
	const double CritMultiplier = bCrit ? 1.0 + CritDamage : 1.0;
	const int32 FinalDamage = FMath::RoundToInt32(Scaled * DamageMultiplier * CritMultiplier);

	UE_LOG(LogValhallaCombat, Log, TEXT("%s backstab on %s: behind=%s critChance=%.3f crit=%s damage=%d"),
		*UValhallaCombatLibrary::GetDisplayName(Context.Caster),
		*UValhallaCombatLibrary::GetDisplayName(Target),
		bBehind ? TEXT("yes") : TEXT("no"), EffectiveCritChance,
		bCrit ? TEXT("yes") : TEXT("no"), FinalDamage);

	// The crit is already rolled with the positional bonus applied, so the hit
	// is delivered directly rather than re-rolled by ApplyDamage.
	ApplyUnavoidableDamage(Context.Caster, Target, FinalDamage, bCrit, Skill.Id, Context.ServerTime);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Poison Blade — poisonBladeHandler.ts
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaPoisonBladeHandler::Execute(const FValhallaSkillContext& Context)
{
	AActor* Target = Context.Target;
	if (!Target || !UValhallaCombatLibrary::IsAliveTarget(Target))
	{
		return;
	}

	// poisonBladeHandler.ts:43 — hard-coded 'dexterity', not the template's
	// scalingStat. They happen to agree today; the 1.0 code is what is ported.
	const double Dex = UValhallaCombatLibrary::GetStatValue(GetCasterStats(Context.Caster), TEXT("dexterity"));
	const float DotDps = static_cast<float>(FMath::RoundToDouble(BaseDotDps + Dex * DexScale));

	FValhallaActiveBuff Buff;
	Buff.SkillId = Context.Skill.Id;
	Buff.Caster = Context.Caster;
	Buff.AppliedAt = Context.ServerTime;
	Buff.LastTickAt = Context.ServerTime;
	Buff.ExpiresAt = Context.ServerTime + PoisonDurationMs / 1000.0;
	Buff.DotDamagePerSec = DotDps;

	// The same buff on either kind of target. 1.0 needed two code paths because
	// its player and NPC buff lists were different types; here ApplyBuffToTarget
	// hides that, and both are ticked once a second by their owner.
	ApplyBuffToTarget(Target, Buff, Context.Skill);
	BroadcastBuffApplied(Context, Target, PoisonDurationMs);

	UE_LOG(LogValhallaCombat, Log, TEXT("%s poisoned %s for %.0f dps over %.0f ms."),
		*UValhallaCombatLibrary::GetDisplayName(Context.Caster),
		*UValhallaCombatLibrary::GetDisplayName(Target), DotDps, PoisonDurationMs);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Registry — handlers/registry.ts + handlers/index.ts
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaSkillHandlerRegistry::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	DefaultHandler = NewObject<UValhallaDefaultSkillHandler>(this, TEXT("DefaultSkillHandler"));
	RegisterBuiltInHandlers();

	UE_LOG(LogValhallaGame, Log, TEXT("Skill handler registry ready: %d custom handlers + the default."), Handlers.Num());
}

void UValhallaSkillHandlerRegistry::Deinitialize()
{
	Handlers.Reset();
	DefaultHandler = nullptr;
	Super::Deinitialize();
}

void UValhallaSkillHandlerRegistry::RegisterBuiltInHandlers()
{
	RegisterHandler(SkillFireball, UValhallaFireballHandler::StaticClass());
	RegisterHandler(SkillMagicMissile, UValhallaMagicMissileHandler::StaticClass());
	RegisterHandler(SkillTaunt, UValhallaTauntHandler::StaticClass());
	RegisterHandler(SkillShieldOfFaith, UValhallaShieldOfFaithHandler::StaticClass());
	RegisterHandler(SkillBackstab, UValhallaBackstabHandler::StaticClass());
	RegisterHandler(SkillPoisonBlade, UValhallaPoisonBladeHandler::StaticClass());
}

void UValhallaSkillHandlerRegistry::RegisterHandler(FName SkillId, TSubclassOf<UValhallaSkillHandler> HandlerClass)
{
	if (SkillId.IsNone() || !HandlerClass)
	{
		return;
	}

	Handlers.Add(SkillId, NewObject<UValhallaSkillHandler>(this, HandlerClass));
}

UValhallaSkillHandler* UValhallaSkillHandlerRegistry::FindHandler(FName SkillId) const
{
	const TObjectPtr<UValhallaSkillHandler>* Found = Handlers.Find(SkillId);
	return Found ? Found->Get() : nullptr;
}

void UValhallaSkillHandlerRegistry::Execute(const FValhallaSkillContext& Context)
{
	// SkillEffectHandler.ts:74 — custom handler first, default second. There is
	// no "both": a skill with a handler has opted out of the data-driven path
	// entirely, which is why fireball does not also deal its baseDamage on cast.
	UValhallaSkillHandler* Handler = FindHandler(Context.Skill.Id);
	if (!Handler)
	{
		Handler = DefaultHandler;
	}

	if (Handler)
	{
		Handler->Execute(Context);
	}
}

void UValhallaSkillHandlerRegistry::RunBuffCleanup(AActor* Target, const FValhallaActiveBuff& Buff)
{
	// SkillSystem.ts:931 — only handlers that registered a cleanup get one, and
	// the default handler's is a no-op, so this is safe to call for every buff.
	if (UValhallaSkillHandler* Handler = FindHandler(Buff.SkillId))
	{
		Handler->OnBuffExpired(Target, Buff);
	}
}
