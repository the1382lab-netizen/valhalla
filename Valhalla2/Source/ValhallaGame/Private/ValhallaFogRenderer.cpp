// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaFogRenderer.h"

#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Engine/Level.h"
#include "HAL/IConsoleManager.h"
#include "Camera/CameraComponent.h"
#include "CanvasItem.h"
#include "Components/LightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "Engine/Light.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "EngineUtils.h"
#include "RenderUtils.h"
#include "ValhallaCharacter.h"
#include "ValhallaFogBounds.h"
#include "ValhallaPlayerController.h"
#include "ValhallaPlayerState.h"
#include "ValhallaZoneAtmosphere.h"
#include "ValhallaZoneSubsystem.h"

CSV_DECLARE_CATEGORY_EXTERN(Valhalla);

namespace
{
	/**
	 * `valhalla.FogAlwaysRecompute` — B-27 Phase 5. 0 (default): recompute the
	 * visible area only when something changed, from the per-level blocker
	 * cache. 1: the old way, an overlap query and both masks redrawn every
	 * frame, for comparing the two.
	 */
	TAutoConsoleVariable<int32> CVarFogAlwaysRecompute(
		TEXT("valhalla.FogAlwaysRecompute"),
		0,
		TEXT("Client. 1: recompute the fog's visible area every frame with an overlap query (before B-27 Phase 5). 0: only when the pawn moved, the walls or the zone changed."),
		ECVF_Default);

	/** Material parameter names. They have to match what `build_fog.py` authors. */
	const FName ParamVisibleMask(TEXT("VisibleMask"));
	const FName ParamExploredMask(TEXT("ExploredMask"));
	const FName ParamFogOrigin(TEXT("FogOrigin"));
	const FName ParamFogInvSize(TEXT("FogInvSize"));

	/** B-06's vision fog. Also build_fog.py's names. */
	const FName ParamVisionCentre(TEXT("VisionCentre"));
	const FName ParamVisionClearRadius(TEXT("VisionClearRadius"));
	const FName ParamVisionFadeWidth(TEXT("VisionFadeWidth"));
	const FName ParamVisionFogColor(TEXT("VisionFogColor"));
	const FName ParamVisionFogStrength(TEXT("VisionFogStrength"));
	const FName ParamGlowMask(TEXT("GlowMask"));
	const FName ParamFirelightStrength(TEXT("FirelightStrength"));
	const FName ParamFirelightRange(TEXT("FirelightRange"));
}

