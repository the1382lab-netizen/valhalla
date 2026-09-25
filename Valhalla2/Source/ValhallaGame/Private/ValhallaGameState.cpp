// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaGameState.h"

#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "ValhallaDataSettings.h"
#include "ValhallaDataSubsystem.h"
#include "ValhallaAnimComponent.h"
#include "ValhallaBackendSubsystem.h"
#include "ValhallaCharacter.h"
#include "ValhallaCombatLibrary.h"
#include "ValhallaConstants.h"
#include "ValhallaGame.h"
#include "ValhallaGameMode.h"
#include "ValhallaNPC.h"
#include "ValhallaPartySubsystem.h"
#include "ValhallaPlayerController.h"
#include "ValhallaPlayerState.h"
#include "ValhallaSkillComponent.h"
#include "ValhallaSpellProjectile.h"
#include "ValhallaVfxLibrary.h"
#include "ValhallaVisibilitySubsystem.h"
#include "ValhallaZoneSubsystem.h"

CSV_DEFINE_CATEGORY(Valhalla, true);

#if !UE_BUILD_SHIPPING
namespace
{
	/**
	 * `valhalla.DataHotReload` — watch the game data files and reload them
	 * when they change on disk.
	 *
	 * On by default, because the entire point of Phase 6 is that a designer
	 * editing `items.json` in the browser sees the change in a running PIE
	 * session without stopping it. Turn it off for a profiling run, or when
	 * deliberately editing the files under a server that must not notice.
	 */
	TAutoConsoleVariable<int32> CVarDataHotReload(
		TEXT("valhalla.DataHotReload"),
		1,
		TEXT("Server, non-shipping. 1: poll shared/data/*.json every 2s and reload on change. 0: off."),
		ECVF_Default);
}
#endif

AValhallaGameState::AValhallaGameState()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// ServerTime only has to be good enough to drive a cooldown sweep; the
	// numbers that decide a cast are compared on the server against its own
	// copy, which is exact.
	SetNetUpdateFrequency(10.f);
}

void AValhallaGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AValhallaGameState, ServerTime);
	DOREPLIFETIME(AValhallaGameState, ZoneId);
	DOREPLIFETIME(AValhallaGameState, DataVersion);
}

void AValhallaGameState::BumpDataVersion()
{
	if (!HasAuthority())
	{
		return;
	}
	++DataVersion;
	ForceNetUpdate();
	UE_LOG(LogValhallaGame, Log, TEXT("data version %d: clients will reload their data."), DataVersion);
}

void AValhallaGameState::OnRep_DataVersion()
{
	// The initial replication of a GameState arrives before its BeginPlay. A
	// non-zero version then only means the server reloaded at some point before
	// this client joined, and this client loaded its data moments ago.
	if (!HasActorBegunPlay())
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UValhallaDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
	if (!Data)
	{
		return;
	}

	// A client reading its downloaded copy (B-03) fetches the changed files
	// from the backend first; one reading the repo's shared/data already sees
	// the server's edit on disk.
	if (Data->IsUsingDownloadedData())
	{
		if (UValhallaBackendSubsystem* Backend = GameInstance->GetSubsystem<UValhallaBackendSubsystem>())
		{
			TWeakObjectPtr<AValhallaGameState> WeakThis(this);
			Backend->SyncGameData(Data->GetDataRootOverride(), [WeakThis](bool bOk, int32 FilesUpdated, const FString& Error)
			{
				if (AValhallaGameState* Self = WeakThis.Get())
				{
					if (!bOk)
					{
						UE_LOG(LogValhallaGame, Warning, TEXT("data version %d: download failed (%s); keeping the current data."), Self->DataVersion, *Error);
						return;
					}
					Self->ApplyClientDataReload();
				}
			});
			return;
		}
	}

	ApplyClientDataReload();
}

