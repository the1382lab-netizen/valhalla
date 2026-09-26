// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaAssetPreload.h"

#include "Animation/AnimSequence.h"
#include "Engine/AssetManager.h"
#include "Engine/GameInstance.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeLock.h"
#include "ValhallaDataSubsystem.h"
#include "ValhallaNPC.h"
#include "ValhallaNPCSpawner.h"
#include "ValhallaPortal.h"
#include "ValhallaVfxLibrary.h"
#include "ValhallaVisuals.h"
#include "ValhallaZoneSubsystem.h"
#include "ValhallaZoneTypes.h"

DEFINE_LOG_CATEGORY(LogValhallaPreload);

namespace
{
	/** Set while a preloader is running in this process: only then is a synchronous load a "miss". */
	int32 GActivePreloaders = 0;
	int32 GMissCount = 0;

	/** Paths already reported (one line each), and paths that do not exist (not tried again in a game). */
	TSet<FString>& ReportedMisses()
	{
		static TSet<FString> Set;
		return Set;
	}
	TSet<FString>& KnownMissing()
	{
		static TSet<FString> Set;
		return Set;
	}

	constexpr float TickIntervalSeconds = 0.5f;
}

// ─────────────────────────────────────────────────────────────────────────────
//  ValhallaAssets
// ─────────────────────────────────────────────────────────────────────────────

FString ValhallaAssets::ToObjectPath(const FString& Path)
{
	if (Path.IsEmpty() || Path.Contains(TEXT(".")))
	{
		return Path;
	}
	FString Name;
	if (!Path.Split(TEXT("/"), nullptr, &Name, ESearchCase::CaseSensitive, ESearchDir::FromEnd) || Name.IsEmpty())
	{
		return Path;
	}
	return Path + TEXT(".") + Name;
}

UObject* ValhallaAssets::LoadImpl(UClass* Class, const FString& Path, const TCHAR* What, bool bQuiet)
{
	if (Path.IsEmpty() || !Class)
	{
		return nullptr;
	}
	const FString ObjectPath = ToObjectPath(Path);
	if (UObject* Found = FSoftObjectPath(ObjectPath).ResolveObject())
	{
		return Found->IsA(Class) ? Found : nullptr;
	}

	// A packaged game's content does not change while it runs, so a path that
	// is not there is not asked for again (PieceForActiveBody tries a swap for
	// every piece). The editor can import art mid-session, so it always asks.
	if (!GIsEditor && KnownMissing().Contains(ObjectPath))
	{
		return nullptr;
	}

	UObject* Loaded = StaticLoadObject(Class, nullptr, *ObjectPath, nullptr, bQuiet ? (LOAD_NoWarn | LOAD_Quiet) : LOAD_None);
	if (!Loaded)
	{
		if (!GIsEditor)
		{
			KnownMissing().Add(ObjectPath);
		}
		return nullptr;
	}

	if (GActivePreloaders > 0 && !ReportedMisses().Contains(ObjectPath))
	{
		ReportedMisses().Add(ObjectPath);
		++GMissCount;
		UE_LOG(LogValhallaPreload, Log, TEXT("preload miss: %s (%s), loaded on the spot"), *ObjectPath, What ? What : TEXT("?"));
	}
	return Loaded;
}

int32 ValhallaAssets::GetMissCount()
{
	return GMissCount;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Subsystem
// ─────────────────────────────────────────────────────────────────────────────

UValhallaAssetPreloadSubsystem* UValhallaAssetPreloadSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UValhallaAssetPreloadSubsystem>() : nullptr;
}

bool UValhallaAssetPreloadSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

void UValhallaAssetPreloadSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	++GActivePreloaders;
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UValhallaAssetPreloadSubsystem::Tick), TickIntervalSeconds);
	// As early as the data allows: the login's map load runs alongside it.
	RequestCommon();
}

void UValhallaAssetPreloadSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	for (TSharedPtr<FStreamableHandle>* Handle : { &CommonHandle, &ZoneHandle, &PendingHandle })
	{
		if (Handle->IsValid())
		{
			(*Handle)->CancelHandle();
			Handle->Reset();
		}
	}
	--GActivePreloaders;
	Super::Deinitialize();
}