AValhallaFogRenderer::AValhallaFogRenderer()
{
	PrimaryActorTick.bCanEverTick = true;
	// Post-physics, so the polygon is built from where the pawn actually ended
	// the frame rather than from where it was before movement ran.
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	bReplicates = false;
	SetHidden(true);
	SetCanBeDamaged(false);

	// Nothing about this actor is in the world; it is a pair of render targets
	// with a tick. A root component is still needed for a spawnable actor.
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

AValhallaFogRenderer* AValhallaFogRenderer::EnsureFor(AValhallaPlayerController* Controller)
{
	if (!Controller || !Controller->IsLocalController())
	{
		return nullptr;
	}

	UWorld* World = Controller->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// One per controller. Searching AActor::Children — the actors this one owns
	// — rather than keeping a pointer on the controller keeps
	// AValhallaPlayerController's header free of Phase 5: the controller's
	// whole contribution is the one call to this function.
	for (AActor* Child : Controller->Children)
	{
		if (AValhallaFogRenderer* Existing = Cast<AValhallaFogRenderer>(Child))
		{
			return Existing;
		}
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Controller;
	SpawnParams.ObjectFlags |= RF_Transient;

	AValhallaFogRenderer* Renderer = World->SpawnActor<AValhallaFogRenderer>(
		AValhallaFogRenderer::StaticClass(), FTransform::Identity, SpawnParams);
	if (Renderer)
	{
		Renderer->OwningController = Controller;
	}
	return Renderer;
}

void AValhallaFogRenderer::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!OwningController.IsValid())
	{
		OwningController = Cast<AValhallaPlayerController>(GetOwner());
	}

	// B-27 Phase 5: the blocker cache follows the levels in and out of the world.
	LevelAddedHandle = FWorldDelegates::LevelAddedToWorld.AddUObject(this, &AValhallaFogRenderer::HandleLevelAdded);
	LevelRemovedHandle = FWorldDelegates::LevelRemovedFromWorld.AddUObject(this, &AValhallaFogRenderer::HandleLevelRemoved);

	// B-06: the zone atmosphere rides along with the fog — same owner, same
	// client-only life — and is spawned before any of the early returns below,
	// because a level with no fog bounds still has lighting to switch.
	AValhallaZoneAtmosphere::EnsureFor(OwningController.Get());

	// ── Where the masks live in the world ───────────────────────────────
	//
	// Resolved against the pawn if there is one already, and re-resolved every
	// time the pawn crosses into a different zone. On BeginPlay the controller
	// usually has no pawn yet, in which case this settles for whatever
	// level-wide bounds exist and the first tick with a pawn corrects it.
	const APawn* InitialPawn = OwningController.IsValid() ? OwningController->GetPawn() : nullptr;
	if (!ResolveBoundsForPawn(InitialPawn) && !InitialPawn)
	{
		// No pawn *and* no level-wide bounds. Not fatal: keep ticking, because
		// the pawn is about to arrive and will bring a zone with it.
		UE_LOG(LogValhallaVision, Log,
			TEXT("fog bounds not resolvable yet in %s; waiting for a pawn."), *World->GetName());
	}
	else if (!FogBounds.bIsValid || FogBounds.GetSize().GetMin() <= 1.f)
	{
		UE_LOG(LogValhallaVision, Warning, TEXT("no usable fog bounds in %s; fog renderer idle."), *World->GetName());
		SetActorTickEnabled(false);
		return;
	}

	// ── The two masks ───────────────────────────────────────────────────
	VisibleRT = UCanvasRenderTarget2D::CreateCanvasRenderTarget2D(
		World, UCanvasRenderTarget2D::StaticClass(), MaskResolution, MaskResolution);
	if (VisibleRT)
	{
		VisibleRT->ClearColor = FLinearColor::Black;
		VisibleRT->OnCanvasRenderTargetUpdate.AddDynamic(this, &AValhallaFogRenderer::DrawVisibleMask);
	}

	// B-06: the beacon glows. Black (nothing) until a zone with vision fog
	// turns the firelight on; see SetFirelight.
	GlowRT = UCanvasRenderTarget2D::CreateCanvasRenderTarget2D(
		World, UCanvasRenderTarget2D::StaticClass(), MaskResolution, MaskResolution);
	if (GlowRT)
	{
		GlowRT->ClearColor = FLinearColor::Black;
		GlowRT->OnCanvasRenderTargetUpdate.AddDynamic(this, &AValhallaFogRenderer::DrawGlowMask);
		GlowRT->UpdateResource();
	}

	// ── The post-process material ───────────────────────────────────────
	if (UMaterialInterface* FogParent = LoadObject<UMaterialInterface>(nullptr, FogMaterialPath))
	{
		FogMaterial = UMaterialInstanceDynamic::Create(FogParent, this);
	}
	else
	{
		UE_LOG(LogValhallaVision, Warning,
			TEXT("fog material %s is missing — run build_fog.build_fog() from the editor. Fog renderer idle."),
			FogMaterialPath);
		SetActorTickEnabled(false);
		return;
	}

	FogMaterial->SetTextureParameterValue(ParamVisibleMask, VisibleRT);
	// The current zone's explored mask (made here the first time), bound to the material.
	SelectExploredMask(CurrentZoneId);
	if (GlowRT)
	{
		FogMaterial->SetTextureParameterValue(ParamGlowMask, GlowRT);
	}
	ApplyBoundsToMaterial();

	const FVector2D Size = FogBounds.GetSize();
	UE_LOG(LogValhallaVision, Log, TEXT("fog renderer up for %s over %.0f x %.0f cm at %d texels (zone '%s')"),
		OwningController.IsValid() ? *OwningController->GetName() : TEXT("?"),
		Size.X, Size.Y, MaskResolution,
		CurrentZoneId.IsNone() ? TEXT("-") : *CurrentZoneId.ToString());
}

