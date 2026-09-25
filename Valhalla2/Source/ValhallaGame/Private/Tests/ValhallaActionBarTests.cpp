// Copyright Valhalla 2.0. All Rights Reserved.
//
// Action bar tests (Kevin, 2026-09-24: the bar starts empty, the player drags
// skills onto it from the skills pane and drags them off to remove them).
//
//   FreshComponentIsEmpty  a new UValhallaSkillComponent has eight empty slots,
//                          saves as eight "", and an empty save keeps it empty.
//   PlacementRule          the static CanPlaceOnActionBar the server and the
//                          HUD both go through: own class and unlocked, or
//                          cross-class; never another class's, never locked.
//   SavedBarFilter         BuildActionBarFromSave (ApplySavedActionBar's body):
//                          slots keep their positions, "" stays empty, a skill
//                          that fails the rule is left empty, past eight is
//                          ignored; and it round-trips through GetActionBarForSave.
//
// None needs a world: the rule and the save filter are static and take
// hand-built skill templates, and the component is created with no owner (so
// its member CanPlaceOnActionBar, which needs a player state and the data
// subsystem, is not exercised here; it is a three-line wrapper over the static
// rule). The drag and drop itself is UMG and is checked in play.
//
// Run from the editor's Session Frontend, or headless:
//   UnrealEditor-Cmd.exe <project>.uproject -ExecCmds="Automation RunTests Valhalla.Game.ActionBar; Quit" -unattended -nullrhi

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "ValhallaSkillComponent.h"
#include "ValhallaTypes.h"

// Guarded because the other ValhallaGame test files define the same flags, and
// a unity build puts them in one translation unit.
#ifndef VALHALLA_GAME_TEST_FLAGS
#define VALHALLA_GAME_TEST_FLAGS ( \
	EAutomationTestFlags::EditorContext \
	| EAutomationTestFlags::ClientContext \
	| EAutomationTestFlags::ServerContext \
	| EAutomationTestFlags::CommandletContext \
	| EAutomationTestFlags::ProductFilter)
#endif

namespace ValhallaActionBarTests
{
	/** A skill template with only the fields the placement rule reads. */
	FValhallaSkillTemplate MakeSkill(const TCHAR* Id, const TCHAR* ClassId, int32 LevelRequired)
	{
		FValhallaSkillTemplate Skill;
		Skill.Id = FName(Id);
		Skill.ClassId = ClassId ? FName(ClassId) : NAME_None;
		Skill.LevelRequired = LevelRequired;
		return Skill;
	}

	/** A small skills.json: the cross-class melee, three warrior skills (one locked until 8), a wizard one. */
	TMap<FName, FValhallaSkillTemplate> MakeSkillTable()
	{
		TMap<FName, FValhallaSkillTemplate> Table;
		for (const FValhallaSkillTemplate& Skill : {
				MakeSkill(TEXT("melee_attack"), nullptr, 1),
				MakeSkill(TEXT("warrior_shield_bash"), TEXT("warrior"), 1),
				MakeSkill(TEXT("warrior_taunt"), TEXT("warrior"), 2),
				MakeSkill(TEXT("warrior_charge"), TEXT("warrior"), 8),
				MakeSkill(TEXT("wizard_fireball"), TEXT("wizard"), 1) })
		{
			Table.Add(Skill.Id, Skill);
		}
		return Table;
	}

