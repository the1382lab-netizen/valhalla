// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaGameState.h"

#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "ValhallaDataSettings.h"
#include "ValhallaDataSubsystem.h"
#include "ValhallaAnimComponent.h"
#include "ValhallaCharacter.h"
#include "ValhallaCombatLibrary.h"
#include "ValhallaConstants.h"
#include "ValhallaGame.h"
#include "ValhallaGameMode.h"
#include "ValhallaNPC.h"
#include "ValhallaPartySubsystem.h"
#include "ValhallaPlayerState.h"
#include "ValhallaSkillComponent.h"
#include "ValhallaSpellProjectile.h"
#include "ValhallaVfxLibrary.h"
#include "ValhallaZoneSubsystem.h"

#if !UE_BUILD_SHIPPING
namespace
{
	/**
	 * `valhalla.DataHotReload` — watch the 1.0 data and the 2.0 overlays and
	 * reload them when they change on disk.
	 *
	 * On by default, because the entire point of Phase 6 is that a designer
	 * editing `items.json` in the browser sees the change in a running PIE
	 * session without stopping it. Turn it off for a profiling run, or when
	 * deliberately editing the files under a server that must not notice.
	 */
	TAutoConsoleVariable<int32> CVarDataHotReload(
		TEXT("valhalla.DataHotReload"),
		1,
		TEXT("Server, non-shipping. 1: poll shared/data/*.json and maps/overlays-2.0/*.json every 2s and reload on change. 0: off."),
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
	// The seven data files by name (UValhallaDataSubsystem::GetDataFilenames,
	// so this list cannot drift from the loader's) and every overlay in the
	// overlay directory by glob. The glob matters: an overlay for a zone that
	// did not exist when the server booted is a *new* file, and a watcher
	// keyed only on the files it saw at startup would never notice it.
	TArray<FString> DataPaths;
	const FString DataRoot = UValhallaDataSettings::Get()->GetResolvedDataRoot();
	for (const FString& Filename : UValhallaDataSubsystem::GetDataFilenames())
	{
		DataPaths.Add(FPaths::Combine(DataRoot, Filename));
	}

	TArray<FString> OverlayPaths;
	const FString OverlayDirectory = UValhallaZoneSubsystem::GetOverlayDirectory();
	FileManager.FindFiles(OverlayPaths, *FPaths::Combine(OverlayDirectory, TEXT("*.json")), /*Files=*/true, /*Directories=*/false);
	for (FString& Overlay : OverlayPaths)
	{
		Overlay = FPaths::Combine(OverlayDirectory, Overlay);
	}

	// ── What changed ─────────────────────────────────────────────────────
	bool bDataChanged = false;
	bool bOverlaysChanged = false;
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
	CheckSet(OverlayPaths, bOverlaysChanged);

	if (!bDataWatchBaselineTaken)
	{
		bDataWatchBaselineTaken = true;
		UE_LOG(LogValhallaGame, Log,
			TEXT("valhalla.DataHotReload: watching %d data files and %d overlays (%s, %s)."),
			DataPaths.Num(), OverlayPaths.Num(), *DataRoot, *OverlayDirectory);
		return;
	}

	// ── Act ──────────────────────────────────────────────────────────────
	//
	// Data first. An overlay names NPC templates, so reloading the overlays
	// against the *old* npc-templates.json and then reloading the data would
	// leave the spawned NPCs a reload behind for one cycle.
	if (bDataChanged)
	{
		UE_LOG(LogValhallaGame, Log, TEXT("valhalla.DataHotReload: %s changed — reloading data."), *FirstChangedPath);
		if (AValhallaGameMode* GameMode = World->GetAuthGameMode<AValhallaGameMode>())
		{
			FValhallaDataReloadCounts Counts;
			GameMode->ReloadGameData(Counts);
		}
	}

	if (bOverlaysChanged)
	{
		UE_LOG(LogValhallaGame, Log, TEXT("valhalla.DataHotReload: an overlay changed — reloading overlays."));
		if (UValhallaZoneSubsystem* Zones = World->GetSubsystem<UValhallaZoneSubsystem>())
		{
			Zones->ReloadOverlays();
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

void AValhallaGameState::MulticastCombatEvent_Implementation(const FValhallaCombatEvent& Event)
{
	RecordCombatEvent(Event);
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
	UValhallaAnimComponent::DispatchCombatEvent(this, Event);

	// Phase 8a: and the same events drive the Niagara. The subsystem does not
	// exist at all on a dedicated server (UValhallaVfxSubsystem::
	// ShouldCreateSubsystem), so this is a null lookup there rather than a
	// guard somebody can forget — and on a listen host the local player gets
	// their effects through exactly the path a remote client does.
	UValhallaVfxSubsystem::DispatchCombatEvent(this, Event);
}