bool AValhallaFogRenderer::ResolveBoundsForPawn(const AActor* Pawn)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// ── The zone the pawn is standing in, if the level has zones ─────────
	if (Pawn)
	{
		if (const UValhallaZoneSubsystem* Zones = World->GetSubsystem<UValhallaZoneSubsystem>())
		{
			if (const FValhallaZoneDef* Zone = Zones->GetZoneAt(Pawn->GetActorLocation()))
			{
				if (Zone->ZoneId == CurrentZoneId && FogBounds.bIsValid)
				{
					return true;
				}

				CurrentZoneId = Zone->ZoneId;
				FogBounds = Zone->GetBounds2D();

				// B-27 Phase 5: each zone keeps its own explored mask (before, one
				// mask was cleared on every zone change). Only once the material
				// exists: BeginPlay selects the first one itself.
				const bool bKnown = ExploredByZone.Contains(CurrentZoneId);
				if (FogMaterial)
				{
					SelectExploredMask(CurrentZoneId);
				}

				ApplyBoundsToMaterial();

				UE_LOG(LogValhallaVision, Log,
					TEXT("fog bounds -> zone '%s': (%.0f, %.0f) .. (%.0f, %.0f), %s explored mask"),
					*CurrentZoneId.ToString(),
					FogBounds.Min.X, FogBounds.Min.Y, FogBounds.Max.X, FogBounds.Max.Y,
					bKnown ? TEXT("its") : TEXT("a new"));
				return true;
			}
		}
	}

	// Already have something workable and the pawn is between zones (or there
	// are none): leave it alone rather than flapping back to the fallback.
	if (FogBounds.bIsValid && FogBounds.GetSize().GetMin() > 1.f)
	{
		return true;
	}

	// ── Phase 5's behaviour, unchanged, for the zoneless test levels ─────
	if (const AValhallaFogBounds* Bounds = AValhallaFogBounds::Find(World))
	{
		FogBounds = Bounds->GetFogBounds2D();
		UE_LOG(LogValhallaVision, Log,
			TEXT("fog bounds from %s: (%.0f, %.0f) .. (%.0f, %.0f)"),
			*Bounds->GetName(), FogBounds.Min.X, FogBounds.Min.Y, FogBounds.Max.X, FogBounds.Max.Y);
	}
	else
	{
		FogBounds = AValhallaFogBounds::ComputeFallbackBounds(World);
	}

	if (!FogBounds.bIsValid || FogBounds.GetSize().GetMin() <= 1.f)
	{
		return false;
	}

	ApplyBoundsToMaterial();
	return true;
}

void AValhallaFogRenderer::ApplyBoundsToMaterial()
{
	if (!FogMaterial || !FogBounds.bIsValid)
	{
		return;
	}

	const FVector2D Size = FogBounds.GetSize();
	if (Size.GetMin() <= 1.f)
	{
		return;
	}

	FogMaterial->SetVectorParameterValue(ParamFogOrigin, FLinearColor(FogBounds.Min.X, FogBounds.Min.Y, 0.f, 0.f));
	FogMaterial->SetVectorParameterValue(ParamFogInvSize, FLinearColor(1.f / Size.X, 1.f / Size.Y, 0.f, 0.f));
}

void AValhallaFogRenderer::SetVisionFog(const FVector2D& Centre, float ClearRadiusCm, float FadeWidthCm, const FLinearColor& Colour, float Strength)
{
	if (!FogMaterial)
	{
		return;
	}

	FogMaterial->SetVectorParameterValue(ParamVisionCentre, FLinearColor(Centre.X, Centre.Y, 0.f, 0.f));
	FogMaterial->SetScalarParameterValue(ParamVisionClearRadius, FMath::Max(0.f, ClearRadiusCm));
	FogMaterial->SetScalarParameterValue(ParamVisionFadeWidth, FMath::Max(1.f, FadeWidthCm));
	FogMaterial->SetVectorParameterValue(ParamVisionFogColor, Colour);
	FogMaterial->SetScalarParameterValue(ParamVisionFogStrength, FMath::Clamp(Strength, 0.f, 1.f));
}

