// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ValhallaTypes.h"
#include "ValhallaZoneAtmosphere.generated.h"

class AValhallaCharacter;
class AValhallaFogRenderer;
class AValhallaPlayerController;
class AValhallaPlayerState;
class UCameraComponent;
class UExponentialHeightFogComponent;
class ULightComponent;
class USkyLightComponent;

/**
 * One player's vision in one zone. Every distance is derived from the one
 * effective range, so the server's line of sight and relevancy, the client's
 * vision fog, hide-beyond-fog and the target refusal can never disagree.
 */
struct FValhallaVision
{
	/** The player's own range before the zone: class vision range (x buff/race modifiers, later). */
	float BaseRangeCm = 0.f;

	/** Base x the zone's visionScale. The vision fog is fully opaque here; hide and target limit. */
	float EffectiveRangeCm = 0.f;

	/** The vision fog is clear out to here (effective x visionClearFraction). */
	float ClearRadiusCm = 0.f;

	/** Server line of sight and net relevancy (effective + relevancyMarginCm). */
	float RelevancyRangeCm = 0.f;

	/** False in a zone without vision fog: all four are the base range, as before B-06. */
	bool bHasVisionFog = false;
};

/**
 * B-06: per-zone atmosphere and vision, shared by the client presentation
 * (AValhallaZoneAtmosphere), the server's line of sight and relevancy
 * (UValhallaVisibilitySubsystem::GetVisionRangeFor) and target validation
 * (AValhallaPlayerState::SetTargetActor).
 *
 * Everything reads `zones.json` through UValhallaDataSubsystem on every call,
 * so a hot reload of the data takes effect on the next frame with nothing
 * cached here to invalidate.
 */
namespace ValhallaAtmosphere
{
	/** The zone's atmosphere, or null when it has none (today's look) or the data is not loaded. */
	VALHALLAGAME_API const FValhallaAtmosphereProfile* Find(const UObject* WorldContextObject, FName ZoneId);

	/**
	 * The player's own vision range before any zone: `classes.json`
	 * `visionRange` (1200 most classes, 1350 rogue, 1800 ranger), or 1200
	 * without a player state.
	 *
	 * THE hook for personal vision modifiers: when B-20 buffs (or races) change
	 * how far someone sees, multiply them in here and every consumer — fog,
	 * hide, targeting, line of sight, relevancy — follows.
	 */
	VALHALLAGAME_API float GetBaseVisionRange(const AValhallaPlayerState* State);

	/**
	 * The player's vision in a zone: base range x the zone's `visionScale`,
	 * with the clear radius and the relevancy range derived from it. ZoneId
	 * None uses the player state's replicated ZoneId (the server's answer); the
	 * client's atmosphere passes the zone its pawn is standing in.
	 */
	VALHALLAGAME_API FValhallaVision ResolveVision(const AValhallaPlayerState* State, FName ZoneId = NAME_None);

	/** ResolveVision(...).EffectiveRangeCm — where this player's vision fog is fully opaque. */
	VALHALLAGAME_API float GetEffectiveVisionRange(const AValhallaPlayerState* State, FName ZoneId = NAME_None);

	/**
	 * The camera's maximum boom length for this player in this zone, cm; 0 =
	 * the character's own limit. Per zone today. A per-class (or per-ability)
	 * camera modifier, e.g. a ranger's wider view, goes here.
	 */
	VALHALLAGAME_API float ResolveCameraMaxArm(const FValhallaAtmosphereProfile* Profile, const AValhallaPlayerState* State);
}

/**
 * Everything the zone blend moves, as absolute values (not the profile's
 * scales), so a blend is a plain per-field lerp and a profile change mid-blend
 * starts from wherever the last one had got to.
 */
struct FValhallaAtmosphereState
{
	float HeightFogDensity = 0.f;
	float HeightFogStartCm = 0.f;
	FLinearColor HeightFogColor = FLinearColor::Black;

	float SunIntensity = 0.f;
	/** A multiplier on each fill light's own baseline, which differ per light. */
	float FillScale = 1.f;
	float SkyIntensity = 0.f;

	FLinearColor GradeTint = FLinearColor::White;

	float VisionClearRadiusCm = 0.f;
	float VisionFadeWidthCm = 0.f;
	FLinearColor VisionFogColor = FLinearColor::Black;
	/** 0 = no vision fog (PP_Fog's default), 1 = full. */
	float VisionFogStrength = 0.f;

	/** Firelight glow through the vision fog; see AValhallaFogRenderer::SetFirelight. */
	float FirelightStrength = 0.f;
	float FirelightRangeCm = 0.f;

	float CameraMaxArmCm = 0.f;

	static FValhallaAtmosphereState Lerp(const FValhallaAtmosphereState& A, const FValhallaAtmosphereState& B, float Alpha);
	bool Equals(const FValhallaAtmosphereState& Other) const;
};

