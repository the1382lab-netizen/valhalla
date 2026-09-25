// Copyright Valhalla 2.0. All Rights Reserved.
//
// B-27 Phase 1, the preloader's lists (UValhallaAssetPreloadSubsystem):
//
//   Valhalla.Game.Preload.Paths — ValhallaAssets::ToObjectPath and
//     UValhallaVisuals::PiecePathForActiveBody (the swap the preloader applies
//     to armour a Blueprint names for the other body), and LoadImpl returning
//     a loaded asset and null for a missing one.
//   Valhalla.Game.Preload.Lists — the common set holds every spell effect,
//     the body and the player animations, with no duplicates; an Eldmoor NPC
//     Type with a body of its own lists that body and its animation folder,
//     one in armour lists its pieces for the active body; every listed path is
//     a package that exists (a list pointing at nothing preloads nothing, and
//     the miss only shows up as a hitch in play).
//
// The background loading and the zone change are checked in the benchmarks
// (Tools/perf): the log's "preload: zone ... ready" and "preload miss" lines.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "ValhallaAssetPreload.h"
#include "ValhallaNPC.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValhallaPreloadPathsTest, "Valhalla.Game.Preload.Paths", VALHALLA_GAME_TEST_FLAGS)

bool FValhallaPreloadPathsTest::RunTest(const FString& /*Parameters*/)
{
	TestEqual(TEXT("package path gets its object name"), ValhallaAssets::ToObjectPath(TEXT("/Game/A/SK_B")), FString(TEXT("/Game/A/SK_B.SK_B")));
	TestEqual(TEXT("object path unchanged"), ValhallaAssets::ToObjectPath(TEXT("/Game/A/SK_B.SK_B")), FString(TEXT("/Game/A/SK_B.SK_B")));
	TestEqual(TEXT("empty unchanged"), ValhallaAssets::ToObjectPath(FString()), FString());

	const bool bMetaHuman = UValhallaVisuals::ActiveBodyProfile() == EValhallaBodyProfile::MetaHuman;
	const FString Legacy = TEXT("/Game/Valhalla/Characters/Equipment/SK_chest_travelers_jerkin");
	const FString Meta = TEXT("/Game/Valhalla/Characters/MetaHuman/Equipment/SK_chest_travelers_jerkin");
	TestEqual(TEXT("armour swapped to the active body"), UValhallaVisuals::PiecePathForActiveBody(Legacy), bMetaHuman ? Meta : Legacy);
	TestEqual(TEXT("already the active body's"), UValhallaVisuals::PiecePathForActiveBody(bMetaHuman ? Meta : Legacy), bMetaHuman ? Meta : Legacy);
	TestEqual(TEXT("object path loses its object name"), UValhallaVisuals::PiecePathForActiveBody(Legacy + TEXT(".SK_chest_travelers_jerkin")), bMetaHuman ? Meta : Legacy);
	TestEqual(TEXT("not armour or hair: unchanged"), UValhallaVisuals::PiecePathForActiveBody(TEXT("/Game/Valhalla/Characters/Weapons/SM_sword")), FString(TEXT("/Game/Valhalla/Characters/Weapons/SM_sword")));

	// LoadImpl: a real asset comes back, a missing one is null (quietly).
	TestNotNull(TEXT("the body loads"), ValhallaAssets::Load<USkeletalMesh>(UValhallaVisuals::ActiveBodyMeshPath(), TEXT("test")));
	TestNull(TEXT("a missing asset is null"), ValhallaAssets::Load<USkeletalMesh>(TEXT("/Game/Valhalla/Nope/SK_Nothing"), TEXT("test"), true));
	TestNull(TEXT("the wrong class is null"), ValhallaAssets::Load<UAnimSequence>(UValhallaVisuals::ActiveBodyMeshPath(), TEXT("test"), true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValhallaPreloadListsTest, "Valhalla.Game.Preload.Lists", VALHALLA_GAME_TEST_FLAGS)

bool FValhallaPreloadListsTest::RunTest(const FString& /*Parameters*/)
{
	auto AllExist = [this](const TArray<FString>& Paths, const TCHAR* Label)
	{
		for (const FString& Path : Paths)
		{
			FString Package = Path;
			int32 Dot = INDEX_NONE;
			if (Package.FindChar(TEXT('.'), Dot))
			{
				Package.LeftInline(Dot);
			}
			TestTrue(*FString::Printf(TEXT("%s: %s exists"), Label, *Package), FPackageName::DoesPackageExist(Package));
		}
	};

	TArray<FString> Common;
	UValhallaAssetPreloadSubsystem::GatherCommonAssets(nullptr, Common);  // no data: no item art
	for (int32 Index = 1; Index <= static_cast<int32>(EValhallaVfx::Cone); ++Index)
	{
		const FString Path = UValhallaVfxLibrary::SystemPath(static_cast<EValhallaVfx>(Index));
		TestTrue(*FString::Printf(TEXT("common set has %s"), *Path), Common.Contains(Path));
	}
	TestTrue(TEXT("common set has the body"), Common.Contains(UValhallaVisuals::ActiveBodyMeshPath()));
	TestTrue(TEXT("common set has the player animations"), Common.Contains(UValhallaVisuals::AnimPath(EValhallaAnim::Idle)));
	TSet<FString> Unique(Common);
	TestEqual(TEXT("common set has no duplicates"), Unique.Num(), Common.Num());
	AllExist(Common, TEXT("common"));

	// An NPC Type with a body of its own, and one in armour.
	const UClass* Goblin = StaticLoadClass(AValhallaNPC::StaticClass(), nullptr, TEXT("/Game/Valhalla/NPCs/Eldmoor/BP_NPC_Eld_Goblin.BP_NPC_Eld_Goblin_C"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	const UClass* Leather = StaticLoadClass(AValhallaNPC::StaticClass(), nullptr, TEXT("/Game/Valhalla/NPCs/Eldmoor/BP_NPC_Eld_Leather.BP_NPC_Eld_Leather_C"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Goblin || !Leather)
	{
		AddWarning(TEXT("The Eldmoor NPC Types are not in this build; the NPC lists were not checked."));
		return true;
	}

	const AValhallaNPC* GoblinType = Goblin->GetDefaultObject<AValhallaNPC>();
	TArray<FString> GoblinPaths;
	UValhallaAssetPreloadSubsystem::GatherNPCTypeAssets(GoblinType, GoblinPaths);
	TestTrue(TEXT("goblin: its own body"), GoblinPaths.Contains(GoblinType->BodyMeshOverride.ToSoftObjectPath().GetLongPackageName()));
	int32 GoblinAnims = 0;
	for (const FString& Path : GoblinPaths)
	{
		GoblinAnims += Path.StartsWith(GoblinType->AnimFolderOverride) ? 1 : 0;
	}
	TestTrue(TEXT("goblin: its animation folder"), GoblinAnims >= 10);
	AllExist(GoblinPaths, TEXT("goblin"));

	TArray<FString> LeatherPaths;
	UValhallaAssetPreloadSubsystem::GatherNPCTypeAssets(Leather->GetDefaultObject<AValhallaNPC>(), LeatherPaths);
	TestTrue(TEXT("leather: some armour"), LeatherPaths.Num() > 0);
	if (UValhallaVisuals::ActiveBodyProfile() == EValhallaBodyProfile::MetaHuman)
	{
		for (const FString& Path : LeatherPaths)
		{
			TestTrue(*FString::Printf(TEXT("leather: %s is for the MetaHuman body"), *Path), Path.StartsWith(TEXT("/Game/Valhalla/Characters/MetaHuman/")));
		}
	}
	AllExist(LeatherPaths, TEXT("leather"));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
