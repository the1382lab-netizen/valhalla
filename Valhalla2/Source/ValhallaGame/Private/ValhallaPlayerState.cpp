// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaPlayerState.h"

#include "Net/UnrealNetwork.h"
#include "ValhallaCharacter.h"
#include "ValhallaConstants.h"
#include "ValhallaDataSubsystem.h"
#include "ValhallaGame.h"
#include "ValhallaInventoryLibrary.h"
#include "ValhallaSkillComponent.h"
#include "ValhallaStats.h"

AValhallaPlayerState::AValhallaPlayerState()
{
	// The 1.0 server pushed the whole schema at 20 Hz. The vitals below are the
	// only things a HUD reads, so the default 100 ms NetUpdateFrequency is plenty
	// and leaves the bandwidth for character movement.
	SetNetUpdateFrequency(20.f);
}

void AValhallaPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AValhallaPlayerState, CharacterName);
	DOREPLIFETIME(AValhallaPlayerState, ClassId);
	DOREPLIFETIME(AValhallaPlayerState, Level);
	DOREPLIFETIME(AValhallaPlayerState, Xp);
	DOREPLIFETIME(AValhallaPlayerState, ZoneId);

	DOREPLIFETIME(AValhallaPlayerState, Hp);
	DOREPLIFETIME(AValhallaPlayerState, MaxHp);
	DOREPLIFETIME(AValhallaPlayerState, Mana);
	DOREPLIFETIME(AValhallaPlayerState, MaxMana);
	DOREPLIFETIME(AValhallaPlayerState, Energy);
	DOREPLIFETIME(AValhallaPlayerState, MaxEnergy);
	DOREPLIFETIME(AValhallaPlayerState, ShieldHp);
	DOREPLIFETIME(AValhallaPlayerState, bAlive);

	DOREPLIFETIME(AValhallaPlayerState, VisionRange);

	// Equipment is public — Phase 4 draws it on everybody's paperdoll.
	DOREPLIFETIME(AValhallaPlayerState, EquipWeapon);
	DOREPLIFETIME(AValhallaPlayerState, EquipOffhand);
	DOREPLIFETIME(AValhallaPlayerState, EquipHelm);
	DOREPLIFETIME(AValhallaPlayerState, EquipChest);
	DOREPLIFETIME(AValhallaPlayerState, EquipLegs);
	DOREPLIFETIME(AValhallaPlayerState, EquipBoots);
	DOREPLIFETIME(AValhallaPlayerState, EquipGloves);
	DOREPLIFETIME(AValhallaPlayerState, EquipBack);
	DOREPLIFETIME(AValhallaPlayerState, EquipRing);

	// Owner only: see each property's comment.
	DOREPLIFETIME_CONDITION(AValhallaPlayerState, ReplicatedTargetActor, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AValhallaPlayerState, Inventory, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AValhallaPlayerState, PartyId, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AValhallaPlayerState, PartyMemberNames, COND_OwnerOnly);
	DOREPLIFETIME(AValhallaPlayerState, bAdminFrozen);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Equipment — PlayerState.ts:179-209
// ─────────────────────────────────────────────────────────────────────────────

FName AValhallaPlayerState::GetEquipped(EValhallaEquipSlot Slot) const
{
	switch (Slot)
	{
	case EValhallaEquipSlot::Weapon:	return EquipWeapon;
	case EValhallaEquipSlot::Offhand:	return EquipOffhand;
	case EValhallaEquipSlot::Helm:		return EquipHelm;
	case EValhallaEquipSlot::Chest:		return EquipChest;
	case EValhallaEquipSlot::Legs:		return EquipLegs;
	case EValhallaEquipSlot::Boots:		return EquipBoots;
	case EValhallaEquipSlot::Gloves:	return EquipGloves;
	case EValhallaEquipSlot::Back:		return EquipBack;
	case EValhallaEquipSlot::Ring:		return EquipRing;
	default:							return NAME_None;
	}
}

