// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaZoneSubsystem.h"

#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
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

namespace
{
	/** How far off an overlay point a level actor may be before it is a mismatch, cm. */
	constexpr double OverlayMatchToleranceCm = 128.0;

	/** Overlay coordinates are 2D; a spawner is lifted to roughly standing height. */
	constexpr double SpawnerZOffsetCm = 8.0;

	/** `type` string -> enum. The 1.0 spellings. */
	EValhallaOverlayPointType ParsePointType(const FString& Text)
	{
		if (Text.Equals(TEXT("player_spawn"), ESearchCase::IgnoreCase)) { return EValhallaOverlayPointType::PlayerSpawn; }
		if (Text.Equals(TEXT("enemy_spawn"), ESearchCase::IgnoreCase))  { return EValhallaOverlayPointType::EnemySpawn; }
		if (Text.Equals(TEXT("npc_spawn"), ESearchCase::IgnoreCase))    { return EValhallaOverlayPointType::NpcSpawn; }
		if (Text.Equals(TEXT("portal"), ESearchCase::IgnoreCase))       { return EValhallaOverlayPointType::Portal; }
		if (Text.Equals(TEXT("zone_entry"), ESearchCase::IgnoreCase))   { return EValhallaOverlayPointType::ZoneEntry; }
		return EValhallaOverlayPointType::Unknown;
	}

	const TCHAR* PointTypeName(EValhallaOverlayPointType Type)
	{
		switch (Type)
		{
		case EValhallaOverlayPointType::PlayerSpawn: return TEXT("player_spawn");
		case EValhallaOverlayPointType::EnemySpawn:  return TEXT("enemy_spawn");
		case EValhallaOverlayPointType::NpcSpawn:    return TEXT("npc_spawn");
		case EValhallaOverlayPointType::Portal:      return TEXT("portal");
		case EValhallaOverlayPointType::ZoneEntry:   return TEXT("zone_entry");
		default:                                     return TEXT("unknown");
		}
	}

	/** An optional FName field: absent and empty both give NAME_None. */
	FName ReadName(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		FString Text;
		if (Object->TryGetStringField(Field, Text) && !Text.IsEmpty())
		{
			return FName(*Text);
		}
		return NAME_None;
	}
}

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

	// Server only. Overlays create actors, and an actor a client invented is
	// an actor the server does not know about.
	if (InWorld.GetNetMode() != NM_Client)
	{
		LoadOverlays();
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
//  Overlays
// ─────────────────────────────────────────────────────────────────────────────

FString UValhallaZoneSubsystem::GetOverlayDirectory()
{
	// DataRoot is `<repo>/shared/data`; the overlays are `<repo>/maps/overlays-2.0`.
	// Going up two and across keeps the single source of truth for where the
	// 1.0 checkout is, which is the settings object, and adds no second one.
	const FString DataRoot = UValhallaDataSettings::Get()->GetResolvedDataRoot();
	const FString RepoRoot = FPaths::GetPath(FPaths::GetPath(DataRoot));
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(RepoRoot, TEXT("maps"), TEXT("overlays-2.0")));
}

FString UValhallaZoneSubsystem::GetOverlayPath(FName InZoneId)
{
	return FPaths::Combine(GetOverlayDirectory(), InZoneId.ToString() + TEXT(".json"));
}

