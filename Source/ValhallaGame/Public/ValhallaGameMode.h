// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ValhallaBackendSubsystem.h"
#include "ValhallaInventoryTypes.h"
#include "ValhallaGameMode.generated.h"

class UValhallaDataSubsystem;
class UValhallaBackendSubsystem;
class AValhallaCharacter;
class AValhallaNPC;
class AValhallaPlayerState;

/**
 * What the game server knows about one logged-in connection's *account*.
 *
 * Deliberately not on AValhallaPlayerState. A player state replicates, and
 * none of this may: the character row id is the key to somebody's save and the
 * user id is the key to their account. The game mode is server-only by
 * construction, which is the same reason every other decision a client must
 * not make already lives there.
 *
 * Note the absence of the JWT. It is read once out of the join URL, spent on
 * `verify`, and dropped — the game server has no reason to hold a credential
 * it can re-derive nothing from.
 */
struct VALHALLAGAME_API FValhallaBackendSession
{
	/** `characters.id`. Zero for a dev join with no token. */
	int32 CharacterId = 0;

	/** `users.id`, from `verify`. */
	int32 UserId = 0;

	/** The backend's account name, for the log line. Not the character name. */
	FString Username;

	/**
	 * Where this character was the last time anything knew, in zone-local cm.
	 *
	 * Kept so a save taken while the pawn is gone — dead, mid-respawn, or one
	 * frame after a disconnect — writes the last real position rather than the
	 * zone's origin, which is the corner of the map.
	 */
	FVector2D LastZoneLocalCm = FVector2D::ZeroVector;

	/** The zone that position is local to. */
	FName LastZoneId;

	/** Phase 8's, carried through a load and a save untouched. */
	TArray<FString> ActionBar;

	/** The 30 s autosave. Cleared on Logout. */
	FTimerHandle AutosaveTimer;

	/** True between PostLogin and the load completing; suppresses the pawn spawn. */
	bool bLoadPending = false;
};

/**
 * What a `reload-data` did, for the log line and for the HTTP response body.
 *
 * The table counts are there to be *compared*: a designer whose edit dropped
 * `items` from 31 to 30 wants to see that in the dashboard's toast, not to find
 * out an hour later when a loot table stops resolving.
 */
struct VALHALLAGAME_API FValhallaDataReloadCounts
{
	int32 Classes = 0;
	int32 Items = 0;
	int32 Skills = 0;
	int32 NpcTemplates = 0;
	int32 LootTables = 0;
	int32 Zones = 0;

	/** Logged-in players whose stat block was re-resolved. */
	int32 PlayersRecomputed = 0;

	/** Live NPCs whose template was re-read. */
	int32 NpcsUpdated = 0;

	/** NPCs whose template id no longer names anything in the reloaded file. */
	int32 NpcsOrphaned = 0;
};

/**
 * The port of `GameRoom.onAuth` / `GameRoom.onJoin` (server/src/rooms/GameRoom.ts).
 *
 * This class only ever exists on the server — that is not a convention, it is
 * how Unreal works, and it is why every decision that must not be a client's
 * to make lives here: which class a player is, what stats that class resolves
 * to, and where they spawn.
 *
 * 1.0 authenticated a JWT and loaded the character row from Postgres. Phase 2a
 * has no backend (Phase 7 does), so the character comes from the join URL
 * instead:
 *
 *     127.0.0.1?class=wizard?charname=Gandalf
 *
 * Both options are validated against the loaded data tables before they are
 * believed: an unknown class falls back to warrior with a warning rather than
 * spawning a character with no stats.
 *
 * The option is `charname`, not `name`, because `name` is reserved: the engine
 * puts the player's account nickname there on every login, so a `name` option
 * would never be the character's name and the default would never be reached.
 * The account name is already on APlayerState::GetPlayerName().
 *
 * `valhalla.ClassAssignment` is the Play-In-Editor stand-in for the character
 * select screen Phase 7 will add. An in-process PIE listen server gives every
 * client the same URL — AdditionalServerGameOptions is only read when PIE
 * launches a separate server process — so there is no way to ask for a warrior
 * and a wizard in one session through the URL. Setting the cvar to a
 * comma-separated list deals classes out to joiners in turn, which is what
 * makes a two-class party testable at all before Phase 7.
 */
