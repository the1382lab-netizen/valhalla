// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaGameMode.h"

#include "CoreGlobals.h"
#include "EngineUtils.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "ValhallaAdminServer.h"
#include "ValhallaBackendSubsystem.h"
#include "ValhallaDataSettings.h"
#include "ValhallaZoneSubsystem.h"
#include "ValhallaZoneTypes.h"
#include "ValhallaCharacter.h"
#include "ValhallaCombatLibrary.h"
#include "ValhallaConstants.h"
#include "ValhallaDataSubsystem.h"
#include "ValhallaGame.h"
#include "ValhallaGameState.h"
#include "ValhallaHUD.h"
#include "ValhallaInventoryLibrary.h"
#include "ValhallaLootBag.h"
#include "ValhallaNPC.h"
#include "ValhallaPartySubsystem.h"
#include "ValhallaPlayerController.h"
#include "ValhallaPlayerState.h"
#include "ValhallaSkillComponent.h"
#include "ValhallaTypes.h"

namespace
{
	/**
	 * Comma-separated class ids dealt out to joining players in turn, e.g.
	 * `warrior,wizard`. Empty means every joiner gets DefaultClassId.
	 *
	 * This exists because an in-process PIE listen server hands every client the
	 * same URL, so `?class=` cannot differ per client. Phase 7's character
	 * select replaces it; nothing in the game should read it.
	 */
	TAutoConsoleVariable<FString> CVarClassAssignment(
		TEXT("valhalla.ClassAssignment"),
		TEXT(""),
		TEXT("Dev only. Comma-separated class ids dealt to joining players in turn, e.g. 'warrior,wizard'. Empty uses the game mode's DefaultClassId."),
		ECVF_Default);
}

AValhallaGameMode::AValhallaGameMode()
{
	DefaultPawnClass = AValhallaCharacter::StaticClass();
	PlayerControllerClass = AValhallaPlayerController::StaticClass();
	PlayerStateClass = AValhallaPlayerState::StaticClass();
	GameStateClass = AValhallaGameState::StaticClass();
	HUDClass = AValhallaHUD::StaticClass();

	bStartPlayersAsSpectators = false;
	bUseSeamlessTravel = false;
}

bool AValhallaGameMode::RequiresRealServerSecret(ENetMode NetMode, bool bIsRunningDedicatedServer, bool bGIsEditor, bool bEditorBuild)
{
	if (bGIsEditor)
	{
		return false; // the editor and PIE (in-process servers included) keep the dev default
	}
	const bool bDedicated = bIsRunningDedicatedServer || NetMode == NM_DedicatedServer;
	const bool bPackagedListen = NetMode == NM_ListenServer && !bEditorBuild;
	return bDedicated || bPackagedListen;
}

FString AValhallaGameMode::RedactJoinOptions(const FString& Options)
{
	static const FString Key(TEXT("token="));
	FString Out;
	int32 Pos = 0;
	while (Pos <= Options.Len())
	{
		const int32 Found = Options.Find(Key, ESearchCase::IgnoreCase, ESearchDir::FromStart, Pos);
		if (Found == INDEX_NONE)
		{
			Out += Options.Mid(Pos);
			break;
		}
		const int32 ValueStart = Found + Key.Len();
		int32 ValueEnd = ValueStart;
		while (ValueEnd < Options.Len() && Options[ValueEnd] != TEXT('?') && Options[ValueEnd] != TEXT('&'))
		{
			++ValueEnd;
		}
		Out += Options.Mid(Pos, ValueStart - Pos);
		Out += UValhallaBackendSubsystem::RedactToken(Options.Mid(ValueStart, ValueEnd - ValueStart));
		Pos = ValueEnd;
		if (Pos >= Options.Len())
		{
			break;
		}
	}
	return Out;
}

void AValhallaGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	// ── B-04: no internet-facing server on the public dev secret ─────────
	//
	// `dev-server-secret` is in this repository, so a server using it lets
	// anybody call the backend's server-to-server routes as it and drive its
	// admin API. The editor and PIE keep it (nothing outside this machine can
	// reach them); a dedicated server, or a listen server in a packaged build,
	// stops here instead of running unprotected. See GetServerSecret for where
	// a real secret comes from.
	if (RequiresRealServerSecret(GetNetMode(), IsRunningDedicatedServer(), GIsEditor, WITH_EDITOR != 0))
	{
		const FString Secret = UValhallaDataSettings::Get()->GetServerSecret();
		if (Secret.IsEmpty() || UValhallaDataSettings::IsDevServerSecret(Secret))
		{
			bRefusedToHost = true;
			UE_LOG(LogValhallaGame, Error,
				TEXT("FATAL: this server resolved %s. A dedicated server outside the editor (or a packaged listen server) must not run on it: ")
				TEXT("put a real VALHALLA_SERVER_SECRET in secrets.local.env at the repo root, set the environment variable, or pass -ValhallaServerSecret=. Exiting."),
				Secret.IsEmpty() ? TEXT("no server secret") : TEXT("the public development secret 'dev-server-secret'"));
			if (GLog)
			{
				GLog->Flush();
			}
			FPlatformMisc::RequestExitWithStatus(false, 1, TEXT("AValhallaGameMode::InitGame (dev server secret)"));
			return;
		}
	}

	const UValhallaDataSubsystem* Data = GetDataSubsystem();
	if (!Data || !Data->IsLoaded())
	{
		// Not fatal: a designer's broken JSON must not stop the server booting.
		// Every join then falls back to the default class and logs loudly.
		UE_LOG(LogValhallaGame, Error, TEXT("Valhalla data is not loaded. Check ValhallaDataSettings.DataRoot in DefaultGame.ini."));
		return;
	}

	UE_LOG(LogValhallaGame, Log, TEXT("GameMode init on '%s': %d classes, %d skills, %d items, %d zones from %s"),
		*MapName, Data->GetClassCount(), Data->GetSkillCount(), Data->GetItemCount(), Data->GetZoneCount(),
		*Data->GetLoadedDataRoot());

	// `zones.json` is what validates DefaultZoneId — see the property's
	// comment. A default zone that is not in the file is not fatal (the zone
	// volumes decide where players actually are) but it does mean the id a
	// player's `general` chat is keyed on matches nothing in the data.
	if (!Data->FindZone(DefaultZoneId))
	{
		UE_LOG(LogValhallaGame, Warning,
			TEXT("DefaultZoneId '%s' is not in zones.json; logins will still work but the id names nothing."),
			*DefaultZoneId.ToString());
	}

	// ── Phase 6b: the editor's live dashboard ────────────────────────────
	//
	// Started here rather than from the subsystem's own Initialize because a
	// world subsystem is created for *every* world, including a listen server's
	// client-side one, and exactly one of them may own the port. `InitGame` runs
	// only where there is a game mode, which is only on the authority — so the
	// call site is the gate, and the subsystem's own authority check is the
	// belt to that pair of braces.
	if (UWorld* World = GetWorld())
	{
		if (UValhallaAdminServer* Admin = World->GetSubsystem<UValhallaAdminServer>())
		{
			Admin->Start();
		}
	}
}

void AValhallaGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Symmetric with InitGame. Without it a PIE session that ends leaves the
	// routes bound to a dead subsystem and the next Play cannot rebind the port
	// — which looks exactly like "the editor has to be restarted to use the
	// dashboard", and was.
	if (UWorld* World = GetWorld())
	{
		if (UValhallaAdminServer* Admin = World->GetSubsystem<UValhallaAdminServer>())
		{
			Admin->Stop();
		}
	}

	Super::EndPlay(EndPlayReason);
}

bool AValhallaGameMode::ReloadGameData(FValhallaDataReloadCounts& OutCounts)
{
	OutCounts = FValhallaDataReloadCounts();

	UWorld* World = GetWorld();
	if (!World || !HasAuthority())
	{
		return false;
	}

	UValhallaDataSubsystem* Data = GetDataSubsystem();
	if (!Data)
	{
		UE_LOG(LogValhallaGame, Error, TEXT("reload-data: no data subsystem in this game instance."));
		return false;
	}

	if (!Data->Reload())
	{
		// UValhallaDataSubsystem has already logged which file failed. Nothing
		// is pushed: half-reloaded data is worse than stale data, because the
		// stale data at least agrees with itself.
		UE_LOG(LogValhallaGame, Error, TEXT("reload-data: at least one file failed to load; nothing was re-resolved."));
		return false;
	}

	OutCounts.Classes      = Data->GetClassCount();
	OutCounts.Items        = Data->GetItemCount();
	OutCounts.Skills       = Data->GetSkillCount();
	OutCounts.NpcTemplates = Data->GetNPCTemplateCount();
	OutCounts.LootTables   = Data->GetLootTableCount();
	OutCounts.Zones        = Data->GetZoneCount();

	// ── Players: Phase 2c's recompute, unchanged ─────────────────────────
	if (const AGameStateBase* GS = GameState)
	{
		for (APlayerState* Entry : GS->PlayerArray)
		{
			AValhallaPlayerState* ValhallaPS = Cast<AValhallaPlayerState>(Entry);
			if (!ValhallaPS)
			{
				continue;
			}

			ValhallaPS->RecomputeStats();
			++OutCounts.PlayersRecomputed;

			UE_LOG(LogValhallaGame, Verbose, TEXT("reload-data: re-resolved %s"), *ValhallaPS->DescribeForLog());
		}
	}

	// ── NPCs: re-read the template they were built from ──────────────────
	for (TActorIterator<AValhallaNPC> It(World); It; ++It)
	{
		AValhallaNPC* Npc = *It;
		const FValhallaNPCTemplate* Template = Data->FindNPCTemplate(Npc->TemplateId);
		if (!Template)
		{
			// Left exactly as it is, still fighting with the numbers it had.
			// Destroying it would turn a typo in the editor into a wave of
			// enemies vanishing mid-pull.
			++OutCounts.NpcsOrphaned;
			UE_LOG(LogValhallaGame, Warning,
				TEXT("reload-data: NPC '%s' has template '%s', which the reloaded npc-templates.json no longer has; left on its old stats."),
				*Npc->GetName(), *Npc->TemplateId.ToString());
			continue;
		}

		Npc->ReapplyTemplate(*Template);
		++OutCounts.NpcsUpdated;
	}

	// ── Clients: tell them to reload their own copy ──────────────────────
	if (AValhallaGameState* ValhallaGS = GetGameState<AValhallaGameState>())
	{
		ValhallaGS->BumpDataVersion();
	}

	UE_LOG(LogValhallaGame, Log,
		TEXT("reload-data: %d classes, %d items, %d skills, %d npc templates, %d loot tables, %d zones; %d players re-resolved, %d NPCs updated, %d orphaned."),
		OutCounts.Classes, OutCounts.Items, OutCounts.Skills, OutCounts.NpcTemplates,
		OutCounts.LootTables, OutCounts.Zones,
		OutCounts.PlayersRecomputed, OutCounts.NpcsUpdated, OutCounts.NpcsOrphaned);

	return true;
}

AActor* AValhallaGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	// Which zone this controller belongs in. A joiner has DefaultZoneId on its
	// player state already (InitializeJoiningPlayer ran in PostLogin, before
	// Super spawned the pawn); a respawner has whatever zone they died in, and
	// that is the right answer — dying in the desert should not post you back
	// to the grasslands, which is the one thing 1.0's respawn got wrong by
	// virtue of only ever having one zone loaded.
	FName WantedZone = DefaultZoneId;
	if (const AValhallaPlayerState* ValhallaPS = Player ? Cast<AValhallaPlayerState>(Player->PlayerState) : nullptr)
	{
		if (!ValhallaPS->ZoneId.IsNone())
		{
			WantedZone = ValhallaPS->ZoneId;
		}
	}

	// `PlayerStartTag` is the zone id. The level builders set it on all four
	// starts they place, which is what makes one property enough to keep two
	// zones' spawn points apart in one world.
	TArray<APlayerStart*> Candidates;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		if (It->PlayerStartTag == WantedZone)
		{
			Candidates.Add(*It);
		}
	}

	if (Candidates.Num() == 0)
	{
		// No tagged starts: L_GreyBox and L_LoSTest, which have untagged ones,
		// and any level where somebody forgot. The engine's own choice is a
		// perfectly good answer and is what Phase 2a through 5 used.
		UE_LOG(LogValhallaGame, Verbose,
			TEXT("no PlayerStart tagged '%s'; falling back to the engine's choice."), *WantedZone.ToString());
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	// Prefer one nobody is standing on. Two characters spawned inside one
	// another is the distraction the four starts exist to avoid.
	constexpr double OccupiedRadiusCm = 120.0;
	TArray<APlayerStart*> Free;
	for (APlayerStart* Start : Candidates)
	{
		bool bOccupied = false;
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			if (FVector::Dist2D(It->GetActorLocation(), Start->GetActorLocation()) < OccupiedRadiusCm)
			{
				bOccupied = true;
				break;
			}
		}
		if (!bOccupied)
		{
			Free.Add(Start);
		}
	}

	const TArray<APlayerStart*>& Pool = Free.Num() > 0 ? Free : Candidates;
	APlayerStart* Chosen = Pool[FMath::RandHelper(Pool.Num())];

	UE_LOG(LogValhallaGame, Log, TEXT("%s spawns in zone '%s' at %s (%.0f, %.0f)"),
		Player && Player->PlayerState ? *Player->PlayerState->GetPlayerName() : TEXT("?"),
		*WantedZone.ToString(), *Chosen->GetName(),
		Chosen->GetActorLocation().X, Chosen->GetActorLocation().Y);

	return Chosen;
}

UValhallaDataSubsystem* AValhallaGameMode::GetDataSubsystem() const
{
	UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
}

FString AValhallaGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal)
{
	// The per-connection options are only handed to this one call. Stash them
	// so PostLogin, which runs after the player state exists, can read them.
	if (NewPlayerController)
	{
		PendingJoinOptions.Add(NewPlayerController, Options);
	}

	return Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
}

FString AValhallaGameMode::ParseTokenOption(const FString& JoinOptions)
{
	// UE URL options are '?'-separated; FURL::Parse does not know '&'. The
	// front end emits '?', so the common case is already clean. The contract
	// this implements was written with '&', though, and a token that arrived
	// with "&characterId=3" glued to the end of it would fail `verify` with a
	// message that pointed at the backend rather than at the URL — so it is
	// trimmed here, once, where the evidence still exists.
	FString Token = UGameplayStatics::ParseOption(JoinOptions, TEXT("token"));

	int32 Ampersand = INDEX_NONE;
	if (Token.FindChar(TEXT('&'), Ampersand))
	{
		Token.LeftInline(Ampersand);
	}

	return Token;
}

