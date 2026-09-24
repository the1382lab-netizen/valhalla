// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaFogRenderer.h"

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ValhallaCharacter.h"
#include "ValhallaFogBounds.h"
#include "ValhallaPlayerController.h"
#include "ValhallaPlayerState.h"
#include "ValhallaZoneAtmosphere.h"
#include "ValhallaZoneSubsystem.h"

namespace
{
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

	ExploredRT = UKismetRenderingLibrary::CreateRenderTarget2D(
		World, MaskResolution, MaskResolution, RTF_RGBA8_SRGB, FLinearColor::Black, /*bAutoGenerateMipMaps*/ false);
	if (ExploredRT)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(World, ExploredRT, FLinearColor::Black);
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
	FogMaterial->SetTextureParameterValue(ParamExploredMask, ExploredRT);
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

				const bool bWasExplored = !CurrentZoneId.IsNone();

				CurrentZoneId = Zone->ZoneId;
				FogBounds = Zone->GetBounds2D();

				// Everything explored was explored in the *other* zone's square
				// of world, and there is one mask. See ResolveBoundsForPawn's
				// comment in the header.
				if (bWasExplored && ExploredRT)
				{
					UKismetRenderingLibrary::ClearRenderTarget2D(World, ExploredRT, FLinearColor::Black);
				}

				ApplyBoundsToMaterial();

				UE_LOG(LogValhallaVision, Log,
					TEXT("fog bounds -> zone '%s': (%.0f, %.0f) .. (%.0f, %.0f)%s"),
					*CurrentZoneId.ToString(),
					FogBounds.Min.X, FogBounds.Min.Y, FogBounds.Max.X, FogBounds.Max.Y,
					bWasExplored ? TEXT(", explored mask reset") : TEXT(""));
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

void AValhallaFogRenderer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (VisibleRT)
	{
		VisibleRT->OnCanvasRenderTargetUpdate.RemoveDynamic(this, &AValhallaFogRenderer::DrawVisibleMask);
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

	const FVector Location = Pawn->GetActorLocation();
	const FVector2D Origin(Location.X, Location.Y);

	// The class vision range, capped by the zone's atmosphere (B-06) — the same
	// number the server culls relevancy with, so the lit polygon never shows
	// ground where the server has stopped sending the NPCs standing on it.
	const float Range = UValhallaVisibilitySubsystem::GetVisionRangeFor(Pawn);

	GatherBlockerSegments(Location, Range, SegmentScratch);

	const TArray<FVector2D> Polygon = ValhallaVisibility::ComputeVisibilityPolygon(
		Origin, SegmentScratch, Range, ValhallaVisibility::DefaultArcRays);

	BuildTriangles(Origin, Polygon);
	if (PendingTriangles.Num() == 0)
	{
		return;
	}

	// This frame's polygon: the canvas clears to black first, so what is left
	// is exactly what is visible now.
	VisibleRT->UpdateResource();

	// The same fan again into the explored mask, without a clear. See the
	// header for why union is the accumulation, not an approximation of it.
	UWorld* World = GetWorld();
	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	FDrawToRenderTargetContext Context;

	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(World, ExploredRT, Canvas, CanvasSize, Context);
	if (Canvas)
	{
		Canvas->K2_DrawTriangle(nullptr, PendingTriangles);
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World, Context);
}
