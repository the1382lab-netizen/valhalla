// Copyright Valhalla 2.0. All Rights Reserved.
//
// Phase 8a: every skill in the game resolves to a Niagara system and a colour.
//
// This is the test the VFX mapping most needs, because the failure it catches
// is silent. A skill that falls off the end of the rules does not crash and
// does not log; it simply casts with nothing on screen, and nobody notices
// until a player asks why their spell is invisible. Running the real
// skills.json through the real function is the only way to know that all 41
// are covered — and it keeps working when 1.0 adds a 42nd, because the test
// iterates the file rather than a list written out here.
//
// UValhallaVfxLibrary::ResolveForSkill is a pure function of a skill template,
// a packed colour and an attack cycle: no world, no actor, no loaded asset. It
// is written that way so this test can exist in the commandlet context.
//
// Run from the editor's Session Frontend, or headless:
//   UnrealEditor-Cmd.exe <project>.uproject -ExecCmds="Automation RunTests Valhalla.; Quit" -unattended -nullrhi

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ValhallaDataSettings.h"
#include "ValhallaDataSubsystem.h"
#include "ValhallaTypes.h"
#include "ValhallaVfxLibrary.h"
#include "ValhallaVisuals.h"

#ifndef VALHALLA_GAME_TEST_FLAGS
#define VALHALLA_GAME_TEST_FLAGS ( \
	EAutomationTestFlags::EditorContext \
	| EAutomationTestFlags::ClientContext \
	| EAutomationTestFlags::ServerContext \
	| EAutomationTestFlags::CommandletContext \
	| EAutomationTestFlags::ProductFilter)
#endif

namespace ValhallaVfxTests
{
	/** `classes.json` warrior red, as a stand-in caster class for the fallback. */
	static constexpr int32 WarriorColor = 13386820;

	/** True when a colour would actually put light on the screen. */
	static bool IsVisible(const FLinearColor& Colour)
	{
		return Colour.R + Colour.G + Colour.B > 0.05f;
	}

