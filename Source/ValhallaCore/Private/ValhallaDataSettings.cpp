// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaDataSettings.h"

#include "Misc/Paths.h"
#include "ValhallaCore.h"

UValhallaDataSettings::UValhallaDataSettings()
	: DataRoot(TEXT("../../valhalla/shared/data"))
{
	// Phase 7: default the admin token check on where there is no editor proxy
	// in front of the port, and off where there is. See the property's comment.
#if UE_SERVER
	bAdminApiRequireSecret = true;
#else
	bAdminApiRequireSecret = false;
#endif
}

FString UValhallaDataSettings::GetResolvedBackendUrl() const
{
	FString Url = BackendUrl;
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
		Path = TEXT("../../valhalla/shared/data");
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
