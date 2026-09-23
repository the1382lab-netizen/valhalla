// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/** Log category for every ValhallaCore subsystem (data loading, stats, rules). */
VALHALLACORE_API DECLARE_LOG_CATEGORY_EXTERN(LogValhallaCore, Log, All);

/**
 * ValhallaCore: engine-agnostic game rules and data.
 *
 * This module owns the ported Valhalla 1.0 data model (classes, items, skills,
 * NPCs, loot, zones) and the deterministic stat/damage math. It deliberately
 * depends on nothing but Core/CoreUObject/Engine/Json so it can be unit tested
 * and reused by both the dedicated server and the client.
 */
class FValhallaCoreModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
