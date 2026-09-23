// Copyright Valhalla 2.0. All Rights Reserved.
//
// Phase 8b: the chat box's command grammar — GameScene.ts:5222
// `submitChatInput`, as a pure function so the automation test can pin it.
//
// The server still parses `/invite`, `/accept`, `/decline`, `/leave`, `/w` and
// `/world` out of a *general* message (AValhallaPlayerController::
// TryHandleChatCommand), because a console or an old client can still send
// one. The chat box does not rely on that: it parses here, sends each message
// on its real channel and calls the party RPCs directly, exactly as 1.0's
// client did. Both parsers accept the same grammar; this one adds `/g` and
// `/p` and the channel the box is sitting on.

#pragma once

#include "CoreMinimal.h"
#include "ValhallaInventoryTypes.h"

/** What a submitted line asks for. */
enum class EValhallaChatAction : uint8
{
	/** Empty after trimming: nothing happens. */
	None,
	/** Send `Text` on `Channel` (to `Target` for a whisper). */
	Send,
	/** `/invite <name>` — ServerPartyInvite(Target). */
	PartyInvite,
	/** `/accept`. */
	PartyAccept,
	/** `/decline`. */
	PartyDecline,
	/** `/leave`. */
	PartyLeave,
	/** A known command used wrongly; `Text` is the usage line to show locally. */
	Usage,
	/** `/something-else`; `Text` is "Unknown command: /something-else". */
	Unknown,
};

struct VALHALLAGAME_API FValhallaChatParse
{
	EValhallaChatAction Action = EValhallaChatAction::None;
	EValhallaChatChannel Channel = EValhallaChatChannel::General;
	FString Text;
	FString Target;

	/**
	 * True when the command switches the box's sticky channel, as `/g` and
	 * `/world` did in 1.0 (GameScene.ts:5236 `this.chatCurrentChannel = …`).
	 * `/w` deliberately does not: a whisper is one line, not a mode.
	 */
	bool bSetsChannel = false;
};

namespace ValhallaChat
{
	/**
	 * Parse one submitted line.
	 *
	 *   /g /general <text>        general, sticky
	 *   /world /y <text>          world, sticky
	 *   /p /party <text>          party, sticky
	 *   /w /whisper <name> <text> whisper
	 *   /invite <name>  /accept  /decline  /leave
	 *   anything else with a leading slash → Unknown
	 *   no slash → Send on `CurrentChannel`
	 */
	VALHALLAGAME_API FValhallaChatParse Parse(const FString& Raw, EValhallaChatChannel CurrentChannel);

	/** "general" / "world" / "whisper" / "party" — ServerChat's channel strings. */
	VALHALLAGAME_API const TCHAR* ChannelToWire(EValhallaChatChannel Channel);

	/** "[General]" etc., for the chat box's channel label. */
	VALHALLAGAME_API FString ChannelLabel(EValhallaChatChannel Channel);

	/** Tab's cycle: General → World → Party → General. */
	VALHALLAGAME_API EValhallaChatChannel NextChannel(EValhallaChatChannel Channel);
}
