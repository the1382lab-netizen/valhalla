// Copyright Valhalla 2.0. All Rights Reserved.
//
// B-27 Phase 1: load a zone's art before the player needs it.
//
// Measured on 2026-09-25, the first fight froze a client for 921 ms in the
// editor (101 ms packaged): the first spell effect, the first goblin, the
// first armour piece were each loaded synchronously the moment they were
// needed (LoadObject, TSoftObjectPtr::LoadSynchronous). This subsystem loads
// them in the background instead:
//
//   * the common set, once per game instance: every spell effect, the body,
//     head and hair, the player body's animations and every item's equipment
//     mesh;
//   * the zone set, whenever the local player's zone changes: for every NPC
//     Spawn Point in the zone, its NPC Types' bodies, armour, animations and
//     their templates' weapons. The previous zone's set is released once the
//     new one is in.
//
// The code that used to load on the spot now goes through ValhallaAssets::Load:
// an asset that is already in memory is returned at once; one that is not is
// still loaded synchronously (nothing breaks), and logged once as
// "preload miss" so a gap in the lists shows up in the log.
//
// B-24 hook: PreloadZone(ZoneId, OnComplete). Until B-24 the subsystem calls it
// itself from a 0.5 s ticker when the local pawn changes zone; under B-24 the
// loading screen calls it and waits for OnComplete before telling the server
// it is ready. It reads the spawn points of the levels that are loaded, which
// under B-24 are exactly the zone's.
//
// Not created on a dedicated server (nothing is drawn there).

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UObject/SoftObjectPath.h"
#include "ValhallaAssetPreload.generated.h"

class AValhallaNPC;
class UWorld;
struct FStreamableHandle;
struct FValhallaZoneDef;

VALHALLAGAME_API DECLARE_LOG_CATEGORY_EXTERN(LogValhallaPreload, Log, All);

namespace ValhallaAssets
{
	/** "/Game/A/SK_B" -> "/Game/A/SK_B.SK_B"; a path that already names its object is returned as it is. */
	VALHALLAGAME_API FString ToObjectPath(const FString& Path);

	/**
	 * The object at `Path` (a package path or an object path): the loaded one
	 * when it is in memory, else a synchronous load, logged once as a preload
	 * miss while the preloader is running. `What` says who asked, for that log
	 * line. `bQuiet`: a path that may not exist (no warning when it does not).
	 */
	VALHALLAGAME_API UObject* LoadImpl(UClass* Class, const FString& Path, const TCHAR* What, bool bQuiet);

	template <typename T>
	T* Load(const FString& Path, const TCHAR* What, bool bQuiet = false)
	{
		return Cast<T>(LoadImpl(T::StaticClass(), Path, What, bQuiet));
	}

	/** A soft pointer's asset through the same path (null when the pointer is null). */
	template <typename T>
	T* LoadSoft(const TSoftObjectPtr<T>& Ptr, const TCHAR* What)
	{
		if (Ptr.IsNull())
		{
			return nullptr;
		}
		if (T* Loaded = Ptr.Get())
		{
			return Loaded;
		}
		return Load<T>(Ptr.ToSoftObjectPath().ToString(), What);
	}

	/** Preload misses logged so far in this process (tests, the benchmark log). */
	VALHALLAGAME_API int32 GetMissCount();
}

UCLASS()
class VALHALLAGAME_API UValhallaAssetPreloadSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UValhallaAssetPreloadSubsystem* Get(const UObject* WorldContext);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Load `ZoneId`'s set in the background: the spawn points of `World` inside
	 * the zone and inside the zones its portals lead to. OnComplete runs when it is all in memory: at once when the
	 * zone is already the preloaded one. The previous zone's set is released
	 * when the new one completes. B-24's loading screen waits on this.
	 */
	void PreloadZone(UWorld* World, FName ZoneId, FSimpleDelegate OnComplete = FSimpleDelegate());

	/** The zone whose set is loaded (NAME_None before the first). */
	FName GetPreloadedZone() const { return LoadedZone; }
	bool IsZoneReady(FName ZoneId) const { return !ZoneId.IsNone() && LoadedZone == ZoneId; }
	bool IsCommonReady() const { return bCommonReady; }

	// ── The lists (pure; tested) ────────────────────────────────────────

	/** Every spell effect, the body, head and hair, the player body's animations, every item's equipment mesh. */
	static void GatherCommonAssets(const class UValhallaDataSubsystem* Data, TArray<FString>& OutPaths);

	/** One NPC Type's art: its own body and animations, or its armour pieces (swapped for the active body) and the placeholder kit. */
	static void GatherNPCTypeAssets(const AValhallaNPC* TypeDefaults, TArray<FString>& OutPaths);

	/** Every NPC Spawn Point of `World` inside `Zone`: its types (and rare type) and their templates' weapons. */
	static void GatherZoneAssets(const UWorld* World, const FValhallaZoneDef& Zone, TArray<FString>& OutPaths);

	/**
	 * The zones the portals inside `Zone` lead to. A portal is a teleport, so the
	 * NPCs on the far side arrive the moment it is used, before a ticker could
	 * notice the new zone: PreloadZone loads them with the zone. Under B-24 the
	 * far zone's levels are not loaded on the client, so this finds nothing and
	 * the loading screen's own PreloadZone covers the destination.
	 */
	static void GatherPortalDestinations(const UWorld* World, const FValhallaZoneDef& Zone, TArray<FName>& OutZoneIds);

private:
	bool Tick(float DeltaSeconds);
	void RequestCommon();
	static void AddUnique(TArray<FString>& OutPaths, const FString& Path);

	FTSTicker::FDelegateHandle TickHandle;
	TSharedPtr<FStreamableHandle> CommonHandle;
	TSharedPtr<FStreamableHandle> ZoneHandle;
	TSharedPtr<FStreamableHandle> PendingHandle;
	FName LoadedZone;
	FName PendingZone;
	double PendingStarted = 0.0;
	bool bCommonRequested = false;
	bool bCommonReady = false;
	TArray<FSimpleDelegate> PendingCallbacks;
};
