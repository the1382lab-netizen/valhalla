// Copyright Valhalla 2.0. All Rights Reserved.
//
// Phase 7b's front end: the login screen, the character select screen, the
// player controller that owns them and the game mode that spawns it.
//
// All four are in one pair of files on purpose. They are one screen's worth of
// software, none of them is reachable from anywhere else in the game, and the
// alternative — eight files averaging sixty lines — would spread one state
// machine over eight places to look for it.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "ValhallaBackendSubsystem.h"
#include "ValhallaFrontEnd.generated.h"

class AValhallaCharacterPreviewStage;
class AValhallaFrontEndController;
class UCheckBox;
class UComboBoxString;
class UEditableTextBox;
class UImage;
class UOverlay;
class UScrollBox;
class UTextBlock;
class UTextureRenderTarget2D;
class UVerticalBox;
class UWidget;

VALHALLAGAME_API DECLARE_LOG_CATEGORY_EXTERN(LogValhallaFrontEnd, Log, All);

// ─────────────────────────────────────────────────────────────────────────────
//  Shared construction helpers
// ─────────────────────────────────────────────────────────────────────────────

/**
 * The handful of `WidgetTree->ConstructWidget` calls both screens make.
 *
 * Free functions rather than a base class: the two screens share a *look*, not
 * a lifecycle, and a shared UUserWidget base would be an inheritance
 * relationship invented to hold four factory functions.
 */
namespace ValhallaUI
{
	/** The palette. One place, so "dark" means the same dark on both screens. */
	VALHALLAGAME_API extern const FLinearColor Background;
	VALHALLAGAME_API extern const FLinearColor Panel;
	VALHALLAGAME_API extern const FLinearColor PanelSelected;
	VALHALLAGAME_API extern const FLinearColor Ink;
	VALHALLAGAME_API extern const FLinearColor InkDim;
	VALHALLAGAME_API extern const FLinearColor Error;
	VALHALLAGAME_API extern const FLinearColor Accent;
}

/**
 * A list row that knows which row it is.
 *
 * UButton::OnClicked is a dynamic delegate and carries no payload, so a list of
 * N buttons needs N somethings to tell them apart. Subclassing the button is
 * the cheapest of those somethings: no per-row UObject, no index lookup by
 * widget pointer, and the row's identity lives on the row.
 */
UCLASS()
class VALHALLAGAME_API UValhallaCharacterRowButton : public UButton
{
	GENERATED_BODY()

public:
	/** Not focusable: the screen keeps keyboard focus for Up / Down / Enter (B-08a). */
	UValhallaCharacterRowButton(const FObjectInitializer& ObjectInitializer);

	/** Index into the owning screen's character array. */
	UPROPERTY()
	int32 RowIndex = INDEX_NONE;

	/** The screen to tell. Weak because the screen owns the button. */
	UPROPERTY()
	TWeakObjectPtr<class UValhallaCharacterSelectWidget> Screen;

	UFUNCTION()
	void HandleClicked();
};

/** B-08a: a class card on the Create Character panel; knows which class it is. */
UCLASS()
class VALHALLAGAME_API UValhallaClassCardButton : public UButton
{
	GENERATED_BODY()

public:
	UValhallaClassCardButton(const FObjectInitializer& ObjectInitializer);

	UPROPERTY()
	int32 CardIndex = INDEX_NONE;

	UPROPERTY()
	TWeakObjectPtr<class UValhallaCharacterSelectWidget> Screen;

	UFUNCTION()
	void HandleClicked();
};

// ─────────────────────────────────────────────────────────────────────────────
//  Login
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Username, password, Log In, Register, one error line and the backend URL.
 *
 * Built entirely in C++ from `WidgetTree` — there is no `WBP_Login` and there
 * is not going to be one until Phase 8 has a reason for one. The reason is not
 * purity: a Blueprint widget is a binary asset that cannot be reviewed in a
 * diff, and the login screen is the one screen in the game where a silent
 * change to what a field is bound to is a security bug.
 *
 * The backend URL is on screen because the single most common failure of this
 * screen is "the backend is not running" and the single most common cause of
 * *that* being hard to see is not knowing which backend was meant.
 */