void AValhallaFogRenderer::SetFirelight(float Strength, float RangeCm)
{
	const float NewStrength = FMath::Max(0.f, Strength);
	if (FirelightStrength <= 0.f && NewStrength > 0.f)
	{
		// Switching on: redraw the glow mask on the next tick rather than up
		// to BeaconRefreshSeconds later.
		GlowBounds = FBox2D(ForceInit);
	}
	FirelightStrength = NewStrength;

	if (!FogMaterial)
	{
		return;
	}
	FogMaterial->SetScalarParameterValue(ParamFirelightStrength, FirelightStrength);
	FogMaterial->SetScalarParameterValue(ParamFirelightRange, FMath::Max(1.f, RangeCm));
}

void AValhallaFogRenderer::GatherBeacons()
{
	Beacons.Reset();

	UWorld* World = GetWorld();
	if (!World || !FogBounds.bIsValid)
	{
		return;
	}

	const FName FireTag(FireLightTag);
	const FName LampTag(LampLightTag);
	const FName HandTag(BeaconTag);

	for (TActorIterator<ALight> It(World); It; ++It)
	{
		const ALight* LightActor = *It;
		if (LightActor->IsHidden()
			|| !(LightActor->ActorHasTag(FireTag) || LightActor->ActorHasTag(LampTag) || LightActor->ActorHasTag(HandTag)))
		{
			continue;
		}

		// Point and spot lights; a directional light has no position to glow at.
		const ULocalLightComponent* Light = Cast<ULocalLightComponent>(LightActor->GetLightComponent());
		if (!Light || !Light->IsVisible() || !Light->bAffectsWorld || Light->Intensity <= 0.f)
		{
			continue;
		}

		const FVector Location = Light->GetComponentLocation();
		const FVector2D LocationXY(Location.X, Location.Y);
		const float Radius = FMath::Max(50.f, Light->AttenuationRadius);
		if (!FogBounds.ExpandBy(Radius).IsInside(LocationXY))
		{
			continue;
		}

		// fire_lights.py and lamp_lights.py author candelas; convert anything
		// else so a hand-placed torch in lumens weighs the same.
		const float Candelas = Light->Intensity
			* ULocalLightComponent::GetUnitsConversionFactor(Light->IntensityUnits, ELightUnits::Candelas);
		const float Weight = FMath::Sqrt(FMath::Max(0.f, Candelas) / BeaconReferenceCandelas);
		if (Weight < BeaconMinWeight)
		{
			continue;
		}

		FBeacon& Beacon = Beacons.AddDefaulted_GetRef();
		Beacon.Location = LocationXY;
		Beacon.RadiusCm = Radius;
		Beacon.Colour = Light->GetLightColor() * FMath::Min(Weight, 1.5f);
	}
}

void AValhallaFogRenderer::DrawGlowMask(UCanvas* Canvas, int32 /*Width*/, int32 /*Height*/)
{
	if (!Canvas || Beacons.Num() == 0 || !FogBounds.bIsValid)
	{
		return;
	}

	// A fan per beacon: its colour at the centre, black at the rim, so the
	// vertex interpolation is the soft falloff. Additive, so the fires of one
	// camp add up instead of cutting each other's discs off.
	constexpr int32 Segments = 24;
	TArray<FCanvasUVTri> Triangles;
	Triangles.Reserve(Beacons.Num() * Segments);

	for (const FBeacon& Beacon : Beacons)
	{
		const FVector2D Centre = WorldToMask(Beacon.Location);
		FLinearColor Core = Beacon.Colour;
		Core.A = 1.f;

		for (int32 Index = 0; Index < Segments; ++Index)
		{
			const double A0 = (2.0 * UE_DOUBLE_PI * Index) / Segments;
			const double A1 = (2.0 * UE_DOUBLE_PI * (Index + 1)) / Segments;

			FCanvasUVTri Tri;
			Tri.V0_Pos = Centre;
			Tri.V1_Pos = WorldToMask(Beacon.Location + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * Beacon.RadiusCm);
			Tri.V2_Pos = WorldToMask(Beacon.Location + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * Beacon.RadiusCm);
			Tri.V0_UV = FVector2D::ZeroVector;
			Tri.V1_UV = FVector2D::ZeroVector;
			Tri.V2_UV = FVector2D::ZeroVector;
			Tri.V0_Color = Core;
			Tri.V1_Color = FLinearColor::Black;
			Tri.V2_Color = FLinearColor::Black;
			Triangles.Add(Tri);
		}
	}

	FCanvasTriangleItem Item(FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector, GWhiteTexture);
	Item.TriangleList = MoveTemp(Triangles);
	Item.BlendMode = SE_BLEND_Additive;
	Canvas->DrawItem(Item);
}

