// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ValhallaDataSettings.generated.h"

/** Where UValhallaDataSettings::ResolveServerSecret found the server secret. */
enum class EValhallaSecretSource : uint8
{
	None,
	CommandLine,
	Environment,
	SecretsFile,
	LegacyIni,
	DevDefault,
};

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
	 * "../shared/data" points at shared/data beside the Unreal project. An
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

	/**
	 * Browser origins the admin API answers with CORS headers (B-04). Exact
	 * `scheme://host[:port]` strings; the default is the web editor,
	 * `http://localhost:5180` and `http://127.0.0.1:5180`.
	 *
	 * The editor's proxy (editor/src/server.ts) is a Node process and sends no
	 * Origin, so it is unaffected. A request that carries an Origin not on this
	 * list, preflight or not, is refused with 403 before the token check: a
	 * page on some other site must not be able to drive the API from a browser
	 * on this machine. In the ini, quote each entry (`//` is a comment there):
	 * `+AdminApiAllowedOrigins="http://localhost:5180"`.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Admin API", meta = (DisplayName = "Admin API Allowed Origins"))
	TArray<FString> AdminApiAllowedOrigins;

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
	 * Legacy ini slot for the server secret. **Leave it empty.**
	 *
	 * Every ini under Config/ is packaged into the *client* too, so a secret set
	 * here ships to every player and lets them call the backend's
	 * server-to-server routes (load or overwrite any character). The secret now
	 * comes from outside the build; see GetServerSecret() for the order. A value
	 * here is still honoured on a server, with a warning, and ignored entirely by
	 * a packaged client. Not BlueprintReadOnly, and not editable in Project
	 * Settings, for the same reason.
	 */
	UPROPERTY(config)
	FString ServerSecret;

	/**
	 * Where the front end sends a client once a character is chosen —
	 * `ClientTravel("<this>?token=…&characterId=…")`.
	 *
	 * Ignored in PIE, where there is no separate server process to travel to;
	 * see `AValhallaFrontEndController`'s class comment for what happens instead.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Backend", meta = (DisplayName = "Game Server Address"))
	FString GameServerAddress = TEXT("127.0.0.1:7777");

	// ── Packaged clients: the public addresses ──────────────────────────

	/**
	 * Backend URL a *packaged client* uses instead of BackendUrl — the public,
	 * HTTPS address testers reach (Caddy in front of the backend; see
	 * deploy/README.md). Empty means "use BackendUrl".
	 *
	 * The editor, PIE and every server keep using BackendUrl (loopback), so this
	 * never changes how development works. Quote it in the ini, as BackendUrl.
	 * `-ValhallaBackendUrl=<url>` on the command line overrides both.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Backend", meta = (DisplayName = "Public Backend URL (packaged client)"))
	FString PublicBackendUrl;

	/**
	 * Game server address a *packaged client* travels to instead of
	 * GameServerAddress. Empty means "use GameServerAddress".
	 * `-ValhallaGameServer=<host:port>` on the command line overrides both.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Backend", meta = (DisplayName = "Public Game Server Address (packaged client)"))
	FString PublicGameServerAddress;

	/**
	 * The backend base URL this process should use, with no trailing slash:
	 * `-ValhallaBackendUrl=` if given, else PublicBackendUrl in a packaged
	 * client (when set), else BackendUrl.
	 */
	UFUNCTION(BlueprintCallable, Category = "Valhalla|Backend")
	FString GetResolvedBackendUrl() const;

	/**
	 * The game server a client travels to: `-ValhallaGameServer=` if given,
	 * else PublicGameServerAddress in a packaged client (when set), else
	 * GameServerAddress.
	 */
	UFUNCTION(BlueprintCallable, Category = "Valhalla|Backend")
	FString GetResolvedGameServerAddress() const;

	/**
	 * The shared secret this *server* presents to the backend's
	 * server-to-server routes (`X-Server-Secret`) and that the admin API
	 * demands (`Authorization: Bearer`). Resolved once, first match wins:
	 *
	 *   1. `-ValhallaServerSecret=` on the command line
	 *   2. the environment variable VALHALLA_SERVER_SECRET
	 *   3. VALHALLA_SERVER_SECRET in `<repo root>/secrets.local.env` — the
	 *      same gitignored file the backend and the web editor read
	 *   4. the legacy ServerSecret ini value (warns)
	 *   5. `dev-server-secret`, in an editor build only
	 *
	 * A server that has to face the internet (a dedicated server outside the
	 * editor, `UnrealEditor.exe -server` included, or a listen server in a
	 * non-editor build) refuses to start on the dev default or on nothing at
	 * all: see AValhallaGameMode::InitGame. Editor and PIE keep the default.
	 *
	 * Always empty in a packaged client, which never needs it. Deliberately not
	 * a UFUNCTION: nothing in Blueprint has any business reading it.
	 */
	FString GetServerSecret() const;

	/**
	 * The resolution order of GetServerSecret as a pure function of its inputs,
	 * so the automation test can check the order without a process per case.
	 * Each input is the (trimmed) value that source holds, or empty; the dev
	 * default is only returned when bAllowDevDefault (editor builds).
	 */
	static FString ResolveServerSecret(const FString& CommandLine, const FString& Environment, const FString& SecretsFile,
		const FString& LegacyIni, bool bAllowDevDefault, EValhallaSecretSource& OutSource);

	/** The public development default, `dev-server-secret`. */
	static const TCHAR* GetDevServerSecret();

	/** True for the public development default (which is no protection at all). */
	static bool IsDevServerSecret(const FString& Secret);

	/** True in a cooked build that is not a dedicated server: what a tester runs. */
	static bool IsPackagedClient();

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