UCLASS()
class VALHALLAGAME_API AValhallaGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AValhallaGameMode();

	//~ Begin AGameModeBase interface
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void PreLoginAsync(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, const FOnPreLoginCompleteDelegate& OnComplete) override;
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal = TEXT("")) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End AGameModeBase interface

	// ── Phase 7: accounts and persistence ───────────────────────────────

	/**
	 * Write one player's character row back to the backend. Server only.
	 *
	 * Called from four places and nowhere else: the autosave timer, a zone
	 * change, `Logout`, and — for the gate — `valhalla.SaveNow`. The body is
	 * built by `UValhallaBackendSubsystem::BuildSaveData` and by nothing else,
	 * which is what stops a fifth caller inventing a fifth idea of what a save
	 * contains.
	 *
	 * `Reason` appears in the log line. A save with no session (a dev join
	 * with no token) is a no-op and is not an error.
	 */
	void SaveCharacterFor(AValhallaPlayerState* ValhallaPS, const TCHAR* Reason);

	/** The same, for every logged-in player. Used by the zone change and the gate. */
	int32 SaveAllCharacters(const TCHAR* Reason);

	/** The backend session for a controller, or null for a dev join. */
	const FValhallaBackendSession* FindBackendSession(const APlayerController* Controller) const;

	/**
	 * Remember where a pawn is, in zone-local cm, so a save taken without one
	 * still writes a real position. Called by the zone tick and by the portal.
	 */
	void NotePlayerPosition(AValhallaPlayerState* ValhallaPS);

	// ── Phase 6b: hot reload ────────────────────────────────────────────

	/**
	 * Re-read the seven JSON files and push the result into everything that
	 * cached a copy of them. Server only. Returns false if any file failed to
	 * load, in which case *nothing* is pushed.
	 *
	 * The push is the whole point, and it is two lists:
	 *
	 *   - Every logged-in player gets `RecomputeStats()` — Phase 2c's class
	 *     template + level growth + equipment aggregation, unchanged. That is
	 *     what makes editing a class's `baseStats` or an item's `statBonuses`
	 *     visible on a character that is already standing there.
	 *   - Every live NPC gets `ReapplyTemplate` with whatever
	 *     `npc-templates.json` now says, because `AValhallaNPC` holds its
	 *     template by value and would otherwise keep the one it spawned with
	 *     forever.
	 *
	 * What is deliberately *not* reloaded: skills already cast, buffs already
	 * applied, and loot already rolled. All three are events that happened
	 * under the old numbers, and retroactively re-resolving them would make the
	 * reload a rewrite of history rather than a change of rules.
	 */
	bool ReloadGameData(FValhallaDataReloadCounts& OutCounts);

	/** The class id used when the join URL does not name one, or names a bad one. */
	UPROPERTY(EditDefaultsOnly, Category = "Valhalla|Join")
	FName DefaultClassId = TEXT("warrior");

	/**
	 * The zone every player joins, and the one a respawn keeps you in.
	 *
	 * `zones.json` lists four zones and marks none of them as the default, so
	 * "the default" is this property and the file is what validates it:
	 * `InitGame` warns if no zone by this id is loaded. `grasslands` because
	 * that is the zone 1.0's `GameRoom.onJoin` put a new character in
	 * (`player.zoneId = 'grasslands'`), and because it is the zone whose
	 * `defaultSpawn` (672, 672) the fallback map generator places its
	 * `player_spawn` at.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Valhalla|Join")
	FName DefaultZoneId = TEXT("grasslands");

	// ── Phase 2b: death, respawn and rewards ────────────────────────────

	/**
	 * GameRoom.ts:1250 `awardKillXP`.
	 *
	 * Solo, the killer takes the whole reward. In a party, the reward is
	 * multiplied by 1.10, floored, then divided equally between the members who
	 * are alive and in the killer's zone — floored again, with a minimum of one
	 * each. Everything about that is 1.0's, including the two separate floors,
	 * which are what make the split lossy: four members splitting a 100 XP kill
	 * get 27 each, not 27.5, and the missing 2 XP are gone rather than going to
	 * anyone.
	 *
	 * Three edge cases 1.0 handles by falling through to the solo branch, all
	 * reproduced: a party of one (`members.size > 1` at GameRoom.ts:1257), a
	 * party in which nobody including the killer is both alive and in the zone
	 * (`zoneSids.length > 0` at :1265), and no party at all.
	 */
	void AwardKillXP(APlayerState* Killer, int32 Xp);

	/**
	 * Called the moment an NPC dies, after its own death bookkeeping. The 2.0
	 * spelling of 1.0 calling `spawnNpcLoot` and `awardKillXP` together out of
	 * `broadcastCombatEvents`.
	 *
	 * Loot first, XP second, and both happen even when the other fails: a loot
	 * table that is missing from loot-tables.json must not cost the killer their
	 * XP, and an unclaimed kill (a DoT from someone who logged out) still drops
	 * a bag for whoever walks past.
	 */
	void OnNPCKilled(AValhallaNPC* Npc, AActor* Killer);

	/**
	 * LootBagSystem.ts:140 `rollLootTable`, resolved against the loaded tables.
	 *
	 * The rolling itself is UValhallaInventoryLibrary::RollLootTable, which takes
	 * its random numbers as an argument and is therefore testable. This is the
	 * thin wrapper that looks the table up and feeds it FMath::FRand — the same
	 * split Phase 1a used for ResolveDamage and its rolls.
	 */
	TArray<FValhallaBagSlot> RollLootTable(FName LootTableId) const;

	/** Put a player down: not alive, hidden, still, and due back in 3 seconds. */
	void HandlePlayerDeath(AValhallaCharacter* Character, AActor* Killer);

	/**
	 * CombatSystem.ts:440 `checkRespawns` — stand up everyone whose timer is up,
	 * at a PlayerStart, with full pools. Called from the fixed tick.
	 */
	void CheckRespawns(double Now);

	/**
	 * Admin: bring a dead player back where they lie, with full pools, and
	 * cancel their pending respawn. False if they are not dead.
	 */
	bool AdminResurrectInPlace(AValhallaPlayerState* ValhallaPS);

	/** Admin: every connected controller logged into this account. */
	TArray<APlayerController*> FindControllersForUser(int32 UserId) const;