void AValhallaFogRenderer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FWorldDelegates::LevelAddedToWorld.Remove(LevelAddedHandle);
	FWorldDelegates::LevelRemovedFromWorld.Remove(LevelRemovedHandle);
	if (VisibleRT)
	{
		VisibleRT->OnCanvasRenderTargetUpdate.RemoveDynamic(this, &AValhallaFogRenderer::DrawVisibleMask);
	}
	if (GlowRT)
	{
		GlowRT->OnCanvasRenderTargetUpdate.RemoveDynamic(this, &AValhallaFogRenderer::DrawGlowMask);
	}
	Super::EndPlay(EndPlayReason);
}

FVector2D AValhallaFogRenderer::WorldToMask(const FVector2D& World) const
{
	const FVector2D Size = FogBounds.GetSize();
	const FVector2D UV((World.X - FogBounds.Min.X) / Size.X, (World.Y - FogBounds.Min.Y) / Size.Y);
	return UV * static_cast<float>(MaskResolution);
}

void AValhallaFogRenderer::GatherBlockerSegments(const FVector& Centre, float Range, TArray<FValhallaVisibilitySegment>& OutSegments) const
{
	OutSegments.Reset();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ValhallaFogBlockers), /*bTraceComplex*/ false);

	World->OverlapMultiByChannel(
		Overlaps, Centre, FQuat::Identity, ValhallaVisionBlockerChannel,
		FCollisionShape::MakeSphere(Range), Params);

	// A mesh with several collision primitives comes back once per primitive.
	TSet<const UPrimitiveComponent*> Seen;
	Seen.Reserve(Overlaps.Num());

	for (const FOverlapResult& Overlap : Overlaps)
	{
		const UStaticMeshComponent* Component = Cast<UStaticMeshComponent>(Overlap.GetComponent());
		if (!Component || Seen.Contains(Component))
		{
			continue;
		}
		Seen.Add(Component);

		const UStaticMesh* Mesh = Component->GetStaticMesh();
		if (!Mesh)
		{
			continue;
		}

		// The mesh's own local box, pushed through the component transform, so
		// a rotated wall stays 64 x 25 instead of becoming its AABB.
		const FBox Local = Mesh->GetBoundingBox();
		const FTransform& ToWorld = Component->GetComponentTransform();
		const double Z = Local.Min.Z;

		const FVector2D Corners[4] =
		{
			FVector2D(ToWorld.TransformPosition(FVector(Local.Min.X, Local.Min.Y, Z))),
			FVector2D(ToWorld.TransformPosition(FVector(Local.Max.X, Local.Min.Y, Z))),
			FVector2D(ToWorld.TransformPosition(FVector(Local.Max.X, Local.Max.Y, Z))),
			FVector2D(ToWorld.TransformPosition(FVector(Local.Min.X, Local.Max.Y, Z))),
		};

		for (int32 Index = 0; Index < 4; ++Index)
		{
			OutSegments.Emplace(Corners[Index], Corners[(Index + 1) % 4]);
		}
	}
}

void AValhallaFogRenderer::BuildTriangles(const FVector2D& Origin, const TArray<FVector2D>& Polygon)
{
	PendingTriangles.Reset();
	if (Polygon.Num() < 3)
	{
		return;
	}

	// A fan from the viewer. The polygon is star-shaped about its origin by
	// construction — every vertex is the end of a ray cast from it — so a fan
	// is exactly right and no general triangulation is needed.
	const FVector2D CentreTexel = WorldToMask(Origin);
	PendingTriangles.Reserve(Polygon.Num());

	for (int32 Index = 0; Index < Polygon.Num(); ++Index)
	{
		FCanvasUVTri Tri;
		Tri.V0_Pos = CentreTexel;
		Tri.V1_Pos = WorldToMask(Polygon[Index]);
		Tri.V2_Pos = WorldToMask(Polygon[(Index + 1) % Polygon.Num()]);
		Tri.V0_UV = FVector2D::ZeroVector;
		Tri.V1_UV = FVector2D::ZeroVector;
		Tri.V2_UV = FVector2D::ZeroVector;
		Tri.V0_Color = FLinearColor::White;
		Tri.V1_Color = FLinearColor::White;
		Tri.V2_Color = FLinearColor::White;
		PendingTriangles.Add(Tri);
	}
}