	/** classSkills.warrior, in pane order. */
	TArray<FName> WarriorClassSkills()
	{
		return { FName(TEXT("melee_attack")), FName(TEXT("warrior_shield_bash")), FName(TEXT("warrior_taunt")), FName(TEXT("warrior_charge")) };
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Valhalla.Game.ActionBar.FreshComponentIsEmpty
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaActionBarFreshComponentTest,
	"Valhalla.Game.ActionBar.FreshComponentIsEmpty",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaActionBarFreshComponentTest::RunTest(const FString& /*Parameters*/)
{
	UValhallaSkillComponent* Skills = NewObject<UValhallaSkillComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!TestNotNull(TEXT("component"), Skills))
	{
		return false;
	}

	TestEqual(TEXT("eight slots"), Skills->ActionBar.Num(), ValhallaActionBarSlots);
	TestEqual(TEXT("ValhallaActionBarSlots is the 1.0 bar's eight"), ValhallaActionBarSlots, 8);
	for (int32 Slot = 1; Slot <= ValhallaActionBarSlots; ++Slot)
	{
		TestTrue(*FString::Printf(TEXT("slot %d is empty"), Slot), Skills->GetSlotSkillId(Slot).IsNone());
	}
	TestTrue(TEXT("slot 0 (out of range) reads as empty"), Skills->GetSlotSkillId(0).IsNone());
	TestTrue(TEXT("slot 9 (out of range) reads as empty"), Skills->GetSlotSkillId(ValhallaActionBarSlots + 1).IsNone());

	const TArray<FString> Saved = Skills->GetActionBarForSave();
	TestEqual(TEXT("saves as eight entries"), Saved.Num(), ValhallaActionBarSlots);
	for (int32 Index = 0; Index < Saved.Num(); ++Index)
	{
		TestTrue(*FString::Printf(TEXT("saved slot %d is \"\""), Index + 1), Saved[Index].IsEmpty());
	}

	// An empty save (a new character; the backend's eight "") and a missing
	// one leave the bar empty and eight long.
	Skills->ApplySavedActionBar(TArray<FString>{ FString(), FString(), FString(), FString(), FString(), FString(), FString(), FString() });
	TestEqual(TEXT("eight \"\" -> eight slots"), Skills->ActionBar.Num(), ValhallaActionBarSlots);
	TestEqual(TEXT("eight \"\" -> nothing on the bar"), Skills->ActionBar.FindLastByPredicate([](const FName& Id) { return !Id.IsNone(); }), static_cast<int32>(INDEX_NONE));
	Skills->ApplySavedActionBar(TArray<FString>());
	TestEqual(TEXT("no save -> eight slots"), Skills->ActionBar.Num(), ValhallaActionBarSlots);
	TestEqual(TEXT("no save -> nothing on the bar"), Skills->ActionBar.FindLastByPredicate([](const FName& Id) { return !Id.IsNone(); }), static_cast<int32>(INDEX_NONE));

	// What is on the bar is what is saved, slot for slot, "" for the gaps.
	Skills->ActionBar[0] = FName(TEXT("melee_attack"));
	Skills->ActionBar[3] = FName(TEXT("warrior_taunt"));
	const TArray<FString> Filled = Skills->GetActionBarForSave();
	TestEqual(TEXT("filled bar saves as eight"), Filled.Num(), ValhallaActionBarSlots);
	TestEqual(TEXT("slot 1 saved"), Filled[0], FString(TEXT("melee_attack")));
	TestTrue(TEXT("slot 2 saved as \"\""), Filled[1].IsEmpty());
	TestEqual(TEXT("slot 4 saved"), Filled[3], FString(TEXT("warrior_taunt")));
	TestTrue(TEXT("slot 8 saved as \"\""), Filled[7].IsEmpty());

	// A short bar (never the case on a live component) still saves as eight.
	Skills->ActionBar.SetNum(2);
	TestEqual(TEXT("short bar saves as eight"), Skills->GetActionBarForSave().Num(), ValhallaActionBarSlots);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Valhalla.Game.ActionBar.PlacementRule
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaActionBarPlacementRuleTest,
	"Valhalla.Game.ActionBar.PlacementRule",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaActionBarPlacementRuleTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ValhallaActionBarTests;
	const TMap<FName, FValhallaSkillTemplate> Table = MakeSkillTable();
	const TArray<FName> Warrior = WarriorClassSkills();
	auto Can = [&Table, &Warrior](const TCHAR* SkillId, int32 Level, const TArray<FName>* ClassSkills = nullptr)
	{
		return UValhallaSkillComponent::CanPlaceOnActionBar(Table.Find(FName(SkillId)), ClassSkills ? *ClassSkills : Warrior, Level);
	};

	// Own class, unlocked.
	TestTrue(TEXT("warrior skill, level 1 of 1"), Can(TEXT("warrior_shield_bash"), 1));
	TestTrue(TEXT("warrior skill, exactly its level (2 of 2)"), Can(TEXT("warrior_taunt"), 2));
	TestTrue(TEXT("warrior skill, above its level (8 of 8)"), Can(TEXT("warrior_charge"), 8));
	TestTrue(TEXT("warrior skill, well above its level"), Can(TEXT("warrior_charge"), 25));

	// Own class, locked: the pane shows it dimmed "(Lv N)" and it may not go on.
	TestFalse(TEXT("warrior skill below its level (1 of 2)"), Can(TEXT("warrior_taunt"), 1));
	TestFalse(TEXT("warrior skill below its level (7 of 8)"), Can(TEXT("warrior_charge"), 7));

	// Another class's skill, at any level.
	TestFalse(TEXT("wizard skill on a warrior, level 1"), Can(TEXT("wizard_fireball"), 1));
	TestFalse(TEXT("wizard skill on a warrior, level 25"), Can(TEXT("wizard_fireball"), 25));

	// Cross-class (classId null): allowed whatever the class list says.
	TestTrue(TEXT("melee_attack on a warrior"), Can(TEXT("melee_attack"), 1));
	const TArray<FName> NoClassSkills;
	TestTrue(TEXT("melee_attack with an empty class list"), Can(TEXT("melee_attack"), 1, &NoClassSkills));
	TestFalse(TEXT("cross-class still needs its level (0 of 1)"), Can(TEXT("melee_attack"), 0));

	// A class skill the class list does not name (validation only warns) is not the class's.
	TestFalse(TEXT("warrior skill missing from the class list"), Can(TEXT("warrior_shield_bash"), 10, &NoClassSkills));

	// Nothing, or a template with no id.
	TestFalse(TEXT("null skill"), UValhallaSkillComponent::CanPlaceOnActionBar(nullptr, Warrior, 25));
	TestFalse(TEXT("unknown id (not in skills.json)"), Can(TEXT("no_such_skill"), 25));
	const FValhallaSkillTemplate NoId = MakeSkill(TEXT(""), nullptr, 1);
	TestFalse(TEXT("template with no id"), UValhallaSkillComponent::CanPlaceOnActionBar(&NoId, Warrior, 25));

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Valhalla.Game.ActionBar.SavedBarFilter
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaActionBarSavedBarFilterTest,
	"Valhalla.Game.ActionBar.SavedBarFilter",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaActionBarSavedBarFilterTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ValhallaActionBarTests;
	const TMap<FName, FValhallaSkillTemplate> Table = MakeSkillTable();
	const TArray<FName> Warrior = WarriorClassSkills();

	// A level 5 warrior, through the same rule the component uses.
	constexpr int32 Level = 5;
	auto CanPlace = [&Table, &Warrior](FName SkillId)
	{
		return UValhallaSkillComponent::CanPlaceOnActionBar(Table.Find(SkillId), Warrior, Level);
	};

	const TArray<FString> Saved = {
		TEXT("melee_attack"),        // 1: cross-class          -> kept
		FString(),                   // 2: empty                -> empty
		TEXT("wizard_fireball"),     // 3: another class's      -> empty
		TEXT("warrior_charge"),      // 4: locked until 8       -> empty
		TEXT("no_such_skill"),       // 5: removed from the data -> empty
		TEXT("warrior_shield_bash"), // 6: own class, unlocked  -> kept
		TEXT("warrior_taunt"),       // 7: own class, unlocked  -> kept
		FString(),                   // 8: empty                -> empty
		TEXT("warrior_shield_bash"), // 9: past the bar         -> ignored
	};

	const TArray<FName> Bar = UValhallaSkillComponent::BuildActionBarFromSave(Saved, CanPlace);
	TestEqual(TEXT("eight slots"), Bar.Num(), ValhallaActionBarSlots);
	if (Bar.Num() != ValhallaActionBarSlots)
	{
		return false;
	}
	TestEqual(TEXT("slot 1 cross-class kept"), Bar[0], FName(TEXT("melee_attack")));
	TestTrue(TEXT("slot 2 empty stays empty"), Bar[1].IsNone());
	TestTrue(TEXT("slot 3 another class's skill left empty"), Bar[2].IsNone());
	TestTrue(TEXT("slot 4 locked skill left empty"), Bar[3].IsNone());
	TestTrue(TEXT("slot 5 unknown skill left empty"), Bar[4].IsNone());
	TestEqual(TEXT("slot 6 kept in its own slot"), Bar[5], FName(TEXT("warrior_shield_bash")));
	TestEqual(TEXT("slot 7 kept in its own slot"), Bar[6], FName(TEXT("warrior_taunt")));
	TestTrue(TEXT("slot 8 empty stays empty"), Bar[7].IsNone());

	// A short save fills the front and leaves the rest empty.
	const TArray<FName> Short = UValhallaSkillComponent::BuildActionBarFromSave({ FString(), TEXT("warrior_taunt") }, CanPlace);
	TestEqual(TEXT("short save -> eight slots"), Short.Num(), ValhallaActionBarSlots);
	TestTrue(TEXT("short save slot 1 empty"), Short[0].IsNone());
	TestEqual(TEXT("short save slot 2"), Short[1], FName(TEXT("warrior_taunt")));
	TestTrue(TEXT("short save slot 3 empty"), Short[2].IsNone());

	// Nothing saved (a new character): eight empty slots.
	const TArray<FName> Unsaved = UValhallaSkillComponent::BuildActionBarFromSave(TArray<FString>(), CanPlace);
	TestEqual(TEXT("no save -> eight slots"), Unsaved.Num(), ValhallaActionBarSlots);
	TestEqual(TEXT("no save -> nothing on the bar"), Unsaved.FindLastByPredicate([](const FName& Id) { return !Id.IsNone(); }), static_cast<int32>(INDEX_NONE));

	// The filtered bar saves back as it now stands, "" where a skill was dropped.
	UValhallaSkillComponent* Skills = NewObject<UValhallaSkillComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!TestNotNull(TEXT("component"), Skills))
	{
		return false;
	}
	Skills->ActionBar = Bar;
	const TArray<FString> Resaved = Skills->GetActionBarForSave();
	const TArray<FString> Expected = {
		TEXT("melee_attack"), FString(), FString(), FString(), FString(),
		TEXT("warrior_shield_bash"), TEXT("warrior_taunt"), FString() };
	TestEqual(TEXT("round trip: eight entries"), Resaved.Num(), Expected.Num());
	for (int32 Index = 0; Index < Expected.Num() && Index < Resaved.Num(); ++Index)
	{
		TestEqual(*FString::Printf(TEXT("round trip slot %d"), Index + 1), Resaved[Index], Expected[Index]);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
