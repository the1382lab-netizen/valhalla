// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaPartySubsystem.h"

#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "ValhallaCombatLibrary.h"
#include "ValhallaGame.h"
#include "ValhallaInventoryLibrary.h"
#include "ValhallaInventoryTypes.h"
#include "ValhallaPlayerController.h"
#include "ValhallaPlayerState.h"

bool UValhallaPartySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	// Editor preview and inactive worlds have no players and no server to be
	// authoritative; creating a party manager for them is pure overhead.
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->IsGameWorld());
}

UValhallaPartySubsystem* UValhallaPartySubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UValhallaPartySubsystem>() : nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Lookups
// ─────────────────────────────────────────────────────────────────────────────

AValhallaPlayerState* UValhallaPartySubsystem::FindPlayerByName(const FString& CharacterName) const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!GameState || CharacterName.IsEmpty())
	{
		return nullptr;
	}

	// GameRoom.ts:371 — case-insensitive, first match wins. 1.0 never enforced
	// unique names at runtime either; CharacterService does it at creation.
	for (APlayerState* Entry : GameState->PlayerArray)
	{
		AValhallaPlayerState* ValhallaPS = Cast<AValhallaPlayerState>(Entry);
		if (ValhallaPS && ValhallaPS->CharacterName.Equals(CharacterName, ESearchCase::IgnoreCase))
		{
			return ValhallaPS;
		}
	}

	return nullptr;
}

FValhallaParty* UValhallaPartySubsystem::FindPartyFor(const AValhallaPlayerState* PlayerState)
{
	if (!PlayerState || PlayerState->PartyId == 0)
	{
		return nullptr;
	}
	return Parties.Find(PlayerState->PartyId);
}

const FValhallaParty* UValhallaPartySubsystem::FindPartyFor(const AValhallaPlayerState* PlayerState) const
{
	if (!PlayerState || PlayerState->PartyId == 0)
	{
		return nullptr;
	}
	return Parties.Find(PlayerState->PartyId);
}

int32 UValhallaPartySubsystem::GetPartyId(const AValhallaPlayerState* PlayerState) const
{
	return PlayerState ? PlayerState->PartyId : 0;
}

TArray<AValhallaPlayerState*> UValhallaPartySubsystem::GetPartyMembers(const AValhallaPlayerState* PlayerState) const
{
	TArray<AValhallaPlayerState*> Result;

	const FValhallaParty* Party = FindPartyFor(PlayerState);
	if (!Party)
	{
		return Result;
	}

	for (const TWeakObjectPtr<AValhallaPlayerState>& Member : Party->Members)
	{
		if (AValhallaPlayerState* Resolved = Member.Get())
		{
			Result.Add(Resolved);
		}
	}

	return Result;
}