void AValhallaFogRenderer::DrawVisibleMask(UCanvas* Canvas, int32 /*Width*/, int32 /*Height*/)
{
	if (Canvas && PendingTriangles.Num() > 0)
	{
		Canvas->K2_DrawTriangle(nullptr, PendingTriangles);
	}
}

void AValhallaFogRenderer::ApplyFogPostProcess(UCameraComponent* Camera)
{
	if (!Camera || !FogMaterial || AppliedCamera == Camera)
	{
		return;
	}

	for (const FWeightedBlendable& Existing : Camera->PostProcessSettings.WeightedBlendables.Array)
	{
		if (Existing.Object == FogMaterial)
		{
			AppliedCamera = Camera;
			return;
		}
	}

	// Ordering against PP_Outline is the material's own BlendablePriority (1 vs
	// the outline's 0), not this call: blendables at the same location are
	// sorted by priority before they run, so the order they were added in does
	// not matter and a respawn cannot swap them.
	Camera->PostProcessSettings.AddBlendable(FogMaterial, 1.f);
	Camera->PostProcessBlendWeight = 1.f;
	AppliedCamera = Camera;

	UE_LOG(LogValhallaVision, Log, TEXT("fog applied to %s"), *Camera->GetPathName());
}

void AValhallaFogRenderer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	AValhallaPlayerController* Controller = OwningController.Get();
	if (!Controller || !FogMaterial || !VisibleRT || !ExploredRT)
	{
		return;
	}

	AValhallaCharacter* Pawn = Cast<AValhallaCharacter>(Controller->GetPawn());
	if (!Pawn)
	{
		return;
	}

	ApplyFogPostProcess(Pawn->GetTopDownCamera());

	// Cheap: a hit on the zone the pawn was already in returns on the first
	// compare, and only a real zone change does any work.
	if (!ResolveBoundsForPawn(Pawn))
	{
		return;
	}

	// ── B-06: the beacon glow mask ──────────────────────────────────────
	//
	// Only while a zone's vision fog has the firelight on. Rebuilt on a zone
	// change and every BeaconRefreshSeconds, so a fire lit or put out, or a
	// light streamed in, shows up within a couple of seconds. A few dozen
	// soft discs, drawn once; nothing per frame.
	if (FirelightStrength > 0.f && GlowRT)
	{
		BeaconRefreshAccumulator += DeltaSeconds;
		if (!(GlowBounds == FogBounds) || BeaconRefreshAccumulator >= BeaconRefreshSeconds)
		{
			BeaconRefreshAccumulator = 0.f;
			GlowBounds = FogBounds;
			GatherBeacons();
			GlowRT->FastUpdateResource();
		}
	}

	CSV_SCOPED_TIMING_STAT(Valhalla, FogTick);

	const FVector Location = Pawn->GetActorLocation();
	const FVector2D Origin(Location.X, Location.Y);

	// The class vision range, capped by the zone's atmosphere (B-06) — the same
	// number the server culls relevancy with, so the lit polygon never shows
	// ground where the server has stopped sending the NPCs standing on it.
	const float Range = UValhallaVisibilitySubsystem::GetVisionRangeFor(Pawn);
	const bool bAlways = CVarFogAlwaysRecompute.GetValueOnGameThread() != 0;

	// ── B-27 Phase 5: only when something changed ────────────────────────
	if (!bAlways)
	{
		RebuildBlockerCacheIfDirty();
	}
	FFogKey Key;
	Key.Origin = Origin;
	Key.Range = Range;
	Key.Bounds = FogBounds;
	Key.BlockerGeneration = BlockerGeneration;
	if (!bAlways && !NeedsRecompute(bHaveLastKey ? &LastKey : nullptr, Key, bLastHadMovableBlocker))
	{
		return;
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Valhalla_Fog_GatherSegments);
		if (bAlways)
		{
			GatherBlockerSegments(Location, Range, SegmentScratch);
			bLastHadMovableBlocker = false;
		}
		else
		{
			SelectSegmentsInRange(BlockerEntries, Location, Range, SegmentScratch, bLastHadMovableBlocker);
		}
	}

	TArray<FVector2D> Polygon;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Valhalla_Fog_VisibilityPolygon);
		Polygon = ValhallaVisibility::ComputeVisibilityPolygon(
			Origin, SegmentScratch, Range, ValhallaVisibility::DefaultArcRays);
	}

	BuildTriangles(Origin, Polygon);
	if (PendingTriangles.Num() == 0)
	{
		return;
	}
	LastKey = Key;
	bHaveLastKey = true;

	// This polygon: the canvas clears to black first, so what is left is
	// exactly what is visible now. FastUpdateResource keeps the texture and
	// only redraws it (UpdateResource re-created it every frame).
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Valhalla_Fog_VisibleRT);
		if (bAlways)
		{
			VisibleRT->UpdateResource();
		}
		else
		{
			VisibleRT->FastUpdateResource();
		}
	}

	// The same fan again into the explored mask, without a clear. See the
	// header for why union is the accumulation, not an approximation of it.
	UWorld* World = GetWorld();
	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	FDrawToRenderTargetContext Context;

	TRACE_CPUPROFILER_EVENT_SCOPE(Valhalla_Fog_ExploredRT);
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(World, ExploredRT, Canvas, CanvasSize, Context);
	if (Canvas)
	{
		Canvas->K2_DrawTriangle(nullptr, PendingTriangles);
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World, Context);
}