UCLASS()
class VALHALLAGAME_API UValhallaLoginWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	//~ Begin UUserWidget interface
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	//~ End UUserWidget interface

	/** Who to tell when the player presses a button. */
	TWeakObjectPtr<AValhallaFrontEndController> Owner;

	/** Show a message on the error line, or clear it with an empty string. */
	void SetError(const FString& Message);

	/** Grey the buttons out while a request is in flight. */
	void SetBusy(bool bBusy);

	/** Fill the fields, for `valhalla.AutoLogin`. */
	void SetCredentials(const FString& Username, const FString& Password);

	/** Replace the small status line under the buttons (game data sync progress). */
	void SetStatusLine(const FString& Text);

	/**
	 * B-08a: after a successful login, store the username if "Remember
	 * username" is ticked (and forget it if not). Only the username is ever
	 * stored, never the password: GameUserSettings.ini, [Valhalla.FrontEnd].
	 */
	void RememberUsernameIfWanted(const FString& Username);

	/** B-08a: the bottom-left server line: online / offline / checking. */
	void SetServerStatus(int32 State);

	/** B-08a: ask the backend's /health and show the answer. */
	void CheckServer();

	/** B-08a: Remember-username storage (tests use the ini section directly). */
	static const TCHAR* RememberSection() { return TEXT("Valhalla.FrontEnd"); }

	/** B-08a: empty the password box (log out). */
	void ClearPassword();

protected:
	UFUNCTION()
	void HandleLoginClicked();

	UFUNCTION()
	void HandleRegisterClicked();

	/** Enter in either field is Log In, because that is what Enter means here. */
	UFUNCTION()
	void HandleTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	/** Everything above, assembled. Idempotent. */
	void BuildUi();

private:
	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> UsernameBox;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> PasswordBox;

	UPROPERTY(Transient)
	TObjectPtr<UButton> LoginButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> RegisterButton;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ErrorText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusText;

	// ── B-08a ────────────────────────────────────────────────────────────

	UPROPERTY(Transient)
	TObjectPtr<UCheckBox> RememberCheck;

	UPROPERTY(Transient)
	TObjectPtr<UImage> ServerDot;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ServerText;

	/** Focus goes to the first empty field once, on the first construct. */
	bool bFocusedOnce = false;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Character select
// ─────────────────────────────────────────────────────────────────────────────

/**
 * The list, Enter World, Create (name + class) and Delete (with a confirm).
 *
 * The class list comes from `UValhallaDataSubsystem::GetAllClassIds` and not
 * from a hard-coded six, for the reason every other list in 2.0 comes from the
 * data: `classes.json` is the 1.0 repo's to edit, and a seventh class must
 * appear here without a recompile.
 *
 * Delete asks. 1.0's own character select did not, and the one thing a
 * character select screen must never do is destroy a level 20 character
 * because a mouse moved two pixels.
 */
UCLASS()
class VALHALLAGAME_API UValhallaCharacterSelectWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	//~ Begin UUserWidget interface
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	//~ End UUserWidget interface

	TWeakObjectPtr<AValhallaFrontEndController> Owner;

	/** (Re)draw the list. Keeps the selection if the id is still present. */
	void SetCharacters(const FString& Username, const TArray<FValhallaCharacterSummary>& InCharacters);

	void SetError(const FString& Message);
	void SetBusy(bool bBusy);

	/** Select by name, for `valhalla.AutoLogin`. False when no such character. */
	bool SelectByName(const FString& CharacterName);

	/** The selected row, or null. */
	const FValhallaCharacterSummary* GetSelected() const;

	/** Called by a row button. */
	void HandleRowClicked(int32 RowIndex);

	/** B-12: the Account panel's result line (errors still go to SetError). */
	void SetAccountStatus(const FString& Message);

	/** B-12: empty the Account panel's password and confirmation boxes. */
	void ClearAccountFields();

	// ── B-08a ────────────────────────────────────────────────────────────

	/** Show the preview stage's render target on the right, or nothing. */
	void SetPreviewTexture(UTextureRenderTarget2D* Target);

	/** Called by a class card on the Create panel. */
	void HandleClassCardClicked(int32 CardIndex);

	/** B-08a: select this character id on the next SetCharacters (the one just created). */
	void PreferCharacter(int32 CharacterId) { PreferredCharacterId = CharacterId; }

	/** Move the selection by Delta rows (Up / Down), clamped. */
	void MoveSelection(int32 Delta);

	/** True while the Create, Delete or Account panel is open over the screen. */
	bool IsModalOpen() const;

