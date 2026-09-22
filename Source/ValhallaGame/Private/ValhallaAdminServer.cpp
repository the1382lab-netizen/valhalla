// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaAdminServer.h"

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

	HttpServerModule.StartAllListeners();

	bRunning = true;
	ActiveInstance = this;

	UE_LOG(LogValhallaAdmin, Log,
		TEXT("admin API listening on http://127.0.0.1:%d%s — %d routes. Point the 1.0 editor's proxy (GAME_SERVER_URL) here."),
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
		Info.Id         = Npc->GetName();
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

	// ── Loot bags ────────────────────────────────────────────────────────
	for (TActorIterator<AValhallaLootBag> It(World); It; ++It)
	{
		const AValhallaLootBag* Bag = *It;
		const FVector Location = Bag->GetActorLocation();
		const FString ZoneKey = ZoneKeyAt(Location);
		const FVector2D Local = LocalFor(FName(*ZoneKey), Location);

		FValhallaAdminLootBagInfo Info;
		Info.Id = Bag->GetName();
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

	/** The NPC actor with this `GetName()`, alive or dead, or null. */
	AValhallaNPC* FindNpcByName(UWorld* World, const FString& NpcId)
	{
		for (TActorIterator<AValhallaNPC> It(World); It; ++It)
		{
			if (It->GetName() == NpcId)
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
	Spawner->Count = 1;
	Spawner->SpawnRadius = 0.f;
	Spawner->DebugLabel = FString::Printf(TEXT("Admin_%s"), *TemplateId);
#if WITH_EDITOR
	Spawner->SetActorLabel(FString::Printf(TEXT("Admin_%s_%s"), *ZoneId, *TemplateId));
#endif
	Spawner->FinishSpawning(SpawnTransform);

	const TArray<TWeakObjectPtr<AValhallaNPC>>& Spawned = Spawner->GetSpawnedNPCs();
	AValhallaNPC* Npc = Spawned.Num() > 0 ? Spawned[0].Get() : nullptr;
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
	Json->SetStringField(TEXT("npcId"), Npc->GetName());
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
	Json->SetStringField(TEXT("bagId"), Bag->GetName());
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

	UE_LOG(LogValhallaAdmin, Log, TEXT("kick-player: %s (session %s)"), *PS->CharacterName, *SessionId);

	// The engine's own kick path, which sends the reason and closes the
	// connection. 1.0 sent a `kicked` message and then `client.leave(4002)`;
	// this is the same two things in one call.
	if (AGameSession* Session = Context.GameMode->GameSession)
	{
		Session->KickPlayer(PC, FText::FromString(TEXT("Kicked by admin.")));
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

	Npc->Respawn();

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

	// Destroying the actor is the whole of "permanently, no respawn": the
	// respawn timer lives on the NPC (`RespawnAt`, ticked by its own
	// ServerFixedTick), so there is nothing left to fire. 1.0 needed
	// `adminDelete` to reach into NPCSystem's tracker precisely because the
	// timer lived somewhere else.
	//
	// The spawner that made it is left standing and is *not* asked to make
	// another: AValhallaNPCSpawner spawns its group once, in BeginPlay, and
	// never again. A deleted NPC stays deleted until the next
	// `reload-overlays`, which is the behaviour a designer clearing a field
	// expects.
	Npc->Destroy();

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

#endif // WITH_VALHALLA_ADMIN_API
