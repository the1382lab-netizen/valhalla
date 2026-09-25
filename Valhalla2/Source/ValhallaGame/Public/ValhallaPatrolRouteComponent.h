// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "ValhallaPatrolRouteComponent.generated.h"

/**
 * Draws an NPC spawn point's idle movement in the editor viewport (B-10 part
 * 2): the patrol route as lines from stop to stop (dashed for PingPong, with
 * the closing leg for Loop), a line to the spawn point it follows, and its
 * roam radius as a circle.
 *
 * A primitive with its own scene proxy rather than debug lines, because debug
 * lines in an editor world are flushed every frame and a level designer wants
 * the route on screen while dragging its points, not only while the actor is
 * selected. The Patrol Points' own diamond handles (MakeEditWidget) are the
 * engine's and appear when the spawn point is selected.
 *
 * Editor-only on AValhallaNPCSpawner (created with
 * CreateEditorOnlyDefaultSubobject, hidden in game), so a game never draws or
 * even creates it. Points are local to the component, which sits on the spawn
 * point's root with no offset, i.e. in the spawn point's own space, exactly as
 * AValhallaNPCSpawner::PatrolPoints are.
 */
UCLASS(ClassGroup = Valhalla, hidecategories = (Object, LOD, Lighting, TextureStreaming, Collision, Physics, Rendering, Activation, Cooking, HLOD, Navigation, VirtualTexture, Tags, AssetUserData))
class VALHALLAGAME_API UValhallaPatrolRouteComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UValhallaPatrolRouteComponent(const FObjectInitializer& ObjectInitializer);

	/** Stops in component space; [0] is the spawn point. Fewer than two draws no route. */
	UPROPERTY(Transient)
	TArray<FVector> RoutePoints;

	/** Loop draws the leg from the last stop back to the first. */
	UPROPERTY(Transient)
	bool bLoop = false;

	/** PingPong draws the route dashed. */
	UPROPERTY(Transient)
	bool bDashed = false;

	/** Roam radius, cm; 0 draws no circle. */
	UPROPERTY(Transient)
	float WanderRadius = 0.f;

	/** The spawn point this one's NPC follows, in component space. */
	UPROPERTY(Transient)
	bool bHasFollowTarget = false;

	UPROPERTY(Transient)
	FVector FollowTarget = FVector::ZeroVector;

	/**
	 * Set everything at once and push it to the render thread. Called from
	 * AValhallaNPCSpawner::OnConstruction, so it runs every time a designer
	 * edits a property or drags a point.
	 */
	void SetRoute(const TArray<FVector>& InPoints, bool bInLoop, bool bInDashed, float InWanderRadius,
		bool bInHasFollowTarget, const FVector& InFollowTarget);

	//~ Begin UPrimitiveComponent interface
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ End UPrimitiveComponent interface
};
