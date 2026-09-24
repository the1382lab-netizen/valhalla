// Copyright Valhalla 2.0. All Rights Reserved.
//
// B-21: the live copy of the player's UI settings (FValhallaUserUISettings,
// ValhallaCore) for the character this client is playing, and its sync.
//
// ## Where the character comes from
//
// The front end knows the character at Enter World and hands the session to
// UValhallaBackendSubsystem (SetPlayerSession: token, user id, character id,
// memory only) just before the ClientTravel. Both subsystems are per game
// instance and survive the travel; the HUD asks this one to load for
// `GetPlayerCharacterId()` when it is created. Without a session (an offline
// PIE on L_World, no front end) the character id is 0: settings then live in
// the disk cache only.
//
// ## Sync
//
//   load   the disk cache (Saved/UI/settings_<id>.json) applies at once, then
//          GET /api/characters/:id/settings; the newer UpdatedAt wins (the
//          backend on a tie). A newer local copy, or a local copy the backend
//          has never seen (404), is PUT back. Unreachable / 401: the cache stays.
//   change Mutate / Set stamp UpdatedAt, mark dirty and broadcast OnChanged;
//          the save (cache file, then PUT) follows SaveDelaySeconds after the
//          last change. ResetToDefaults, FlushPendingSave (the HUD going away),
//          FlushAndForget (the front end opening) and Deinitialize save at once.
//
// Client only: not created on a dedicated server.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ValhallaUserUISettings.h"
#include "ValhallaUserSettingsSubsystem.generated.h"

VALHALLAGAME_API DECLARE_LOG_CATEGORY_EXTERN(LogValhallaUserSettings, Log, All);

DECLARE_MULTICAST_DELEGATE_OneParam(FOnValhallaUserSettingsChanged, const FValhallaUserUISettings& /*Settings*/);

UCLASS()
class VALHALLAGAME_API UValhallaUserSettingsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem interface
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem interface

	/** The subsystem for any world context object, or null (no game instance, a dedicated server). */
	static UValhallaUserSettingsSubsystem* Get(const UObject* WorldContextObject);

	/** The current settings (the defaults until a load). */
	const FValhallaUserUISettings& Get() const { return Settings; }

	/** Edit in place: Sanitize, stamp UpdatedAt, mark dirty (debounced save), broadcast. */
	void Mutate(TFunctionRef<void(FValhallaUserUISettings&)> Edit);

	/** Replace wholesale (UnknownFields of the current copy are kept when New has none), as Mutate. */
	void Set(const FValhallaUserUISettings& New);

	/** One section back to the defaults, broadcast, and saved at once. */
	void ResetToDefaults(EValhallaUISettingsSection Section);

	/** Write the cache file and PUT to the backend now. No-op before a load. */
	void Save();

	/** Save now when a change is still waiting for its debounce. */
	void FlushPendingSave();

	/** FlushPendingSave, then forget the character: the next Ensure loads afresh. */
	void FlushAndForget();

	/**
	 * Load for `CharacterId` (0 = offline, cache only): the cache now
	 * (broadcast), then the backend when this client has a session for that
	 * character (broadcast again if it wins). A pending change for the
	 * previous character is saved first.
	 */
	void LoadForCharacter(int32 CharacterId);

	/** LoadForCharacter(the backend session's character, or 0) unless that one is already loaded. */
	void EnsureLoadedForCurrentCharacter();

	FOnValhallaUserSettingsChanged OnChanged;

	/** INDEX_NONE before any load. */
	int32 GetCharacterId() const { return CharacterId; }
	bool IsLoaded() const { return CharacterId != INDEX_NONE; }
	bool IsDirty() const { return bDirty; }
	/** True while the backend's copy is being fetched. */
	bool IsFetching() const { return bFetching; }

	/** Saved/UI/settings_<id>.json, or settings_offline.json for 0. */
	static FString CachePathFor(int32 InCharacterId);

	/** How long after the last change the save happens, seconds. */
	static constexpr float SaveDelaySeconds = 2.f;

private:
	bool TickDebounce(float DeltaSeconds);
	void MarkChanged();
	void Broadcast();
	void WriteCache() const;
	bool ReadCache(int32 InCharacterId, FValhallaUserUISettings& Out) const;
	/** PUT when this client has a backend session for CharacterId. */
	void PushToBackend();
	/** The backend session's token when it is for CharacterId, else empty. */
	FString TokenForCurrentCharacter() const;

	FValhallaUserUISettings Settings;
	int32 CharacterId = INDEX_NONE;
	bool bDirty = false;
	bool bFetching = false;
	double LastChangeSeconds = 0.0;
	/** Bumped by every load, so a late GET for a previous character is dropped. */
	int32 LoadSerial = 0;
	FTSTicker::FDelegateHandle TickHandle;
};