/**
 * The client's zone atmosphere: fog, light, grade, vision fog, camera limit
 * and hide-beyond-fog, from the local player's zone's `atmosphere` profile.
 *
 * ## Why client-side, and why a switch rather than a volume
 *
 * Every zone is a sub-level of one persistent L_World, which owns the only
 * sun, sky light, height fog and post-process volume (lighting_remaster.py).
 * Each client only ever sees its own zone, so changing those shared actors on
 * *this* client when *its* player changes zone is enough, and nothing about it
 * is replicated. A dedicated server never spawns one (it has no local
 * controller, and AValhallaFogRenderer, which spawns this, is client-only).
 *
 * ## Today's look is the baseline
 *
 * On the first tick the actor records the persistent level's current values —
 * sun, fill lights, sky light, height fog, the global post-process gain and
 * the character's camera limit. A zone without an `atmosphere`, and every
 * field a profile leaves out, blends back to exactly those values; once a
 * blend back to the baseline finishes, the camera's colour-gain override is
 * switched off again, so nothing is left overriding the level.
 *
 * ## What it drives
 *
 *   - the persistent level's ExponentialHeightFog (density, start, colour),
 *     the sun (the directional light used as the atmosphere sun), the other
 *     directional lights (the cool fill) and the sky light;
 *   - the player camera's colour gain (the grade tint);
 *   - PP_Fog's vision fog parameters, through AValhallaFogRenderer;
 *   - the camera boom's length, clamped to the zone's maximum;
 *   - NPCs, other players and loot bags beyond the fully fogged distance are
 *     hidden. The server stops sending them too (relevancy, see
 *     UValhallaVisibilitySubsystem::GetVisionRangeFor); hiding here is what
 *     makes it immediate, because an actor channel lingers a few seconds after
 *     the actor stops being relevant.
 */
UCLASS(NotPlaceable)
class VALHALLAGAME_API AValhallaZoneAtmosphere : public AActor
{
	GENERATED_BODY()

public:
	AValhallaZoneAtmosphere();

	//~ Begin AActor interface
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End AActor interface

	/**
	 * Spawn the atmosphere for a local controller, or return the one it has.
	 * Null on a dedicated server and for any non-local controller.
	 */
	static AValhallaZoneAtmosphere* EnsureFor(AValhallaPlayerController* Controller);

	/** How long a zone change takes to blend, seconds. */
	static constexpr float BlendSeconds = 1.f;

	/** How often the hide-beyond-fog pass runs, seconds. */
	static constexpr float HideIntervalSeconds = 0.1f;

	/** Without `firelightRangeCm`, fire glows fade out at this many times the fully fogged distance. */
	static constexpr float DefaultFirelightRangeScale = 1.5f;

	/** The zone the profile currently comes from (the pawn's, by position). */
	FName GetCurrentZoneId() const { return CurrentZoneId; }

private:
	/** A light and the intensity the level gave it. */
	struct FLightBaseline
	{
		TWeakObjectPtr<ULightComponent> Light;
		float Intensity = 0.f;
	};

	/** Find the persistent level's lighting actors and record their values. */
	void CaptureBaseline();

	/** The state a profile asks for. Null profile: the baseline. */
	FValhallaAtmosphereState BuildTarget(const FValhallaAtmosphereProfile* Profile, const FValhallaVision& Vision, const AValhallaPlayerState* State) const;

	/** Push a state onto the world, the camera and PP_Fog. */
	void ApplyState(const FValhallaAtmosphereState& State, AValhallaCharacter* Pawn, bool bBlendFinished);

	/** Hide or show the actors around the pawn against the fog's far edge. 0 shows everything. */
	void UpdateHiddenActors(const APawn* OwnPawn, float LimitCm);

	/** Show everything this actor hid. */
	void ShowAllHidden();

	/** The fog renderer owned by the same controller, if any. */
	AValhallaFogRenderer* FindFogRenderer() const;

	UPROPERTY(Transient)
	TWeakObjectPtr<AValhallaPlayerController> OwningController;

	FName CurrentZoneId;

	bool bHaveBaseline = false;
	bool bHaveState = false;
	bool bBlending = false;
	float BlendElapsed = 0.f;

	FValhallaAtmosphereState Baseline;
	FValhallaAtmosphereState Current;
	FValhallaAtmosphereState BlendFrom;
	FValhallaAtmosphereState Target;

	/** The zone's fully fogged distance, for the hide pass. 0: nothing hidden. */
	float HideLimitCm = 0.f;
	float HideAccumulator = 0.f;

	TWeakObjectPtr<UExponentialHeightFogComponent> HeightFog;
	FLightBaseline Sun;
	TArray<FLightBaseline> Fills;
	TWeakObjectPtr<USkyLightComponent> SkyLight;

	/** The global post-process volume's colour gain, which the tint multiplies. */
	FVector4 BaseColorGain = FVector4(1.0, 1.0, 1.0, 1.0);

	/** The camera whose colour gain this actor is overriding, if any. */
	TWeakObjectPtr<UCameraComponent> TintedCamera;

	/** Actors this pass hid, so it only ever un-hides its own. */
	TSet<TWeakObjectPtr<AActor>> HiddenByFog;
};