bool UValhallaZoneSubsystem::ParseOverlay(const FString& JsonText, FValhallaZoneOverlay& OutOverlay, TArray<FString>& OutErrors)
{
	OutOverlay = FValhallaZoneOverlay();

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutErrors.Add(TEXT("not a JSON object"));
		return false;
	}

	Root->TryGetStringField(TEXT("version"), OutOverlay.Version);
	Root->TryGetStringField(TEXT("units"), OutOverlay.Units);
	OutOverlay.ZoneId = ReadName(Root, TEXT("zoneId"));

	if (OutOverlay.Version != TEXT("2.0"))
	{
		OutErrors.Add(FString::Printf(
			TEXT("version is '%s', not '2.0'; refusing the file rather than guessing its units"),
			*OutOverlay.Version));
		return false;
	}

	// Absent `units` is allowed and means cm; a *different* value is not, because
	// every coordinate in the file would be wrong by a factor nobody would spot.
	if (!OutOverlay.Units.IsEmpty() && !OutOverlay.Units.Equals(TEXT("cm"), ESearchCase::IgnoreCase))
	{
		OutErrors.Add(FString::Printf(TEXT("units is '%s', not 'cm'"), *OutOverlay.Units));
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
	if (!Root->TryGetArrayField(TEXT("spawnPoints"), Points) || !Points)
	{
		// An overlay with no points is a legitimate thing to author — it is how
		// you say "this zone has nothing in it yet" — so this is not a failure.
		OutErrors.Add(TEXT("no spawnPoints array; treating the overlay as empty"));
		return true;
	}

	int32 Index = -1;
	for (const TSharedPtr<FJsonValue>& Value : *Points)
	{
		++Index;

		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(Object) || !Object)
		{
			OutErrors.Add(FString::Printf(TEXT("spawnPoints[%d] is not an object; skipped"), Index));
			continue;
		}

		const TSharedPtr<FJsonObject>& Point = *Object;

		FValhallaOverlayPoint Parsed;
		Parsed.Id = ReadName(Point, TEXT("id"));

		FString TypeText;
		Point->TryGetStringField(TEXT("type"), TypeText);
		Parsed.Type = ParsePointType(TypeText);

		if (Parsed.Id.IsNone())
		{
			OutErrors.Add(FString::Printf(TEXT("spawnPoints[%d] has no id; skipped"), Index));
			continue;
		}

		if (Parsed.Type == EValhallaOverlayPointType::Unknown)
		{
			OutErrors.Add(FString::Printf(
				TEXT("spawnPoints[%d] ('%s') has type '%s', which is not one of the five; skipped"),
				Index, *Parsed.Id.ToString(), *TypeText));
			continue;
		}

		// A point with no coordinates would land at the zone's corner and look
		// deliberate, which is worse than not being there.
		double XValue = 0.0;
		double YValue = 0.0;
		if (!Point->TryGetNumberField(TEXT("x"), XValue) || !Point->TryGetNumberField(TEXT("y"), YValue))
		{
			OutErrors.Add(FString::Printf(
				TEXT("spawnPoints[%d] ('%s') has no numeric x/y; skipped"), Index, *Parsed.Id.ToString()));
			continue;
		}

		Parsed.X = XValue;
		Parsed.Y = YValue;

		Parsed.TemplateId = ReadName(Point, TEXT("templateId"));
		Parsed.TargetZone = ReadName(Point, TEXT("targetZone"));
		Parsed.TargetEntry = ReadName(Point, TEXT("targetEntry"));
		Parsed.FromZone = ReadName(Point, TEXT("fromZone"));
		Point->TryGetStringField(TEXT("label"), Parsed.Label);

		int32 CountValue = 0;
		if (Point->TryGetNumberField(TEXT("count"), CountValue))
		{
			Parsed.Count = FMath::Max(0, CountValue);
		}

		double RadiusValue = 0.0;
		if (Point->TryGetNumberField(TEXT("radius"), RadiusValue))
		{
			Parsed.Radius = FMath::Max(0.0, RadiusValue);
		}

		// Per-type requirements, checked here so the loader never has to.
		const bool bNeedsTemplate =
			Parsed.Type == EValhallaOverlayPointType::EnemySpawn ||
			Parsed.Type == EValhallaOverlayPointType::NpcSpawn;

		if (bNeedsTemplate && Parsed.TemplateId.IsNone())
		{
			OutErrors.Add(FString::Printf(
				TEXT("spawnPoints[%d] ('%s') is a %s with no templateId; skipped"),
				Index, *Parsed.Id.ToString(), PointTypeName(Parsed.Type)));
			continue;
		}

		if (Parsed.Type == EValhallaOverlayPointType::Portal && Parsed.TargetZone.IsNone())
		{
			OutErrors.Add(FString::Printf(
				TEXT("spawnPoints[%d] ('%s') is a portal with no targetZone; skipped"),
				Index, *Parsed.Id.ToString()));
			continue;
		}

		OutOverlay.SpawnPoints.Add(MoveTemp(Parsed));
	}

	return true;
}

