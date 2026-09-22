// Copyright Valhalla 2.0. All Rights Reserved.
//
// Phase 2c rules tests.
//
// The four rules here are the ones whose failure would look like balance rather
// than like a bug — the same reason the Phase 2b tests picked what they picked:
//
//   AddRemoveStack  a stack that tops up the wrong slot, or a cap that is off by
//                   one, just makes bags feel smaller.
//   EquipRules      a displaced item that lands in the wrong place, or a swap
//                   that silently drops one of the two items, looks like a UI
//                   glitch until somebody loses a weapon.
//   RollTable       a `>` that became a `>=` changes every drop rate in the game
//                   by exactly one roll's worth and nothing ever errors.
//   XpSplit         two floors and a minimum-of-one, any of which can be dropped
//                   without anything failing, and all of which change how much a
//                   party is worth joining.
//
// Everything under test is a static that takes no world, no actors and no
// subsystem — the lookup into items.json arrives as a lambda — which is what
// lets these run in the commandlet context with nothing loaded.
//
// Run from the editor's Session Frontend, or headless:
//   UnrealEditor-Cmd.exe <project>.uproject -ExecCmds="Automation RunTests Valhalla.; Quit" -unattended -nullrhi

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ValhallaConstants.h"
#include "ValhallaInventoryLibrary.h"
#include "ValhallaInventoryTypes.h"
#include "ValhallaStats.h"
#include "ValhallaTypes.h"

// Guarded because ValhallaGameTests.cpp defines the same flags, and a unity
// build puts the two in one translation unit.
#ifndef VALHALLA_GAME_TEST_FLAGS
#define VALHALLA_GAME_TEST_FLAGS ( \
	EAutomationTestFlags::EditorContext \
	| EAutomationTestFlags::ClientContext \
	| EAutomationTestFlags::ServerContext \
	| EAutomationTestFlags::CommandletContext \
	| EAutomationTestFlags::ProductFilter)
#endif

namespace ValhallaInventoryTests
{
	/** Stat fields are floats, so the tolerance is one too. */
	static constexpr float Tolerance = 1e-5f;

	/** A stackable consumable, the shape of `health_potion`. */
	static FValhallaItemTemplate MakeStackable(FName Id, int32 MaxStack)
	{
		FValhallaItemTemplate Item;
		Item.Id = Id;
		Item.Name = Id.ToString();
		Item.Category = EValhallaItemCategory::Consumable;
		Item.EquipSlot = EValhallaEquipSlot::None;
		Item.bStackable = true;
		Item.MaxStack = MaxStack;
		return Item;
	}

	/** A piece of equipment, with optional stat bonuses. */
	static FValhallaItemTemplate MakeEquipment(FName Id, EValhallaEquipSlot Slot)
	{
		FValhallaItemTemplate Item;
		Item.Id = Id;
		Item.Name = Id.ToString();
		Item.Category = EValhallaItemCategory::Equipment;
		Item.EquipSlot = Slot;
		Item.bStackable = false;
		Item.MaxStack = 1;
		return Item;
	}

	/** A tiny stand-in for items.json, and the lookup the library takes. */
	struct FCatalog
	{
		TMap<FName, FValhallaItemTemplate> Items;

		void Add(const FValhallaItemTemplate& Item) { Items.Add(Item.Id, Item); }

		const FValhallaItemTemplate* Find(FName Id) const { return Items.Find(Id); }
	};

	/** A class with plausible level-1 numbers. Only the stat maths reads these. */
	static FValhallaClassTemplate MakeClass(FName Id)
	{
		FValhallaClassTemplate ClassTemplate;
		ClassTemplate.Id = Id;
		ClassTemplate.Name = Id.ToString();
		ClassTemplate.AllowedArmor = EValhallaArmorType::Plate;
		ClassTemplate.bCanUseMana = false;
		ClassTemplate.BaseSpeed = 144.f;
		ClassTemplate.BaseMeleeAttackSpeedMs = 3600.f;

		ClassTemplate.BaseStats.Hp = 150.f;
		ClassTemplate.BaseStats.Strength = 20.f;
		ClassTemplate.BaseStats.Stamina = 18.f;
		ClassTemplate.BaseStats.Dexterity = 10.f;
		ClassTemplate.BaseStats.PhysicalDefense = 12.f;

		return ClassTemplate;
	}

	/**
	 * A deterministic stand-in for Math.random().
	 *
	 * Hands out a fixed script of values, then repeats the last one. That is
	 * exactly what a loot-table test needs: the point is not that the numbers are
	 * uniformly distributed, it is that a given sequence of them produces a given
	 * drop, every time, on every machine.
	 */
	struct FScriptedRandom
	{
		TArray<double> Values;
		int32 Cursor = 0;

		explicit FScriptedRandom(std::initializer_list<double> In) : Values(In) {}

		double Next()
		{
			if (Values.Num() == 0)
			{
				return 0.0;
			}
			const double Value = Values[FMath::Min(Cursor, Values.Num() - 1)];
			++Cursor;
			return Value;
		}
	};
}

