// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ValhallaTypes.h"
#include "ValhallaUIConfig.h"
#include "ValhallaDataSubsystem.generated.h"

/**
 * Every table parsed out of <DataRoot>/*.json.
 *
 * Deliberately a plain struct, not a USTRUCT: it holds no UObject references,
 * so it needs no reflection and no GC participation, and keeping it unreflected
 * lets it store a raw TSharedPtr<FJsonObject> and nested containers that UHT
 * would reject. The individual templates inside it are reflected and are what
 * Blueprints actually see.
 */
struct VALHALLACORE_API FValhallaDataTables
{
	/** classes.json `classes` — keyed by class id (warrior, cleric, …). */
	TMap<FName, FValhallaClassTemplate> Classes;

	/** classes.json `classColors` — packed 0xRRGGBB per class id. */
	TMap<FName, int32> ClassColors;

	/** items.json `items` — keyed by item id. */
	TMap<FName, FValhallaItemTemplate> Items;

	/** skills.json `skills` — keyed by skill id. */
	TMap<FName, FValhallaSkillTemplate> Skills;

	/** skills.json `classSkills` — class id -> the skill ids it may learn. */
	TMap<FName, TArray<FName>> ClassSkills;

	/** npc-templates.json `templates` — keyed by NPC template id. */
	TMap<FName, FValhallaNPCTemplate> NPCTemplates;

	/** loot-tables.json `tables` — keyed by loot table id. */
	TMap<FName, FValhallaLootTable> LootTables;

	/** zones.json `zones` — keyed by zone id. */
	TMap<FName, FValhallaZoneConfig> Zones;

	/**
	 * ui-config.json, kept as raw JSON. Phase 8 decides what the UMG layer
	 * needs from it; converting it now would only guess wrong.
	 */
	TSharedPtr<FJsonObject> UiConfig;

	/**
	 * Phase 8b: the same file, typed. Parsed from UiConfig on every load, and
	 * defaulted (DEFAULT_UI_CONFIG) when the file is missing, so a reader never
	 * has to null-check it.
	 */
	FValhallaUIConfig UIConfigTyped;

	/** Files that failed to load or parse at all, by filename. */
	TArray<FString> FailedFiles;

	/** Drop every table. */
	void Reset();
};

/**
 * Loads and owns the ported Valhalla 1.0 game data for the lifetime of the
 * game instance.
 *
 * The data is read from plain JSON on disk (see UValhallaDataSettings) rather
 * than from UE assets, so 1.0's editor stays the single authoring tool and the
 * two projects cannot drift. Loading is done by hand with explicit
 * TryGet*Field calls instead of FJsonObjectConverter: the 1.0 schema uses
 * optional fields, `null` class ids and `[min, max]` tuples that the automatic
 * converter cannot express, and a hand-written loader can warn about one bad
 * field instead of dropping a whole record.
 *
 * Nothing here ever fails hard on malformed data. A missing required field logs
 * a warning and leaves the struct default; a missing file logs an error and
 * leaves that table empty. A designer's typo must not take the game down.
 */
