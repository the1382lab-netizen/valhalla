// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaChatCommands.h"

namespace
{
	/** Does `Lower` start with any of the given "/verb " prefixes? Sets the matched length. */
	bool MatchPrefix(const FString& Lower, std::initializer_list<const TCHAR*> Prefixes, int32& OutLength)
	{
		for (const TCHAR* Prefix : Prefixes)
		{
			if (Lower.StartsWith(Prefix))
			{
				OutLength = FCString::Strlen(Prefix);
				return true;
			}
		}
		return false;
	}
}

namespace ValhallaChat
{
	FValhallaChatParse Parse(const FString& RawIn, EValhallaChatChannel CurrentChannel)
	{
		FValhallaChatParse Out;
		const FString Raw = RawIn.TrimStartAndEnd();
		if (Raw.IsEmpty())
		{
			return Out;
		}

		const FString Lower = Raw.ToLower();
		int32 Skip = 0;

		auto Channelled = [&Out, &Raw](EValhallaChatChannel Channel, int32 SkipChars, bool bSticky)
		{
			const FString Message = Raw.Mid(SkipChars).TrimStartAndEnd();
			if (Message.IsEmpty())
			{
				// 1.0 returned silently for an empty `/g ` (GameScene.ts:5234).
				Out.Action = EValhallaChatAction::None;
				return;
			}
			Out.Action = EValhallaChatAction::Send;
			Out.Channel = Channel;
			Out.Text = Message;
			Out.bSetsChannel = bSticky;
		};

		// A bare "/g" or "/world" with nothing after it is a channel switch.
		if (Lower == TEXT("/g") || Lower == TEXT("/general"))
		{
			Out.Action = EValhallaChatAction::None;
			Out.Channel = EValhallaChatChannel::General;
			Out.bSetsChannel = true;
			return Out;
		}
		if (Lower == TEXT("/world") || Lower == TEXT("/y"))
		{
			Out.Action = EValhallaChatAction::None;
			Out.Channel = EValhallaChatChannel::World;
			Out.bSetsChannel = true;
			return Out;
		}
		if (Lower == TEXT("/p") || Lower == TEXT("/party"))
		{
			Out.Action = EValhallaChatAction::None;
			Out.Channel = EValhallaChatChannel::Party;
			Out.bSetsChannel = true;
			return Out;
		}

		if (MatchPrefix(Lower, { TEXT("/general "), TEXT("/g ") }, Skip))
		{
			Channelled(EValhallaChatChannel::General, Skip, true);
			return Out;
		}
		if (MatchPrefix(Lower, { TEXT("/world "), TEXT("/y ") }, Skip))
		{
			Channelled(EValhallaChatChannel::World, Skip, true);
			return Out;
		}
		if (MatchPrefix(Lower, { TEXT("/party "), TEXT("/p ") }, Skip))
		{
			Channelled(EValhallaChatChannel::Party, Skip, true);
			return Out;
		}

		if (Lower == TEXT("/w") || Lower == TEXT("/whisper"))
		{
			Out.Action = EValhallaChatAction::Usage;
			Out.Channel = EValhallaChatChannel::System;
			Out.Text = TEXT("Usage: /w PlayerName message");
			return Out;
		}

		if (MatchPrefix(Lower, { TEXT("/whisper "), TEXT("/w ") }, Skip))
		{
			// GameScene.ts:5244 — "<name> <message>", both required.
			const FString Rest = Raw.Mid(Skip).TrimStart();
			int32 Space = INDEX_NONE;
			Rest.FindChar(TEXT(' '), Space);
			const FString Message = Space > 0 ? Rest.Mid(Space + 1).TrimStartAndEnd() : FString();
			if (Space < 1 || Message.IsEmpty())
			{
				Out.Action = EValhallaChatAction::Usage;
				Out.Channel = EValhallaChatChannel::System;
				Out.Text = TEXT("Usage: /w PlayerName message");
				return Out;
			}
			Out.Action = EValhallaChatAction::Send;
			Out.Channel = EValhallaChatChannel::Whisper;
			Out.Target = Rest.Left(Space);
			Out.Text = Message;
			return Out;
		}

		if (Lower == TEXT("/invite") || MatchPrefix(Lower, { TEXT("/invite ") }, Skip))
		{
			const FString Name = Lower == TEXT("/invite") ? FString() : Raw.Mid(Skip).TrimStartAndEnd();
			if (Name.IsEmpty())
			{
				Out.Action = EValhallaChatAction::Usage;
				Out.Channel = EValhallaChatChannel::System;
				Out.Text = TEXT("Usage: /invite PlayerName");
				return Out;
			}
			Out.Action = EValhallaChatAction::PartyInvite;
			Out.Target = Name;
			return Out;
		}

		if (Lower == TEXT("/accept"))  { Out.Action = EValhallaChatAction::PartyAccept;  return Out; }
		if (Lower == TEXT("/decline")) { Out.Action = EValhallaChatAction::PartyDecline; return Out; }
		if (Lower == TEXT("/leave"))   { Out.Action = EValhallaChatAction::PartyLeave;   return Out; }

		if (Raw.StartsWith(TEXT("/")))
		{
			int32 Space = INDEX_NONE;
			const FString Verb = Raw.FindChar(TEXT(' '), Space) ? Raw.Left(Space) : Raw;
			Out.Action = EValhallaChatAction::Unknown;
			Out.Channel = EValhallaChatChannel::System;
			Out.Text = FString::Printf(TEXT("Unknown command: %s"), *Verb);
			return Out;
		}

		// No prefix: the sticky channel. A whisper is never sticky, so a box
		// that somehow sits on Whisper falls back to General.
		Out.Action = EValhallaChatAction::Send;
		Out.Channel = (CurrentChannel == EValhallaChatChannel::Whisper || CurrentChannel == EValhallaChatChannel::System)
			? EValhallaChatChannel::General : CurrentChannel;
		Out.Text = Raw;
		return Out;
	}

	const TCHAR* ChannelToWire(EValhallaChatChannel Channel)
	{
		switch (Channel)
		{
		case EValhallaChatChannel::World:   return TEXT("world");
		case EValhallaChatChannel::Whisper: return TEXT("whisper");
		case EValhallaChatChannel::Party:   return TEXT("party");
		default:                            return TEXT("general");
		}
	}

	FString ChannelLabel(EValhallaChatChannel Channel)
	{
		switch (Channel)
		{
		case EValhallaChatChannel::World:   return TEXT("[World]");
		case EValhallaChatChannel::Whisper: return TEXT("[Whisper]");
		case EValhallaChatChannel::Party:   return TEXT("[Party]");
		default:                            return TEXT("[General]");
		}
	}

	EValhallaChatChannel NextChannel(EValhallaChatChannel Channel)
	{
		switch (Channel)
		{
		case EValhallaChatChannel::General: return EValhallaChatChannel::World;
		case EValhallaChatChannel::World:   return EValhallaChatChannel::Party;
		default:                            return EValhallaChatChannel::General;
		}
	}
}
