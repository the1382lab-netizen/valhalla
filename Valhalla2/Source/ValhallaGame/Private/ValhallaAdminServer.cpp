// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaAdminServer.h"
#include "ValhallaBackendSubsystem.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameInstance.h"
#include "GameFramework/GameStateBase.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ValhallaDataSettings.h"
#include "ValhallaDataSubsystem.h"
#include "ValhallaGame.h"
#include "ValhallaGameMode.h"
#include "ValhallaGameState.h"
#include "ValhallaInventoryTypes.h"
#include "ValhallaLootBag.h"
#include "ValhallaNPC.h"
#include "ValhallaNPCSpawner.h"
#include "ValhallaPlayerState.h"
#include "ValhallaTypes.h"
#include "ValhallaZoneSubsystem.h"
#include "ValhallaCharacter.h"
#include "ValhallaCombatLibrary.h"
#include "ValhallaConstants.h"
#include "ValhallaInventoryLibrary.h"
#include "ValhallaPlayerController.h"
#include "ValhallaSkillComponent.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Engine/Level.h"
#include "TimerManager.h"

#if WITH_VALHALLA_ADMIN_API
#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "IHttpRouter.h"
#endif

DEFINE_LOG_CATEGORY(LogValhallaAdmin);

namespace
{
	/** The path every route hangs off. `admin.ts` is mounted at the same one. */
	const TCHAR* AdminRoot = TEXT("/api/admin");

	/** 1.0 filed an entity with no zone under this. */
	const TCHAR* UnknownZone = TEXT("unknown");

	/** What `Math.round` did to every coordinate and vital in `admin.ts`. */
	double Round(double Value)
	{
		return FMath::RoundToDouble(Value);
	}

	/**
	 * A world-unique id for an actor, for the admin API.
	 *
	 * `GetName()` is only unique within one level, and since NPC Spawn Points
	 * (and the NPCs they spawn) live in each zone's gameplay sublevel, both
	 * `L_Grasslands_Gameplay` and `L_Desert_Gameplay` have a
	 * `ValhallaNPCSpawner_0`. An actor in the persistent level keeps its bare
	 * name; anything else is prefixed with its level's short package name
	 * (PIE's `UEDPIE_n_` prefix stripped), e.g. `L_Desert_Gameplay.ValhallaNPCSpawner_0`.
	 */
	FString AdminActorId(const AActor* Actor)
	{
		if (!Actor)
		{
			return FString();
		}
		const ULevel* Level = Actor->GetLevel();
		const UWorld* World = Actor->GetWorld();
		if (!Level || !World || Level == World->PersistentLevel)
		{
			return Actor->GetName();
		}
		FString LevelName = FPackageName::GetShortName(Level->GetOutermost()->GetName());
		if (LevelName.StartsWith(TEXT("UEDPIE_")))
		{
			int32 Underscore = INDEX_NONE;
			const FString Rest = LevelName.Mid(7);
			if (Rest.FindChar(TEXT('_'), Underscore))
			{
				LevelName = Rest.Mid(Underscore + 1);
			}
		}
		return LevelName + TEXT(".") + Actor->GetName();
	}

	/** `NPCTemplate.type` as the 1.0 JSON spelled it. */
	FString NpcTypeString(EValhallaNPCType Type)
	{
		return Type == EValhallaNPCType::Npc ? TEXT("npc") : TEXT("enemy");
	}

#if WITH_VALHALLA_ADMIN_API

	/** An FJsonObject as a compact UTF-8 HTTP body. */
	FString JsonToString(const TSharedRef<FJsonObject>& Json)
	{
		FString Text;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
		FJsonSerializer::Serialize(Json, Writer);
		return Text;
	}

	/**
	 * A JSON response with the CORS header on it.
	 *
	 * `Access-Control-Allow-Origin: *` even though the editor proxies through
	 * its own Express server and never sees this origin: the header costs one
	 * line and makes the API usable from a browser tab, a `curl`, or a future
	 * editor that drops the proxy. It is not a security decision — a loopback
	 * listener is reachable by anything already on the machine, header or not.
	 */
	TUniquePtr<FHttpServerResponse> MakeJsonResponse(const TSharedRef<FJsonObject>& Json, EHttpServerResponseCodes Code)
	{
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(JsonToString(Json), TEXT("application/json"));
		Response->Code = Code;
		Response->Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("*") });
		Response->Headers.Add(TEXT("Access-Control-Allow-Headers"), { TEXT("Content-Type, Authorization") });
		Response->Headers.Add(TEXT("Access-Control-Allow-Methods"), { TEXT("GET, POST, OPTIONS") });
		return Response;
	}

	/** `{ ok: true }` — the body every mutating route returns on success. */
	TUniquePtr<FHttpServerResponse> MakeOkResponse()
	{
		const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetBoolField(TEXT("ok"), true);
		return MakeJsonResponse(Json, EHttpServerResponseCodes::Ok);
	}

	/**
	 * `{ error: "..." }` with a 4xx/5xx, which is exactly what `admin.ts`
	 * returned and what the dashboard's `postAdmin` surfaces.
	 */
	TUniquePtr<FHttpServerResponse> MakeErrorResponse(EHttpServerResponseCodes Code, const FString& Message)
	{
		const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("error"), Message);
		return MakeJsonResponse(Json, Code);
	}

	/** The request body as a JSON object, or an invalid pointer. */
	TSharedPtr<FJsonObject> ParseBody(const FHttpServerRequest& Request)
	{
		if (Request.Body.Num() == 0)
		{
			return nullptr;
		}

		// Null-terminated first: FHttpServerRequest::Body is a byte array with no
		// terminator, and every UTF-8 -> FString path in the engine that does not
		// want a length wants one.
		TArray<uint8> Terminated(Request.Body);
		Terminated.Add(0);
		const FString Text(UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(Terminated.GetData())));

		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
		{
			return nullptr;
		}
		return Object;
	}

	/** A required string field, with 1.0's "missing and empty are the same" rule. */
	bool GetRequiredString(const TSharedPtr<FJsonObject>& Body, const TCHAR* Field, FString& Out)
	{
		return Body.IsValid() && Body->TryGetStringField(Field, Out) && !Out.IsEmpty();
	}

	/** A required number field. `x == null` in 1.0 is "absent or null" here. */
	bool GetRequiredNumber(const TSharedPtr<FJsonObject>& Body, const TCHAR* Field, double& Out)
	{
		return Body.IsValid() && Body->TryGetNumberField(Field, Out);
	}

	/** True when a request is a CORS preflight rather than a real call. */
	bool IsPreflight(const FHttpServerRequest& Request)
	{
		return Request.Verb == EHttpServerRequestVerbs::VERB_OPTIONS;
	}

#endif // WITH_VALHALLA_ADMIN_API
}

// ─────────────────────────────────────────────────────────────────────────────
//  Lifetime
// ─────────────────────────────────────────────────────────────────────────────

TWeakObjectPtr<UValhallaAdminServer> UValhallaAdminServer::ActiveInstance;

UValhallaAdminServer::UValhallaAdminServer() = default;
UValhallaAdminServer::~UValhallaAdminServer() = default;

bool UValhallaAdminServer::ShouldCreateSubsystem(UObject* Outer) const
{
	// Game and PIE worlds only. An editor preview world, a thumbnail world and
	// an asset editor's world all get world subsystems too, and none of them
	// has a game mode to start this.
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->IsGameWorld());
}

void UValhallaAdminServer::Deinitialize()
{
	Stop();
	Super::Deinitialize();
}

int32 UValhallaAdminServer::GetConfiguredPort()
{
	const UValhallaDataSettings* Settings = UValhallaDataSettings::Get();
	return Settings ? Settings->AdminApiPort : 2568;
}