protected:
	//~ Begin UUserWidget interface
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	//~ End UUserWidget interface

	UFUNCTION() void HandleEnterWorldClicked();
	UFUNCTION() void HandleCreateClicked();
	UFUNCTION() void HandleCreateConfirmClicked();
	UFUNCTION() void HandleCreateCancelClicked();
	UFUNCTION() void HandleDeleteClicked();
	UFUNCTION() void HandleDeleteConfirmClicked();
	UFUNCTION() void HandleDeleteCancelClicked();
	UFUNCTION() void HandleLogOutClicked();
	UFUNCTION() void HandleAccountClicked();
	UFUNCTION() void HandleAccountCloseClicked();
	UFUNCTION() void HandleChangePasswordClicked();
	UFUNCTION() void HandleDeleteAccountClicked();

	void BuildUi();
	void RefreshRows();

	/** B-08a: the name and "Level N Class · Zone" under the preview; tells the controller. */
	void UpdateSelectedDetails();

	/** B-08a: open one of the three panels over the screen (or none), hiding the preview under it. */
	void ShowModal(UWidget* Card);

	/** B-08a: redraw the class cards' selected state. */
	void RefreshClassCards();

private:
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HeaderText;

	// ── B-08a: Gilded Hall layout ─────────────────────────────────────────

	UPROPERTY(Transient)
	TObjectPtr<UImage> PreviewImage;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> PreviewArea;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SelectedNameText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SelectedLineText;

	UPROPERTY(Transient)
	TObjectPtr<UOverlay> ModalLayer;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> CreateCard;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> DeleteCard;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> AccountCard;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UValhallaClassCardButton>> ClassCards;

	/** Class id per card, in GetAllClassIds order. */
	TArray<FName> ClassCardIds;

	/** Index into ClassCardIds, or INDEX_NONE. */
	int32 SelectedClassCard = INDEX_NONE;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> RowBox;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> CreatePanel;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> DeletePanel;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> NewNameBox;

	UPROPERTY(Transient)
	TObjectPtr<UComboBoxString> NewClassBox;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DeletePrompt;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ErrorText;

	UPROPERTY(Transient)
	TObjectPtr<UButton> EnterWorldButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> CreateButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> DeleteButton;

	// ── B-12: account panel (change password, delete account) ──────────

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> AccountPanel;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> CurrentPasswordBox;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> NewPasswordBox;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> RepeatPasswordBox;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> DeleteAccountPasswordBox;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> DeleteAccountConfirmBox;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> AccountStatusText;

	/** B-08a: each card's copy of the error line (the footer's is under the dimmer). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> ModalErrorTexts;

	/** What the last list call returned, in the backend's order. */
	TArray<FValhallaCharacterSummary> Characters;

	/** Index into Characters, or INDEX_NONE. */
	int32 SelectedIndex = INDEX_NONE;

	/** B-08a: PreferCharacter's id, used once. */
	int32 PreferredCharacterId = INDEX_NONE;

	/** B-08a: the mask and floor glow over the preview, hidden with it. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UWidget>> PreviewDressing;

	/** The rows, so RefreshRows can recolour the selected one. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UValhallaCharacterRowButton>> RowButtons;
};

// ─────────────────────────────────────────────────────────────────────────────
//  The controller
// ─────────────────────────────────────────────────────────────────────────────

/**
 * The whole session flow, on the client, in one actor.
 *
 * ## The state machine
 *
 *   Login screen  -> Login/Register -> a token and a character list
 *   Select screen -> Enter World    -> ClientTravel to the game server
 *
 * The token lives here and nowhere else. It is never replicated, never written
 * to disk, and leaves this process exactly once: as the `token` option of the
 * travel URL, which is a client-to-server connection string and not a log line.
 *
 * ## Play-In-Editor
 *
 * `L_FrontEnd` is a *client* map. A PIE dedicated server that loads it (which
 * it does, because PIE hands the server whatever level the editor has open)
 * finds itself in `AValhallaFrontEndGameMode::InitGame` with no players and
 * immediately `ServerTravel`s to `ServerDefaultMap`. So one PIE launch gives a
 * server on `L_World` and clients on the front end, which is exactly the
 * shipping topology and needs no per-run editor fiddling beyond turning
 * "Auto Connect To Server" off.
 *
 * The address the client then travels to is `GameServerAddress` outside PIE
 * and `valhalla.FrontEnd.PieServerAddress` inside it — 127.0.0.1:17777,
 * because that is `ULevelEditorPlaySettings::ServerPort`'s default and a PIE
 * server does not listen on 7777.
 *
 * ## Scripting the gate
 *
 * `valhalla.AutoLogin` is a comma-separated list of
 * `user:pass:charname[:class]` entries dealt to front-end clients in the order
 * they start, exactly as `valhalla.ClassAssignment` deals classes — and for
 * the same reason, which is that every PIE client in one process shares one
 * set of cvars and there is otherwise no way to give two clients two
 * identities. An entry whose account does not exist is registered; a character
 * that does not exist is created with the named class. It is a developer tool
 * and the shipping client never reads it.
 */
