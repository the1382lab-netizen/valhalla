// Copyright Valhalla 2.0. All Rights Reserved.
//
// Phase 7b persistence tests.
//
// Two rules, picked on the principle every other Valhalla test was: each one
// fails *quietly*, and each one destroys something.
//
//   SaveDataJson  The save body is the only description of a character that
//                 outlives the process. A key spelled `slot` where the backend
//                 reads `slotIndex`, or an equipment array written as a map,
//                 does not error: `saveCharacter` deletes the old rows, writes
//                 none, and `saveToDisk()` commits that. The character logs
//                 back in naked and the log line still says "saved". The round
//                 trip is the property that matters, so the round trip is what
//                 is tested.
//
//   ApplyLoaded   A load that skipped the clamp gives back whatever hp the row
//                 held — including an hp from before a balance edit lowered the
//                 pool it was valid against. That is a health bar drawn off the
//                 end of itself and a regen loop that never reaches its target,
//                 both months later and neither pointing here. The same
//                 function decides whether the class kit is granted, and that
//                 decision is the difference between a login and a login that
//                 hands out a free sword.
//
// Neither needs a world, an actor or a backend: `SaveDataToJson`,
// `SaveDataFromJson` and `ResolveLoadedCharacter` are all statics over plain
// structs, for exactly this reason — the same split ResolveDamage and its
// rolls have had since Phase 1a.
//
// Run from the editor's Session Frontend, or headless:
//   UnrealEditor-Cmd.exe <project>.uproject -ExecCmds="Automation RunTests Valhalla.; Quit" -unattended -nullrhi

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ValhallaBackendSubsystem.h"
#include "ValhallaConstants.h"
#include "ValhallaInventoryLibrary.h"
#include "ValhallaStats.h"
#include "ValhallaTypes.h"

// Guarded because the other ValhallaGame test files define the same flags, and
// a unity build puts several of them in one translation unit.
#ifndef VALHALLA_GAME_TEST_FLAGS
#define VALHALLA_GAME_TEST_FLAGS ( \
	EAutomationTestFlags::EditorContext \
	| EAutomationTestFlags::ClientContext \
	| EAutomationTestFlags::ServerContext \
	| EAutomationTestFlags::CommandletContext \
	| EAutomationTestFlags::ProductFilter)
#endif

namespace ValhallaBackendTests
{
	static constexpr float Tolerance = 1e-3f;

	/** A piece of equipment that moves a stat, so the clamp has something to clamp. */
	static FValhallaItemTemplate MakeEquipment(FName Id, EValhallaEquipSlot Slot, float HpBonus, float StaminaBonus = 0.f)
	{
		FValhallaItemTemplate Item;
		Item.Id = Id;
		Item.Name = Id.ToString();
		Item.Category = EValhallaItemCategory::Equipment;
		Item.EquipSlot = Slot;
		Item.bStackable = false;
		Item.MaxStack = 1;
		Item.StatBonuses.Hp = HpBonus;
		Item.StatBonuses.Stamina = StaminaBonus;
		// SumEquipmentBonuses skips an item whose `statBonuses` key was absent
		// from items.json, and this flag is how it knows. A fixture that forgot
		// it would contribute nothing and the test would pass for the wrong
		// reason — which is exactly what happened the first time this was run.
		Item.bHasStatBonuses = true;
		return Item;
	}

	/** Enough of a class for ComputeStatsWithEquipment to have real numbers. */
	static FValhallaClassTemplate MakeClass(FName Id)
	{
		FValhallaClassTemplate ClassTemplate;
		ClassTemplate.Id = Id;
		ClassTemplate.Name = Id.ToString();
		ClassTemplate.AllowedArmor = EValhallaArmorType::Plate;
		// A non-caster, so MaxEnergy is non-zero and the "energy comes back
		// full" check is not vacuously true. MaxMana is the scaled `mana` stat
		// either way, so the mana clamp still has a real ceiling to clamp to.
		ClassTemplate.bCanUseMana = false;
		ClassTemplate.BaseSpeed = 144.f;
		ClassTemplate.BaseMeleeAttackSpeedMs = 3600.f;
		ClassTemplate.VisionRange = 1350.f;

		ClassTemplate.BaseStats.Hp = 150.f;
		ClassTemplate.BaseStats.Mana = 120.f;
		ClassTemplate.BaseStats.Strength = 20.f;
		ClassTemplate.BaseStats.Stamina = 18.f;
		ClassTemplate.BaseStats.Dexterity = 10.f;
		ClassTemplate.BaseStats.Intelligence = 22.f;
		ClassTemplate.BaseStats.PhysicalDefense = 12.f;

		return ClassTemplate;
	}

