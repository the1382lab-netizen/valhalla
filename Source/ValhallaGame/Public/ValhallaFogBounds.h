// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ValhallaFogBounds.generated.h"

class UBoxComponent;

/**
 * The rectangle of world the fog render targets are stretched over.
 *
 * The fog is two 1024x1024 masks in world space, not screen space, so
 * something has to say which square of centimetres those 1024 texels cover.
 * That is a level authoring decision — it is the level designer who knows where
 * their map stops — so it is an actor a level carries rather than a number in
 * an ini, and `build_lostest.py` and `build_greybox.py` each place exactly one.
 *
 * Only the XY footprint matters; the box's height is there so the actor is
 * visible and grabbable in the editor viewport. Bigger is not free: the whole
 * box maps onto 1024 texels, so a box twice the size of the playable area
 * halves the fog's resolution. One tile (64 cm) of margin is plenty.
 *
 * Not replicated and not spawned at runtime. The fog is client presentation;
 * the server's own culling is the trace in UValhallaVisibilitySubsystem and
 * does not consult this at all.
 */
UCLASS(Blueprintable)
class VALHALLAGAME_API AValhallaFogBounds : public AActor
{
	GENERATED_BODY()

public:
	AValhallaFogBounds();

	//~ Begin AActor interface
	virtual void OnConstruction(const FTransform& Transform) override;
	//~ End AActor interface

	/**
	 * Half-size of the covered rectangle, cm.
	 *
	 * An EditAnywhere property pushed onto the box in OnConstruction rather
	 * than the box's own extent, because a default subobject's extent is
	 * awkward to set from the Python level builders and this is not: it is one
	 * `set_editor_property('extent', ...)` and it survives a re-save.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Fog")
	FVector Extent = FVector(1280.f, 1280.f, 400.f);

	/** The editor handle. Query-only; nothing collides with the fog. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Fog")
	TObjectPtr<UBoxComponent> Box;

	/** The covered rectangle in world XY. */
	FBox2D GetFogBounds2D() const;

	/** The first fog bounds in a world, or null. */
	static AValhallaFogBounds* Find(const UWorld* World);

	/**
	 * The fallback when a level has no fog bounds actor: the union of every
	 * static mesh actor built from a `VB_` (vision blocker) or `SM_` (kit)
	 * mesh, grown by ten per cent.
	 *
	 * Deliberately naming-based, and that naming rule is the Phase 3 contract —
	 * see PLAN.md. An importer that keeps the kit's mesh names gets working fog
	 * bounds for free even before anybody remembers to drop the actor in.
	 */
	static FBox2D ComputeFallbackBounds(const UWorld* World);

	/** Margin the fallback adds on each side, as a fraction of the size. */
	static constexpr float FallbackMargin = 0.1f;
};