void AValhallaGameMode::PreLoginAsync(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, const FOnPreLoginCompleteDelegate& OnComplete)
{
	// The engine's own checks first — bans, the GameModePreLoginEvent
	// broadcast, the session's ApproveLogin. AGameModeBase::PreLoginAsync's
	// contract says an override must still run them, and it is right: a banned
	// account with a perfectly good JWT is still banned.
	if (bRefusedToHost)
	{
		OnComplete.ExecuteIfBound(TEXT("This server is shutting down (no server secret configured)."));
		return;
	}

	FString ErrorMessage;
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (!ErrorMessage.IsEmpty())
	{
		OnComplete.ExecuteIfBound(ErrorMessage);
		return;
	}

	const FString Token = ParseTokenOption(Options);
	if (Token.IsEmpty())
	{
		// A dev join: `?class=wizard?charname=Gandalf`, or PIE with no front
		// end. Allowed, because the whole of Phases 2 through 6 is tested that
		// way and a server that could only be reached through a running
		// backend would be a server that could not be debugged.
		UE_LOG(LogValhallaGame, Log, TEXT("PreLogin from %s: no token; joining as a dev session."), *Address);
		OnComplete.ExecuteIfBound(FString());
		return;
	}

	UValhallaBackendSubsystem* Backend = GetGameInstance() ? GetGameInstance()->GetSubsystem<UValhallaBackendSubsystem>() : nullptr;
	if (!Backend)
	{
		OnComplete.ExecuteIfBound(TEXT("The server cannot reach its account backend."));
		return;
	}

	// Sweep first. An entry only lives until the PostLogin that consumes it,
	// and a connection that never got there must not leave one behind.
	const double Now = FPlatformTime::Seconds();
	for (auto It = VerifiedTokens.CreateIterator(); It; ++It)
	{
		if (Now - It.Value().Value > VerifiedTokenLifetimeSeconds)
		{
			It.RemoveCurrent();
		}
	}

	TWeakObjectPtr<AValhallaGameMode> WeakThis(this);
	const FString Redacted = UValhallaBackendSubsystem::RedactToken(Token);

	Backend->Verify(Token,
		[WeakThis, OnComplete, Token, Redacted, Address](bool bOk, const FValhallaVerifiedToken& Verified, const FString& Error)
		{
			AValhallaGameMode* Self = WeakThis.Get();
			if (!Self)
			{
				// The world went away mid-verify. Completing with an error is
				// the only safe answer: not completing hangs the connection
				// until it times out, which the delegate's own comment warns
				// about.
				OnComplete.ExecuteIfBound(TEXT("The server is shutting down."));
				return;
			}

			if (!bOk)
			{
				UE_LOG(LogValhallaGame, Warning, TEXT("PreLogin rejected %s (token %s): %s"),
					*Address, *Redacted, *Error);
				OnComplete.ExecuteIfBound(FString::Printf(TEXT("Login rejected: %s"), *Error));
				return;
			}

			UE_LOG(LogValhallaGame, Log, TEXT("PreLogin accepted %s: token %s is userId %d ('%s')"),
				*Address, *Redacted, Verified.UserId, *Verified.Username);

			Self->VerifiedTokens.Add(Token, TPair<FValhallaVerifiedToken, double>(Verified, FPlatformTime::Seconds()));
			OnComplete.ExecuteIfBound(FString());
		});
}

void AValhallaGameMode::PostLogin(APlayerController* NewPlayer)
{
	// ── Is this a backend join, and whose? ───────────────────────────────
	const FString* OptionsPtr = NewPlayer ? PendingJoinOptions.Find(NewPlayer) : nullptr;
	const FString JoinOptions = OptionsPtr ? *OptionsPtr : FString();

	const FString Token = ParseTokenOption(JoinOptions);
	const int32 CharacterId = UGameplayStatics::GetIntOption(JoinOptions, TEXT("characterId"), 0);

	if (NewPlayer && !Token.IsEmpty() && CharacterId > 0)
	{
		if (const TPair<FValhallaVerifiedToken, double>* Entry = VerifiedTokens.Find(Token))
		{
			FValhallaBackendSession Session;
			Session.CharacterId  = CharacterId;
			Session.UserId       = Entry->Key.UserId;
			Session.Username     = Entry->Key.Username;
			Session.bLoadPending = true;
			BackendSessions.Add(NewPlayer, Session);

			// Spent. The token is not kept past this point — see
			// FValhallaBackendSession's comment.
			VerifiedTokens.Remove(Token);
		}
		else
		{
			// PreLoginAsync either did not run (a listen server's own host
			// player never goes through it) or its entry expired. Either way
			// the server has no verified user id and must not guess one.
			UE_LOG(LogValhallaGame, Warning,
				TEXT("PostLogin for character %d presented a token with no verified session; joining as a dev session."),
				CharacterId);
		}
	}

	// Deliberately before Super: Super::PostLogin spawns the pawn, and
	// AValhallaCharacter::PossessedBy reads the class off the player state to
	// set MaxWalkSpeed. Resolving the class first means the character is never
	// briefly alive at the wrong speed with the wrong colour.
	InitializeJoiningPlayer(NewPlayer);

	// Super::PostLogin ends in HandleStartingNewPlayer, which spawns the pawn.
	// When a load is pending that override does nothing and the load's
	// continuation spawns instead — see BeginCharacterLoad.
	Super::PostLogin(NewPlayer);

	const AValhallaPlayerState* ValhallaPS = NewPlayer ? NewPlayer->GetPlayerState<AValhallaPlayerState>() : nullptr;
	if (!ValhallaPS)
	{
		return;
	}

	if (const FValhallaBackendSession* Session = BackendSessions.Find(NewPlayer))
	{
		if (Session->bLoadPending)
		{
			BeginCharacterLoad(NewPlayer, Session->CharacterId, Session->UserId);
			PendingJoinOptions.Remove(NewPlayer);
			return;
		}
	}

	// ── The dev path, unchanged since Phase 2a ───────────────────────────
	const APawn* SpawnedPawn = NewPlayer->GetPawn();
	UE_LOG(LogValhallaGame, Log, TEXT("Login: %s"), *ValhallaPS->DescribeForLog());

	if (SpawnedPawn)
	{
		UE_LOG(LogValhallaGame, Log, TEXT("Spawned %s for %s at %s"),
			*SpawnedPawn->GetName(), *ValhallaPS->CharacterName, *SpawnedPawn->GetActorLocation().ToCompactString());
	}
	else
	{
		UE_LOG(LogValhallaGame, Error, TEXT("No pawn spawned for %s — is there a PlayerStart in the level?"),
			*ValhallaPS->CharacterName);
	}

	PendingJoinOptions.Remove(NewPlayer);
}

void AValhallaGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// The one line Phase 7 adds to the spawn path: a character whose row has
	// not arrived yet does not get a pawn, because there is nowhere to put it.
	if (const FValhallaBackendSession* Session = BackendSessions.Find(NewPlayer))
	{
		if (Session->bLoadPending)
		{
			UE_LOG(LogValhallaGame, Verbose,
				TEXT("deferring the pawn for character %d until its row arrives."), Session->CharacterId);
			return;
		}
	}

	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}