void AValhallaGameState::ApplyClientDataReload()
{
	UGameInstance* GameInstance = GetGameInstance();
	UValhallaDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
	if (!Data)
	{
		return;
	}

	const bool bOk = Data->Reload();
	UE_LOG(LogValhallaGame, Log, TEXT("data version %d from the server: client reload %s."),
		DataVersion, bOk ? TEXT("ok") : TEXT("FAILED (see LogValhallaCore)"));
	if (!bOk)
	{
		return;
	}

	// Paperdolls resolve item ids to meshes on the client; everything else the
	// HUD reads (names, tooltips, skill numbers) is looked up per frame.
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AValhallaCharacter> It(World); It; ++It)
		{
			It->RefreshEquipmentVisuals();
		}

		if (AValhallaPlayerController* PC = Cast<AValhallaPlayerController>(World->GetFirstPlayerController()))
		{
			if (PC->IsLocalController())
			{
				PC->ShowLocalSystemMessage(TEXT("Game data updated by the server."));
			}
		}
	}
}

void AValhallaGameState::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority())
	{
		return;
	}

#if !UE_BUILD_SHIPPING
	// Before the fixed steps, and on real time rather than simulation time: a
	// reload is an editor event, not a game event, and it must keep happening
	// at the same wall-clock rate whether the server is hitching or idle.
	TickDataWatcher(DeltaSeconds);
#endif

	constexpr double FixedStep = 1.0 / static_cast<double>(Valhalla::ServerTickRate);

	TickAccumulator += DeltaSeconds;

	// Never try to catch up more than a quarter second: a hitch must not turn
	// into a burst of 200 regen steps that hands everyone a full mana bar, or
	// fifteen auto-attack swings landing in the same frame.
	TickAccumulator = FMath::Min(TickAccumulator, 0.25);

	while (TickAccumulator >= FixedStep)
	{
		TickAccumulator -= FixedStep;
		ServerTime += FixedStep;
		ServerFixedTick(static_cast<float>(FixedStep));
	}

	// Everything raised this frame — by RPCs processed before the world tick
	// and by the fixed steps above — goes out as one batch.
	FlushCombatEvents();
}

#if !UE_BUILD_SHIPPING
void AValhallaGameState::TickDataWatcher(float DeltaSeconds)
{
	if (CVarDataHotReload.GetValueOnGameThread() == 0)
	{
		return;
	}

	DataWatchAccumulator += DeltaSeconds;
	if (DataWatchAccumulator < DataWatchIntervalSeconds)
	{
		return;
	}
	DataWatchAccumulator = 0.0;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	IFileManager& FileManager = IFileManager::Get();

	// ── What is watched ──────────────────────────────────────────────────
	//
	// The data files by name (UValhallaDataSubsystem::GetDataFilenames, so
	// this list cannot drift from the loader's). Zone actors — portals,
	// entries, player starts, NPC Spawn Points — live in the levels, not on
	// disk as JSON, so there is nothing else to watch.
	TArray<FString> DataPaths;
	const FString DataRoot = UValhallaDataSettings::Get()->GetResolvedDataRoot();
	for (const FString& Filename : UValhallaDataSubsystem::GetDataFilenames())
	{
		DataPaths.Add(FPaths::Combine(DataRoot, Filename));
	}

	// ── What changed ─────────────────────────────────────────────────────
	bool bDataChanged = false;
	FString FirstChangedPath;

	auto CheckSet = [this, &FileManager, &FirstChangedPath](const TArray<FString>& Paths, bool& bOutChanged)
	{
		for (const FString& Path : Paths)
		{
			// A file being written right now reads as a missing file on some
			// platforms. Treating that as "changed" would fire a reload against
			// a half-written document; skipping it means the *next* poll, two
			// seconds later, sees the finished file and fires then.
			const FDateTime Timestamp = FileManager.GetTimeStamp(*Path);
			if (Timestamp == FDateTime::MinValue())
			{
				continue;
			}

			const FDateTime* Known = WatchedTimestamps.Find(Path);
			if (Known && *Known != Timestamp)
			{
				bOutChanged = true;
				if (FirstChangedPath.IsEmpty())
				{
					FirstChangedPath = Path;
				}
			}
			else if (!Known && bDataWatchBaselineTaken)
			{
				// A file that appeared after the baseline.
				bOutChanged = true;
				if (FirstChangedPath.IsEmpty())
				{
					FirstChangedPath = Path;
				}
			}

			WatchedTimestamps.Add(Path, Timestamp);
		}
	};

	CheckSet(DataPaths, bDataChanged);

	if (!bDataWatchBaselineTaken)
	{
		bDataWatchBaselineTaken = true;
		UE_LOG(LogValhallaGame, Log,
			TEXT("valhalla.DataHotReload: watching %d data files (%s)."),
			DataPaths.Num(), *DataRoot);
		return;
	}

	// ── Act ──────────────────────────────────────────────────────────────
	if (bDataChanged)
	{
		UE_LOG(LogValhallaGame, Log, TEXT("valhalla.DataHotReload: %s changed — reloading data."), *FirstChangedPath);
		if (AValhallaGameMode* GameMode = World->GetAuthGameMode<AValhallaGameMode>())
		{
			FValhallaDataReloadCounts Counts;
			GameMode->ReloadGameData(Counts);
		}
	}
}
#endif // !UE_BUILD_SHIPPING

