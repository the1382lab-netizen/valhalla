// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaFogBounds.h"

#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "ValhallaVisibilitySubsystem.h"

AValhallaFogBounds::AValhallaFogBounds()
{
	PrimaryActorTick.bCanEverTick = false;

	// Never sent to a client: the fog renderer reads it out of its own world,
	// which for a level-placed actor is the copy the client loaded off disk.
	bReplicates = false;

	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	SetRootComponent(Box);
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Box->SetBoxExtent(Extent);
	Box->SetHiddenInGame(true);
	Box->ShapeColor = FColor(64, 160, 255);
}

void AValhallaFogBounds::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (Box)
	{
		Box->SetBoxExtent(Extent);
	}
}

FBox2D AValhallaFogBounds::GetFogBounds2D() const
{
	// The actor's scale is honoured, so a designer can drag the handles as well
	// as type into Extent and get the same answer either way.
	const FVector Scale = GetActorScale3D();
	const FVector2D Half(
		FMath::Abs(Extent.X * Scale.X),
		FMath::Abs(Extent.Y * Scale.Y));

	const FVector Centre = GetActorLocation();
	const FVector2D Centre2D(Centre.X, Centre.Y);

	return FBox2D(Centre2D - Half, Centre2D + Half);
}

AValhallaFogBounds* AValhallaFogBounds::Find(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AValhallaFogBounds> It(const_cast<UWorld*>(World)); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

FBox2D AValhallaFogBounds::ComputeFallbackBounds(const UWorld* World)
{
	FBox2D Bounds(ForceInit);
	if (!World)
	{
		return Bounds;
	}

	for (TActorIterator<AStaticMeshActor> It(const_cast<UWorld*>(World)); It; ++It)
	{
		const UStaticMeshComponent* Component = It->GetStaticMeshComponent();
		const UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
		if (!Mesh)
		{
			continue;
		}

		const FString MeshName = Mesh->GetName();
		if (!MeshName.StartsWith(TEXT("VB_")) && !MeshName.StartsWith(TEXT("SM_")))
		{
			continue;
		}

		const FBox ActorBounds = It->GetComponentsBoundingBox(/*bNonColliding*/ true);
		Bounds += FVector2D(ActorBounds.Min.X, ActorBounds.Min.Y);
		Bounds += FVector2D(ActorBounds.Max.X, ActorBounds.Max.Y);
	}

	if (!Bounds.bIsValid)
	{
		UE_LOG(LogValhallaVision, Warning,
			TEXT("%s has no fog bounds actor and no VB_/SM_ static meshes to fall back on; fog is disabled."),
			*World->GetName());
		return Bounds;
	}

	const FVector2D Margin = Bounds.GetSize() * (FallbackMargin * 0.5f);
	Bounds = FBox2D(Bounds.Min - Margin, Bounds.Max + Margin);

	UE_LOG(LogValhallaVision, Log,
		TEXT("%s has no AValhallaFogBounds; falling back to the VB_/SM_ bounds (%.0f, %.0f) .. (%.0f, %.0f)"),
		*World->GetName(), Bounds.Min.X, Bounds.Min.Y, Bounds.Max.X, Bounds.Max.Y);

	return Bounds;
}
