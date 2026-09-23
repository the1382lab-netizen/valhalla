// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaCombatLibrary.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "ValhallaCharacter.h"
#include "ValhallaConstants.h"
#include "ValhallaGame.h"
#include "ValhallaGameMode.h"
#include "ValhallaGameState.h"
#include "ValhallaNPC.h"
#include "ValhallaPlayerState.h"
#include "ValhallaStats.h"

DEFINE_LOG_CATEGORY(LogValhallaCombat);

namespace
{
	/**
	 * Per-target invulnerability, in server-time seconds.
	 *
	 * 1.0 kept `invulnerableUntil` on the player schema. NPCs did not have one at
	 * all, which is why a fireball's AoE could hit the same NPC twice in a frame.
	 * Keeping it in one side table here applies the same window to both, and
	 * keeps a purely server-side anti-cheat number off the replicated state.
	 *
	 * Entries for dead actors are cheap and are cleaned up on death.
	 */
	TMap<TWeakObjectPtr<const AActor>, double> GInvulnerableUntil;

	/** True while the target is inside its i-frame window. */
	bool IsInvulnerable(const AActor* Actor, double Now)
	{
		const double* Until = GInvulnerableUntil.Find(Actor);
		return Until && Now < *Until;
	}

	/** Start the window. CombatSystem.ts:396 — every landed hit grants it. */
	void GrantInvulnerability(const AActor* Actor, double Now, double Milliseconds)
	{
		GInvulnerableUntil.Add(Actor, Now + Milliseconds / 1000.0);
	}
}

