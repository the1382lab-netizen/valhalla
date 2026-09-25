// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaZoneSubsystem.h"

#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "ValhallaCombatLibrary.h"
#include "ValhallaDataSettings.h"
#include "ValhallaGame.h"
#include "ValhallaGameMode.h"
#include "ValhallaGameTypes.h"
#include "ValhallaNPC.h"
#include "ValhallaNPCSpawner.h"
#include "ValhallaPlayerState.h"
#include "ValhallaPortal.h"
#include "ValhallaZoneEntry.h"
#include "ValhallaZoneVolume.h"

DEFINE_LOG_CATEGORY(LogValhallaZones);

// ─────────────────────────────────────────────────────────────────────────────
//  Lifetime
// ─────────────────────────────────────────────────────────────────────────────

bool UValhallaZoneSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Game and PIE worlds only. An editor preview world or a thumbnail world
	// has no players and no portals, and building a zone list for each one
	// would only fill the log.
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->IsGameWorld());
}

void UValhallaZoneSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UValhallaZoneSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	DiscoverZones();

	// Server only: a client's world is whatever the server replicated to it,
	// and a warning per client window would only repeat the server's.
	if (InWorld.GetNetMode() != NM_Client)
	{
		CheckPlacedActors();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  The zone list
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaZoneSubsystem::DiscoverZones()
{
	Zones.Reset();

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<AValhallaZoneVolume*> Volumes;
	AValhallaZoneVolume::FindAll(World, Volumes);

	for (const AValhallaZoneVolume* Volume : Volumes)
	{
		if (!Volume)
		{
			continue;
		}

		if (Volume->ZoneId.IsNone())
		{
			UE_LOG(LogValhallaZones, Warning,
				TEXT("zone volume %s has no ZoneId; ignored."), *Volume->GetName());
			continue;
		}

		if (FindZone(Volume->ZoneId))
		{
			UE_LOG(LogValhallaZones, Warning,
				TEXT("two zone volumes claim id '%s'; keeping the first."), *Volume->ZoneId.ToString());
			continue;
		}

		const FValhallaZoneDef Def = Volume->ToZoneDef();
		if (!Def.IsValid())
		{
			UE_LOG(LogValhallaZones, Warning,
				TEXT("zone volume %s ('%s') has a degenerate box; ignored."),
				*Volume->GetName(), *Volume->ZoneId.ToString());
			continue;
		}

		Zones.Add(Def);

		UE_LOG(LogValhallaZones, Log,
			TEXT("zone '%s' (%s): (%.0f, %.0f) .. (%.0f, %.0f), spawn (%.0f, %.0f, %.0f) yaw %.0f"),
			*Def.ZoneId.ToString(), *Def.DisplayName,
			Def.Bounds.Min.X, Def.Bounds.Min.Y, Def.Bounds.Max.X, Def.Bounds.Max.Y,
			Def.DefaultSpawn.X, Def.DefaultSpawn.Y, Def.DefaultSpawn.Z, Def.DefaultSpawnYaw);
	}

	if (Zones.Num() == 0)
	{
		// Not a warning: L_GreyBox and L_LoSTest are supposed to have none, and
		// everything about them keeps working without a zone list.
		UE_LOG(LogValhallaZones, Log,
			TEXT("no zone volumes in %s; zone lookups will return nothing."), *World->GetName());
	}
}

const FValhallaZoneDef* UValhallaZoneSubsystem::GetZoneAt(const FVector& WorldLocation) const
{
	for (const FValhallaZoneDef& Def : Zones)
	{
		if (Def.Contains2D(WorldLocation))
		{
			return &Def;
		}
	}
	return nullptr;
}

const FValhallaZoneDef* UValhallaZoneSubsystem::FindZone(FName InZoneId) const
{
	if (InZoneId.IsNone())
	{
		return nullptr;
	}

	for (const FValhallaZoneDef& Def : Zones)
	{
		if (Def.ZoneId == InZoneId)
		{
			return &Def;
		}
	}
	return nullptr;
}

bool UValhallaZoneSubsystem::GetDefaultSpawn(FName InZoneId, FVector& OutLocation, float& OutYaw) const
{
	if (const FValhallaZoneDef* Def = FindZone(InZoneId))
	{
		OutLocation = Def->DefaultSpawn;
		OutYaw = Def->DefaultSpawnYaw;
		return true;
	}
	return false;
}

bool UValhallaZoneSubsystem::FindStandingZ(const UWorld* World, double X, double Y, double TopZ, double BottomZ,
	float CapsuleRadius, float CapsuleHalfHeight, const AActor* IgnoreActor, double& OutCentreZ)
{
	if (!World || TopZ <= BottomZ || CapsuleRadius <= 0.f || CapsuleHalfHeight <= 0.f)
	{
		return false;
	}

	// The walkable-floor limit CharacterMovement uses by default (44.8 deg), and
	// how far above the terrain a floor can be and still count as "the floor
	// here": a tavern's ground floor (+6), a bridge deck (~+180 over the river
	// bed) or the great hall over the undercroft pit (+306) are; a roof
	// (+360 and up) or a wall top is not.
	constexpr double MinWalkableNormalZ = 0.71;
	constexpr double MaxAboveTerrainCm = 320.0;
	constexpr double MaxBelowTerrainCm = 100.0;

	// Landscape by class path, so this module does not have to link Landscape.
	static const UClass* LandscapeCollisionClass =
		FindObject<UClass>(nullptr, TEXT("/Script/Landscape.LandscapeHeightfieldCollisionComponent"));

	// An object-type query returns every surface along the ray, not just the first.
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ValhallaFindStandingZ), /*bTraceComplex*/ false, IgnoreActor);

	TArray<FHitResult> Hits;
	World->LineTraceMultiByObjectType(Hits, FVector(X, Y, TopZ), FVector(X, Y, BottomZ), Objects, Params);

	double TerrainZ = 0.0;
	bool bTerrain = false;
	for (const FHitResult& Hit : Hits)
	{
		const UPrimitiveComponent* Component = Hit.GetComponent();
		if (Component && LandscapeCollisionClass && Component->IsA(LandscapeCollisionClass))
		{
			TerrainZ = Hit.ImpactPoint.Z;
			bTerrain = true;
			break;
		}
	}
	if (!bTerrain)
	{
		return false;   // a flat tile zone: the caller's own height is right
	}

	// The highest walkable surface near the terrain that the capsule fits on.
	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight);
	FCollisionQueryParams OverlapParams(SCENE_QUERY_STAT(ValhallaFindStandingZOverlap), false, IgnoreActor);
	double Best = TerrainZ;
	bool bBest = false;
	for (const FHitResult& Hit : Hits)
	{
		const double Z = Hit.ImpactPoint.Z;
		if (Hit.ImpactNormal.Z < MinWalkableNormalZ || Z > TerrainZ + MaxAboveTerrainCm || Z < TerrainZ - MaxBelowTerrainCm)
		{
			continue;
		}
		const FVector Centre(X, Y, Z + CapsuleHalfHeight + 2.0);
		if (World->OverlapBlockingTestByChannel(Centre, FQuat::Identity, ECC_Pawn, Capsule, OverlapParams))
		{
			continue;
		}
		if (!bBest || Z > Best)
		{
			Best = Z;
			bBest = true;
		}
	}
	OutCentreZ = (bBest ? Best : TerrainZ) + CapsuleHalfHeight;
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Zone tracking
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaZoneSubsystem::UpdatePlayerZones()
{
	UWorld* World = GetWorld();
	if (!World || Zones.Num() == 0 || World->GetNetMode() == NM_Client)
	{
		return;
	}

	const AGameStateBase* GameState = World->GetGameState();
	if (!GameState)
	{
		return;
	}

	for (APlayerState* Entry : GameState->PlayerArray)
	{
		AValhallaPlayerState* ValhallaPS = Cast<AValhallaPlayerState>(Entry);
		if (!ValhallaPS)
		{
			continue;
		}

		const APawn* Pawn = ValhallaPS->GetPawn();
		if (!Pawn)
		{
			continue;
		}

		const FVector Location = Pawn->GetActorLocation();

		// Fast path: still where they were. Two compares, and this is the
		// answer 3599 out of 3600 fixed ticks.
		if (const FValhallaZoneDef* Current = FindZone(ValhallaPS->ZoneId))
		{
			if (Current->Contains2D(Location))
			{
				continue;
			}
		}

		if (const FValhallaZoneDef* Found = GetZoneAt(Location))
		{
			if (Found->ZoneId != ValhallaPS->ZoneId)
			{
				UE_LOG(LogValhallaZones, Log,
					TEXT("%s walked from zone '%s' into '%s'"),
					*ValhallaPS->GetPlayerName(), *ValhallaPS->ZoneId.ToString(), *Found->ZoneId.ToString());
				ValhallaPS->ZoneId = Found->ZoneId;
			}
		}
		// Outside every zone: keep the id they had. See the class comment.
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Travel
// ─────────────────────────────────────────────────────────────────────────────

bool UValhallaZoneSubsystem::ResolveArrival(FName TargetZoneId, FName TargetEntryId, FVector& OutLocation, float& OutYaw) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// 1.0's precedence, MapManager.ts:134-147: the matching entry first, the
	// zone's default spawn second.
	if (!TargetEntryId.IsNone())
	{
		if (const AValhallaZoneEntry* Entry = AValhallaZoneEntry::Find(World, TargetEntryId))
		{
			OutLocation = Entry->GetActorLocation();
			OutYaw = Entry->GetActorRotation().Yaw;
			return true;
		}

		UE_LOG(LogValhallaZones, Warning,
			TEXT("portal names entry '%s', which no actor in the world has; falling back to zone '%s' default spawn."),
			*TargetEntryId.ToString(), *TargetZoneId.ToString());
	}

	return GetDefaultSpawn(TargetZoneId, OutLocation, OutYaw);
}

bool UValhallaZoneSubsystem::TravelThroughPortal(APawn* Pawn, const AValhallaPortal* Portal)
{
	if (!Portal)
	{
		return false;
	}
	return TravelToZone(Pawn, Portal->TargetZoneId, Portal->TargetEntryId);
}

bool UValhallaZoneSubsystem::TravelToZone(APawn* Pawn, FName TargetZoneId, FName TargetEntryId)
{
	UWorld* World = GetWorld();
	if (!Pawn || !World || World->GetNetMode() == NM_Client)
	{
		return false;
	}

	if (TargetZoneId.IsNone())
	{
		return false;
	}

	const double Now = World->GetTimeSeconds();

	if (const double* Last = LastTravelTime.Find(Pawn))
	{
		if (Now - *Last < TravelCooldownSeconds)
		{
			// Silent: a pawn standing in a trigger re-overlaps constantly and a
			// warning per frame would bury the log.
			return false;
		}
	}

	FVector Arrival = FVector::ZeroVector;
	float ArrivalYaw = 0.f;
	if (!ResolveArrival(TargetZoneId, TargetEntryId, Arrival, ArrivalYaw))
	{
		UE_LOG(LogValhallaZones, Warning,
			TEXT("cannot travel to zone '%s': no entry '%s' and no zone volume for it."),
			*TargetZoneId.ToString(), *TargetEntryId.ToString());
		return false;
	}

	AValhallaPlayerState* ValhallaPS = Cast<AValhallaPlayerState>(Pawn->GetPlayerState());
	const FName FromZone = ValhallaPS ? ValhallaPS->ZoneId : NAME_None;

	// ── Move, in 1.0's order: place first, re-zone second ────────────────

	// Lift the arrival point to where the pawn's origin belongs. A zone entry
	// arrow is authored on the floor; a character's origin is its capsule
	// centre, so dropping it in at floor height puts half the character
	// underground and lets the first movement tick eject it.
	double CapsuleHalfHeight = 0.0;
	if (const ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		}
	}

	const FVector Destination = Arrival + FVector(0.0, 0.0, CapsuleHalfHeight + 2.0);

	Pawn->SetActorLocationAndRotation(
		Destination, FRotator(0.f, ArrivalYaw, 0.f),
		/*bSweep*/ false, nullptr, ETeleportType::TeleportPhysics);

	// Velocity zero, or a player arrives still running in the direction of a
	// portal they can no longer see.
	if (UPawnMovementComponent* Movement = Pawn->GetMovementComponent())
	{
		Movement->StopMovementImmediately();
		Movement->Velocity = FVector::ZeroVector;
	}

	if (ValhallaPS)
	{
		ValhallaPS->ZoneId = TargetZoneId;
	}

	LastTravelTime.Add(Pawn, Now);

	// ── Announce ─────────────────────────────────────────────────────────

	FValhallaCombatEvent Event;
	Event.Kind = EValhallaCombatEventKind::ZoneChanged;
	Event.Target = Pawn;
	Event.Location = Destination;
	Event.Text = FString::Printf(TEXT("%s -> %s"),
		FromZone.IsNone() ? TEXT("?") : *FromZone.ToString(), *TargetZoneId.ToString());
	UValhallaCombatLibrary::BroadcastCombatEvent(Pawn, Event);

	UE_LOG(LogValhallaZones, Log,
		TEXT("zoneChange %s: %s -> %s at (%.0f, %.0f, %.0f) via entry '%s'"),
		ValhallaPS ? *ValhallaPS->GetPlayerName() : *Pawn->GetName(),
		FromZone.IsNone() ? TEXT("?") : *FromZone.ToString(), *TargetZoneId.ToString(),
		Destination.X, Destination.Y, Destination.Z,
		TargetEntryId.IsNone() ? TEXT("(default spawn)") : *TargetEntryId.ToString());

	// ── Phase 7: persist the crossing ────────────────────────────────────
	//
	// A zone change is the one movement that must not wait for the 30 s
	// autosave. Everything else a save records can be re-derived from where
	// the player is standing; the zone cannot, because a crash between the
	// crossing and the next autosave would bring the character back in the
	// zone it left, at a position that is local to the zone it arrived in —
	// two wrong answers that compose into a character inside the terrain.
	if (ValhallaPS)
	{
		if (AValhallaGameMode* GameMode = World->GetAuthGameMode<AValhallaGameMode>())
		{
			GameMode->SaveCharacterFor(ValhallaPS, TEXT("zone change"));
		}
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Placed-actor check
// ─────────────────────────────────────────────────────────────────────────────

int32 UValhallaZoneSubsystem::CheckPlacedActors() const
{
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || Zones.Num() == 0)
	{
		return 0;
	}

	int32 Problems = 0;

	// ── Zone entries: ids must be unique, or a portal's arrival is a coin toss ──
	TMap<FName, const AValhallaZoneEntry*> EntriesById;
	int32 EntryCount = 0;
	for (TActorIterator<AValhallaZoneEntry> It(World); It; ++It)
	{
		++EntryCount;
		const AValhallaZoneEntry* Entry = *It;
		if (Entry->EntryId.IsNone())
		{
			UE_LOG(LogValhallaZones, Warning, TEXT("zone entry %s has no EntryId; no portal can arrive at it."),
				*Entry->GetName());
			++Problems;
			continue;
		}
		if (const AValhallaZoneEntry* const* Existing = EntriesById.Find(Entry->EntryId))
		{
			UE_LOG(LogValhallaZones, Warning,
				TEXT("zone entries %s and %s both use EntryId '%s'; portals will arrive at whichever is found first."),
				*(*Existing)->GetName(), *Entry->GetName(), *Entry->EntryId.ToString());
			++Problems;
			continue;
		}
		EntriesById.Add(Entry->EntryId, Entry);

		if (!Entry->FromZoneId.IsNone() && !FindZone(Entry->FromZoneId))
		{
			UE_LOG(LogValhallaZones, Warning, TEXT("zone entry %s ('%s') says FromZoneId '%s', which is not a zone."),
				*Entry->GetName(), *Entry->EntryId.ToString(), *Entry->FromZoneId.ToString());
			++Problems;
		}
	}

	// ── Portals: the target zone exists, the target entry exists and is in it ──
	int32 PortalCount = 0;
	for (TActorIterator<AValhallaPortal> It(World); It; ++It)
	{
		++PortalCount;
		const AValhallaPortal* Portal = *It;
		const FValhallaZoneDef* Target = FindZone(Portal->TargetZoneId);
		if (!Target)
		{
			UE_LOG(LogValhallaZones, Warning, TEXT("portal %s leads to zone '%s', which is not a zone; it will do nothing."),
				*Portal->GetName(), *Portal->TargetZoneId.ToString());
			++Problems;
			continue;
		}
		if (Portal->TargetEntryId.IsNone())
		{
			// Legal: travel falls back to the zone's default spawn.
			continue;
		}
		const AValhallaZoneEntry* const* Entry = EntriesById.Find(Portal->TargetEntryId);
		if (!Entry)
		{
			UE_LOG(LogValhallaZones, Warning,
				TEXT("portal %s names entry '%s', which no zone entry has; players will arrive at '%s' default spawn."),
				*Portal->GetName(), *Portal->TargetEntryId.ToString(), *Portal->TargetZoneId.ToString());
			++Problems;
			continue;
		}
		const FValhallaZoneDef* EntryZone = GetZoneAt((*Entry)->GetActorLocation());
		if (EntryZone && EntryZone->ZoneId != Target->ZoneId)
		{
			UE_LOG(LogValhallaZones, Warning,
				TEXT("portal %s leads to zone '%s' but its entry '%s' stands in zone '%s'."),
				*Portal->GetName(), *Target->ZoneId.ToString(), *Portal->TargetEntryId.ToString(),
				*EntryZone->ZoneId.ToString());
			++Problems;
		}
	}

	// ── Player starts: every zone needs one tagged with its id ────────────────
	TSet<FName> TaggedZones;
	int32 StartCount = 0;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		++StartCount;
		TaggedZones.Add(It->PlayerStartTag);
	}
	for (const FValhallaZoneDef& Zone : Zones)
	{
		if (!TaggedZones.Contains(Zone.ZoneId))
		{
			UE_LOG(LogValhallaZones, Warning,
				TEXT("zone '%s' has no PlayerStart tagged '%s'; logins and deaths there fall back to the engine's pick of any PlayerStart, possibly in another zone."),
				*Zone.ZoneId.ToString(), *Zone.ZoneId.ToString());
			++Problems;
		}
	}

	UE_LOG(LogValhallaZones, Log,
		TEXT("placed actors: %d zones, %d portals, %d zone entries, %d player starts, %d NPC spawn points; %d problem(s)."),
		Zones.Num(), PortalCount, EntryCount, StartCount, GetSpawnedSpawnerCount(), Problems);

	return Problems;
}

