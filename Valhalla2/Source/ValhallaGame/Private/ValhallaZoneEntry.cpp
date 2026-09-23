// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaZoneEntry.h"

#include "Components/ArrowComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

AValhallaZoneEntry::AValhallaZoneEntry()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetCanBeDamaged(false);

	Arrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	SetRootComponent(Arrow);
	Arrow->SetHiddenInGame(true);
	Arrow->ArrowColor = FColor(80, 180, 255);
	Arrow->ArrowSize = 3.f;
}

AValhallaZoneEntry* AValhallaZoneEntry::Find(const UWorld* World, FName InEntryId)
{
	if (!World || InEntryId.IsNone())
	{
		return nullptr;
	}

	for (TActorIterator<AValhallaZoneEntry> It(World); It; ++It)
	{
		if (It->EntryId == InEntryId)
		{
			return *It;
		}
	}
	return nullptr;
}