void AValhallaGameState::ServerFixedTick(float FixedDeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const double Now = ServerTime;

	// ── GameRoom.update step 3: spell projectiles ────────────────────────
	// First, so a projectile that reaches its target this tick detonates before
	// anything has had a chance to move out of its blast.
	for (TActorIterator<AValhallaSpellProjectile> It(World); It; ++It)
	{
		It->ServerFixedTick(FixedDeltaSeconds, Now);
	}

	// ── step 4: regen, cast progression, buffs; 4b: auto-attacks ─────────
	for (APlayerState* Entry : PlayerArray)
	{
		AValhallaPlayerState* ValhallaPS = Cast<AValhallaPlayerState>(Entry);
		if (!ValhallaPS)
		{
			continue;
		}

		// SkillSystem.update:324 — regen is for the living only.
		ValhallaPS->TickRegen(FixedDeltaSeconds);

		if (const AValhallaCharacter* Character = Cast<AValhallaCharacter>(ValhallaPS->GetPawn()))
		{
			if (UValhallaSkillComponent* Skills = Character->FindComponentByClass<UValhallaSkillComponent>())
			{
				Skills->ServerFixedTick(FixedDeltaSeconds, Now);
			}
		}
	}

	// ── step 5: respawns ─────────────────────────────────────────────────
	if (AValhallaGameMode* GameMode = World->GetAuthGameMode<AValhallaGameMode>())
	{
		GameMode->CheckRespawns(Now);
	}

	// ── step 6: NPCs ─────────────────────────────────────────────────────
	for (TActorIterator<AValhallaNPC> It(World); It; ++It)
	{
		It->ServerFixedTick(FixedDeltaSeconds, Now);
	}

	// ── step 7: party invite expiry ──────────────────────────────────────
	// Last, because nothing else this tick depends on it, and cheap: it returns
	// immediately when there are no parties and no invites, which is almost
	// always. 1.0 had no equivalent step — its invites never expired.
	if (UValhallaPartySubsystem* PartySubsystem = World->GetSubsystem<UValhallaPartySubsystem>())
	{
		PartySubsystem->ExpireInvites(Now);
	}

	// ── step 8: whose zone is whose ──────────────────────────────────────
	// 1.0 did the equivalent in `checkZoneTransitions` (step 9), which only
	// ever changed `zoneId` at a portal. Phase 3's zones are boxes in one
	// world, so a player can also simply *walk* out of one, and
	// `PlayerState::ZoneId` has to follow — it is what `general` chat and the
	// party XP split compare. Last, because nothing else this tick reads it,
	// and cheap: a player who has not left their zone costs two compares.
	if (UValhallaZoneSubsystem* ZoneSubsystem = World->GetSubsystem<UValhallaZoneSubsystem>())
	{
		ZoneSubsystem->UpdatePlayerZones();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Combat events
// ─────────────────────────────────────────────────────────────────────────────

void AValhallaGameState::QueueCombatEvent(const FValhallaCombatEvent& Event)
{
	if (!HasAuthority())
	{
		RecordCombatEvent(Event);
		return;
	}
	PendingCombatEvents.Add(Event);
}

void AValhallaGameState::FlushCombatEvents()
{
	if (PendingCombatEvents.Num() == 0)
	{
		return;
	}

	// Moved out first: RecordCombatEvent runs on the server too, and a listener
	// that raised another event while it ran must land in the next batch rather
	// than in the array being iterated.
	TArray<FValhallaCombatEvent> Batch = MoveTemp(PendingCombatEvents);
	PendingCombatEvents.Reset();

	// The server handles each event once, as the multicast used to: the
	// dedicated server's listeners, and on a listen server or standalone the
	// host's own HUD, bodies and effects. The host is never sent a copy.
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Valhalla_CombatEvents);
		for (const FValhallaCombatEvent& Event : Batch)
		{
			RecordCombatEvent(Event);
		}
	}

	RouteCombatEvents(Batch);
}