void AValhallaPlayerState::SetEquipped(EValhallaEquipSlot Slot, FName ItemId)
{
	if (!HasAuthority())
	{
		return;
	}

	switch (Slot)
	{
	case EValhallaEquipSlot::Weapon:	EquipWeapon = ItemId; break;
	case EValhallaEquipSlot::Offhand:	EquipOffhand = ItemId; break;
	case EValhallaEquipSlot::Helm:		EquipHelm = ItemId; break;
	case EValhallaEquipSlot::Chest:		EquipChest = ItemId; break;
	case EValhallaEquipSlot::Legs:		EquipLegs = ItemId; break;
	case EValhallaEquipSlot::Boots:		EquipBoots = ItemId; break;
	case EValhallaEquipSlot::Gloves:	EquipGloves = ItemId; break;
	case EValhallaEquipSlot::Back:		EquipBack = ItemId; break;
	case EValhallaEquipSlot::Ring:		EquipRing = ItemId; break;
	default: break;
	}

	// The authority never fires its own RepNotify, so the listen-server host
	// would be the one player whose gear never appeared. Same call, same
	// idempotence check inside it.
	OnRep_Equipment();
}

TArray<FName> AValhallaPlayerState::GatherEquipment() const
{
	TArray<FName> Equipment;
	Equipment.SetNum(ValhallaEquipSlotCount);

	for (int32 Index = 0; Index < ValhallaEquipSlotCount; ++Index)
	{
		Equipment[Index] = GetEquipped(ValhallaEquipSlotFromIndex(Index));
	}

	return Equipment;
}

void AValhallaPlayerState::ApplyEquipment(const TArray<FName>& Equipment)
{
	if (!HasAuthority())
	{
		return;
	}

	// A short array clears the slots it does not reach, so passing an empty one
	// is how everything is unequipped at once.
	for (int32 Index = 0; Index < ValhallaEquipSlotCount; ++Index)
	{
		const FName ItemId = Equipment.IsValidIndex(Index) ? Equipment[Index] : NAME_None;
		SetEquipped(ValhallaEquipSlotFromIndex(Index), ItemId);
	}
}

FString AValhallaPlayerState::DescribeEquipment() const
{
	FString Result;

	for (int32 Index = 0; Index < ValhallaEquipSlotCount; ++Index)
	{
		const EValhallaEquipSlot Slot = ValhallaEquipSlotFromIndex(Index);
		const FName ItemId = GetEquipped(Slot);
		if (ItemId.IsNone())
		{
			continue;
		}

		Result += FString::Printf(TEXT("%s=%s "),
			*UValhallaInventoryLibrary::EquipSlotToName(Slot).ToString(), *ItemId.ToString());
	}

	return Result.IsEmpty() ? TEXT("<nothing equipped>") : Result.TrimEnd();
}

