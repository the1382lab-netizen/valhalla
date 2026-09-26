// Copyright Valhalla 2.0. All Rights Reserved.
//
// First public test, bugs 1 and 2: the packaged client left out what the game
// loads by name. ValhallaContentCheck.h has the list.
//
//   Valhalla.Content.Paths — ToPackagePath and IsInsideAny (a folder is not
//     matched by a sibling that shares its prefix: /Game/A/UI is not inside
//     /Game/A/U).
//   Valhalla.Content.CookCoverage — with the real item data and every NPC
//     Type Blueprint under /Game/Valhalla/NPCs, every path on the list exists
//     and lies in a DirectoriesToAlwaysCook folder. The failure names the
//     path, so the fix is a line in DefaultGame.ini (or the missing asset).

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "ValhallaContentCheck.h"
#include "ValhallaDataSettings.h"
#include "ValhallaDataSubsystem.h"
#include "ValhallaNPC.h"
#include "ValhallaVisuals.h"

#ifndef VALHALLA_GAME_TEST_FLAGS
#define VALHALLA_GAME_TEST_FLAGS ( \
	EAutomationTestFlags::EditorContext \
	| EAutomationTestFlags::ClientContext \
	| EAutomationTestFlags::ServerContext \
	| EAutomationTestFlags::CommandletContext \
	| EAutomationTestFlags::ProductFilter)
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValhallaContentPathsTest, "Valhalla.Content.Paths", VALHALLA_GAME_TEST_FLAGS)

bool FValhallaContentPathsTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ValhallaContent;
	TestEqual(TEXT("object path -> package"), ToPackagePath(TEXT("/Game/A/SK_B.SK_B")), FString(TEXT("/Game/A/SK_B")));
	TestEqual(TEXT("package unchanged"), ToPackagePath(TEXT("/Game/A/SK_B")), FString(TEXT("/Game/A/SK_B")));

	const TArray<FString> Dirs = { TEXT("/Game/Valhalla/UI"), TEXT("/Game/Valhalla/Characters/Weapons") };
	TestTrue(TEXT("inside"), IsInsideAny(TEXT("/Game/Valhalla/UI/HUD/WBP_GameHUD"), Dirs));
	TestTrue(TEXT("inside, deeper"), IsInsideAny(TEXT("/Game/Valhalla/Characters/Weapons/SM_sword_iron"), Dirs));
	TestFalse(TEXT("a sibling sharing the prefix is not inside"), IsInsideAny(TEXT("/Game/Valhalla/UIExtra/T_Icon"), Dirs));
	TestFalse(TEXT("the parent is not inside"), IsInsideAny(TEXT("/Game/Valhalla/Characters/Body/SK_Valhalla_Body"), Dirs));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValhallaContentCookCoverageTest, "Valhalla.Content.CookCoverage", VALHALLA_GAME_TEST_FLAGS)

bool FValhallaContentCookCoverageTest::RunTest(const FString& /*Parameters*/)
{
	// The real item data.
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
		AddError(FString::Printf(TEXT("could not load the game data from %s"), *DataRoot));
		return false;
	}

	// Every NPC Type Blueprint (they live under /Game/Valhalla/NPCs).
	IAssetRegistry& Registry = IAssetRegistry::GetChecked();
	Registry.SearchAllAssets(/*bSynchronousSearch*/ true);
	TArray<FAssetData> Assets;
	Registry.GetAssetsByPath(FName(TEXT("/Game/Valhalla/NPCs")), Assets, /*bRecursive*/ true);
	TArray<const AValhallaNPC*> Types;
	for (const FAssetData& Asset : Assets)
	{
		FString GeneratedClass;
		if (!Asset.GetTagValue(FName(TEXT("GeneratedClass")), GeneratedClass) || GeneratedClass.IsEmpty())
		{
			continue;
		}
		const FString ClassPath = FPackageName::ExportTextPathToObjectPath(GeneratedClass);
		const UClass* Class = StaticLoadClass(AValhallaNPC::StaticClass(), nullptr, *ClassPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (Class)
		{
			Types.AddUnique(Class->GetDefaultObject<AValhallaNPC>());
		}
	}
	TestTrue(TEXT("found the NPC Type Blueprints"), Types.Num() >= 5);

	TArray<FString> Paths;
	ValhallaContent::GatherNamedAssets(&Tables.Items, Types, Paths);
	TestTrue(TEXT("the list is not trivially small"), Paths.Num() >= 50);

	TArray<FString> Cooked;
	ValhallaContent::GetAlwaysCookDirectories(Cooked);
	TestTrue(TEXT("DirectoriesToAlwaysCook is read"), Cooked.Contains(TEXT("/Game/Valhalla/VFX")));

	int32 Outside = 0;
	int32 Missing = 0;
	for (const FString& Path : Paths)
	{
		if (!Path.StartsWith(TEXT("/Game/")))
		{
			continue; // engine content ships with the engine
		}
		if (!FPackageName::DoesPackageExist(Path))
		{
			++Missing;
			AddError(FString::Printf(TEXT("%s is loaded by name but does not exist"), *Path));
			continue;
		}
		if (!ValhallaContent::IsInsideAny(Path, Cooked))
		{
			++Outside;
			AddError(FString::Printf(TEXT("%s is loaded by name but no DirectoriesToAlwaysCook folder holds it (DefaultGame.ini)"), *Path));
		}
	}
	AddInfo(FString::Printf(TEXT("%d named assets, %d NPC types, %d cooked folders; %d missing, %d outside"),
		Paths.Num(), Types.Num(), Cooked.Num(), Missing, Outside));
	return Missing == 0 && Outside == 0;
}

#endif // WITH_DEV_AUTOMATION_TESTS
