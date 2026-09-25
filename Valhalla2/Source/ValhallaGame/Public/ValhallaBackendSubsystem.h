// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ValhallaInventoryLibrary.h"
#include "ValhallaInventoryTypes.h"
#include "ValhallaTypes.h"
#include "ValhallaBackendSubsystem.generated.h"

class AValhallaPlayerState;
class FJsonObject;

VALHALLAGAME_API DECLARE_LOG_CATEGORY_EXTERN(LogValhallaBackend, Log, All);

// ─────────────────────────────────────────────────────────────────────────────
//  Wire shapes
// ─────────────────────────────────────────────────────────────────────────────

/**
 * `CharacterSummary` (server/src/services/CharacterService.ts:23).
 *
 * What a login and `GET /api/characters` return, and all the character-select
 * screen ever needs: four fields and no inventory. The full row only crosses
 * the wire on `load`, and only ever to the game server.
 */
USTRUCT(BlueprintType)
struct VALHALLAGAME_API FValhallaCharacterSummary
{
	GENERATED_BODY()

	/** `characters.id`. The primary key, and the `characterId` join option. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	int32 Id = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	FName ClassId;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	int32 Level = 1;
};

/**
 * What `POST /api/auth/login` and `/register` return.
 *
 * The token is a JWT the client holds and never inspects: 2.0 treats it as an
 * opaque bearer string, hands it to the game server in the join URL, and lets
 * the backend be the only thing that can read it. That is why nothing here
 * parses a JWT, and why the *server* asks `/api/auth/verify` rather than
 * validating a signature it would need the backend's secret to check.
 */
USTRUCT(BlueprintType)
struct VALHALLAGAME_API FValhallaAuthSession
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	FString Token;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	int32 UserId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	FString Username;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	TArray<FValhallaCharacterSummary> Characters;

	/** HTTP status of the login that produced this (or failed to). 403 = the account is banned. */
	int32 HttpStatus = 0;

	bool IsValid() const { return !Token.IsEmpty() && UserId > 0; }
};

/** What `POST /api/auth/verify` returns to the game server. */
USTRUCT(BlueprintType)
struct VALHALLAGAME_API FValhallaVerifiedToken
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	int32 UserId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	FString Username;

	/** Unix ms, or 0 when the backend did not say. Informational only. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	double ExpiresAt = 0.0;

	bool IsValid() const { return UserId > 0; }
};

/**
 * `LoadedCharacter` (CharacterService.ts:41) — the whole saved row.
 *
 * `PositionX` / `PositionY` are the opaque numbers the `position_x` /
 * `position_y` columns hold. 1.0 wrote map pixels into them; 2.0 writes
 * **zone-local centimetres**, which are the same numbers for the same place
 * because Phase 3 fixed 1 px == 1 cm and because the zone's min corner is the
 * origin in both. Nothing else in 2.0 may touch those two fields: the
 * conversion is `UValhallaAdminServer::ToZoneLocalCm` / `FromZoneLocalCm`, as
 * it is for the admin API, and for the same reason — `L_Desert` sits at world
 * X +40000 and that offset must never reach a database column.
 *
 * `ActionBar` is the character's saved bar (eight skill ids, "" for empty):
 * the game mode puts it on the pawn at spawn and saves the pawn's bar back.
 */
USTRUCT(BlueprintType)
struct VALHALLAGAME_API FValhallaLoadedCharacter
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	int32 Id = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	int32 UserId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	FName ClassId;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	int32 Level = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	int32 Xp = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	float Hp = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	float Mana = 0.f;

	/** Zone-local centimetres. See the struct comment. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	double PositionX = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	double PositionY = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	FName ZoneId;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	bool bAlive = true;

	/** Dense, in the backend's `slotIndex` order. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	TArray<FValhallaInventorySlot> Inventory;

	/** ValhallaEquipSlotCount entries, indexed by ValhallaEquipSlotToIndex. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	TArray<FName> Equipment;

	/** Carried through untouched; see the struct comment. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	TArray<FString> ActionBar;

	FValhallaLoadedCharacter()
	{
		Equipment.SetNum(ValhallaEquipSlotCount);
	}

	/** True when nothing has ever been saved over the row `createCharacter` wrote. */
	bool HasAnyEquipment() const
	{
		for (const FName& ItemId : Equipment)
		{
			if (!ItemId.IsNone())
			{
				return true;
			}
		}
		return false;
	}
};

