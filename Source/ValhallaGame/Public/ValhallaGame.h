// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Log category for the primary game module (gameplay actors, netcode, world).
 * Phase 2 fills this module in; today it exists only so the project is a real
 * C++ project with a primary game module.
 */
VALHALLAGAME_API DECLARE_LOG_CATEGORY_EXTERN(LogValhallaGame, Log, All);