UCLASS()
class VALHALLACORE_API UValhallaDataSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem interface

	/**
	 * Read all seven JSON files from the configured data root.
	 * Returns true when every file loaded; individual field problems only warn.
	 */
	bool LoadAll();

	/** Drop everything and re-read from disk. Phase 6 calls this on file change. */
	UFUNCTION(BlueprintCallable, Category = "Valhalla|Data")
	bool Reload();

	/** True once LoadAll has completed at least once. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Data")
	bool IsLoaded() const { return bLoaded; }

	/** The absolute data root this subsystem last read from. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Data")
	FString GetLoadedDataRoot() const { return LoadedDataRoot; }

	// ── C++ accessors: null when the id is unknown ──────────────────────

	/** Look up a class template. Returns nullptr if the id is unknown. */
	const FValhallaClassTemplate* FindClass(FName ClassId) const;

	/** Look up an item template. Returns nullptr if the id is unknown. */
	const FValhallaItemTemplate* FindItem(FName ItemId) const;

	/** Look up a skill template. Returns nullptr if the id is unknown. */
	const FValhallaSkillTemplate* FindSkill(FName SkillId) const;

	/** Look up an NPC template. Returns nullptr if the id is unknown. */
	const FValhallaNPCTemplate* FindNPCTemplate(FName NPCId) const;

	/** Look up a loot table. Returns nullptr if the id is unknown. */
	const FValhallaLootTable* FindLootTable(FName LootTableId) const;

	/** Look up a zone config. Returns nullptr if the id is unknown. */
	const FValhallaZoneConfig* FindZone(FName ZoneId) const;

	/** The raw ui-config.json object, or an invalid pointer if it failed to load. */
	TSharedPtr<FJsonObject> GetUiConfig() const { return Tables.UiConfig; }

	/** Phase 8b: ui-config.json as FValhallaUIConfig. Defaults when the file failed. */
	const FValhallaUIConfig& GetUIConfig() const { return Tables.UIConfigTyped; }

	/**
	 * Re-read ui-config.json alone and re-parse it. What the HUD calls on
	 * `valhalla.ReloadUI` and when it sees the file's timestamp move: the client
	 * has its own game instance (and so its own copy of the data), and the
	 * server's hot-reload watcher never touches it. True when the file parsed.
	 */
	bool ReloadUIConfig();

	/** Absolute path of ui-config.json under the loaded data root. */
	FString GetUIConfigPath() const;

	/** Direct read-only access to every table, for systems that iterate. */
	const FValhallaDataTables& GetTables() const { return Tables; }

	// ── Blueprint accessors: bool + out param ───────────────────────────

	/**
	 * Fetch a class template by id.
	 * Named GetClassTemplate rather than GetClass so it cannot shadow
	 * UObject::GetClass; it is displayed as "Get Class" in Blueprints.
	 */
	UFUNCTION(BlueprintCallable, Category = "Valhalla|Data", meta = (DisplayName = "Get Class"))
	bool GetClassTemplate(FName ClassId, FValhallaClassTemplate& OutClass) const;

	/** Fetch an item template by id. */
	UFUNCTION(BlueprintCallable, Category = "Valhalla|Data", meta = (DisplayName = "Get Item"))
	bool GetItem(FName ItemId, FValhallaItemTemplate& OutItem) const;

	/** Fetch a skill template by id. */
	UFUNCTION(BlueprintCallable, Category = "Valhalla|Data", meta = (DisplayName = "Get Skill"))
	bool GetSkill(FName SkillId, FValhallaSkillTemplate& OutSkill) const;

	/** Fetch an NPC template by id. */
	UFUNCTION(BlueprintCallable, Category = "Valhalla|Data", meta = (DisplayName = "Get NPC Template"))
	bool GetNPCTemplate(FName NPCId, FValhallaNPCTemplate& OutTemplate) const;

	/** Fetch a loot table by id. */
	UFUNCTION(BlueprintCallable, Category = "Valhalla|Data", meta = (DisplayName = "Get Loot Table"))
	bool GetLootTable(FName LootTableId, FValhallaLootTable& OutTable) const;

	/** Fetch a zone config by id. */
	UFUNCTION(BlueprintCallable, Category = "Valhalla|Data", meta = (DisplayName = "Get Zone"))
	bool GetZone(FName ZoneId, FValhallaZoneConfig& OutZone) const;

	/** The skill ids a class may learn, in authoring order. Empty if unknown. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Data")
	TArray<FName> GetClassSkills(FName ClassId) const;

	/** The class's display color, packed 0xRRGGBB. Returns 0xFFFFFF if unknown. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Data")
	int32 GetClassColor(FName ClassId) const;

	/** Every class id, sorted. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Data")
	TArray<FName> GetAllClassIds() const;

	/** Every item id, sorted. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Data")
	TArray<FName> GetAllItemIds() const;

	/** Every skill id, sorted. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Data")
	TArray<FName> GetAllSkillIds() const;

	/** Every NPC template id, sorted. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Data")
	TArray<FName> GetAllNPCTemplateIds() const;

	/** Every loot table id, sorted. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Data")
	TArray<FName> GetAllLootTableIds() const;

	/** Every zone id, sorted. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Data")
	TArray<FName> GetAllZoneIds() const;

	// ── Counts (also what the load banner logs) ─────────────────────────

	/** Number of loaded class templates. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Data")
	int32 GetClassCount() const { return Tables.Classes.Num(); }

	/** Number of loaded item templates. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Data")
	int32 GetItemCount() const { return Tables.Items.Num(); }

	/** Number of loaded skill templates. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Data")
	int32 GetSkillCount() const { return Tables.Skills.Num(); }

	/** Number of loaded NPC templates. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Data")
	int32 GetNPCTemplateCount() const { return Tables.NPCTemplates.Num(); }

	/** Number of loaded loot tables. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Data")
	int32 GetLootTableCount() const { return Tables.LootTables.Num(); }

	/** Number of loaded zones. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Data")
	int32 GetZoneCount() const { return Tables.Zones.Num(); }

	// ── Static loader ───────────────────────────────────────────────────

	/**
	 * Parse every table out of an already-resolved absolute data root.
	 *
	 * Static and world-free on purpose: the automation tests and the Python
	 * validation tool need to load the data without a UGameInstance, and
	 * keeping the parsing out of the subsystem keeps it that way.
	 *
	 * @return true when all seven files were read and parsed.
	 */
	static bool LoadTablesFromRoot(const FString& ResolvedDataRoot, FValhallaDataTables& OutTables);

	/**
	 * The seven filenames LoadTablesFromRoot reads, in load order.
	 *
	 * Exposed because Phase 6b's hot-reload watcher has to stat exactly this set
	 * every two seconds, and a watcher with its own copy of the list is a
	 * watcher that silently stops noticing the eighth file somebody adds.
	 */
	static const TArray<FString>& GetDataFilenames();

	// ── Where the data comes from (backlog B-03) ────────────────────────
	//
	// A dedicated server, and any client that can see the repo's shared/data
	// (every editor session), reads DataRoot as it always has. A client that
	// can't (a packaged build on someone else's PC) reads its own copy in
	// Saved/Data instead: seeded from the copy staged into the build
	// (Content/Data), then kept current by UValhallaBackendSubsystem::
	// SyncGameData from the backend's /api/data. `valhalla.Data.ForceDownload 1`
	// makes editor clients take the download path too, for testing it.

	/** Read `InRoot` instead of DataRoot from the next LoadAll/Reload on. Empty restores DataRoot. */
	void SetDataRootOverride(const FString& InRoot);

	/** The downloaded-copy directory in use, or empty when reading DataRoot. */
	const FString& GetDataRootOverride() const { return DataRootOverride; }

	/** True when this game instance reads the downloaded copy rather than DataRoot. */
	bool IsUsingDownloadedData() const { return !DataRootOverride.IsEmpty(); }

	/** `<Project>/Saved/Data`: where a client keeps the files downloaded from the backend. */
	static FString GetDownloadedDataDir();

	/** `<Project>/Content/Data`: the copy staged into a packaged build, used until the first download. */
	static FString GetBundledDataDir();

	/** True when every file in GetDataFilenames() exists in `Dir`. */
	static bool HasAllDataFiles(const FString& Dir);

	// ── B-13: what was loaded ───────────────────────────────────────────

	/**
	 * Lower-case hex SHA-1 of each data file's bytes as of the last LoadAll,
	 * keyed by file name (GetDataFilenames). The backend's /api/data manifest
	 * hashes the same way, and the web editor compares these with the files
	 * on disk to answer "did my reload actually apply?". A file that could not
	 * be read is absent.
	 */
	const TMap<FString, FString>& GetLoadedFileHashes() const { return LoadedFileHashes; }

	/** When the last LoadAll ran (UTC). */
	const FDateTime& GetLoadedAtUtc() const { return LoadedAtUtc; }

private:
	/** Decide between DataRoot and the downloaded copy, once, at Initialize. */
	void ChooseDataSource();

	/** See SetDataRootOverride. */
	FString DataRootOverride;

	/** All parsed tables. Plain data; see FValhallaDataTables. */
	FValhallaDataTables Tables;

	/** The absolute root LoadAll last read from. */
	FString LoadedDataRoot;

	/** Set by a successful LoadAll. */
	bool bLoaded = false;

	/** See GetLoadedFileHashes. */
	TMap<FString, FString> LoadedFileHashes;

	/** See GetLoadedAtUtc. */
	FDateTime LoadedAtUtc;
};