// ─────────────────────────────────────────────────────────────────────────────
//  B-27 Phase 5: when to recompute, the blocker cache, the explored masks
// ─────────────────────────────────────────────────────────────────────────────

bool AValhallaFogRenderer::NeedsRecompute(const FFogKey* Last, const FFogKey& Now, bool bMovableBlockerInRange)
{
	if (!Last || bMovableBlockerInRange)
	{
		return true;
	}
	return FVector2D::DistSquared(Last->Origin, Now.Origin) >= FMath::Square(RecomputeDistanceCm)
		|| !FMath::IsNearlyEqual(Last->Range, Now.Range, 1.f)
		|| !(Last->Bounds == Now.Bounds)
		|| Last->BlockerGeneration != Now.BlockerGeneration;
}

void AValhallaFogRenderer::MakeBoxSegments(const FBox& Local, const FTransform& ToWorld, FValhallaVisibilitySegment OutSegments[4])
{
	const double Z = Local.Min.Z;
	const FVector2D Corners[4] =
	{
		FVector2D(ToWorld.TransformPosition(FVector(Local.Min.X, Local.Min.Y, Z))),
		FVector2D(ToWorld.TransformPosition(FVector(Local.Max.X, Local.Min.Y, Z))),
		FVector2D(ToWorld.TransformPosition(FVector(Local.Max.X, Local.Max.Y, Z))),
		FVector2D(ToWorld.TransformPosition(FVector(Local.Min.X, Local.Max.Y, Z))),
	};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		OutSegments[Index] = FValhallaVisibilitySegment(Corners[Index], Corners[(Index + 1) % 4]);
	}
}

void AValhallaFogRenderer::SelectSegmentsInRange(TArrayView<const FBlockerEntry> Entries, const FVector& Centre, float Range,
	TArray<FValhallaVisibilitySegment>& OutSegments, bool& bOutMovable)
{
	OutSegments.Reset();
	bOutMovable = false;
	const double RangeSq = FMath::Square(static_cast<double>(Range));
	for (const FBlockerEntry& Entry : Entries)
	{
		if (!Entry.Bounds.IsValid || Entry.Bounds.ComputeSquaredDistanceToPoint(Centre) > RangeSq)
		{
			continue;
		}
		if (Entry.bMovable)
		{
			// Where it is now, not where it was when the level was cached.
			const UStaticMeshComponent* Component = Entry.Component.Get();
			const UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
			if (!Mesh)
			{
				continue;
			}
			FValhallaVisibilitySegment Live[4];
			MakeBoxSegments(Mesh->GetBoundingBox(), Component->GetComponentTransform(), Live);
			OutSegments.Append(Live, 4);
			bOutMovable = true;
			continue;
		}
		OutSegments.Append(Entry.Segments, 4);
	}
}

