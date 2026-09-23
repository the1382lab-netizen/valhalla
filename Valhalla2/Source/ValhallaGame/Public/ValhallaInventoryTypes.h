// Copyright Valhalla 2.0. All Rights Reserved.
//
// The replicated shapes Phase 2c adds: an inventory slot, a loot bag slot and a
// chat line. All three are the 2.0 spelling of a Colyseus schema class, and all
// three are deliberately two or three fields wide — they cross the wire.

#pragma once

#include "CoreMinimal.h"
#include "ValhallaTypes.h"
#include "ValhallaInventoryTypes.generated.h"

/**
 * One occupied inventory slot. The port of `InventorySlotState`
 * (server/src/schema/PlayerState.ts:20).
 *
 * `ItemId` is never None in a slot that is present: the 1.0 array is *dense*,
 * and an empty slot is an absent element rather than a blank one. See
 * AValhallaPlayerState::Inventory for why that matters.
 */
USTRUCT(BlueprintType)
struct VALHALLAGAME_API FValhallaInventorySlot
{
	GENERATED_BODY()

	/** Key into UValhallaDataSubsystem::FindItem. NAME_None only in a default-constructed slot. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Inventory")
	FName ItemId;

	/** How many. Always 1 for anything whose template is not `stackable`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Inventory")
	int32 Quantity = 1;

	FValhallaInventorySlot() = default;
	FValhallaInventorySlot(FName InItemId, int32 InQuantity)
		: ItemId(InItemId), Quantity(InQuantity) {}

	bool IsEmpty() const { return ItemId.IsNone() || Quantity <= 0; }

	bool operator==(const FValhallaInventorySlot& Other) const
	{
		return ItemId == Other.ItemId && Quantity == Other.Quantity;
	}
};

/**
 * One slot of a ground loot bag. The port of `BagSlotState`
 * (server/src/schema/LootBagState.ts:4).
 *
 * Identical in shape to FValhallaInventorySlot, and kept a separate type for
 * the same reason 1.0 kept two schema classes: they replicate from different
 * actors with different visibility rules, and a bag slot's quantity is a
 * `uint16` where an inventory slot's is a `uint8`.
 */
USTRUCT(BlueprintType)
struct VALHALLAGAME_API FValhallaBagSlot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Loot")
	FName ItemId;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Loot")
	int32 Quantity = 1;

	FValhallaBagSlot() = default;
	FValhallaBagSlot(FName InItemId, int32 InQuantity)
		: ItemId(InItemId), Quantity(InQuantity) {}
};

/**
 * GameRoom.ts:297 — the channels a CHAT_MESSAGE may carry.
 *
 * `System` is not a channel a client may send on; the server uses it for its
 * own notices (GameRoom.sendSystemChat:1151), and ServerChat rejects it coming
 * the other way. `Party` is new in 2.0: 1.0 had a party but routed all party
 * talk through `general`.
 */
UENUM(BlueprintType)
enum class EValhallaChatChannel : uint8
{
	/** Same zone only. GameRoom.ts:321. */
	General		UMETA(DisplayName = "general"),
	/** Everyone connected. GameRoom.ts:318. */
	World		UMETA(DisplayName = "world"),
	/** One named character, plus the sender. GameRoom.ts:331. */
	Whisper		UMETA(DisplayName = "whisper"),
	/** Every member of the sender's party. */
	Party		UMETA(DisplayName = "party"),
	/** Server notices. Never accepted from a client. */
	System		UMETA(DisplayName = "system"),
};

/**
 * One delivered chat line. The port of `ChatMessagePayload` (shared/src/types.ts).
 *
 * Carries the resolved sender name rather than an actor, because the sender may
 * have left by the time the line is drawn and because a name is what the line
 * says anyway.
 */
USTRUCT(BlueprintType)
struct VALHALLAGAME_API FValhallaChatMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Chat")
	EValhallaChatChannel Channel = EValhallaChatChannel::General;

	/** The sender's character name. Empty for a system notice. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Chat")
	FString SenderName;

	/** Already trimmed and length-capped by the server. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Chat")
	FString Message;

	/** Whisper only: who it was addressed to, so the sender's copy reads "To X". */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Chat")
	FString TargetName;

	/** AValhallaGameState::GetServerTime() when the server accepted it. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Chat")
	double Timestamp = 0.0;
};