bool UValhallaAssetPreloadSubsystem::Tick(float /*DeltaSeconds*/)
{
	const UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer)
	{
		return true;
	}
	if (!bCommonRequested)
	{
		RequestCommon();
	}

	const APlayerController* PC = GameInstance->GetFirstLocalPlayerController(World);
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const UValhallaZoneSubsystem* Zones = World->GetSubsystem<UValhallaZoneSubsystem>();
	const FValhallaZoneDef* Zone = (Pawn && Zones) ? Zones->GetZoneAt(Pawn->GetActorLocation()) : nullptr;
	if (Zone && Zone->ZoneId != LoadedZone && Zone->ZoneId != PendingZone)
	{
		PreloadZone(World, Zone->ZoneId);
	}
	return true;
}

void UValhallaAssetPreloadSubsystem::RequestCommon()
{
	UGameInstance* GameInstance = GetGameInstance();
	const UValhallaDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
	if (!Data || !Data->IsLoaded())
	{
		return; // the item list comes from the data; try again next tick
	}
	bCommonRequested = true;

	TArray<FString> Paths;
	GatherCommonAssets(Data, Paths);
	TArray<FSoftObjectPath> Soft;
	for (const FString& Path : Paths)
	{
		Soft.Emplace(ValhallaAssets::ToObjectPath(Path));
	}
	const double Started = FPlatformTime::Seconds();
	const int32 Count = Soft.Num();
	TWeakObjectPtr<UValhallaAssetPreloadSubsystem> WeakThis(this);
	CommonHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(MoveTemp(Soft),
		FStreamableDelegate::CreateLambda([WeakThis, Started, Count]()
		{
			if (UValhallaAssetPreloadSubsystem* Self = WeakThis.Get())
			{
				Self->bCommonReady = true;
				UE_LOG(LogValhallaPreload, Log, TEXT("preload: common set ready (%d assets, %.2f s)"), Count, FPlatformTime::Seconds() - Started);
			}
		}),
		FStreamableManager::AsyncLoadHighPriority, /*bManageActiveHandle*/ false, /*bStartStalled*/ false, TEXT("ValhallaPreload:Common"));
	UE_LOG(LogValhallaPreload, Log, TEXT("preload: common set requested (%d assets)"), Count);
}