protected:
	/** Server time in seconds at which each dead player comes back. */
	TMap<TWeakObjectPtr<AValhallaPlayerState>, double> PendingRespawns;
	/**
	 * GameRoom.onJoin:600-620 — resolve the class, build the stat block, fill
	 * the pools. Runs before Super::PostLogin spawns the pawn so the character
	 * is never alive for a frame with an unresolved class.
	 */
	void InitializeJoiningPlayer(APlayerController* NewPlayer);

	/** The loaded 1.0 data tables, or null if the subsystem is missing. */
	UValhallaDataSubsystem* GetDataSubsystem() const;

	/**
	 * Which class this joiner asked for, before it is validated.
	 *
	 * Precedence, highest first: the `class` URL option, then the next entry of
	 * the `valhalla.ClassAssignment` cvar list, then DefaultClassId.
	 */
	FString ResolveRequestedClass(const FString& JoinOptions) const;

	/**
	 * Ask the backend for a character row and, when it arrives, put it on the
	 * player state and spawn the pawn where it says.
	 *
	 * The pawn spawn is *inside* the continuation on purpose. Spawning first
	 * and teleporting on arrival would put a character in the grasslands for
	 * however long the round trip took — visible to every other player, and
	 * long enough for an NPC to notice.
	 */
	void BeginCharacterLoad(APlayerController* NewPlayer, int32 CharacterId, int32 UserId);

	/** The load's continuation: apply, then spawn. */
	void ApplyLoadedCharacter(APlayerController* NewPlayer, const FValhallaLoadedCharacter& Loaded);

	/** Spawn the pawn for a player whose load is done, at the loaded position. */
	void SpawnLoadedPawn(APlayerController* NewPlayer, const FValhallaAppliedCharacter& Applied);

	/** Start the 30 s autosave for one connection. */
	void StartAutosave(APlayerController* NewPlayer);

	/** `token` off a join URL, tolerating the '&' the contract was written with. */
	static FString ParseTokenOption(const FString& JoinOptions);

private:
	/** Per-connection URL options, captured in InitNewPlayer for PostLogin. */
	UPROPERTY(Transient)
	TMap<TObjectPtr<APlayerController>, FString> PendingJoinOptions;

	/**
	 * What `PreLoginAsync` verified, waiting for the `PostLogin` that follows.
	 *
	 * Keyed by the token, because PreLogin has no controller to key on — it
	 * runs before one exists, which is the entire point of it. Entries are
	 * consumed by the matching PostLogin and swept after
	 * `VerifiedTokenLifetimeSeconds` so a connection that never completed does
	 * not leave a user id in memory for the session.
	 */
	TMap<FString, TPair<FValhallaVerifiedToken, double>> VerifiedTokens;

	/** See VerifiedTokens. Generous: a client still has to finish connecting. */
	static constexpr double VerifiedTokenLifetimeSeconds = 120.0;

	/** Account and character ids per connection. See FValhallaBackendSession. */
	TMap<TWeakObjectPtr<APlayerController>, FValhallaBackendSession> BackendSessions;

	/** Supplies the default `Player<N>` names. */
	int32 JoinCounter = 0;
};