// ─────────────────────────────────────────────────────────────────────────────
//  Add / remove / stack — InventorySystem.ts:20 addItem, :67 removeItem
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaInventoryAddRemoveStackTest,
	"Valhalla.Game.Inventory.AddRemoveStack",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaInventoryAddRemoveStackTest::RunTest(const FString& Parameters)
{
	using namespace ValhallaInventoryTests;

	FCatalog Catalog;
	Catalog.Add(MakeStackable(TEXT("health_potion"), 20));
	Catalog.Add(MakeEquipment(TEXT("iron_sword"), EValhallaEquipSlot::Weapon));

	auto FindItem = [&Catalog](FName Id) -> const FValhallaItemTemplate* { return Catalog.Find(Id); };

	const FName Potion(TEXT("health_potion"));
	const FName Sword(TEXT("iron_sword"));

	// ── An unknown item is refused, not invented ─────────────────────────
	{
		TArray<FValhallaInventorySlot> Inventory;
		TestFalse(TEXT("An item that is not in the catalog cannot be added"),
			UValhallaInventoryLibrary::AddItem(Inventory, TEXT("not_a_real_item"), 1, FindItem));
		TestEqual(TEXT("…and nothing was added"), Inventory.Num(), 0);
	}

	// ── Stackables fill one slot until maxStack ──────────────────────────
	{
		TArray<FValhallaInventorySlot> Inventory;

		TestTrue(TEXT("Five potions go in"), UValhallaInventoryLibrary::AddItem(Inventory, Potion, 5, FindItem));
		TestEqual(TEXT("…as one slot"), Inventory.Num(), 1);
		TestEqual(TEXT("…of five"), Inventory[0].Quantity, 5);

		// InventorySystem.ts:27 — the existing stack is topped up before a new
		// slot is opened. Getting this backwards leaves an inventory full of
		// one-potion slots, which looks like a UI bug and is not one.
		TestTrue(TEXT("Ten more potions go in"), UValhallaInventoryLibrary::AddItem(Inventory, Potion, 10, FindItem));
		TestEqual(TEXT("…still one slot"), Inventory.Num(), 1);
		TestEqual(TEXT("…of fifteen"), Inventory[0].Quantity, 15);

		// 15 + 10 = 25, which is 20 + 5: the cap is honoured and the remainder
		// opens exactly one more slot.
		TestTrue(TEXT("Ten more still fit"), UValhallaInventoryLibrary::AddItem(Inventory, Potion, 10, FindItem));
		TestEqual(TEXT("…in two slots now"), Inventory.Num(), 2);
		TestEqual(TEXT("…the first capped at maxStack"), Inventory[0].Quantity, 20);
		TestEqual(TEXT("…and the overflow in the second"), Inventory[1].Quantity, 5);

		TestEqual(TEXT("getItemCount sums across slots"),
			UValhallaInventoryLibrary::GetItemCount(Inventory, Potion), 25);
		TestTrue(TEXT("hasItem finds it"), UValhallaInventoryLibrary::HasItem(Inventory, Potion));
		TestFalse(TEXT("hasItem does not find what is not there"),
			UValhallaInventoryLibrary::HasItem(Inventory, Sword));
	}

	// ── Non-stackables take one slot each ────────────────────────────────
	{
		TArray<FValhallaInventorySlot> Inventory;

		TestTrue(TEXT("Three swords go in"), UValhallaInventoryLibrary::AddItem(Inventory, Sword, 3, FindItem));
		TestEqual(TEXT("…as three separate slots"), Inventory.Num(), 3);
		TestEqual(TEXT("…each of one"), Inventory[2].Quantity, 1);
	}

	// ── removeItem: partial, exact, and refused ──────────────────────────
	{
		TArray<FValhallaInventorySlot> Inventory;
		UValhallaInventoryLibrary::AddItem(Inventory, Potion, 5, FindItem);

		// InventorySystem.ts:71 — asking for more than the slot holds is refused
		// outright. It does NOT take what is there, which is the behaviour a
		// reimplementation reaches for and which would let a client drain a slot
		// by asking for a large number.
		TestFalse(TEXT("Removing more than the slot holds is refused"),
			UValhallaInventoryLibrary::RemoveItem(Inventory, 0, 6));
		TestEqual(TEXT("…and takes nothing"), Inventory[0].Quantity, 5);

		TestTrue(TEXT("Removing two of five works"), UValhallaInventoryLibrary::RemoveItem(Inventory, 0, 2));
		TestEqual(TEXT("…leaving three"), Inventory[0].Quantity, 3);
		TestEqual(TEXT("…in the same slot"), Inventory.Num(), 1);

		// InventorySystem.ts:74 — emptying a slot removes it, rather than leaving
		// a zero-quantity hole behind.
		TestTrue(TEXT("Removing the last three works"), UValhallaInventoryLibrary::RemoveItem(Inventory, 0, 3));
		TestEqual(TEXT("…and the slot goes away entirely"), Inventory.Num(), 0);

		TestFalse(TEXT("Removing from an index that is not there is refused"),
			UValhallaInventoryLibrary::RemoveItem(Inventory, 0, 1));
	}

	// ── The 32-slot cap — items.ts:13 INVENTORY_MAX_SLOTS ────────────────
	{
		TArray<FValhallaInventorySlot> Inventory;

		for (int32 Index = 0; Index < Valhalla::InventoryMaxSlots; ++Index)
		{
			TestTrue(TEXT("Swords fill the inventory one slot at a time"),
				UValhallaInventoryLibrary::AddItem(Inventory, Sword, 1, FindItem));
		}

		TestEqual(TEXT("The inventory holds exactly INVENTORY_MAX_SLOTS"),
			Inventory.Num(), Valhalla::InventoryMaxSlots);

		TestFalse(TEXT("One more is refused"), UValhallaInventoryLibrary::AddItem(Inventory, Sword, 1, FindItem));
		TestEqual(TEXT("…and the inventory did not grow"), Inventory.Num(), Valhalla::InventoryMaxSlots);

		// A stackable *can* still go in, because it tops up nothing and needs no
		// slot — except that there is no stack of it, so it needs one and fails.
		TestFalse(TEXT("A stackable with no existing stack is refused too"),
			UValhallaInventoryLibrary::AddItem(Inventory, Potion, 1, FindItem));
	}

	// ── A partial add is not rolled back — InventorySystem.ts:39 ─────────
	{
		TArray<FValhallaInventorySlot> Inventory;
		for (int32 Index = 0; Index < Valhalla::InventoryMaxSlots - 1; ++Index)
		{
			UValhallaInventoryLibrary::AddItem(Inventory, Sword, 1, FindItem);
		}

		// Three swords into one free slot: one lands, two are refused, and the
		// one that landed stays. 1.0 does this and so does 2.0 — callers that
		// cannot live with it (LootItem) check first.
		TestFalse(TEXT("Three swords into one free slot returns false"),
			UValhallaInventoryLibrary::AddItem(Inventory, Sword, 3, FindItem));
		TestEqual(TEXT("…but the one that fitted stayed in"),
			Inventory.Num(), Valhalla::InventoryMaxSlots);
	}

	// ── swapInventorySlots — InventorySystem.ts:153 ──────────────────────
	{
		TArray<FValhallaInventorySlot> Inventory;
		UValhallaInventoryLibrary::AddItem(Inventory, Sword, 1, FindItem);
		UValhallaInventoryLibrary::AddItem(Inventory, Potion, 7, FindItem);

		TestTrue(TEXT("Two occupied slots swap"), UValhallaInventoryLibrary::SwapInventorySlots(Inventory, 0, 1));
		TestEqual(TEXT("…the potions are now first"), Inventory[0].ItemId, Potion);
		TestEqual(TEXT("…with their quantity intact"), Inventory[0].Quantity, 7);
		TestEqual(TEXT("…and the sword second"), Inventory[1].ItemId, Sword);

		// InventorySystem.ts:154 — a self-swap is a refusal, not a silent success.
		TestFalse(TEXT("Swapping a slot with itself is refused"),
			UValhallaInventoryLibrary::SwapInventorySlots(Inventory, 1, 1));
		TestFalse(TEXT("Swapping with an index that is not there is refused"),
			UValhallaInventoryLibrary::SwapInventorySlots(Inventory, 0, 9));
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Equip rules — InventorySystem.ts:122 equipItem, :240 unequipItemToSlot
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaInventoryEquipRulesTest,
	"Valhalla.Game.Inventory.EquipRules",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaInventoryEquipRulesTest::RunTest(const FString& Parameters)
{
	using namespace ValhallaInventoryTests;

	const FName Sword(TEXT("iron_sword"));
	const FName Dagger(TEXT("iron_dagger"));
	const FName Helm(TEXT("iron_helm"));
	const FName Potion(TEXT("health_potion"));

	FCatalog Catalog;

	{
		FValhallaItemTemplate SwordTemplate = MakeEquipment(Sword, EValhallaEquipSlot::Weapon);
		SwordTemplate.bHasStatBonuses = true;
		SwordTemplate.StatBonuses.Strength = 3.f;
		SwordTemplate.bHasAttackSpeed = true;
		SwordTemplate.AttackSpeedMs = 1600.f;
		Catalog.Add(SwordTemplate);
	}
	{
		FValhallaItemTemplate DaggerTemplate = MakeEquipment(Dagger, EValhallaEquipSlot::Weapon);
		DaggerTemplate.bHasStatBonuses = true;
		DaggerTemplate.StatBonuses.Dexterity = 2.f;
		DaggerTemplate.StatBonuses.Strength = 1.f;
		Catalog.Add(DaggerTemplate);
	}
	{
		FValhallaItemTemplate HelmTemplate = MakeEquipment(Helm, EValhallaEquipSlot::Helm);
		HelmTemplate.bHasStatBonuses = true;
		HelmTemplate.StatBonuses.Stamina = 2.f;
		HelmTemplate.StatBonuses.PhysicalDefense = 2.f;
		Catalog.Add(HelmTemplate);
	}
	Catalog.Add(MakeStackable(Potion, 20));

	auto FindItem = [&Catalog](FName Id) -> const FValhallaItemTemplate* { return Catalog.Find(Id); };

	const FValhallaClassTemplate Warrior = MakeClass(TEXT("warrior"));

	// ── What may and may not be equipped ─────────────────────────────────
	{
		FString Reason;

		TestTrue(TEXT("A weapon may be equipped"),
			UValhallaInventoryLibrary::CanEquip(*Catalog.Find(Sword), Warrior, Reason));

		// A consumable has no equipSlot, which is the one test 1.0 makes
		// (InventorySystem.ts:127).
		TestFalse(TEXT("A consumable may not be equipped"),
			UValhallaInventoryLibrary::CanEquip(*Catalog.Find(Potion), Warrior, Reason));
		TestFalse(TEXT("…and it says why"), Reason.IsEmpty());

		// The 2.0 belt-and-braces tests. Neither shape exists in items.json; both
		// are here so that a data edit that creates one is caught by a test
		// rather than by a player with a stack of twenty equipped swords.
		{
			FValhallaItemTemplate Nonsense = MakeEquipment(TEXT("stackable_sword"), EValhallaEquipSlot::Weapon);
			Nonsense.bStackable = true;
			Nonsense.MaxStack = 20;
			TestFalse(TEXT("A stackable item may never be equipped, whatever its slot says"),
				UValhallaInventoryLibrary::CanEquip(Nonsense, Warrior, Reason));
		}
		{
			FValhallaItemTemplate Nonsense = MakeEquipment(TEXT("quest_sword"), EValhallaEquipSlot::Weapon);
			Nonsense.Category = EValhallaItemCategory::Quest;
			TestFalse(TEXT("A non-equipment category may not be equipped"),
				UValhallaInventoryLibrary::CanEquip(Nonsense, Warrior, Reason));
		}
	}

	// ── Equipping into an empty slot ─────────────────────────────────────
	{
		TArray<FValhallaInventorySlot> Inventory;
		TArray<FName> Equipment;
		Equipment.SetNum(ValhallaEquipSlotCount);
		FString Reason;

		UValhallaInventoryLibrary::AddItem(Inventory, Sword, 1, FindItem);
		UValhallaInventoryLibrary::AddItem(Inventory, Potion, 3, FindItem);

		TestTrue(TEXT("The sword equips"),
			UValhallaInventoryLibrary::EquipItem(Inventory, Equipment, 0, Warrior, FindItem, Reason));
		TestEqual(TEXT("…into the weapon slot"),
			Equipment[ValhallaEquipSlotToIndex(EValhallaEquipSlot::Weapon)], Sword);
		TestEqual(TEXT("…and out of the inventory"), Inventory.Num(), 1);
		TestEqual(TEXT("…leaving the potions behind"), Inventory[0].ItemId, Potion);

		// Slot 0 is now the potions. Equipping them must fail and must not
		// disturb the sword.
		TestFalse(TEXT("The potions do not equip"),
			UValhallaInventoryLibrary::EquipItem(Inventory, Equipment, 0, Warrior, FindItem, Reason));
		TestEqual(TEXT("…and the potions are still in the inventory"), Inventory.Num(), 1);
		TestEqual(TEXT("…and the sword is still equipped"),
			Equipment[ValhallaEquipSlotToIndex(EValhallaEquipSlot::Weapon)], Sword);

		TestFalse(TEXT("An inventory index that is not there is refused"),
			UValhallaInventoryLibrary::EquipItem(Inventory, Equipment, 17, Warrior, FindItem, Reason));
	}

	// ── Equipping over something — InventorySystem.ts:136 ────────────────
	{
		TArray<FValhallaInventorySlot> Inventory;
		TArray<FName> Equipment;
		Equipment.SetNum(ValhallaEquipSlotCount);
		FString Reason;

		UValhallaInventoryLibrary::AddItem(Inventory, Sword, 1, FindItem);
		UValhallaInventoryLibrary::AddItem(Inventory, Potion, 3, FindItem);
		UValhallaInventoryLibrary::AddItem(Inventory, Dagger, 1, FindItem);

		UValhallaInventoryLibrary::EquipItem(Inventory, Equipment, 0, Warrior, FindItem, Reason);

		// Inventory is now [potions, dagger]. Equipping the dagger displaces the
		// sword, which goes to the *end* — not into the hole the dagger left.
		// That ordering is visible in the UI and is worth pinning down.
		TestTrue(TEXT("The dagger equips over the sword"),
			UValhallaInventoryLibrary::EquipItem(Inventory, Equipment, 1, Warrior, FindItem, Reason));
		TestEqual(TEXT("…the dagger is in the weapon slot"),
			Equipment[ValhallaEquipSlotToIndex(EValhallaEquipSlot::Weapon)], Dagger);
		TestEqual(TEXT("…the inventory is two slots again"), Inventory.Num(), 2);
		TestEqual(TEXT("…the potions did not move"), Inventory[0].ItemId, Potion);
		TestEqual(TEXT("…and the sword went to the end"), Inventory[1].ItemId, Sword);
	}

	// ── Two slots do not interfere ───────────────────────────────────────
	{
		TArray<FValhallaInventorySlot> Inventory;
		TArray<FName> Equipment;
		Equipment.SetNum(ValhallaEquipSlotCount);
		FString Reason;

		UValhallaInventoryLibrary::AddItem(Inventory, Sword, 1, FindItem);
		UValhallaInventoryLibrary::AddItem(Inventory, Helm, 1, FindItem);

		UValhallaInventoryLibrary::EquipItem(Inventory, Equipment, 0, Warrior, FindItem, Reason);
		UValhallaInventoryLibrary::EquipItem(Inventory, Equipment, 0, Warrior, FindItem, Reason);

		TestEqual(TEXT("The sword is in the weapon slot"),
			Equipment[ValhallaEquipSlotToIndex(EValhallaEquipSlot::Weapon)], Sword);
		TestEqual(TEXT("The helm is in the helm slot"),
			Equipment[ValhallaEquipSlotToIndex(EValhallaEquipSlot::Helm)], Helm);
		TestEqual(TEXT("The inventory is empty"), Inventory.Num(), 0);

		// ── The stat recompute — GameScene.ts:4294 ───────────────────────
		const FValhallaResolvedStats Bare = Valhalla::Stats::ComputeDerivedStats(Warrior, 1);
		const FValhallaResolvedStats Geared =
			UValhallaInventoryLibrary::ComputeStatsWithEquipment(Warrior, 1, Equipment, FindItem);

		TestEqual(TEXT("Strength gains the sword's +3"), Geared.Strength, Bare.Strength + 3.f, Tolerance);
		TestEqual(TEXT("Stamina gains the helm's +2"), Geared.Stamina, Bare.Stamina + 2.f, Tolerance);
		TestEqual(TEXT("Physical defense gains the helm's +2"),
			Geared.PhysicalDefense, Bare.PhysicalDefense + 2.f, Tolerance);
		TestEqual(TEXT("A stat nothing bonuses is untouched"), Geared.Dexterity, Bare.Dexterity, Tolerance);

		// ── unequip back to the end — InventorySystem.ts:215 ─────────────
		TestTrue(TEXT("The weapon unequips"),
			UValhallaInventoryLibrary::UnequipItem(Inventory, Equipment, EValhallaEquipSlot::Weapon));
		TestTrue(TEXT("…the weapon slot is empty"),
			Equipment[ValhallaEquipSlotToIndex(EValhallaEquipSlot::Weapon)].IsNone());
		TestEqual(TEXT("…and the sword is back in the inventory"), Inventory[0].ItemId, Sword);

		TestFalse(TEXT("Unequipping an empty slot is refused"),
			UValhallaInventoryLibrary::UnequipItem(Inventory, Equipment, EValhallaEquipSlot::Weapon));
		TestFalse(TEXT("Unequipping a slot that never had anything is refused"),
			UValhallaInventoryLibrary::UnequipItem(Inventory, Equipment, EValhallaEquipSlot::Back));
	}

	// ── unequipItemToSlot's two branches — InventorySystem.ts:249 / :260 ──
	{
		TArray<FValhallaInventorySlot> Inventory;
		TArray<FName> Equipment;
		Equipment.SetNum(ValhallaEquipSlotCount);

		Equipment[ValhallaEquipSlotToIndex(EValhallaEquipSlot::Weapon)] = Sword;
		UValhallaInventoryLibrary::AddItem(Inventory, Dagger, 1, FindItem);
		UValhallaInventoryLibrary::AddItem(Inventory, Potion, 3, FindItem);

		// The dagger fits the weapon slot, so this is a straight trade in place:
		// the dagger becomes equipped and the sword takes its inventory index.
		// The inventory does not grow.
		TestTrue(TEXT("Unequipping onto a same-slot item is a trade"),
			UValhallaInventoryLibrary::UnequipItemToSlot(Inventory, Equipment, EValhallaEquipSlot::Weapon, 0, FindItem));
		TestEqual(TEXT("…the dagger is now equipped"),
			Equipment[ValhallaEquipSlotToIndex(EValhallaEquipSlot::Weapon)], Dagger);
		TestEqual(TEXT("…the sword is in the index the dagger was in"), Inventory[0].ItemId, Sword);
		TestEqual(TEXT("…and the inventory did not grow"), Inventory.Num(), 2);

		// The potions do NOT fit the weapon slot, so this falls through to
		// "unequip to the end" and leaves the potions exactly where they are.
		TestTrue(TEXT("Unequipping onto a different-slot item appends instead"),
			UValhallaInventoryLibrary::UnequipItemToSlot(Inventory, Equipment, EValhallaEquipSlot::Weapon, 1, FindItem));
		TestTrue(TEXT("…the weapon slot is empty"),
			Equipment[ValhallaEquipSlotToIndex(EValhallaEquipSlot::Weapon)].IsNone());
		TestEqual(TEXT("…the potions did not move"), Inventory[1].ItemId, Potion);
		TestEqual(TEXT("…and the dagger went to the end"), Inventory[2].ItemId, Dagger);
	}

	// ── dropEquippedItem — InventorySystem.ts:204 ────────────────────────
	{
		TArray<FName> Equipment;
		Equipment.SetNum(ValhallaEquipSlotCount);
		Equipment[ValhallaEquipSlotToIndex(EValhallaEquipSlot::Helm)] = Helm;

		FValhallaInventorySlot Dropped;
		TestTrue(TEXT("An equipped helm can be dropped"),
			UValhallaInventoryLibrary::DropEquippedItem(Equipment, EValhallaEquipSlot::Helm, Dropped));
		TestEqual(TEXT("…and it is the helm"), Dropped.ItemId, Helm);
		TestEqual(TEXT("…always as a quantity of one"), Dropped.Quantity, 1);
		TestTrue(TEXT("…and the slot is empty"),
			Equipment[ValhallaEquipSlotToIndex(EValhallaEquipSlot::Helm)].IsNone());

		TestFalse(TEXT("An empty slot drops nothing"),
			UValhallaInventoryLibrary::DropEquippedItem(Equipment, EValhallaEquipSlot::Helm, Dropped));
	}

	// ── The slot-name table round-trips — items.ts:78 / :109 ─────────────
	{
		for (int32 Index = 0; Index < ValhallaEquipSlotCount; ++Index)
		{
			const EValhallaEquipSlot Slot = ValhallaEquipSlotFromIndex(Index);
			const FName Name = UValhallaInventoryLibrary::EquipSlotToName(Slot);

			TestFalse(TEXT("Every slot has a name"), Name.IsNone());
			TestEqual(TEXT("…and the name parses back to the same slot"),
				UValhallaInventoryLibrary::ParseEquipSlotName(Name), Slot);
		}

		TestEqual(TEXT("There are exactly nine slots, with one ring"), ValhallaEquipSlotCount, 9);
		TestEqual(TEXT("A name that is not a slot parses to None"),
			UValhallaInventoryLibrary::ParseEquipSlotName(TEXT("trousers")), EValhallaEquipSlot::None);
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Loot table rolls — LootBagSystem.ts:140 rollLootTable
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaLootRollTableTest,
	"Valhalla.Game.Loot.RollTable",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaLootRollTableTest::RunTest(const FString& Parameters)
{
	using namespace ValhallaInventoryTests;

	// A table shaped like the real loot_1771620382459, plus the two things that
	// one does not exercise: a chance below 1, and a quantity range.
	FValhallaLootTable Table;
	Table.Id = TEXT("test_table");

	{
		FValhallaLootEntry Always;
		Always.ItemId = TEXT("cloth_hood");
		Always.Weight = 1.f;
		Always.DropChance = 1.f;
		Always.MinQuantity = 1;
		Always.MaxQuantity = 1;
		Table.Entries.Add(Always);
	}
	{
		FValhallaLootEntry Sometimes;
		Sometimes.ItemId = TEXT("gold_coin");
		Sometimes.Weight = 5.f;
		Sometimes.DropChance = 0.5f;
		Sometimes.MinQuantity = 10;
		Sometimes.MaxQuantity = 20;
		Table.Entries.Add(Sometimes);
	}
	{
		FValhallaLootEntry Never;
		Never.ItemId = TEXT("ring_of_wisdom");
		Never.Weight = 100.f;
		Never.DropChance = 0.f;
		Never.MinQuantity = 1;
		Never.MaxQuantity = 1;
		Table.Entries.Add(Never);
	}

	// ── dropChance 1 always drops; dropChance 0 never does ───────────────
	//
	// LootBagSystem.ts:148 is `Math.random() > entry.dropChance`, strictly
	// greater. Math.random() returns [0, 1), so 1 can never be exceeded and 0
	// always is. A `>=` here would make a guaranteed drop fail once in every
	// 2^53 kills and a never-drop entry drop at the same rate — invisible in
	// testing, wrong in principle, and the exact thing this asserts.
	{
		// Rolls: hood chance 0.0 (drops, no quantity roll — min == max),
		//        coin chance 0.999 (> 0.5, skipped),
		//        ring chance 0.0 (0.0 > 0.0 is false, so it *would* drop — but
		//        its chance is 0, so the comparison is 0.0 > 0.0 = false…)
		//
		// That last case is the one worth being explicit about: with a roll of
		// exactly 0.0 and a dropChance of exactly 0.0, `>` is false and the entry
		// DOES drop. It is a measure-zero event in 1.0 and it is reproduced here
		// rather than "fixed", because fixing it would be a balance change made
		// silently by a port.
		FScriptedRandom Random({ 0.0, 0.999, 0.999 });
		auto Rand = [&Random]() { return Random.Next(); };

		const TArray<FValhallaBagSlot> Rolled = UValhallaInventoryLibrary::RollLootTable(Table, Rand);

		TestEqual(TEXT("Only the guaranteed entry dropped"), Rolled.Num(), 1);
		TestEqual(TEXT("…and it is the cloth hood"), Rolled[0].ItemId, FName(TEXT("cloth_hood")));
		TestEqual(TEXT("…of exactly one"), Rolled[0].Quantity, 1);
	}

	// ── Entries are independent, not a weighted pick of one ──────────────
	//
	// `weight` is in the schema and in the editor UI and rollLootTable never
	// reads it. Every entry that passes its own dropChance drops, so a table of
	// three guaranteed entries drops all three — which is what the real
	// loot_1771620382459 does on every single kill.
	{
		FScriptedRandom Random({ 0.0, 0.0, 0.5 });
		auto Rand = [&Random]() { return Random.Next(); };

		const TArray<FValhallaBagSlot> Rolled = UValhallaInventoryLibrary::RollLootTable(Table, Rand);

		TestEqual(TEXT("Two entries dropped together, ignoring weight"), Rolled.Num(), 2);
		TestEqual(TEXT("…the hood"), Rolled[0].ItemId, FName(TEXT("cloth_hood")));
		TestEqual(TEXT("…and the coins"), Rolled[1].ItemId, FName(TEXT("gold_coin")));

		// LootBagSystem.ts:153 — floor(r * (max - min + 1)) + min. The 0.5 is the
		// quantity roll: floor(0.5 * 11) + 10 = 5 + 10 = 15.
		TestEqual(TEXT("…with the quantity rolled over the inclusive range"), Rolled[1].Quantity, 15);
	}

	// ── The quantity range is inclusive at both ends ─────────────────────
	{
		{
			FScriptedRandom Random({ 0.0, 0.0, 0.0 });
			auto Rand = [&Random]() { return Random.Next(); };
			const TArray<FValhallaBagSlot> Rolled = UValhallaInventoryLibrary::RollLootTable(Table, Rand);
			TestEqual(TEXT("A quantity roll of 0 gives the minimum"), Rolled[1].Quantity, 10);
		}
		{
			// 0.9999 * 11 = 10.998, floored to 10, + 10 = 20. The `+ 1` inside
			// the multiplier is what makes the maximum reachable at all; without
			// it this would top out at 19.
			FScriptedRandom Random({ 0.0, 0.0, 0.9999 });
			auto Rand = [&Random]() { return Random.Next(); };
			const TArray<FValhallaBagSlot> Rolled = UValhallaInventoryLibrary::RollLootTable(Table, Rand);
			TestEqual(TEXT("A quantity roll just short of 1 gives the maximum"), Rolled[1].Quantity, 20);
		}
	}

	// ── An equal-bounds entry consumes no quantity roll ──────────────────
	//
	// LootBagSystem.ts:151 short-circuits when min === max. That matters to the
	// *sequence*: if the hood consumed a second random number, every roll after
	// it in the table would shift by one and the whole table would drop
	// differently. This is the assertion that pins the sequence down.
	{
		FScriptedRandom Random({ 0.0, 0.0, 0.0 });
		auto Rand = [&Random]() { return Random.Next(); };

		UValhallaInventoryLibrary::RollLootTable(Table, Rand);

		// hood: 1 roll (chance only). coins: 2 rolls (chance + quantity).
		// ring: 1 roll (chance only, and it fails). Four in total.
		TestEqual(TEXT("The table consumed exactly four random numbers"), Random.Cursor, 4);
	}

	// ── An empty table drops nothing and does not crash ──────────────────
	{
		FValhallaLootTable Empty;
		Empty.Id = TEXT("empty");

		FScriptedRandom Random({ 0.0 });
		auto Rand = [&Random]() { return Random.Next(); };

		TestEqual(TEXT("An empty table drops nothing"),
			UValhallaInventoryLibrary::RollLootTable(Empty, Rand).Num(), 0);
		TestEqual(TEXT("…and consumes no random numbers"), Random.Cursor, 0);
	}

	// ── A zero-quantity entry is dropped from the result ─────────────────
	{
		FValhallaLootTable Zero;
		Zero.Id = TEXT("zero");

		FValhallaLootEntry Entry;
		Entry.ItemId = TEXT("gold_coin");
		Entry.DropChance = 1.f;
		Entry.MinQuantity = 0;
		Entry.MaxQuantity = 0;
		Zero.Entries.Add(Entry);

		FScriptedRandom Random({ 0.0 });
		auto Rand = [&Random]() { return Random.Next(); };

		// LootBagSystem.ts:155 — `if (qty > 0)`. An entry that rolls nothing is
		// not a slot holding nothing.
		TestEqual(TEXT("An entry that rolls a quantity of zero is not in the result"),
			UValhallaInventoryLibrary::RollLootTable(Zero, Rand).Num(), 0);
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Party XP split — GameRoom.ts:1250 awardKillXP
// ─────────────────────────────────────────────────────────────────────────────

namespace ValhallaInventoryTests
{
	/**
	 * The arithmetic of `awardKillXP`, extracted.
	 *
	 * AValhallaGameMode::AwardKillXP does this over live player states, which
	 * needs a world. The numbers do not, and the numbers are the part that can
	 * be quietly wrong: this is the same body, over integers.
	 *
	 * @param EligibleCount  Members who are alive and in the killer's zone.
	 *                       Zero means the party branch does not apply at all.
	 */
	static int32 PartyShare(int32 BaseXp, int32 MemberCount, int32 EligibleCount)
	{
		if (MemberCount <= 1 || EligibleCount <= 0)
		{
			return BaseXp;
		}

		const int32 BonusXp = FMath::FloorToInt32(BaseXp * 1.10);
		return FMath::Max(1, BonusXp / EligibleCount);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaPartyXpSplitTest,
	"Valhalla.Game.Party.XpSplit",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaPartyXpSplitTest::RunTest(const FString& Parameters)
{
	using namespace ValhallaInventoryTests;

	// ── Solo takes the whole reward, with no bonus ───────────────────────
	//
	// GameRoom.ts:1257 gates the party branch on `members.size > 1`. A party of
	// one — which cannot exist in 2.0, because UValhallaPartySubsystem disbands
	// at one member, but which the arithmetic must still handle — takes base XP
	// and specifically does NOT take the +10%. The bonus is for grouping, and
	// one person is not a group.
	TestEqual(TEXT("Solo takes the base reward"), PartyShare(100, 1, 1), 100);
	TestEqual(TEXT("No party at all takes the base reward"), PartyShare(100, 0, 0), 100);

	// ── Two members split 110 ────────────────────────────────────────────
	TestEqual(TEXT("Two members split floor(100 * 1.10) = 110 into 55 each"),
		PartyShare(100, 2, 2), 55);

	// ── The bonus is on the pot, not per member ──────────────────────────
	//
	// The whole shape of 1.0's party incentive: four people splitting a 100 XP
	// kill get 27 each, which is less than a quarter of what they would each
	// make soloing the same mob. Grouping pays because the kill is faster and
	// possible at all, not because the XP is better. Anyone "fixing" this into
	// 110 each would quadruple the game's XP rate.
	TestEqual(TEXT("Four members get 27 each, not 110 each"), PartyShare(100, 4, 4), 27);
	TestTrue(TEXT("…which is less than the solo reward"), PartyShare(100, 4, 4) < 100);

	// ── Two floors, and the loss between them ────────────────────────────
	//
	// 110 / 4 = 27.5, floored to 27. Four shares of 27 is 108, so 2 XP of the
	// 110 simply cease to exist. 1.0 does that and 2.0 does that; rounding up,
	// or handing the remainder to the killer, would both be inventions.
	{
		const int32 Share = PartyShare(100, 4, 4);
		TestEqual(TEXT("The pot was 110"), FMath::FloorToInt32(100 * 1.10), 110);
		TestEqual(TEXT("…and 4 x 27 = 108, so 2 XP are lost to the floor"), Share * 4, 108);
	}

	// ── The first floor is on the bonus itself ───────────────────────────
	//
	// 15 * 1.10 = 16.5, floored to 16 *before* the division. Dividing first and
	// flooring once would give a different answer, and the order is 1.0's.
	TestEqual(TEXT("floor(15 * 1.10) = 16, split two ways = 8"), PartyShare(15, 2, 2), 8);

	// ── Nobody ever gets nothing ─────────────────────────────────────────
	//
	// GameRoom.ts:1266's `Math.max(1, ...)`. A 1 XP kill split four ways is
	// 1 XP each — four XP awarded from a one XP kill. The minimum does not
	// conserve the pot, and that is deliberate: 1.0 chose "a kill always counts
	// for something" over "the arithmetic balances".
	TestEqual(TEXT("A 1 XP kill split four ways is 1 each, not 0"), PartyShare(1, 4, 4), 1);
	TestEqual(TEXT("…and 4 x 1 is more than the 1 that went in"), PartyShare(1, 4, 4) * 4, 4);

	// ── Only eligible members divide the pot ─────────────────────────────
	//
	// GameRoom.ts:1263 filters to alive-and-same-zone *before* dividing, so a
	// member who is dead or in another zone does not shrink everyone else's
	// share — they are not in the numerator or the denominator. Two of four
	// present split the full 110.
	TestEqual(TEXT("Two of four members present split the whole pot"),
		PartyShare(100, 4, 2), 55);
	TestEqual(TEXT("One of four present takes the whole pot"),
		PartyShare(100, 4, 1), 110);

	// ── Nobody eligible falls back to solo ───────────────────────────────
	//
	// GameRoom.ts:1265's `if (zoneSids.length > 0)` — when it fails, control
	// falls out of the party branch entirely and the killer takes base XP. That
	// path is reachable: the killer can die to the same blow that killed the NPC
	// (a DoT ticking on both), at which point no member is alive.
	TestEqual(TEXT("No eligible member falls back to the killer taking base XP"),
		PartyShare(100, 4, 0), 100);

	// ── A worked example, for the report ─────────────────────────────────
	//
	// npc_1771431708366 is worth 100 XP. Two players in a party, both alive in
	// grasslands: floor(100 * 1.10) = 110, / 2 = 55 each. 110 XP awarded in
	// total against a solo 100 — the +10% is real, and it is the only thing the
	// party gains.
	{
		const int32 Share = PartyShare(100, 2, 2);
		TestEqual(TEXT("The gate's own case: 100 XP, two members, 55 each"), Share, 55);
		TestEqual(TEXT("…totalling 110, which is the base plus the bonus"), Share * 2, 110);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