namespace
{
	/**
	 * `valhalla.CombatEventRouting` — B-27 Phase 3. 1 (default): each player is
	 * sent the events they are part of or can see. 0: every player gets every
	 * event on the reliable batch, as before B-27, for comparing the two in the
	 * combat benchmark.
	 */
	TAutoConsoleVariable<int32> CVarCombatEventRouting(
		TEXT("valhalla.CombatEventRouting"),
		1,
		TEXT("Server. 1: send each player only the combat events they are part of or can see. 0: send every event to everyone."),
		ECVF_Default);

	/** The router's facts about one actor that do not depend on who is receiving. */
	FValhallaCombatEventActor DescribeEventActor(const AActor* Actor)
	{
		FValhallaCombatEventActor Out;
		Out.Actor = Actor;
		const APawn* Pawn = Cast<APawn>(Actor);
		if (const AValhallaPlayerState* PlayerState = Pawn ? Pawn->GetPlayerState<AValhallaPlayerState>() : nullptr)
		{
			Out.PlayerState = PlayerState;
			Out.ZoneId = PlayerState->ZoneId;
			Out.PartyId = PlayerState->PartyId;
		}
		return Out;
	}

	/**
	 * Is `Actor` on `Controller`'s client right now? The replication system's
	 * own answer: an open actor channel on the connection. Under Iris there are
	 * no actor channels, so it falls back to the relevancy rule that decides
	 * them (UValhallaVisibilitySubsystem::IsRelevantForViewer).
	 */
	bool IsOnClient(const APlayerController* Controller, const AActor* Actor)
	{
		if (!Actor)
		{
			return false;
		}
		if (Actor->bAlwaysRelevant)
		{
			return true;
		}
		UNetConnection* Connection = Controller->GetNetConnection();
		if (!Connection)
		{
			return true;
		}
		const UNetDriver* Driver = Connection->GetDriver();
		if (Driver && Driver->IsUsingIrisReplication())
		{
			return UValhallaVisibilitySubsystem::IsRelevantForViewer(Actor, Controller, Controller->GetViewTarget());
		}
		return Connection->FindActorChannelRef(TWeakObjectPtr<AActor>(const_cast<AActor*>(Actor))) != nullptr;
	}
}

EValhallaCombatEventRoute AValhallaGameState::RouteCombatEvent(const FValhallaCombatEventViewer& Viewer,
	const FValhallaCombatEventActor& Target, const FValhallaCombatEventActor& Instigator, FName EventZone)
{
	const FValhallaCombatEventActor* Actors[2] = { &Target, &Instigator };

	auto IsPartyMember = [&Viewer](const FValhallaCombatEventActor& Actor)
	{
		return Viewer.PartyId != 0 && Actor.PartyId == Viewer.PartyId;
	};

	// The player's own events, and their party's while the member is in the
	// same zone (decision 4): the combat log is written from these.
	for (const FValhallaCombatEventActor* Actor : Actors)
	{
		if (!Actor->Actor)
		{
			continue;
		}
		const bool bSelf = Actor->Actor == Viewer.Pawn || (Actor->PlayerState && Actor->PlayerState == Viewer.PlayerState);
		if (bSelf || (IsPartyMember(*Actor) && Actor->ZoneId == Viewer.ZoneId))
		{
			return EValhallaCombatEventRoute::Guaranteed;
		}
	}

	// B-24: on a loading screen nothing around the player is drawn.
	if (Viewer.bInTransit)
	{
		return EValhallaCombatEventRoute::Skip;
	}

	bool bNamesAnActor = false;
	for (const FValhallaCombatEventActor* Actor : Actors)
	{
		if (!Actor->Actor)
		{
			continue;
		}
		bNamesAnActor = true;
		// A party member's pawn is on every member's client wherever it is (the
		// visibility subsystem's party rule), so being there says nothing about
		// being seen: a member in another zone does not count (one in the same
		// zone was guaranteed above).
		if (Actor->bOnClient && !IsPartyMember(*Actor))
		{
			return EValhallaCombatEventRoute::Seen;
		}
	}

	// An event with no actor at all (its caster gone): whoever is in its zone.
	if (!bNamesAnActor && !EventZone.IsNone() && EventZone == Viewer.ZoneId)
	{
		return EValhallaCombatEventRoute::Seen;
	}
	return EValhallaCombatEventRoute::Skip;
}