/**
 * `SaveCharacterData` (CharacterService.ts:61) — everything a save writes.
 *
 * Note what is *not* here, and matches 1.0: the class and the character name.
 * Both are set at creation and the game server has no business changing
 * either, so a save that carried them would be a save that could rename
 * somebody's character because a URL option said so.
 */
USTRUCT(BlueprintType)
struct VALHALLAGAME_API FValhallaSaveData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	float Hp = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	float Mana = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	int32 Xp = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	int32 Level = 1;

	/** Zone-local centimetres — see FValhallaLoadedCharacter. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	double PositionX = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	double PositionY = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	FName ZoneId;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	bool bAlive = true;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	TArray<FValhallaInventorySlot> Inventory;

	/** ValhallaEquipSlotCount entries, indexed by ValhallaEquipSlotToIndex. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	TArray<FName> Equipment;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Backend")
	TArray<FString> ActionBar;

	FValhallaSaveData()
	{
		Equipment.SetNum(ValhallaEquipSlotCount);
	}
};

/**
 * A loaded row resolved against the data tables: what the game server actually
 * writes onto an AValhallaPlayerState.
 *
 * Separated from the apply itself so that `Valhalla.Game.Backend.ApplyLoaded`
 * can check the clamping without a world, a net driver or a player state —
 * the same split as `ResolveDamage` and its rolls, and as `BuildStateJson` and
 * its snapshot.
 */
struct VALHALLAGAME_API FValhallaAppliedCharacter
{
	FString CharacterName;
	FName ClassId;
	int32 Level = 1;
	int32 Xp = 0;
	FName ZoneId;
	bool bAlive = true;

	TArray<FValhallaInventorySlot> Inventory;
	TArray<FName> Equipment;

	/** Class base + level growth + equipment, exactly as RecomputeStats does it. */
	FValhallaResolvedStats Stats;

	float Hp = 0.f;
	float MaxHp = 0.f;
	float Mana = 0.f;
	float MaxMana = 0.f;
	float Energy = 0.f;
	float MaxEnergy = 0.f;
	float VisionRange = 0.f;

