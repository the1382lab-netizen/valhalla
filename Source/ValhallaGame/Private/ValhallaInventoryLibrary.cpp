// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaInventoryLibrary.h"

#include "ValhallaConstants.h"
#include "ValhallaStats.h"

DEFINE_LOG_CATEGORY(LogValhallaInventory);

int32 ValhallaEquipSlotToIndex(EValhallaEquipSlot Slot)
{
	// None is 0 and is not a slot, so the nine real slots are 1..9 -> 0..8.
	const int32 Raw = static_cast<int32>(Slot) - 1;
	return (Raw >= 0 && Raw < ValhallaEquipSlotCount) ? Raw : INDEX_NONE;
}

EValhallaEquipSlot ValhallaEquipSlotFromIndex(int32 Index)
{
	if (Index < 0 || Index >= ValhallaEquipSlotCount)
	{
		return EValhallaEquipSlot::None;
	}
	return static_cast<EValhallaEquipSlot>(Index + 1);
}

namespace
{
	/** The 1.0 spelling of every slot, in EQUIP_SLOTS order (items.ts:78). */
	const FName EquipSlotNames[ValhallaEquipSlotCount] = {
		TEXT("weapon"), TEXT("offhand"), TEXT("helm"),
		TEXT("chest"), TEXT("legs"), TEXT("boots"),
		TEXT("gloves"), TEXT("back"), TEXT("ring"),
	};

	/**
	 * `maxStack` as addItem actually uses it.
	 *
	 * A non-stackable item's template says `maxStack: 1`, and 1.0 relies on that
	 * rather than on a separate branch. A template that says `stackable: true`
	 * with `maxStack: 0` would loop forever in 1.0; here it takes one per slot,
	 * which at least terminates.
	 */
	int32 EffectiveMaxStack(const FValhallaItemTemplate& Template)
	{
		return Template.bStackable ? FMath::Max(1, Template.MaxStack) : 1;
	}

