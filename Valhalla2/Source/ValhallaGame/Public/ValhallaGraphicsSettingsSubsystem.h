// Copyright Valhalla 2.0. All Rights Reserved.
//
// B-27 Phase 4: the live copy of the player's graphics settings
// (FValhallaGraphicsSettings, ValhallaCore), applied to the engine and synced
// per account.
//
// ## When they are applied
//
//   start   Saved/Settings/graphics_local.json (what this PC applied last), or
//           the defaults on a first launch (High, VSync off, no cap). Before
//           anyone logs in, so the front end already runs with them.
//   login   the front end calls LoadForAccount: the account's cache file, then
//           GET /api/account/settings; the newer UpdatedAt wins (the backend on
//           a tie) and is applied at once, still on the front end, before the
//           world loads. A newer local copy, or one the backend has never seen
//           (404), is PUT back. Unreachable / 401: the cache stays.
//   change  the options menu's Graphics tab (Mutate): applied at once, saved
//           (both cache files, then PUT) SaveDelaySeconds after the last change.
//
// A dev session with no login (PIE on L_World, the benchmarks' clients) uses
// graphics_local.json only. Applying goes through UGameUserSettings
// (ApplyNonResolutionSettings, then SaveSettings, so GameUserSettings.ini agrees)
// plus r.MotionBlurQuality. In the editor (PIE) nothing is applied until the
// player changes something, so opening PIE never changes the editor's own
// scalability. Not created on a dedicated server; never applied without a
// renderer (-nullrhi).
//
// `valhalla.Graphics [preset=low|medium|high|epic] [gi=0|1] [scale=50..100]
// [cap=0|30..360] [vsync=0|1] [mb=0|1]` applies without saving (benchmarks,
// comparisons); with no arguments it prints what is in force.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ValhallaGraphicsSettings.h"
#include "ValhallaGraphicsSettingsSubsystem.generated.h"

VALHALLAGAME_API DECLARE_LOG_CATEGORY_EXTERN(LogValhallaGraphics, Log, All);

DECLARE_MULTICAST_DELEGATE_OneParam(FOnValhallaGraphicsSettingsChanged, const FValhallaGraphicsSettings& /*Settings*/);

UCLASS()
class VALHALLAGAME_API UValhallaGraphicsSettingsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem interface
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem interface

	static UValhallaGraphicsSettingsSubsystem* Get(const UObject* WorldContextObject);

	/** The settings in force. */
	const FValhallaGraphicsSettings& Get() const { return Settings; }

	/** Edit: Sanitize, stamp UpdatedAt, apply, broadcast; saved SaveDelaySeconds later. */
	void Mutate(TFunctionRef<void(FValhallaGraphicsSettings&)> Edit);

	/** Everything back to the defaults, applied and saved at once. */
	void ResetToDefaults();

	/**
	 * The front end, at login: this account's settings (cache now, the backend
	 * when it answers), applied before the world loads. `Token` is kept in
	 * memory only, for the PUTs (the in-world backend session's token is
	 * preferred once there is one for the same user).
	 */
	void LoadForAccount(int32 UserId, const FString& Token);

	/** Log out: a pending change is saved, the account forgotten (the settings stay in force). */
	void ForgetAccount();

	/** Write the cache files and PUT now. */
	void Save();
	void FlushPendingSave();

	/** Apply `InSettings` to the engine (no save). */
	static void ApplyToEngine(const FValhallaGraphicsSettings& InSettings);

	/** Saved/Settings/graphics_<id>.json, or graphics_local.json for 0. */
	static FString CachePathFor(int32 UserId);

	int32 GetUserId() const { return UserId; }

	/** `valhalla.Graphics` (registered once for the process; see the .cpp). */
	void RunConsoleCommand(const TArray<FString>& Args);
	bool IsDirty() const { return bDirty; }

	FOnValhallaGraphicsSettingsChanged OnChanged;

	static constexpr float SaveDelaySeconds = 2.f;

private:
	bool TickDebounce(float DeltaSeconds);
	void Adopt(const FValhallaGraphicsSettings& New, const TCHAR* Source);
	void WriteCache(int32 ForUserId) const;
	bool ReadCache(int32 ForUserId, FValhallaGraphicsSettings& Out) const;
	FString CurrentToken() const;
	void PushToBackend();
	/** Can this process apply at all (a renderer), and should it now (not the editor, unless the player asked)? */
	static bool CanApply();

	FValhallaGraphicsSettings Settings;
	/** 0: no account (graphics_local.json only). */
	int32 UserId = 0;
	FString AccountToken;
	bool bDirty = false;
	double LastChangeSeconds = 0.0;
	int32 LoadSerial = 0;
	FTSTicker::FDelegateHandle TickHandle;
};
