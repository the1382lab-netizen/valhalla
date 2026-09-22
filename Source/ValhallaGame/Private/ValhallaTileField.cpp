// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaTileField.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"

AValhallaTileField::AValhallaTileField()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetCanBeDamaged(false);

	Tiles = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Tiles"));
	SetRootComponent(Tiles);

	// A floor has to hold a character up, so it collides — but with `BlockAll`,
	// not with `VisionBlocker`. The `VisionBlocker` trace channel's default
	// response is Ignore (Config/DefaultEngine.ini), so a `BlockAll` floor is
	// invisible to the line-of-sight trace without anything here saying so.
	// That is the property that makes instancing the floors safe at all; see
	// the class comment.
	Tiles->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Tiles->SetCollisionProfileName(TEXT("BlockAll"));
	Tiles->SetMobility(EComponentMobility::Static);

	// The field is authored flat and never moves, so nothing needs to be
	// recomputed per frame; and a floor casting shadows onto itself is
	// a cost with no visual return in a flat-shaded top-down game.
	Tiles->SetCastShadow(false);

	// ── Phase 8a: tell PP_Outline this is a floor ────────────────────────
	//
	// The Sobel outline finds a silhouette wherever depth steps, and a 64 cm
	// tile grid seen at 45 degrees steps at every seam: the first pass drew a
	// black line around all 4096 tiles and the town looked like graph paper.
	// Raising the threshold until the seams vanished also took the outline off
	// the characters, because a seam between two tiles and the edge of a
	// character are the same size of depth step at this camera distance. There
	// is no threshold that separates them, so the material is *told* instead.
	//
	// Every tile field writes stencil value 1 (ValhallaStencil::Floor). The
	// outline ignores an edge only where *both* of its taps are floor, which is
	// the exact rule that keeps the field's outer boundary — floor against
	// grass, floor against a wall — while dropping every seam inside it.
	//
	// bRenderCustomDepth on an instanced component covers every instance; there
	// is no per-instance stencil and none is wanted, because every tile in a
	// field is the same kind of thing.
	Tiles->SetRenderCustomDepth(true);
	Tiles->SetCustomDepthStencilValue(ValhallaStencil::Floor);
}

void AValhallaTileField::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (Tiles)
	{
		Tiles->SetRenderCustomDepth(true);
		Tiles->SetCustomDepthStencilValue(ValhallaStencil::Floor);
	}
}

void AValhallaTileField::SetTileMesh(UStaticMesh* Mesh)
{
	if (Tiles)
	{
		Tiles->SetStaticMesh(Mesh);
	}
}

int32 AValhallaTileField::AddTiles(const TArray<FTransform>& Transforms)
{
	if (!Tiles)
	{
		return 0;
	}

	// `bWorldSpace = false`: the transforms are in the component's local space,
	// which for a field placed at the zone's origin makes an instance's
	// translation the tile's zone-local position. A builder that had to
	// pre-multiply by the actor transform would be one refactor away from
	// putting the desert's floor under the grasslands.
	Tiles->AddInstances(Transforms, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ false);

	return Tiles->GetInstanceCount();
}

void AValhallaTileField::ClearTiles()
{
	if (Tiles)
	{
		Tiles->ClearInstances();
	}
}

int32 AValhallaTileField::GetTileCount() const
{
	return Tiles ? Tiles->GetInstanceCount() : 0;
}