void UValhallaAdminServer::Start()
{
#if !WITH_VALHALLA_ADMIN_API
	UE_LOG(LogValhallaAdmin, Log, TEXT("admin API is compiled out of this target; not starting."));
#else
	if (bRunning)
	{
		return;
	}

	const UValhallaDataSettings* Settings = UValhallaDataSettings::Get();
	if (Settings && !Settings->bEnableAdminApi)
	{
		UE_LOG(LogValhallaAdmin, Log, TEXT("admin API disabled by ValhallaDataSettings.bEnableAdminApi."));
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// One admin API per process — see ActiveInstance. Log, not Warning: a
	// second world asking is normal (a kicked client makes one), and it is not
	// a problem, it is just not this world's job.
	if (ActiveInstance.IsValid() && ActiveInstance.Get() != this)
	{
		UE_LOG(LogValhallaAdmin, Log,
			TEXT("world '%s': the admin API is already served by another world in this process; not starting a second."),
			*World->GetName());
		return;
	}

	// The runtime half of the server-only rule. `IsRunningDedicatedServer()`
	// covers the packaged Server target; `GetAuthGameMode() != nullptr` covers
	// the editor's listen server and PIE's dedicated-server world, and excludes
	// the client worlds that sit beside it in the same process — which is the
	// case that actually arises, because "Play as dedicated server with two
	// clients" makes three worlds and three subsystems.
	const bool bHasAuthority = IsRunningDedicatedServer() || World->GetAuthGameMode() != nullptr;
	if (!bHasAuthority)
	{
		UE_LOG(LogValhallaAdmin, Verbose, TEXT("world '%s' has no authority; the admin API belongs to the server world."),
			*World->GetName());
		return;
	}

	BoundPort = GetConfiguredPort();

	FHttpServerModule& HttpServerModule = FHttpServerModule::Get();

	// bFailOnBindFailure: without it a port already in use returns a perfectly
	// good router that never receives anything, and the symptom is a dashboard
	// that says "unreachable" while the log says the routes bound fine.
	Router = HttpServerModule.GetHttpRouter(static_cast<uint32>(BoundPort), /*bFailOnBindFailure=*/true);
	if (!Router.IsValid())
	{
		UE_LOG(LogValhallaAdmin, Error,
			TEXT("could not bind port %d. Another process (a 1.0 server, or a previous editor) is probably holding it."),
			BoundPort);
		BoundPort = 0;
		return;
	}

	constexpr uint16 Get = static_cast<uint16>(EHttpServerRequestVerbs::VERB_GET) | static_cast<uint16>(EHttpServerRequestVerbs::VERB_OPTIONS);
	constexpr uint16 Post = static_cast<uint16>(EHttpServerRequestVerbs::VERB_POST) | static_cast<uint16>(EHttpServerRequestVerbs::VERB_OPTIONS);

	// The first route is also the probe: if `/api/admin/state` is already bound
	// on this port, something else in this process owns the API and the right
	// thing to do is nothing at all — not to bind the other nine and serve half
	// a contract.
	if (!BindRoute(TEXT("/state"), Get, &UValhallaAdminServer::HandleState))
	{
		UE_LOG(LogValhallaAdmin, Log,
			TEXT("port %d already has /api/admin/state bound; not starting a second admin API."), BoundPort);
		Router.Reset();
		BoundPort = 0;
		return;
	}

	// ── The 1.0 contract, route for route ────────────────────────────────
	BindRoute(TEXT("/spawn-npc"),        Post, &UValhallaAdminServer::HandleSpawnNpc);
	BindRoute(TEXT("/drop-item"),        Post, &UValhallaAdminServer::HandleDropItem);
	BindRoute(TEXT("/kick-player"),      Post, &UValhallaAdminServer::HandleKickPlayer);
	BindRoute(TEXT("/teleport-player"),  Post, &UValhallaAdminServer::HandleTeleportPlayer);
	BindRoute(TEXT("/kill-npc"),         Post, &UValhallaAdminServer::HandleKillNpc);
	BindRoute(TEXT("/respawn-npc"),      Post, &UValhallaAdminServer::HandleRespawnNpc);
	BindRoute(TEXT("/delete-npc"),       Post, &UValhallaAdminServer::HandleDeleteNpc);

	// ── 2.0 only: the two the editor loop needs ──────────────────────────
	BindRoute(TEXT("/reload-overlays"),  Post, &UValhallaAdminServer::HandleReloadOverlays);
	BindRoute(TEXT("/reload-data"),      Post, &UValhallaAdminServer::HandleReloadData);

	// ── 2.0 only: MMO admin actions ───────────────────────────────────────
	BindRoute(TEXT("/player-action"),      Post, &UValhallaAdminServer::HandlePlayerAction);
	BindRoute(TEXT("/player-inspect"),     Post, &UValhallaAdminServer::HandlePlayerInspect);
	BindRoute(TEXT("/broadcast"),          Post, &UValhallaAdminServer::HandleBroadcast);
	BindRoute(TEXT("/spawn-point-action"), Post, &UValhallaAdminServer::HandleSpawnPointAction);
	BindRoute(TEXT("/account-action"),     Post, &UValhallaAdminServer::HandleAccountAction);

	HttpServerModule.StartAllListeners();

	bRunning = true;
	ActiveInstance = this;

	UE_LOG(LogValhallaAdmin, Log,
		TEXT("admin API listening on http://127.0.0.1:%d%s — %d routes. The web editor's proxy reaches it through VALHALLA_ADMIN_URL."),
		BoundPort, AdminRoot, RouteHandles.Num());
#endif // WITH_VALHALLA_ADMIN_API
}

void UValhallaAdminServer::Stop()
{
#if WITH_VALHALLA_ADMIN_API
	if (!bRunning)
	{
		return;
	}

	if (Router.IsValid())
	{
		for (const TSharedPtr<const FHttpRouteHandleInternal>& Handle : RouteHandles)
		{
			if (Handle.IsValid())
			{
				Router->UnbindRoute(Handle);
			}
		}
	}
	RouteHandles.Reset();
	Router.Reset();

	// Note what is *not* done: StopAllListeners(). The listener is per-port and
	// shared with anything else in the process that asked for a router, and the
	// engine tears it down when the last route goes. Stopping every listener
	// because one game mode ended would take the Remote Control API and the web
	// profiler down with it.

	UE_LOG(LogValhallaAdmin, Log, TEXT("admin API on port %d stopped."), BoundPort);

	if (ActiveInstance.Get() == this)
	{
		ActiveInstance.Reset();
	}

	bRunning = false;
	BoundPort = 0;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Coordinates — the one conversion, in both directions
// ─────────────────────────────────────────────────────────────────────────────

FVector2D UValhallaAdminServer::ToZoneLocalCm(const FValhallaZoneDef& Zone, const FVector& WorldLocation)
{
	return Zone.ToZoneLocal(WorldLocation);
}

FVector UValhallaAdminServer::FromZoneLocalCm(const FValhallaZoneDef& Zone, double LocalX, double LocalY, double ZOffsetCm)
{
	return Zone.FromZoneLocal(LocalX, LocalY, ZOffsetCm);
}

// ─────────────────────────────────────────────────────────────────────────────
//  The snapshot
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaAdminServer::BuildSnapshot(FValhallaAdminSnapshot& OutSnapshot) const
{
	OutSnapshot = FValhallaAdminSnapshot();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const UValhallaZoneSubsystem* ZoneSubsystem = World->GetSubsystem<UValhallaZoneSubsystem>();
	if (!ZoneSubsystem)
	{
		return;
	}

	OutSnapshot.bOnline = World->GetAuthGameMode() != nullptr || IsRunningDedicatedServer();

	const TArray<FValhallaZoneDef>& Zones = ZoneSubsystem->GetZones();

	// Every discovered zone gets an entry up front — see the header comment on
	// why this is one deliberate difference from `admin.ts`.
	for (const FValhallaZoneDef& Zone : Zones)
	{
		OutSnapshot.Zones.FindOrAdd(Zone.ZoneId.ToString());
	}

	/** The zone a world point is in, as a key into OutSnapshot.Zones. */
	auto ZoneKeyAt = [ZoneSubsystem](const FVector& Location) -> FString
	{
		const FValhallaZoneDef* Zone = ZoneSubsystem->GetZoneAt(Location);
		return Zone ? Zone->ZoneId.ToString() : FString(UnknownZone);
	};

	/** World -> zone-local cm for the zone named by a key, or raw XY if unknown. */
	auto LocalFor = [ZoneSubsystem](FName ZoneId, const FVector& Location) -> FVector2D
	{
		if (const FValhallaZoneDef* Zone = ZoneSubsystem->FindZone(ZoneId))
		{
			return UValhallaAdminServer::ToZoneLocalCm(*Zone, Location);
		}
		return FVector2D(Location.X, Location.Y);
	};

	// ── Players ──────────────────────────────────────────────────────────
	//
	// Off the game state's PlayerArray rather than an actor iterator: a player
	// state is the thing that survives a pawn's death, and a dead player must
	// still appear in the dashboard (greyed out) exactly as 1.0's did.
	if (const AGameStateBase* GameState = World->GetGameState())
	{
		for (APlayerState* Entry : GameState->PlayerArray)
		{
			const AValhallaPlayerState* PS = Cast<AValhallaPlayerState>(Entry);
			if (!PS)
			{
				continue;
			}

			FValhallaAdminPlayerInfo Info;
			Info.SessionId = FString::FromInt(PS->GetPlayerId());
			Info.Name      = PS->CharacterName;
			Info.ClassId   = PS->ClassId.ToString();
			Info.Level     = PS->Level;
			Info.Hp        = PS->Hp;
			Info.MaxHp     = PS->MaxHp;
			Info.MaxMana   = PS->MaxMana;
			Info.bAlive    = PS->bAlive;
			Info.bGodMode  = PS->bAdminGodMode;
			Info.bFrozen   = PS->bAdminFrozen;
			if (const AValhallaGameMode* GameMode = World->GetAuthGameMode<AValhallaGameMode>())
			{
				if (const FValhallaBackendSession* Session = GameMode->FindBackendSession(Cast<APlayerController>(PS->GetOwner())))
				{
					Info.Account = Session->Username;
					Info.UserId  = Session->UserId;
				}
			}
			if (const AValhallaGameState* GS = World->GetGameState<AValhallaGameState>())
			{
				Info.MutedSeconds = FMath::Max(0.0, PS->AdminMutedUntil - GS->GetServerTime());
			}

			// 1.0's `mana` was one pool; 2.0 splits mana and energy by class.
			// The dashboard draws one blue bar, so it gets whichever pool the
			// class actually uses — a rogue showing 0/0 mana would read as a
			// bug rather than as "rogues use energy".
			if (PS->MaxMana > 0.f)
			{
				Info.Mana    = PS->Mana;
				Info.MaxMana = PS->MaxMana;
			}
			else
			{
				Info.Mana    = PS->Energy;
				Info.MaxMana = PS->MaxEnergy;
			}

			// Position comes from the pawn, because Phase 2 deliberately left it
			// off the player state (see AValhallaPlayerState's class comment).
			// A player with no pawn — dead, or mid-respawn — reports (0, 0),
			// which is what 1.0 did with a player whose x/y were never set.
			FString ZoneKey = PS->ZoneId.IsNone() ? FString(UnknownZone) : PS->ZoneId.ToString();
			if (const APawn* Pawn = PS->GetPawn())
			{
				const FVector Location = Pawn->GetActorLocation();
				// The pawn's own zone wins over the replicated id: the id is
				// updated once per fixed tick and the pawn has already moved.
				ZoneKey = ZoneKeyAt(Location);
				const FVector2D Local = LocalFor(FName(*ZoneKey), Location);
				Info.X = Local.X;
				Info.Y = Local.Y;
			}

			OutSnapshot.Zones.FindOrAdd(ZoneKey).Players.Add(MoveTemp(Info));
		}
	}

	// ── NPCs ─────────────────────────────────────────────────────────────
	for (TActorIterator<AValhallaNPC> It(World); It; ++It)
	{
		const AValhallaNPC* Npc = *It;
		const FVector Location = Npc->GetActorLocation();
		const FString ZoneKey = ZoneKeyAt(Location);
		const FVector2D Local = LocalFor(FName(*ZoneKey), Location);

		FValhallaAdminNpcInfo Info;
		Info.Id         = AdminActorId(Npc);
		Info.TemplateId = Npc->TemplateId.ToString();
		Info.Name       = Npc->DisplayName;
		Info.NpcType    = NpcTypeString(Npc->GetTemplate().Type);
		Info.Level      = Npc->Level;
		Info.X          = Local.X;
		Info.Y          = Local.Y;
		Info.Hp         = Npc->Hp;
		Info.MaxHp      = Npc->MaxHp;
		Info.bAlive     = Npc->IsAlive();

		OutSnapshot.Zones.FindOrAdd(ZoneKey).Npcs.Add(MoveTemp(Info));
	}

	// ── NPC Spawn Points ─────────────────────────────────────────────────
	for (TActorIterator<AValhallaNPCSpawner> It(World); It; ++It)
	{
		const AValhallaNPCSpawner* Spawner = *It;
		const FVector Location = Spawner->GetActorLocation();
		const FString ZoneKey = ZoneKeyAt(Location);
		const FVector2D Local = LocalFor(FName(*ZoneKey), Location);

		FValhallaAdminSpawnPointInfo Info;
		Info.Id = AdminActorId(Spawner);
#if WITH_EDITOR
		Info.Label = Spawner->GetActorLabel();
#endif
		if (Info.Label.IsEmpty())
		{
			Info.Label = Spawner->DebugLabel.IsEmpty() ? Info.Id : Spawner->DebugLabel;
		}
		FString ClassName = Spawner->NPCClass ? Spawner->NPCClass->GetName() : TEXT("ValhallaNPC");
		ClassName.RemoveFromEnd(TEXT("_C"));
		Info.NpcClass = ClassName;
		Info.TemplateId = Spawner->GetEffectiveTemplateId().ToString();
		Info.X = Local.X;
		Info.Y = Local.Y;
		Info.RespawnSeconds = Spawner->GetEffectiveRespawnSeconds();
		Info.bRespawnOverride = Spawner->RespawnSeconds > 0.f;
		Info.SecondsUntilRespawn = Spawner->GetSecondsUntilRespawn();
		if (const AValhallaNPC* Npc = Spawner->GetSpawnedNPC())
		{
			Info.NpcId = AdminActorId(Npc);
			Info.bNpcAlive = Npc->IsAlive();
		}

		OutSnapshot.Zones.FindOrAdd(ZoneKey).SpawnPoints.Add(MoveTemp(Info));
	}

	// ── Loot bags ────────────────────────────────────────────────────────
	for (TActorIterator<AValhallaLootBag> It(World); It; ++It)
	{
		const AValhallaLootBag* Bag = *It;
		const FVector Location = Bag->GetActorLocation();
		const FString ZoneKey = ZoneKeyAt(Location);
		const FVector2D Local = LocalFor(FName(*ZoneKey), Location);

		FValhallaAdminLootBagInfo Info;
		Info.Id = AdminActorId(Bag);
		Info.X  = Local.X;
		Info.Y  = Local.Y;

		for (const FValhallaBagSlot& Slot : Bag->Items)
		{
			// 1.0 skipped slots with a falsy itemId; 2.0's array is dense, but
			// the guard costs nothing and keeps the shapes identical.
			if (Slot.ItemId.IsNone())
			{
				continue;
			}
			FValhallaAdminItemStack Stack;
			Stack.ItemId = Slot.ItemId.ToString();
			Stack.Quantity = Slot.Quantity;
			Info.Items.Add(MoveTemp(Stack));
		}

		OutSnapshot.Zones.FindOrAdd(ZoneKey).LootBags.Add(MoveTemp(Info));
	}
}

TSharedRef<FJsonObject> UValhallaAdminServer::BuildStateJson(const FValhallaAdminSnapshot& Snapshot)
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	const TSharedRef<FJsonObject> ZonesJson = MakeShared<FJsonObject>();

	int32 TotalPlayers = 0;
	int32 TotalNpcs = 0;
	int32 TotalLootBags = 0;

	// Sorted so two consecutive polls produce byte-identical documents for an
	// unchanged world. The dashboard does not care; a human diffing two curl
	// outputs to find what moved does.
	TArray<FString> ZoneKeys;
	Snapshot.Zones.GetKeys(ZoneKeys);
	ZoneKeys.Sort();

	for (const FString& ZoneKey : ZoneKeys)
	{
		const FValhallaAdminZoneSnapshot& Zone = Snapshot.Zones[ZoneKey];
		const TSharedRef<FJsonObject> ZoneJson = MakeShared<FJsonObject>();

		TArray<TSharedPtr<FJsonValue>> PlayersJson;
		for (const FValhallaAdminPlayerInfo& Player : Zone.Players)
		{
			const TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("sessionId"), Player.SessionId);
			Obj->SetStringField(TEXT("name"),      Player.Name);
			Obj->SetStringField(TEXT("classId"),   Player.ClassId);
			Obj->SetNumberField(TEXT("level"),     Player.Level);
			Obj->SetNumberField(TEXT("x"),         Round(Player.X));
			Obj->SetNumberField(TEXT("y"),         Round(Player.Y));
			Obj->SetNumberField(TEXT("hp"),        Round(Player.Hp));
			Obj->SetNumberField(TEXT("maxHp"),     Round(Player.MaxHp));
			Obj->SetNumberField(TEXT("mana"),      Round(Player.Mana));
			Obj->SetNumberField(TEXT("maxMana"),   Round(Player.MaxMana));
			Obj->SetBoolField  (TEXT("alive"),     Player.bAlive);
			Obj->SetStringField(TEXT("zoneId"),    ZoneKey);
			Obj->SetBoolField  (TEXT("godMode"),   Player.bGodMode);
			Obj->SetBoolField  (TEXT("frozen"),    Player.bFrozen);
			Obj->SetNumberField(TEXT("mutedSeconds"), Round(Player.MutedSeconds));
			Obj->SetStringField(TEXT("account"),   Player.Account);
			Obj->SetNumberField(TEXT("userId"),    Player.UserId);
			PlayersJson.Add(MakeShared<FJsonValueObject>(Obj));
		}

		TArray<TSharedPtr<FJsonValue>> NpcsJson;
		for (const FValhallaAdminNpcInfo& Npc : Zone.Npcs)
		{
			const TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("id"),         Npc.Id);
			Obj->SetStringField(TEXT("templateId"), Npc.TemplateId);
			Obj->SetStringField(TEXT("name"),       Npc.Name);
			Obj->SetStringField(TEXT("npcType"),    Npc.NpcType);
			Obj->SetNumberField(TEXT("level"),      Npc.Level);
			Obj->SetNumberField(TEXT("x"),          Round(Npc.X));
			Obj->SetNumberField(TEXT("y"),          Round(Npc.Y));
			Obj->SetNumberField(TEXT("hp"),         Round(Npc.Hp));
			Obj->SetNumberField(TEXT("maxHp"),      Round(Npc.MaxHp));
			Obj->SetBoolField  (TEXT("alive"),      Npc.bAlive);
			NpcsJson.Add(MakeShared<FJsonValueObject>(Obj));
		}

		TArray<TSharedPtr<FJsonValue>> BagsJson;
		for (const FValhallaAdminLootBagInfo& Bag : Zone.LootBags)
		{
			const TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("id"),        Bag.Id);
			Obj->SetNumberField(TEXT("x"),         Round(Bag.X));
			Obj->SetNumberField(TEXT("y"),         Round(Bag.Y));
			Obj->SetNumberField(TEXT("itemCount"), Bag.Items.Num());

			TArray<TSharedPtr<FJsonValue>> ItemsJson;
			for (const FValhallaAdminItemStack& Stack : Bag.Items)
			{
				const TSharedRef<FJsonObject> ItemObj = MakeShared<FJsonObject>();
				ItemObj->SetStringField(TEXT("itemId"),   Stack.ItemId);
				ItemObj->SetNumberField(TEXT("quantity"), Stack.Quantity);
				ItemsJson.Add(MakeShared<FJsonValueObject>(ItemObj));
			}
			Obj->SetArrayField(TEXT("items"), ItemsJson);

			BagsJson.Add(MakeShared<FJsonValueObject>(Obj));
		}

		TArray<TSharedPtr<FJsonValue>> SpawnPointsJson;
		for (const FValhallaAdminSpawnPointInfo& Point : Zone.SpawnPoints)
		{
			const TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("id"),              Point.Id);
			Obj->SetStringField(TEXT("label"),           Point.Label);
			Obj->SetStringField(TEXT("npcClass"),        Point.NpcClass);
			Obj->SetStringField(TEXT("templateId"),      Point.TemplateId);
			Obj->SetNumberField(TEXT("x"),               Round(Point.X));
			Obj->SetNumberField(TEXT("y"),               Round(Point.Y));
			Obj->SetNumberField(TEXT("respawnSeconds"),  Point.RespawnSeconds);
			Obj->SetBoolField  (TEXT("respawnOverride"), Point.bRespawnOverride);
			Obj->SetNumberField(TEXT("respawnIn"),       FMath::RoundToDouble(Point.SecondsUntilRespawn * 10.0) / 10.0);
			Obj->SetStringField(TEXT("npcId"),           Point.NpcId);
			Obj->SetBoolField  (TEXT("npcAlive"),        Point.bNpcAlive);
			SpawnPointsJson.Add(MakeShared<FJsonValueObject>(Obj));
		}
		ZoneJson->SetArrayField(TEXT("spawnPoints"), SpawnPointsJson);

		TotalPlayers  += Zone.Players.Num();
		TotalNpcs     += Zone.Npcs.Num();
		TotalLootBags += Zone.LootBags.Num();

		ZoneJson->SetArrayField(TEXT("players"),  PlayersJson);
		ZoneJson->SetArrayField(TEXT("npcs"),     NpcsJson);
		ZoneJson->SetArrayField(TEXT("lootBags"), BagsJson);

		ZonesJson->SetObjectField(ZoneKey, ZoneJson);
	}

	Root->SetBoolField(TEXT("online"), Snapshot.bOnline);
	Root->SetObjectField(TEXT("zones"), ZonesJson);
	Root->SetNumberField(TEXT("totalPlayers"),  TotalPlayers);
	Root->SetNumberField(TEXT("totalNpcs"),     TotalNpcs);
	Root->SetNumberField(TEXT("totalLootBags"), TotalLootBags);

	return Root;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Routing
