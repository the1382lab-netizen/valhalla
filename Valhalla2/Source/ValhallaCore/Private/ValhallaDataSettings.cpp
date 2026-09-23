// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaDataSettings.h"

#include "CoreGlobals.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProperties.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ValhallaCore.h"

namespace ValhallaDataSettingsPrivate
{
	/** The backend's own development default; accepted only while NODE_ENV != production. */
	const TCHAR* const DevServerSecret = TEXT("dev-server-secret");

	/** `<repo root>/secrets.local.env`: gitignored, shared with the backend and the web editor. */
	FString GetSecretsFilePath()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("secrets.local.env")));
	}

	/**
	 * One KEY=VALUE from secrets.local.env, or empty. The format is the dotenv
	 * subset the Node side reads (server/src/loadEnv.ts): `#` comments, blank
	 * lines, optional matching quotes around the value.
	 */
	FString ReadSecretsFileValue(const FString& Key)
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *GetSecretsFilePath()))
		{
			return FString();
		}

		for (FString Line : Lines)
		{
			Line.TrimStartAndEndInline();
			if (Line.IsEmpty() || Line.StartsWith(TEXT("#")))
			{
				continue;
			}

			FString Name;
			FString Value;
			if (!Line.Split(TEXT("="), &Name, &Value))
			{
				continue;
			}

			Name.TrimStartAndEndInline();
			if (!Name.Equals(Key, ESearchCase::CaseSensitive))
			{
				continue;
			}

			Value.TrimStartAndEndInline();
			const bool bDoubleQuoted = Value.Len() >= 2 && Value.StartsWith(TEXT("\"")) && Value.EndsWith(TEXT("\""));
			const bool bSingleQuoted = Value.Len() >= 2 && Value.StartsWith(TEXT("'")) && Value.EndsWith(TEXT("'"));
			if (bDoubleQuoted || bSingleQuoted)
			{
				Value = Value.Mid(1, Value.Len() - 2);
			}
			return Value;
		}

		return FString();
	}

	/** `-<Match><value>` from the process command line, or empty. `Match` ends in '='. */
	FString CommandLineValue(const TCHAR* Match)
	{
		FString Value;
		if (FParse::Value(FCommandLine::Get(), Match, Value))
		{
			Value.TrimStartAndEndInline();
			return Value;
		}
		return FString();
	}
}

UValhallaDataSettings::UValhallaDataSettings()
	: DataRoot(TEXT("../shared/data"))
{
	// Phase 7: default the admin token check on where there is no editor proxy
	// in front of the port, and off where there is. See the property's comment.
#if UE_SERVER
	bAdminApiRequireSecret = true;
#else
	bAdminApiRequireSecret = false;
#endif
}

bool UValhallaDataSettings::IsPackagedClient()
{
	return FPlatformProperties::RequiresCookedData() && !IsRunningDedicatedServer();
}

FString UValhallaDataSettings::GetResolvedBackendUrl() const
{
	// Which URL, before the repairs below: the command line, then the public
	// URL in a packaged client, then BackendUrl (editor, PIE, every server).
	FString Url = ValhallaDataSettingsPrivate::CommandLineValue(TEXT("ValhallaBackendUrl="));
	if (Url.IsEmpty() && IsPackagedClient())
	{
		Url = PublicBackendUrl.TrimStartAndEnd();
	}
	if (Url.IsEmpty())
	{
		Url = BackendUrl;
	}
	Url.TrimStartAndEndInline();

	// ── The `//` that an ini file eats ───────────────────────────────────
	//
	// Unreal's ini parser strips `//` comments — `FParse::LineExtended` is
	// called with `SwallowDoubleSlashComments` — so an unquoted
	//
	//     BackendUrl=http://127.0.0.1:2567
	//
	// arrives here as the string "http:" and *nothing errors*. The login
	// screen then shows "backend http:", every request fails to connect, and
	// the symptom is indistinguishable from a backend that is not running. The
	// fix in the ini is to quote the value; this is the belt, because the next
	// person to type that line without quotes should not lose an afternoon.
	//
	// Two repairs, both of which log:
	//   "http:" / "https:" / ""   -> the default, which is what was meant.
	//   "127.0.0.1:2567"          -> prefixed with http://, because a bare
	//                                host:port is a reasonable thing to type.
	const FString Default(TEXT("http://127.0.0.1:2567"));

	if (Url.IsEmpty() || Url.Equals(TEXT("http:"), ESearchCase::IgnoreCase) || Url.Equals(TEXT("https:"), ESearchCase::IgnoreCase))
	{
		if (!Url.IsEmpty())
		{
			UE_LOG(LogValhallaCore, Warning,
				TEXT("ValhallaDataSettings.BackendUrl is '%s' — an ini file ate the '//'. Quote the value: BackendUrl=\"http://host:port\". Using '%s'."),
				*Url, *Default);
		}
		Url = Default;
	}
	else if (!Url.Contains(TEXT("://")))
	{
		UE_LOG(LogValhallaCore, Warning, TEXT("ValhallaDataSettings.BackendUrl '%s' has no scheme; assuming http://."), *Url);
		Url = TEXT("http://") + Url;
	}

	// One trailing slash is what a copy-paste from a browser's address bar
	// leaves behind, and "http://host:2567//api/auth/login" is a 404 on Express.
	while (Url.EndsWith(TEXT("/")))
	{
		Url.LeftChopInline(1);
	}

	return Url;
}

