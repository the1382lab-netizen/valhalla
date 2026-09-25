// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ValhallaGameTypes.h"
#include "ValhallaInventoryTypes.h"
#include "ValhallaPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class UValhallaSkillComponent;
class AValhallaCharacter;
class AValhallaLootBag;
class AValhallaPlayerState;
struct FInputActionValue;
struct FInputActionInstance;

/**
 * The port of client/src/systems/InputManager.ts.
 *
 * Every Enhanced Input asset this controller needs is built in code, in
 * SetupInputComponent: one UInputMappingContext and eighteen UInputActions,
 * NewObject'd into this controller and thrown away with it. The project
 * therefore contains no input .uassets, which is deliberate — the key bindings
 * are part of the port and belong in source control as source, not as a binary
 * asset somebody has to open the editor to read.
 *
 * Three things here are the whole feel of the game (controls rework,
 * 2026-09-22):
 *
 *   Camera-relative movement that turns the body. The WASD vector is rotated
 *   by the camera boom's world yaw (W walks away from the camera; 1.0 did the
 *   same with a hard-coded 45, MovementSystem.ts:32), and the character turns
 *   to face the way it walks (orient-to-movement, see AValhallaCharacter).
 *
 *   The right-mouse drag. Hold and drag to orbit the camera; the body turns
 *   with the camera's yaw while standing still (pitch is camera-only). While
 *   walking, orient-to-movement wins and the drag only turns the view.
 *
 *   The cursor does not aim. It stays visible for click-to-target and loot,
 *   and it is read exactly once per press of an `aoeGround` skill, to find
 *   the ground point the skill lands on (SampleCursorGroundPoint). Moving the
 *   mouse never turns the character.
 */