// ─────────────────────────────────────────────────────────────────────────────

#if WITH_VALHALLA_ADMIN_API

bool UValhallaAdminServer::BindRoute(const FString& Path, uint16 Verbs, bool (UValhallaAdminServer::*Handler)(const FHttpServerRequest&, const FHttpResultCallback&))
{
	if (!Router.IsValid())
	{
		return false;
	}

	const FString FullPath = FString(AdminRoot) + Path;

	// A weak lambda rather than CreateUObject: the route outlives nothing in
	// practice (Stop unbinds it), but a request already in flight when a PIE
	// session tears down would otherwise call through a stale `this`.
	TWeakObjectPtr<UValhallaAdminServer> WeakThis(this);
	FHttpRequestHandler Delegate = FHttpRequestHandler::CreateLambda(
		[WeakThis, Handler, FullPath](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) -> bool
		{
			UValhallaAdminServer* Self = WeakThis.Get();
			if (!Self)
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServiceUnavail, TEXT("Game server not running")));
				return true;
			}

			UE_LOG(LogValhallaAdmin, Verbose, TEXT("%s %s (%d body bytes)"),
				Request.Verb == EHttpServerRequestVerbs::VERB_GET ? TEXT("GET") :
				Request.Verb == EHttpServerRequestVerbs::VERB_POST ? TEXT("POST") : TEXT("OPTIONS"),
				*FullPath, Request.Body.Num());

			if (IsPreflight(Request))
			{
				const TSharedRef<FJsonObject> Empty = MakeShared<FJsonObject>();
				OnComplete(MakeJsonResponse(Empty, EHttpServerResponseCodes::NoContent));
				return true;
			}

			TUniquePtr<FHttpServerResponse> Denied;
			if (!Self->Authorize(Request, Denied))
			{
				OnComplete(MoveTemp(Denied));
				return true;
			}

			if (Request.Verb == EHttpServerRequestVerbs::VERB_POST)
			{
				AppendAuditLine(FullPath, Request);
			}

			return (Self->*Handler)(Request, OnComplete);
		});

	const FHttpRouteHandle Handle = Router->BindRoute(FHttpPath(FullPath), static_cast<EHttpServerRequestVerbs>(Verbs), Delegate);
	if (!Handle.IsValid())
	{
		UE_LOG(LogValhallaAdmin, Error, TEXT("failed to bind route %s."), *FullPath);
		return false;
	}

	RouteHandles.Add(Handle);
	UE_LOG(LogValhallaAdmin, Verbose, TEXT("bound %s"), *FullPath);
	return true;
}

