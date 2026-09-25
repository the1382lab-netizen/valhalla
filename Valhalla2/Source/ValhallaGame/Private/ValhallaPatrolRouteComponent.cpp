// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaPatrolRouteComponent.h"

#include "PrimitiveSceneProxy.h"
#include "SceneManagement.h"

namespace
{
	/** Lines are lifted this far off the floor so the ground does not swallow them, cm. */
	constexpr double DrawLiftCm = 20.0;

	/** Dash length for a PingPong route, cm. */
	constexpr double DashCm = 40.0;

	/** Segments in the roam circle. */
	constexpr int32 CircleSides = 48;

	/** Small cross at each stop, cm. */
	constexpr double StopMarkCm = 15.0;

	/** Route: the spawn point's orange (its facing arrow). Follow link: blue. Roam circle: green. */
	FLinearColor RouteColor() { return FLinearColor(FColor(255, 170, 40)); }
	FLinearColor FollowColor() { return FLinearColor(FColor(120, 200, 255)); }
	FLinearColor WanderColor() { return FLinearColor(FColor(140, 230, 120)); }

	/**
	 * The render-thread copy. Everything is copied by value when the proxy is
	 * made; SetRoute marks the render state dirty, which makes a new one.
	 */
	class FValhallaPatrolRouteSceneProxy final : public FPrimitiveSceneProxy
	{
	public:
		explicit FValhallaPatrolRouteSceneProxy(const UValhallaPatrolRouteComponent* Component)
			: FPrimitiveSceneProxy(Component)
			, Points(Component->RoutePoints)
			, bLoop(Component->bLoop)
			, bDashed(Component->bDashed)
			, WanderRadius(Component->WanderRadius)
			, bHasFollowTarget(Component->bHasFollowTarget)
			, FollowTarget(Component->FollowTarget)
		{
			bWillEverBeLit = false;
		}

		virtual SIZE_T GetTypeHash() const override
		{
			static size_t UniquePointer;
			return reinterpret_cast<size_t>(&UniquePointer);
		}

		virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& /*ViewFamily*/,
			uint32 VisibilityMap, FMeshElementCollector& Collector) const override
		{
			const FMatrix& ProxyToWorld = GetLocalToWorld();
			const FVector Lift(0.0, 0.0, DrawLiftCm);
			auto ToWorld = [&ProxyToWorld, &Lift](const FVector& Local)
			{
				return ProxyToWorld.TransformPosition(Local) + Lift;
			};

			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
			{
				if (!(VisibilityMap & (1u << ViewIndex)))
				{
					continue;
				}
				FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);

				// ── The route ──
				if (Points.Num() >= 2)
				{
					const FLinearColor Color = RouteColor();
					const int32 Legs = bLoop ? Points.Num() : Points.Num() - 1;
					for (int32 Leg = 0; Leg < Legs; ++Leg)
					{
						const FVector A = ToWorld(Points[Leg]);
						const FVector B = ToWorld(Points[(Leg + 1) % Points.Num()]);
						if (bDashed)
						{
							DrawDashedLine(PDI, A, B, Color, DashCm, SDPG_World);
						}
						else
						{
							PDI->DrawLine(A, B, Color, SDPG_World, 2.f);
						}
					}
					for (int32 Index = 1; Index < Points.Num(); ++Index)
					{
						const FVector P = ToWorld(Points[Index]);
						PDI->DrawLine(P - FVector(StopMarkCm, 0.0, 0.0), P + FVector(StopMarkCm, 0.0, 0.0), Color, SDPG_World, 2.f);
						PDI->DrawLine(P - FVector(0.0, StopMarkCm, 0.0), P + FVector(0.0, StopMarkCm, 0.0), Color, SDPG_World, 2.f);
					}
				}

				// ── The link to the leader ──
				if (bHasFollowTarget)
				{
					DrawDashedLine(PDI, ToWorld(FVector::ZeroVector), ToWorld(FollowTarget),
						FollowColor(), 20.0, SDPG_World);
				}

				// ── The roam radius ──
				if (WanderRadius > 0.f)
				{
					const FVector Centre = ToWorld(FVector::ZeroVector);
					DrawCircle(PDI, Centre, FVector::ForwardVector, FVector::RightVector,
						WanderColor(), WanderRadius, CircleSides, SDPG_World, 1.5f);
				}
			}
		}

		virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
		{
			FPrimitiveViewRelevance Result;
			Result.bDrawRelevance = IsShown(View);
			Result.bDynamicRelevance = true;
			Result.bShadowRelevance = false;
			Result.bEditorPrimitiveRelevance = UseEditorCompositing(View);
			return Result;
		}

		virtual uint32 GetMemoryFootprint() const override
		{
			return static_cast<uint32>(sizeof(*this) + Points.GetAllocatedSize());
		}

	private:
		TArray<FVector> Points;
		bool bLoop;
		bool bDashed;
		float WanderRadius;
		bool bHasFollowTarget;
		FVector FollowTarget;
	};
}

UValhallaPatrolRouteComponent::UValhallaPatrolRouteComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCastShadow(false);
	SetHiddenInGame(true);
	bIsEditorOnly = true;
	bUseEditorCompositing = true;
	bSelectable = false;
	SetCanEverAffectNavigation(false);
}

void UValhallaPatrolRouteComponent::SetRoute(const TArray<FVector>& InPoints, bool bInLoop, bool bInDashed, float InWanderRadius,
	bool bInHasFollowTarget, const FVector& InFollowTarget)
{
	RoutePoints = InPoints;
	bLoop = bInLoop;
	bDashed = bInDashed;
	WanderRadius = FMath::Max(0.f, InWanderRadius);
	bHasFollowTarget = bInHasFollowTarget;
	FollowTarget = InFollowTarget;

	UpdateBounds();
	MarkRenderStateDirty();
}

FPrimitiveSceneProxy* UValhallaPatrolRouteComponent::CreateSceneProxy()
{
	if (RoutePoints.Num() < 2 && WanderRadius <= 0.f && !bHasFollowTarget)
	{
		return nullptr;
	}
	return new FValhallaPatrolRouteSceneProxy(this);
}

FBoxSphereBounds UValhallaPatrolRouteComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBox Box(ForceInit);
	Box += FVector::ZeroVector;
	for (const FVector& Point : RoutePoints)
	{
		Box += Point;
	}
	if (bHasFollowTarget)
	{
		Box += FollowTarget;
	}
	if (WanderRadius > 0.f)
	{
		Box += FVector(WanderRadius, WanderRadius, 0.0);
		Box += FVector(-WanderRadius, -WanderRadius, 0.0);
	}
	// A little height so a flat route is not a zero-thickness box.
	Box = Box.ExpandBy(FVector(10.0, 10.0, DrawLiftCm + 10.0));
	return FBoxSphereBounds(Box).TransformBy(LocalToWorld);
}
