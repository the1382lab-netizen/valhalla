// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "ValhallaGameTypes.h"
#include "ValhallaInventoryTypes.h"
#include "ValhallaTypes.h"
#include "ValhallaPlayerState.generated.h"

struct FValhallaClassTemplate;
struct FValhallaItemTemplate;

/**
 * The port of `PlayerState` (server/src/schema/PlayerState.ts).
 *
 * The 1.0 schema drew a hard line between the fields Colyseus synced and the
 * plain TypeScript members it did not; that line is reproduced exactly here,
 * because it is an anti-cheat boundary and not a bandwidth optimisation:
 *
 *   replicated   identity, level, the vitals a health bar needs, vision range
 *   owner-only   a read-only copy of this player's own resolved stat block
 *                (ClientStats, B-07), for the character panel
 *   server-only  the authoritative stat block, skill cooldowns, active buffs
 *
 * A client that knew the defender's DodgeRating could predict the outcome of
 * a swing before the server resolved it, so it does not get to know — about
 * anyone else. Since B-07 the owning client does receive its *own* resolved
 * stats (COND_OwnerOnly), because a character sheet needs them and a player
 * knowing their own dodge chance gives nothing away. The server never reads
 * ClientStats back: all combat math still runs on `Stats`, which stays
 * private and unreplicated.
 *
 * Position, rotation and velocity are NOT here. In 1.0 they lived on the
 * schema; in 2.0 they belong to the character's replicated movement.
 */