bool UValhallaAdminServer::Authorize(const FHttpServerRequest& Request, TUniquePtr<FHttpServerResponse>& OutResponse) const
{
	const UValhallaDataSettings* Settings = UValhallaDataSettings::Get();

	// Off in the Editor, on in a Server build — see the setting's comment. The
	// switch is the *default*, not the check: an editor session that wants the
	// check can turn it on in DefaultGame.ini and is then subject to it.
	if (!Settings->bAdminApiRequireSecret)
	{
		return true;
	}

	// An empty secret with the check turned on is a configuration mistake that
	// would otherwise authorise everybody, so it refuses everybody instead.
	const FString& Secret = Settings->ServerSecret;
	if (Secret.IsEmpty())
	{
		UE_LOG(LogValhallaAdmin, Error,
			TEXT("bAdminApiRequireSecret is on but ServerSecret is empty; every request is refused."));
		OutResponse = MakeErrorResponse(EHttpServerResponseCodes::Denied, TEXT("Admin API misconfigured"));
		return false;
	}

	// FHttpServerRequest lower-cases its header keys.
	const TArray<FString>* Values = Request.Headers.Find(TEXT("authorization"));
	const FString Presented = (Values && Values->Num() > 0) ? (*Values)[0].TrimStartAndEnd() : FString();

	static const FString BearerPrefix(TEXT("Bearer "));
	const bool bWellFormed = Presented.StartsWith(BearerPrefix, ESearchCase::IgnoreCase);
	const FString Token = bWellFormed ? Presented.RightChop(BearerPrefix.Len()).TrimStartAndEnd() : FString();

	// A plain == on FStrings is a byte compare that returns early on the first
	// difference. That is a timing side channel, and the fix is three lines:
	// compare every character and fold the result.
	bool bMatches = bWellFormed && Token.Len() == Secret.Len();
	if (bMatches)
	{
		uint32 Difference = 0;
		for (int32 Index = 0; Index < Secret.Len(); ++Index)
		{
			Difference |= static_cast<uint32>(Token[Index] ^ Secret[Index]);
		}
		bMatches = (Difference == 0);
	}

	if (!bMatches)
	{
		UE_LOG(LogValhallaAdmin, Warning, TEXT("refused an unauthorised request (%s)."),
			Presented.IsEmpty() ? TEXT("no Authorization header") : TEXT("bad token"));
		OutResponse = MakeErrorResponse(EHttpServerResponseCodes::Denied, TEXT("Unauthorized"));
		return false;
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Handlers
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	/** Everything a mutating handler needs, resolved once with one failure path. */
	struct FAdminContext
	{
		UWorld* World = nullptr;
		AValhallaGameMode* GameMode = nullptr;
		UValhallaZoneSubsystem* Zones = nullptr;
		UValhallaDataSubsystem* Data = nullptr;
	};

	/** 503 "Game server not running", which is 1.0's answer to a missing room. */
	bool ResolveContext(const UObject* WorldContext, FAdminContext& Out, const FHttpResultCallback& OnComplete)
	{
		Out.World = WorldContext ? WorldContext->GetWorld() : nullptr;
		Out.GameMode = Out.World ? Out.World->GetAuthGameMode<AValhallaGameMode>() : nullptr;
		Out.Zones = Out.World ? Out.World->GetSubsystem<UValhallaZoneSubsystem>() : nullptr;
		Out.Data = Out.World && Out.World->GetGameInstance()
			? Out.World->GetGameInstance()->GetSubsystem<UValhallaDataSubsystem>()
			: nullptr;

		if (!Out.World || !Out.GameMode)
		{
			OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServiceUnavail, TEXT("Game server not running")));
			return false;
		}
		return true;
	}

	/** The NPC actor with this admin id (see AdminActorId), alive or dead, or null. */
	AValhallaNPC* FindNpcByName(UWorld* World, const FString& NpcId)
	{
		for (TActorIterator<AValhallaNPC> It(World); It; ++It)
		{
			if (AdminActorId(*It) == NpcId)
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** The player state whose `GetPlayerId()` prints as this session id, or null. */
	AValhallaPlayerState* FindPlayerBySessionId(UWorld* World, const FString& SessionId)
	{
		const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
		if (!GameState)
		{
			return nullptr;
		}
		for (APlayerState* Entry : GameState->PlayerArray)
		{
			AValhallaPlayerState* PS = Cast<AValhallaPlayerState>(Entry);
			if (PS && FString::FromInt(PS->GetPlayerId()) == SessionId)
			{
				return PS;
			}
		}
		return nullptr;
	}
}

bool UValhallaAdminServer::HandleState(const FHttpServerRequest& /*Request*/, const FHttpResultCallback& OnComplete)
{
	FValhallaAdminSnapshot Snapshot;
	BuildSnapshot(Snapshot);
	OnComplete(MakeJsonResponse(BuildStateJson(Snapshot), EHttpServerResponseCodes::Ok));
	return true;
}

bool UValhallaAdminServer::HandleSpawnNpc(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const TSharedPtr<FJsonObject> Body = ParseBody(Request);

	FString TemplateId, ZoneId;
	double X = 0.0, Y = 0.0;
	if (!GetRequiredString(Body, TEXT("templateId"), TemplateId)
		|| !GetRequiredString(Body, TEXT("zoneId"), ZoneId)
		|| !GetRequiredNumber(Body, TEXT("x"), X)
		|| !GetRequiredNumber(Body, TEXT("y"), Y))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("templateId, zoneId, x, y required")));
		return true;
	}

	FAdminContext Context;
	if (!ResolveContext(this, Context, OnComplete))
	{
		return true;
	}

	if (!Context.Data || !Context.Data->FindNPCTemplate(FName(*TemplateId)))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::NotFound, FString::Printf(TEXT("Unknown template: %s"), *TemplateId)));
		return true;
	}

	const FValhallaZoneDef* Zone = Context.Zones ? Context.Zones->FindZone(FName(*ZoneId)) : nullptr;
	if (!Zone)
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::NotFound, FString::Printf(TEXT("Unknown zone: %s"), *ZoneId)));
		return true;
	}

	const FVector Location = FromZoneLocalCm(*Zone, X, Y, PlacementZOffsetCm);

	// Spawned through an AValhallaNPCSpawner rather than as a bare NPC, and
	// deferred for the reason UValhallaZoneSubsystem::SpawnFromOverlayPoint
	// documents at length: the spawner reads TemplateId in BeginPlay, and a
	// non-deferred spawn into a world that has already begun play dispatches
	// BeginPlay before we can set it.
	//
	// The spawner is what gives the NPC a home point, so an admin-placed enemy
	// leashes and respawns exactly like an overlay-placed one. 1.0's
	// `registerAdminNPC` did the same thing for the same reason.
	const FTransform SpawnTransform(FRotator::ZeroRotator, Location);
	AValhallaNPCSpawner* Spawner = Context.World->SpawnActorDeferred<AValhallaNPCSpawner>(
		AValhallaNPCSpawner::StaticClass(), SpawnTransform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (!Spawner)
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServerError, TEXT("Spawner would not spawn")));
		return true;
	}

	Spawner->TemplateId = FName(*TemplateId);
	// A one-time spawn: the dashboard's "Spawn NPC here" is a GM dropping a
	// mob, not level design. Its corpse decays and nothing replaces it.
	Spawner->bRespawn = false;
#if WITH_EDITOR
	Spawner->SetActorLabel(FString::Printf(TEXT("Admin_%s_%s"), *ZoneId, *TemplateId));
#endif
	Spawner->FinishSpawning(SpawnTransform);

	AValhallaNPC* Npc = Spawner->GetSpawnedNPC();
	if (!Npc)
	{
		Spawner->Destroy();
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServerError, TEXT("Spawner produced no NPC")));
		return true;
	}

	UE_LOG(LogValhallaAdmin, Log,
		TEXT("spawn-npc: '%s' as %s in %s at zone-local (%.0f, %.0f) = world %s"),
		*TemplateId, *Npc->GetName(), *ZoneId, X, Y, *Location.ToCompactString());

	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetBoolField(TEXT("ok"), true);
	Json->SetStringField(TEXT("npcId"), AdminActorId(Npc));
	OnComplete(MakeJsonResponse(Json, EHttpServerResponseCodes::Ok));
	return true;
}

bool UValhallaAdminServer::HandleDropItem(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const TSharedPtr<FJsonObject> Body = ParseBody(Request);

	FString ItemId, ZoneId;
	double X = 0.0, Y = 0.0;
	if (!GetRequiredString(Body, TEXT("itemId"), ItemId)
		|| !GetRequiredString(Body, TEXT("zoneId"), ZoneId)
		|| !GetRequiredNumber(Body, TEXT("x"), X)
		|| !GetRequiredNumber(Body, TEXT("y"), Y))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("itemId, zoneId, x, y required")));
		return true;
	}

	// 1.0: `quantity ?? 1`.
	double Quantity = 1.0;
	Body->TryGetNumberField(TEXT("quantity"), Quantity);

	FAdminContext Context;
	if (!ResolveContext(this, Context, OnComplete))
	{
		return true;
	}

	if (!Context.Data || !Context.Data->FindItem(FName(*ItemId)))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::NotFound, FString::Printf(TEXT("Unknown item: %s"), *ItemId)));
		return true;
	}

	const FValhallaZoneDef* Zone = Context.Zones ? Context.Zones->FindZone(FName(*ZoneId)) : nullptr;
	if (!Zone)
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::NotFound, FString::Printf(TEXT("Unknown zone: %s"), *ZoneId)));
		return true;
	}

	const FVector Location = FromZoneLocalCm(*Zone, X, Y, PlacementZOffsetCm);

	FValhallaBagSlot Slot;
	Slot.ItemId = FName(*ItemId);
	Slot.Quantity = FMath::Max(1, FMath::RoundToInt32(Quantity));

	// SpawnOrMerge, not Spawn: dropping two items on the same spot has to make
	// one bag with two things in it, exactly as `LootBagSystem.spawnBag` did.
	AValhallaLootBag* Bag = AValhallaLootBag::SpawnOrMerge(Context.World, Location, { Slot }, TEXT("Admin"));
	if (!Bag)
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServerError, TEXT("Bag would not spawn")));
		return true;
	}

	UE_LOG(LogValhallaAdmin, Log,
		TEXT("drop-item: %d x '%s' as %s in %s at zone-local (%.0f, %.0f) = world %s"),
		Slot.Quantity, *ItemId, *Bag->GetName(), *ZoneId, X, Y, *Location.ToCompactString());

	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetBoolField(TEXT("ok"), true);
	Json->SetStringField(TEXT("bagId"), AdminActorId(Bag));
	OnComplete(MakeJsonResponse(Json, EHttpServerResponseCodes::Ok));
	return true;
}

