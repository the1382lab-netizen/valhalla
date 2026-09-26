// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaGraphicsSettingsSubsystem.h"

#include "CoreGlobals.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ValhallaBackendSubsystem.h"

DEFINE_LOG_CATEGORY(LogValhallaGraphics);

namespace
{
	void LogWarnings(const TCHAR* Source, const TArray<FString>& Warnings)
	{
		for (const FString& Warning : Warnings)
		{
			UE_LOG(LogValhallaGraphics, Warning, TEXT("graphics settings (%s): %s"), Source, *Warning);
		}
	}

	FString Describe(const FValhallaGraphicsSettings& S)
	{
		return FString::Printf(TEXT("%s%s (GI %s), %d%% resolution, cap %s, VSync %s, motion blur %s"),
			FValhallaGraphicsSettings::QualityToString(S.Quality), S.IsCustom() ? TEXT(" custom") : TEXT(""),
			S.bGlobalIllumination ? TEXT("on") : TEXT("off"), S.ResolutionScale,
			S.FrameRateCap > 0 ? *FString::FromInt(S.FrameRateCap) : TEXT("none"),
			S.bVSync ? TEXT("on") : TEXT("off"), S.bMotionBlur ? TEXT("on") : TEXT("off"));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

bool UValhallaGraphicsSettingsSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

void UValhallaGraphicsSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UValhallaGraphicsSettingsSubsystem::TickDebounce), 0.25f);

	ConsoleCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("valhalla.Graphics"),
		TEXT("B-27. No arguments: print the graphics settings in force. preset=low|medium|high|epic gi=0|1 scale=50..100 cap=0|30..360 vsync=0|1 mb=0|1: apply without saving (benchmarks)."),
		FConsoleCommandWithArgsDelegate::CreateUObject(this, &UValhallaGraphicsSettingsSubsystem::HandleConsoleCommand),
		ECVF_Default);

	FValhallaGraphicsSettings Local;
	const bool bHasLocal = ReadCache(0, Local);
	Settings = bHasLocal ? Local : FValhallaGraphicsSettings();
	UE_LOG(LogValhallaGraphics, Log, TEXT("graphics: %s from %s."), *Describe(Settings),
		bHasLocal ? *CachePathFor(0) : TEXT("the defaults (first launch)"));

	// The editor keeps its own scalability until the player changes something.
	if (CanApply() && !GIsEditor)
	{
		ApplyToEngine(Settings);
		if (!bHasLocal)
		{
			WriteCache(0);
		}
	}
}

void UValhallaGraphicsSettingsSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	TickHandle.Reset();
	if (ConsoleCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(ConsoleCommand);
		ConsoleCommand = nullptr;
	}
	FlushPendingSave();
	OnChanged.Clear();
	Super::Deinitialize();
}

UValhallaGraphicsSettingsSubsystem* UValhallaGraphicsSettingsSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UValhallaGraphicsSettingsSubsystem>() : nullptr;
}

