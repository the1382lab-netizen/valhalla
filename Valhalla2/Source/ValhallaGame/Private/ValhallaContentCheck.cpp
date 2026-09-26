// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaContentCheck.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/UObjectIterator.h"
#include "ValhallaAssetPreload.h"
#include "ValhallaDataSubsystem.h"
#include "ValhallaFogRenderer.h"
#include "ValhallaNPC.h"
#include "ValhallaPlayerController.h"
#include "ValhallaPortal.h"
#include "ValhallaVisuals.h"

DEFINE_LOG_CATEGORY(LogValhallaContent);

namespace
{
	/** The class-proxy tint (AValhallaSpellProjectile, AValhallaLootBag name it in their .cpp files). */
	const TCHAR* ClassProxyMaterialPath = TEXT("/Game/Valhalla/Materials/M_ClassProxy");

	void AddUnique(TArray<FString>& OutPaths, const FString& Path)
	{
		const FString Package = ValhallaContent::ToPackagePath(Path);
		if (!Package.IsEmpty())
		{
			OutPaths.AddUnique(Package);
		}
	}

	/** Everything the levels ask for is in: no streaming level that should be loaded is still loading. */
	bool AreLevelsLoaded(const UWorld* World)
	{
		if (!World)
		{
			return false;
		}
		for (const ULevelStreaming* Streaming : World->GetStreamingLevels())
		{
			if (Streaming && Streaming->ShouldBeLoaded() && !Streaming->IsLevelLoaded())
			{
				return false;
			}
		}
		return true;
	}

	/** Waits for the data and the levels (at most MaxWaitSeconds), then checks; `quit` exits with 0 or 3. */
	void RunWhenReady(TWeakObjectPtr<UWorld> WeakWorld, bool bQuit)
	{
		constexpr double MaxWaitSeconds = 120.0;
		const double Started = FPlatformTime::Seconds();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, bQuit, Started](float) -> bool
		{
			const UWorld* World = WeakWorld.Get();
			const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
			const UValhallaDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
			const bool bReady = Data && Data->IsLoaded() && AreLevelsLoaded(World);
			const bool bTimedOut = FPlatformTime::Seconds() - Started > MaxWaitSeconds;
			if (!bReady && !bTimedOut)
			{
				return true; // ask again next tick
			}
			if (!bReady)
			{
				UE_LOG(LogValhallaContent, Warning, TEXT("content check: the data or the levels were not ready after %.0f s; checking what is there."), MaxWaitSeconds);
			}

			int32 Checked = 0;
			const int32 Missing = ValhallaContent::CheckAll(World, Checked);
			if (bQuit)
			{
				UE_LOG(LogValhallaContent, Display, TEXT("content check: quitting (exit code %d)"), Missing > 0 ? 3 : 0);
				FPlatformMisc::RequestExitWithStatus(false, Missing > 0 ? 3 : 0, TEXT("valhalla.CheckContent"));
			}
			return false;
		}), 0.25f);
	}

	FAutoConsoleCommandWithWorldAndArgs GCheckContentCommand(
		TEXT("valhalla.CheckContent"),
		TEXT("Loads every asset the game loads by name and logs 'content check: N checked, M missing'. ")
		TEXT("Waits for the data and the levels first. `valhalla.CheckContent quit` exits afterwards (code 3 when anything is missing)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World && GEngine)
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if (Context.World() && Context.World()->IsGameWorld())
					{
						World = Context.World();
						break;
					}
				}
			}
			const bool bQuit = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase); });
			RunWhenReady(World, bQuit);
		}));
}

FString ValhallaContent::ToPackagePath(const FString& Path)
{
	int32 Dot = INDEX_NONE;
	return Path.FindChar(TEXT('.'), Dot) ? Path.Left(Dot) : Path;
}