bool UValhallaAdminServer::HandleKickPlayer(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const TSharedPtr<FJsonObject> Body = ParseBody(Request);

	FString SessionId;
	if (!GetRequiredString(Body, TEXT("sessionId"), SessionId))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("sessionId required")));
		return true;
	}

	FAdminContext Context;
	if (!ResolveContext(this, Context, OnComplete))
	{
		return true;
	}

	AValhallaPlayerState* PS = FindPlayerBySessionId(Context.World, SessionId);
	APlayerController* PC = PS ? Cast<APlayerController>(PS->GetOwner()) : nullptr;
	if (!PS || !PC)
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::NotFound, TEXT("Player not found")));
		return true;
	}

	// Optional `reason`, shown to the player. New in 2.0.
	FString Reason;
	if (Body.IsValid())
	{
		Body->TryGetStringField(TEXT("reason"), Reason);
	}
	Reason = Reason.TrimStartAndEnd();
	const FString KickText = Reason.IsEmpty() ? FString(TEXT("Kicked by admin.")) : FString::Printf(TEXT("Kicked by admin: %s"), *Reason);

	UE_LOG(LogValhallaAdmin, Log, TEXT("kick-player: %s (session %s)%s%s"), *PS->CharacterName, *SessionId,
		Reason.IsEmpty() ? TEXT("") : TEXT(" — "), *Reason);


	// The engine's own kick path, which sends the reason and closes the
	// connection. 1.0 sent a `kicked` message and then `client.leave(4002)`;
	// this is the same two things in one call.
	if (AGameSession* Session = Context.GameMode->GameSession)
	{
		Session->KickPlayer(PC, FText::FromString(KickText));
	}
	else
	{
		// No game session (PIE sometimes has none): fall back to the logout the
		// session would have driven, so the route is never a silent no-op.
		Context.GameMode->Logout(PC);
	}

	OnComplete(MakeOkResponse());
	return true;
}

bool UValhallaAdminServer::HandleTeleportPlayer(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const TSharedPtr<FJsonObject> Body = ParseBody(Request);

	FString SessionId, ZoneId;
	double X = 0.0, Y = 0.0;
	if (!GetRequiredString(Body, TEXT("sessionId"), SessionId)
		|| !GetRequiredString(Body, TEXT("zoneId"), ZoneId)
		|| !GetRequiredNumber(Body, TEXT("x"), X)
		|| !GetRequiredNumber(Body, TEXT("y"), Y))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("sessionId, zoneId, x, y required")));
		return true;
	}

	FAdminContext Context;
	if (!ResolveContext(this, Context, OnComplete))
	{
		return true;
	}

	AValhallaPlayerState* PS = FindPlayerBySessionId(Context.World, SessionId);
	if (!PS)
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::NotFound, TEXT("Player not found")));
		return true;
	}

	const FValhallaZoneDef* Zone = Context.Zones ? Context.Zones->FindZone(FName(*ZoneId)) : nullptr;
	if (!Zone)
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::NotFound, FString::Printf(TEXT("Unknown zone: %s"), *ZoneId)));
		return true;
	}

	APawn* Pawn = PS->GetPawn();
	if (!Pawn)
	{
		// A dead player has no pawn. 1.0 could move a dead player because its
		// position was a schema field; 2.0's is the pawn's, so there is nothing
		// to move and saying so beats moving nothing and reporting success.
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::Conflict, TEXT("Player has no pawn (dead or respawning)")));
		return true;
	}

	// Z: the zone's own default spawn height when it has one, because that is a
	// height a character demonstrably stands at in this zone. The box floor
	// plus the placement offset is the fallback for a zone volume with no arrow.
	const double ZOffset = Zone->DefaultSpawn.Z > Zone->Bounds.Min.Z
		? Zone->DefaultSpawn.Z - Zone->Bounds.Min.Z
		: PlacementZOffsetCm;

	const FVector Destination = FromZoneLocalCm(*Zone, X, Y, ZOffset);

	Pawn->TeleportTo(Destination, Pawn->GetActorRotation(), /*bIsATest=*/false, /*bNoCheck=*/true);

	// The same velocity zeroing UValhallaZoneSubsystem::TravelThroughPortal
	// does, for the same reason: a pawn that arrives still running carries the
	// momentum it had where it left.
	if (UCharacterMovementComponent* Movement = Pawn->FindComponentByClass<UCharacterMovementComponent>())
	{
		Movement->StopMovementImmediately();
	}

	// Written now rather than left to the next fixed tick's UpdatePlayerZones,
	// so a `general` chat message sent in the same frame goes to the new zone.
	// The tick will agree with it a sixtieth of a second later.
	PS->ZoneId = FName(*ZoneId);

	UE_LOG(LogValhallaAdmin, Log,
		TEXT("teleport-player: %s (session %s) to %s zone-local (%.0f, %.0f) = world %s"),
		*PS->CharacterName, *SessionId, *ZoneId, X, Y, *Destination.ToCompactString());

	OnComplete(MakeOkResponse());
	return true;
}

bool UValhallaAdminServer::HandleKillNpc(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const TSharedPtr<FJsonObject> Body = ParseBody(Request);

	FString NpcId;
	if (!GetRequiredString(Body, TEXT("npcId"), NpcId))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("npcId required")));
		return true;
	}

	FAdminContext Context;
	if (!ResolveContext(this, Context, OnComplete))
	{
		return true;
	}

	AValhallaNPC* Npc = FindNpcByName(Context.World, NpcId);
	if (!Npc)
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::NotFound, TEXT("NPC not found")));
		return true;
	}

	const AValhallaGameState* GameState = Context.World->GetGameState<AValhallaGameState>();
	const double Now = GameState ? GameState->GetServerTime() : 0.0;

	// `Die`, not "set alive = false": it is the only death path in 2.0, and an
	// NPC killed from the dashboard has to end up in a state the game itself
	// can produce — corpse animation held, collision off, respawn timer armed,
	// `npcDied` multicast so every client's HUD agrees. The consequence, and it
	// is a deliberate difference from 1.0's `adminKill`, is that an admin kill
	// also rolls the loot table: 2.0 has one death, and it drops.
	Npc->Die(/*Killer=*/nullptr, Now);

	UE_LOG(LogValhallaAdmin, Log, TEXT("kill-npc: %s ('%s')"), *NpcId, *Npc->DisplayName);

	OnComplete(MakeOkResponse());
	return true;
}

bool UValhallaAdminServer::HandleRespawnNpc(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const TSharedPtr<FJsonObject> Body = ParseBody(Request);

	FString NpcId;
	if (!GetRequiredString(Body, TEXT("npcId"), NpcId))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("npcId required")));
		return true;
	}

	FAdminContext Context;
	if (!ResolveContext(this, Context, OnComplete))
	{
		return true;
	}

	AValhallaNPC* Npc = FindNpcByName(Context.World, NpcId);
	if (!Npc)
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::NotFound, TEXT("NPC not found")));
		return true;
	}

	if (Npc->IsAlive())
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::Conflict, TEXT("NPC is already alive")));
		return true;
	}

	// A spawn point's NPC is replaced by a fresh instance, exactly as its timer
	// would have done; a hand-placed one stands back up.
	if (AValhallaNPCSpawner* Spawner = Npc->GetSpawner())
	{
		Spawner->RespawnNow();
	}
	else
	{
		Npc->Respawn();
	}

	UE_LOG(LogValhallaAdmin, Log, TEXT("respawn-npc: %s ('%s')"), *NpcId, *Npc->DisplayName);

	OnComplete(MakeOkResponse());
	return true;
}

bool UValhallaAdminServer::HandleDeleteNpc(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const TSharedPtr<FJsonObject> Body = ParseBody(Request);

	FString NpcId;
	if (!GetRequiredString(Body, TEXT("npcId"), NpcId))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("npcId required")));
		return true;
	}

	FAdminContext Context;
	if (!ResolveContext(this, Context, OnComplete))
	{
		return true;
	}

	AValhallaNPC* Npc = FindNpcByName(Context.World, NpcId);
	if (!Npc)
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::NotFound, TEXT("NPC not found")));
		return true;
	}

	const FString DisplayName = Npc->DisplayName;

	// A spawn point's NPC: remove it and let the spawn point's countdown bring
	// the next one, as if it had died. Anything else is simply gone.
	if (AValhallaNPCSpawner* Spawner = Npc->GetSpawner())
	{
		Spawner->RemoveNPCAndScheduleRespawn();
	}
	else
	{
		Npc->Destroy();
	}

	UE_LOG(LogValhallaAdmin, Log, TEXT("delete-npc: %s ('%s') destroyed"), *NpcId, *DisplayName);

	OnComplete(MakeOkResponse());
	return true;
}

bool UValhallaAdminServer::HandleReloadOverlays(const FHttpServerRequest& /*Request*/, const FHttpResultCallback& OnComplete)
{
	FAdminContext Context;
	if (!ResolveContext(this, Context, OnComplete))
	{
		return true;
	}

	if (!Context.Zones)
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServerError, TEXT("No zone subsystem in this world")));
		return true;
	}

	// Exactly what `valhalla.ReloadOverlays` runs. The console command and the
	// route are two front doors onto one function on purpose — a designer in
	// the editor and a designer in the browser must not be able to reach two
	// different behaviours.
	Context.Zones->ReloadOverlays();

	const TSharedRef<FJsonObject> Counts = MakeShared<FJsonObject>();
	Counts->SetNumberField(TEXT("zones"), Context.Zones->GetZones().Num());
	Counts->SetNumberField(TEXT("spawners"), Context.Zones->GetSpawnedSpawnerCount());

	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetBoolField(TEXT("ok"), true);
	Json->SetObjectField(TEXT("counts"), Counts);

	UE_LOG(LogValhallaAdmin, Log, TEXT("reload-overlays: %d zones, %d spawners."),
		Context.Zones->GetZones().Num(), Context.Zones->GetSpawnedSpawnerCount());

	OnComplete(MakeJsonResponse(Json, EHttpServerResponseCodes::Ok));
	return true;
}