	/**
	 * True when this row has never been played: no inventory, no equipment and
	 * level 1. Only then may the game server grant the class kit — see
	 * `UValhallaBackendSubsystem::ResolveLoadedCharacter`.
	 */
	bool bNeedsStartingItems = false;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Callbacks
// ─────────────────────────────────────────────────────────────────────────────
//
// TFunction rather than a dynamic multicast delegate, because every one of
// these is a one-shot continuation for one request and the callers are all
// C++. A UObject that wants to survive the round trip captures a weak pointer
// to itself, which is what the front end and the game mode both do.

/** bSuccess, the session, and the backend's own error string when it failed. */
using FValhallaAuthCallback = TFunction<void(bool /*bSuccess*/, const FValhallaAuthSession& /*Session*/, const FString& /*Error*/)>;
using FValhallaCharacterListCallback = TFunction<void(bool, const TArray<FValhallaCharacterSummary>& /*Characters*/, const FString& /*Error*/)>;
using FValhallaCharacterCallback = TFunction<void(bool, const FValhallaCharacterSummary& /*Character*/, const FString& /*Error*/)>;
using FValhallaVerifyCallback = TFunction<void(bool, const FValhallaVerifiedToken& /*Verified*/, const FString& /*Error*/)>;
using FValhallaLoadCallback = TFunction<void(bool, const FValhallaLoadedCharacter& /*Character*/, const FString& /*Error*/)>;
using FValhallaSimpleCallback = TFunction<void(bool /*bSuccess*/, const FString& /*Error*/)>;
/** The raw parsed body, for the admin account routes whose answer goes straight back to the dashboard. */
/** bOk, how many files were replaced (0 = already current), and why it failed. */
using FValhallaDataSyncCallback = TFunction<void(bool /*bOk*/, int32 /*FilesUpdated*/, const FString& /*Error*/)>;
using FValhallaJsonCallback = TFunction<void(bool /*bSuccess*/, int32 /*StatusCode*/, const TSharedPtr<FJsonObject>& /*Json*/, const FString& /*Error*/)>;
/**
 * B-21 `GET /api/characters/:id/settings`: bOk, the HTTP status (404 = nothing
 * saved, or not this account's character: use the defaults; 0 = unreachable),
 * the `ui` document and the backend's `updatedAt` (ISO 8601).
 */
using FValhallaSettingsGetCallback = TFunction<void(bool /*bOk*/, int32 /*StatusCode*/, const TSharedPtr<FJsonObject>& /*Ui*/, const FString& /*UpdatedAt*/, const FString& /*Error*/)>;
/** B-21 `PUT /api/characters/:id/settings`: bOk, the status, the stored `updatedAt`. */
using FValhallaSettingsPutCallback = TFunction<void(bool /*bOk*/, int32 /*StatusCode*/, const FString& /*UpdatedAt*/, const FString& /*Error*/)>;

// ─────────────────────────────────────────────────────────────────────────────
//  The subsystem
// ─────────────────────────────────────────────────────────────────────────────

/**
 * The 1.0 account backend, as seen from Unreal.
 *
 * ## Why the 1.0 server and not a UE-native service
 *
 * Phase 7's open question was whether to keep 1.0's Express + SQLite + JWT
 * stack or build a new one. Kept, and the reason is the same one the whole
 * port rests on: the 1.0 repo is the source of truth for data, and an account
 * is data. Two services would mean two user tables, two password hashes and a
 * decision about which one a player's characters really live in. One service
 * means a 1.0 client and a 2.0 client log into the same account and see the
 * same characters, which is the only version of this that can be tested
 * against 1.0 at all.
 *
 * ## Two audiences, one class
 *
 * A GameInstance subsystem exists on the client *and* on the dedicated
 * server, and the split is by method, not by instance:
 *
 *   client  Register / Login / ListCharacters / CreateCharacter / DeleteCharacter
 *           / GetCharacterSettings / PutCharacterSettings (B-21)
 *           — authenticated by the player's own bearer token.
 *   server  Verify / LoadCharacter / SaveCharacter / Health
 *           — authenticated by `X-Server-Secret`, which is the shared secret in
 *           `UValhallaDataSettings::GetServerSecret()` and which a client must never
 *           have. Nothing stops a client calling them; what stops it is that a
 *           packaged client's ini does not carry the real secret.
 *
 * A player's JWT never travels server-to-server except once, as the body of
 * `verify`, and the game server never stores it past that call.
 *
 * ## Logging
 *
 * Every call logs at Verbose: the verb, the path, the status and the elapsed
 * ms. Passwords are never logged at all, and a token is logged as its first
 * eight characters — enough to tell two sessions apart in a log, not enough to
 * be one.
 */
UCLASS()
class VALHALLAGAME_API UValhallaBackendSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem interface

	/** The subsystem for any world context object, or null outside a game instance. */
	static UValhallaBackendSubsystem* Get(const UObject* WorldContextObject);

	// ── Client routes (bearer token) ────────────────────────────────────

	/** `POST /api/auth/register`. A 409 comes back as bSuccess=false with the reason. */
	void Register(const FString& Username, const FString& Password, FValhallaAuthCallback OnDone);

	/** `POST /api/auth/login`. */
	void Login(const FString& Username, const FString& Password, FValhallaAuthCallback OnDone);

	/** `GET /api/characters`, Authorization: Bearer <Token>. */
	void ListCharacters(const FString& Token, FValhallaCharacterListCallback OnDone);

	/** `POST /api/characters`. The backend grants the class kit; 2.0 must not. */
	void CreateCharacter(const FString& Token, const FString& Name, FName ClassId, FValhallaCharacterCallback OnDone);

	/** `DELETE /api/characters/:id`. */
	void DeleteCharacter(const FString& Token, int32 CharacterId, FValhallaSimpleCallback OnDone);

	/** B-21 `GET /api/characters/:id/settings` — the character's UI settings document. */
	void GetCharacterSettings(const FString& Token, int32 CharacterId, FValhallaSettingsGetCallback OnDone);

	/** B-21 `PUT /api/characters/:id/settings` with body `{ "ui": Ui }` (the backend refuses over 64 KB). */
	void PutCharacterSettings(const FString& Token, int32 CharacterId, const TSharedRef<FJsonObject>& Ui, FValhallaSettingsPutCallback OnDone);

	// ── The in-world player session (B-21) ─────────────────────────────