bool UValhallaPartySubsystem::AreInSameParty(const AValhallaPlayerState* A, const AValhallaPlayerState* B) const
{
	// GameRoom.ts:1240 — a player is not their own party member. That is what
	// makes this usable as "should this AoE spare them".
	if (!A || !B || A == B)
	{
		return false;
	}

	return A->PartyId != 0 && A->PartyId == B->PartyId;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Notification
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaPartySubsystem::SendSystemChat(AValhallaPlayerState* PlayerState, const FString& Message)
{
	AValhallaPlayerController* Controller = PlayerState ? Cast<AValhallaPlayerController>(PlayerState->GetOwner()) : nullptr;
	if (!Controller)
	{
		return;
	}

	FValhallaChatMessage Line;
	Line.Channel = EValhallaChatChannel::System;
	Line.Message = Message;
	Line.Timestamp = UValhallaCombatLibrary::GetServerTime(PlayerState);

	Controller->ClientChatMessage(Line);

	UE_LOG(LogValhallaGame, Log, TEXT("party [system -> %s] %s"), *PlayerState->CharacterName, *Message);
}

void UValhallaPartySubsystem::BroadcastPartyUpdate(int32 PartyId)
{
	FValhallaParty* Party = Parties.Find(PartyId);
	if (!Party)
	{
		return;
	}

	// GameRoom.ts:1165 — the member list is built once and sent to everyone, so
	// every client agrees about the order.
	TArray<FString> Names;
	for (const TWeakObjectPtr<AValhallaPlayerState>& Member : Party->Members)
	{
		if (const AValhallaPlayerState* Resolved = Member.Get())
		{
			Names.Add(Resolved->CharacterName);
		}
	}

	for (const TWeakObjectPtr<AValhallaPlayerState>& Member : Party->Members)
	{
		AValhallaPlayerState* Resolved = Member.Get();
		if (!Resolved)
		{
			continue;
		}

		Resolved->PartyId = PartyId;
		Resolved->PartyMemberNames = Names;

		if (AValhallaPlayerController* Controller = Cast<AValhallaPlayerController>(Resolved->GetOwner()))
		{
			// The fields above replicate on their own schedule; this is the
			// immediate nudge, which is what PARTY_UPDATE was.
			Controller->ClientPartyUpdate(PartyId, Names);
		}
	}

	UE_LOG(LogValhallaGame, Log, TEXT("partyUpdate party %d: %s"), PartyId, *FString::Join(Names, TEXT(", ")));
}

void UValhallaPartySubsystem::ClearPartyView(AValhallaPlayerState* PlayerState)
{
	if (!PlayerState)
	{
		return;
	}

	PlayerState->PartyId = 0;
	PlayerState->PartyMemberNames.Reset();

	if (AValhallaPlayerController* Controller = Cast<AValhallaPlayerController>(PlayerState->GetOwner()))
	{
		Controller->ClientPartyUpdate(0, TArray<FString>());
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Invite — GameRoom.ts:364
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaPartySubsystem::Invite(AValhallaPlayerState* Inviter, const FString& TargetName)
{
	if (!Inviter || TargetName.IsEmpty())
	{
		return;
	}

	AValhallaPlayerState* Target = FindPlayerByName(TargetName);
	if (!Target)
	{
		SendSystemChat(Inviter, FString::Printf(TEXT("Player \"%s\" is not online."), *TargetName));
		return;
	}

	if (Target == Inviter)
	{
		SendSystemChat(Inviter, TEXT("You cannot invite yourself."));
		return;
	}

	// GameRoom.ts:393 — the target's party first, then the inviter's. Two
	// separate messages, because they mean different things to whoever is reading.
	if (const FValhallaParty* TargetParty = FindPartyFor(Target))
	{
		if (TargetParty->Members.Num() >= ValhallaPartyMaxMembers)
		{
			SendSystemChat(Inviter, FString::Printf(TEXT("%s's party is full."), *Target->CharacterName));
			return;
		}
	}

	if (const FValhallaParty* InviterParty = FindPartyFor(Inviter))
	{
		if (InviterParty->Members.Num() >= ValhallaPartyMaxMembers)
		{
			SendSystemChat(Inviter, TEXT("Your party is full."));
			return;
		}
	}

	// GameRoom.ts:414 — one pending invite per invitee; a second one overwrites
	// the first rather than queueing, so /accept is never ambiguous.
	FValhallaPartyInvite& Invite_ = PendingInvites.FindOrAdd(Target);
	Invite_.Inviter = Inviter;
	Invite_.ExpiresAt = UValhallaCombatLibrary::GetServerTime(Inviter) + ValhallaPartyInviteExpirySeconds;

	SendSystemChat(Target, FString::Printf(TEXT("%s has invited you to a party. Type /accept to join."), *Inviter->CharacterName));
	SendSystemChat(Inviter, FString::Printf(TEXT("Invite sent to %s."), *Target->CharacterName));

	// Phase 8b: the invitee's HUD raises an Accept / Decline prompt from this.
	if (AValhallaPlayerController* TargetController = Cast<AValhallaPlayerController>(Target->GetOwner()))
	{
		TargetController->ClientPartyInvite(Inviter->CharacterName);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Accept — GameRoom.ts:422
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaPartySubsystem::Accept(AValhallaPlayerState* Accepter)
{
	if (!Accepter)
	{
		return;
	}

	const FValhallaPartyInvite* Found = PendingInvites.Find(Accepter);
	if (!Found)
	{
		SendSystemChat(Accepter, TEXT("You have no pending party invite."));
		return;
	}

	const double Now = UValhallaCombatLibrary::GetServerTime(Accepter);
	if (Now > Found->ExpiresAt)
	{
		// No 1.0 equivalent; see ValhallaPartyInviteExpirySeconds.
		PendingInvites.Remove(Accepter);
		SendSystemChat(Accepter, TEXT("That party invite has expired."));
		return;
	}

	AValhallaPlayerState* Inviter = Found->Inviter.Get();
	PendingInvites.Remove(Accepter);

	if (!Inviter)
	{
		SendSystemChat(Accepter, TEXT("Whoever invited you is no longer online."));
		return;
	}

	// GameRoom.ts:435 — the inviter's party is created lazily, on the first
	// acceptance. There is no such thing as a party of one waiting for members.
	int32 PartyId = Inviter->PartyId;
	if (PartyId == 0)
	{
		PartyId = NextPartyId++;
		FValhallaParty& NewParty = Parties.Add(PartyId);
		NewParty.Members.Add(Inviter);
		Inviter->PartyId = PartyId;
	}

	FValhallaParty* Party = Parties.Find(PartyId);
	if (!Party)
	{
		return;
	}

	if (Party->Members.Num() >= ValhallaPartyMaxMembers)
	{
		SendSystemChat(Accepter, TEXT("The party is full."));
		return;
	}

	// GameRoom.ts:449 — leaving the old party is silent, because the player is
	// not leaving so much as moving, and two contradictory notices in one frame
	// read as a bug.
	Leave(Accepter, /*bSilent=*/true);

	Party->Members.Add(Accepter);
	Accepter->PartyId = PartyId;

	for (const TWeakObjectPtr<AValhallaPlayerState>& Member : Party->Members)
	{
		if (AValhallaPlayerState* Resolved = Member.Get())
		{
			SendSystemChat(Resolved, FString::Printf(TEXT("%s has joined the party."), *Accepter->CharacterName));
		}
	}

	BroadcastPartyUpdate(PartyId);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Decline — GameRoom.ts:466
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaPartySubsystem::Decline(AValhallaPlayerState* Decliner)
{
	if (!Decliner)
	{
		return;
	}

	const FValhallaPartyInvite* Found = PendingInvites.Find(Decliner);
	if (!Found)
	{
		// GameRoom.ts:468 returns silently here. 2.0 says so, because a player
		// who typed /decline deserves to know it did nothing.
		SendSystemChat(Decliner, TEXT("You have no pending party invite."));
		return;
	}

	AValhallaPlayerState* Inviter = Found->Inviter.Get();
	PendingInvites.Remove(Decliner);

	if (Inviter)
	{
		SendSystemChat(Inviter, FString::Printf(TEXT("%s declined your party invite."), *Decliner->CharacterName));
	}
	SendSystemChat(Decliner, TEXT("You declined the party invite."));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Leave — GameRoom.ts:1186 removeFromParty
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaPartySubsystem::Leave(AValhallaPlayerState* Leaver, bool bSilent)
{
	if (!Leaver || Leaver->PartyId == 0)
	{
		return;
	}

	const int32 PartyId = Leaver->PartyId;
	FValhallaParty* Party = Parties.Find(PartyId);
	if (!Party)
	{
		ClearPartyView(Leaver);
		return;
	}

	const bool bWasLeader = Party->Members.Num() > 0 && Party->Members[0].Get() == Leaver;

	Party->Members.RemoveAll([Leaver](const TWeakObjectPtr<AValhallaPlayerState>& Member)
	{
		return Member.Get() == Leaver;
	});

	ClearPartyView(Leaver);

	if (!bSilent)
	{
		SendSystemChat(Leaver, TEXT("You have left the party."));
	}

	// GameRoom.ts:1202 — one member left is not a party.
	if (Party->Members.Num() <= 1)
	{
		for (const TWeakObjectPtr<AValhallaPlayerState>& Member : Party->Members)
		{
			if (AValhallaPlayerState* Resolved = Member.Get())
			{
				if (!bSilent)
				{
					SendSystemChat(Resolved, TEXT("The party has been disbanded."));
				}
				ClearPartyView(Resolved);
			}
		}

		Parties.Remove(PartyId);
		UE_LOG(LogValhallaGame, Log, TEXT("party %d disbanded after %s left."), PartyId, *Leaver->CharacterName);
		return;
	}

	if (!bSilent)
	{
		for (const TWeakObjectPtr<AValhallaPlayerState>& Member : Party->Members)
		{
			if (AValhallaPlayerState* Resolved = Member.Get())
			{
				SendSystemChat(Resolved, FString::Printf(TEXT("%s has left the party."), *Leaver->CharacterName));
			}
		}

		// Leader transfer. No 1.0 equivalent — 1.0 had no leader to transfer —
		// but with one, the rule has to be stated: the oldest remaining member
		// takes it, which is what removing index 0 already leaves behind. Saying
		// so in the log is the whole of the feature until something reads it.
		if (bWasLeader)
		{
			if (AValhallaPlayerState* NewLeader = Party->Members.Num() > 0 ? Party->Members[0].Get() : nullptr)
			{
				for (const TWeakObjectPtr<AValhallaPlayerState>& Member : Party->Members)
				{
					if (AValhallaPlayerState* Resolved = Member.Get())
					{
						SendSystemChat(Resolved, FString::Printf(TEXT("%s is now the party leader."), *NewLeader->CharacterName));
					}
				}
				UE_LOG(LogValhallaGame, Log, TEXT("party %d leader -> %s"), PartyId, *NewLeader->CharacterName);
			}
		}
	}

	BroadcastPartyUpdate(PartyId);
}

void UValhallaPartySubsystem::HandlePlayerLeft(AValhallaPlayerState* PlayerState)
{
	if (!PlayerState)
	{
		return;
	}

	// GameRoom.onLeave:696 — out of the party, and out of everyone's invites.
	Leave(PlayerState, /*bSilent=*/false);

	PendingInvites.Remove(PlayerState);
	for (auto It = PendingInvites.CreateIterator(); It; ++It)
	{
		if (It.Value().Inviter.Get() == PlayerState)
		{
			It.RemoveCurrent();
		}
	}
}

void UValhallaPartySubsystem::ExpireInvites(double Now)
{
	// A player state that was destroyed without a Logout — a seamless travel, a
	// PIE shutdown mid-frame — leaves a stale weak pointer in a party. Sweeping
	// here keeps GetPartyMembers honest without every caller having to check.
	TArray<int32> PartyIds;
	Parties.GetKeys(PartyIds);
	for (const int32 PartyId : PartyIds)
	{
		PruneParty(PartyId);
	}

	if (PendingInvites.Num() == 0)
	{
		return;
	}

	for (auto It = PendingInvites.CreateIterator(); It; ++It)
	{
		AValhallaPlayerState* Invitee = It.Key().Get();

		if (!Invitee || !It.Value().Inviter.IsValid() || Now > It.Value().ExpiresAt)
		{
			if (Invitee && Now > It.Value().ExpiresAt)
			{
				SendSystemChat(Invitee, TEXT("Your party invite has expired."));
			}
			It.RemoveCurrent();
		}
	}
}

void UValhallaPartySubsystem::PruneParty(int32 PartyId)
{
	FValhallaParty* Party = Parties.Find(PartyId);
	if (!Party)
	{
		return;
	}

	Party->Members.RemoveAll([](const TWeakObjectPtr<AValhallaPlayerState>& Member)
	{
		return !Member.IsValid();
	});

	if (Party->Members.Num() <= 1)
	{
		for (const TWeakObjectPtr<AValhallaPlayerState>& Member : Party->Members)
		{
			ClearPartyView(Member.Get());
		}
		Parties.Remove(PartyId);
	}
}