bool UValhallaAdminServer::HandleReloadData(const FHttpServerRequest& /*Request*/, const FHttpResultCallback& OnComplete)
{
	FAdminContext Context;
	if (!ResolveContext(this, Context, OnComplete))
	{
		return true;
	}

	FValhallaDataReloadCounts Counts;
	if (!Context.GameMode->ReloadGameData(Counts))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServerError,
			TEXT("Reload failed; see the server log. Nothing was changed.")));
		return true;
	}

	const TSharedRef<FJsonObject> CountsJson = MakeShared<FJsonObject>();
	CountsJson->SetNumberField(TEXT("classes"),           Counts.Classes);
	CountsJson->SetNumberField(TEXT("items"),             Counts.Items);
	CountsJson->SetNumberField(TEXT("skills"),            Counts.Skills);
	CountsJson->SetNumberField(TEXT("npcTemplates"),      Counts.NpcTemplates);
	CountsJson->SetNumberField(TEXT("lootTables"),        Counts.LootTables);
	CountsJson->SetNumberField(TEXT("zones"),             Counts.Zones);
	CountsJson->SetNumberField(TEXT("playersRecomputed"), Counts.PlayersRecomputed);
	CountsJson->SetNumberField(TEXT("npcsUpdated"),       Counts.NpcsUpdated);
	CountsJson->SetNumberField(TEXT("npcsOrphaned"),      Counts.NpcsOrphaned);

	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetBoolField(TEXT("ok"), true);
	Json->SetObjectField(TEXT("counts"), CountsJson);

	OnComplete(MakeJsonResponse(Json, EHttpServerResponseCodes::Ok));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  MMO admin actions (2.0 only)
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaAdminServer::AppendAuditLine(const FString& Route, const FHttpServerRequest& Request)
{
	// One line per mutating request: when, which route, and the body as sent.
	// The body never carries a secret (that is a header), so it is safe to keep.
	FString Body;
	if (Request.Body.Num() > 0)
	{
		const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
		Body = FString(Converted.Length(), Converted.Get());
		Body.ReplaceInline(TEXT("\r"), TEXT(" "));
		Body.ReplaceInline(TEXT("\n"), TEXT(" "));
	}

	const FString Line = FString::Printf(TEXT("%s\t%s\t%s\n"), *FDateTime::UtcNow().ToIso8601(), *Route, *Body);
	const FString Path = FPaths::Combine(FPaths::ProjectLogDir(), TEXT("ValhallaAdminAudit.log"));
	FFileHelper::SaveStringToFile(Line, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(), FILEWRITE_Append);
	UE_LOG(LogValhallaAdmin, Log, TEXT("audit: %s %s"), *Route, *Body);
}

namespace
{
	/** A system chat line from the admin. */
	FValhallaChatMessage MakeAdminLine(const FString& Text, double Now)
	{
		FValhallaChatMessage Line;
		Line.Channel = EValhallaChatChannel::System;
		Line.SenderName = TEXT("Admin");
		Line.Message = FString::Printf(TEXT("[Admin] %s"), *Text);
		Line.Timestamp = Now;
		return Line;
	}

	/** Move a pawn somewhere and stop it, the way every admin teleport does. */
	void TeleportPawn(APawn* Pawn, const FVector& Destination)
	{
		Pawn->TeleportTo(Destination, Pawn->GetActorRotation(), /*bIsATest=*/false, /*bNoCheck=*/true);
		if (UCharacterMovementComponent* Movement = Pawn->FindComponentByClass<UCharacterMovementComponent>())
		{
			Movement->StopMovementImmediately();
		}
	}

	FString EquipSlotKey(EValhallaEquipSlot Slot)
	{
		return StaticEnum<EValhallaEquipSlot>()->GetNameStringByValue(static_cast<int64>(Slot)).ToLower();
	}

	TSharedRef<FJsonObject> MakeOkMessage(const FString& Message)
	{
		const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetBoolField(TEXT("ok"), true);
		Json->SetStringField(TEXT("message"), Message);
		return Json;
	}

	/** The account backend's failure status, as the admin API's. */
	EHttpServerResponseCodes BackendFailureCode(int32 Status)
	{
		switch (Status)
		{
		case 0:   return EHttpServerResponseCodes::ServiceUnavail;
		case 400: return EHttpServerResponseCodes::BadRequest;
		case 404: return EHttpServerResponseCodes::NotFound;
		default:  return EHttpServerResponseCodes::ServerError;
		}
	}

	/** Kick every connection logged into this account. Returns how many. */
	int32 KickAccount(AValhallaGameMode* GameMode, int32 UserId, const FString& Text)
	{
		int32 Kicked = 0;
		for (APlayerController* PC : GameMode->FindControllersForUser(UserId))
		{
			if (AGameSession* Session = GameMode->GameSession)
			{
				Session->KickPlayer(PC, FText::FromString(Text));
			}
			else
			{
				GameMode->Logout(PC);
			}
			++Kicked;
		}
		return Kicked;
	}

	/**
	 * Ban through the account backend, then kick every connection on that
	 * account. The HTTP answer goes out from the backend's continuation, so the
	 * dashboard only reports success once the ban is actually stored.
	 */
	void StartAccountBan(UWorld* World, int32 UserId, const FString& Username, double Minutes, const FString& Reason, const FHttpResultCallback& OnComplete)
	{
		UValhallaBackendSubsystem* Backend = UValhallaBackendSubsystem::Get(World);
		if (!Backend)
		{
			OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServiceUnavail, TEXT("No account backend in this game instance")));
			return;
		}

		TWeakObjectPtr<UWorld> WeakWorld(World);
		Backend->BanAccount(UserId, Username, Minutes, Reason, TEXT("admin"),
			[WeakWorld, OnComplete, Minutes](bool bOk, int32 Status, const TSharedPtr<FJsonObject>& Json, const FString& Error)
			{
				if (!bOk || !Json.IsValid())
				{
					OnComplete(MakeErrorResponse(BackendFailureCode(Status), FString::Printf(TEXT("Ban failed: %s"), *Error)));
					return;
				}

				double BannedUserId = 0.0;
				FString Account, Sentence;
				Json->TryGetNumberField(TEXT("userId"), BannedUserId);
				Json->TryGetStringField(TEXT("username"), Account);
				Json->TryGetStringField(TEXT("message"), Sentence);
				if (Sentence.IsEmpty())
				{
					Sentence = TEXT("This account has been banned.");
				}

				int32 Kicked = 0;
				if (UWorld* LiveWorld = WeakWorld.Get())
				{
					if (AValhallaGameMode* GameMode = LiveWorld->GetAuthGameMode<AValhallaGameMode>())
					{
						Kicked = KickAccount(GameMode, static_cast<int32>(BannedUserId), Sentence);
					}
				}

				const FString Summary = FString::Printf(TEXT("%s %s; %d connection(s) kicked"),
					*Account,
					Minutes > 0.0 ? *FString::Printf(TEXT("suspended for %.0f min"), Minutes) : TEXT("banned permanently"),
					Kicked);
				UE_LOG(LogValhallaAdmin, Log, TEXT("account ban: %s"), *Summary);

				const TSharedRef<FJsonObject> Out = MakeOkMessage(Summary);
				const TSharedPtr<FJsonObject>* Ban = nullptr;
				if (Json->TryGetObjectField(TEXT("ban"), Ban) && Ban)
				{
					Out->SetObjectField(TEXT("ban"), *Ban);
				}
				OnComplete(MakeJsonResponse(Out, EHttpServerResponseCodes::Ok));
			});
	}
}

