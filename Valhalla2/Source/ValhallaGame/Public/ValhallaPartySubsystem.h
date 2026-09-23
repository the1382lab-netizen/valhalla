// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ValhallaPartySubsystem.generated.h"

class AValhallaPlayerState;

/** devlog_changes.txt, 2026-02-20 — "groups of up to 4 players". GameRoom.ts:396. */
inline constexpr int32 ValhallaPartyMaxMembers = 4;

/**
 * How long a party invite stands before it lapses, seconds.
 *
 * No 1.0 equivalent: `pendingInvites` (GameRoom.ts:80) was a plain Map with no
 * timeout at all, so an invite sent on Tuesday could be accepted on Friday, and
 * the only thing that ever cleared one was accepting, declining, or the invitee
 * disconnecting. Sixty seconds is the rule Phase 2c was asked for, and it is
 * the more defensible behaviour — an invite is an offer, and an offer that
 * never expires is a trap for whoever sent it.
 */
inline constexpr double ValhallaPartyInviteExpirySeconds = 60.0;

/**
 * One party. The 2.0 spelling of an entry in GameRoom's `parties` map
 * (GameRoom.ts:78).
 *
 * 1.0 stored a `Set<string>` of session ids with no ordering and no leader; it
 * had no notion of one either, because nothing in 1.0 needed one — invites did
 * not require leadership and there was no kick. 2.0 keeps an ordered array and
 * calls index 0 the leader, so that Phase 3's zone travel and Phase 7's
 * loot-rights have something to hang off. Ordering is also what makes the
 * member list a client sees stable between updates.
 */
USTRUCT()
struct FValhallaParty
{
	GENERATED_BODY()

	/** Members in join order. Index 0 is the leader. Weak, so a logout empties itself. */
	TArray<TWeakObjectPtr<AValhallaPlayerState>> Members;
};

/** One outstanding invite. Keyed by invitee. */
USTRUCT()
struct FValhallaPartyInvite
{
	GENERATED_BODY()

	TWeakObjectPtr<AValhallaPlayerState> Inviter;

	/** AValhallaGameState::GetServerTime() after which this invite is dead. */
	double ExpiresAt = 0.0;
};

/**
 * The port of GameRoom's party half (state at GameRoom.ts:78-83, handlers at
 * :364/:422/:466/:478, helpers at :1161/:1186).
 *
 * A world subsystem rather than something on the game mode, because the game
 * mode is about joining and dying and this is neither, and because a subsystem
 * is reachable from a player controller's RPC without the controller needing to
 * know what game mode it is under. It only ever does anything on the server:
 * every mutator returns early without authority, and the client's view of its
 * own party arrives as replicated fields on AValhallaPlayerState.
 *
 * Nothing here is replicated. The party *view* is — see
 * AValhallaPlayerState::PartyId and PartyMemberNames — and it is pushed by
 * BroadcastPartyUpdate after every change, which is what GameRoom.sendPartyUpdate
 * did with a PARTY_UPDATE message.
 */
UCLASS()
class VALHALLAGAME_API UValhallaPartySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem interface
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	//~ End USubsystem interface

	/** The subsystem for a world, or null. Convenience for the RPC bodies. */
	static UValhallaPartySubsystem* Get(const UObject* WorldContextObject);

	// ── Commands — GameRoom.ts:364-479 ──────────────────────────────────

	/**
	 * GameRoom.ts:364 PARTY_INVITE. Finds the target by character name,
	 * case-insensitively, and records a pending invite.
	 *
	 * Both parties are size-checked up front, exactly as 1.0 does: inviting
	 * someone into a full party and being in a full party are separate refusals
	 * with separate messages.
	 */
	void Invite(AValhallaPlayerState* Inviter, const FString& TargetName);

	/**
	 * GameRoom.ts:422 PARTY_ACCEPT. Creates the inviter's party if they had none,
	 * then adds the accepter — leaving whatever party they were in first.
	 */
	void Accept(AValhallaPlayerState* Accepter);

	/** GameRoom.ts:466 PARTY_DECLINE. Tells the inviter, and forgets the invite. */
	void Decline(AValhallaPlayerState* Decliner);

	/**
	 * GameRoom.ts:478 PARTY_LEAVE, via `removeFromParty`.
	 *
	 * A party that drops below two members is disbanded, not left as a party of
	 * one — that is 1.0's `party.size <= 1` branch (GameRoom.ts:1202), and it is
	 * why there is no such thing as a solo party to award XP to.
	 *
	 * @param bSilent  Skip the "X has left" notices. Used when a party switch is
	 *                 about to put them somewhere else. GameRoom.ts:1186's flag.
	 */
	void Leave(AValhallaPlayerState* Leaver, bool bSilent = false);

	/** Everything a disconnect has to clean up. GameRoom.onLeave:696. */
	void HandlePlayerLeft(AValhallaPlayerState* PlayerState);

	// ── Queries ─────────────────────────────────────────────────────────

	/** The party id of a player, or 0 for "not in a party". Ids start at 1. */
	int32 GetPartyId(const AValhallaPlayerState* PlayerState) const;

	/** Every member of a player's party, including them. Empty when they have none. */
	TArray<AValhallaPlayerState*> GetPartyMembers(const AValhallaPlayerState* PlayerState) const;

	/**
	 * GameRoom.ts:1239 `isPartyMember`. False for a player and themselves, which
	 * is what makes it usable as a friendly-fire test.
	 */
	bool AreInSameParty(const AValhallaPlayerState* A, const AValhallaPlayerState* B) const;

	/** Drop invites whose 60 seconds are up. Called from the fixed tick. */
	void ExpireInvites(double Now);

	/** Find a connected player by character name, case-insensitively. GameRoom.ts:370. */
	AValhallaPlayerState* FindPlayerByName(const FString& CharacterName) const;

private:
	/** Push PartyId and PartyMemberNames onto every member, then tell their clients. */
	void BroadcastPartyUpdate(int32 PartyId);

	/** Clear one player's replicated party view and tell their client. */
	void ClearPartyView(AValhallaPlayerState* PlayerState);

	/** Send one player a `system` channel chat line. GameRoom.ts:1151 `sendSystemChat`. */
	static void SendSystemChat(AValhallaPlayerState* PlayerState, const FString& Message);

	/** The party a player is in, or null. */
	FValhallaParty* FindPartyFor(const AValhallaPlayerState* PlayerState);
	const FValhallaParty* FindPartyFor(const AValhallaPlayerState* PlayerState) const;

	/** Drop dead weak pointers from a party, disbanding it if too few remain. */
	void PruneParty(int32 PartyId);

	/** partyId -> members. GameRoom.ts:79 `parties`. */
	UPROPERTY()
	TMap<int32, FValhallaParty> Parties;

	/** invitee -> invite. GameRoom.ts:83 `pendingInvites`, plus an expiry. */
	TMap<TWeakObjectPtr<AValhallaPlayerState>, FValhallaPartyInvite> PendingInvites;

	/** GameRoom.ts:84 `nextPartyId`. Starts at 1 so 0 can mean "no party". */
	int32 NextPartyId = 1;
};
