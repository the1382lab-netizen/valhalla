// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ValhallaInventoryTypes.h"
#include "ValhallaTypes.h"
#include "ValhallaInventoryLibrary.generated.h"

/** Log for inventory, equipment and loot. Its stat lines are the audit trail. */
VALHALLAGAME_API DECLARE_LOG_CATEGORY_EXTERN(LogValhallaInventory, Log, All);

/**
 * How many equip slots there are. items.ts:78 `EQUIP_SLOTS`.
 *
 * Nine, and exactly one ring: 1.0's `EQUIP_SLOT_FIELD` (items.ts:109) names a
 * single `equipRing` field, so 2.0 has a single ring slot too. A second ring is
 * a data and schema change in both projects, not something 2.0 may invent.
 */
inline constexpr int32 ValhallaEquipSlotCount = 9;

/** EValhallaEquipSlot -> 0..8, for the flat equipment array the pure functions take. */
VALHALLAGAME_API int32 ValhallaEquipSlotToIndex(EValhallaEquipSlot Slot);

/** The inverse. Returns None for an out-of-range index. */
VALHALLAGAME_API EValhallaEquipSlot ValhallaEquipSlotFromIndex(int32 Index);

/**
 * How a pure function reaches items.json without owning a subsystem pointer.
 *
 * Every function below takes one of these instead of calling
 * `UValhallaDataSubsystem::FindItem` itself, which is the whole reason they can
 * run in the automation tests with no world, no game instance and no data
 * loaded. It is the same trick UValhallaCombatLibrary::ApplyCooldown uses for
 * skills.
 */
using FValhallaItemLookup = TFunctionRef<const FValhallaItemTemplate*(FName)>;

/**
 * The port of `InventorySystem.ts`, plus the two halves of `LootBagSystem.ts`
 * that are rules rather than entity management.
 *
 * 1.0 wrote these as free functions over a `PlayerState` that happened to be a
 * Colyseus schema; here they are statics over a plain `TArray`, which is the
 * same thing with the networking taken out. Nothing in this file touches an
 * actor, a world or a subsystem, and nothing in it may start to: these are the
 * functions the automation tests pin down, and a test that needs a world is a
 * test nobody runs.
 *
 * ── On the inventory being a dense array ────────────────────────────────────
 *
 * 1.0's inventory is a Colyseus `ArraySchema` that is pushed onto and spliced
 * out of: an empty slot is an *absent element*, not a blank one, and
 * `INVENTORY_MAX_SLOTS` is a cap on `length`. Every function in
 * InventorySystem.ts is written against that — `addItem` fails when
 * `length >= 32`, `removeItem` splices the element away, `swapInventorySlots`
 * swaps two occupied indices, `unequipItemToSlot` distinguishes "the target
 * index holds something" from "the target index is past the end".
 *
 * A fixed 32-entry array with holes would quietly change all four. 2.0
 * therefore keeps the dense model and caps the array at
 * Valhalla::InventoryMaxSlots. Phase 8's UI draws a 32-cell grid over it, which
 * is exactly what the 1.0 client does.
 */
