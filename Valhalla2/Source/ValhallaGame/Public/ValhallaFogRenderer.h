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
class UStaticMeshComponent;
class ULevel;

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
 *
 * B-27 Phase 5 (it cost 1.9 ms a frame in the walled village, redoing
 * everything every frame):
 *
 *   - the visible area is recomputed only when the pawn has moved
 *     RecomputeDistanceCm, the vision range or the zone changed, the walls
 *     changed (a level streamed in or out, MarkBlockersDirty), or a movable
 *     blocker is in range (NeedsRecompute);
 *   - the wall segments come from a cache kept per level (built once when the
 *     level is added to the world, dropped when it is removed), not from an
 *     overlap query every frame; so B-24's zone streaming needs nothing extra;
 *   - the visible mask is redrawn with FastUpdateResource and only with a new
 *     polygon; the explored mask likewise;
 *   - there is an explored mask per zone (ExploredByZone), so walking back into
 *     a zone, or B-24 unloading and reloading it, brings its explored fog back.
 *
 * `valhalla.FogAlwaysRecompute 1` restores the old behaviour (overlap query
 * and redraw every frame) for comparing the two. CSV stat Valhalla/FogTick.
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

	/**
	 * B-06: the zone's vision fog, pushed by AValhallaZoneAtmosphere every
	 * frame — the player's ground position, where the fog starts, how wide its
	 * fade is, its colour, and a 0..1 strength. Strength 0 is off, and is also
	 * PP_Fog's own default, so a zone without an atmosphere profile (and a
	 * PP_Fog built before B-06, which has no such parameters) renders exactly
	 * as before.
	 */
	void SetVisionFog(const FVector2D& Centre, float ClearRadiusCm, float FadeWidthCm, const FLinearColor& Colour, float Strength);

	/**
	 * B-06: firelight through the vision fog. Strength multiplies the glow of
	 * every beacon light (0 = off, PP_Fog's default); glows fade out beyond
	 * RangeCm of ground distance from the player. Only ever visible where the
	 * vision fog is, because PP_Fog multiplies it by the fog's own alpha.
	 *
	 * Beacons are the light actors tagged `ValhallaFireLight` (fire_lights.py:
	 * campfires, cooking fires, braziers, fireplaces), `ValhallaLampLight`
	 * (lamp_lights.py: lamp posts) or `ValhallaBeacon` (anything placed by hand,
	 * e.g. a torch). Each is drawn once into a world-space glow mask — a soft
	 * disc the size of its attenuation radius, in its own colour, weighted by
	 * its intensity — which PP_Fog adds on top of the fog and uses to thin the
	 * fog over it, so the flame and the lit ground show through as a glow.
	 * The actor that carries a light (an NPC with a torch) is still hidden by
	 * AValhallaZoneAtmosphere; only tagged level lights are beacons.
	 */
	void SetFirelight(float Strength, float RangeCm);

	/** Actor tags that make a light a beacon. */
	static constexpr const TCHAR* FireLightTag = TEXT("ValhallaFireLight");
	static constexpr const TCHAR* LampLightTag = TEXT("ValhallaLampLight");
	static constexpr const TCHAR* BeaconTag = TEXT("ValhallaBeacon");

	/** A campfire's 70 cd is a beacon of weight 1; weight goes as sqrt(cd / this). */
	static constexpr float BeaconReferenceCandelas = 70.f;

	/** Lights lighter than this (candles, an altar) are not beacons. */
	static constexpr float BeaconMinWeight = 0.3f;

	/** How often the beacon list and the glow mask are rebuilt, seconds. */
	static constexpr float BeaconRefreshSeconds = 2.f;

	/** `/Game/Valhalla/Materials/PP_Fog`. Authored by `build_fog.py`. */
	static constexpr const TCHAR* FogMaterialPath = TEXT("/Game/Valhalla/Materials/PP_Fog");

	// ── B-27 Phase 5 ─────────────────────────────────────────────────────

	/** How far the pawn has to move (2D) before the visible area is recomputed, cm. */
	static constexpr float RecomputeDistanceCm = 10.f;

	/** What the last visible area was computed for. */
	struct FFogKey
	{
		FVector2D Origin = FVector2D::ZeroVector;
		float Range = 0.f;
		FBox2D Bounds = FBox2D(ForceInit);
		uint32 BlockerGeneration = 0;
	};

	/**
	 * Recompute? True with no previous key, when the origin moved
	 * RecomputeDistanceCm or more, the range, bounds or blocker generation
	 * changed, or a movable blocker was in range last time. Pure; tested.
	 */
	static bool NeedsRecompute(const FFogKey* Last, const FFogKey& Now, bool bMovableBlockerInRange);

	/** One vision blocker in the cache: its world bounds and its footprint's four edges. */
	struct FBlockerEntry
	{
		TWeakObjectPtr<const UStaticMeshComponent> Component;
		FBox Bounds = FBox(ForceInit);
		FValhallaVisibilitySegment Segments[4];
		/** Movable: its edges are re-read from the component at every gather. */
		bool bMovable = false;
	};

	/** The four XY edges of a mesh's local box pushed through a transform (the old overlap path's shape). */
	static void MakeBoxSegments(const FBox& LocalBox, const FTransform& ToWorld, FValhallaVisibilitySegment OutSegments[4]);

	/**
	 * The segments of every entry whose bounds come within Range of Centre (3D,
	 * as the overlap sphere did). bOutMovable: a movable one was among them.
	 * Pure; tested.
	 */
	static void SelectSegmentsInRange(TArrayView<const FBlockerEntry> Entries, const FVector& Centre, float Range,
		TArray<FValhallaVisibilitySegment>& OutSegments, bool& bOutMovable);

	/** The walls changed without a level streaming (B-23's doors): rebuild the cache and recompute. */
	void MarkBlockersDirty();