void AValhallaGameState::RouteCombatEvents(const TArray<FValhallaCombatEvent>& Batch)
{
	UWorld* World = GetWorld();
	if (!World || GetNetMode() == NM_Standalone)
	{
		return;
	}
	TRACE_CPUPROFILER_EVENT_SCOPE(Valhalla_CombatEvents_Route);

	const bool bRoute = CVarCombatEventRouting.GetValueOnGameThread() != 0;

	// What does not depend on the receiver, once per event.
	struct FEventFacts
	{
		FValhallaCombatEventActor Target;
		FValhallaCombatEventActor Instigator;
		FName Zone;
	};
	TArray<FEventFacts> Facts;
	if (bRoute)
	{
		const UValhallaZoneSubsystem* Zones = World->GetSubsystem<UValhallaZoneSubsystem>();
		Facts.Reserve(Batch.Num());
		for (const FValhallaCombatEvent& Event : Batch)
		{
			FEventFacts& Fact = Facts.AddDefaulted_GetRef();
			Fact.Target = DescribeEventActor(Event.Target);
			Fact.Instigator = DescribeEventActor(Event.Instigator);
			if (!Event.Target && !Event.Instigator && Zones)
			{
				const FValhallaZoneDef* Zone = Zones->GetZoneAt(Event.Location);
				Fact.Zone = Zone ? Zone->ZoneId : NAME_None;
			}
		}
	}

	int32 SentGuaranteed = 0;
	int32 SentSeen = 0;
	int32 Skipped = 0;
	TArray<FValhallaCombatEvent> Guaranteed;
	TArray<FValhallaCombatEvent> Seen;

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		AValhallaPlayerController* Controller = Cast<AValhallaPlayerController>(It->Get());
		// A local controller (the listen-server host) already has the events.
		if (!Controller || Controller->IsLocalController())
		{
			continue;
		}

		if (!bRoute)
		{
			Controller->ClientCombatEvents(Batch);
			SentGuaranteed += Batch.Num();
			continue;
		}

		const AValhallaPlayerState* PlayerState = Controller->GetPlayerState<AValhallaPlayerState>();
		FValhallaCombatEventViewer Viewer;
		Viewer.Pawn = Controller->GetPawn();
		Viewer.PlayerState = PlayerState;
		Viewer.ZoneId = PlayerState ? PlayerState->ZoneId : NAME_None;
		Viewer.PartyId = PlayerState ? PlayerState->PartyId : 0;
		Viewer.bInTransit = PlayerState && PlayerState->bInZoneTransit;

		Guaranteed.Reset();
		Seen.Reset();
		for (int32 Index = 0; Index < Batch.Num(); ++Index)
		{
			FEventFacts& Fact = Facts[Index];
			Fact.Target.bOnClient = IsOnClient(Controller, Fact.Target.Actor);
			Fact.Instigator.bOnClient = IsOnClient(Controller, Fact.Instigator.Actor);
			switch (RouteCombatEvent(Viewer, Fact.Target, Fact.Instigator, Fact.Zone))
			{
			case EValhallaCombatEventRoute::Guaranteed:
				Guaranteed.Add(Batch[Index]);
				break;
			case EValhallaCombatEventRoute::Seen:
				Seen.Add(Batch[Index]);
				break;
			default:
				++Skipped;
				break;
			}
		}

		// The guaranteed batch first, so on a clean link a frame's events arrive
		// in the order they were raised.
		if (Guaranteed.Num() > 0)
		{
			Controller->ClientCombatEvents(Guaranteed);
			SentGuaranteed += Guaranteed.Num();
		}
		if (Seen.Num() > 0)
		{
			Controller->ClientCombatEventsSeen(Seen);
			SentSeen += Seen.Num();
		}
	}

	CSV_CUSTOM_STAT(Valhalla, CombatEventsSentGuaranteed, SentGuaranteed, ECsvCustomStatOp::Accumulate);
	CSV_CUSTOM_STAT(Valhalla, CombatEventsSentSeen, SentSeen, ECsvCustomStatOp::Accumulate);
	CSV_CUSTOM_STAT(Valhalla, CombatEventsSkipped, Skipped, ECsvCustomStatOp::Accumulate);
}