UCLASS()
class VALHALLAGAME_API AValhallaPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AValhallaPlayerController();

	//~ Begin AActor interface
	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;
	//~ End AActor interface

	//~ Begin APlayerController interface
	virtual void SetupInputComponent() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void AcknowledgePossession(APawn* InPawn) override;

	/**
	 * The engine's is empty, so a kicked player used to land on the login
	 * screen with no idea why. Keeps the server's sentence (for a ban: "This
	 * account is suspended until <date> UTC. Reason: <reason>") for the front
	 * end, which shows it after the disconnect (B-14).
	 */
	virtual void ClientWasKicked_Implementation(const FText& KickReason) override;
	//~ End APlayerController interface

	/**
	 * Put PP_Outline on this player's camera, so every level gets the dark
	 * silhouette line whether or not it happens to contain a PostProcessVolume.
	 *
	 * Called from BeginPlay and from both halves of possession — the server's
	 * OnPossess and the client's AcknowledgePossession — because the pawn that
	 * owns the camera may not exist at any one of those moments and the first
	 * one that finds it wins. Idempotent, so being called three times costs a
	 * loop over an array of one.
	 */
	void ApplyOutlinePostProcess();

	/**
	 * The ground point the last `aoeGround` press was aimed at, in world space
	 * at the character's foot height. Sampled from the cursor once, at the key
	 * press (SampleCursorGroundPoint); nothing updates it continuously.
	 *
	 * Only meaningful on the owning client; on the server it is never set.
	 */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Aim")
	FVector GetAimWorldPoint() const { return AimWorldPoint; }

	/**
	 * Intersect the cursor ray with the ground plane at the character's feet.
	 * Stores the point in AimWorldPoint and returns true on a hit; false (and
	 * AimWorldPoint unchanged) with no pawn, no cursor, or the cursor above the
	 * horizon. Owning client only.
	 */
	bool SampleCursorGroundPoint();

	/** SampleCursorGroundPoint's plane maths for any viewport position (pixels). */
	bool SampleGroundPointAtScreen(const FVector2D& ScreenPosition);

	/**
	 * Press a bar slot as if the cursor were at ScreenPosition (viewport
	 * pixels): the key press, with the one cursor read an aoeGround skill makes
	 * taken from there. For `valhalla.UI @class press <slot> <x> <y>` — typing
	 * into the editor console takes the real cursor out of the PIE viewport.
	 */
	void PressActionBarAimedAt(int32 Slot, const FVector2D& ScreenPosition);

	/**
	 * HandleMove's body: one frame of camera-relative WASD (X = strafe,
	 * Y = forward). Public for `valhalla.UI @class walk <w|a|s|d> <seconds>`,
	 * which holds a key by calling it every frame on the owning client.
	 */
	void ApplyMoveInput(const FVector2D& Axis);

	/**
	 * One step of the right-mouse drag: orbit the boom, then turn the body to
	 * the boom's yaw (a no-op while walking). HandleCameraLook's body; public
	 * so `valhalla.UI @class orbit <deg>` can drive the same path in a gate.
	 */
	void OrbitCameraBy(float DeltaYawDegrees, float DeltaPitchDegrees);

	/** The end of a drag: force out the final facing yaw. EndCameraOrbit's tail. */
	void FinishCameraOrbitTurn();

	// ── Phase 2b hooks ──────────────────────────────────────────────────
	// Empty on purpose. The bindings, the slot numbering and the click routing
	// are settled here in 2a so that 2b only has to fill in the bodies.

	/** Action bar key 1-8 pressed. Slot is 1-based, matching the 1.0 UI. */
	UFUNCTION(BlueprintNativeEvent, Category = "Valhalla|Input")
	void OnActionBarPressed(int32 Slot);
	virtual void OnActionBarPressed_Implementation(int32 Slot);

	/** Left mouse button. 1.0 started the auto-attack on the target under it. */
	UFUNCTION(BlueprintNativeEvent, Category = "Valhalla|Input")
	void OnPrimaryClick();
	virtual void OnPrimaryClick_Implementation();

	// The right mouse button is the camera: hold and drag to orbit, turning the
	// character with it while standing (see BeginCameraOrbit). It does not
	// target, attack or loot — attacking is the auto melee / auto ranged skills
	// on the action bar, and they need the character facing the target.

	/** K — the 1.0 skill book toggle. */
	UFUNCTION(BlueprintNativeEvent, Category = "Valhalla|Input")
	void OnToggleSkills();
	virtual void OnToggleSkills_Implementation();

	// ── Targeting ───────────────────────────────────────────────────────

	/**
	 * Ask the server to select an actor. Null clears the selection.
	 * Validated server-side: a client may ask to target anything, and the server
	 * decides whether it is a real, targetable thing.
	 */
	UFUNCTION(Server, Reliable)
	void ServerSetTarget(AActor* NewTarget);

	/**
	 * Target a party member by character name: a click on their party frame
	 * row. Resolved on the server, because the member's pawn may not be
	 * relevant to this client (the party names are all it has). Only a member
	 * of the caller's own party (themselves included) with a pawn in this world
	 * and not past the caller's vision fog; anything else leaves the current
	 * target as it is (never a de-target).
	 */
	UFUNCTION(Server, Reliable)
	void ServerSetTargetByName(const FString& MemberName);

	/** What this player has selected, or null. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Targeting")
	AActor* GetCurrentTarget() const;

	// ── Combat feedback ─────────────────────────────────────────────────

	/**
	 * A combat event meant for this player alone — skillFailed, xpGained,
	 * levelUp. World events arrive on the two batches below instead.
	 */
	UFUNCTION(Client, Unreliable)
	void ClientOnCombatEvent(const FValhallaCombatEvent& Event);

	/**
	 * B-27 Phase 3: this frame's events this player is part of, or a party
	 * member in the same zone is (AValhallaGameState::RouteCombatEvent).
	 * Reliable: the combat log is written from them.
	 */
	UFUNCTION(Client, Reliable)
	void ClientCombatEvents(const TArray<FValhallaCombatEvent>& Events);

	/**
	 * B-27 Phase 3: this frame's events that name an actor this client has
	 * (someone else's fight in view). Unreliable: a lost one is a missing swing
	 * or spark, and a lagging client is not made to queue them.
	 */
	UFUNCTION(Client, Unreliable)
	void ClientCombatEventsSeen(const TArray<FValhallaCombatEvent>& Events);

	/** The pawn's skill component, or null before the pawn exists. */
	UValhallaSkillComponent* GetSkillComponent() const;

	// ── Inventory and equipment — GameRoom.ts:143-206 ───────────────────
	//
	// Every one of these is a *request*. The client names an index or a slot and
	// nothing else; the server reads its own copy of the inventory, its own copy
	// of items.json and its own idea of who is alive, and decides. A client that
	// asks to equip slot 99, or to loot a bag on the other side of the map, is
	// refused rather than trusted — that is the whole reason these are RPCs and
	// not a replicated writable property.
	//
	// All five refuse outright while the player is dead, as 1.0 does
	// (`if (player && player.alive)`, GameRoom.ts:145).

	/** GameRoom.ts:143 EQUIP_ITEM. Equip the item in an inventory slot. */
	UFUNCTION(Server, Reliable)
	void ServerEquipItem(int32 Slot);

	/**
	 * GameRoom.ts:150 UNEQUIP_ITEM.
	 * @param ToSlot  An inventory index to unequip *onto*, or -1 for "the end".
	 *                1.0 sent `targetIndex` only when the player dragged.
	 */
	UFUNCTION(Server, Reliable)
	void ServerUnequipItem(FName EquipSlot, int32 ToSlot = -1);

	/** GameRoom.ts:162 DROP_ITEM, `source: 'inventory'`. Spawns a bag at the player. */
	UFUNCTION(Server, Reliable)
	void ServerDropItem(int32 Slot);

	/** GameRoom.ts:162 DROP_ITEM, `source: 'equipment'`. */
	UFUNCTION(Server, Reliable)
	void ServerDropEquipped(FName EquipSlot);

	/** GameRoom.ts:206 SWAP_INVENTORY. */
	UFUNCTION(Server, Reliable)
	void ServerSwapInventory(int32 A, int32 B);

	// ── Loot — GameRoom.ts:180 / :194 ───────────────────────────────────

	/** GameRoom.ts:180 LOOT_ITEM. Takes one bag slot, reach-checked server-side. */
	UFUNCTION(Server, Reliable)
	void ServerLootItem(AValhallaLootBag* Bag, int32 Slot);

	/** GameRoom.ts:194 LOOT_ALL. */
	UFUNCTION(Server, Reliable)
	void ServerLootAll(AValhallaLootBag* Bag);

	/** The bag whose loot window is open on this client, or null. */
	AValhallaLootBag* GetOpenLootBag() const { return OpenLootBag.Get(); }

	/**
	 * Open a bag's loot window if it is within reach; otherwise say so in chat
	 * ("You are too far away to loot that."), as EverQuest did. Client side.
	 */
	void TryOpenLootBag(AValhallaLootBag* Bag);

	/** Close the loot window (the HUD's loot panel). */
	void CloseLootWindow();

	// ── Party — GameRoom.ts:364-479 ─────────────────────────────────────

	/** GameRoom.ts:364 PARTY_INVITE. */
	UFUNCTION(Server, Reliable)
	void ServerPartyInvite(const FString& TargetName);

	/** GameRoom.ts:422 PARTY_ACCEPT. */
	UFUNCTION(Server, Reliable)
	void ServerPartyAccept();

	/** GameRoom.ts:466 PARTY_DECLINE. */
	UFUNCTION(Server, Reliable)
	void ServerPartyDecline();

	/** GameRoom.ts:478 PARTY_LEAVE. */
	UFUNCTION(Server, Reliable)
	void ServerPartyLeave();

	/**
	 * /sit /stand /wave /cheer /bow (B-15 A-030). `Emote` is the verb without
	 * the slash. /sit toggles, as in EverQuest. Refused while dead or casting.
	 */
	UFUNCTION(Server, Reliable)
	void ServerEmote(FName Emote);

	/** The party view the server last pushed. GameRoom.ts:1161 PARTY_UPDATE. */
	UFUNCTION(Client, Reliable)
	void ClientPartyUpdate(int32 InPartyId, const TArray<FString>& MemberNames);

	// ── Chat — GameRoom.ts:297 ──────────────────────────────────────────

	/**
	 * GameRoom.ts:297 CHAT_MESSAGE.
	 *
	 * @param Channel  "general", "world", "whisper" or "party". Anything else,
	 *                 including "system", is refused: `system` is the server's
	 *                 own voice and a client may not borrow it.
	 * @param Text     Trimmed and capped at 200 characters server-side, as 1.0
	 *                 does (GameRoom.ts:307).
	 * @param Target   Whisper only. The recipient's character name.
	 *
	 * A `general` message beginning with `/` is parsed as a command first — see
	 * TryHandleChatCommand. That parsing lives on the *server* in 2.0 although
	 * 1.0 did it in the client (GameScene.ts:5290-5306), because a command is an
	 * instruction and instructions do not get to be a client's decision. The
	 * grammar is 1.0's, command for command.
	 */
	UFUNCTION(Server, Reliable)
	void ServerChat(const FString& Channel, const FString& Text, const FString& Target);

	/** One delivered chat line. GameScene.ts:5316 `receiveChatMessage`. */
	UFUNCTION(Client, Reliable)
	void ClientChatMessage(const FValhallaChatMessage& Line);

	/** The last few lines, oldest first. Client-side presentation state for the HUD. */
	const TArray<FValhallaChatMessage>& GetChatLog() const { return ChatLog; }

	/** How many lines the client keeps. GameScene's CHAT_MAX_MESSAGES (ui-config `chat.maxMessages`). */
	static constexpr int32 ChatLogMaxLines = 50;

	/** Every line ever received, so the HUD can tell a new line from a trimmed log. */
	int32 GetChatReceivedCount() const { return ChatReceivedCount; }

	/** A client-side system line (a usage message the chat box produced). Never sent. */
	void AddLocalChatLine(const FValhallaChatMessage& Line);

	// ── Phase 8b: the HUD ───────────────────────────────────────────────

	/** The UMG game HUD, or null on a server or before AValhallaHUD has made it. */
	class UValhallaGameHUDWidget* GetGameHUD() const;

	/** B-21: the code-built mapping context (the options menu's Controls tab lists it); null before SetupInputComponent. */
	const UInputMappingContext* GetInputMappingContext() const;

	/**
	 * True while the chat box has keyboard focus. Movement, the action bar
	 * keys and the panel toggles are ignored while it is set — belt to the
	 * UI-only input mode's braces, which already stops them reaching Enhanced
	 * Input at all.
	 */
	void SetUiTyping(bool bTyping) { bUiTyping = bTyping; }
	bool IsUiTyping() const { return bUiTyping; }

	/**
	 * GameRoom.ts:414 — the invitee is told. 1.0 said so in chat only; 2.0
	 * also raises the HUD's Accept / Decline prompt from this.
	 */
	UFUNCTION(Client, Reliable)
	void ClientPartyInvite(const FString& InviterName);

	/** Who invited us, or empty. Cleared by accept, decline, a party update or the invite's expiry. */
	const FString& GetPendingPartyInviter() const { return PendingPartyInviter; }
	void ClearPendingPartyInvite() { PendingPartyInviter.Reset(); }