	/**
	 * The front end hands the session over at Enter World, just before the
	 * ClientTravel that destroys it (the token otherwise lives only on
	 * AValhallaFrontEndController). Held in memory only — never written to
	 * disk, never replicated, never logged but redacted — so that the in-world
	 * client can call its own player routes (UI settings) with its own bearer
	 * token. Cleared when the front end opens again or logs out.
	 */
	void SetPlayerSession(const FString& Token, int32 UserId, int32 CharacterId);
	void ClearPlayerSession();
	bool HasPlayerSession() const { return !PlayerToken.IsEmpty() && PlayerCharacterId > 0; }
	const FString& GetPlayerToken() const { return PlayerToken; }
	/** `characters.id` of the character this client entered the world as; 0 = none (offline PIE). */
	int32 GetPlayerCharacterId() const { return PlayerCharacterId; }
	int32 GetPlayerUserId() const { return PlayerUserId; }

	// ── Account (B-12) ─────────────────────────────────────────────────

	/**
	 * B-12 `POST /api/auth/password`. On success `OnDone` gets the replacement
	 * token; the old one, and every other session on the account, stops working.
	 */
	void ChangePassword(const FString& Token, const FString& CurrentPassword, const FString& NewPassword,
		TFunction<void(bool /*bSuccess*/, const FString& /*NewToken*/, const FString& /*Error*/)> OnDone);

	/**
	 * B-12 `POST /api/auth/account/delete`: the account, its characters and their
	 * items, permanently. `Confirm` is the account name typed out by the player.
	 */
	void DeleteAccount(const FString& Token, const FString& Password, const FString& Confirm, FValhallaSimpleCallback OnDone);

	// ── Server routes (X-Server-Secret) ─────────────────────────────────

	/** `POST /api/auth/verify` — is this token a real, unexpired session, and whose. */
	void Verify(const FString& Token, FValhallaVerifyCallback OnDone);

	/** `GET /api/characters/:id/load?userId=<n>` — the whole row, ownership checked backend-side. */
	void LoadCharacter(int32 CharacterId, int32 UserId, FValhallaLoadCallback OnDone);

	/** `PUT /api/characters/:id/save`. */
	void SaveCharacter(int32 CharacterId, const FValhallaSaveData& Data, FValhallaSimpleCallback OnDone);

	/** `GET /api/health` — is the backend up at all. Used by the front end's banner. */
	void Health(FValhallaSimpleCallback OnDone);

	// ── Account admin (X-Server-Secret; driven by the admin API) ────────

	/**
	 * `POST /api/accounts/ban`. Identify the account by UserId (> 0) or, when
	 * that is zero, by Username. Minutes <= 0 is a permanent ban. `By` is the
	 * name written to `users.banned_by`.
	 */
	void BanAccount(int32 UserId, const FString& Username, double Minutes, const FString& Reason, const FString& By, FValhallaJsonCallback OnDone);

	/** `POST /api/accounts/unban`, same identification rules as BanAccount. */
	void UnbanAccount(int32 UserId, const FString& Username, FValhallaJsonCallback OnDone);

	/** `GET /api/accounts/bans` — every ban still in force. */
	void ListBans(FValhallaJsonCallback OnDone);

	// ── Game data (no auth; backlog B-03) ───────────────────────────────

	/**
	 * Bring the game data files in `TargetDir` up to date with the backend.
	 *
	 * Reads `GET /api/data/manifest`, compares each file's SHA-1 with the copy
	 * in TargetDir, downloads only the ones that differ, checks each download
	 * against the manifest, and writes them only once *all* of them arrived —
	 * so a dropped connection never leaves half old, half new data. The caller
	 * reloads UValhallaDataSubsystem when FilesUpdated > 0.
	 */
	void SyncGameData(const FString& TargetDir, FValhallaDataSyncCallback OnDone);

	// ── Pure helpers ────────────────────────────────────────────────────

	/**
	 * Everything the game server writes onto a player state for a loaded row.
	 *
	 * Pure: no world, no actor, no HTTP. Resolves the stat block the same way
	 * `AValhallaPlayerState::RecomputeStats` does (class base + level growth +
	 * equipment), then clamps the saved hp/mana into the pools that block
	 * implies — because a character saved at level 3 in +12 STR gear and loaded
	 * after a balance edit dropped that item's bonus would otherwise come back
	 * with more HP than it can hold.
	 *
	 * `bNeedsStartingItems` is the first-entry test: no inventory, no equipment
	 * and level 1. 1.0's `createCharacter` (CharacterService.ts:167) already
	 * writes the class kit into `character_equipment` and `inventory_items` at
	 * creation, so a character created through the API arrives *with* its kit
	 * and this is false. It is true only for a row that predates that — and
	 * granting then is what keeps a 1.0-era character from arriving naked.
	 */
	static void ResolveLoadedCharacter(
		const FValhallaLoadedCharacter& Loaded,
		const FValhallaClassTemplate& ClassTemplate,
		FValhallaItemLookup FindItem,
		FValhallaAppliedCharacter& Out);

