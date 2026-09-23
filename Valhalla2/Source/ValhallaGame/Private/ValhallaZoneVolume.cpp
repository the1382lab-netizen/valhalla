// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaZoneVolume.h"

#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

AValhallaZoneVolume::AValhallaZoneVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	// Never replicated: a level actor is already in every client's world, and
	// a zone's box is authoring data that cannot change at runtime.
	bReplicates = false;
	SetCanBeDamaged(false);

	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	SetRootComponent(Box);
	Box->SetBoxExtent(Extent, /*bUpdateOverlaps*/ false);
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Box->SetGenerateOverlapEvents(false);
	Box->SetHiddenInGame(true);
	Box->ShapeColor = FColor(60, 200, 120);

	DefaultSpawn = CreateDefaultSubobject<UArrowComponent>(TEXT("DefaultSpawn"));
	DefaultSpawn->SetupAttachment(Box);
	DefaultSpawn->SetHiddenInGame(true);
	DefaultSpawn->ArrowColor = FColor(255, 210, 60);
	DefaultSpawn->ArrowSize = 4.f;
}

void AValhallaZoneVolume::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (Box)
	{
		Box->SetBoxExtent(Extent, /*bUpdateOverlaps*/ false);
	}
}

FBox AValhallaZoneVolume::GetZoneBounds() const
{
	if (!Box)
	{
		return FBox(ForceInit);
	}

	// The component's own scaled extent about its world location, rather than
	// Box->Bounds.GetBox(): the latter is the *rendered* bounds and a
	// UBoxComponent pads them, which would quietly make every zone a few
	// centimetres larger than authored and make two adjacent zones overlap.
	const FVector Centre = Box->GetComponentLocation();
	const FVector Half = Box->GetScaledBoxExtent();
	return FBox(Centre - Half, Centre + Half);
}

FValhallaZoneDef AValhallaZoneVolume::ToZoneDef() const
{
	FValhallaZoneDef Def;
	Def.ZoneId = ZoneId;
	Def.DisplayName = DisplayName.IsEmpty() ? ZoneId.ToString() : DisplayName;
	Def.Bounds = GetZoneBounds();
	Def.Volume = const_cast<AValhallaZoneVolume*>(this);

	if (DefaultSpawn)
	{
		Def.DefaultSpawn = DefaultSpawn->GetComponentLocation();
		Def.DefaultSpawnYaw = DefaultSpawn->GetComponentRotation().Yaw;
	}
	else
	{
		Def.DefaultSpawn = Def.Bounds.GetCenter();
	}

	return Def;
}

void AValhallaZoneVolume::FindAll(const UWorld* World, TArray<AValhallaZoneVolume*>& OutVolumes)
{
	OutVolumes.Reset();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AValhallaZoneVolume> It(World); It; ++It)
	{
		OutVolumes.Add(*It);
	}
}

AValhallaZoneVolume* AValhallaZoneVolume::Find(const UWorld* World, FName InZoneId)
{
	if (!World || InZoneId.IsNone())
	{
		return nullptr;
	}

	for (TActorIterator<AValhallaZoneVolume> It(World); It; ++It)
	{
		if (It->ZoneId == InZoneId)
		{
			return *It;
		}
	}
	return nullptr;
}
