// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ValhallaDataSettings.generated.h"

/**
 * Project settings for the Valhalla 1.0 data hand-off.
 *
 * Phase 1 reads the game's balance data straight out of the 1.0 repo rather
 * than duplicating it into UE assets, so the two projects cannot drift. Phase 6
 * watches the same directory for hot reload.
 *
 * Edit under Project Settings > Game > Valhalla Data; the value is saved to
 * Config/DefaultGame.ini under [/Script/ValhallaCore.ValhallaDataSettings].
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Valhalla Data"))
class VALHALLACORE_API UValhallaDataSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UValhallaDataSettings();

	/**
	 * Directory holding classes.json, items.json, skills.json, npc-templates.json,
	 * loot-tables.json, zones.json and ui-config.json.
	 *
	 * A relative path is resolved against FPaths::ProjectDir(); the default
	 * "../../valhalla/shared/data" points at the sibling 1.0 checkout. An
	 * absolute path is used as-is.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Data", meta = (DisplayName = "Data Root"))
	FString DataRoot;

	// ── Phase 6b: the editor's live dashboard ───────────────────────────

	/**
	 * Whether the running server exposes the admin HTTP API at all.
	 *
	 * Defaults on, and is still only ever *reachable* in an Editor or Server
	 * build: the API is compiled out of a packaged Game target entirely
	 * (WITH_VALHALLA_ADMIN_API), and even where it is compiled in the game mode
	 * refuses to start it without authority. Turning this off is the switch for
	 * a developer who wants the port free, not a security boundary — Phase 7's
	 * token check is that.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Admin API", meta = (DisplayName = "Enable Admin API"))
	bool bEnableAdminApi = true;

	/**
	 * TCP port the admin API listens on, loopback only.
	 *
	 * 2568 rather than 1.0's 2567 on purpose: the two servers have to be able to
	 * run side by side on one machine while the 1.0 editor is pointed at
	 * whichever is up, and a port collision that only shows as "game server
	 * unreachable" in a React panel is a bad afternoon. The editor's proxy
	 * (editor/src/server.ts, GAME_SERVER_URL) is the one place that chooses.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Admin API", meta = (DisplayName = "Admin API Port", ClampMin = "1024", ClampMax = "65535"))
	int32 AdminApiPort = 2568;

	/**
	 * Whether the admin API refuses a request that does not present ServerSecret.
	 *
	 * Defaults *off*, and `UValhallaDataSettings()` turns it on for a Server
	 * target. That asymmetry is the whole design: in the Editor the 1.0 React
	 * dashboard talks to 2568 through `editor/src/server.ts`'s proxy, which today
	 * sends no Authorization header, so a default-on check would break the
	 * dashboard the moment this shipped. A packaged dedicated server has no
	 * dashboard in front of it and no reason to trust its loopback.
	 *
	 * See `UValhallaAdminServer::Authorize`. Phase 8 owes the proxy the header.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Admin API", meta = (DisplayName = "Admin API Requires Secret"))
	bool bAdminApiRequireSecret = false;

	// ── Phase 7: the 1.0 account backend ────────────────────────────────

	/**
	 * Base URL of the 1.0 Express server — accounts, characters, persistence.
	 *
	 * The same process 1.0's own client talks to (`server/src/index.ts`,
	 * `SERVER_PORT` 2567), and deliberately *not* 2.0's own admin port: 2.0 owns
	 * the world and 1.0 owns the database, which is what makes the two projects
	 * share one account per player rather than two.
	 *
	 * No trailing slash; `UValhallaBackendSubsystem` appends `/api/...`.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Backend", meta = (DisplayName = "Backend URL"))
	FString BackendUrl = TEXT("http://127.0.0.1:2567");

	/**
	 * Shared secret the game server presents to the backend's server-to-server
	 * routes (`X-Server-Secret`), and — when bAdminApiRequireSecret is on — the
	 * bearer token the admin API demands.
	 *
	 * One value for both because there is one trust boundary: "this process is
	 * the game server". A player's JWT is the *other* boundary and never appears
	 * here. The default is the backend's own dev default; a real deployment sets
	 * it in an ini the repo does not carry.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Backend", meta = (DisplayName = "Server Secret"))
	FString ServerSecret = TEXT("dev-server-secret");

	/**
	 * Where the front end sends a client once a character is chosen —
	 * `ClientTravel("<this>?token=…&characterId=…")`.
	 *
	 * Ignored in PIE, where there is no separate server process to travel to;
	 * see `AValhallaFrontEndController`'s class comment for what happens instead.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Backend", meta = (DisplayName = "Game Server Address"))
	FString GameServerAddress = TEXT("127.0.0.1:7777");

	/** BackendUrl with any trailing slashes removed. */
	UFUNCTION(BlueprintCallable, Category = "Valhalla|Backend")
	FString GetResolvedBackendUrl() const;

	/** The DataRoot as an absolute, normalised path with no trailing separator. */
	UFUNCTION(BlueprintCallable, Category = "Valhalla|Data")
	FString GetResolvedDataRoot() const;

	/** Convenience accessor for the singleton. Never returns null. */
	static const UValhallaDataSettings* Get();

	/**
	 * Resolve an arbitrary data root the same way GetResolvedDataRoot does.
	 * Exposed so the automation tests can resolve a root without a settings
	 * object, and so tools can preview a path the user is typing.
	 */
	static FString ResolveDataRoot(const FString& InDataRoot);

	//~ Begin UDeveloperSettings interface
	virtual FName GetCategoryName() const override;
	//~ End UDeveloperSettings interface
};
