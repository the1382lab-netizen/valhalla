// Copyright Valhalla 2.0. All Rights Reserved.
//
// B-07: which game HUD class AValhallaHUD creates: a Widget Blueprint child of
// UValhallaGameHUDWidget that lays the HUD out in the designer. DefaultGame.ini
// sets it to /Game/Valhalla/UI/HUD/WBP_GameHUD (step 4). The code-built layout
// is gone, so empty (or a class that will not load) means a blank HUD, with an
// error in the log.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Templates/SubclassOf.h"
#include "UObject/SoftObjectPtr.h"
#include "ValhallaUISettings.generated.h"

class UValhallaGameHUDWidget;

/**
 * Project Settings > Valhalla > UI. Saved to Config/DefaultGame.ini under
 * [/Script/ValhallaGame.ValhallaUISettings].
 *
 * `valhalla.HudClass <path>` overrides it for the next HUD created (the next
 * PIE session or map load), e.g.
 * `valhalla.HudClass /Game/Valhalla/UI/HUD/WBP_GameHUD`; an empty value goes
 * back to the setting.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "UI"))
class VALHALLAGAME_API UValhallaUISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UValhallaUISettings();

	/**
	 * The game HUD widget class: a Widget Blueprint child of
	 * UValhallaGameHUDWidget (WBP_GameHUD). Empty, or a class that cannot be
	 * loaded, falls back to UValhallaGameHUDWidget itself, which has no layout
	 * since B-07 step 4: only nameplates and floating text show.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD", meta = (DisplayName = "Game HUD Class"))
	TSoftClassPtr<UValhallaGameHUDWidget> GameHUDClass;

	/**
	 * The class to create: `ConsoleOverride` (a class or Widget Blueprint path,
	 * `_C` optional) if set and loadable, else `Setting` if set and loadable,
	 * else UValhallaGameHUDWidget. Logs which, and why it fell back.
	 */
	static TSubclassOf<UValhallaGameHUDWidget> ResolveGameHUDClass(const TSoftClassPtr<UValhallaGameHUDWidget>& Setting, const FString& ConsoleOverride);

	/** ResolveGameHUDClass(GameHUDClass, the `valhalla.HudClass` console variable). */
	static TSubclassOf<UValhallaGameHUDWidget> GetGameHUDClass();
};