const FValhallaItemTemplate* AValhallaPlayerState::GetEquippedWeapon() const
{
	if (EquipWeapon.IsNone())
	{
		return nullptr;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	const UValhallaDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
	return Data ? Data->FindItem(EquipWeapon) : nullptr;
}

void AValhallaPlayerState::GrantStartingItems(const FValhallaClassTemplate& ClassTemplate)
{
	if (!HasAuthority())
	{
		return;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	const UValhallaDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
	if (!Data)
	{
		return;
	}

	auto FindItem = [Data](FName ItemId) -> const FValhallaItemTemplate*
	{
		return Data->FindItem(ItemId);
	};

	if (ClassTemplate.StartingItems.Num() == 0)
	{
		// Not an error. classes.json currently gives startingItems only to the
		// rogue and the wizard; classes.ts has more, but CharacterService.ts:169
		// prefers the DataManager's copy, so 1.0 behaves the same way. The kit is
		// data, and data is the 1.0 repo's to author.
		UE_LOG(LogValhallaInventory, Log, TEXT("startingItems: class '%s' has none in classes.json; %s joins empty-handed."),
			*ClassTemplate.Id.ToString(), *CharacterName);
		return;
	}

	TArray<FName> Equipment = GatherEquipment();

	for (const FValhallaStartingItem& StartingItem : ClassTemplate.StartingItems)
	{
		const FValhallaItemTemplate* Template = FindItem(StartingItem.ItemId);
		if (!Template)
		{
			UE_LOG(LogValhallaInventory, Warning, TEXT("startingItems: class '%s' names unknown item '%s'; skipped."),
				*ClassTemplate.Id.ToString(), *StartingItem.ItemId.ToString());
			continue;
		}

		if (StartingItem.bEquipped)
		{
			// CharacterService.ts:173 — the slot comes from the item, and an item
			// with no equipSlot falls back to `weapon`. That fallback is a 1.0
			// quirk worth keeping: it is how a mis-flagged starting item still
			// ends up somewhere visible rather than vanishing.
			EValhallaEquipSlot Slot = Template->EquipSlot;
			if (Slot == EValhallaEquipSlot::None)
			{
				Slot = EValhallaEquipSlot::Weapon;
			}

			const int32 SlotIndex = ValhallaEquipSlotToIndex(Slot);
			if (SlotIndex != INDEX_NONE)
			{
				Equipment[SlotIndex] = Template->Id;
			}
		}
		else
		{
			UValhallaInventoryLibrary::AddItem(Inventory, StartingItem.ItemId, StartingItem.Quantity, FindItem);
		}
	}

	ApplyEquipment(Equipment);

	// The bare-class block, kept for the log line below: this is the one moment
	// where the before and after are both interesting.
	const FValhallaResolvedStats Bare = Valhalla::Stats::ComputeDerivedStats(ClassTemplate, Level);

	RecomputeStats();

	FString InventoryLine;
	for (const FValhallaInventorySlot& Slot : Inventory)
	{
		InventoryLine += FString::Printf(TEXT("%s x%d "), *Slot.ItemId.ToString(), Slot.Quantity);
	}

	UE_LOG(LogValhallaInventory, Log, TEXT("startingItems %s [%s]: equipment %s | inventory %s"),
		*CharacterName, *ClassTemplate.Id.ToString(), *DescribeEquipment(),
		InventoryLine.IsEmpty() ? TEXT("<empty>") : *InventoryLine.TrimEnd());

	UE_LOG(LogValhallaInventory, Log,
		TEXT("stats %s bare(str=%.0f sta=%.0f dex=%.0f int=%.0f wis=%.0f pdef=%.0f sres=%.0f dodge=%.3f hp=%.0f) -> geared(str=%.0f sta=%.0f dex=%.0f int=%.0f wis=%.0f pdef=%.0f sres=%.0f dodge=%.3f hp=%.0f)"),
		*CharacterName,
		Bare.Strength, Bare.Stamina, Bare.Dexterity, Bare.Intelligence, Bare.Wisdom, Bare.PhysicalDefense, Bare.SpellResist, Bare.DodgeRating, Bare.MaxHp,
		Stats.Strength, Stats.Stamina, Stats.Dexterity, Stats.Intelligence, Stats.Wisdom, Stats.PhysicalDefense, Stats.SpellResist, Stats.DodgeRating, Stats.MaxHp);
}

void AValhallaPlayerState::OnRep_TargetActor()
{
	TargetActor = ReplicatedTargetActor;
}

void AValhallaPlayerState::OnRep_Equipment()
{
	// GetPawn() is this player state's *own* pawn on any end, so this is how a
	// client redraws somebody else's paperdoll: the equipment fields replicate
	// to everyone, and each remote player state finds its own body.
	if (AValhallaCharacter* Character = Cast<AValhallaCharacter>(GetPawn()))
	{
		Character->RefreshEquipmentVisuals();
	}
}

void AValhallaPlayerState::SetTargetActor(AActor* NewTarget)
{
	if (!HasAuthority())
	{
		return;
	}

	// A dead or destroyed thing is not a selection. GameScene.clearTarget:3633
	// also stopped the auto-attack, and for the same reason: an auto-attack
	// without a target is a loop that stops on its next tick anyway, and
	// stopping it here means the client's button state is right immediately.
	AActor* Resolved = NewTarget;
	if (Resolved && !Resolved->IsValidLowLevelFast())
	{
		Resolved = nullptr;
	}

	if (TargetActor.Get() == Resolved)
	{
		return;
	}

	TargetActor = Resolved;
	ReplicatedTargetActor = Resolved;

	if (!Resolved)
	{
		if (const AValhallaCharacter* Character = Cast<AValhallaCharacter>(GetPawn()))
		{
			if (UValhallaSkillComponent* Skills = Character->FindComponentByClass<UValhallaSkillComponent>())
			{
				Skills->ServerStopAutoAttack();
			}
		}
	}

	UE_LOG(LogValhallaGame, Verbose, TEXT("%s target -> %s"),
		*CharacterName, Resolved ? *Resolved->GetName() : TEXT("<none>"));
}

void AValhallaPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);

	if (AValhallaPlayerState* Other = Cast<AValhallaPlayerState>(PlayerState))
	{
		Other->CharacterName = CharacterName;
		Other->ClassId = ClassId;
		Other->Level = Level;
		Other->Xp = Xp;
		Other->ZoneId = ZoneId;

		Other->Hp = Hp;
		Other->MaxHp = MaxHp;
		Other->Mana = Mana;
		Other->MaxMana = MaxMana;
		Other->Energy = Energy;
		Other->MaxEnergy = MaxEnergy;
		Other->ShieldHp = ShieldHp;
		Other->bAlive = bAlive;
		Other->VisionRange = VisionRange;

		Other->Inventory = Inventory;
		Other->ApplyEquipment(GatherEquipment());

		// The party view carries; the party itself lives in the subsystem, which
		// is keyed on the player state, so a travel that replaces one would need
		// the subsystem re-pointed too. Phase 3 owns that when zone travel lands.
		Other->PartyId = PartyId;
		Other->PartyMemberNames = PartyMemberNames;

		Other->Stats = Stats;
		Other->SkillCooldownExpiry = SkillCooldownExpiry;
		Other->ActiveBuffs = ActiveBuffs;

		// The selection deliberately does NOT carry: the new pawn is somewhere
		// else and whatever was selected is almost certainly out of range.
	}
}