void AValhallaGameMode::InitializeJoiningPlayer(APlayerController* NewPlayer)
{
	if (!NewPlayer)
	{
		return;
	}

	AValhallaPlayerState* ValhallaPS = NewPlayer->GetPlayerState<AValhallaPlayerState>();
	if (!ValhallaPS)
	{
		UE_LOG(LogValhallaGame, Error, TEXT("Joining controller %s has no AValhallaPlayerState."), *NewPlayer->GetName());
		return;
	}

	const FString* Options = PendingJoinOptions.Find(NewPlayer);
	const FString JoinOptions = Options ? *Options : FString();

	++JoinCounter;

	// ── name ────────────────────────────────────────────────────────────
	// `charname`, not `name`: see the header. The engine owns `name`.
	FString CharacterName = UGameplayStatics::ParseOption(JoinOptions, TEXT("charname"));
	if (CharacterName.IsEmpty())
	{
		CharacterName = FString::Printf(TEXT("Player%d"), JoinCounter);
	}

	// ── class ───────────────────────────────────────────────────────────
	const FString RequestedClass = ResolveRequestedClass(JoinOptions);

	const UValhallaDataSubsystem* Data = GetDataSubsystem();
	if (!Data)
	{
		UE_LOG(LogValhallaGame, Error, TEXT("No data subsystem; %s joins with an empty stat block."), *CharacterName);
		return;
	}

	FName ResolvedClassId(*RequestedClass);
	const FValhallaClassTemplate* ClassTemplate = Data->FindClass(ResolvedClassId);

	if (!ClassTemplate)
	{
		// A client asking for a class that is not in classes.json is either a
		// typo or an attack; either way it gets the default, never a blank one.
		UE_LOG(LogValhallaGame, Warning, TEXT("%s requested unknown class '%s'; falling back to '%s'."),
			*CharacterName, *RequestedClass, *DefaultClassId.ToString());

		ResolvedClassId = DefaultClassId;
		ClassTemplate = Data->FindClass(ResolvedClassId);
	}

	if (!ClassTemplate)
	{
		UE_LOG(LogValhallaGame, Error, TEXT("Default class '%s' is missing from classes.json; %s cannot be initialised."),
			*DefaultClassId.ToString(), *CharacterName);
		return;
	}

	ValhallaPS->InitializeFromClass(*ClassTemplate, /*Level=*/1, CharacterName, DefaultZoneId);

	// CharacterService.ts:169 — the class's kit, auto-equipping what it says to
	// auto-equip, then a stat recompute over the top.
	//
	// In 1.0 this ran once, at character *creation*, and every login afterwards
	// loaded the saved rows. Phase 2c had no persistence, so every login was a
	// first login and the kit was granted every time. **This is the call Phase 7
	// replaces with a load.** A joiner that presented a verified token is about
	// to have its saved row applied over the top of this provisional state, and
	// granting here would hand it a second sword every time it logged in.
	//
	// The grant survives for the dev path — `?class=wizard` with no token —
	// which is the only way the levels are played without a backend, and for a
	// loaded row that has never been played: see
	// UValhallaBackendSubsystem::ResolveLoadedCharacter's bNeedsStartingItems.
	const FValhallaBackendSession* Session = BackendSessions.Find(NewPlayer);
	const bool bLoadPending = Session && Session->bLoadPending;

	if (!bLoadPending)
	{
		ValhallaPS->GrantStartingItems(*ClassTemplate);
	}

	UE_LOG(LogValhallaGame, Log, TEXT("Join request: name='%s' class='%s'%s (options '%s')"),
		*CharacterName, *ResolvedClassId.ToString(),
		bLoadPending ? TEXT(" [provisional; a saved row is on its way]") : TEXT(""),
		*RedactJoinOptions(JoinOptions));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Phase 7: load
// ─────────────────────────────────────────────────────────────────────────────

void AValhallaGameMode::BeginCharacterLoad(APlayerController* NewPlayer, int32 CharacterId, int32 UserId)
{
	UValhallaBackendSubsystem* Backend = GetGameInstance() ? GetGameInstance()->GetSubsystem<UValhallaBackendSubsystem>() : nullptr;
	if (!Backend)
	{
		UE_LOG(LogValhallaGame, Error, TEXT("no backend subsystem; character %d cannot be loaded."), CharacterId);
		if (FValhallaBackendSession* Session = BackendSessions.Find(NewPlayer))
		{
			Session->bLoadPending = false;
		}
		HandleStartingNewPlayer(NewPlayer);
		return;
	}

	UE_LOG(LogValhallaGame, Log, TEXT("loading character %d for userId %d…"), CharacterId, UserId);

	TWeakObjectPtr<AValhallaGameMode> WeakThis(this);
	TWeakObjectPtr<APlayerController> WeakPlayer(NewPlayer);

	Backend->LoadCharacter(CharacterId, UserId,
		[WeakThis, WeakPlayer, CharacterId](bool bSuccess, const FValhallaLoadedCharacter& Loaded, const FString& Error)
		{
			AValhallaGameMode* Self = WeakThis.Get();
			APlayerController* Player = WeakPlayer.Get();
			if (!Self || !Player)
			{
				return;
			}

			if (!bSuccess)
			{
				// Refusing the connection is the only correct answer. Letting
				// them in on the provisional level-1 state would mean the next
				// autosave overwrote a real character with a blank one.
				UE_LOG(LogValhallaGame, Error, TEXT("character %d failed to load: %s"), CharacterId, *Error);

				Self->BackendSessions.Remove(Player);
				if (AGameSession* GameSession = Self->GameSession)
				{
					GameSession->KickPlayer(Player, FText::FromString(
						FString::Printf(TEXT("Your character could not be loaded: %s"), *Error)));
				}
				return;
			}

			Self->ApplyLoadedCharacter(Player, Loaded);
		});
}

void AValhallaGameMode::ApplyLoadedCharacter(APlayerController* NewPlayer, const FValhallaLoadedCharacter& Loaded)
{
	AValhallaPlayerState* ValhallaPS = NewPlayer ? NewPlayer->GetPlayerState<AValhallaPlayerState>() : nullptr;
	FValhallaBackendSession* Session = NewPlayer ? BackendSessions.Find(NewPlayer) : nullptr;
	if (!ValhallaPS || !Session)
	{
		return;
	}

	const UValhallaDataSubsystem* Data = GetDataSubsystem();
	const FValhallaClassTemplate* ClassTemplate = Data ? Data->FindClass(Loaded.ClassId) : nullptr;
	if (!ClassTemplate)
	{
		// The saved class is not in classes.json. Falling back to the default
		// would silently turn somebody's rogue into a warrior and then save
		// that, so it is a refusal instead.
		UE_LOG(LogValhallaGame, Error,
			TEXT("character %d has class '%s', which classes.json does not have; refusing the join."),
			Loaded.Id, *Loaded.ClassId.ToString());

		BackendSessions.Remove(NewPlayer);
		if (AGameSession* Sess = GameSession)
		{
			Sess->KickPlayer(NewPlayer, FText::FromString(
				FString::Printf(TEXT("Unknown class '%s' — the server's data is out of step with your character."),
					*Loaded.ClassId.ToString())));
		}
		return;
	}

	auto FindItem = [Data](FName ItemId) -> const FValhallaItemTemplate* { return Data->FindItem(ItemId); };

	FValhallaAppliedCharacter Applied;
	UValhallaBackendSubsystem::ResolveLoadedCharacter(Loaded, *ClassTemplate, FindItem, Applied);

	// InitializeFromClass, then the saved values over the top of it: the same
	// order a fresh join uses, so there is exactly one path that fills the
	// vitals and it is the one the class template drives.
	ValhallaPS->InitializeFromClass(*ClassTemplate, Applied.Level, Applied.CharacterName,
		Applied.ZoneId.IsNone() ? DefaultZoneId : Applied.ZoneId);

	ValhallaPS->Xp = Applied.Xp;
	ValhallaPS->Inventory = Applied.Inventory;
	ValhallaPS->ApplyEquipment(Applied.Equipment);

	// Equipment is on, so the stat block and the pool ceilings are the loaded
	// character's. Only then are the saved hp/mana clamped into them.
	ValhallaPS->RecomputeStats();
	ValhallaPS->Hp   = FMath::Clamp(Loaded.Hp, 0.f, ValhallaPS->MaxHp);
	ValhallaPS->Mana = FMath::Clamp(Loaded.Mana, 0.f, ValhallaPS->MaxMana);

	if (Applied.bNeedsStartingItems)
	{
		// A row that predates CharacterService.createCharacter's own grant.
		// See ResolveLoadedCharacter for why this is not the common case and
		// must not become one.
		UE_LOG(LogValhallaGame, Log,
			TEXT("character %d arrived with no inventory, no equipment and level 1; granting '%s' starting items."),
			Loaded.Id, *ClassTemplate->Id.ToString());
		ValhallaPS->GrantStartingItems(*ClassTemplate);
	}

	Session->CharacterId    = Loaded.Id;
	Session->LastZoneId     = ValhallaPS->ZoneId;
	Session->LastZoneLocalCm = FVector2D(Loaded.PositionX, Loaded.PositionY);
	Session->ActionBar      = Loaded.ActionBar;
	Session->bLoadPending   = false;

	UE_LOG(LogValhallaGame, Log, TEXT("Login: %s  xp=%d zone=%s equip=[%s]"),
		*ValhallaPS->DescribeForLog(), ValhallaPS->Xp, *ValhallaPS->ZoneId.ToString(),
		*ValhallaPS->DescribeEquipment().TrimEnd());

	SpawnLoadedPawn(NewPlayer, Applied);
	StartAutosave(NewPlayer);
}

void AValhallaGameMode::SpawnLoadedPawn(APlayerController* NewPlayer, const FValhallaAppliedCharacter& /*Applied*/)
{
	AValhallaPlayerState* ValhallaPS = NewPlayer ? NewPlayer->GetPlayerState<AValhallaPlayerState>() : nullptr;
	const FValhallaBackendSession* Session = NewPlayer ? BackendSessions.Find(NewPlayer) : nullptr;
	if (!ValhallaPS || !Session)
	{
		return;
	}

	UWorld* World = GetWorld();
	const UValhallaZoneSubsystem* Zones = World ? World->GetSubsystem<UValhallaZoneSubsystem>() : nullptr;
	const FValhallaZoneDef* Zone = Zones ? Zones->FindZone(ValhallaPS->ZoneId) : nullptr;

	// Zone-local cm -> world, through the one conversion the admin API uses and
	// for the same reason: L_Desert's +40000 cm streaming offset lives in the
	// zone's bounds and nowhere else.
	bool bHavePosition = false;
	FVector Location = FVector::ZeroVector;

	if (Zone)
	{
		const FVector2D Local = Session->LastZoneLocalCm;

		// A row saved before this phase has (0, 0) in both columns, and the
		// corner of a zone is not a place anybody chose to stand. Treating it
		// as "no saved position" costs a first login its two metres and saves
		// every pre-Phase-7 character from waking up in a wall.
		const bool bLooksSaved = !Local.IsNearlyZero();
		if (bLooksSaved)
		{
			FVector Candidate = UValhallaAdminServer::FromZoneLocalCm(
				*Zone, Local.X, Local.Y, UValhallaAdminServer::PlacementZOffsetCm);

			// Phase 8c: PlacementZOffsetCm puts the capsule *centre* 8 cm above
			// the floor, i.e. half the capsule inside it, and SpawnActor's
			// AdjustIfPossibleButDontSpawnIfColliding then refused most saved
			// positions ("SpawnActor failed because of collision"). Stand the
			// capsule on the floor instead.
			if (const ACharacter* PawnDefaults = Cast<ACharacter>(GetDefaultPawnClassForController(NewPlayer)
					? GetDefaultPawnClassForController(NewPlayer)->GetDefaultObject() : nullptr))
			{
				if (const UCapsuleComponent* Capsule = PawnDefaults->GetCapsuleComponent())
				{
					Candidate.Z += Capsule->GetScaledCapsuleHalfHeight();

					// B-06: a saved position is 2D and a Landscape zone is not
					// flat. Stand the capsule on the ground actually under the
					// saved XY (the lowest walkable floor with room for it),
					// searching the zone's whole height.
					double StandZ = 0.0;
					if (UValhallaZoneSubsystem::FindStandingZ(GetWorld(), Candidate.X, Candidate.Y,
							Zone->Bounds.Max.Z + 500.0, Zone->Bounds.Min.Z - 1500.0,
							Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight(), nullptr, StandZ))
					{
						Candidate.Z = StandZ + 2.0;
					}
				}
			}

			if (Zone->Contains2D(Candidate))
			{
				Location = Candidate;
				bHavePosition = true;
			}
			else
			{
				UE_LOG(LogValhallaGame, Warning,
					TEXT("character %d's saved position (%.0f, %.0f) is outside zone '%s'; using the default spawn."),
					Session->CharacterId, Local.X, Local.Y, *ValhallaPS->ZoneId.ToString());
			}
		}
	}
	else if (!ValhallaPS->ZoneId.IsNone())
	{
		UE_LOG(LogValhallaGame, Warning,
			TEXT("character %d's zone '%s' is not loaded in this world; using the default spawn."),
			Session->CharacterId, *ValhallaPS->ZoneId.ToString());
	}

	if (bHavePosition)
	{
		// Straight to the saved spot. ChoosePlayerStart is not consulted: the
		// saved position *is* the answer, and a PlayerStart would override it.
		RestartPlayerAtTransform(NewPlayer, FTransform(FRotator::ZeroRotator, Location));

		// Phase 8c: a saved spot that is still blocked (a prop placed since,
		// another capsule) must not leave the player bodiless; fall back to the
		// zone's default spawn.
		if (!NewPlayer->GetPawn())
		{
			UE_LOG(LogValhallaGame, Warning,
				TEXT("character %d's saved position is blocked; using the default spawn."), Session->CharacterId);
			bHavePosition = false;
			RestartPlayer(NewPlayer);
		}
	}
	else
	{
		// No usable position: the ordinary path, which already picks a start
		// tagged with the player state's zone.
		RestartPlayer(NewPlayer);
	}

	const APawn* SpawnedPawn = NewPlayer->GetPawn();
	if (SpawnedPawn)
	{
		UE_LOG(LogValhallaGame, Log, TEXT("Spawned %s for %s at %s (%s)"),
			*SpawnedPawn->GetName(), *ValhallaPS->CharacterName,
			*SpawnedPawn->GetActorLocation().ToCompactString(),
			bHavePosition ? TEXT("loaded position") : TEXT("default spawn"));

		NotePlayerPosition(ValhallaPS);
	}
	else
	{
		UE_LOG(LogValhallaGame, Error, TEXT("No pawn spawned for %s after its load completed."),
			*ValhallaPS->CharacterName);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Phase 7: save
// ─────────────────────────────────────────────────────────────────────────────

void AValhallaGameMode::StartAutosave(APlayerController* NewPlayer)
{
	UWorld* World = GetWorld();
	FValhallaBackendSession* Session = NewPlayer ? BackendSessions.Find(NewPlayer) : nullptr;
	if (!World || !Session)
	{
		return;
	}

	const float IntervalSeconds = static_cast<float>(Valhalla::SaveIntervalMs / 1000.0);

	TWeakObjectPtr<AValhallaGameMode> WeakThis(this);
	TWeakObjectPtr<APlayerController> WeakPlayer(NewPlayer);

	World->GetTimerManager().SetTimer(Session->AutosaveTimer,
		FTimerDelegate::CreateLambda([WeakThis, WeakPlayer]()
		{
			AValhallaGameMode* Self = WeakThis.Get();
			APlayerController* Player = WeakPlayer.Get();
			if (!Self || !Player)
			{
				return;
			}

			Self->SaveCharacterFor(Player->GetPlayerState<AValhallaPlayerState>(), TEXT("autosave"));
		}),
		IntervalSeconds, /*bLoop=*/true, /*FirstDelay=*/IntervalSeconds);

	UE_LOG(LogValhallaGame, Verbose, TEXT("autosave every %.0fs for character %d."),
		IntervalSeconds, Session->CharacterId);
}

const FValhallaBackendSession* AValhallaGameMode::FindBackendSession(const APlayerController* Controller) const
{
	return BackendSessions.Find(Controller);
}

void AValhallaGameMode::NotePlayerPosition(AValhallaPlayerState* ValhallaPS)
{
	if (!ValhallaPS || !HasAuthority())
	{
		return;
	}

	APlayerController* Controller = Cast<APlayerController>(ValhallaPS->GetOwner());
	FValhallaBackendSession* Session = Controller ? BackendSessions.Find(Controller) : nullptr;
	const APawn* Pawn = ValhallaPS->GetPawn();
	if (!Session || !Pawn)
	{
		return;
	}

	UWorld* World = GetWorld();
	const UValhallaZoneSubsystem* Zones = World ? World->GetSubsystem<UValhallaZoneSubsystem>() : nullptr;
	const FValhallaZoneDef* Zone = Zones ? Zones->FindZone(ValhallaPS->ZoneId) : nullptr;
	if (!Zone)
	{
		return;
	}

	Session->LastZoneId = ValhallaPS->ZoneId;
	Session->LastZoneLocalCm = UValhallaAdminServer::ToZoneLocalCm(*Zone, Pawn->GetActorLocation());
}

void AValhallaGameMode::SaveCharacterFor(AValhallaPlayerState* ValhallaPS, const TCHAR* Reason)
{
	if (!ValhallaPS || !HasAuthority())
	{
		return;
	}

	APlayerController* Controller = Cast<APlayerController>(ValhallaPS->GetOwner());
	FValhallaBackendSession* Session = Controller ? BackendSessions.Find(Controller) : nullptr;
	if (!Session || Session->CharacterId <= 0 || Session->bLoadPending)
	{
		// A dev join, or a row that has not arrived yet. Not an error: saving a
		// character the server invented from a URL option would write over a
		// real one if the ids ever collided.
		return;
	}

	UValhallaBackendSubsystem* Backend = GetGameInstance() ? GetGameInstance()->GetSubsystem<UValhallaBackendSubsystem>() : nullptr;
	if (!Backend)
	{
		return;
	}

	// Refresh the remembered position if there is a pawn to read it off, then
	// save from what is remembered either way.
	NotePlayerPosition(ValhallaPS);

	const FValhallaSaveData Data = UValhallaBackendSubsystem::BuildSaveData(
		*ValhallaPS, Session->LastZoneLocalCm, Session->ActionBar);

	const int32 CharacterId = Session->CharacterId;

	UE_LOG(LogValhallaGame, Verbose, TEXT("saving char=%d (%s): lv%d xp=%d hp=%.0f zone=%s pos=(%.0f, %.0f)"),
		CharacterId, Reason, Data.Level, Data.Xp, Data.Hp, *Data.ZoneId.ToString(), Data.PositionX, Data.PositionY);

	Backend->SaveCharacter(CharacterId, Data,
		[CharacterId, ReasonText = FString(Reason)](bool bSuccess, const FString& Error)
		{
			if (bSuccess)
			{
				UE_LOG(LogValhallaGame, Log, TEXT("saved char=%d (%s)"), CharacterId, *ReasonText);
			}
			else
			{
				UE_LOG(LogValhallaGame, Warning, TEXT("save char=%d (%s) failed: %s"),
					CharacterId, *ReasonText, *Error);
			}
		});
}

int32 AValhallaGameMode::SaveAllCharacters(const TCHAR* Reason)
{
	int32 Saved = 0;

	if (const AGameStateBase* GS = GameState)
	{
		for (APlayerState* Entry : GS->PlayerArray)
		{
			if (AValhallaPlayerState* ValhallaPS = Cast<AValhallaPlayerState>(Entry))
			{
				APlayerController* Controller = Cast<APlayerController>(ValhallaPS->GetOwner());
				if (Controller && BackendSessions.Contains(Controller))
				{
					SaveCharacterFor(ValhallaPS, Reason);
					++Saved;
				}
			}
		}
	}

	return Saved;
}

namespace
{
	/** `valhalla.SaveNow` — flush every logged-in character, for the gate. */
	FAutoConsoleCommandWithWorld GSaveNowCommand(
		TEXT("valhalla.SaveNow"),
		TEXT("Dev only. Saves every logged-in character to the backend immediately."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			AValhallaGameMode* GameMode = World ? World->GetAuthGameMode<AValhallaGameMode>() : nullptr;
			if (!GameMode)
			{
				UE_LOG(LogValhallaGame, Warning, TEXT("valhalla.SaveNow: this world has no authoritative game mode."));
				return;
			}

			UE_LOG(LogValhallaGame, Log, TEXT("valhalla.SaveNow: %d character(s)."),
				GameMode->SaveAllCharacters(TEXT("manual")));
		}));
}

FString AValhallaGameMode::ResolveRequestedClass(const FString& JoinOptions) const
{
	// A client that names a class in its URL gets it, subject to validation.
	const FString FromUrl = UGameplayStatics::ParseOption(JoinOptions, TEXT("class")).ToLower();
	if (!FromUrl.IsEmpty())
	{
		return FromUrl;
	}

	// Otherwise deal from the dev rotation, if one is set.
	const FString Assignment = CVarClassAssignment.GetValueOnGameThread();
	if (!Assignment.IsEmpty())
	{
		TArray<FString> Rotation;
		Assignment.ParseIntoArray(Rotation, TEXT(","), /*InCullEmpty=*/true);
		if (Rotation.Num() > 0)
		{
			// JoinCounter has already been incremented for this joiner, so the
			// first player takes entry 0.
			const int32 Index = (JoinCounter - 1) % Rotation.Num();
			return Rotation[Index].TrimStartAndEnd().ToLower();
		}
	}

	return DefaultClassId.ToString();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Phase 2b: death, respawn and rewards
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	/**
	 * GameRoom.ts:1261 — the party bonus multiplier.
	 *
	 * Ten per cent on the *pot*, not per member: a party of four splitting a
	 * 100 XP kill share 110, not 400. Grouping in 1.0 is worth it because the
	 * kill happens four times as fast, not because the XP is four times as good.
	 */
	constexpr double PartyXpBonusMultiplier = 1.10;

	/** Tell one player they gained XP, and log it. GameRoom.ts:1271. */
	void SendXpGained(AValhallaPlayerState& ValhallaPS, int32 Amount, bool bLeveled, const TCHAR* Context)
	{
		AValhallaPlayerController* Controller = Cast<AValhallaPlayerController>(ValhallaPS.GetOwner());

		if (Controller)
		{
			// xpGained and levelUp are both caster-private in 1.0
			// (GameRoom.ts:1271): a personal notification, not a world event.
			FValhallaCombatEvent XpEvent;
			XpEvent.Kind = EValhallaCombatEventKind::XpGained;
			XpEvent.Target = ValhallaPS.GetPawn();
			XpEvent.Amount = static_cast<float>(Amount);
			Controller->ClientOnCombatEvent(XpEvent);
		}

		UE_LOG(LogValhallaGame, Log, TEXT("xpGained %s +%d xp (%s) (total %d, level %d)"),
			*ValhallaPS.CharacterName, Amount, Context, ValhallaPS.Xp, ValhallaPS.Level);

		if (bLeveled && Controller)
		{
			FValhallaCombatEvent LevelEvent;
			LevelEvent.Kind = EValhallaCombatEventKind::LevelUp;
			LevelEvent.Target = ValhallaPS.GetPawn();
			LevelEvent.Amount = static_cast<float>(ValhallaPS.Level);
			Controller->ClientOnCombatEvent(LevelEvent);
		}
	}
}

void AValhallaGameMode::AwardKillXP(APlayerState* Killer, int32 Xp)
{
	AValhallaPlayerState* ValhallaPS = Cast<AValhallaPlayerState>(Killer);
	if (!ValhallaPS || Xp <= 0)
	{
		return;
	}

	// ── The party branch — GameRoom.ts:1255 ─────────────────────────────
	if (UValhallaPartySubsystem* PartySubsystem = UValhallaPartySubsystem::Get(this))
	{
		const TArray<AValhallaPlayerState*> Members = PartySubsystem->GetPartyMembers(ValhallaPS);

		// GameRoom.ts:1257 `members.size > 1` — a party of one is not a party,
		// and falls through to the solo branch rather than taking the +10%.
		if (Members.Num() > 1)
		{
			const int32 BonusXp = FMath::FloorToInt32(Xp * PartyXpBonusMultiplier);

			// GameRoom.ts:1263 — alive, and in the *killer's* zone. A member in
			// another zone or lying dead is not counted and gets nothing; they
			// are also not counted in the divisor, so the members who are there
			// take the whole pot between them.
			TArray<AValhallaPlayerState*> Eligible;
			for (AValhallaPlayerState* Member : Members)
			{
				if (Member && Member->IsAlive() && Member->ZoneId == ValhallaPS->ZoneId)
				{
					Eligible.Add(Member);
				}
			}

			if (Eligible.Num() > 0)
			{
				// GameRoom.ts:1266 — floor, then a floor of 1. A 1 XP kill split
				// four ways is 1 XP each, which is four XP awarded from one:
				// minimums do not conserve, and 1.0 chose "never award nothing".
				const int32 Share = FMath::Max(1, BonusXp / Eligible.Num());

				UE_LOG(LogValhallaGame, Log,
					TEXT("awardKillXP party %d: base %d xp x1.10 = %d, split %d ways = %d each (%d member(s), %d eligible)"),
					ValhallaPS->PartyId, Xp, BonusXp, Eligible.Num(), Share, Members.Num(), Eligible.Num());

				for (AValhallaPlayerState* Member : Eligible)
				{
					const bool bLeveled = Member->AwardXp(Share);
					SendXpGained(*Member, Share, bLeveled, TEXT("party share"));
				}

				return;
			}
		}
	}

	// ── Solo — GameRoom.ts:1279 ─────────────────────────────────────────
	const bool bLeveled = ValhallaPS->AwardXp(Xp);
	SendXpGained(*ValhallaPS, Xp, bLeveled, TEXT("solo"));
}

TArray<FValhallaBagSlot> AValhallaGameMode::RollLootTable(FName LootTableId) const
{
	if (LootTableId.IsNone())
	{
		return TArray<FValhallaBagSlot>();
	}

	const UValhallaDataSubsystem* Data = GetDataSubsystem();
	const FValhallaLootTable* Table = Data ? Data->FindLootTable(LootTableId) : nullptr;
	if (!Table)
	{
		// LootBagSystem.ts:142 — a missing table drops nothing, and is not fatal.
		UE_LOG(LogValhallaInventory, Warning, TEXT("rollLootTable: no table '%s' in loot-tables.json."), *LootTableId.ToString());
		return TArray<FValhallaBagSlot>();
	}

	// The server's own RNG, as everywhere else in 2.0's combat: rolls are made
	// here and only here, and the results are logged so a drop can be argued
	// about afterwards.
	auto Rand = []() -> double { return FMath::FRand(); };

	TArray<FValhallaBagSlot> Rolled = UValhallaInventoryLibrary::RollLootTable(*Table, Rand);

	FString Line;
	for (const FValhallaBagSlot& Slot : Rolled)
	{
		Line += FString::Printf(TEXT("%s x%d "), *Slot.ItemId.ToString(), Slot.Quantity);
	}

	UE_LOG(LogValhallaInventory, Log, TEXT("rollLootTable '%s' (%d entries) -> %s"),
		*LootTableId.ToString(), Table->Entries.Num(),
		Line.IsEmpty() ? TEXT("<nothing>") : *Line.TrimEnd());

	return Rolled;
}

void AValhallaGameMode::OnNPCKilled(AValhallaNPC* Npc, AActor* Killer)
{
	if (!Npc)
	{
		return;
	}

	const FValhallaNPCTemplate& Template = Npc->GetTemplate();
	const AValhallaCharacter* KillerCharacter = Cast<AValhallaCharacter>(Killer);
	const AValhallaPlayerState* KillerPS = KillerCharacter ? KillerCharacter->GetPlayerState<AValhallaPlayerState>() : nullptr;

	// ── Loot ────────────────────────────────────────────────────────────
	// Before the XP, and independent of it: an unclaimed kill still leaves a
	// corpse worth searching.
	const TArray<FValhallaBagSlot> Dropped = RollLootTable(Template.LootTableId);
	if (Dropped.Num() > 0)
	{
		AValhallaLootBag::SpawnOrMerge(
			GetWorld(),
			Npc->GetActorLocation(),
			Dropped,
			Npc->DisplayName.IsEmpty() ? Template.Name : Npc->DisplayName);
	}

	// ── XP ──────────────────────────────────────────────────────────────
	if (!KillerPS)
	{
		// A kill with no player behind it — a DoT from someone who logged out,
		// or an NPC finishing another NPC — awards nothing and that is correct.
		return;
	}

	AwardKillXP(KillerCharacter->GetPlayerState(), Template.XpReward);
}

void AValhallaGameMode::HandlePlayerDeath(AValhallaCharacter* Character, AActor* Killer)
{
	AValhallaPlayerState* ValhallaPS = Character ? Character->GetValhallaPlayerState() : nullptr;
	if (!ValhallaPS || !ValhallaPS->IsAlive())
	{
		return;
	}

	ValhallaPS->Hp = 0.f;
	ValhallaPS->bAlive = false;
	ValhallaPS->ShieldHp = 0.f;

	// Whatever they were doing, they have stopped doing it.
	if (UValhallaSkillComponent* Skills = Character->FindComponentByClass<UValhallaSkillComponent>())
	{
		Skills->ServerCancelCast();
		Skills->ServerStopAutoAttack();
	}
	ValhallaPS->SetTargetActor(nullptr);

	// Hide and freeze rather than ragdoll: the proxy mesh has no physics asset,
	// and Phase 4 owns what a death actually looks like.
	Character->SetDeathPresentation(true);

	const double Now = UValhallaCombatLibrary::GetServerTime(this);
	PendingRespawns.Add(ValhallaPS, Now + Valhalla::RespawnTimeMs / 1000.0);

	FValhallaCombatEvent Event;
	Event.Kind = EValhallaCombatEventKind::PlayerDied;
	Event.Target = Character;
	Event.Instigator = Killer;
	Event.Location = Character->GetActorLocation();
	UValhallaCombatLibrary::BroadcastCombatEvent(this, Event);

	UE_LOG(LogValhallaCombat, Log, TEXT("playerDied %s killed by %s, respawn in %.0f ms"),
		*ValhallaPS->CharacterName, *UValhallaCombatLibrary::GetDisplayName(Killer), Valhalla::RespawnTimeMs);
}

void AValhallaGameMode::CheckRespawns(double Now)
{
	if (PendingRespawns.Num() == 0)
	{
		return;
	}

	for (auto It = PendingRespawns.CreateIterator(); It; ++It)
	{
		AValhallaPlayerState* ValhallaPS = It.Key().Get();
		if (!ValhallaPS)
		{
			It.RemoveCurrent();
			continue;
		}

		if (Now < It.Value())
		{
			continue;
		}

		AValhallaCharacter* Character = Cast<AValhallaCharacter>(ValhallaPS->GetPawn());
		if (!Character)
		{
			It.RemoveCurrent();
			continue;
		}

		// CombatSystem.ts:459 — back at the zone's spawn point. 2.0 reuses the
		// same PlayerStart the game mode spawned them at in the first place, so
		// respawning and joining put you in the same place.
		FVector RespawnLocation = Character->GetActorLocation();
		if (AActor* Start = FindPlayerStart(Character->GetController()))
		{
			RespawnLocation = Start->GetActorLocation();
		}

		ValhallaPS->RespawnWithFullPools();

		Character->SetActorLocation(RespawnLocation, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
		Character->SetDeathPresentation(false);

		FValhallaCombatEvent Event;
		Event.Kind = EValhallaCombatEventKind::PlayerRespawned;
		Event.Target = Character;
		Event.Location = RespawnLocation;
		Event.RemainingHp = ValhallaPS->Hp;
		UValhallaCombatLibrary::BroadcastCombatEvent(this, Event);

		UE_LOG(LogValhallaCombat, Log, TEXT("playerRespawned %s at %s with hp=%.0f/%.0f mana=%.0f/%.0f energy=%.0f/%.0f"),
			*ValhallaPS->CharacterName, *RespawnLocation.ToCompactString(),
			ValhallaPS->Hp, ValhallaPS->MaxHp, ValhallaPS->Mana, ValhallaPS->MaxMana,
			ValhallaPS->Energy, ValhallaPS->MaxEnergy);

		It.RemoveCurrent();
	}
}

TArray<APlayerController*> AValhallaGameMode::FindControllersForUser(int32 UserId) const
{
	TArray<APlayerController*> Result;
	if (UserId <= 0)
	{
		return Result;
	}
	for (const TPair<TWeakObjectPtr<APlayerController>, FValhallaBackendSession>& Entry : BackendSessions)
	{
		if (APlayerController* PC = Entry.Key.Get())
		{
			if (Entry.Value.UserId == UserId)
			{
				Result.Add(PC);
			}
		}
	}
	return Result;
}

bool AValhallaGameMode::AdminResurrectInPlace(AValhallaPlayerState* ValhallaPS)
{
	AValhallaCharacter* Character = ValhallaPS ? Cast<AValhallaCharacter>(ValhallaPS->GetPawn()) : nullptr;
	if (!ValhallaPS || !Character || ValhallaPS->IsAlive())
	{
		return false;
	}

	PendingRespawns.Remove(ValhallaPS);
	ValhallaPS->RespawnWithFullPools();
	Character->SetDeathPresentation(false);

	FValhallaCombatEvent Event;
	Event.Kind = EValhallaCombatEventKind::PlayerRespawned;
	Event.Target = Character;
	Event.Location = Character->GetActorLocation();
	Event.RemainingHp = ValhallaPS->Hp;
	UValhallaCombatLibrary::BroadcastCombatEvent(this, Event);

	UE_LOG(LogValhallaCombat, Log, TEXT("playerRespawned %s in place by an admin"), *ValhallaPS->CharacterName);
	return true;
}

void AValhallaGameMode::Logout(AController* Exiting)
{
	if (APlayerController* ExitingPC = Cast<APlayerController>(Exiting))
	{
		if (AValhallaPlayerState* ValhallaPS = ExitingPC->GetPlayerState<AValhallaPlayerState>())
		{
			UE_LOG(LogValhallaGame, Log, TEXT("Logout: %s"), *ValhallaPS->CharacterName);

			// GameRoom.onLeave:701 `savePlayer` — before anything is torn down,
			// because the save reads the pawn's position and the pawn is about
			// to be destroyed. The request itself outlives this frame; the
			// subsystem's completion holds only a weak pointer, so a server
			// shutting down drops it instead of writing through a dead world.
			SaveCharacterFor(ValhallaPS, TEXT("logout"));

			// GameRoom.onLeave:696 — out of the party and out of everyone's
			// pending invites, before the player state goes away and the weak
			// pointers holding it turn into holes.
			if (UValhallaPartySubsystem* PartySubsystem = UValhallaPartySubsystem::Get(this))
			{
				PartySubsystem->HandlePlayerLeft(ValhallaPS);
			}
		}

		if (FValhallaBackendSession* Session = BackendSessions.Find(ExitingPC))
		{
			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().ClearTimer(Session->AutosaveTimer);
			}
			BackendSessions.Remove(ExitingPC);
		}

		PendingJoinOptions.Remove(ExitingPC);
	}

	Super::Logout(Exiting);
}
