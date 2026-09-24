// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ValhallaTypes.h"
#include "ValhallaZoneAtmosphere.generated.h"

class AValhallaCharacter;
class AValhallaFogRenderer;
class AValhallaPlayerController;
class UCameraComponent;
class UExponentialHeightFogComponent;
class ULightComponent;
class USkyLightComponent;

/**
 * B-06: per-zone atmosphere lookups, shared by the client presentation
 * (AValhallaZoneAtmosphere), the server's relevancy (GetVisionRangeFor) and
 * target validation (AValhallaPlayerState::SetTargetActor).
 *
 * All three read `zones.json` through UValhallaDataSubsystem on every call, so
 * a hot reload of the data takes effect on the next frame with nothing cached
 * here to invalidate.
 */
namespace ValhallaAtmosphere
{
	/** The zone's atmosphere, or null when it has none (today's look) or the data is not loaded. */
	VALHALLAGAME_API const FValhallaAtmosphereProfile* Find(const UObject* WorldContextObject, FName ZoneId);

	/** The zone's server relevancy cap, cm. 0: no cap (class vision range only). */
	VALHALLAGAME_API float GetRelevancyCapCm(const UObject* WorldContextObject, FName ZoneId);

	/** Where the zone's vision fog is fully opaque, cm. 0: no vision fog. */
	VALHALLAGAME_API float GetVisionLimitCm(const UObject* WorldContextObject, FName ZoneId);
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
	FValhallaAtmosphereState BuildTarget(const FValhallaAtmosphereProfile* Profile) const;

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