void UValhallaZoneSubsystem::LoadOverlays()
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}

	const FString Directory = GetOverlayDirectory();
	int32 TotalPoints = 0;
	int32 TotalSpawners = 0;

	for (const FValhallaZoneDef& Zone : Zones)
	{
		const FString Path = GetOverlayPath(Zone.ZoneId);

		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *Path))
		{
			UE_LOG(LogValhallaZones, Warning,
				TEXT("zone '%s' has no overlay at %s; the level's own actors are all it gets."),
				*Zone.ZoneId.ToString(), *Path);
			continue;
		}

		FValhallaZoneOverlay Overlay;
		TArray<FString> Errors;
		const bool bOk = ParseOverlay(JsonText, Overlay, Errors);

		for (const FString& Error : Errors)
		{
			UE_LOG(LogValhallaZones, Warning, TEXT("overlay %s: %s"), *Zone.ZoneId.ToString(), *Error);
		}

		if (!bOk)
		{
			continue;
		}

		if (!Overlay.ZoneId.IsNone() && Overlay.ZoneId != Zone.ZoneId)
		{
			UE_LOG(LogValhallaZones, Warning,
				TEXT("overlay %s declares zoneId '%s'; using the file's name, not its contents."),
				*Path, *Overlay.ZoneId.ToString());
		}

		for (const FValhallaOverlayPoint& Point : Overlay.SpawnPoints)
		{
			++TotalPoints;

			switch (Point.Type)
			{
			case EValhallaOverlayPointType::EnemySpawn:
			case EValhallaOverlayPointType::NpcSpawn:
				SpawnFromOverlayPoint(Zone, Point);
				++TotalSpawners;
				break;
			default:
				ValidateOverlayPoint(Zone, Point);
				break;
			}
		}

		UE_LOG(LogValhallaZones, Log,
			TEXT("overlay %s: %d points loaded from %s"),
			*Zone.ZoneId.ToString(), Overlay.SpawnPoints.Num(), *Path);
	}

	bOverlaysLoaded = true;

	UE_LOG(LogValhallaZones, Log,
		TEXT("overlays-2.0 loaded from %s: %d zones, %d points, %d spawners created."),
		*Directory, Zones.Num(), TotalPoints, TotalSpawners);
}

void UValhallaZoneSubsystem::ReloadOverlays()
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}

	// The spawners go, and with them the NPCs they own: AValhallaNPCSpawner
	// spawns its group as attached-in-spirit children and a stale group left
	// behind would double every enemy count on the second reload.
	int32 Destroyed = 0;
	for (const TWeakObjectPtr<AActor>& Weak : CreatedSpawners)
	{
		if (AValhallaNPCSpawner* Spawner = Cast<AValhallaNPCSpawner>(Weak.Get()))
		{
			for (const TWeakObjectPtr<AValhallaNPC>& Npc : Spawner->GetSpawnedNPCs())
			{
				if (AActor* NpcActor = Cast<AActor>(Npc.Get()))
				{
					NpcActor->Destroy();
				}
			}
			Spawner->Destroy();
			++Destroyed;
		}
	}
	CreatedSpawners.Reset();

	UE_LOG(LogValhallaZones, Log, TEXT("valhalla.ReloadOverlays: %d spawners destroyed."), Destroyed);

	DiscoverZones();
	LoadOverlays();
}