UCLASS()
class VALHALLAGAME_API UValhallaInventoryLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ── Inventory — InventorySystem.ts:20-102 ───────────────────────────

	/**
	 * InventorySystem.ts:20 `addItem`. Fills existing stacks first when the item
	 * is stackable, then takes empty slots.
	 *
	 * All-or-nothing is *not* the 1.0 behaviour and is not reproduced: a partial
	 * add that runs out of room returns false having already placed what fit,
	 * exactly as 1.0 does. Callers that care (LootItem) check first.
	 *
	 * @return false when the item is unknown, or when the inventory filled up.
	 */
	static bool AddItem(TArray<FValhallaInventorySlot>& Inventory, FName ItemId, int32 Quantity, FValhallaItemLookup FindItem);

	/** InventorySystem.ts:67 `removeItem`. Removes the slot entirely when it empties. */
	static bool RemoveItem(TArray<FValhallaInventorySlot>& Inventory, int32 SlotIndex, int32 Quantity = 1);

	/** InventorySystem.ts:84 `hasItem`. */
	static bool HasItem(const TArray<FValhallaInventorySlot>& Inventory, FName ItemId);

	/** InventorySystem.ts:94 `getItemCount`. */
	static int32 GetItemCount(const TArray<FValhallaInventorySlot>& Inventory, FName ItemId);

	/** InventorySystem.ts:153 `swapInventorySlots`. Both indices must be occupied. */
	static bool SwapInventorySlots(TArray<FValhallaInventorySlot>& Inventory, int32 FromIndex, int32 ToIndex);

	/** InventorySystem.ts:192 `dropInventoryItem`. The whole stack leaves. */
	static bool DropInventoryItem(TArray<FValhallaInventorySlot>& Inventory, int32 SlotIndex, FValhallaInventorySlot& OutDropped);

	// ── Equipment — InventorySystem.ts:122-279 ──────────────────────────

	/**
	 * Whether an item may go into an equip slot at all, and why not.
	 *
	 * 1.0's `equipItem` tests one thing: that the template has an `equipSlot`
	 * (InventorySystem.ts:127). The other two tests here are 2.0 belt-and-braces
	 * against a data edit rather than ports — no item in items.json is both
	 * stackable and equippable, or equippable and not `equipment` — and they
	 * fail closed, which the single 1.0 test does not.
	 *
	 * ── On `allowedArmor` ───────────────────────────────────────────────────
	 * There is deliberately no armour-type test, because there is nothing to
	 * test against. `FValhallaClassTemplate::AllowedArmor` exists and is loaded,
	 * but no item in items.json or items.ts carries an armour type, and the only
	 * read of `allowedArmor` in all of 1.0 is ClassSelectScene.ts:160, which
	 * prints it on the class-select screen. No 1.0 server path has ever stopped
	 * a wizard wearing chainmail. Adding the rule here would need an `armorType`
	 * field on the item template in *both* projects and a pass over the catalog;
	 * that is a data change, and data is the 1.0 repo's to make.
	 *
	 * @return true when the item may be equipped. OutReason carries the 1.0-style
	 *         refusal text otherwise.
	 */
	static bool CanEquip(const FValhallaItemTemplate& Item, const FValhallaClassTemplate& ClassTemplate, FString& OutReason);

	/**
	 * InventorySystem.ts:122 `equipItem`. Takes the item out of the inventory and
	 * into its slot; anything already in that slot goes back to the end of the
	 * inventory.
	 *
	 * @param Equipment  ValhallaEquipSlotCount entries, indexed by ValhallaEquipSlotToIndex.
	 */
	static bool EquipItem(
		TArray<FValhallaInventorySlot>& Inventory,
		TArray<FName>& Equipment,
		int32 InventorySlotIndex,
		const FValhallaClassTemplate& ClassTemplate,
		FValhallaItemLookup FindItem,
		FString& OutReason);

	/** InventorySystem.ts:215 `unequipItem`. Appends to the end of the inventory. */
	static bool UnequipItem(TArray<FValhallaInventorySlot>& Inventory, TArray<FName>& Equipment, EValhallaEquipSlot Slot);

	/**
	 * InventorySystem.ts:240 `unequipItemToSlot`.
	 *
	 * When the target index holds an item that fits the same equip slot, the two
	 * trade places — that is the drag-a-sword-onto-a-sword gesture. Anything else
	 * falls through to appending at the end.
	 */
	static bool UnequipItemToSlot(
		TArray<FValhallaInventorySlot>& Inventory,
		TArray<FName>& Equipment,
		EValhallaEquipSlot Slot,
		int32 TargetIndex,
		FValhallaItemLookup FindItem);

	/** InventorySystem.ts:204 `dropEquippedItem`. */
	static bool DropEquippedItem(TArray<FName>& Equipment, EValhallaEquipSlot Slot, FValhallaInventorySlot& OutDropped);

	// ── Stats — GameScene.ts:4285 ───────────────────────────────────────

	/**
	 * The class's stats at a level, plus every equipped item's `statBonuses`.
	 *
	 * ── Where this formula comes from ───────────────────────────────────────
	 * Nowhere on the 1.0 server, which is the point. The only summation of
	 * `statBonuses` in all of 1.0 is client/src/scenes/GameScene.ts:4285-4303,
	 * in the character panel, under the comment "display-only — server is
	 * authoritative". The server never actually applies it: `computeDerivedStats`
	 * (stats.ts:25) takes a class and a level and nothing else, and
	 * `CharacterService` and `GameRoom` only ever call it with those two. A 1.0
	 * warrior's Iron Sword is +3 Strength on the character sheet and +0 Strength
	 * in every damage roll.
	 *
	 * So this is the client loop, ported and made authoritative — flat addition
	 * into the resolved block, and only for a key that exists in it, which is
	 * what `if (stat in finalStats)` on line 4296 says. The two consequences of
	 * porting it *exactly* are worth knowing:
	 *
	 *   - A `hp` or `mana` bonus moves `Hp`/`Mana` and NOT `MaxHp`/`MaxMana`,
	 *     because line 4291 spreads `baseStats` (in which `maxHp` is already a
	 *     separate, already-computed key) and then adds into the `StatBlock`
	 *     names only. No item in the catalog has one, so nothing exercises it.
	 *   - `blockRating` is a 0..1 probability, and `iron_buckler` /
	 *     `iron_kite_shield` in items.json give +3 and +5 of it. That is a data
	 *     bug in 1.0 that was invisible while nothing summed the bonuses, and it
	 *     becomes a 100%-block shield the moment something does. Left as-is and
	 *     flagged rather than silently rescaled: 2.0 does not get to retune 1.0's
	 *     balance data from the port.
	 */
	static FValhallaResolvedStats ComputeStatsWithEquipment(
		const FValhallaClassTemplate& ClassTemplate,
		int32 Level,
		const TArray<FName>& Equipment,
		FValhallaItemLookup FindItem);

	/** The `statBonuses` of everything equipped, summed. Exposed for the log line. */
	static FValhallaStatBlock SumEquipmentBonuses(const TArray<FName>& Equipment, FValhallaItemLookup FindItem);

	// ── Loot — LootBagSystem.ts:62-237 ──────────────────────────────────

	/**
	 * LootBagSystem.ts:140 `rollLootTable`.
	 *
	 * Each entry is rolled *independently*: `dropChance` decides whether it drops
	 * at all, then the quantity is uniform over [min, max]. `weight` is read and
	 * ignored — it is in the schema and in the editor UI, and `rollLootTable`
	 * never looks at it, because the table is not a weighted pick of one entry.
	 * Ported as-is; a 2.0 that started honouring `weight` would drop different
	 * loot from the same table.
	 *
	 * @param Rand  Supplies `Math.random()`. Injected so the test can seed it —
	 *              the same reason ResolveDamage takes its rolls.
	 */
	static TArray<FValhallaBagSlot> RollLootTable(const FValhallaLootTable& Table, TFunctionRef<double()> Rand);

	/** LootBagSystem.ts:200 `addItemsToBag`. Stacks first, then new slots, capped at LootBagMaxSlots. */
	static void AddItemsToBag(TArray<FValhallaBagSlot>& Bag, const TArray<FValhallaBagSlot>& Items, FValhallaItemLookup FindItem);

	/** LootBagSystem.ts:62 `lootItem`, without the reach check — the caller owns that. */
	static bool LootItem(
		TArray<FValhallaBagSlot>& Bag,
		TArray<FValhallaInventorySlot>& Inventory,
		int32 SlotIndex,
		int32 Quantity,
		FValhallaItemLookup FindItem);

	/** LootBagSystem.ts:93 `lootAll`. Returns how many bag slots were taken. */
	static int32 LootAll(
		TArray<FValhallaBagSlot>& Bag,
		TArray<FValhallaInventorySlot>& Inventory,
		FValhallaItemLookup FindItem);

	// ── Names ───────────────────────────────────────────────────────────

	/** "weapon" -> EValhallaEquipSlot::Weapon. items.ts:66. Case-insensitive. */
	static EValhallaEquipSlot ParseEquipSlotName(FName Name);

	/** The inverse, in the 1.0 spelling. */
	static FName EquipSlotToName(EValhallaEquipSlot Slot);
};