double UValhallaCombatLibrary::GetServerTime(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const AValhallaGameState* GameState = World ? World->GetGameState<AValhallaGameState>() : nullptr;
	return GameState ? GameState->GetServerTime() : 0.0;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Reading a combatant
// ─────────────────────────────────────────────────────────────────────────────

bool UValhallaCombatLibrary::IsNpcTarget(const AActor* Actor)
{
	return Actor && Actor->IsA<AValhallaNPC>();
}

bool UValhallaCombatLibrary::IsAliveTarget(const AActor* Actor)
{
	if (const AValhallaNPC* Npc = Cast<AValhallaNPC>(Actor))
	{
		return Npc->IsAlive();
	}
	if (const AValhallaCharacter* Character = Cast<AValhallaCharacter>(Actor))
	{
		return Character->IsAlive();
	}
	return false;
}

bool UValhallaCombatLibrary::IsFacingPoint(const FVector& ActorLocation, float ActorYawDegrees, const FVector& TargetLocation, float HalfAngleDeg)
{
	const FVector2D ToTarget(TargetLocation.X - ActorLocation.X, TargetLocation.Y - ActorLocation.Y);

	// Standing on top of each other: there is no direction to face, and
	// refusing would be a failure the player can do nothing about.
	if (ToTarget.SizeSquared() < 1.0)
	{
		return true;
	}

	const double AngleToTarget = FMath::RadiansToDegrees(FMath::Atan2(ToTarget.Y, ToTarget.X));
	const double Off = FMath::Abs(FRotator::NormalizeAxis(static_cast<float>(AngleToTarget - ActorYawDegrees)));
	return Off <= HalfAngleDeg;
}

bool UValhallaCombatLibrary::IsFacing(const AActor* Actor, const AActor* Target, float HalfAngleDeg)
{
	if (!Actor || !Target)
	{
		return false;
	}
	if (Actor == Target)
	{
		return true;
	}
	return IsFacingPoint(Actor->GetActorLocation(), static_cast<float>(Actor->GetActorRotation().Yaw),
		Target->GetActorLocation(), HalfAngleDeg);
}

bool UValhallaCombatLibrary::AreHostile(const AActor* A, const AActor* B)
{
	if (!A || !B || A == B)
	{
		return false;
	}

	// A friendly NPC (template `type: "npc"` — a merchant, a townsperson) is
	// nobody's enemy: it cannot be attacked and does not attack.
	const AValhallaNPC* NpcA = Cast<AValhallaNPC>(A);
	const AValhallaNPC* NpcB = Cast<AValhallaNPC>(B);
	if ((NpcA && NpcA->bFriendly) || (NpcB && NpcB->bFriendly))
	{
		return false;
	}

	// No PvP and no factions. Players fight NPCs and nothing else.
	return IsNpcTarget(A) != IsNpcTarget(B);
}

FString UValhallaCombatLibrary::GetDisplayName(const AActor* Actor)
{
	if (const AValhallaNPC* Npc = Cast<AValhallaNPC>(Actor))
	{
		return Npc->DisplayName.IsEmpty() ? Npc->GetName() : Npc->DisplayName;
	}
	if (const AValhallaCharacter* Character = Cast<AValhallaCharacter>(Actor))
	{
		if (const AValhallaPlayerState* PlayerState = Character->GetValhallaPlayerState())
		{
			return PlayerState->CharacterName;
		}
	}
	return Actor ? Actor->GetName() : TEXT("<none>");
}

double UValhallaCombatLibrary::GetStatValue(const FValhallaResolvedStats& Stats, FName StatName)
{
	// utils.ts got this for free with `(player.stats as any)[statName]`. C++ has
	// to spell the mapping out, which is no worse: the set of scaling stats is
	// closed, and a typo in skills.json now warns instead of silently scaling by
	// undefined (which JavaScript turned into NaN and then into 0 damage).
	static const TMap<FName, float FValhallaStatBlock::*> StatFields = {
		{ TEXT("hp"),				&FValhallaStatBlock::Hp },
		{ TEXT("mana"),				&FValhallaStatBlock::Mana },
		{ TEXT("strength"),			&FValhallaStatBlock::Strength },
		{ TEXT("stamina"),			&FValhallaStatBlock::Stamina },
		{ TEXT("dexterity"),		&FValhallaStatBlock::Dexterity },
		{ TEXT("intelligence"),		&FValhallaStatBlock::Intelligence },
		{ TEXT("wisdom"),			&FValhallaStatBlock::Wisdom },
		{ TEXT("physicalResist"),	&FValhallaStatBlock::PhysicalResist },
		{ TEXT("spellResist"),		&FValhallaStatBlock::SpellResist },
		{ TEXT("critChance"),		&FValhallaStatBlock::CritChance },
		{ TEXT("critDamage"),		&FValhallaStatBlock::CritDamage },
		{ TEXT("physicalDefense"),	&FValhallaStatBlock::PhysicalDefense },
		{ TEXT("blockRating"),		&FValhallaStatBlock::BlockRating },
		{ TEXT("dodgeRating"),		&FValhallaStatBlock::DodgeRating },
	};

	if (StatName.IsNone())
	{
		return 0.0;
	}

	if (const float FValhallaStatBlock::* const* Field = StatFields.Find(StatName))
	{
		return static_cast<double>(Stats.**Field);
	}

	UE_LOG(LogValhallaCombat, Warning, TEXT("Unknown scalingStat '%s' — scaling as 0."), *StatName.ToString());
	return 0.0;
}

FValhallaCombatant UValhallaCombatLibrary::DescribeCombatant(const AActor* Actor)
{
	FValhallaCombatant Out;
	if (!Actor)
	{
		return Out;
	}

	if (const AValhallaNPC* Npc = Cast<AValhallaNPC>(Actor))
	{
		const FValhallaNPCTemplate& Template = Npc->GetTemplate();

		Out.bValid = true;
		Out.bIsPlayer = false;
		Out.bAlive = Npc->IsAlive();
		Out.Hp = Npc->Hp;
		Out.MaxHp = Npc->MaxHp;
		Out.Level = Npc->Level;
		Out.DisplayName = GetDisplayName(Actor);

		// npc-templates.json ships `stats: {}` for all three placeholder
		// templates, so every rating below is 0 and an NPC mitigates nothing.
		// That is faithful: 1.0's damageNPC subtracted the raw number and never
		// looked at a stat. The difference in 2.0 is that the *attacker's* hit
		// roll now applies to NPCs too, so swings at an NPC can miss — which is
		// what makes the `missed` event and the logged rolls mean anything
		// outside of PvP. Authoring real stats on a template will just work.
		Out.Strength = Template.Stats.Strength;
		Out.Dexterity = Template.Stats.Dexterity;
		Out.Intelligence = Template.Stats.Intelligence;
		Out.Wisdom = Template.Stats.Wisdom;
		Out.CritChance = Template.Stats.CritChance;
		Out.CritDamage = Template.Stats.CritDamage;
		Out.DodgeRating = Template.Stats.DodgeRating;
		Out.BlockRating = Template.Stats.BlockRating;
		Out.PhysicalDefense = Template.Stats.PhysicalDefense;
		Out.SpellResist = Template.Stats.SpellResist;
		Out.ShieldHp = 0.0;
		return Out;
	}

	if (const AValhallaCharacter* Character = Cast<AValhallaCharacter>(Actor))
	{
		const AValhallaPlayerState* PlayerState = Character->GetValhallaPlayerState();
		if (!PlayerState)
		{
			return Out;
		}

		const FValhallaResolvedStats& Stats = PlayerState->GetStats();

		Out.bValid = true;
		Out.bIsPlayer = true;
		Out.bAlive = PlayerState->IsAlive();
		Out.Hp = PlayerState->Hp;
		Out.MaxHp = PlayerState->MaxHp;
		Out.Level = PlayerState->Level;
		Out.DisplayName = PlayerState->CharacterName;

		Out.Strength = Stats.Strength;
		Out.Dexterity = Stats.Dexterity;
		Out.Intelligence = Stats.Intelligence;
		Out.Wisdom = Stats.Wisdom;
		Out.CritChance = Stats.CritChance;
		Out.CritDamage = Stats.CritDamage;
		Out.DodgeRating = Stats.DodgeRating;
		Out.BlockRating = Stats.BlockRating;
		Out.PhysicalDefense = Stats.PhysicalDefense;
		Out.SpellResist = Stats.SpellResist;
		Out.ShieldHp = PlayerState->ShieldHp;
		return Out;
	}

	return Out;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Events
// ─────────────────────────────────────────────────────────────────────────────

FValhallaCombatEvent UValhallaCombatLibrary::MakeHitEvent(AActor* Attacker, AActor* Target, const FValhallaDamageResult& Result, FName SkillId)
{
	FValhallaCombatEvent Event;
	Event.Target = Target;
	Event.Instigator = Attacker;
	Event.SkillId = SkillId;
	Event.Amount = static_cast<float>(Result.Damage);
	Event.bCrit = Result.bCrit;
	Event.bBlocked = Result.bBlocked;
	Event.Location = Target ? Target->GetActorLocation() : FVector::ZeroVector;

	switch (Result.Outcome)
	{
	case EValhallaDamageOutcome::Miss:
		Event.Kind = EValhallaCombatEventKind::Missed;
		break;
	case EValhallaDamageOutcome::Dodge:
		Event.Kind = EValhallaCombatEventKind::Dodged;
		break;
	default:
		Event.Kind = IsNpcTarget(Target) ? EValhallaCombatEventKind::NpcHit : EValhallaCombatEventKind::PlayerHit;
		break;
	}

	const FValhallaCombatant TargetInfo = DescribeCombatant(Target);
	Event.RemainingHp = static_cast<float>(TargetInfo.Hp);
	return Event;
}

void UValhallaCombatLibrary::BroadcastCombatEvent(const UObject* WorldContext, const FValhallaCombatEvent& Event)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (AValhallaGameState* GameState = World ? World->GetGameState<AValhallaGameState>() : nullptr)
	{
		GameState->QueueCombatEvent(Event);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  The damage path
// ─────────────────────────────────────────────────────────────────────────────

int32 UValhallaCombatLibrary::ApplyDamage(AActor* Attacker, AActor* Target, double RawDamage, bool bMagical, FName SourceSkillId)
{
	if (!Target || !Target->HasAuthority())
	{
		// Every write to HP is the server's. A client calling this is either a
		// bug or an attack, and in both cases the right answer is nothing.
		return 0;
	}

	const FValhallaCombatant TargetInfo = DescribeCombatant(Target);
	if (!TargetInfo.bValid || !TargetInfo.bAlive)
	{
		return 0;
	}

	const double Now = GetServerTime(Target);

	// CombatSystem.ts:343 — the i-frame gate is before the rolls, so a hit that
	// lands inside the window is not merely harmless, it never happened.
	if (IsInvulnerable(Target, Now))
	{
		return 0;
	}

	const FValhallaCombatant AttackerInfo = DescribeCombatant(Attacker);

	FValhallaDamageInput Input;
	Input.RawDamage = RawDamage;
	Input.bIsMagical = bMagical;
	Input.AttackerDexterity = AttackerInfo.Dexterity;
	Input.AttackerCritChance = AttackerInfo.CritChance;
	Input.AttackerCritDamage = AttackerInfo.CritDamage;
	Input.DefenderDodgeRating = TargetInfo.DodgeRating;
	Input.DefenderBlockRating = TargetInfo.BlockRating;
	Input.DefenderPhysicalDefense = TargetInfo.PhysicalDefense;
	Input.DefenderSpellResist = TargetInfo.SpellResist;
	Input.DefenderShieldHp = TargetInfo.ShieldHp;

	// The four draws, made here and nowhere else, and written to the log before
	// they are used. This is the point of the whole FValhallaDamageRolls design:
	// a contested hit can be replayed exactly by feeding these four numbers back
	// into ResolveDamage in a test, with no server and no network.
	FValhallaDamageRolls Rolls;
	Rolls.Hit = FMath::FRand();
	Rolls.Dodge = FMath::FRand();
	Rolls.Crit = FMath::FRand();
	Rolls.Block = FMath::FRand();

	const FValhallaDamageResult Result = Valhalla::Stats::ResolveDamage(Input, Rolls);

	UE_LOG(LogValhallaCombat, Log,
		TEXT("rolls hit=%.4f dodge=%.4f crit=%.4f block=%.4f -> %s damage=%d  %s -> %s  skill=%s raw=%.1f %s%s"),
		Rolls.Hit, Rolls.Dodge, Rolls.Crit, Rolls.Block,
		*Valhalla::DamageOutcomeToString(Result.Outcome), Result.Damage,
		*AttackerInfo.DisplayName, *TargetInfo.DisplayName,
		*SourceSkillId.ToString(), RawDamage,
		bMagical ? TEXT("magical") : TEXT("physical"),
		Result.bCrit ? TEXT(" CRIT") : (Result.bBlocked ? TEXT(" BLOCKED") : TEXT("")));

	// ── Miss and dodge: tell the client, take nothing off ────────────────
	if (Result.Outcome != EValhallaDamageOutcome::Hit)
	{
		BroadcastCombatEvent(Target, MakeHitEvent(Attacker, Target, Result, SourceSkillId));
		return 0;
	}

	// ── A block is its own event *and* part of the hit, as in 1.0 ────────
	if (Result.bBlocked)
	{
		FValhallaCombatEvent BlockEvent;
		BlockEvent.Kind = EValhallaCombatEventKind::Blocked;
		BlockEvent.Target = Target;
		BlockEvent.Instigator = Attacker;
		BlockEvent.SkillId = SourceSkillId;
		BlockEvent.Location = Target->GetActorLocation();
		BroadcastCombatEvent(Target, BlockEvent);
	}

	GrantInvulnerability(Target, Now, Valhalla::InvulnerabilityMs);

	bool bDied = false;

	if (AValhallaNPC* Npc = Cast<AValhallaNPC>(Target))
	{
		// The NPC owns its own HP, its threat table and its respawn timer, so the
		// write goes through it rather than around it.
		Npc->ApplyDamageFromAttacker(Attacker, Result.Damage, Now, bDied);
	}
	else if (AValhallaCharacter* Character = Cast<AValhallaCharacter>(Target))
	{
		if (AValhallaPlayerState* PlayerState = Character->GetValhallaPlayerState())
		{
			// PlayerState.ts:162 — the shield is spent first and what is left of
			// it comes back on the result, so the HUD's shield bar is right on
			// the same frame the damage lands.
			PlayerState->ShieldHp = static_cast<float>(Result.RemainingShieldHp);
			// Admin god mode: the hit is still announced, the HP does not move.
			if (!PlayerState->bAdminGodMode)
			{
				PlayerState->Hp = FMath::Max(0.f, PlayerState->Hp - static_cast<float>(Result.Damage));
			}

			if (PlayerState->Hp <= 0.f)
			{
				bDied = true;
			}
		}
	}

	BroadcastCombatEvent(Target, MakeHitEvent(Attacker, Target, Result, SourceSkillId));

	// ── Death ────────────────────────────────────────────────────────────
	if (bDied && !IsNpcTarget(Target))
	{
		if (AValhallaGameMode* GameMode = Target->GetWorld() ? Target->GetWorld()->GetAuthGameMode<AValhallaGameMode>() : nullptr)
		{
			GameMode->HandlePlayerDeath(Cast<AValhallaCharacter>(Target), Attacker);
		}
	}

	return Result.Damage;
}

int32 UValhallaCombatLibrary::ApplyHealing(AActor* Healer, AActor* Target, double Amount, FName SourceSkillId)
{
	if (!Target || !Target->HasAuthority() || Amount <= 0.0)
	{
		return 0;
	}

	const FValhallaCombatant TargetInfo = DescribeCombatant(Target);
	if (!TargetInfo.bValid || !TargetInfo.bAlive)
	{
		return 0;
	}

	const int32 Healed = FMath::Max(0, FMath::RoundToInt32(FMath::Min(Amount, TargetInfo.MaxHp - TargetInfo.Hp)));
	if (Healed <= 0)
	{
		return 0;
	}

	if (AValhallaNPC* Npc = Cast<AValhallaNPC>(Target))
	{
		Npc->Hp = FMath::Min(Npc->MaxHp, Npc->Hp + Healed);
	}
	else if (AValhallaCharacter* Character = Cast<AValhallaCharacter>(Target))
	{
		if (AValhallaPlayerState* PlayerState = Character->GetValhallaPlayerState())
		{
			PlayerState->Hp = FMath::Min(PlayerState->MaxHp, PlayerState->Hp + Healed);
		}
	}

	FValhallaCombatEvent Event;
	Event.Kind = EValhallaCombatEventKind::SkillEffect;
	Event.Target = Target;
	Event.Instigator = Healer;
	Event.SkillId = SourceSkillId;
	Event.Amount = static_cast<float>(Healed);
	Event.bHeal = true;
	Event.RemainingHp = static_cast<float>(DescribeCombatant(Target).Hp);
	Event.Location = Target->GetActorLocation();
	BroadcastCombatEvent(Target, Event);

	UE_LOG(LogValhallaCombat, Log, TEXT("heal %d  %s -> %s  skill=%s"),
		Healed, *GetDisplayName(Healer), *TargetInfo.DisplayName, *SourceSkillId.ToString());

	return Healed;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Pure rules
// ─────────────────────────────────────────────────────────────────────────────

float UValhallaCombatLibrary::ComputeFalloff(float Distance, float Radius)
{
	// SpellProjectileSystem.ts:340. `t` is clamped at 1 so a target sitting
	// exactly on the edge takes the floor rather than something negative, and
	// callers that pass a distance past the radius get the floor too instead of
	// a value that would heal.
	if (Radius <= 0.f)
	{
		return 1.f;
	}

	const float T = FMath::Min(Distance / Radius, 1.f);
	return 1.f - T * (1.f - static_cast<float>(Valhalla::FireballDamageFalloffMin));
}

void UValhallaCombatLibrary::ApplyCooldown(
	TMap<FName, double>& Cooldowns,
	const FValhallaSkillTemplate& Skill,
	const TArray<FName>& ClassSkills,
	TFunctionRef<const FValhallaSkillTemplate*(FName)> FindSkill,
	double Now)
{
	// SkillSystem.ts:890 — a zero cooldown is not a zero-length cooldown, it is
	// no entry at all. Writing `Now` would make the skill briefly un-castable on
	// a client whose clock ran a hair behind the server's.
	if (Skill.CooldownMs <= 0.f)
	{
		return;
	}

	const double ExpiresAt = Now + Skill.CooldownMs / 1000.0;
	Cooldowns.Add(Skill.Id, ExpiresAt);

	if (Skill.CooldownGroup.IsNone())
	{
		return;
	}

	// SkillSystem.ts:896 — every other skill in the group takes *this* skill's
	// expiry, not its own cooldown. Casting the short skill in a group therefore
	// only locks the long one out briefly, which is how the 1.0 designers
	// expected it to behave and is easy to get backwards.
	for (const FName& OtherId : ClassSkills)
	{
		if (OtherId == Skill.Id)
		{
			continue;
		}

		const FValhallaSkillTemplate* Other = FindSkill(OtherId);
		if (Other && Other->CooldownGroup == Skill.CooldownGroup)
		{
			Cooldowns.Add(OtherId, ExpiresAt);
		}
	}
}

double UValhallaCombatLibrary::GetCooldownRemaining(
	const TMap<FName, double>& Cooldowns,
	const FValhallaSkillTemplate& Skill,
	const TArray<FName>& ClassSkills,
	TFunctionRef<const FValhallaSkillTemplate*(FName)> FindSkill,
	double Now,
	FName& OutBlockingSkill)
{
	OutBlockingSkill = NAME_None;

	if (const double* OwnExpiry = Cooldowns.Find(Skill.Id))
	{
		if (Now < *OwnExpiry)
		{
			OutBlockingSkill = Skill.Id;
			return *OwnExpiry - Now;
		}
	}

	if (Skill.CooldownGroup.IsNone())
	{
		return 0.0;
	}

	// SkillSystem.ts:722 — the group is checked separately from the skill's own
	// entry, because ApplyCooldown may have written the group member's expiry
	// without this skill ever having been cast.
	double Worst = 0.0;
	for (const FName& OtherId : ClassSkills)
	{
		if (OtherId == Skill.Id)
		{
			continue;
		}

		const FValhallaSkillTemplate* Other = FindSkill(OtherId);
		if (!Other || Other->CooldownGroup != Skill.CooldownGroup)
		{
			continue;
		}

		const double* OtherExpiry = Cooldowns.Find(OtherId);
		if (OtherExpiry && Now < *OtherExpiry && (*OtherExpiry - Now) > Worst)
		{
			Worst = *OtherExpiry - Now;
			OutBlockingSkill = OtherId;
		}
	}

	return Worst;
}

FValhallaActiveBuff* UValhallaCombatLibrary::FindBuff(TArray<FValhallaActiveBuff>& Buffs, FName SkillId, const AActor* Caster)
{
	return Buffs.FindByPredicate([SkillId, Caster](const FValhallaActiveBuff& Buff)
	{
		return Buff.SkillId == SkillId && Buff.Caster.Get() == Caster;
	});
}

void UValhallaCombatLibrary::ApplyBuff(
	TArray<FValhallaActiveBuff>& Buffs,
	const FValhallaActiveBuff& NewBuff,
	EValhallaStackingMode StackingMode,
	int32 MaxStacks)
{
	FValhallaActiveBuff* Existing = FindBuff(Buffs, NewBuff.SkillId, NewBuff.Caster.Get());

	if (!Existing)
	{
		// SkillEffectHandler.ts:262 — a first application is a push whatever the
		// mode is. Mode only decides what happens on the *second* one.
		Buffs.Add(NewBuff);
		Buffs.Last().Stacks = FMath::Max(1, NewBuff.Stacks);
		return;
	}

	switch (StackingMode)
	{
	case EValhallaStackingMode::Stack:
	{
		// SkillEffectHandler.ts:238 — one stack per application, clamped, and the
		// duration is refreshed as well. The per-tick damage is *not* multiplied
		// here: tickBuffs multiplies by Stacks when it fires, so a stack that
		// expires stops doing its share immediately.
		const int32 Cap = FMath::Max(1, MaxStacks);
		Existing->Stacks = FMath::Min(Existing->Stacks + 1, Cap);
		Existing->AppliedAt = NewBuff.AppliedAt;
		Existing->ExpiresAt = NewBuff.ExpiresAt;
		if (NewBuff.DotDamagePerSec > 0.f)
		{
			Existing->DotDamagePerSec = NewBuff.DotDamagePerSec;
		}
		if (NewBuff.HotHealPerSec > 0.f)
		{
			Existing->HotHealPerSec = NewBuff.HotHealPerSec;
		}
		return;
	}

	case EValhallaStackingMode::Extend:
	{
		// SkillEffectHandler.ts:247 — remaining + new, not max(remaining, new).
		// Re-applying an extend buff banks duration, which is the whole point of
		// the mode and the thing a reimplementation gets wrong.
		const double RemainingSeconds = FMath::Max(0.0, Existing->ExpiresAt - NewBuff.AppliedAt);
		const double ExtensionSeconds = NewBuff.ExpiresAt - NewBuff.AppliedAt;
		Existing->ExpiresAt = NewBuff.AppliedAt + RemainingSeconds + ExtensionSeconds;
		return;
	}

	case EValhallaStackingMode::Replace:
	default:
	{
		// SkillEffectHandler.ts:253 — drop the old entry entirely and push the
		// new one, so the tick clock restarts rather than carrying over.
		Buffs.RemoveAll([&NewBuff](const FValhallaActiveBuff& Buff)
		{
			return Buff.SkillId == NewBuff.SkillId && Buff.Caster.Get() == NewBuff.Caster.Get();
		});
		Buffs.Add(NewBuff);
		Buffs.Last().Stacks = FMath::Max(1, NewBuff.Stacks);
		return;
	}
	}
}