	/**
	 * The single place a save body is built. Server only.
	 *
	 * Reads the player state for everything except the position, which comes
	 * from the pawn and is converted to zone-local centimetres through the
	 * zone subsystem. A player state with no pawn (dead, or mid-respawn) saves
	 * the position it loaded with rather than the origin, which is why
	 * `LastSavedPosition` exists on the game mode.
	 */
	static FValhallaSaveData BuildSaveData(const AValhallaPlayerState& PlayerState, const FVector2D& ZoneLocalCm, const TArray<FString>& ActionBar);

	// ── JSON, exposed for the tests ─────────────────────────────────────

	/** The exact body `PUT /api/characters/:id/save` receives. */
	static TSharedRef<FJsonObject> SaveDataToJson(const FValhallaSaveData& Data);

	/** The inverse. Tolerant in the same way UValhallaDataSubsystem is. */
	static bool SaveDataFromJson(const TSharedPtr<FJsonObject>& Json, FValhallaSaveData& Out);

	/**
	 * Parse a `LoadedCharacter` document.
	 *
	 * Deliberately tolerant about two shapes the backend has been seen to use,
	 * because the route that serves this was written in a different repo by a
	 * different hand and the cost of being wrong is a character that loads
	 * naked rather than an error:
	 *
	 *   inventory  `slotIndex` or `slot` for the index; either may be absent,
	 *              in which case array order is the order.
	 *   equipment  either `[{slotType, itemId}, …]` — which is what
	 *              `CharacterService.loadCharacter` returns — or the object map
	 *              `{weapon: …, helm: …}`.
	 *
	 * Anything naming a slot that `ParseEquipSlotName` does not know is skipped
	 * with a warning, never fatal.
	 */
	static bool LoadedCharacterFromJson(const TSharedPtr<FJsonObject>& Json, FValhallaLoadedCharacter& Out);

	/** First eight characters and an ellipsis. The only form a token is ever logged in. */
	static FString RedactToken(const FString& Token);

	/** How long any one call may take before it is a failure, seconds. */
	static constexpr float RequestTimeoutSeconds = 10.f;

	// ── Why the last game server let this client go (B-14) ──────────────

	/**
	 * Kept by AValhallaPlayerController::ClientWasKicked (a kick, or a ban with
	 * its end date and reason) so the front end can show it after the
	 * disconnect has sent the client back to L_FrontEnd. Lives here because
	 * this subsystem is per game instance and survives that travel.
	 */
	void SetDisconnectNotice(const FString& Notice) { DisconnectNotice = Notice; }

	/** The notice, and clears it. Empty when the last session ended normally. */
	FString TakeDisconnectNotice()
	{
		FString Out = MoveTemp(DisconnectNotice);
		DisconnectNotice.Reset();
		return Out;
	}

private:
	FString DisconnectNotice;

	/** SetPlayerSession's. Memory only. */
	FString PlayerToken;
	int32 PlayerUserId = 0;
	int32 PlayerCharacterId = 0;

	/**
	 * Fire one request and hand the parsed body to a continuation.
	 *
	 * `bServerAuth` chooses the secret header over the bearer one. The parsed
	 * JSON is passed through even on a failure status, because that is where
	 * the backend puts `{ "error": "Username already taken." }` and that
	 * sentence is what the login screen shows the player.
	 */
	void Send(
		const FString& Verb,
		const FString& Path,
		const TSharedPtr<FJsonObject>& Body,
		const FString& BearerToken,
		bool bServerAuth,
		TFunction<void(bool /*bSuccess*/, int32 /*StatusCode*/, const TSharedPtr<FJsonObject>& /*Json*/, const FString& /*Error*/)> OnDone);

	/** `<BackendUrl><Path>`. */
	FString MakeUrl(const FString& Path) const;
};