FString UValhallaGraphicsSettingsSubsystem::CachePathFor(int32 InUserId)
{
	const FString Name = InUserId > 0 ? FString::Printf(TEXT("graphics_%d.json"), InUserId) : FString(TEXT("graphics_local.json"));
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Settings"), Name);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Applying
// ─────────────────────────────────────────────────────────────────────────────

bool UValhallaGraphicsSettingsSubsystem::CanApply()
{
	return FApp::CanEverRender() && GEngine && GEngine->GetGameUserSettings();
}

void UValhallaGraphicsSettingsSubsystem::ApplyToEngine(const FValhallaGraphicsSettings& In)
{
	if (!CanApply())
	{
		return;
	}
	UGameUserSettings* User = GEngine->GetGameUserSettings();

	// The preset first (it sets every group, the resolution too), then what the
	// player chose on top of it.
	User->SetOverallScalabilityLevel(static_cast<int32>(In.Quality));
	User->SetGlobalIlluminationQuality(In.GetGlobalIlluminationLevel());
	User->SetResolutionScaleValueEx(static_cast<float>(In.ResolutionScale));
	User->SetVSyncEnabled(In.bVSync);
	User->SetFrameRateLimit(static_cast<float>(In.FrameRateCap));
	// Not ApplySettings: that also re-applies the window mode and size, which
	// would undo a -windowed / -resx launch and flicker the window.
	User->ApplyNonResolutionSettings();
	User->SaveSettings();

	// Scalability sets r.MotionBlurQuality from the post-process group; the
	// player's check box wins over it (a game setting outranks scalability).
	if (IConsoleVariable* MotionBlur = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlurQuality")))
	{
		MotionBlur->Set(In.GetMotionBlurQualityCVar(), ECVF_SetByGameSetting);
	}

	UE_LOG(LogValhallaGraphics, Log, TEXT("graphics applied: %s."), *Describe(In));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Changes
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaGraphicsSettingsSubsystem::Mutate(TFunctionRef<void(FValhallaGraphicsSettings&)> Edit)
{
	FValhallaGraphicsSettings Before = Settings;
	Edit(Settings);
	Settings.Sanitize();
	if (Settings.SameValues(Before))
	{
		return;
	}
	Settings.Touch();
	bDirty = true;
	LastChangeSeconds = FPlatformTime::Seconds();
	ApplyToEngine(Settings);
	OnChanged.Broadcast(Settings);
}

void UValhallaGraphicsSettingsSubsystem::ResetToDefaults()
{
	Mutate([](FValhallaGraphicsSettings& S)
	{
		TMap<FString, TSharedPtr<FJsonValue>> Unknown = MoveTemp(S.UnknownFields);
		S = FValhallaGraphicsSettings();
		S.UnknownFields = MoveTemp(Unknown);
	});
	UE_LOG(LogValhallaGraphics, Log, TEXT("graphics: reset to the defaults."));
	Save();
}

bool UValhallaGraphicsSettingsSubsystem::TickDebounce(float /*DeltaSeconds*/)
{
	if (bDirty && FPlatformTime::Seconds() - LastChangeSeconds >= SaveDelaySeconds)
	{
		Save();
	}
	return true;
}

void UValhallaGraphicsSettingsSubsystem::Save()
{
	bDirty = false;
	WriteCache(0);
	if (UserId > 0)
	{
		WriteCache(UserId);
	}
	PushToBackend();
}

void UValhallaGraphicsSettingsSubsystem::FlushPendingSave()
{
	if (bDirty)
	{
		Save();
	}
}

void UValhallaGraphicsSettingsSubsystem::WriteCache(int32 ForUserId) const
{
	const FString Path = CachePathFor(ForUserId);
	const FString Temp = Path + TEXT(".tmp");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree=*/true);
	if (!FFileHelper::SaveStringToFile(Settings.ToJsonString(), *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
		|| !IFileManager::Get().Move(*Path, *Temp, /*bReplace=*/true))
	{
		UE_LOG(LogValhallaGraphics, Warning, TEXT("graphics settings: could not write %s."), *Path);
	}
}

bool UValhallaGraphicsSettingsSubsystem::ReadCache(int32 ForUserId, FValhallaGraphicsSettings& Out) const
{
	const FString Path = CachePathFor(ForUserId);
	FString Text;
	if (!FPaths::FileExists(Path) || !FFileHelper::LoadFileToString(Text, *Path))
	{
		return false;
	}
	TArray<FString> Warnings;
	const bool bParsed = FValhallaGraphicsSettings::FromJsonString(Text, Out, &Warnings);
	LogWarnings(*Path, Warnings);
	return bParsed;
}

// ─────────────────────────────────────────────────────────────────────────────
//  The account
// ─────────────────────────────────────────────────────────────────────────────

FString UValhallaGraphicsSettingsSubsystem::CurrentToken() const
{
	if (UserId <= 0)
	{
		return FString();
	}
	// In the world, the backend session's token (it is the newest one, e.g.
	// after a password change); on the front end, the one login handed over.
	const UGameInstance* GameInstance = GetGameInstance();
	const UValhallaBackendSubsystem* Backend = GameInstance ? GameInstance->GetSubsystem<UValhallaBackendSubsystem>() : nullptr;
	if (Backend && Backend->HasPlayerSession() && Backend->GetPlayerUserId() == UserId)
	{
		return Backend->GetPlayerToken();
	}
	return AccountToken;
}

void UValhallaGraphicsSettingsSubsystem::PushToBackend()
{
	const FString Token = CurrentToken();
	UValhallaBackendSubsystem* Backend = GetGameInstance() ? GetGameInstance()->GetSubsystem<UValhallaBackendSubsystem>() : nullptr;
	if (Token.IsEmpty() || !Backend)
	{
		return;
	}
	const int32 Id = UserId;
	Backend->PutAccountSettings(Token, Settings.ToJson(),
		[Id](bool bOk, int32 Status, const FString& /*UpdatedAt*/, const FString& Error)
		{
			if (!bOk)
			{
				UE_LOG(LogValhallaGraphics, Warning, TEXT("graphics settings: backend save for account %d failed (%d: %s); kept in the cache file."),
					Id, Status, *Error);
			}
		});
}

void UValhallaGraphicsSettingsSubsystem::Adopt(const FValhallaGraphicsSettings& New, const TCHAR* Source)
{
	const bool bChanged = !New.SameValues(Settings);
	Settings = New;
	WriteCache(0);
	if (UserId > 0)
	{
		WriteCache(UserId);
	}
	UE_LOG(LogValhallaGraphics, Log, TEXT("graphics: account %d from %s (updated %s): %s."), UserId, Source, *Settings.UpdatedAt, *Describe(Settings));
	if (bChanged)
	{
		if (CanApply() && !GIsEditor)
		{
			ApplyToEngine(Settings);
		}
		OnChanged.Broadcast(Settings);
	}
}

void UValhallaGraphicsSettingsSubsystem::LoadForAccount(int32 InUserId, const FString& Token)
{
	if (InUserId <= 0)
	{
		return;
	}
	if (UserId != InUserId)
	{
		FlushPendingSave();
	}
	UserId = InUserId;
	AccountToken = Token;
	const int32 Serial = ++LoadSerial;

	// The account's cache when this PC has one and it is newer than what is in force.
	FValhallaGraphicsSettings Cached;
	if (ReadCache(UserId, Cached) && Cached.GetUpdatedAtTime() > Settings.GetUpdatedAtTime())
	{
		Adopt(Cached, *CachePathFor(UserId));
	}

	UValhallaBackendSubsystem* Backend = GetGameInstance() ? GetGameInstance()->GetSubsystem<UValhallaBackendSubsystem>() : nullptr;
	if (!Backend || Token.IsEmpty())
	{
		return;
	}
	const TWeakObjectPtr<UValhallaGraphicsSettingsSubsystem> WeakThis(this);
	const int32 Id = UserId;
	Backend->GetAccountSettings(Token,
		[WeakThis, Serial, Id](bool bOk, int32 Status, const TSharedPtr<FJsonObject>& Graphics, const FString& RowUpdatedAt, const FString& Error)
		{
			UValhallaGraphicsSettingsSubsystem* Self = WeakThis.Get();
			if (!Self || Self->LoadSerial != Serial)
			{
				return;
			}
			if (bOk)
			{
				FValhallaGraphicsSettings Remote;
				TArray<FString> Warnings;
				FValhallaGraphicsSettings::FromJson(Graphics, Remote, &Warnings);
				LogWarnings(TEXT("backend"), Warnings);
				if (Remote.UpdatedAt.IsEmpty())
				{
					Remote.UpdatedAt = RowUpdatedAt;
				}
				if (Remote.GetUpdatedAtTime() >= Self->Settings.GetUpdatedAtTime())
				{
					Self->bDirty = false;
					Self->Adopt(Remote, TEXT("the backend"));
				}
				else
				{
					UE_LOG(LogValhallaGraphics, Log, TEXT("graphics: this PC's copy (%s) is newer than account %d's on the backend (%s); sending it."),
						*Self->Settings.UpdatedAt, Id, *Remote.UpdatedAt);
					Self->WriteCache(Id);
					Self->PushToBackend();
				}
				return;
			}
			if (Status == 404)
			{
				// Nothing saved for this account yet: what this PC runs becomes its settings.
				UE_LOG(LogValhallaGraphics, Log, TEXT("graphics: account %d has none saved; sending this PC's."), Id);
				if (Self->Settings.UpdatedAt.IsEmpty())
				{
					Self->Settings.Touch();
				}
				Self->WriteCache(0);
				Self->WriteCache(Id);
				Self->PushToBackend();
				return;
			}
			UE_LOG(LogValhallaGraphics, Warning, TEXT("graphics: could not fetch account %d's settings (%d: %s); keeping this PC's."), Id, Status, *Error);
		});
}

void UValhallaGraphicsSettingsSubsystem::ForgetAccount()
{
	FlushPendingSave();
	UserId = 0;
	AccountToken.Reset();
	++LoadSerial;
}

// ─────────────────────────────────────────────────────────────────────────────
//  valhalla.Graphics
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaGraphicsSettingsSubsystem::HandleConsoleCommand(const TArray<FString>& Args)
{
	if (Args.Num() == 0)
	{
		UE_LOG(LogValhallaGraphics, Display, TEXT("graphics in force: %s (account %d%s)."), *Describe(Settings), UserId, bDirty ? TEXT(", unsaved") : TEXT(""));
		return;
	}
	FValhallaGraphicsSettings Trial = Settings;
	for (const FString& Arg : Args)
	{
		FString Key, Value;
		if (!Arg.Split(TEXT("="), &Key, &Value))
		{
			UE_LOG(LogValhallaGraphics, Warning, TEXT("valhalla.Graphics: '%s' is not key=value."), *Arg);
			continue;
		}
		Key = Key.ToLower();
		const int32 Number = FCString::Atoi(*Value);
		if (Key == TEXT("preset"))
		{
			EValhallaGraphicsQuality Quality;
			if (FValhallaGraphicsSettings::QualityFromString(Value, Quality))
			{
				Trial.SetPreset(Quality);
			}
		}
		else if (Key == TEXT("gi")) { Trial.bGlobalIllumination = Number != 0; }
		else if (Key == TEXT("scale")) { Trial.ResolutionScale = Number; }
		else if (Key == TEXT("cap")) { Trial.FrameRateCap = Number; }
		else if (Key == TEXT("vsync")) { Trial.bVSync = Number != 0; }
		else if (Key == TEXT("mb")) { Trial.bMotionBlur = Number != 0; }
		else
		{
			UE_LOG(LogValhallaGraphics, Warning, TEXT("valhalla.Graphics: unknown key '%s'."), *Key);
		}
	}
	Trial.Sanitize();
	ApplyToEngine(Trial);
	UE_LOG(LogValhallaGraphics, Display, TEXT("valhalla.Graphics: applied without saving: %s."), *Describe(Trial));
}