void ValhallaContent::GatherNamedAssets(const TMap<FName, FValhallaItemTemplate>* Items,
	const TArray<const AValhallaNPC*>& NpcTypes, TArray<FString>& OutPaths)
{
	TArray<FString> Raw;

	// The preloader's common set: spell effects, body, head, hair, the player animations.
	UValhallaAssetPreloadSubsystem::GatherCommonAssets(nullptr, Raw);

	// Every item: armour (for the active body), weapons, shields.
	if (Items)
	{
		UValhallaAssetPreloadSubsystem::GatherItemAssets(*Items, Raw);
	}

	// Every NPC Type: its own body and animation folder (the goblin), or its armour.
	for (const AValhallaNPC* Type : NpcTypes)
	{
		UValhallaAssetPreloadSubsystem::GatherNPCTypeAssets(Type, Raw);
	}

	// The placeholder kit an NPC in no armour wears (AValhallaNPC::ApplyAppearance).
	Raw.Add(UValhallaVisuals::EquipmentMeshPath(AValhallaNPC::PlaceholderChestName()));
	Raw.Add(UValhallaVisuals::EquipmentMeshPath(AValhallaNPC::PlaceholderHelmName()));

	// Named straight from code.
	Raw.Add(AValhallaFogRenderer::FogMaterialPath);
	Raw.Add(AValhallaPlayerController::OutlineMaterialPath);
	Raw.Add(ClassProxyMaterialPath);
	Raw.Add(AValhallaPortal::MarkerMeshPath);

	for (const FString& Path : Raw)
	{
		AddUnique(OutPaths, Path);
	}
}

void ValhallaContent::GatherLoadedNPCTypes(TArray<const AValhallaNPC*>& OutTypes)
{
	for (TObjectIterator<UClass> It; It; ++It)
	{
		const UClass* Class = *It;
		if (!Class->IsChildOf(AValhallaNPC::StaticClass())
			|| Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			continue;
		}
		const FString Name = Class->GetName();
		if (Name.StartsWith(TEXT("SKEL_")) || Name.StartsWith(TEXT("REINST_")) || Name.StartsWith(TEXT("TRASHCLASS_")))
		{
			continue;
		}
		if (const AValhallaNPC* Defaults = Class->GetDefaultObject<AValhallaNPC>())
		{
			OutTypes.AddUnique(Defaults);
		}
	}
}

void ValhallaContent::GetAlwaysCookDirectories(TArray<FString>& OutDirectories)
{
	// Entries read `(Path="/Game/Valhalla/UI")`. GConfig holds DefaultGame.ini
	// merged into GGameIni in the editor; a packaged game still has it too.
	TArray<FString> Entries;
	GConfig->GetArray(TEXT("/Script/UnrealEd.ProjectPackagingSettings"), TEXT("DirectoriesToAlwaysCook"), Entries, GGameIni);
	for (const FString& Entry : Entries)
	{
		int32 Start = Entry.Find(TEXT("Path=\""));
		if (Start == INDEX_NONE)
		{
			continue;
		}
		Start += 6;
		const int32 End = Entry.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
		if (End > Start)
		{
			FString Directory = Entry.Mid(Start, End - Start);
			Directory.RemoveFromEnd(TEXT("/"));
			OutDirectories.AddUnique(Directory);
		}
	}
}

bool ValhallaContent::IsInsideAny(const FString& PackagePath, const TArray<FString>& Directories)
{
	for (const FString& Directory : Directories)
	{
		if (PackagePath.Equals(Directory, ESearchCase::IgnoreCase)
			|| PackagePath.StartsWith(Directory + TEXT("/"), ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

int32 ValhallaContent::CheckAll(const UWorld* World, int32& OutChecked, TArray<FString>* OutMissing)
{
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UValhallaDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
	if (!Data || !Data->IsLoaded())
	{
		UE_LOG(LogValhallaContent, Warning, TEXT("content check: no game data loaded; item art is not checked."));
	}

	TArray<const AValhallaNPC*> Types;
	GatherLoadedNPCTypes(Types);

	TArray<FString> Paths;
	GatherNamedAssets((Data && Data->IsLoaded()) ? &Data->GetItems() : nullptr, Types, Paths);

	int32 Missing = 0;
	for (const FString& Path : Paths)
	{
		if (!ValhallaAssets::Load<UObject>(Path, TEXT("content check"), /*bQuiet*/ true))
		{
			++Missing;
			UE_LOG(LogValhallaContent, Error, TEXT("content check: MISSING %s"), *Path);
			if (OutMissing)
			{
				OutMissing->Add(Path);
			}
		}
	}
	OutChecked = Paths.Num();
	UE_LOG(LogValhallaContent, Display, TEXT("content check: %d checked, %d missing (%d NPC types)"), OutChecked, Missing, Types.Num());
	return Missing;
}