	/** Add every field of one stat block into another. See ComputeStatsWithEquipment. */
	void AccumulateStatBlock(FValhallaStatBlock& Target, const FValhallaStatBlock& Bonus)
	{
		Target.Hp += Bonus.Hp;
		Target.Mana += Bonus.Mana;
		Target.Strength += Bonus.Strength;
		Target.Stamina += Bonus.Stamina;
		Target.Dexterity += Bonus.Dexterity;
		Target.Intelligence += Bonus.Intelligence;
		Target.Wisdom += Bonus.Wisdom;
		Target.PhysicalResist += Bonus.PhysicalResist;
		Target.SpellResist += Bonus.SpellResist;
		Target.CritChance += Bonus.CritChance;
		Target.CritDamage += Bonus.CritDamage;
		Target.PhysicalDefense += Bonus.PhysicalDefense;
		Target.BlockRating += Bonus.BlockRating;
		Target.DodgeRating += Bonus.DodgeRating;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Inventory — InventorySystem.ts:20-102
// ─────────────────────────────────────────────────────────────────────────────

bool UValhallaInventoryLibrary::AddItem(TArray<FValhallaInventorySlot>& Inventory, FName ItemId, int32 Quantity, FValhallaItemLookup FindItem)
{
	const FValhallaItemTemplate* Template = FindItem(ItemId);
	if (!Template)
	{
		// InventorySystem.ts:22 — an unknown item id is refused, not invented.
		return false;
	}

	if (Quantity <= 0)
	{
		return false;
	}

	int32 Remaining = Quantity;
	const int32 MaxStack = EffectiveMaxStack(*Template);

	// InventorySystem.ts:27 — top up existing stacks before opening a new slot.
	if (Template->bStackable)
	{
		for (int32 Index = 0; Index < Inventory.Num() && Remaining > 0; ++Index)
		{
			FValhallaInventorySlot& Slot = Inventory[Index];
			if (Slot.ItemId == ItemId && Slot.Quantity < MaxStack)
			{
				const int32 CanAdd = FMath::Min(Remaining, MaxStack - Slot.Quantity);
				Slot.Quantity += CanAdd;
				Remaining -= CanAdd;
			}
		}
	}

	// InventorySystem.ts:39 — then new slots, until the cap.
	while (Remaining > 0)
	{
		if (Inventory.Num() >= Valhalla::InventoryMaxSlots)
		{
			return false;
		}

		if (Template->bStackable)
		{
			const int32 ToAdd = FMath::Min(Remaining, MaxStack);
			Inventory.Emplace(ItemId, ToAdd);
			Remaining -= ToAdd;
		}
		else
		{
			Inventory.Emplace(ItemId, 1);
			Remaining -= 1;
		}
	}

	return true;
}

bool UValhallaInventoryLibrary::RemoveItem(TArray<FValhallaInventorySlot>& Inventory, int32 SlotIndex, int32 Quantity)
{
	if (!Inventory.IsValidIndex(SlotIndex))
	{
		return false;
	}

	FValhallaInventorySlot& Slot = Inventory[SlotIndex];
	if (Slot.Quantity < Quantity)
	{
		// InventorySystem.ts:71 — a partial removal is refused outright rather
		// than taking what is there.
		return false;
	}

	Slot.Quantity -= Quantity;
	if (Slot.Quantity <= 0)
	{
		Inventory.RemoveAt(SlotIndex);
	}

	return true;
}

bool UValhallaInventoryLibrary::HasItem(const TArray<FValhallaInventorySlot>& Inventory, FName ItemId)
{
	for (const FValhallaInventorySlot& Slot : Inventory)
	{
		if (Slot.ItemId == ItemId)
		{
			return true;
		}
	}
	return false;
}

int32 UValhallaInventoryLibrary::GetItemCount(const TArray<FValhallaInventorySlot>& Inventory, FName ItemId)
{
	int32 Total = 0;
	for (const FValhallaInventorySlot& Slot : Inventory)
	{
		if (Slot.ItemId == ItemId)
		{
			Total += Slot.Quantity;
		}
	}
	return Total;
}

bool UValhallaInventoryLibrary::SwapInventorySlots(TArray<FValhallaInventorySlot>& Inventory, int32 FromIndex, int32 ToIndex)
{
	// InventorySystem.ts:154 — swapping a slot with itself is a refusal, not a
	// no-op success, because 1.0 used the return value to decide whether to
	// re-broadcast the inventory.
	if (FromIndex == ToIndex)
	{
		return false;
	}
	if (!Inventory.IsValidIndex(FromIndex) || !Inventory.IsValidIndex(ToIndex))
	{
		return false;
	}

	// 1.0 spliced the higher index first to keep Colyseus's ChangeTree happy
	// (InventorySystem.ts:173). TArray has no such constraint, so this is just a
	// swap — the observable result is identical.
	Inventory.Swap(FromIndex, ToIndex);
	return true;
}

bool UValhallaInventoryLibrary::DropInventoryItem(TArray<FValhallaInventorySlot>& Inventory, int32 SlotIndex, FValhallaInventorySlot& OutDropped)
{
	if (!Inventory.IsValidIndex(SlotIndex))
	{
		return false;
	}

	// InventorySystem.ts:195 — the whole stack goes, not one of it.
	OutDropped = Inventory[SlotIndex];
	Inventory.RemoveAt(SlotIndex);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Equipment — InventorySystem.ts:122-279
// ─────────────────────────────────────────────────────────────────────────────

bool UValhallaInventoryLibrary::CanEquip(const FValhallaItemTemplate& Item, const FValhallaClassTemplate& ClassTemplate, FString& OutReason)
{
	// InventorySystem.ts:127 — the one test 1.0 makes.
	if (Item.EquipSlot == EValhallaEquipSlot::None)
	{
		OutReason = TEXT("That item cannot be equipped");
		return false;
	}

	if (Item.Category != EValhallaItemCategory::Equipment)
	{
		OutReason = TEXT("That item cannot be equipped");
		return false;
	}

	if (Item.bStackable)
	{
		// A stack in an equip slot has no meaning: the slot holds one id and no
		// quantity, so the rest of the stack would vanish.
		OutReason = TEXT("That item cannot be equipped");
		return false;
	}

	// See the header for why AllowedArmor is not tested. Referenced so the
	// parameter is not merely decorative and so a grep for AllowedArmor lands here.
	(void)ClassTemplate.AllowedArmor;

	OutReason.Reset();
	return true;
}

bool UValhallaInventoryLibrary::EquipItem(
	TArray<FValhallaInventorySlot>& Inventory,
	TArray<FName>& Equipment,
	int32 InventorySlotIndex,
	const FValhallaClassTemplate& ClassTemplate,
	FValhallaItemLookup FindItem,
	FString& OutReason)
{
	if (Equipment.Num() != ValhallaEquipSlotCount)
	{
		Equipment.SetNum(ValhallaEquipSlotCount);
	}

	if (!Inventory.IsValidIndex(InventorySlotIndex))
	{
		OutReason = TEXT("No such inventory slot");
		return false;
	}

	const FName ItemId = Inventory[InventorySlotIndex].ItemId;
	const FValhallaItemTemplate* Template = FindItem(ItemId);
	if (!Template)
	{
		OutReason = TEXT("Unknown item");
		return false;
	}

	if (!CanEquip(*Template, ClassTemplate, OutReason))
	{
		return false;
	}

	const int32 SlotIndex = ValhallaEquipSlotToIndex(Template->EquipSlot);
	if (SlotIndex == INDEX_NONE)
	{
		OutReason = TEXT("That item cannot be equipped");
		return false;
	}

	const FName CurrentlyEquipped = Equipment[SlotIndex];

	// InventorySystem.ts:133 — out of the inventory first. Equipment is always
	// quantity 1, which is why this is a whole-slot removal and not a decrement.
	Inventory.RemoveAt(InventorySlotIndex);

	// InventorySystem.ts:136 — the displaced item goes to the *end*, not into the
	// hole the new item left. That is visible in the UI and is worth preserving.
	if (!CurrentlyEquipped.IsNone())
	{
		Inventory.Emplace(CurrentlyEquipped, 1);
	}

	Equipment[SlotIndex] = Template->Id;
	OutReason.Reset();
	return true;
}

bool UValhallaInventoryLibrary::UnequipItem(TArray<FValhallaInventorySlot>& Inventory, TArray<FName>& Equipment, EValhallaEquipSlot Slot)
{
	const int32 SlotIndex = ValhallaEquipSlotToIndex(Slot);
	if (SlotIndex == INDEX_NONE || !Equipment.IsValidIndex(SlotIndex))
	{
		return false;
	}

	const FName CurrentlyEquipped = Equipment[SlotIndex];
	if (CurrentlyEquipped.IsNone())
	{
		return false;
	}

	// InventorySystem.ts:220 — a full inventory keeps the item equipped rather
	// than dropping it on the floor.
	if (Inventory.Num() >= Valhalla::InventoryMaxSlots)
	{
		return false;
	}

	Inventory.Emplace(CurrentlyEquipped, 1);
	Equipment[SlotIndex] = NAME_None;
	return true;
}

bool UValhallaInventoryLibrary::UnequipItemToSlot(
	TArray<FValhallaInventorySlot>& Inventory,
	TArray<FName>& Equipment,
	EValhallaEquipSlot Slot,
	int32 TargetIndex,
	FValhallaItemLookup FindItem)
{
	const int32 SlotIndex = ValhallaEquipSlotToIndex(Slot);
	if (SlotIndex == INDEX_NONE || !Equipment.IsValidIndex(SlotIndex))
	{
		return false;
	}

	const FName CurrentlyEquipped = Equipment[SlotIndex];
	if (CurrentlyEquipped.IsNone())
	{
		return false;
	}

	if (Inventory.IsValidIndex(TargetIndex))
	{
		const FValhallaItemTemplate* TargetTemplate = FindItem(Inventory[TargetIndex].ItemId);

		// InventorySystem.ts:249 — only a straight trade when the item under the
		// cursor fits the very slot being emptied. Anything else falls through.
		if (TargetTemplate && TargetTemplate->EquipSlot == Slot)
		{
			Equipment[SlotIndex] = TargetTemplate->Id;
			Inventory[TargetIndex] = FValhallaInventorySlot(CurrentlyEquipped, 1);
			return true;
		}
	}

	// InventorySystem.ts:260 / :269 — both remaining branches are the same thing:
	// unequip to the end of the inventory, refusing when it is full.
	return UnequipItem(Inventory, Equipment, Slot);
}

bool UValhallaInventoryLibrary::DropEquippedItem(TArray<FName>& Equipment, EValhallaEquipSlot Slot, FValhallaInventorySlot& OutDropped)
{
	const int32 SlotIndex = ValhallaEquipSlotToIndex(Slot);
	if (SlotIndex == INDEX_NONE || !Equipment.IsValidIndex(SlotIndex))
	{
		return false;
	}

	const FName CurrentlyEquipped = Equipment[SlotIndex];
	if (CurrentlyEquipped.IsNone())
	{
		return false;
	}

	Equipment[SlotIndex] = NAME_None;
	OutDropped = FValhallaInventorySlot(CurrentlyEquipped, 1);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Stats — GameScene.ts:4285
// ─────────────────────────────────────────────────────────────────────────────

FValhallaStatBlock UValhallaInventoryLibrary::SumEquipmentBonuses(const TArray<FName>& Equipment, FValhallaItemLookup FindItem)
{
	FValhallaStatBlock Total;

	for (const FName& ItemId : Equipment)
	{
		if (ItemId.IsNone())
		{
			continue;
		}

		const FValhallaItemTemplate* Template = FindItem(ItemId);
		if (!Template || !Template->bHasStatBonuses)
		{
			continue;
		}

		AccumulateStatBlock(Total, Template->StatBonuses);
	}

	return Total;
}

FValhallaResolvedStats UValhallaInventoryLibrary::ComputeStatsWithEquipment(
	const FValhallaClassTemplate& ClassTemplate,
	int32 Level,
	const TArray<FName>& Equipment,
	FValhallaItemLookup FindItem)
{
	// stats.ts:25 — class base plus level growth, and nothing else.
	FValhallaResolvedStats Stats = Valhalla::Stats::ComputeDerivedStats(ClassTemplate, Level);

	// GameScene.ts:4294 — then flat addition of every equipped item's bonuses
	// into the StatBlock half of the resolved block. See the header for why
	// MaxHp / MaxMana are deliberately not touched by an `hp` / `mana` bonus.
	const FValhallaStatBlock Bonuses = SumEquipmentBonuses(Equipment, FindItem);
	AccumulateStatBlock(Stats, Bonuses);

	return Stats;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Loot — LootBagSystem.ts:62-237
// ─────────────────────────────────────────────────────────────────────────────

TArray<FValhallaBagSlot> UValhallaInventoryLibrary::RollLootTable(const FValhallaLootTable& Table, TFunctionRef<double()> Rand)
{
	TArray<FValhallaBagSlot> Result;

	for (const FValhallaLootEntry& Entry : Table.Entries)
	{
		// LootBagSystem.ts:148 — strictly greater than. An entry with
		// dropChance 1 therefore always drops, and one with dropChance 0 never
		// does, since Math.random() is in [0, 1).
		if (Rand() > Entry.DropChance)
		{
			continue;
		}

		// LootBagSystem.ts:151 — equal bounds skip the roll entirely, so a
		// 1..1 entry consumes one random number, not two. That matters to
		// anything replaying a seeded sequence.
		int32 Quantity;
		if (Entry.MinQuantity == Entry.MaxQuantity)
		{
			Quantity = Entry.MinQuantity;
		}
		else
		{
			Quantity = FMath::FloorToInt32(Rand() * (Entry.MaxQuantity - Entry.MinQuantity + 1)) + Entry.MinQuantity;
		}

		if (Quantity > 0)
		{
			Result.Emplace(Entry.ItemId, Quantity);
		}
	}

	return Result;
}

void UValhallaInventoryLibrary::AddItemsToBag(TArray<FValhallaBagSlot>& Bag, const TArray<FValhallaBagSlot>& Items, FValhallaItemLookup FindItem)
{
	for (const FValhallaBagSlot& Item : Items)
	{
		const FValhallaItemTemplate* Template = FindItem(Item.ItemId);
		int32 Remaining = Item.Quantity;

		// LootBagSystem.ts:209 — an unknown item is *not* refused here, unlike in
		// addItem: 1.0 uses optional chaining and treats a missing template as
		// non-stackable. Mirrored, so a data edit that removes an item does not
		// make an existing bag undroppable.
		const bool bStackable = Template && Template->bStackable;
		const int32 MaxStack = Template ? EffectiveMaxStack(*Template) : 1;

		if (bStackable)
		{
			for (int32 Index = 0; Index < Bag.Num() && Remaining > 0; ++Index)
			{
				FValhallaBagSlot& Slot = Bag[Index];
				if (Slot.ItemId == Item.ItemId && Slot.Quantity < MaxStack)
				{
					const int32 CanAdd = FMath::Min(Remaining, MaxStack - Slot.Quantity);
					Slot.Quantity += CanAdd;
					Remaining -= CanAdd;
				}
			}
		}

		// LootBagSystem.ts:221 — anything that does not fit in an 18-slot bag is
		// simply lost. 1.0 drops it silently and so does this.
		while (Remaining > 0 && Bag.Num() < Valhalla::LootBagMaxSlots)
		{
			if (bStackable)
			{
				const int32 ToAdd = FMath::Min(Remaining, MaxStack);
				Bag.Emplace(Item.ItemId, ToAdd);
				Remaining -= ToAdd;
			}
			else
			{
				Bag.Emplace(Item.ItemId, 1);
				Remaining -= 1;
			}
		}
	}
}

bool UValhallaInventoryLibrary::LootItem(
	TArray<FValhallaBagSlot>& Bag,
	TArray<FValhallaInventorySlot>& Inventory,
	int32 SlotIndex,
	int32 Quantity,
	FValhallaItemLookup FindItem)
{
	if (!Bag.IsValidIndex(SlotIndex))
	{
		return false;
	}

	const int32 ActualQuantity = FMath::Min(Quantity, Bag[SlotIndex].Quantity);
	if (ActualQuantity <= 0)
	{
		return false;
	}

	// LootBagSystem.ts:76 — a full inventory leaves the item in the bag. The add
	// happens first because only the add knows whether there is room.
	if (!AddItem(Inventory, Bag[SlotIndex].ItemId, ActualQuantity, FindItem))
	{
		return false;
	}

	Bag[SlotIndex].Quantity -= ActualQuantity;
	if (Bag[SlotIndex].Quantity <= 0)
	{
		Bag.RemoveAt(SlotIndex);
	}

	return true;
}

int32 UValhallaInventoryLibrary::LootAll(
	TArray<FValhallaBagSlot>& Bag,
	TArray<FValhallaInventorySlot>& Inventory,
	FValhallaItemLookup FindItem)
{
	int32 LootedCount = 0;

	// LootBagSystem.ts:99 — backwards, so removing a slot does not shift the
	// ones still to be visited.
	for (int32 Index = Bag.Num() - 1; Index >= 0; --Index)
	{
		if (AddItem(Inventory, Bag[Index].ItemId, Bag[Index].Quantity, FindItem))
		{
			Bag.RemoveAt(Index);
			++LootedCount;
		}
	}

	return LootedCount;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Names
// ─────────────────────────────────────────────────────────────────────────────

EValhallaEquipSlot UValhallaInventoryLibrary::ParseEquipSlotName(FName Name)
{
	if (Name.IsNone())
	{
		return EValhallaEquipSlot::None;
	}

	const FString Lower = Name.ToString().ToLower();
	for (int32 Index = 0; Index < ValhallaEquipSlotCount; ++Index)
	{
		if (Lower == EquipSlotNames[Index].ToString())
		{
			return ValhallaEquipSlotFromIndex(Index);
		}
	}

	return EValhallaEquipSlot::None;
}

FName UValhallaInventoryLibrary::EquipSlotToName(EValhallaEquipSlot Slot)
{
	const int32 Index = ValhallaEquipSlotToIndex(Slot);
	return Index == INDEX_NONE ? NAME_None : EquipSlotNames[Index];
}