	static FString Describe(const FValhallaSkillTemplate& Skill, const FValhallaVfxPlan& Plan)
	{
		return FString::Printf(TEXT("%s (category %d, targetType %d) -> %s"),
			*Skill.Id.ToString(),
			static_cast<int32>(Skill.Category),
			static_cast<int32>(Skill.TargetType),
			UValhallaVfxLibrary::SystemName(Plan.System));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Valhalla.Game.Vfx.Mapping
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaVfxMappingTest,
	"Valhalla.Game.Vfx.Mapping",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaVfxMappingTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ValhallaVfxTests;

	// ── The real 1.0 data, through the real loader ───────────────────────
	FString DataRoot;
	if (const UValhallaDataSettings* Settings = UValhallaDataSettings::Get())
	{
		DataRoot = Settings->GetResolvedDataRoot();
	}
	else
	{
		DataRoot = UValhallaDataSettings::ResolveDataRoot(FString());
	}

	FValhallaDataTables Tables;
	if (!UValhallaDataSubsystem::LoadTablesFromRoot(DataRoot, Tables))
	{
		AddError(FString::Printf(TEXT("could not load the 1.0 data from %s"), *DataRoot));
		return false;
	}

	TestTrue(TEXT("skills.json still holds 41 skills"), Tables.Skills.Num() == 41);

	// ── 1. Every skill resolves to a system and a colour ─────────────────
	//
	// Resolved twice, with a melee weapon and with a bow, because an
	// auto-attack's answer depends on the weapon and nothing else's does.
	int32 Resolved = 0;
	for (const TPair<FName, FValhallaSkillTemplate>& Entry : Tables.Skills)
	{
		const FValhallaSkillTemplate& Skill = Entry.Value;

		for (const EValhallaAttackCycle Cycle : {
				EValhallaAttackCycle::Melee,
				EValhallaAttackCycle::Shoot,
				EValhallaAttackCycle::Cast })
		{
			const FValhallaVfxPlan Plan =
				UValhallaVfxLibrary::ResolveForSkill(Skill, WarriorColor, Cycle);

			if (!Plan.IsValid())
			{
				AddError(FString::Printf(
					TEXT("%s resolved to no system (weapon cycle %d)."),
					*Skill.Id.ToString(), static_cast<int32>(Cycle)));
				continue;
			}

			if (!IsVisible(Plan.Color))
			{
				AddError(FString::Printf(
					TEXT("%s resolved to a black colour, which would draw nothing."),
					*Skill.Id.ToString()));
			}

			if (Plan.Radius <= 0.f)
			{
				AddError(FString::Printf(
					TEXT("%s resolved to radius %.1f; every system is sized in centimetres."),
					*Skill.Id.ToString(), Plan.Radius));
			}

			// The asset path has to be one a LoadObject could actually find.
			const FString Path = UValhallaVfxLibrary::SystemPath(Plan.System);
			if (!Path.StartsWith(UValhallaVfxLibrary::VfxRoot()))
			{
				AddError(FString::Printf(TEXT("%s -> %s, which is not under %s."),
					*Skill.Id.ToString(), *Path, UValhallaVfxLibrary::VfxRoot()));
			}
		}

		++Resolved;
	}

	TestTrue(TEXT("every skill was visited"), Resolved == Tables.Skills.Num());

	// ── 2. Auto-attacks resolve per weapon style ─────────────────────────
	if (const FValhallaSkillTemplate* Melee = Tables.Skills.Find(FName(TEXT("melee_attack"))))
	{
		const FValhallaVfxPlan Swing =
			UValhallaVfxLibrary::ResolveForSkill(*Melee, WarriorColor, EValhallaAttackCycle::Melee);
		TestTrue(TEXT("a sword swing is a Slash"), Swing.System == EValhallaVfx::Slash);
		TestTrue(TEXT("the slash sits on the attacker"), Swing.Attach == EValhallaVfxAttach::ActorFeet);

		// A wizard holding a staff auto-attacks with a bolt, because
		// AttackCycleForWeaponStyle says a staff casts — the same rule the
		// animation uses to pick A_Cast over A_Attack.
		const FValhallaVfxPlan Staff =
			UValhallaVfxLibrary::ResolveForSkill(*Melee, WarriorColor, EValhallaAttackCycle::Cast);
		TestTrue(TEXT("a staff auto-attack is a Bolt"), Staff.System == EValhallaVfx::Bolt);
		TestTrue(TEXT("a staff bolt leaves the hand"), Staff.Attach == EValhallaVfxAttach::WeaponHand);
	}
	else
	{
		AddError(TEXT("skills.json has no 'melee_attack'."));
	}

	if (const FValhallaSkillTemplate* Ranged = Tables.Skills.Find(FName(TEXT("ranged_attack"))))
	{
		const FValhallaVfxPlan Shot =
			UValhallaVfxLibrary::ResolveForSkill(*Ranged, WarriorColor, EValhallaAttackCycle::Shoot);
		TestTrue(TEXT("a bow shot is a Bolt"), Shot.System == EValhallaVfx::Bolt);
		TestTrue(TEXT("a bow bolt is thin"), Shot.Radius <= UValhallaVfxLibrary::ArrowBoltRadius);

		// Brown, not the dexterity green the scalingStat would otherwise give
		// it: an arrow is wood and fletching.
		TestTrue(TEXT("an arrow is browner than it is green"), Shot.Color.R > Shot.Color.G);
	}
	else
	{
		AddError(TEXT("skills.json has no 'ranged_attack'."));
	}

	// ── 3. The named rules, one skill each ───────────────────────────────
	//
	// One representative per rule, chosen so that a reordering of the rule
	// chain breaks exactly the rule it reordered.
	struct FExpectation
	{
		const TCHAR* SkillId;
		EValhallaVfx System;
		const TCHAR* Why;
	};

	static const FExpectation Expectations[] =
	{
		{ TEXT("wizard_fireball"),        EValhallaVfx::Bolt,     TEXT("a projectile beats its aoeGround shape") },
		{ TEXT("wizard_magic_missile"),   EValhallaVfx::Bolt,     TEXT("magic missile flies even though it is instant") },
		{ TEXT("cleric_minor_heal"),      EValhallaVfx::Heal,     TEXT("category healing") },
		{ TEXT("shaman_ancestral_spirit"),EValhallaVfx::Heal,     TEXT("healing beats aoeSelf") },
		{ TEXT("warrior_battle_shout"),   EValhallaVfx::BuffAura, TEXT("category buff beats aoeSelf") },
		{ TEXT("warrior_shield_wall"),    EValhallaVfx::BuffAura, TEXT("category defensive") },
		{ TEXT("ranger_multi_shot"),      EValhallaVfx::Cone,     TEXT("targetType cone") },
		{ TEXT("wizard_meteor"),          EValhallaVfx::AoERing,  TEXT("targetType aoeGround") },
		{ TEXT("warrior_cleave"),         EValhallaVfx::AoERing,  TEXT("targetType aoeSelf") },
		{ TEXT("wizard_frost_nova"),      EValhallaVfx::AoERing,  TEXT("an area debuff shows its area") },
		{ TEXT("rogue_poison_blade"),     EValhallaVfx::Debuff,   TEXT("a single-target debuff sits on the target") },
		{ TEXT("cleric_cure_ailment"),    EValhallaVfx::BuffAura, TEXT("a utility cast on an ally") },
		{ TEXT("warrior_charge"),         EValhallaVfx::Impact,   TEXT("an offensive single-target skill is a hit") },
		{ TEXT("cleric_smite"),           EValhallaVfx::Impact,   TEXT("the default for an offensive strike") },
	};

	for (const FExpectation& Expected : Expectations)
	{
		const FValhallaSkillTemplate* Skill = Tables.Skills.Find(FName(Expected.SkillId));
		if (!Skill)
		{
			AddError(FString::Printf(TEXT("skills.json has no '%s'."), Expected.SkillId));
			continue;
		}

		const FValhallaVfxPlan Plan =
			UValhallaVfxLibrary::ResolveForSkill(*Skill, WarriorColor, EValhallaAttackCycle::Melee);

		if (Plan.System != Expected.System)
		{
			AddError(FString::Printf(TEXT("%s: expected %s (%s), got %s"),
				*Describe(*Skill, Plan),
				UValhallaVfxLibrary::SystemName(Expected.System),
				Expected.Why,
				UValhallaVfxLibrary::SystemName(Plan.System)));
		}
	}

	// ── 4. The palette is keyed on scalingStat ───────────────────────────
	{
		FValhallaSkillTemplate Probe;
		Probe.Id = TEXT("probe");
		Probe.Category = EValhallaSkillCategory::Offensive;
		Probe.TargetType = EValhallaSkillTargetType::SingleEnemy;

		Probe.ScalingStat = TEXT("strength");
		const FLinearColor Strength = UValhallaVfxLibrary::ColorForSkill(Probe, WarriorColor);
		TestTrue(TEXT("strength is red-orange"), Strength.R > Strength.G && Strength.G > Strength.B);

		Probe.ScalingStat = TEXT("dexterity");
		const FLinearColor Dexterity = UValhallaVfxLibrary::ColorForSkill(Probe, WarriorColor);
		TestTrue(TEXT("dexterity is green"), Dexterity.G > Dexterity.R && Dexterity.G > Dexterity.B);

		Probe.ScalingStat = TEXT("intelligence");
		const FLinearColor Intelligence = UValhallaVfxLibrary::ColorForSkill(Probe, WarriorColor);
		TestTrue(TEXT("intelligence is blue-violet"), Intelligence.B > Intelligence.R && Intelligence.B > Intelligence.G);

		Probe.ScalingStat = TEXT("wisdom");
		const FLinearColor Wisdom = UValhallaVfxLibrary::ColorForSkill(Probe, WarriorColor);
		TestTrue(TEXT("wisdom is gold"), Wisdom.R > Wisdom.B && Wisdom.G > Wisdom.B);

		// stamina names no palette entry, so it falls back to the caster's
		// class colour. Three of the 41 skills are stamina-scaled and they
		// would otherwise all be invisible.
		Probe.ScalingStat = TEXT("stamina");
		const FLinearColor Stamina = UValhallaVfxLibrary::ColorForSkill(Probe, WarriorColor);
		TestTrue(TEXT("stamina falls back to the class colour"), IsVisible(Stamina));

		const FLinearColor NoClass = UValhallaVfxLibrary::ColorForSkill(Probe, 0);
		TestTrue(TEXT("and to something visible even with no class"), IsVisible(NoClass));
	}

	// ── 5. The eight systems are eight distinct assets ───────────────────
	{
		TSet<FString> Paths;
		for (uint8 Index = static_cast<uint8>(EValhallaVfx::Bolt);
			Index <= static_cast<uint8>(EValhallaVfx::Cone); ++Index)
		{
			const FString Path = UValhallaVfxLibrary::SystemPath(static_cast<EValhallaVfx>(Index));
			TestFalse(FString::Printf(TEXT("system %d has a path"), Index), Path.IsEmpty());
			Paths.Add(Path);
		}
		TestTrue(TEXT("eight distinct system paths"), Paths.Num() == 8);
		TestTrue(TEXT("None has no path"),
			UValhallaVfxLibrary::SystemPath(EValhallaVfx::None).IsEmpty());
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
