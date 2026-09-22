// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaPortal.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "ValhallaGame.h"
#include "ValhallaZoneSubsystem.h"

AValhallaPortal::AValhallaPortal()
{
	PrimaryActorTick.bCanEverTick = false;

	// The actor is placed in the level, so it exists everywhere already; what
	// it *does* is a server-side teleport, and that replicates as movement.
	bReplicates = false;
	SetCanBeDamaged(false);

	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	SetRootComponent(Trigger);
	Trigger->SetBoxExtent(TriggerExtent, /*bUpdateOverlaps*/ false);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionObjectType(ECC_WorldStatic);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);
	Trigger->SetHiddenInGame(true);
	Trigger->ShapeColor = FColor(200, 80, 255);

	Marker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Marker"));
	Marker->SetupAttachment(Trigger);
	// Nothing collides with the marker, and above all it must not block the
	// VisionBlocker trace: a portal you cannot see through would put a hole in
	// the fog of war at the one place a player is looking.
	Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Marker->SetCollisionProfileName(TEXT("NoCollision"));

	// The mesh is assigned in OnConstruction, not here: a constructor also runs
	// for the class default object during module startup, when the content
	// browser is not necessarily up, and an asset load there is a warning at
	// best.
}

void AValhallaPortal::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (Trigger)
	{
		Trigger->SetBoxExtent(TriggerExtent, /*bUpdateOverlaps*/ false);
	}

	// Late-loaded, because a portal placed by a Python builder is constructed
	// before the props have necessarily been cooked into memory.
	if (Marker && !Marker->GetStaticMesh())
	{
		if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, MarkerMeshPath))
		{
			Marker->SetStaticMesh(Mesh);
		}
	}
}

void AValhallaPortal::BeginPlay()
{
	Super::BeginPlay();

	if (Trigger)
	{
		Trigger->OnComponentBeginOverlap.AddDynamic(this, &AValhallaPortal::HandleOverlap);
	}

	if (!HasAuthority())
	{
		return;
	}

	if (TargetZoneId.IsNone())
	{
		UE_LOG(LogValhallaGame, Warning,
			TEXT("portal %s has no TargetZoneId and will never do anything."), *GetName());
	}
}

void AValhallaPortal::HandleOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/,
	AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/,
	int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/,
	const FHitResult& /*SweepResult*/)
{
	if (!HasAuthority())
	{
		return;
	}

	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn || !Pawn->IsPlayerControlled())
	{
		// NPCs do not travel. 1.0's NPCs were per-zone arrays and could not
		// have; 2.0's could, and must not: an NPC leashed to a home point in
		// the grasslands has no business being dragged into the desert.
		return;
	}

	UWorld* World = GetWorld();
	UValhallaZoneSubsystem* Zones = World ? World->GetSubsystem<UValhallaZoneSubsystem>() : nullptr;
	if (!Zones)
	{
		return;
	}

	Zones->TravelThroughPortal(Pawn, this);
}