void UValhallaZoneSubsystem::SpawnFromOverlayPoint(const FValhallaZoneDef& Zone, const FValhallaOverlayPoint& Point)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector Location = Zone.FromZoneLocal(Point.X, Point.Y, SpawnerZOffsetCm);

	if (!Zone.Contains2D(Location))
	{
		UE_LOG(LogValhallaZones, Warning,
			TEXT("overlay %s point '%s' is at zone-local (%.0f, %.0f), outside the zone box; spawned anyway."),
			*Zone.ZoneId.ToString(), *Point.Id.ToString(), Point.X, Point.Y);
	}

	// ── Deferred, and it has to be ────────────────────────────────────────
	//
	// `AValhallaNPCSpawner` reads `TemplateId` in `BeginPlay`. A plain
	// `SpawnActor` into a world that has *already* begun play dispatches
	// BeginPlay from inside the call — before this function gets the chance to
	// set anything — so the spawner wakes up with `TemplateId` still None,
	// logs "unknown NPC template 'None'; nothing spawned" and makes nothing.
	//
	// That is invisible on the initial load, because there the world has not
	// begun play yet and BeginPlay comes later; it shows up only on
	// `valhalla.ReloadOverlays`, which is exactly the path the Phase 6 editor
	// loop depends on. Deferring makes both paths the same: construct, set the
	// properties, *then* finish, and BeginPlay runs once with real data.
	const FTransform SpawnTransform(FRotator::ZeroRotator, Location);

	AValhallaNPCSpawner* Spawner = World->SpawnActorDeferred<AValhallaNPCSpawner>(
		AValhallaNPCSpawner::StaticClass(), SpawnTransform, /*Owner*/ nullptr, /*Instigator*/ nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (!Spawner)
	{
		UE_LOG(LogValhallaZones, Warning,
			TEXT("overlay %s point '%s': spawner would not spawn."),
			*Zone.ZoneId.ToString(), *Point.Id.ToString());
		return;
	}

	Spawner->TemplateId = Point.TemplateId;
	Spawner->Count = Point.Count;
	Spawner->SpawnRadius = static_cast<float>(Point.Radius);
	Spawner->DebugLabel = Point.Label.IsEmpty() ? Point.Id.ToString() : Point.Label;
#if WITH_EDITOR
	Spawner->SetActorLabel(FString::Printf(TEXT("Overlay_%s_%s"), *Zone.ZoneId.ToString(), *Point.Id.ToString()));
#endif

	Spawner->FinishSpawning(SpawnTransform);

	CreatedSpawners.Add(Spawner);

	UE_LOG(LogValhallaZones, Log,
		TEXT("overlay %s: spawner '%s' template '%s' x%d r%.0f at (%.0f, %.0f) [zone-local %.0f, %.0f]"),
		*Zone.ZoneId.ToString(), *Spawner->DebugLabel, *Point.TemplateId.ToString(),
		Point.Count, Point.Radius, Location.X, Location.Y, Point.X, Point.Y);
}