void UValhallaAssetPreloadSubsystem::PreloadZone(UWorld* World, FName ZoneId, FSimpleDelegate OnComplete)
{
	if (ZoneId.IsNone() || !World)
	{
		OnComplete.ExecuteIfBound();
		return;
	}
	if (ZoneId == LoadedZone && !PendingHandle.IsValid())
	{
		OnComplete.ExecuteIfBound();
		return;
	}
	if (ZoneId == PendingZone)
	{
		if (OnComplete.IsBound())
		{
			PendingCallbacks.Add(OnComplete);
		}
		return;
	}

	const UValhallaZoneSubsystem* Zones = World->GetSubsystem<UValhallaZoneSubsystem>();
	const FValhallaZoneDef* Zone = Zones ? Zones->FindZone(ZoneId) : nullptr;
	TArray<FString> Paths;
	if (Zone)
	{
		GatherZoneAssets(World, *Zone, Paths);
		TArray<FName> Destinations;
		GatherPortalDestinations(World, *Zone, Destinations);
		for (const FName Destination : Destinations)
		{
			if (const FValhallaZoneDef* Next = Zones->FindZone(Destination))
			{
				GatherZoneAssets(World, *Next, Paths);
			}
		}
	}

	if (PendingHandle.IsValid())
	{
		PendingHandle->CancelHandle();
		PendingHandle.Reset();
	}
	PendingZone = ZoneId;
	PendingStarted = FPlatformTime::Seconds();
	PendingCallbacks.Reset();
	if (OnComplete.IsBound())
	{
		PendingCallbacks.Add(OnComplete);
	}

	TWeakObjectPtr<UValhallaAssetPreloadSubsystem> WeakThis(this);
	const int32 Count = Paths.Num();
	auto Finish = [WeakThis, ZoneId, Count]()
	{
		UValhallaAssetPreloadSubsystem* Self = WeakThis.Get();
		if (!Self || Self->PendingZone != ZoneId)
		{
			return;
		}
		// The new set is in: now the old one can go.
		if (Self->ZoneHandle.IsValid())
		{
			Self->ZoneHandle->ReleaseHandle();
		}
		Self->ZoneHandle = Self->PendingHandle;
		Self->PendingHandle.Reset();
		Self->LoadedZone = ZoneId;
		Self->PendingZone = NAME_None;
		UE_LOG(LogValhallaPreload, Log, TEXT("preload: zone '%s' ready (%d assets, %.2f s)"),
			*ZoneId.ToString(), Count, FPlatformTime::Seconds() - Self->PendingStarted);
		TArray<FSimpleDelegate> Callbacks = MoveTemp(Self->PendingCallbacks);
		for (FSimpleDelegate& Callback : Callbacks)
		{
			Callback.ExecuteIfBound();
		}
	};

	if (Paths.Num() == 0)
	{
		Finish();
		return;
	}

	TArray<FSoftObjectPath> Soft;
	for (const FString& Path : Paths)
	{
		Soft.Emplace(ValhallaAssets::ToObjectPath(Path));
	}
	UE_LOG(LogValhallaPreload, Log, TEXT("preload: zone '%s' requested (%d assets)"), *ZoneId.ToString(), Count);
	PendingHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(MoveTemp(Soft),
		FStreamableDelegate::CreateLambda(Finish), FStreamableManager::AsyncLoadHighPriority,
		/*bManageActiveHandle*/ false, /*bStartStalled*/ false, TEXT("ValhallaPreload:Zone"));
	// A handle whose assets were all in memory already has completed inside the call.
	if (PendingHandle.IsValid() && PendingHandle->HasLoadCompleted() && PendingZone == ZoneId)
	{
		Finish();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  The lists
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaAssetPreloadSubsystem::AddUnique(TArray<FString>& OutPaths, const FString& Path)
{
	if (!Path.IsEmpty())
	{
		OutPaths.AddUnique(Path);
	}
}

void UValhallaAssetPreloadSubsystem::GatherCommonAssets(const UValhallaDataSubsystem* Data, TArray<FString>& OutPaths)
{
	// Every spell effect (UValhallaVfxSubsystem::GetSystem and the projectile's bolt).
	for (int32 Index = 1; Index <= static_cast<int32>(EValhallaVfx::Cone); ++Index)
	{
		AddUnique(OutPaths, UValhallaVfxLibrary::SystemPath(static_cast<EValhallaVfx>(Index)));
	}

	// The body every player and most NPCs wear, its head and the default hair.
	AddUnique(OutPaths, UValhallaVisuals::ActiveBodyMeshPath());
	AddUnique(OutPaths, UValhallaVisuals::ActiveHeadMeshPath());
	AddUnique(OutPaths, UValhallaVisuals::PiecePathForActiveBody(UValhallaVisuals::HairMeshPath(TEXT("SK_Hair_Brown_Short"))));

	// The player body's animations (UValhallaAnimComponent::LoadSequences without an override).
	for (int32 Index = 0; Index < ValhallaAnimCount; ++Index)
	{
		AddUnique(OutPaths, UValhallaVisuals::AnimPath(static_cast<EValhallaAnim>(Index)));
	}

	// Every item's equipment mesh: what any player might be wearing.
	if (Data)
	{
		GatherItemAssets(Data->GetItems(), OutPaths);
	}
}

void UValhallaAssetPreloadSubsystem::GatherItemAssets(const TMap<FName, FValhallaItemTemplate>& Items, TArray<FString>& OutPaths)
{
	for (const TPair<FName, FValhallaItemTemplate>& Item : Items)
	{
		bool bSkeletal = true;
		const FString Path = UValhallaVisuals::EquipmentAssetPath(Item.Value, bSkeletal);
		AddUnique(OutPaths, bSkeletal ? UValhallaVisuals::PiecePathForActiveBody(Path) : Path);
	}
}

void UValhallaAssetPreloadSubsystem::GatherNPCTypeAssets(const AValhallaNPC* TypeDefaults, TArray<FString>& OutPaths)
{
	if (!TypeDefaults)
	{
		return;
	}

	if (TypeDefaults->HasBodyOverride())
	{
		// A body of its own (the goblin): the mesh and its animation folder.
		AddUnique(OutPaths, TypeDefaults->BodyMeshOverride.ToSoftObjectPath().GetLongPackageName());
		if (!TypeDefaults->AnimFolderOverride.IsEmpty())
		{
			for (int32 Index = 0; Index < ValhallaAnimCount; ++Index)
			{
				const FString Path = UValhallaVisuals::AnimPath(static_cast<EValhallaAnim>(Index));
				if (!Path.IsEmpty())
				{
					AddUnique(OutPaths, TypeDefaults->AnimFolderOverride / FPaths::GetBaseFilename(Path));
				}
			}
		}
		return;
	}

	// The armour it names, as the active body wears it (GetAppearanceMeshes).
	for (const TSoftObjectPtr<USkeletalMesh>* Slot : { &TypeDefaults->ChestMeshAsset, &TypeDefaults->HelmMeshAsset,
			 &TypeDefaults->LegsMeshAsset, &TypeDefaults->BootsMeshAsset, &TypeDefaults->GlovesMeshAsset })
	{
		if (!Slot->IsNull())
		{
			AddUnique(OutPaths, UValhallaVisuals::PiecePathForActiveBody(Slot->ToSoftObjectPath().GetLongPackageName()));
		}
	}
	if (TypeDefaults->bWearPlaceholderKit)
	{
		AddUnique(OutPaths, UValhallaVisuals::EquipmentMeshPath(AValhallaNPC::PlaceholderChestName()));
		AddUnique(OutPaths, UValhallaVisuals::EquipmentMeshPath(AValhallaNPC::PlaceholderHelmName()));
	}
}

void UValhallaAssetPreloadSubsystem::GatherZoneAssets(const UWorld* World, const FValhallaZoneDef& Zone, TArray<FString>& OutPaths)
{
	if (!World)
	{
		return;
	}
	const UGameInstance* GameInstance = World->GetGameInstance();
	const UValhallaDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;

	TSet<const UClass*> Types;
	TSet<FName> Templates;
	for (TActorIterator<AValhallaNPCSpawner> It(World); It; ++It)
	{
		const AValhallaNPCSpawner* Spawner = *It;
		if (!Zone.Contains2D(Spawner->GetActorLocation()))
		{
			continue;
		}
		for (const UClass* Type : { Spawner->NPCClass.Get(), Spawner->RareNPCClass.Get() })
		{
			if (Type && !Types.Contains(Type))
			{
				Types.Add(Type);
				GatherNPCTypeAssets(Type->GetDefaultObject<AValhallaNPC>(), OutPaths);
			}
		}
		Templates.Add(Spawner->GetEffectiveTemplateId());
		Templates.Add(Spawner->GetEffectiveRareTemplateId());
	}

	// The templates' weapons (AValhallaNPC's weapon mesh).
	if (Data)
	{
		for (const FName TemplateId : Templates)
		{
			const FValhallaNPCTemplate* Template = TemplateId.IsNone() ? nullptr : Data->FindNPCTemplate(TemplateId);
			const FValhallaItemTemplate* Weapon = (Template && !Template->WeaponId.IsNone()) ? Data->FindItem(Template->WeaponId) : nullptr;
			if (Weapon)
			{
				bool bSkeletal = true;
				const FString Path = UValhallaVisuals::EquipmentAssetPath(*Weapon, bSkeletal);
				AddUnique(OutPaths, bSkeletal ? UValhallaVisuals::PiecePathForActiveBody(Path) : Path);
			}
		}
	}
}

void UValhallaAssetPreloadSubsystem::GatherPortalDestinations(const UWorld* World, const FValhallaZoneDef& Zone, TArray<FName>& OutZoneIds)
{
	if (!World)
	{
		return;
	}
	for (TActorIterator<AValhallaPortal> It(World); It; ++It)
	{
		if (Zone.Contains2D(It->GetActorLocation()) && !It->TargetZoneId.IsNone() && It->TargetZoneId != Zone.ZoneId)
		{
			OutZoneIds.AddUnique(It->TargetZoneId);
		}
	}
}