void AValhallaFogRenderer::CacheLevel(const ULevel* Level, TArray<FBlockerEntry>& OutEntries)
{
	OutEntries.Reset();
	if (!Level)
	{
		return;
	}
	TArray<UStaticMeshComponent*> Components;
	for (const AActor* Actor : Level->Actors)
	{
		if (!Actor)
		{
			continue;
		}
		Components.Reset();
		Actor->GetComponents(Components);
		for (const UStaticMeshComponent* Component : Components)
		{
			// What the overlap on the VisionBlocker channel found: a registered
			// component that queries and does not ignore the channel.
			if (!Component || !Component->IsRegistered() || !Component->IsQueryCollisionEnabled()
				|| Component->GetCollisionResponseToChannel(ValhallaVisionBlockerChannel) == ECR_Ignore)
			{
				continue;
			}
			const UStaticMesh* Mesh = Component->GetStaticMesh();
			if (!Mesh)
			{
				continue;
			}
			FBlockerEntry& Entry = OutEntries.AddDefaulted_GetRef();
			Entry.Component = Component;
			Entry.Bounds = Component->Bounds.GetBox();
			Entry.bMovable = Component->Mobility == EComponentMobility::Movable;
			MakeBoxSegments(Mesh->GetBoundingBox(), Component->GetComponentTransform(), Entry.Segments);
		}
	}
}

void AValhallaFogRenderer::RebuildBlockerCacheIfDirty()
{
	if (!bBlockerCacheDirty)
	{
		return;
	}
	bBlockerCacheDirty = false;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	TRACE_CPUPROFILER_EVENT_SCOPE(Valhalla_Fog_CacheBlockers);

	// Drop the levels that left, cache the ones that are new; a level already
	// cached is not walked again.
	TSet<const ULevel*> Present;
	for (const ULevel* Level : World->GetLevels())
	{
		if (Level && Level->bIsVisible)
		{
			Present.Add(Level);
		}
	}
	for (auto It = BlockersByLevel.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || !Present.Contains(It.Key().Get()))
		{
			It.RemoveCurrent();
		}
	}
	int32 Cached = 0;
	for (const ULevel* Level : Present)
	{
		const TWeakObjectPtr<ULevel> Key(const_cast<ULevel*>(Level));
		if (!BlockersByLevel.Contains(Key))
		{
			CacheLevel(Level, BlockersByLevel.Add(Key));
			++Cached;
		}
	}

	BlockerEntries.Reset();
	for (const TPair<TWeakObjectPtr<ULevel>, TArray<FBlockerEntry>>& Pair : BlockersByLevel)
	{
		BlockerEntries.Append(Pair.Value);
	}
	++BlockerGeneration;
	UE_LOG(LogValhallaVision, Log, TEXT("fog: %d vision blockers in %d levels (%d newly cached)."),
		BlockerEntries.Num(), BlockersByLevel.Num(), Cached);
}

void AValhallaFogRenderer::MarkBlockersDirty()
{
	BlockersByLevel.Reset();
	bBlockerCacheDirty = true;
}

void AValhallaFogRenderer::HandleLevelAdded(ULevel* /*Level*/, UWorld* InWorld)
{
	if (InWorld == GetWorld())
	{
		bBlockerCacheDirty = true;
	}
}

void AValhallaFogRenderer::HandleLevelRemoved(ULevel* /*Level*/, UWorld* InWorld)
{
	if (InWorld == GetWorld())
	{
		bBlockerCacheDirty = true;
	}
}

void AValhallaFogRenderer::SelectExploredMask(FName ZoneId)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	TObjectPtr<UTextureRenderTarget2D>& Mask = ExploredByZone.FindOrAdd(ZoneId);
	if (!Mask)
	{
		Mask = UKismetRenderingLibrary::CreateRenderTarget2D(
			World, MaskResolution, MaskResolution, RTF_RGBA8_SRGB, FLinearColor::Black, /*bAutoGenerateMipMaps*/ false);
		if (Mask)
		{
			UKismetRenderingLibrary::ClearRenderTarget2D(World, Mask, FLinearColor::Black);
		}
	}
	ExploredRT = Mask;
	if (FogMaterial && ExploredRT)
	{
		FogMaterial->SetTextureParameterValue(ParamExploredMask, ExploredRT);
	}
	// The new zone's mask has nothing of this polygon yet.
	bHaveLastKey = false;
}