bool UValhallaAdminServer::HandlePlayerAction(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const TSharedPtr<FJsonObject> Body = ParseBody(Request);

	FString SessionId, Action;
	if (!GetRequiredString(Body, TEXT("sessionId"), SessionId) || !GetRequiredString(Body, TEXT("action"), Action))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("sessionId and action required")));
		return true;
	}

	FAdminContext Context;
	if (!ResolveContext(this, Context, OnComplete))
	{
		return true;
	}

	AValhallaPlayerState* PS = FindPlayerBySessionId(Context.World, SessionId);
	AValhallaPlayerController* PC = PS ? Cast<AValhallaPlayerController>(PS->GetOwner()) : nullptr;
	if (!PS)
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::NotFound, TEXT("Player not found")));
		return true;
	}

	AValhallaCharacter* Character = Cast<AValhallaCharacter>(PS->GetPawn());
	const AValhallaGameState* GameState = Context.World->GetGameState<AValhallaGameState>();
	const double Now = GameState ? GameState->GetServerTime() : 0.0;

	auto Fail = [&OnComplete](EHttpServerResponseCodes Code, const FString& Message)
	{
		OnComplete(MakeErrorResponse(Code, Message));
		return true;
	};
	auto Ok = [&OnComplete, PS, &Action](const FString& Message)
	{
		UE_LOG(LogValhallaAdmin, Log, TEXT("player-action %s on %s: %s"), *Action, *PS->CharacterName, *Message);
		OnComplete(MakeJsonResponse(MakeOkMessage(Message), EHttpServerResponseCodes::Ok));
		return true;
	};
	auto Number = [&Body](const TCHAR* Field, double& Out) { return Body->TryGetNumberField(Field, Out); };
	auto Flag = [&Body](const TCHAR* Field, bool Default)
	{
		bool Value = Default;
		Body->TryGetBoolField(Field, Value);
		return Value;
	};
	const bool bUsesMana = PS->MaxMana > 0.f;

	// ── Account ──────────────────────────────────────────────────────────
	if (Action == TEXT("ban"))
	{
		const FValhallaBackendSession* Session = Context.GameMode->FindBackendSession(PC);
		if (!Session || Session->UserId <= 0)
		{
			return Fail(EHttpServerResponseCodes::Conflict, TEXT("This player has no backend account (dev join without a token) - kick them instead"));
		}
		double Minutes = 0.0;
		Number(TEXT("minutes"), Minutes);
		FString Reason;
		Body->TryGetStringField(TEXT("reason"), Reason);
		UE_LOG(LogValhallaAdmin, Log, TEXT("player-action ban on %s (account '%s'): requesting"), *PS->CharacterName, *Session->Username);
		StartAccountBan(Context.World, Session->UserId, Session->Username, Minutes, Reason.TrimStartAndEnd().Left(200), OnComplete);
		return true;
	}

	// ── Vitals ───────────────────────────────────────────────────────────
	if (Action == TEXT("set-vitals"))
	{
		if (!PS->IsAlive())
		{
			return Fail(EHttpServerResponseCodes::Conflict, TEXT("Player is dead; resurrect first"));
		}
		double Hp = PS->Hp, Pool = bUsesMana ? PS->Mana : PS->Energy;
		const bool bHp = Number(TEXT("hp"), Hp);
		const bool bPool = Number(TEXT("mana"), Pool);
		if (!bHp && !bPool)
		{
			return Fail(EHttpServerResponseCodes::BadRequest, TEXT("hp and/or mana required"));
		}
		if (bPool)
		{
			if (bUsesMana) { PS->Mana = FMath::Clamp(static_cast<float>(Pool), 0.f, PS->MaxMana); }
			else { PS->Energy = FMath::Clamp(static_cast<float>(Pool), 0.f, PS->MaxEnergy); }
		}
		if (bHp)
		{
			PS->Hp = FMath::Clamp(static_cast<float>(Hp), 0.f, PS->MaxHp);
			if (PS->Hp <= 0.f && Character)
			{
				Context.GameMode->HandlePlayerDeath(Character, nullptr);
				return Ok(TEXT("HP set to 0; the player died"));
			}
		}
		return Ok(FString::Printf(TEXT("HP %.0f/%.0f, %s %.0f/%.0f"), PS->Hp, PS->MaxHp,
			bUsesMana ? TEXT("mana") : TEXT("energy"),
			bUsesMana ? PS->Mana : PS->Energy, bUsesMana ? PS->MaxMana : PS->MaxEnergy));
	}

	if (Action == TEXT("heal"))
	{
		if (!PS->IsAlive())
		{
			return Fail(EHttpServerResponseCodes::Conflict, TEXT("Player is dead; resurrect first"));
		}
		PS->Hp = PS->MaxHp;
		PS->Mana = PS->MaxMana;
		PS->Energy = PS->MaxEnergy;
		return Ok(TEXT("Fully healed"));
	}

	if (Action == TEXT("kill"))
	{
		if (!PS->IsAlive() || !Character)
		{
			return Fail(EHttpServerResponseCodes::Conflict, TEXT("Player is already dead"));
		}
		PS->Hp = 0.f;
		Context.GameMode->HandlePlayerDeath(Character, nullptr);
		return Ok(TEXT("Killed"));
	}

	if (Action == TEXT("resurrect"))
	{
		if (!Context.GameMode->AdminResurrectInPlace(PS))
		{
			return Fail(EHttpServerResponseCodes::Conflict, TEXT("Player is not dead"));
		}
		return Ok(TEXT("Resurrected where they fell, full HP"));
	}

	// ── Items ────────────────────────────────────────────────────────────
	if (Action == TEXT("give-item"))
	{
		FString ItemId;
		if (!GetRequiredString(Body, TEXT("itemId"), ItemId))
		{
			return Fail(EHttpServerResponseCodes::BadRequest, TEXT("itemId required"));
		}
		const FValhallaItemTemplate* Item = Context.Data ? Context.Data->FindItem(FName(*ItemId)) : nullptr;
		if (!Item)
		{
			return Fail(EHttpServerResponseCodes::NotFound, FString::Printf(TEXT("Unknown item: %s"), *ItemId));
		}
		double Quantity = 1.0;
		Number(TEXT("quantity"), Quantity);
		const int32 Count = FMath::Clamp(FMath::RoundToInt32(Quantity), 1, 9999);
		const UValhallaDataSubsystem* Data = Context.Data;
		auto FindItem = [Data](FName Id) -> const FValhallaItemTemplate* { return Data->FindItem(Id); };
		if (!UValhallaInventoryLibrary::AddItem(PS->Inventory, FName(*ItemId), Count, FindItem))
		{
			return Fail(EHttpServerResponseCodes::Conflict, TEXT("Inventory full"));
		}
		if (PC)
		{
			PC->ClientChatMessage(MakeAdminLine(FString::Printf(TEXT("You received %d x %s."), Count, *Item->Name), Now));
		}
		return Ok(FString::Printf(TEXT("Gave %d x %s"), Count, *Item->Name));
	}

	if (Action == TEXT("remove-item"))
	{
		double Slot = -1.0;
		if (!Number(TEXT("slot"), Slot) || !PS->Inventory.IsValidIndex(FMath::RoundToInt32(Slot)))
		{
			return Fail(EHttpServerResponseCodes::BadRequest, TEXT("valid inventory slot required"));
		}
		const int32 Index = FMath::RoundToInt32(Slot);
		const FValhallaInventorySlot Removed = PS->Inventory[Index];
		double Quantity = Removed.Quantity;
		Number(TEXT("quantity"), Quantity);
		const int32 Count = FMath::Clamp(FMath::RoundToInt32(Quantity), 1, Removed.Quantity);
		UValhallaInventoryLibrary::RemoveItem(PS->Inventory, Index, Count);
		return Ok(FString::Printf(TEXT("Removed %d x %s"), Count, *Removed.ItemId.ToString()));
	}

	// ── Progression ──────────────────────────────────────────────────────
	if (Action == TEXT("set-level"))
	{
		double Level = 0.0;
		if (!Number(TEXT("level"), Level))
		{
			return Fail(EHttpServerResponseCodes::BadRequest, TEXT("level required"));
		}
		PS->Level = FMath::Clamp(FMath::RoundToInt32(Level), 1, Valhalla::MaxLevel);
		PS->Xp = 0;
		PS->RecomputeStats();
		PS->Hp = PS->IsAlive() ? PS->MaxHp : 0.f;
		PS->Mana = PS->MaxMana;
		PS->Energy = PS->MaxEnergy;
		return Ok(FString::Printf(TEXT("Level set to %d"), PS->Level));
	}

	if (Action == TEXT("grant-xp"))
	{
		double Amount = 0.0;
		if (!Number(TEXT("amount"), Amount) || Amount <= 0.0)
		{
			return Fail(EHttpServerResponseCodes::BadRequest, TEXT("positive amount required"));
		}
		const int32 Before = PS->Level;
		PS->AwardXp(FMath::RoundToInt32(Amount));
		return Ok(FString::Printf(TEXT("Granted %.0f XP (level %d -> %d)"), Amount, Before, PS->Level));
	}

	// ── Movement ─────────────────────────────────────────────────────────
	if (Action == TEXT("teleport-to-player"))
	{
		FString TargetSession;
		if (!GetRequiredString(Body, TEXT("targetSessionId"), TargetSession))
		{
			return Fail(EHttpServerResponseCodes::BadRequest, TEXT("targetSessionId required"));
		}
		AValhallaPlayerState* TargetPS = FindPlayerBySessionId(Context.World, TargetSession);
		APawn* TargetPawn = TargetPS ? TargetPS->GetPawn() : nullptr;
		if (!Character || !TargetPawn || TargetPS == PS)
		{
			return Fail(EHttpServerResponseCodes::Conflict, TEXT("Both players need a pawn, and they must differ"));
		}
		// Beside, not inside: a capsule-width and a bit to the target's right.
		const FVector Destination = TargetPawn->GetActorLocation() + TargetPawn->GetActorRightVector() * 80.0 + FVector(0, 0, 10);
		TeleportPawn(Character, Destination);
		PS->ZoneId = TargetPS->ZoneId;
		return Ok(FString::Printf(TEXT("Moved to %s"), *TargetPS->CharacterName));
	}

	if (Action == TEXT("unstuck"))
	{
		const FValhallaZoneDef* Zone = Context.Zones ? Context.Zones->FindZone(PS->ZoneId) : nullptr;
		if (!Character || !Zone)
		{
			return Fail(EHttpServerResponseCodes::Conflict, TEXT("Player has no pawn, or their zone is unknown"));
		}
		const double HalfHeight = Character->GetDefaultHalfHeight();
		TeleportPawn(Character, Zone->DefaultSpawn + FVector(0, 0, HalfHeight + 2.0));
		return Ok(FString::Printf(TEXT("Sent to %s's default spawn"), *Zone->DisplayName));
	}

	if (Action == TEXT("freeze"))
	{
		const bool bEnable = Flag(TEXT("enabled"), true);
		PS->bAdminFrozen = bEnable;
		if (Character)
		{
			if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
			{
				if (bEnable)
				{
					Movement->StopMovementImmediately();
					Movement->DisableMovement();
				}
				else
				{
					Movement->SetMovementMode(MOVE_Walking);
				}
			}
			if (bEnable)
			{
				if (UValhallaSkillComponent* Skills = Character->GetSkillComponent())
				{
					Skills->ServerCancelCast();
					Skills->ServerStopAutoAttack();
				}
			}
		}
		if (PC)
		{
			PC->ClientChatMessage(MakeAdminLine(bEnable ? TEXT("You have been frozen by an admin.") : TEXT("You can move again."), Now));
		}
		return Ok(bEnable ? TEXT("Frozen") : TEXT("Unfrozen"));
	}

	// ── Combat state ─────────────────────────────────────────────────────
	if (Action == TEXT("god-mode"))
	{
		PS->bAdminGodMode = Flag(TEXT("enabled"), true);
		return Ok(PS->bAdminGodMode ? TEXT("God mode on") : TEXT("God mode off"));
	}

	if (Action == TEXT("reset-cooldowns"))
	{
		const int32 Count = PS->GetSkillCooldownExpiry().Num();
		PS->GetSkillCooldownExpiry().Reset();
		return Ok(FString::Printf(TEXT("Cleared %d cooldown(s)"), Count));
	}

	if (Action == TEXT("clear-buffs"))
	{
		const int32 Count = PS->GetActiveBuffs().Num();
		PS->GetActiveBuffs().Reset();
		PS->RecomputeStats();
		return Ok(FString::Printf(TEXT("Removed %d buff(s)/debuff(s)"), Count));
	}

	// ── Communication ────────────────────────────────────────────────────
	if (Action == TEXT("message"))
	{
		FString Text;
		if (!GetRequiredString(Body, TEXT("text"), Text) || Text.TrimStartAndEnd().IsEmpty())
		{
			return Fail(EHttpServerResponseCodes::BadRequest, TEXT("text required"));
		}
		if (!PC)
		{
			return Fail(EHttpServerResponseCodes::Conflict, TEXT("Player has no controller"));
		}
		PC->ClientChatMessage(MakeAdminLine(Text.TrimStartAndEnd().Left(200), Now));
		return Ok(TEXT("Message sent"));
	}

	if (Action == TEXT("mute"))
	{
		double Minutes = 0.0;
		Number(TEXT("minutes"), Minutes);
		PS->AdminMutedUntil = Minutes > 0.0 ? Now + Minutes * 60.0 : 0.0;
		if (PC)
		{
			PC->ClientChatMessage(MakeAdminLine(Minutes > 0.0
				? FString::Printf(TEXT("You have been muted for %.0f minute(s)."), Minutes)
				: FString(TEXT("You are no longer muted.")), Now));
		}
		return Ok(Minutes > 0.0 ? FString::Printf(TEXT("Muted for %.0f min"), Minutes) : FString(TEXT("Unmuted")));
	}

	// ── Persistence ──────────────────────────────────────────────────────
	if (Action == TEXT("save"))
	{
		Context.GameMode->SaveCharacterFor(PS, TEXT("admin"));
		return Ok(TEXT("Save requested; see the server log for the backend's answer"));
	}

	return Fail(EHttpServerResponseCodes::BadRequest, FString::Printf(TEXT("Unknown action: %s"), *Action));
}

