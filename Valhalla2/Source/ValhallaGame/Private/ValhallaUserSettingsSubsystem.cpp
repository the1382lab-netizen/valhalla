// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaUserSettingsSubsystem.h"

#include "CoreGlobals.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ValhallaBackendSubsystem.h"

DEFINE_LOG_CATEGORY(LogValhallaUserSettings);

namespace
{
	void LogWarnings(const TCHAR* Source, const TArray<FString>& Warnings)
	{
		for (const FString& Warning : Warnings)
		{
			UE_LOG(LogValhallaUserSettings, Warning, TEXT("UI settings (%s): %s"), Source, *Warning);
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

bool UValhallaUserSettingsSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// A dedicated server has no HUD and no player of its own.
	return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

void UValhallaUserSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UValhallaUserSettingsSubsystem::TickDebounce), 0.25f);
}

void UValhallaUserSettingsSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	TickHandle.Reset();
	FlushPendingSave();
	OnChanged.Clear();
	Super::Deinitialize();
}

UValhallaUserSettingsSubsystem* UValhallaUserSettingsSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UValhallaUserSettingsSubsystem>() : nullptr;
}

FString UValhallaUserSettingsSubsystem::CachePathFor(int32 InCharacterId)
{
	const FString Name = InCharacterId > 0 ? FString::Printf(TEXT("settings_%d.json"), InCharacterId) : FString(TEXT("settings_offline.json"));
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UI"), Name);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Changes
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaUserSettingsSubsystem::Mutate(TFunctionRef<void(FValhallaUserUISettings&)> Edit)
{
	if (!IsLoaded())
	{
		EnsureLoadedForCurrentCharacter();
	}
	Edit(Settings);
	Settings.Sanitize();
	MarkChanged();
}

void UValhallaUserSettingsSubsystem::Set(const FValhallaUserUISettings& New)
{
	Mutate([&New](FValhallaUserUISettings& S)
	{
		TMap<FString, TSharedPtr<FJsonValue>> Unknown = S.UnknownFields;
		S = New;
		if (S.UnknownFields.Num() == 0)
		{
			S.UnknownFields = MoveTemp(Unknown);
		}
	});
}

void UValhallaUserSettingsSubsystem::ResetToDefaults(EValhallaUISettingsSection Section)
{
	Mutate([Section](FValhallaUserUISettings& S) { S.ResetSection(Section); });
	UE_LOG(LogValhallaUserSettings, Log, TEXT("UI settings: reset %s for character %d."),
		*StaticEnum<EValhallaUISettingsSection>()->GetNameStringByValue(static_cast<int64>(Section)), CharacterId);
	Save();
}

void UValhallaUserSettingsSubsystem::MarkChanged()
{
	Settings.Touch();
	bDirty = true;
	LastChangeSeconds = FPlatformTime::Seconds();
	Broadcast();
}

void UValhallaUserSettingsSubsystem::Broadcast()
{
	OnChanged.Broadcast(Settings);
}

bool UValhallaUserSettingsSubsystem::TickDebounce(float /*DeltaSeconds*/)
{
	if (bDirty && FPlatformTime::Seconds() - LastChangeSeconds >= SaveDelaySeconds)
	{
		Save();
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Save
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaUserSettingsSubsystem::Save()
{
	if (!IsLoaded())
	{
		return;
	}
	bDirty = false;
	WriteCache();
	PushToBackend();
}

void UValhallaUserSettingsSubsystem::FlushPendingSave()
{
	if (bDirty)
	{
		Save();
	}
}

void UValhallaUserSettingsSubsystem::FlushAndForget()
{
	FlushPendingSave();
	if (IsLoaded())
	{
		UE_LOG(LogValhallaUserSettings, Log, TEXT("UI settings: done with character %d."), CharacterId);
	}
	CharacterId = INDEX_NONE;
	++LoadSerial;
	bFetching = false;
	Settings = FValhallaUserUISettings();
}

void UValhallaUserSettingsSubsystem::WriteCache() const
{
	const FString Path = CachePathFor(CharacterId);
	const FString Temp = Path + TEXT(".tmp");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree=*/true);
	// Write beside, then move over: a crash mid-write never leaves half a file.
	if (!FFileHelper::SaveStringToFile(Settings.ToJsonString(), *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
		|| !IFileManager::Get().Move(*Path, *Temp, /*bReplace=*/true))
	{
		UE_LOG(LogValhallaUserSettings, Warning, TEXT("UI settings: could not write %s."), *Path);
		return;
	}
	UE_LOG(LogValhallaUserSettings, Log, TEXT("UI settings: saved %s (updated %s)."), *Path, *Settings.UpdatedAt);
}

bool UValhallaUserSettingsSubsystem::ReadCache(int32 InCharacterId, FValhallaUserUISettings& Out) const
{
	const FString Path = CachePathFor(InCharacterId);
	FString Text;
	if (!FPaths::FileExists(Path) || !FFileHelper::LoadFileToString(Text, *Path))
	{
		return false;
	}
	TArray<FString> Warnings;
	const bool bParsed = FValhallaUserUISettings::FromJsonString(Text, Out, &Warnings);
	LogWarnings(*Path, Warnings);
	return bParsed;
}

FString UValhallaUserSettingsSubsystem::TokenForCurrentCharacter() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UValhallaBackendSubsystem* Backend = GameInstance ? GameInstance->GetSubsystem<UValhallaBackendSubsystem>() : nullptr;
	return Backend && CharacterId > 0 && Backend->HasPlayerSession() && Backend->GetPlayerCharacterId() == CharacterId
		? Backend->GetPlayerToken() : FString();
}

void UValhallaUserSettingsSubsystem::PushToBackend()
{
	const FString Token = TokenForCurrentCharacter();
	UValhallaBackendSubsystem* Backend = GetGameInstance() ? GetGameInstance()->GetSubsystem<UValhallaBackendSubsystem>() : nullptr;
	if (Token.IsEmpty() || !Backend)
	{
		return;
	}
	const int32 Id = CharacterId;
	Backend->PutCharacterSettings(Token, Id, Settings.ToJson(),
		[Id](bool bOk, int32 Status, const FString& /*UpdatedAt*/, const FString& Error)
		{
			if (!bOk)
			{
				// The cache file has it; the next change, or the next login's
				// newer-local-copy rule, sends it again.
				UE_LOG(LogValhallaUserSettings, Warning, TEXT("UI settings: backend save for character %d failed (%d: %s); kept in the cache file."),
					Id, Status, *Error);
			}
		});
}

// ─────────────────────────────────────────────────────────────────────────────
//  Load
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaUserSettingsSubsystem::EnsureLoadedForCurrentCharacter()
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UValhallaBackendSubsystem* Backend = GameInstance ? GameInstance->GetSubsystem<UValhallaBackendSubsystem>() : nullptr;
	const int32 Wanted = Backend ? FMath::Max(0, Backend->GetPlayerCharacterId()) : 0;
	if (CharacterId != Wanted)
	{
		LoadForCharacter(Wanted);
	}
}

void UValhallaUserSettingsSubsystem::LoadForCharacter(int32 InCharacterId)
{
	InCharacterId = FMath::Max(0, InCharacterId);
	if (IsLoaded() && CharacterId != InCharacterId)
	{
		FlushPendingSave();
	}
	CharacterId = InCharacterId;
	const int32 Serial = ++LoadSerial;
	bDirty = false;

	// The disk copy (or the defaults) at once, so the HUD never waits on HTTP.
	FValhallaUserUISettings Local;
	const bool bHasLocal = ReadCache(CharacterId, Local);
	Settings = bHasLocal ? Local : FValhallaUserUISettings();
	UE_LOG(LogValhallaUserSettings, Log, TEXT("UI settings: character %d from %s%s."), CharacterId,
		bHasLocal ? *CachePathFor(CharacterId) : TEXT("the defaults"),
		bHasLocal ? *FString::Printf(TEXT(" (updated %s)"), *Settings.UpdatedAt) : TEXT(""));
	Broadcast();

	const FString Token = TokenForCurrentCharacter();
	UValhallaBackendSubsystem* Backend = GetGameInstance() ? GetGameInstance()->GetSubsystem<UValhallaBackendSubsystem>() : nullptr;
	if (Token.IsEmpty() || !Backend)
	{
		if (CharacterId > 0)
		{
			UE_LOG(LogValhallaUserSettings, Log, TEXT("UI settings: no backend session for character %d; the cache file only."), CharacterId);
		}
		return;
	}

	bFetching = true;
	const TWeakObjectPtr<UValhallaUserSettingsSubsystem> WeakThis(this);
	const int32 Id = CharacterId;
	Backend->GetCharacterSettings(Token, Id,
		[WeakThis, Serial, Id](bool bOk, int32 Status, const TSharedPtr<FJsonObject>& Ui, const FString& RowUpdatedAt, const FString& Error)
		{
			UValhallaUserSettingsSubsystem* Self = WeakThis.Get();
			if (!Self || Self->LoadSerial != Serial)
			{
				return; // another character (or none) since
			}
			Self->bFetching = false;

			if (bOk)
			{
				FValhallaUserUISettings Remote;
				TArray<FString> Warnings;
				FValhallaUserUISettings::FromJson(Ui, Remote, &Warnings);
				LogWarnings(TEXT("backend"), Warnings);
				if (Remote.UpdatedAt.IsEmpty())
				{
					Remote.UpdatedAt = RowUpdatedAt;
				}
				// Compared with what is live now, which includes any change made
				// while the GET was in flight (that one is newer and stays).
				if (Remote.GetUpdatedAtTime() >= Self->Settings.GetUpdatedAtTime())
				{
					Self->Settings = Remote;
					Self->bDirty = false;
					Self->WriteCache();
					UE_LOG(LogValhallaUserSettings, Log, TEXT("UI settings: character %d from the backend (updated %s)."), Id, *Remote.UpdatedAt);
					Self->Broadcast();
				}
				else
				{
					UE_LOG(LogValhallaUserSettings, Log, TEXT("UI settings: character %d's local copy (%s) is newer than the backend's (%s); sending it."),
						Id, *Self->Settings.UpdatedAt, *Remote.UpdatedAt);
					Self->PushToBackend();
				}
				return;
			}

			if (Status == 404)
			{
				// Nothing on the backend (or not this account's character: then the PUT is refused too).
				if (!Self->Settings.UpdatedAt.IsEmpty())
				{
					UE_LOG(LogValhallaUserSettings, Log, TEXT("UI settings: the backend has none for character %d; sending the local copy."), Id);
					Self->PushToBackend();
				}
				else
				{
					UE_LOG(LogValhallaUserSettings, Log, TEXT("UI settings: character %d has none saved; the defaults."), Id);
				}
				return;
			}

			UE_LOG(LogValhallaUserSettings, Warning, TEXT("UI settings: could not fetch character %d's from the backend (%d: %s); using %s."),
				Id, Status, *Error, Self->Settings.UpdatedAt.IsEmpty() ? TEXT("the defaults") : TEXT("the cache file"));
		});
}