protected:
	/**
	 * Parse a `/command` out of a general-channel message.
	 *
	 * GameScene.ts:5290 — `/invite <name>`, `/accept`, `/decline`, `/leave`,
	 * plus `/w <name> <text>` and `/world <text>` re-routing to another channel.
	 *
	 * @return true when the message was a command and must not be broadcast.
	 */
	bool TryHandleChatCommand(const FString& Raw);

	/** Deliver one line to one controller, and log it. Server only. */
	static void DeliverChat(class AValhallaPlayerController* To, const FValhallaChatMessage& Line);

	/** The player state, as everything in this file wants it. */
	AValhallaPlayerState* GetValhallaPlayerState() const;

	/**
	 * The loot bag under the cursor, reach or not, found on the dedicated
	 * Interact channel so nothing standing in front of the bag can hide it.
	 */
	AValhallaLootBag* TraceForLootBagUnderCursor() const;

public:
	/** Print one system line in this client's chat log. Client side. */
	void ShowLocalSystemMessage(const FString& Text);

protected:

	/** See GetChatLog. Trimmed to ChatLogMaxLines as lines arrive. */
	TArray<FValhallaChatMessage> ChatLog;
	/** Trace from the cursor for a pawn. Null when the cursor is on empty ground. */
	AActor* TraceForTargetUnderCursor() const;

	/**
	 * The living hostile a ground-targeted skill pressed at ScreenPosition is
	 * aimed at, or null. A visibility trace first (the cursor is on the enemy's
	 * body), then the cursor ray against each hostile's own height rather than
	 * the floor, so pointing at an enemy's chest under a pitched camera counts
	 * as pointing at the enemy and not at the ground behind it.
	 */
	AActor* FindHostileAtScreen(const FVector2D& ScreenPosition) const;
	// ── Input handlers ──────────────────────────────────────────────────

	/** Axis2D (X = strafe, Y = forward), rotated into world space by camera yaw. */
	void HandleMove(const FInputActionValue& Value);

	/** One handler for all eight bar keys; the slot comes from the source action. */
	void HandleActionBar(const FInputActionInstance& Instance);

	void HandlePrimaryClick();
	void HandleToggleSkills();
	/** I: the combined character + inventory panel. */
	void HandleToggleInventory();
	/** Enter: open the chat box. */
	void HandleOpenChat();
	/** Escape: close the topmost panel. */
	void HandleEscape();

	/** Right mouse pressed: start orbiting, hide and park the cursor. */
	void BeginCameraOrbit();
	/** Right mouse released: stop orbiting, send the final facing, put the cursor back. */
	void EndCameraOrbit();
	/**
	 * Mouse delta while orbiting: turn the boom, then the body to the boom's
	 * yaw (AValhallaCharacter::SetFacingYawFromCamera, a no-op while walking).
	 * Ignored when not orbiting — a moving mouse turns nothing.
	 */
	void HandleCameraLook(const FInputActionValue& Value);
	/** Mouse wheel. */
	void HandleCameraZoom(const FInputActionValue& Value);

	// ── Input objects, all built in SetupInputComponent ─────────────────

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> ValhallaMappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MoveAction;

	/** Index 0 is bar slot 1. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> ActionBarActions;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ToggleSkillsAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> PrimaryClickAction;

	/**
	 * Phase 8b: I — the combined character + inventory panel. B-07 removed
	 * 1.0's second key for it (B, IA_Character): one panel, one key.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> InventoryAction;

	/** Phase 8b: Enter — open the chat box. */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ChatAction;

	/** Phase 8b: Escape — close the topmost panel. */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> EscapeAction;

	/** Right mouse button, held. */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CameraOrbitAction;

	/** Mouse2D delta. Only applied while CameraOrbitAction is held. */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CameraLookAction;

	/** Mouse wheel. */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CameraZoomAction;

	/**
	 * Degrees of orbit per unit of Mouse2D delta. The project's input config
	 * scales raw mouse pixels by 0.07 (DefaultInput.ini, AxisConfig), so this
	 * is roughly a quarter of a degree per pixel.
	 */
	static constexpr float CameraOrbitDegreesPerUnit = 3.5f;

	/** The Sobel silhouette material. See build_toon.py for how it is authored. */
	static constexpr const TCHAR* OutlineMaterialPath = TEXT("/Game/Valhalla/Materials/PP_Outline");

	/** Priority of the mapping context. Phase 8's UI pushes its own above this. */
	static constexpr int32 MappingContextPriority = 0;

	/** How many action bar slots the 1.0 UI has. */
	static constexpr int32 ActionBarSlots = 8;

private:
	/** True while the right mouse button is held. */
	bool bCameraOrbiting = false;

	/** See GetOpenLootBag. Closed automatically when out of reach or emptied. */
	TWeakObjectPtr<AValhallaLootBag> OpenLootBag;

	/** Where the cursor was when the orbit started, restored when it ends. */
	FVector2D OrbitCursorRestore = FVector2D::ZeroVector;

	/** See SetUiTyping. */
	bool bUiTyping = false;

	/** See GetChatReceivedCount. */
	int32 ChatReceivedCount = 0;

	/** See GetPendingPartyInviter. */
	FString PendingPartyInviter;

	/** Client world time the invite arrived, for the prompt's expiry. */
	double PendingPartyInviteAt = 0.0;

	/** See GetAimWorldPoint. Written only by SampleGroundPointAtScreen. */
	FVector AimWorldPoint = FVector::ZeroVector;

	/** Set only for the duration of PressActionBarAimedAt. */
	TOptional<FVector2D> AimScreenOverride;
};