bool UValhallaAdminServer::HandlePlayerInspect(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const TSharedPtr<FJsonObject> Body = ParseBody(Request);

	FString SessionId;
	if (!GetRequiredString(Body, TEXT("sessionId"), SessionId))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("sessionId required")));
		return true;
	}

	FAdminContext Context;
	if (!ResolveContext(this, Context, OnComplete))
	{
		return true;
	}

	const AValhallaPlayerState* PS = FindPlayerBySessionId(Context.World, SessionId);
	if (!PS)
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::NotFound, TEXT("Player not found")));
		return true;
	}

	const AValhallaGameState* GameState = Context.World->GetGameState<AValhallaGameState>();
	const double Now = GameState ? GameState->GetServerTime() : 0.0;
	auto ItemName = [&Context](FName Id) -> FString
	{
		const FValhallaItemTemplate* Item = Context.Data ? Context.Data->FindItem(Id) : nullptr;
		return Item ? Item->Name : Id.ToString();
	};

	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetBoolField(TEXT("ok"), true);
	Json->SetStringField(TEXT("sessionId"), SessionId);
	Json->SetStringField(TEXT("name"), PS->CharacterName);
	Json->SetStringField(TEXT("classId"), PS->ClassId.ToString());
	Json->SetNumberField(TEXT("level"), PS->Level);
	Json->SetNumberField(TEXT("xp"), PS->Xp);
	Json->SetStringField(TEXT("zoneId"), PS->ZoneId.ToString());
	Json->SetBoolField(TEXT("alive"), PS->IsAlive());
	Json->SetNumberField(TEXT("hp"), Round(PS->Hp));
	Json->SetNumberField(TEXT("maxHp"), Round(PS->MaxHp));
	Json->SetNumberField(TEXT("mana"), Round(PS->Mana));
	Json->SetNumberField(TEXT("maxMana"), Round(PS->MaxMana));
	Json->SetNumberField(TEXT("energy"), Round(PS->Energy));
	Json->SetNumberField(TEXT("maxEnergy"), Round(PS->MaxEnergy));
	Json->SetBoolField(TEXT("godMode"), PS->bAdminGodMode);
	Json->SetBoolField(TEXT("frozen"), PS->bAdminFrozen);
	Json->SetNumberField(TEXT("mutedSeconds"), Round(FMath::Max(0.0, PS->AdminMutedUntil - Now)));

	const FValhallaResolvedStats& Stats = PS->GetStats();
	const TSharedRef<FJsonObject> StatsJson = MakeShared<FJsonObject>();
	StatsJson->SetNumberField(TEXT("strength"), Round(Stats.Strength));
	StatsJson->SetNumberField(TEXT("stamina"), Round(Stats.Stamina));
	StatsJson->SetNumberField(TEXT("dexterity"), Round(Stats.Dexterity));
	StatsJson->SetNumberField(TEXT("intelligence"), Round(Stats.Intelligence));
	StatsJson->SetNumberField(TEXT("wisdom"), Round(Stats.Wisdom));
	StatsJson->SetNumberField(TEXT("physicalResist"), Stats.PhysicalResist);
	StatsJson->SetNumberField(TEXT("spellResist"), Stats.SpellResist);
	StatsJson->SetNumberField(TEXT("speed"), Round(Stats.Speed));
	Json->SetObjectField(TEXT("stats"), StatsJson);

	TArray<TSharedPtr<FJsonValue>> InventoryJson;
	for (int32 Index = 0; Index < PS->Inventory.Num(); ++Index)
	{
		const FValhallaInventorySlot& Slot = PS->Inventory[Index];
		const TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("slot"), Index);
		Obj->SetStringField(TEXT("itemId"), Slot.ItemId.ToString());
		Obj->SetStringField(TEXT("name"), ItemName(Slot.ItemId));
		Obj->SetNumberField(TEXT("quantity"), Slot.Quantity);
		InventoryJson.Add(MakeShared<FJsonValueObject>(Obj));
	}
	Json->SetArrayField(TEXT("inventory"), InventoryJson);

	const TSharedRef<FJsonObject> EquipmentJson = MakeShared<FJsonObject>();
	for (int64 Value = 1; Value <= static_cast<int64>(EValhallaEquipSlot::Ring); ++Value)
	{
		const EValhallaEquipSlot Slot = static_cast<EValhallaEquipSlot>(Value);
		const FName ItemId = PS->GetEquipped(Slot);
		if (!ItemId.IsNone())
		{
			EquipmentJson->SetStringField(EquipSlotKey(Slot), FString::Printf(TEXT("%s (%s)"), *ItemName(ItemId), *ItemId.ToString()));
		}
	}
	Json->SetObjectField(TEXT("equipment"), EquipmentJson);

	TArray<TSharedPtr<FJsonValue>> BuffsJson;
	for (const FValhallaActiveBuff& Buff : PS->GetActiveBuffs())
	{
		const TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("skillId"), Buff.SkillId.ToString());
		Obj->SetNumberField(TEXT("secondsLeft"), Round(FMath::Max(0.0, Buff.ExpiresAt - Now)));
		BuffsJson.Add(MakeShared<FJsonValueObject>(Obj));
	}
	Json->SetArrayField(TEXT("buffs"), BuffsJson);

	OnComplete(MakeJsonResponse(Json, EHttpServerResponseCodes::Ok));
	return true;
}

bool UValhallaAdminServer::HandleBroadcast(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const TSharedPtr<FJsonObject> Body = ParseBody(Request);

	FString Text;
	if (!GetRequiredString(Body, TEXT("text"), Text) || Text.TrimStartAndEnd().IsEmpty())
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("text required")));
		return true;
	}

	FString ZoneId;
	Body->TryGetStringField(TEXT("zoneId"), ZoneId);

	FAdminContext Context;
	if (!ResolveContext(this, Context, OnComplete))
	{
		return true;
	}

	const AValhallaGameState* GameState = Context.World->GetGameState<AValhallaGameState>();
	const FValhallaChatMessage Line = MakeAdminLine(Text.TrimStartAndEnd().Left(200), GameState ? GameState->GetServerTime() : 0.0);

	int32 Delivered = 0;
	for (FConstPlayerControllerIterator It = Context.World->GetPlayerControllerIterator(); It; ++It)
	{
		AValhallaPlayerController* PC = Cast<AValhallaPlayerController>(It->Get());
		const AValhallaPlayerState* PS = PC ? PC->GetPlayerState<AValhallaPlayerState>() : nullptr;
		if (!PC || !PS || (!ZoneId.IsEmpty() && PS->ZoneId != FName(*ZoneId)))
		{
			continue;
		}
		PC->ClientChatMessage(Line);
		++Delivered;
	}

	UE_LOG(LogValhallaAdmin, Log, TEXT("broadcast to %s: %d player(s): %s"),
		ZoneId.IsEmpty() ? TEXT("everyone") : *ZoneId, Delivered, *Text);

	OnComplete(MakeJsonResponse(MakeOkMessage(FString::Printf(TEXT("Delivered to %d player(s)"), Delivered)), EHttpServerResponseCodes::Ok));
	return true;
}

bool UValhallaAdminServer::HandleSpawnPointAction(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const TSharedPtr<FJsonObject> Body = ParseBody(Request);

	FString SpawnPointId, Action;
	if (!GetRequiredString(Body, TEXT("spawnPointId"), SpawnPointId) || !GetRequiredString(Body, TEXT("action"), Action))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("spawnPointId and action required")));
		return true;
	}

	FAdminContext Context;
	if (!ResolveContext(this, Context, OnComplete))
	{
		return true;
	}

	AValhallaNPCSpawner* Spawner = nullptr;
	for (TActorIterator<AValhallaNPCSpawner> It(Context.World); It; ++It)
	{
		if (AdminActorId(*It) == SpawnPointId)
		{
			Spawner = *It;
			break;
		}
	}
	if (!Spawner)
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::NotFound, TEXT("Spawn point not found")));
		return true;
	}

	if (Action == TEXT("respawn-now"))
	{
		const AValhallaNPC* Current = Spawner->GetSpawnedNPC();
		if (Current && Current->IsAlive())
		{
			OnComplete(MakeErrorResponse(EHttpServerResponseCodes::Conflict, TEXT("Its NPC is alive")));
			return true;
		}
		Spawner->RespawnNow();
		OnComplete(MakeJsonResponse(MakeOkMessage(TEXT("Spawned")), EHttpServerResponseCodes::Ok));
		return true;
	}

	if (Action == TEXT("set-respawn-seconds"))
	{
		double Seconds = 0.0;
		if (!Body->TryGetNumberField(TEXT("seconds"), Seconds) || Seconds < 0.0)
		{
			OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("seconds >= 0 required (0 = template)")));
			return true;
		}
		Spawner->RespawnSeconds = static_cast<float>(Seconds);
		OnComplete(MakeJsonResponse(MakeOkMessage(FString::Printf(
			TEXT("Respawn is now %.0f s for this session. To keep it, set Respawn Time Override on the spawn point in Unreal."),
			Spawner->GetEffectiveRespawnSeconds())), EHttpServerResponseCodes::Ok));
		return true;
	}

	OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, FString::Printf(TEXT("Unknown action: %s"), *Action)));
	return true;
}

#endif // WITH_VALHALLA_ADMIN_API

bool UValhallaAdminServer::HandleAccountAction(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const TSharedPtr<FJsonObject> Body = ParseBody(Request);

	FString Action;
	if (!GetRequiredString(Body, TEXT("action"), Action))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("action required (ban, unban, list-bans)")));
		return true;
	}

	FAdminContext Context;
	if (!ResolveContext(this, Context, OnComplete))
	{
		return true;
	}

	UValhallaBackendSubsystem* Backend = UValhallaBackendSubsystem::Get(Context.World);
	if (!Backend)
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServiceUnavail, TEXT("No account backend in this game instance")));
		return true;
	}

	if (Action == TEXT("list-bans"))
	{
		Backend->ListBans([OnComplete](bool bOk, int32 Status, const TSharedPtr<FJsonObject>& Json, const FString& Error)
		{
			if (!bOk || !Json.IsValid())
			{
				OnComplete(MakeErrorResponse(BackendFailureCode(Status), FString::Printf(TEXT("Could not list bans: %s"), *Error)));
				return;
			}
			OnComplete(MakeJsonResponse(Json.ToSharedRef(), EHttpServerResponseCodes::Ok));
		});
		return true;
	}

	FString Username;
	double UserIdNumber = 0.0;
	Body->TryGetStringField(TEXT("username"), Username);
	Body->TryGetNumberField(TEXT("userId"), UserIdNumber);
	Username = Username.TrimStartAndEnd();
	const int32 UserId = static_cast<int32>(UserIdNumber);
	if (Username.IsEmpty() && UserId <= 0)
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("username or userId required")));
		return true;
	}

	if (Action == TEXT("ban"))
	{
		double Minutes = 0.0;
		FString Reason;
		Body->TryGetNumberField(TEXT("minutes"), Minutes);
		Body->TryGetStringField(TEXT("reason"), Reason);
		StartAccountBan(Context.World, UserId, Username, Minutes, Reason.TrimStartAndEnd().Left(200), OnComplete);
		return true;
	}

	if (Action == TEXT("unban"))
	{
		Backend->UnbanAccount(UserId, Username, [OnComplete](bool bOk, int32 Status, const TSharedPtr<FJsonObject>& Json, const FString& Error)
		{
			if (!bOk)
			{
				OnComplete(MakeErrorResponse(BackendFailureCode(Status), FString::Printf(TEXT("Unban failed: %s"), *Error)));
				return;
			}
			FString Account;
			if (Json.IsValid())
			{
				Json->TryGetStringField(TEXT("username"), Account);
			}
			UE_LOG(LogValhallaAdmin, Log, TEXT("account unban: %s"), *Account);
			OnComplete(MakeJsonResponse(MakeOkMessage(FString::Printf(TEXT("%s unbanned"), *Account)), EHttpServerResponseCodes::Ok));
		});
		return true;
	}

	OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, FString::Printf(TEXT("Unknown action: %s"), *Action)));
	return true;
}
