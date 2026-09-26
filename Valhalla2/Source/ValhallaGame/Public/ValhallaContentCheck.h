// Copyright Valhalla 2.0. All Rights Reserved.
//
// First public test, bugs 1 and 2 (2026-09-25): the packaged client had no
// animations, no vision fog, no NPC weapons, no hair and no spell effects.
// Every one of those is loaded by name (ValhallaAssets::Load, LoadObject) and
// nothing holds a hard reference to it, so the cook left it out: the cook only
// follows references from the maps, plus the folders named in
// DefaultGame.ini's DirectoriesToAlwaysCook.
//
// This file is the one list of what the game loads by name, used twice:
//
//   * Valhalla.Content.CookCoverage (editor test): every path on the list
//     exists and lies inside a DirectoriesToAlwaysCook folder, so an asset
//     folder that is added without cooking fails the test run, not a tester;
//   * `valhalla.CheckContent [quit]` (console command, any build): loads every
//     path and logs "content check: N checked, M missing". Tools/perf/
//     package_client.cmd runs it in the packaged client and fails the build on
//     a missing asset.

#pragma once

#include "CoreMinimal.h"

class AValhallaNPC;
class UWorld;
struct FValhallaItemTemplate;

VALHALLAGAME_API DECLARE_LOG_CATEGORY_EXTERN(LogValhallaContent, Log, All);

namespace ValhallaContent
{
	/**
	 * Everything the game loads by name: the preloader's common set (every
	 * spell effect, the body, head and hair, the player animations), every
	 * item's mesh, the art of each NPC Type in `NpcTypes` (a body and animation
	 * folder of its own, or armour for the active body), the NPC placeholder
	 * kit, and the materials and meshes code names directly (fog, outline, the
	 * class-proxy tint, the portal marker). Package paths, no duplicates.
	 */
	VALHALLAGAME_API void GatherNamedAssets(const TMap<FName, FValhallaItemTemplate>* Items,
		const TArray<const AValhallaNPC*>& NpcTypes, TArray<FString>& OutPaths);

	/** The defaults of every NPC class in memory (the Blueprint NPC Types a loaded level's spawners name). */
	VALHALLAGAME_API void GatherLoadedNPCTypes(TArray<const AValhallaNPC*>& OutTypes);

	/** DirectoriesToAlwaysCook from the project's packaging settings (DefaultGame.ini), as "/Game/..." paths. */
	VALHALLAGAME_API void GetAlwaysCookDirectories(TArray<FString>& OutDirectories);

	/** True when `PackagePath` is `Directory` itself or inside it, for one of `Directories`. */
	VALHALLAGAME_API bool IsInsideAny(const FString& PackagePath, const TArray<FString>& Directories);

	/** "/Game/A/B.B" -> "/Game/A/B". */
	VALHALLAGAME_API FString ToPackagePath(const FString& Path);

	/**
	 * Loads every named asset (the NPC Types of the loaded levels) and logs the
	 * result. Returns the number missing; OutChecked is the number tried.
	 */
	VALHALLAGAME_API int32 CheckAll(const UWorld* World, int32& OutChecked, TArray<FString>* OutMissing = nullptr);
}
