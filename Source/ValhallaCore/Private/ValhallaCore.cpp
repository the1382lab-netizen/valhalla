// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaCore.h"

#define LOCTEXT_NAMESPACE "FValhallaCoreModule"

DEFINE_LOG_CATEGORY(LogValhallaCore);

void FValhallaCoreModule::StartupModule()
{
	UE_LOG(LogValhallaCore, Log, TEXT("ValhallaCore started."));
}

void FValhallaCoreModule::ShutdownModule()
{
	UE_LOG(LogValhallaCore, Log, TEXT("ValhallaCore shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FValhallaCoreModule, ValhallaCore)