void AValhallaPlayerState::InitializeFromClass(const FValhallaClassTemplate& ClassTemplate, int32 InLevel, const FString& InCharacterName, FName InZoneId)
{
	if (!HasAuthority())
	{
		return;
	}

	CharacterName = InCharacterName;
	ClassId = ClassTemplate.Id;
	Level = FMath::Max(1, InLevel);
	Xp = 0;
	ZoneId = InZoneId;

	// GameRoom.ts:607 — the stat block is the single source for every pool.
	Stats = Valhalla::Stats::ComputeDerivedStats(ClassTemplate, Level);

	MaxHp = Stats.MaxHp;
	MaxMana = Stats.MaxMana;
	MaxEnergy = Stats.MaxEnergy;

	// GameRoom.ts:617 — a fresh character joins with everything full.
	Hp = MaxHp;
	Mana = MaxMana;
	Energy = MaxEnergy;
	ShieldHp = 0.f;
	bAlive = true;

	VisionRange = ClassTemplate.VisionRange;

	SkillCooldownExpiry.Reset();
	ActiveBuffs.Reset();

	// A fresh character owns nothing. AValhallaGameMode calls GrantStartingItems
	// straight after this, which is what fills both back in and recomputes the
	// stat block over the top of the bare one just built.
	Inventory.Reset();
	ApplyEquipment(TArray<FName>());
	PartyId = 0;
	PartyMemberNames.Reset();

	// APlayerState's own name is what the engine prints in `showdebug` and in
	// the default scoreboard, so keep it in step with the character name.
	SetPlayerName(CharacterName);
}

void AValhallaPlayerState::TickRegen(float DeltaSeconds)
{
	if (!HasAuthority() || !bAlive || DeltaSeconds <= 0.f)
	{
		return;
	}

	// SkillSystem.ts:327 — energy, non-casters only (EnergyRegenRate is 0 otherwise).
	if (Stats.EnergyRegenRate > 0.f && MaxEnergy > 0.f)
	{
		Energy = FMath::Min(MaxEnergy, Energy + Stats.EnergyRegenRate * DeltaSeconds);
	}

	// SkillSystem.ts:334 — mana, casters only.
	if (Stats.ManaRegenRate > 0.f && MaxMana > 0.f)
	{
		Mana = FMath::Min(MaxMana, Mana + Stats.ManaRegenRate * DeltaSeconds);
	}
}