void UValhallaZoneSubsystem::ValidateOverlayPoint(const FValhallaZoneDef& Zone, const FValhallaOverlayPoint& Point)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector Expected = Zone.FromZoneLocal(Point.X, Point.Y, 0.0);

	// A small helper rather than three copies of the same distance test.
	const auto Report = [&Zone, &Point, &Expected](const AActor* Found, const TCHAR* What)
	{
		if (!Found)
		{
			UE_LOG(LogValhallaZones, Warning,
				TEXT("overlay %s: %s '%s' at zone-local (%.0f, %.0f) has no matching %s actor in the level."),
				*Zone.ZoneId.ToString(), PointTypeName(Point.Type), *Point.Id.ToString(), Point.X, Point.Y, What);
			return;
		}

		const FVector Actual = Found->GetActorLocation();
		const double Distance = FVector2D(Actual.X - Expected.X, Actual.Y - Expected.Y).Size();
		if (Distance > OverlayMatchToleranceCm)
		{
			UE_LOG(LogValhallaZones, Warning,
				TEXT("overlay %s: %s '%s' expects (%.0f, %.0f) but %s is at (%.0f, %.0f) — %.0f cm apart."),
				*Zone.ZoneId.ToString(), PointTypeName(Point.Type), *Point.Id.ToString(),
				Expected.X, Expected.Y, *Found->GetName(), Actual.X, Actual.Y, Distance);
		}
		else
		{
			UE_LOG(LogValhallaZones, Verbose,
				TEXT("overlay %s: %s '%s' matches %s (%.0f cm)."),
				*Zone.ZoneId.ToString(), PointTypeName(Point.Type), *Point.Id.ToString(), *Found->GetName(), Distance);
		}
	};

	switch (Point.Type)
	{
	case EValhallaOverlayPointType::Portal:
	{
		const AValhallaPortal* Best = nullptr;
		double BestDistance = TNumericLimits<double>::Max();
		for (TActorIterator<AValhallaPortal> It(World); It; ++It)
		{
			if (It->TargetZoneId != Point.TargetZone)
			{
				continue;
			}
			const FVector Location = It->GetActorLocation();
			const double Distance = FVector2D(Location.X - Expected.X, Location.Y - Expected.Y).Size();
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = *It;
			}
		}
		Report(Best, TEXT("AValhallaPortal"));

		if (Best && !Point.TargetEntry.IsNone() && Best->TargetEntryId != Point.TargetEntry)
		{
			UE_LOG(LogValhallaZones, Warning,
				TEXT("overlay %s: portal '%s' says targetEntry '%s' but %s is set to '%s'."),
				*Zone.ZoneId.ToString(), *Point.Id.ToString(), *Point.TargetEntry.ToString(),
				*Best->GetName(), *Best->TargetEntryId.ToString());
		}
		break;
	}

	case EValhallaOverlayPointType::ZoneEntry:
	{
		const AValhallaZoneEntry* Entry = AValhallaZoneEntry::Find(World, Point.Id);
		Report(Entry, TEXT("AValhallaZoneEntry"));

		if (Entry && !Point.FromZone.IsNone() && Entry->FromZoneId != Point.FromZone)
		{
			UE_LOG(LogValhallaZones, Warning,
				TEXT("overlay %s: zone_entry '%s' says fromZone '%s' but %s is set to '%s'."),
				*Zone.ZoneId.ToString(), *Point.Id.ToString(), *Point.FromZone.ToString(),
				*Entry->GetName(), *Entry->FromZoneId.ToString());
		}
		break;
	}

	case EValhallaOverlayPointType::PlayerSpawn:
	{
		const APlayerStart* Best = nullptr;
		double BestDistance = TNumericLimits<double>::Max();
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			const FVector Location = It->GetActorLocation();
			const double Distance = FVector2D(Location.X - Expected.X, Location.Y - Expected.Y).Size();
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = *It;
			}
		}
		Report(Best, TEXT("APlayerStart"));
		break;
	}

	default:
		break;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Console
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	/**
	 * `valhalla.ReloadOverlays` — re-read `maps/overlays-2.0/*.json` and rebuild
	 * the spawners from them, without leaving PIE.
	 *
	 * This is the Phase 3 half of the Phase 6 editor loop: change an enemy
	 * group in the 1.0 web editor, run this, see the new group. It is also how
	 * the gate proves that enemy placement really is data — a level rebuild
	 * would prove nothing, because the level builder writes the overlay too.
	 */
	FAutoConsoleCommandWithWorld GReloadOverlaysCommand(
		TEXT("valhalla.ReloadOverlays"),
		TEXT("Re-read maps/overlays-2.0/*.json, destroy the spawners it made last time and make them again."),
		FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
		{
			if (!World)
			{
				return;
			}

			UValhallaZoneSubsystem* Zones = World->GetSubsystem<UValhallaZoneSubsystem>();
			if (!Zones)
			{
				UE_LOG(LogValhallaZones, Warning, TEXT("valhalla.ReloadOverlays: no zone subsystem in this world."));
				return;
			}

			if (World->GetNetMode() == NM_Client)
			{
				UE_LOG(LogValhallaZones, Warning,
					TEXT("valhalla.ReloadOverlays only works on the server; run it in the server window."));
				return;
			}

			Zones->ReloadOverlays();
		}));
}