int32 UValhallaZoneSubsystem::GetSpawnedSpawnerCount() const
{
	int32 Count = 0;
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AValhallaNPCSpawner> It(World); It; ++It)
		{
			++Count;
		}
	}
	return Count;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Console
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	/**
	 * `valhalla.CheckZones` — rediscover the zone volumes and re-run the
	 * placed-actor check, without leaving PIE. Portals, zone entries, player
	 * starts and NPC Spawn Points are all Unreal actors; this is the one place
	 * that checks they agree with each other.
	 */
	FAutoConsoleCommandWithWorld GCheckZonesCommand(
		TEXT("valhalla.CheckZones"),
		TEXT("Rediscover the zone volumes and check the placed portals, zone entries and player starts."),
		FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
		{
			if (!World)
			{
				return;
			}

			UValhallaZoneSubsystem* Zones = World->GetSubsystem<UValhallaZoneSubsystem>();
			if (!Zones)
			{
				UE_LOG(LogValhallaZones, Warning, TEXT("valhalla.CheckZones: no zone subsystem in this world."));
				return;
			}

			if (World->GetNetMode() == NM_Client)
			{
				UE_LOG(LogValhallaZones, Warning,
					TEXT("valhalla.CheckZones only works on the server; run it in the server window."));
				return;
			}

			Zones->DiscoverZones();
			Zones->CheckPlacedActors();
		}));
}