UCLASS()
class VALHALLAGAME_API AValhallaFrontEndController : public APlayerController
{
	GENERATED_BODY()

public:
	AValhallaFrontEndController();

	//~ Begin AActor interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End AActor interface

	// ── Called by the widgets ───────────────────────────────────────────

	void SubmitLogin(const FString& Username, const FString& Password);
	void SubmitRegister(const FString& Username, const FString& Password);
	void SubmitCreateCharacter(const FString& Name, FName ClassId);
	void SubmitDeleteCharacter(int32 CharacterId);
	void EnterWorld(int32 CharacterId);

	/** B-12: change this account's password; the session keeps working on the new token. */
	void SubmitChangePassword(const FString& CurrentPassword, const FString& NewPassword);

	/** B-12: delete this account and every character on it, then return to the login screen. */
	void SubmitDeleteAccount(const FString& Password, const FString& Confirm);
	void LogOut();

	/** The session, or an empty one before a login. */
	const FValhallaAuthSession& GetSession() const { return Session; }

	/**
	 * B-08a: the character select screen's selection changed (null: none, or
	 * a panel covers the preview). Dresses the preview stage for it.
	 */
	void ShowPreviewFor(const FValhallaCharacterSummary* Summary);

	// ── Dev ─────────────────────────────────────────────────────────────

	/**
	 * Run the whole flow unattended: log in (registering if the account is
	 * new), pick the named character (creating it if it is not there), enter
	 * the world. Every step logs at Log.
	 */
	void AutoLogin(const FString& Username, const FString& Password, const FString& CharacterName, FName ClassId);

	/** Re-read `valhalla.AutoLogin` and run the entry for this client, if any. */
	void TryAutoLoginFromCVar();

protected:
	/** Swap the login screen for the character select screen. */
	void ShowCharacterSelect();

	/** Ask the backend for the character list again and redraw. */
	void RefreshCharacters();

	/** The address to travel to — see the class comment. */
	FString ResolveServerAddress() const;

	/** Both screens' error line goes through here so every failure is logged once. */
	void ReportError(const FString& Message);

	/**
	 * B-03: a client that reads its downloaded copy of the game data (a
	 * packaged build, or `valhalla.Data.ForceDownload`) brings it up to date
	 * from the backend when the front end opens, and reloads if anything
	 * changed. A failure is not fatal: the last copy is still loaded.
	 */
	void SyncGameDataIfNeeded();

private:
	UPROPERTY(Transient)
	TObjectPtr<UValhallaLoginWidget> LoginScreen;

	UPROPERTY(Transient)
	TObjectPtr<UValhallaCharacterSelectWidget> SelectScreen;

	/** B-08a: the lit stage the character select preview is captured from. */
	UPROPERTY(Transient)
	TObjectPtr<AValhallaCharacterPreviewStage> PreviewStage;

	/** Token, userId, username and the last character list. Never replicated. */
	FValhallaAuthSession Session;

	// ── Auto-login state ────────────────────────────────────────────────

	bool bAutoLoginActive = false;
	FString AutoCharacterName;
	FName AutoClassId;

	/**
	 * Which entry of `valhalla.AutoLogin` this client takes.
	 *
	 * Assigned in BeginPlay from a process-wide counter, because PIE clients
	 * are separate worlds in one process and their creation order is the only
	 * thing that distinguishes them. Same shape as the game mode's JoinCounter.
	 */
	int32 AutoLoginSlot = INDEX_NONE;

	/** How many front-end controllers this process has started. */
	static int32 FrontEndClientCounter;
};

// ─────────────────────────────────────────────────────────────────────────────
//  The game mode
// ─────────────────────────────────────────────────────────────────────────────

/**
 * `L_FrontEnd`'s game mode: no pawn, no game state, a UI controller, and an
 * immediate `ServerTravel` away if it finds itself on a server.
 *
 * See `AValhallaFrontEndController`'s class comment for why the travel is
 * here rather than in the PIE settings.
 */
UCLASS()
class VALHALLAGAME_API AValhallaFrontEndGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AValhallaFrontEndGameMode();

	//~ Begin AGameModeBase interface
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	//~ End AGameModeBase interface

	/** Where a dedicated server that loaded the front end goes instead. */
	UPROPERTY(EditDefaultsOnly, Category = "Valhalla|FrontEnd")
	FString ServerRedirectMap = TEXT("/Game/Valhalla/Maps/L_World");
};