void AValhallaPlayerState::RecomputeStats()
{
	if (!HasAuthority())
	{
		return;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	const UValhallaDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
	const FValhallaClassTemplate* ClassTemplate = Data ? Data->FindClass(ClassId) : nullptr;
	if (!ClassTemplate)
	{
		return;
	}

	auto FindItem = [Data](FName ItemId) -> const FValhallaItemTemplate*
	{
		return Data->FindItem(ItemId);
	};

	// GameScene.ts:4290 — class base plus level growth plus equipped bonuses. See
	// UValhallaInventoryLibrary::ComputeStatsWithEquipment for where that
	// summation comes from and what it deliberately does not touch.
	Stats = UValhallaInventoryLibrary::ComputeStatsWithEquipment(*ClassTemplate, Level, GatherEquipment(), FindItem);

	MaxHp = Stats.MaxHp;
	MaxMana = Stats.MaxMana;
	MaxEnergy = Stats.MaxEnergy;

	// Re-read from the class template, not left where InitializeFromClass put
	// it. Nothing in 1.0 ever changed `visionRange` on a living character and
	// this line was not needed until Phase 6b: a data hot reload re-resolves
	// every logged-in player through here, and a ranger whose class file now
	// says 1801 has to *be* 1801 — otherwise the reload silently half-applies,
	// which is the one thing a hot reload must never do. Doing it on every
	// equip change and level-up too costs one assignment and keeps the
	// replicated value and the template from ever disagreeing.
	VisionRange = ClassTemplate->VisionRange;

	// Taking off the gear that was holding a pool up must not leave the pool
	// above its new ceiling. Nothing in the catalog moves a maximum today, but a
	// pool above its max is the kind of thing that only shows up as a health bar
	// drawn off the end of itself six months later.
	Hp = FMath::Min(Hp, MaxHp);
	Mana = FMath::Min(Mana, MaxMana);
	Energy = FMath::Min(Energy, MaxEnergy);
}

bool AValhallaPlayerState::AwardXp(int32 Amount)
{
	if (!HasAuthority() || Amount <= 0)
	{
		return false;
	}

	Xp += Amount;

	// GameRoom.update step 8 — level up in a loop, because a single large XP
	// award can cross more than one threshold at once.
	bool bLeveled = false;
	while (Level < Valhalla::MaxLevel)
	{
		const double Needed = Valhalla::Stats::XpRequiredForLevel(Level);
		if (!FMath::IsFinite(Needed) || Xp < Needed)
		{
			break;
		}

		// XP is *spent*, not accumulated: after levelling, the bar restarts.
		Xp -= FMath::RoundToInt32(Needed);
		++Level;
		bLeveled = true;
	}

	if (bLeveled)
	{
		// GameRoom.ts:832 — recompute and then fill. A level-up is a full heal,
		// which is why it is worth saving one for a hard fight.
		RecomputeStats();
		Hp = MaxHp;
		Mana = MaxMana;
		Energy = MaxEnergy;

		UE_LOG(LogValhallaGame, Log, TEXT("levelUp %s -> level %d (hp %.0f, mana %.0f)"), *CharacterName, Level, MaxHp, MaxMana);
	}

	return bLeveled;
}

void AValhallaPlayerState::RespawnWithFullPools()
{
	if (!HasAuthority())
	{
		return;
	}

	// CombatSystem.ts:448 — alive, full, and with the cast and buff state gone.
	// Leaving a DoT on across a respawn would kill the player again instantly.
	bAlive = true;
	Hp = MaxHp;
	Mana = MaxMana;
	Energy = MaxEnergy;
	ShieldHp = 0.f;
	ActiveBuffs.Reset();
	SetTargetActor(nullptr);
}

FString AValhallaPlayerState::DescribeForLog() const
{
	return FString::Printf(
		TEXT("%s [%s lv%d] hp=%.0f/%.0f mana=%.0f/%.0f energy=%.0f/%.0f speed=%.0f vision=%.0f"),
		*CharacterName, *ClassId.ToString(), Level,
		Hp, MaxHp, Mana, MaxMana, Energy, MaxEnergy,
		Stats.Speed, VisionRange);
}
