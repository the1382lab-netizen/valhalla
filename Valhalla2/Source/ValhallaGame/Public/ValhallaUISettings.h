// Copyright Valhalla 2.0. All Rights Reserved.
//
// B-07 step 2: which game HUD class AValhallaHUD creates. Empty (the default)
// is the code-built UValhallaGameHUDWidget; a Widget Blueprint child of it
// (WBP_GameHUD, step 3) lays the HUD out in the designer instead.

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
	 * UValhallaGameHUDWidget. Empty = the code-built HUD (UValhallaGameHUDWidget
	 * itself), which is also the fallback if the class cannot be loaded.
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
