// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Canvas.h"
#include "GameFramework/Actor.h"
#include "ValhallaVisibilitySubsystem.h"
#include "ValhallaFogRenderer.generated.h"

class AValhallaPlayerController;
class UCameraComponent;
class UCanvasRenderTarget2D;
class UMaterialInstanceDynamic;
class UTextureRenderTarget2D;

/**
 * The client's fog of war. The port of `client/src/systems/FogOfWar.ts`.
 *
 * 1.0 drew fog as a grid of black rectangles, one per tile, alpha chosen from a
 * `Set<string>` of explored tile keys — which is exactly what a 2D canvas
 * makes cheap and a 3D renderer makes expensive. 2.0 keeps the three states and
 * their brightnesses and changes nothing else:
 *
 *     visible   x1.0     inside this frame's visibility polygon
 *     explored  x0.5     inside some previous frame's polygon
 *     hidden    x0.05    never seen
 *
 * (1.0 expressed them as fog alphas — 0, 0.5, 0.95 — over a black overlay,
 * which is the same three numbers written from the other end.)
 *
 * The mechanism is two 1024x1024 masks in *world* space, stretched over the
 * level's AValhallaFogBounds, plus a post-process material that reads world XY
 * back out of the scene depth and looks the pixel up in them. World space
 * rather than screen space is what makes the explored mask a permanent record:
 * a screen-space mask would have to be reprojected every time the camera moved
 * and would smear.
 *
 * Client-only, and spawned by the local AValhallaPlayerController rather than
 * placed in the level or attached to the pawn:
 *
 *   - Not in the level, because a dedicated server would then run it, and it is
 *     pure presentation — the server's own culling is the trace in
 *     UValhallaVisibilitySubsystem and shares nothing with this but the
 *     geometry.
 *   - Not on the pawn, because a pawn dies. Everything a player has explored
 *     would be forgotten on every respawn, which is precisely the opposite of
 *     what "explored" means. The controller outlives the pawn, so this does.
 *
 * The explored mask therefore lasts for the session and resets on a level
 * change, because the actor and its render targets go with the world.
 */
UCLASS(NotPlaceable)
class VALHALLAGAME_API AValhallaFogRenderer : public AActor
{
	GENERATED_BODY()

public:
	AValhallaFogRenderer();

	//~ Begin AActor interface
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End AActor interface

	/**
	 * Spawn the fog for a local controller, or return the one it already has.
	 * Does nothing without a local controller, which is what keeps the server
	 * and the simulated proxies out of it.
	 */
	static AValhallaFogRenderer* EnsureFor(AValhallaPlayerController* Controller);

	/** Edge of the render targets, texels. Square. */
	static constexpr int32 MaskResolution = 1024;

	/** `/Game/Valhalla/Materials/PP_Fog`. Authored by `build_fog.py`. */
	static constexpr const TCHAR* FogMaterialPath = TEXT("/Game/Valhalla/Materials/PP_Fog");

protected:
	/** UCanvasRenderTarget2D's update hook. Draws PendingTriangles. */
	UFUNCTION()
	void DrawVisibleMask(UCanvas* Canvas, int32 Width, int32 Height);

	/**
	 * Every VisionBlocker within Range of Centre, as 2D footprint edges.
	 *
	 * One overlap on the VisionBlocker channel and then, per hit static mesh
	 * component, the four XY edges of its mesh bounding box pushed through the
	 * component transform. The oriented box rather than the axis-aligned world
	 * bounds, because `VB_StoneWall_Straight` is 64 x 25 and half the walls in
	 * a level are rotated 90 degrees: the AABB of a diagonal wall would be a
	 * square three times too wide and would shadow ground that is in plain
	 * sight.
	 *
	 * This is the 2.0 replacement for 1.0's `extractWallSegments`, which read a
	 * tile collision grid it no longer has. The segments it produces are the
	 * same shape of data and go into the same algorithm.
	 */
	void GatherBlockerSegments(const FVector& Centre, float Range, TArray<FValhallaVisibilitySegment>& OutSegments) const;

	/** Put PP_Fog on the pawn's camera, after PP_Outline. Idempotent. */
	void ApplyFogPostProcess(UCameraComponent* Camera);

	/** World XY -> mask texel. */
	FVector2D WorldToMask(const FVector2D& World) const;

	/**
	 * Point the masks at the zone the pawn is in, clearing what was explored.
	 *
	 * Phase 3 made the fog per *zone* rather than per level, because one level
	 * now holds every zone: `L_World` streams the grasslands at the origin and
	 * the desert 40000 cm away, so a single pair of masks stretched over both
	 * would spend most of its 1024 texels on the empty ground between them and
	 * give each zone about a fifth of the resolution it had.
	 *
	 * The explored mask is reset on every change, and that is the intended
	 * behaviour rather than a limitation being accepted: the mask is one square
	 * of world, so keeping the desert's exploration while rendering the
	 * grasslands would mean reading the grasslands through the desert's record.
	 * A player who walks back through the portal explores again. Remembering
	 * both would need a mask per zone, which is Phase 8's problem if anyone
	 * ever minds.
	 *
	 * Falls back the way it always did — `AValhallaFogBounds`, then the
	 * `VB_`/`SM_` union — so `L_GreyBox` and `L_LoSTest`, which have no zone
	 * volumes, behave exactly as they did in Phase 5.
	 *
	 * Returns false when there is no usable rectangle at all.
	 */
	bool ResolveBoundsForPawn(const AActor* Pawn);

	/** Push FogBounds onto the material. Called after every resolve. */
	void ApplyBoundsToMaterial();

	/** Build the triangle fan for one polygon and leave it in PendingTriangles. */
	void BuildTriangles(const FVector2D& Origin, const TArray<FVector2D>& Polygon);

	/** This frame's polygon, white on black. Cleared by the canvas every update. */
	UPROPERTY(Transient)
	TObjectPtr<UCanvasRenderTarget2D> VisibleRT;

	/**
	 * The union of every frame's polygon so far.
	 *
	 * A plain render target drawn without a clear, which for a 0-or-1 mask is
	 * exactly `max(Explored, Visible)` — the accumulation the design asks for —
	 * and avoids the ping-pong a real max() pass would need, because a shader
	 * cannot read and write one render target in the same draw.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> ExploredRT;

	/** PP_Fog, with the two masks and the bounds bound to it. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FogMaterial;

	/** The controller that spawned this. Weak: it is not our lifetime. */
	UPROPERTY(Transient)
	TWeakObjectPtr<AValhallaPlayerController> OwningController;

	/** The camera PP_Fog is currently on, so a respawn re-applies it once. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCameraComponent> AppliedCamera;

private:
	/** The current zone's fog bounds. Re-resolved whenever the zone changes. */
	FBox2D FogBounds = FBox2D(ForceInit);

	/**
	 * Which zone FogBounds belongs to, or None for a level with no zones.
	 *
	 * The client's own answer, read off the pawn's position against the zone
	 * volumes in its copy of the level — not the replicated
	 * `PlayerState::ZoneId`. Those two agree within a frame, and using the
	 * local one means the fog snaps at the same instant the pawn does instead
	 * of a round trip later.
	 */
	FName CurrentZoneId;

	/** This frame's fan, handed to the canvas callback. */
	TArray<FCanvasUVTri> PendingTriangles;

	/** Scratch, kept between frames so the per-frame pass does not allocate. */
	TArray<FValhallaVisibilitySegment> SegmentScratch;
};