protected:
	/** UCanvasRenderTarget2D's update hook. Draws PendingTriangles. */
	UFUNCTION()
	void DrawVisibleMask(UCanvas* Canvas, int32 Width, int32 Height);

	/** GlowRT's update hook: one additive soft disc per beacon. */
	UFUNCTION()
	void DrawGlowMask(UCanvas* Canvas, int32 Width, int32 Height);

	/** Rebuild Beacons from the tagged lights inside FogBounds. */
	void GatherBeacons();

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
	 * Point the masks at the zone the pawn is in (B-27: and at that zone's own
	 * explored mask; the paragraphs below describe the one shared mask before it).
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

	/** B-06: the beacon glows, world space over FogBounds like the other masks. */
	UPROPERTY(Transient)
	TObjectPtr<UCanvasRenderTarget2D> GlowRT;

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

	// ── B-27 Phase 5 ─────────────────────────────────────────────────────

	/** Every loaded level's vision blockers. Rebuilt lazily when bBlockerCacheDirty. */
	TMap<TWeakObjectPtr<ULevel>, TArray<FBlockerEntry>> BlockersByLevel;
	/** Flat copy of BlockersByLevel's entries for the per-gather scan. */
	TArray<FBlockerEntry> BlockerEntries;
	bool bBlockerCacheDirty = true;
	/** Bumped whenever the cache changes, so NeedsRecompute sees new walls. */
	uint32 BlockerGeneration = 1;

	void RebuildBlockerCacheIfDirty();
	static void CacheLevel(const ULevel* Level, TArray<FBlockerEntry>& OutEntries);
	void HandleLevelAdded(ULevel* Level, UWorld* World);
	void HandleLevelRemoved(ULevel* Level, UWorld* World);
	FDelegateHandle LevelAddedHandle;
	FDelegateHandle LevelRemovedHandle;

	FFogKey LastKey;
	bool bHaveLastKey = false;
	bool bLastHadMovableBlocker = false;

	/** The explored mask for each zone this session has seen. ExploredRT is the current zone's. */
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UTextureRenderTarget2D>> ExploredByZone;

	/** ExploredRT := the zone's mask (made, black, the first time), bound to the material. */
	void SelectExploredMask(FName ZoneId);

	/** One beacon light, as the glow mask draws it. */
	struct FBeacon
	{
		FVector2D Location = FVector2D::ZeroVector;
		float RadiusCm = 0.f;
		/** Light colour times the beacon's weight. */
		FLinearColor Colour = FLinearColor::Black;
	};

	TArray<FBeacon> Beacons;

	/** The last strength SetFirelight was given. 0: the glow mask is not maintained. */
	float FirelightStrength = 0.f;

	/** Seconds since the glow mask was last rebuilt. */
	float BeaconRefreshAccumulator = 0.f;

	/** The bounds the glow mask was drawn for; a zone change redraws it at once. */
	FBox2D GlowBounds = FBox2D(ForceInit);
};