	/** JSON text -> object, so the round trip goes through the wire and not a pointer. */
	static TSharedPtr<FJsonObject> RoundTripThroughText(const TSharedRef<FJsonObject>& In)
	{
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(In, Writer);

		TSharedPtr<FJsonObject> Out;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		FJsonSerializer::Deserialize(Reader, Out);
		return Out;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Valhalla.Game.Backend.SaveDataJson
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaBackendSaveDataJsonTest,
	"Valhalla.Game.Backend.SaveDataJson",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaBackendSaveDataJsonTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ValhallaBackendTests;

	FValhallaSaveData Original;
	// Integral, because that is what comes back: `hp` and `mana` are integer
	// columns and `internal.ts`'s requireInt refuses anything else. The
	// rounding is asserted on its own below.
	Original.Hp        = 173.f;
	Original.Mana      = 41.f;
	Original.Xp        = 275;
	Original.Level     = 4;
	// Zone-local centimetres, and deliberately not round: an axis swap or a
	// float that became an int would survive (672, 672) and not these.
	Original.PositionX = 1234.5;
	Original.PositionY = 3987.25;
	Original.ZoneId    = TEXT("desert");
	Original.bAlive    = true;

	Original.Inventory.Add(FValhallaInventorySlot(TEXT("health_potion"), 5));
	Original.Inventory.Add(FValhallaInventorySlot(TEXT("iron_ore"), 1));
	Original.Inventory.Add(FValhallaInventorySlot(TEXT("rusty_dagger"), 1));

	Original.Equipment[ValhallaEquipSlotToIndex(EValhallaEquipSlot::Weapon)] = TEXT("iron_sword");
	Original.Equipment[ValhallaEquipSlotToIndex(EValhallaEquipSlot::Chest)]  = TEXT("leather_vest");
	Original.Equipment[ValhallaEquipSlotToIndex(EValhallaEquipSlot::Ring)]   = TEXT("copper_ring");

	Original.ActionBar = { TEXT("cleave"), FString(), TEXT("shield_bash") };

	// ── the shape the backend actually reads ─────────────────────────────
	const TSharedRef<FJsonObject> Json = UValhallaBackendSubsystem::SaveDataToJson(Original);

	// CharacterService.SaveCharacterData's own field names, spelled out here so
	// that renaming one in the serialiser fails the test rather than the save.
	for (const TCHAR* Field : { TEXT("hp"), TEXT("mana"), TEXT("xp"), TEXT("level"),
		TEXT("positionX"), TEXT("positionY"), TEXT("zoneId"), TEXT("alive"),
		TEXT("inventory"), TEXT("equipment"), TEXT("actionBar") })
	{
		TestTrue(FString::Printf(TEXT("the save body has '%s'"), Field), Json->HasField(Field));
	}

	// Equipment is an array of {slotType, itemId} rows — the shape
	// `saveCharacter` inserts into character_equipment — and holds only the
	// occupied slots, not nine rows with six empty strings.
	const TArray<TSharedPtr<FJsonValue>>* EquipArray = nullptr;
	if (TestTrue(TEXT("equipment is an array"), Json->TryGetArrayField(TEXT("equipment"), EquipArray)) && EquipArray)
	{
		TestEqual(TEXT("only the occupied equip slots are written"), EquipArray->Num(), 3);

		const TSharedPtr<FJsonObject>* First = nullptr;
		if ((*EquipArray)[0]->TryGetObject(First) && First)
		{
			TestTrue(TEXT("an equipment row has slotType"), (*First)->HasField(TEXT("slotType")));
			TestTrue(TEXT("an equipment row has itemId"), (*First)->HasField(TEXT("itemId")));
		}
	}

	// Inventory rows carry `slotIndex`, which is the dense array's index.
	const TArray<TSharedPtr<FJsonValue>>* InvArray = nullptr;
	if (TestTrue(TEXT("inventory is an array"), Json->TryGetArrayField(TEXT("inventory"), InvArray)) && InvArray)
	{
		TestEqual(TEXT("every occupied inventory slot is written"), InvArray->Num(), 3);

		const TSharedPtr<FJsonObject>* Last = nullptr;
		if ((*InvArray)[2]->TryGetObject(Last) && Last)
		{
			double SlotIndex = -1.0;
			(*Last)->TryGetNumberField(TEXT("slotIndex"), SlotIndex);
			TestEqual(TEXT("the third inventory row is slotIndex 2"), static_cast<int32>(SlotIndex), 2);
		}
	}

	// ── and back again, through actual text ──────────────────────────────
	const TSharedPtr<FJsonObject> Reparsed = RoundTripThroughText(Json);
	TestTrue(TEXT("the serialised body parses"), Reparsed.IsValid());

	FValhallaSaveData Returned;
	TestTrue(TEXT("SaveDataFromJson accepts it"), UValhallaBackendSubsystem::SaveDataFromJson(Reparsed, Returned));

	TestEqual(TEXT("hp survives"), Returned.Hp, Original.Hp, Tolerance);
	TestEqual(TEXT("mana survives"), Returned.Mana, Original.Mana, Tolerance);
	TestEqual(TEXT("xp survives"), Returned.Xp, Original.Xp);
	TestEqual(TEXT("level survives"), Returned.Level, Original.Level);
	TestEqual(TEXT("positionX survives"), Returned.PositionX, Original.PositionX, 1e-6);
	TestEqual(TEXT("positionY survives"), Returned.PositionY, Original.PositionY, 1e-6);
	TestEqual(TEXT("zoneId survives"), Returned.ZoneId, Original.ZoneId);
	TestEqual(TEXT("alive survives"), Returned.bAlive, Original.bAlive);

	TestEqual(TEXT("every inventory slot survives"), Returned.Inventory.Num(), Original.Inventory.Num());
	for (int32 Index = 0; Index < Original.Inventory.Num() && Index < Returned.Inventory.Num(); ++Index)
	{
		TestEqual(FString::Printf(TEXT("inventory[%d] item survives"), Index),
			Returned.Inventory[Index].ItemId, Original.Inventory[Index].ItemId);
		TestEqual(FString::Printf(TEXT("inventory[%d] quantity survives"), Index),
			Returned.Inventory[Index].Quantity, Original.Inventory[Index].Quantity);
	}

	for (int32 Index = 0; Index < ValhallaEquipSlotCount; ++Index)
	{
		TestEqual(FString::Printf(TEXT("equipment slot %d survives"), Index),
			Returned.Equipment[Index], Original.Equipment[Index]);
	}

	TestEqual(TEXT("the action bar survives"), Returned.ActionBar.Num(), Original.ActionBar.Num());
	if (Returned.ActionBar.Num() == Original.ActionBar.Num())
	{
		for (int32 Index = 0; Index < Original.ActionBar.Num(); ++Index)
		{
			TestEqual(FString::Printf(TEXT("actionBar[%d] survives"), Index),
				Returned.ActionBar[Index], Original.ActionBar[Index]);
		}
	}

	// ── hp and mana are integers on the wire ─────────────────────────────
	//
	// `internal.ts` validates them with requireInt and answers 400 otherwise,
	// and a mid-regen hp is a fraction almost all the time — so a save that
	// wrote the float would fail far more often than it succeeded, and the
	// only symptom would be a character that stopped persisting.
	{
		FValhallaSaveData Fractional = Original;
		Fractional.Hp   = 173.6f;
		Fractional.Mana = 41.2f;

		const TSharedRef<FJsonObject> Rounded = UValhallaBackendSubsystem::SaveDataToJson(Fractional);
		double Hp = 0.0;
		double Mana = 0.0;
		Rounded->TryGetNumberField(TEXT("hp"), Hp);
		Rounded->TryGetNumberField(TEXT("mana"), Mana);

		TestEqual(TEXT("hp is rounded to an integer"), Hp, 174.0, 1e-9);
		TestEqual(TEXT("mana is rounded to an integer"), Mana, 41.0, 1e-9);
	}

	// ── the other equipment spelling the contract allows ─────────────────
	//
	// `{weapon: "…", helm: "…"}` rather than a row array. A backend that
	// returned that shape and a client that only read the other would load
	// every character with no gear and no error anywhere.
	{
		const TSharedRef<FJsonObject> MapShape = MakeShared<FJsonObject>();
		const TSharedRef<FJsonObject> EquipMap = MakeShared<FJsonObject>();
		EquipMap->SetStringField(TEXT("weapon"), TEXT("oak_staff"));
		EquipMap->SetStringField(TEXT("boots"), TEXT("worn_boots"));
		MapShape->SetObjectField(TEXT("equipment"), EquipMap);
		MapShape->SetNumberField(TEXT("level"), 2);

		FValhallaSaveData FromMap;
		TestTrue(TEXT("an object-map equipment body parses"),
			UValhallaBackendSubsystem::SaveDataFromJson(MapShape, FromMap));
		TestEqual(TEXT("the mapped weapon lands in the weapon slot"),
			FromMap.Equipment[ValhallaEquipSlotToIndex(EValhallaEquipSlot::Weapon)], FName(TEXT("oak_staff")));
		TestEqual(TEXT("the mapped boots land in the boots slot"),
			FromMap.Equipment[ValhallaEquipSlotToIndex(EValhallaEquipSlot::Boots)], FName(TEXT("worn_boots")));
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Valhalla.Game.Backend.ApplyLoaded
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaBackendApplyLoadedTest,
	"Valhalla.Game.Backend.ApplyLoaded",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaBackendApplyLoadedTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ValhallaBackendTests;

	const FValhallaClassTemplate ClassTemplate = MakeClass(TEXT("ranger"));

	TMap<FName, FValhallaItemTemplate> Catalog;
	Catalog.Add(TEXT("stout_vest"), MakeEquipment(TEXT("stout_vest"), EValhallaEquipSlot::Chest, /*Hp=*/40.f, /*Stamina=*/6.f));
	Catalog.Add(TEXT("yew_bow"), MakeEquipment(TEXT("yew_bow"), EValhallaEquipSlot::Weapon, /*Hp=*/0.f));

	auto FindItem = [&Catalog](FName Id) -> const FValhallaItemTemplate* { return Catalog.Find(Id); };

	// ── a played character ───────────────────────────────────────────────
	FValhallaLoadedCharacter Loaded;
	Loaded.Id        = 17;
	Loaded.UserId    = 3;
	Loaded.Name      = TEXT("Sigrun");
	Loaded.ClassId   = TEXT("ranger");
	Loaded.Level     = 6;
	Loaded.Xp        = 410;
	Loaded.ZoneId    = TEXT("desert");
	Loaded.PositionX = 1024.0;
	Loaded.PositionY = 2048.0;
	Loaded.bAlive    = true;
	Loaded.Inventory.Add(FValhallaInventorySlot(TEXT("health_potion"), 3));
	Loaded.Equipment[ValhallaEquipSlotToIndex(EValhallaEquipSlot::Chest)]  = TEXT("stout_vest");
	Loaded.Equipment[ValhallaEquipSlotToIndex(EValhallaEquipSlot::Weapon)] = TEXT("yew_bow");

	// The saved pools are absurd on purpose: a row written before a balance
	// edit, or by a 1.0 server whose numbers were different. Both are the case
	// the clamp exists for.
	Loaded.Hp   = 99999.f;
	Loaded.Mana = 99999.f;

	FValhallaAppliedCharacter Applied;
	UValhallaBackendSubsystem::ResolveLoadedCharacter(Loaded, ClassTemplate, FindItem, Applied);

	// The stat block is the *equipped* one, not the bare class — a load that
	// resolved the class alone would clamp against the wrong ceiling and the
	// error would be exactly the size of the gear bonus.
	const FValhallaResolvedStats Expected = UValhallaInventoryLibrary::ComputeStatsWithEquipment(
		ClassTemplate, Loaded.Level, Loaded.Equipment, FindItem);

	TestEqual(TEXT("the name comes across"), Applied.CharacterName, Loaded.Name);
	TestEqual(TEXT("the class comes across"), Applied.ClassId, ClassTemplate.Id);
	TestEqual(TEXT("the level comes across"), Applied.Level, Loaded.Level);
	TestEqual(TEXT("the xp comes across"), Applied.Xp, Loaded.Xp);
	TestEqual(TEXT("the zone comes across"), Applied.ZoneId, Loaded.ZoneId);
	TestEqual(TEXT("the inventory comes across"), Applied.Inventory.Num(), 1);
	TestEqual(TEXT("the chest slot comes across"),
		Applied.Equipment[ValhallaEquipSlotToIndex(EValhallaEquipSlot::Chest)], FName(TEXT("stout_vest")));
	TestEqual(TEXT("the equipment array is nine wide"), Applied.Equipment.Num(), ValhallaEquipSlotCount);

	TestEqual(TEXT("MaxHp is the equipped block's"), Applied.MaxHp, Expected.MaxHp, Tolerance);
	TestEqual(TEXT("MaxMana is the equipped block's"), Applied.MaxMana, Expected.MaxMana, Tolerance);
	TestEqual(TEXT("vision range comes from the class template"), Applied.VisionRange, ClassTemplate.VisionRange, Tolerance);

	// The clamp, which is the point of the test. The ceilings are real numbers,
	// not zero — see MakeClass for why the fixture is a non-caster.
	TestTrue(TEXT("MaxHp is a real ceiling"), Applied.MaxHp > 0.f);
	TestTrue(TEXT("MaxMana is a real ceiling"), Applied.MaxMana > 0.f);
	TestEqual(TEXT("an over-large saved hp is clamped to MaxHp"), Applied.Hp, Applied.MaxHp, Tolerance);
	TestEqual(TEXT("an over-large saved mana is clamped to MaxMana"), Applied.Mana, Applied.MaxMana, Tolerance);

	// The equipment bonus lands in the StatBlock half and *not* in MaxHp, and
	// that asymmetry is 1.0's, not an oversight: GameScene.ts:4294 adds an
	// item's `hp` bonus to the stat and never to the pool ceiling. Pinned here
	// because a load is the one place somebody would be tempted to "fix" it,
	// and doing so would silently regrade every character's health bar.
	TestEqual(TEXT("the chest hp bonus lands in the stat block"),
		Applied.Stats.Hp, ClassTemplate.BaseStats.Hp + 40.f, Tolerance);
	TestEqual(TEXT("the chest stamina bonus lands in the stat block"),
		Applied.Stats.Stamina, ClassTemplate.BaseStats.Stamina + 6.f, Tolerance);
	TestEqual(TEXT("an hp bonus does NOT raise MaxHp — see the comment"),
		Applied.MaxHp, ClassTemplate.BaseStats.Hp, Tolerance);

	// Energy is not a saved column; a load fills the bar, as GameRoom.onJoin did.
	TestEqual(TEXT("energy comes back full"), Applied.Energy, Applied.MaxEnergy, Tolerance);

	// A played character never gets the class kit again.
	TestFalse(TEXT("a level 6 character with gear does not get starting items"), Applied.bNeedsStartingItems);

	// ── a saved hp below zero, and a negative xp ─────────────────────────
	{
		FValhallaLoadedCharacter Corrupt = Loaded;
		Corrupt.Hp = -25.f;
		Corrupt.Mana = -1.f;
		Corrupt.Xp = -100;

		FValhallaAppliedCharacter Clamped;
		UValhallaBackendSubsystem::ResolveLoadedCharacter(Corrupt, ClassTemplate, FindItem, Clamped);

		TestEqual(TEXT("a negative saved hp clamps to zero"), Clamped.Hp, 0.f, Tolerance);
		TestEqual(TEXT("a negative saved mana clamps to zero"), Clamped.Mana, 0.f, Tolerance);
		TestEqual(TEXT("a negative saved xp clamps to zero"), Clamped.Xp, 0);
	}

	// ── a level above the cap ────────────────────────────────────────────
	{
		FValhallaLoadedCharacter TooHigh = Loaded;
		TooHigh.Level = Valhalla::MaxLevel + 40;

		FValhallaAppliedCharacter Capped;
		UValhallaBackendSubsystem::ResolveLoadedCharacter(TooHigh, ClassTemplate, FindItem, Capped);
		TestEqual(TEXT("a level above MaxLevel is capped, not believed"), Capped.Level, Valhalla::MaxLevel);
	}

	// ── the first-entry case ─────────────────────────────────────────────
	//
	// No inventory, no equipment, level 1. CharacterService.createCharacter
	// writes the kit at creation, so this is a row from before that path —
	// and it is the *only* shape that may be granted a second kit. Anything
	// else here would hand every login a free sword.
	{
		FValhallaLoadedCharacter Fresh;
		Fresh.Id = 42;
		Fresh.ClassId = TEXT("ranger");
		Fresh.Name = TEXT("Nobody");
		Fresh.Level = 1;

		FValhallaAppliedCharacter FreshApplied;
		UValhallaBackendSubsystem::ResolveLoadedCharacter(Fresh, ClassTemplate, FindItem, FreshApplied);
		TestTrue(TEXT("an empty level 1 row asks for the class kit"), FreshApplied.bNeedsStartingItems);
		// The row's saved hp is 0, and a load does not invent one: Phase 2b's
		// respawn is what stands a character up, not the load.
		TestEqual(TEXT("a zero saved hp is carried, not invented"), FreshApplied.Hp, 0.f, Tolerance);
	}

	// A level 1 row with a single inventory item has been played and must not
	// be re-granted — the `AND` in the test is what this pins.
	{
		FValhallaLoadedCharacter Started;
		Started.Id = 43;
		Started.ClassId = TEXT("ranger");
		Started.Level = 1;
		Started.Inventory.Add(FValhallaInventorySlot(TEXT("health_potion"), 1));

		FValhallaAppliedCharacter StartedApplied;
		UValhallaBackendSubsystem::ResolveLoadedCharacter(Started, ClassTemplate, FindItem, StartedApplied);
		TestFalse(TEXT("a level 1 row that owns something is not granted a kit"), StartedApplied.bNeedsStartingItems);
	}

	// The same, one equipped item and an empty bag.
	{
		FValhallaLoadedCharacter Armed;
		Armed.Id = 44;
		Armed.ClassId = TEXT("ranger");
		Armed.Level = 1;
		Armed.Equipment[ValhallaEquipSlotToIndex(EValhallaEquipSlot::Weapon)] = TEXT("yew_bow");

		FValhallaAppliedCharacter ArmedApplied;
		UValhallaBackendSubsystem::ResolveLoadedCharacter(Armed, ClassTemplate, FindItem, ArmedApplied);
		TestFalse(TEXT("a level 1 row that has equipment is not granted a kit"), ArmedApplied.bNeedsStartingItems);
	}

	return true;
}


// ─────────────────────────────────────────────────────────────────────────────
//  Valhalla.Game.Backend.CharacterSummary (B-08a)
// ─────────────────────────────────────────────────────────────────────────────
//
// The character select screen draws each row's zone and dresses its preview
// from the list entry. A backend from before B-08a sends none of that and must
// still list characters; one after it must fill every slot it names.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaBackendCharacterSummaryTest,
	"Valhalla.Game.Backend.CharacterSummary",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaBackendCharacterSummaryTest::RunTest(const FString& /*Parameters*/)
{
	auto Parse = [](const TCHAR* Text) -> FValhallaCharacterSummary
	{
		TSharedPtr<FJsonObject> Json;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FString(Text));
		FJsonSerializer::Deserialize(Reader, Json);
		return UValhallaBackendSubsystem::CharacterSummaryFromJson(Json);
	};

	{
		const FValhallaCharacterSummary Old = Parse(TEXT("{\"id\":5,\"name\":\"Brynja\",\"classId\":\"cleric\",\"level\":3}"));
		TestEqual(TEXT("old body: id"), Old.Id, 5);
		TestEqual(TEXT("old body: class"), Old.ClassId, FName(TEXT("cleric")));
		TestTrue(TEXT("old body: no zone"), Old.ZoneId.IsNone());
		TestTrue(TEXT("old body: no body id"), Old.BodyId.IsEmpty());
		TestEqual(TEXT("old body: no equipment array"), Old.Equipment.Num(), 0);
	}

	{
		const FValhallaCharacterSummary New = Parse(TEXT(
			"{\"id\":9,\"name\":\"Sigrun\",\"classId\":\"ranger\",\"level\":6,\"zoneId\":\"grasslands\",\"bodyId\":\"body_tan\","
			"\"equipment\":[{\"slotType\":\"chest\",\"itemId\":\"stout_vest\"},{\"slotType\":\"weapon\",\"itemId\":\"yew_bow\"}]}"));
		TestEqual(TEXT("new body: zone"), New.ZoneId, FName(TEXT("grasslands")));
		TestEqual(TEXT("new body: body id"), New.BodyId, FString(TEXT("body_tan")));
		TestEqual(TEXT("new body: one entry per slot"), New.Equipment.Num(), static_cast<int32>(ValhallaEquipSlotCount));
		if (New.Equipment.Num() == ValhallaEquipSlotCount)
		{
			TestEqual(TEXT("new body: chest"), New.Equipment[ValhallaEquipSlotToIndex(EValhallaEquipSlot::Chest)], FName(TEXT("stout_vest")));
			TestEqual(TEXT("new body: weapon"), New.Equipment[ValhallaEquipSlotToIndex(EValhallaEquipSlot::Weapon)], FName(TEXT("yew_bow")));
		}

		const FValhallaCharacterSummary Empty = Parse(TEXT("{\"id\":10,\"name\":\"Ulf\",\"classId\":\"warrior\",\"level\":1,\"equipment\":[]}"));
		TestEqual(TEXT("empty equipment: still one entry per slot"), Empty.Equipment.Num(), static_cast<int32>(ValhallaEquipSlotCount));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