void AValhallaGameState::ReceiveCombatEvents(const TArray<FValhallaCombatEvent>& Events)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Valhalla_CombatEvents);
	// How many per frame, for the CSV profiler; and how many arrived naming no
	// actor this client has (the events of a fight it cannot see: B-27's target is 0).
	int32 Unresolved = 0;
	for (const FValhallaCombatEvent& Event : Events)
	{
		Unresolved += (!Event.Target && !Event.Instigator) ? 1 : 0;
		RecordCombatEvent(Event);
	}
	CSV_CUSTOM_STAT(Valhalla, CombatEventsReceived, Events.Num(), ECsvCustomStatOp::Accumulate);
	CSV_CUSTOM_STAT(Valhalla, CombatEventsUnresolved, Unresolved, ECsvCustomStatOp::Accumulate);
}

void AValhallaGameState::MirrorCooldownFromEvent(const FValhallaCombatEvent& Event)
{
	if (Event.Kind != EValhallaCombatEventKind::SkillEffect || GetNetMode() != NM_Client)
	{
		return;
	}

	const AValhallaCharacter* Caster = Cast<AValhallaCharacter>(Event.Instigator);
	AValhallaPlayerState* CasterState = Caster ? Caster->GetValhallaPlayerState() : nullptr;
	const UGameInstance* GameInstance = GetGameInstance();
	const UValhallaDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
	const FValhallaSkillTemplate* Skill = Data ? Data->FindSkill(Event.SkillId) : nullptr;
	if (!CasterState || !Skill)
	{
		return;
	}

	UValhallaCombatLibrary::ApplyCooldown(
		CasterState->GetSkillCooldownExpiry(), *Skill, Data->GetClassSkills(CasterState->ClassId),
		[Data](FName Id) { return Data->FindSkill(Id); }, ServerTime);
}

void AValhallaGameState::RecordCombatEvent(const FValhallaCombatEvent& Event)
{
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;

	// Real time, not ServerTime: this drives a one-second fade on screen, which
	// should be one second of wall clock whatever the server's clock is doing.
	RecentEvents.Add(TPair<double, FValhallaCombatEvent>(Now, Event));

	RecentEvents.RemoveAll([Now](const TPair<double, FValhallaCombatEvent>& Entry)
	{
		return Now - Entry.Key > CombatEventLifetimeSeconds;
	});

	while (RecentEvents.Num() > MaxRecentEvents)
	{
		RecentEvents.RemoveAt(0);
	}

	// Phase 4c: the same events that draw a floating damage number also drive
	// the bodies. This is the single funnel every combat event passes through
	// on every end — the authority, the listen host and each client — so a
	// simulated proxy swings, flinches and dies off exactly the information the
	// combat log is written from, with nothing extra on the wire.
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Valhalla_CombatEvent_Anim);
		UValhallaAnimComponent::DispatchCombatEvent(this, Event);
	}

	// Phase 8a: and the same events drive the Niagara. The subsystem does not
	// exist at all on a dedicated server (UValhallaVfxSubsystem::
	// ShouldCreateSubsystem), so this is a null lookup there rather than a
	// guard somebody can forget — and on a listen host the local player gets
	// their effects through exactly the path a remote client does.
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Valhalla_CombatEvent_Vfx);
		UValhallaVfxSubsystem::DispatchCombatEvent(this, Event);
	}

	// Phase 8b: the client's action bar sweep, and the HUD.
	MirrorCooldownFromEvent(Event);
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Valhalla_CombatEvent_Listeners);
		OnCombatEvent.Broadcast(Event);
	}
}