UCLASS()
class VALHALLAGAME_API AValhallaPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	AValhallaPlayerState();

	//~ Begin AActor interface
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End AActor interface

	//~ Begin APlayerState interface
	/** Carries everything below across a seamless travel or a pawn respawn. */
	virtual void CopyProperties(APlayerState* PlayerState) override;
	//~ End APlayerState interface

	// ── Replicated: identity & progression ──────────────────────────────

	/** PlayerState.ts:38 `characterName`. Distinct from APlayerState's login name. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|Identity")
	FString CharacterName;

	/** PlayerState.ts:39 `classId` — warrior, cleric, ranger, rogue, shaman, wizard. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|Identity")
	FName ClassId = TEXT("warrior");

	/** PlayerState.ts:40 `level`. Phase 2a always joins at 1. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|Identity")
	int32 Level = 1;

	/** PlayerState.ts:41 `xp`. The amount within the current level; spent on level-up. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|Identity")
	int32 Xp = 0;

	/**
	 * XP needed to go from the current Level to the next (stats.ts:199
	 * `xpRequiredForLevel`), or INDEX_NONE (-1) at the level cap. A pure
	 * function of the replicated Level, so it is right on every machine.
	 */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Identity")
	int32 GetXpToNextLevel() const { return XpToNextLevelFor(Level); }

	/** Xp / GetXpToNextLevel(), clamped to [0, 1]; 1 at the level cap. For the XP bar. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Identity")
	float GetXpFraction() const { return XpFractionFor(Level, Xp); }

	/** GetXpToNextLevel for an arbitrary level. Static so tests need no actor. */
	static int32 XpToNextLevelFor(int32 InLevel);

	/** GetXpFraction for an arbitrary level and in-level XP. */
	static float XpFractionFor(int32 InLevel, int32 InXp);

	/** PlayerState.ts:42 `zoneId`. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|Identity")
	FName ZoneId = TEXT("grasslands");

	// ── Replicated: vitals ──────────────────────────────────────────────

	/** PlayerState.ts:45 `hp`. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|Vitals")
	float Hp = 0.f;

	/** PlayerState.ts:46 `maxHp` — FValhallaResolvedStats::MaxHp. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|Vitals")
	float MaxHp = 0.f;

	/** PlayerState.ts:47 `mana`. 0 for energy classes. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|Vitals")
	float Mana = 0.f;

	/** PlayerState.ts:48 `maxMana`. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|Vitals")
	float MaxMana = 0.f;

	/** PlayerState.ts:49 `energy`. 0 for caster classes. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|Vitals")
	float Energy = 0.f;

	/** PlayerState.ts:50 `maxEnergy`. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|Vitals")
	float MaxEnergy = 0.f;

	/** PlayerState.ts:54 `shieldHp` — remaining absorb from Shield of Faith. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|Vitals")
	float ShieldHp = 0.f;

	/** PlayerState.ts:51 `alive`. Phase 2b owns the death/respawn transitions. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|Vitals")
	bool bAlive = true;

	// ── Replicated: vision ──────────────────────────────────────────────

	/**
	 * FValhallaClassTemplate::VisionRange, in 1.0 pixels (== cm, 1 tile is 64 of
	 * both). Replicated because the client draws the fog-of-war edge with it in
	 * Phase 5; the server still does the authoritative culling itself.
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|Vision")
	float VisionRange = 1200.f;

	// ── Replicated, owner only: own resolved stats ──────────────────────

	/**
	 * The owning client's read-only copy of its own resolved stat block (B-07),
	 * for the character panel. Written only by the server, at the end of every
	 * recompute; the server itself never reads it — combat uses `Stats`. See
	 * the class comment for why the owner may know this and nobody else may.
	 */
	const FValhallaResolvedStats& GetClientStats() const { return ClientStats; }

	// ── Replicated: inventory ───────────────────────────────────────────

	/**
	 * PlayerState.ts:77 `inventory`.
	 *
	 * A *dense* array capped at Valhalla::InventoryMaxSlots, not 32 fixed cells
	 * with holes — see UValhallaInventoryLibrary's class comment for why every
	 * function in InventorySystem.ts depends on that.
	 *
	 * Owner only, and that is a rule rather than a saving. 1.0's Colyseus schema
	 * synced the whole `players` map to every client, so one player's client knew
	 * every other player's inventory; 2.0 does not reproduce that, because
	 * knowing who is carrying the rare drop is exactly what a griefer wants and
	 * nothing in the game needs it. What other clients *do* need is the
	 * equipment, for the paperdoll, and that is the nine fields below.
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|Inventory")
	TArray<FValhallaInventorySlot> Inventory;

	// ── Replicated: equipment ───────────────────────────────────────────
	//
	// items.ts:109 `EQUIP_SLOT_FIELD`, field for field and name for name. Nine
	// fields and not a TMap, for the same reason 1.0 used nine schema properties:
	// a map replicates as a keyed delta with per-entry overhead, nine FNames
	// replicate as nine FNames, and the set of slots is fixed by the data.
	//
	// Replicated to everyone, unlike the inventory: Phase 4c draws other
	// players' gear from these, and what someone is visibly wearing is public
	// by definition. NAME_None is the empty string 1.0 used.
	//
	// All nine share one RepNotify. A client that receives two slot changes in
	// the same bunch — which is what swapping a whole armour set looks like —
	// would otherwise re-resolve the paperdoll once per field; sharing the
	// notify means it re-resolves once per bunch, and the per-slot "has this
	// actually changed" check in AValhallaCharacter::ApplySlotVisual makes the
	// redundant passes free anyway.

	/** `equipWeapon`. */
	UPROPERTY(ReplicatedUsing = OnRep_Equipment, BlueprintReadOnly, Category = "Valhalla|Equipment")
	FName EquipWeapon;

	/** `equipOffhand`. */
	UPROPERTY(ReplicatedUsing = OnRep_Equipment, BlueprintReadOnly, Category = "Valhalla|Equipment")
	FName EquipOffhand;

	/** `equipHelm`. */
	UPROPERTY(ReplicatedUsing = OnRep_Equipment, BlueprintReadOnly, Category = "Valhalla|Equipment")
	FName EquipHelm;

	/** `equipChest`. */
	UPROPERTY(ReplicatedUsing = OnRep_Equipment, BlueprintReadOnly, Category = "Valhalla|Equipment")
	FName EquipChest;

	/** `equipLegs`. */
	UPROPERTY(ReplicatedUsing = OnRep_Equipment, BlueprintReadOnly, Category = "Valhalla|Equipment")
	FName EquipLegs;

	/** `equipBoots`. */
	UPROPERTY(ReplicatedUsing = OnRep_Equipment, BlueprintReadOnly, Category = "Valhalla|Equipment")
	FName EquipBoots;

	/** `equipGloves`. */
	UPROPERTY(ReplicatedUsing = OnRep_Equipment, BlueprintReadOnly, Category = "Valhalla|Equipment")
	FName EquipGloves;

	/** `equipBack`. */
	UPROPERTY(ReplicatedUsing = OnRep_Equipment, BlueprintReadOnly, Category = "Valhalla|Equipment")
	FName EquipBack;

	/** `equipRing`. One ring slot, because `EQUIP_SLOT_FIELD` names one. */
	UPROPERTY(ReplicatedUsing = OnRep_Equipment, BlueprintReadOnly, Category = "Valhalla|Equipment")
	FName EquipRing;

	// ── Replicated: party view ──────────────────────────────────────────

	/**
	 * The id of the party this player is in, or 0.
	 *
	 * The party itself lives in UValhallaPartySubsystem and never replicates;
	 * this and PartyMemberNames are the *view* of it, which is what 1.0's
	 * PARTY_UPDATE message carried. Owner only: who else is grouped with whom is
	 * not a client's business.
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|Party")
	int32 PartyId = 0;

	/** Every member's character name, leader first. Empty when PartyId is 0. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|Party")
	TArray<FString> PartyMemberNames;

	// ── Replicated: targeting ───────────────────────────────────────────

	/**
	 * GameScene.ts:3624 `currentTargetId` — what this player has selected.
	 *
	 * Held as a weak pointer so a target that is destroyed (an NPC despawning,
	 * a player leaving) clears itself rather than keeping a dead actor alive,
	 * and replicated through a plain AActor* because TWeakObjectPtr is not a
	 * replicatable property type. TargetActor is the accessor; anything that
	 * needs the selection should go through it.
	 *
	 * Replicated to the owner only. Knowing what everyone else has selected is
	 * exactly the information that makes focus-fire scripting possible.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_TargetActor, BlueprintReadOnly, Category = "Valhalla|Targeting")
	TObjectPtr<AActor> ReplicatedTargetActor = nullptr;

	/** The current selection, or null. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Targeting")
	AActor* GetTargetActor() const { return TargetActor.Get(); }

	/**
	 * GameScene.ts:3624 `setTarget` / 3633 `clearTarget`.
	 * Server only. A null or invalid actor clears the selection and, as in 1.0,
	 * stops the auto-attack that was running against it.
	 */
	void SetTargetActor(AActor* NewTarget);

	// ── Server-only ─────────────────────────────────────────────────────

	/** The full resolved stat block. Never replicated (the owner gets ClientStats) — see the class comment. */
	const FValhallaResolvedStats& GetStats() const { return Stats; }

	/** Skill id (or cooldown group) -> server time in seconds when it comes off cooldown. */
	TMap<FName, double>& GetSkillCooldownExpiry() { return SkillCooldownExpiry; }
	const TMap<FName, double>& GetSkillCooldownExpiry() const { return SkillCooldownExpiry; }

	// ── Admin state (set through the admin API) ─────────────────────────

	/** God mode: damage still lands as an event but never lowers HP. Server only. */
	bool bAdminGodMode = false;

	/**
	 * Frozen by an admin: cannot move. Replicated so the owning client stops
	 * predicting movement instead of rubber-banding against the server.
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|Admin")
	bool bAdminFrozen = false;

	/** Server time until which this player's chat is refused. 0 = not muted. */
	double AdminMutedUntil = 0.0;

	/** Every buff, debuff, DoT and HoT currently applied. Server-only. */
	TArray<FValhallaActiveBuff>& GetActiveBuffs() { return ActiveBuffs; }
	const TArray<FValhallaActiveBuff>& GetActiveBuffs() const { return ActiveBuffs; }

	// ── Server API ──────────────────────────────────────────────────────

	/**
	 * Put this player state into its level-1 join state for a class, exactly as
	 * GameRoom.onJoin does: resolve the stats, fill the pools, copy the vision
	 * range. Authority only; silently ignored on a client.
	 */
	void InitializeFromClass(const FValhallaClassTemplate& ClassTemplate, int32 InLevel, const FString& InCharacterName, FName InZoneId);

	/**
	 * One regen step. Ported from SkillSystem.ts:323-337: energy for non-casters,
	 * mana for casters, each clamped to its pool. Authority only.
	 *
	 * Called by AValhallaGameState at a fixed Valhalla::ServerTickRate, not once
	 * per rendered frame, so the rate is frame-independent like the 1.0 server's.
	 */
	void TickRegen(float DeltaSeconds);

	/** True while this player is up. Phase 2b flips it. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Vitals")
	bool IsAlive() const { return bAlive; }

	/** A one-line summary for LogValhalla. */
	FString DescribeForLog() const;

	/**
	 * GameRoom.ts:1250 `awardKillXP`, the solo half. Adds XP, then levels up
	 * while the threshold is met, recomputing stats and refilling the pools.
	 * Server only. Returns true when a level was gained.
	 */
	bool AwardXp(int32 Amount);

	/** CombatSystem.ts:440 `checkRespawns` — back to full, alive again. Server only. */
	void RespawnWithFullPools();

	/**
	 * Recompute Stats from the class template at the current level *and* the
	 * equipped items, then clamp the pools to their new maximums. Server only.
	 *
	 * Called on every equipment change and on every level-up, which is the union
	 * of 1.0's "join, level-up, and (later) gear changes" (PlayerState.ts:109) —
	 * and the "later" is now.
	 */
	void RecomputeStats();

	// ── Equipment accessors — PlayerState.ts:190 getEquipped / :196 setEquipped ──

	/** The item in a slot, or NAME_None. The 2.0 spelling of `getEquipped`. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Equipment")
	FName GetEquipped(EValhallaEquipSlot Slot) const;

	/** Write a slot. Server only. The 2.0 spelling of `setEquipped`. */
	void SetEquipped(EValhallaEquipSlot Slot, FName ItemId);

	/** The nine fields as a flat array the pure library can work on. */
	TArray<FName> GatherEquipment() const;

	/** Write a flat array back into the nine fields. Server only. */
	void ApplyEquipment(const TArray<FName>& Equipment);

	/** Every non-empty slot, as "weapon=iron_sword offhand=…". For the log and the HUD. */
	FString DescribeEquipment() const;

	/**
	 * CharacterService.ts:169 — give a fresh character its class's
	 * `startingItems`, auto-equipping the ones flagged `equipped`, then
	 * recompute. Server only, and only ever on a first login.
	 *
	 * 1.0 wrote equipment rows straight into `character_equipment` without
	 * consulting `equipItem`, so a starting item is equipped whether or not it
	 * would pass the equip rules. Mirrored: the class's own kit is the one place
	 * the server is allowed to trust the data over the rule.
	 */
	void GrantStartingItems(const FValhallaClassTemplate& ClassTemplate);

	/** The equipped weapon's template, or null. Used by the auto-attack. */
	const FValhallaItemTemplate* GetEquippedWeapon() const;

