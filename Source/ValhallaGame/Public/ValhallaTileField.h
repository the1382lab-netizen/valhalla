// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ValhallaTileField.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;

/**
 * The custom-depth stencil values `PP_Outline` reads.
 *
 * Only one value is in use and it is written in exactly one place
 * (AValhallaTileField's constructor), but it is named here rather than left as
 * a `1` in two files — the material's HLSL compares against the same number and
 * a silent disagreement between them shows up as "the outline stopped working"
 * with nothing to grep for.
 */
namespace ValhallaStencil
{
	/** Nothing has opted in. Characters, walls, props and the sky. */
	inline constexpr int32 Default = 0;

	/** A floor tile field. An edge with floor on both sides is not an outline. */
	inline constexpr int32 Floor = 1;
}

/**
 * Every floor tile of one mesh in one zone, as a single instanced component.
 *
 * ## Why instancing, and only for floors
 *
 * A 64 x 64 tile zone is 4096 floor tiles. As individual `AStaticMeshActor`s
 * that is 4096 actors per zone, 8192 across the two — an outliner nobody can
 * read, a level that takes a minute to load, and 8192 draw calls' worth of
 * per-actor overhead for geometry that is literally flat.
 *
 * Walls, buildings and props stay individual actors, and that is not
 * inconsistency. Phase 5's line of sight reads the *component* bounding box of
 * every `VisionBlocker` it overlaps (`GatherBlockerSegments`), and an instanced
 * component has one bounding box for the whole field: instancing the walls
 * would hand the visibility polygon a single box covering the entire zone and
 * the fog would hide everything. Floors never block sight — the
 * `VisionBlocker` channel's default response is Ignore, so a floor is
 * transparent to the trace without being touched — so there is nothing for
 * instancing to break. The rule is exactly "instance what cannot block sight".
 *
 * ## Why a C++ actor rather than a Python-built one
 *
 * `AActor::AddComponentByClass` is not exposed to Python and UE ships no
 * generic instanced-static-mesh *actor*, so a Python level builder has no way
 * to make one. It could go through a Blueprint, but then the level's geometry
 * would depend on an asset whose contents are not in source control in any
 * readable form. This is four properties and two functions, and it means
 * `build_grasslands.py` can say what it means.
 *
 * Not replicated: it is level geometry, present in every client's copy of the
 * level before anybody connects.
 */
UCLASS(Blueprintable)
class VALHALLAGAME_API AValhallaTileField : public AActor
{
	GENERATED_BODY()

public:
	AValhallaTileField();

	/**
	 * Re-apply the outline stencil.
	 *
	 * The constructor sets it, which covers a field spawned from script. A
	 * field that was *placed and saved* before Phase 8a existed serialised its
	 * component against the old defaults, and whether that delta comes back is
	 * a question about property serialisation nobody should have to answer
	 * while looking at a town covered in tile seams. OnConstruction runs in the
	 * editor and on spawn, so it settles the question for both.
	 */
	virtual void OnConstruction(const FTransform& Transform) override;

	/** The field. Instances are in this component's local space. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Tiles")
	TObjectPtr<UInstancedStaticMeshComponent> Tiles;

	/** Which mesh this field is made of. Set before adding instances. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Valhalla|Tiles")
	void SetTileMesh(UStaticMesh* Mesh);

	/**
	 * Add tiles. Returns how many the field holds afterwards.
	 *
	 * Takes the whole array rather than one transform at a time because the
	 * per-call cost is a render-state rebuild: 4096 single `AddInstance` calls
	 * from Python take minutes, one batched call takes no measurable time.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Valhalla|Tiles")
	int32 AddTiles(const TArray<FTransform>& Transforms);

	/** Empty the field, so a rebuild does not double it. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Valhalla|Tiles")
	void ClearTiles();

	/** How many instances the field holds. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Tiles")
	int32 GetTileCount() const;
};
