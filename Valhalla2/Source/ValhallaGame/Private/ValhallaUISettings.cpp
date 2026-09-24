// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaUISettings.h"

#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "ValhallaGameHUDWidget.h"

namespace
{
	TAutoConsoleVariable<FString> CVarHudClass(
		TEXT("valhalla.HudClass"),
		TEXT(""),
		TEXT("B-07: the game HUD class for the next HUD created (next PIE / map load), overriding Project Settings > Valhalla > UI. ")
		TEXT("A Widget Blueprint path such as /Game/Valhalla/UI/HUD/WBP_GameHUD (\"_C\" optional). Empty = the setting."),
		ECVF_Default);

	/** "/Game/X/WBP_A" or "/Game/X/WBP_A.WBP_A" -> "/Game/X/WBP_A.WBP_A_C"; a /Script/ path is left alone. */
	FString ToClassPath(const FString& In)
	{
		FString Path = In.TrimStartAndEnd();
		if (Path.IsEmpty() || Path.StartsWith(TEXT("/Script/")))
		{
			return Path;
		}
		FString Package = Path;
		FString Object;
		if (!Path.Split(TEXT("."), &Package, &Object))
		{
			Object = FPaths::GetBaseFilename(Package);
		}
		if (!Object.EndsWith(TEXT("_C")))
		{
			Object += TEXT("_C");
		}
		return Package + TEXT(".") + Object;
	}
}

UValhallaUISettings::UValhallaUISettings()
{
	CategoryName = TEXT("Valhalla");
	SectionName = TEXT("UI");
}

TSubclassOf<UValhallaGameHUDWidget> UValhallaUISettings::ResolveGameHUDClass(const TSoftClassPtr<UValhallaGameHUDWidget>& Setting, const FString& ConsoleOverride)
{
	if (!ConsoleOverride.TrimStartAndEnd().IsEmpty())
	{
		const FString Path = ToClassPath(ConsoleOverride);
		if (UClass* Loaded = LoadClass<UValhallaGameHUDWidget>(nullptr, *Path))
		{
			UE_LOG(LogValhallaHUD, Log, TEXT("game HUD class %s (valhalla.HudClass)."), *Loaded->GetPathName());
			return Loaded;
		}
		UE_LOG(LogValhallaHUD, Warning, TEXT("valhalla.HudClass '%s' is not a UValhallaGameHUDWidget class; ignoring it."), *Path);
	}

	if (!Setting.IsNull())
	{
		if (UClass* Loaded = Setting.LoadSynchronous())
		{
			UE_LOG(LogValhallaHUD, Log, TEXT("game HUD class %s (Project Settings > Valhalla > UI)."), *Loaded->GetPathName());
			return Loaded;
		}
		UE_LOG(LogValhallaHUD, Warning, TEXT("Game HUD Class '%s' could not be loaded; using the code-built HUD."), *Setting.ToString());
	}

	return UValhallaGameHUDWidget::StaticClass();
}

TSubclassOf<UValhallaGameHUDWidget> UValhallaUISettings::GetGameHUDClass()
{
	return ResolveGameHUDClass(GetDefault<UValhallaUISettings>()->GameHUDClass, CVarHudClass.GetValueOnGameThread());
}