protected:
	/** Mirror the replicated raw pointer back into the weak one clients read. */
	UFUNCTION()
	void OnRep_TargetActor();

	/**
	 * Redraw the paperdoll on the pawn this player state belongs to.
	 *
	 * Shared by all nine Equip* fields. Silently does nothing when there is no
	 * pawn yet — AValhallaCharacter::OnRep_PlayerState calls
	 * RefreshEquipmentVisuals itself for exactly that case, so a slot that
	 * arrived before the pawn did is not lost.
	 */
	UFUNCTION()
	void OnRep_Equipment();

private:
	/** stats.ts `ResolvedStats`. Server-only; see the class comment. */
	FValhallaResolvedStats Stats;

	/**
	 * A copy of Stats for the owning client (COND_OwnerOnly). Private so only
	 * the server's recompute paths write it; C++ reads it via GetClientStats().
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|Stats", meta = (AllowPrivateAccess = "true"))
	FValhallaResolvedStats ClientStats;

	/** The selection, as everything except the replication layer sees it. */
	TWeakObjectPtr<AActor> TargetActor;

	/** PlayerState.ts:101 `skillCooldowns`. Server-only. */
	TMap<FName, double> SkillCooldownExpiry;

	/** PlayerState.ts:103 `activeBuffs`. Server-only. */
	TArray<FValhallaActiveBuff> ActiveBuffs;
};