FString UValhallaDataSettings::GetResolvedGameServerAddress() const
{
	FString Address = ValhallaDataSettingsPrivate::CommandLineValue(TEXT("ValhallaGameServer="));
	if (Address.IsEmpty() && IsPackagedClient())
	{
		Address = PublicGameServerAddress.TrimStartAndEnd();
	}
	if (Address.IsEmpty())
	{
		Address = GameServerAddress.TrimStartAndEnd();
	}
	return Address;
}

FString UValhallaDataSettings::GetServerSecret() const
{
	// A packaged client never talks to the server-to-server routes or the admin
	// API, so it gets nothing, whatever any ini or file on the tester's machine says.
	if (IsPackagedClient())
	{
		return FString();
	}

	// Resolved once per process (thread-safe static init) and logged once, so
	// the log says where the secret came from without ever printing it.
	static const FString Resolved = [this]() -> FString
	{
		using namespace ValhallaDataSettingsPrivate;

		FString Secret = CommandLineValue(TEXT("ValhallaServerSecret="));
		if (!Secret.IsEmpty())
		{
			UE_LOG(LogValhallaCore, Log, TEXT("Server secret: from -ValhallaServerSecret."));
			return Secret;
		}

		Secret = FPlatformMisc::GetEnvironmentVariable(TEXT("VALHALLA_SERVER_SECRET")).TrimStartAndEnd();
		if (!Secret.IsEmpty())
		{
			UE_LOG(LogValhallaCore, Log, TEXT("Server secret: from the VALHALLA_SERVER_SECRET environment variable."));
			return Secret;
		}

		Secret = ReadSecretsFileValue(TEXT("VALHALLA_SERVER_SECRET"));
		if (!Secret.IsEmpty())
		{
			UE_LOG(LogValhallaCore, Log, TEXT("Server secret: from %s."), *GetSecretsFilePath());
			return Secret;
		}

		Secret = ServerSecret.TrimStartAndEnd();
		if (!Secret.IsEmpty())
		{
			UE_LOG(LogValhallaCore, Warning,
				TEXT("Server secret: from the ServerSecret ini value. Every ini under Config/ is packaged into the client; move it to secrets.local.env and delete the ini line."));
			return Secret;
		}

#if WITH_EDITOR
		UE_LOG(LogValhallaCore, Log,
			TEXT("Server secret: the development default (no -ValhallaServerSecret, VALHALLA_SERVER_SECRET or %s). A backend running with NODE_ENV=production will refuse it."),
			*GetSecretsFilePath());
		return FString(DevServerSecret);
#else
		UE_LOG(LogValhallaCore, Error,
			TEXT("Server secret: none configured. Pass -ValhallaServerSecret=, set VALHALLA_SERVER_SECRET, or put it in %s. Logins will fail."),
			*GetSecretsFilePath());
		return FString();
#endif
	}();

	return Resolved;
}

FName UValhallaDataSettings::GetCategoryName() const
{
	return FName(TEXT("Game"));
}

const UValhallaDataSettings* UValhallaDataSettings::Get()
{
	// GetDefault never returns null for a CDO-backed settings class.
	return GetDefault<UValhallaDataSettings>();
}

FString UValhallaDataSettings::ResolveDataRoot(const FString& InDataRoot)
{
	FString Path = InDataRoot;
	Path.TrimStartAndEndInline();

	if (Path.IsEmpty())
	{
		Path = TEXT("../shared/data");
	}

	// Absolute paths (C:\..., /mnt/...) pass straight through; relative ones
	// hang off the .uproject directory so the setting stays machine-independent.
	if (!FPaths::IsRelative(Path))
	{
		FPaths::NormalizeDirectoryName(Path);
		return FPaths::ConvertRelativePathToFull(Path);
	}

	const FString Combined = FPaths::Combine(FPaths::ProjectDir(), Path);
	FString Full = FPaths::ConvertRelativePathToFull(Combined);
	FPaths::NormalizeDirectoryName(Full);
	return Full;
}

FString UValhallaDataSettings::GetResolvedDataRoot() const
{
	return ResolveDataRoot(DataRoot);
}
