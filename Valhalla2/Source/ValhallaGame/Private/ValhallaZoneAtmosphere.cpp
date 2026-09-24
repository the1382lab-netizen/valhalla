// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaZoneAtmosphere.h"

#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"
#include "ValhallaCharacter.h"
#include "ValhallaDataSubsystem.h"
#include "ValhallaFogRenderer.h"
#include "ValhallaLootBag.h"
#include "ValhallaNPC.h"
#include "ValhallaConstants.h"
#include "ValhallaPlayerController.h"
#include "ValhallaPlayerState.h"
#include "ValhallaVisibilitySubsystem.h"
#include "ValhallaZoneSubsystem.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Lookups
// ─────────────────────────────────────────────────────────────────────────────

const FValhallaAtmosphereProfile* ValhallaAtmosphere::Find(const UObject* WorldContextObject, FName ZoneId)
{
	if (!WorldContextObject || ZoneId.IsNone())
	{
		return nullptr;
	}

	const UWorld* World = WorldContextObject->GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UValhallaDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
	const FValhallaZoneConfig* Zone = Data ? Data->FindZone(ZoneId) : nullptr;
	return (Zone && Zone->bHasAtmosphere) ? &Zone->Atmosphere : nullptr;
}

float ValhallaAtmosphere::GetBaseVisionRange(const AValhallaPlayerState* State)
{
	const float ClassRange = (State && State->VisionRange > 0.f)
		? State->VisionRange
		: static_cast<float>(Valhalla::DefaultVisionRange);

	// B-20 (buffs) and races: multiply personal vision modifiers in here.
	return ClassRange;
}

FValhallaVision ValhallaAtmosphere::ResolveVision(const AValhallaPlayerState* State, FName ZoneId)
{
	FValhallaVision Vision;
	Vision.BaseRangeCm = GetBaseVisionRange(State);
	Vision.EffectiveRangeCm = Vision.BaseRangeCm;
	Vision.ClearRadiusCm = Vision.BaseRangeCm;
	Vision.RelevancyRangeCm = Vision.BaseRangeCm;

	const FName Zone = !ZoneId.IsNone() ? ZoneId : (State ? State->ZoneId : NAME_None);
	const FValhallaAtmosphereProfile* Profile = State ? Find(State, Zone) : nullptr;
	if (!Profile || !Profile->HasVisionFog())
	{
		// No vision fog: exactly the pre-B-06 class range for everything.
		return Vision;
	}

	Vision.bHasVisionFog = true;
	Vision.EffectiveRangeCm = FMath::Max(1.f, Vision.BaseRangeCm * Profile->VisionScale);
	Vision.ClearRadiusCm = Vision.EffectiveRangeCm * FMath::Clamp(Profile->VisionClearFraction, 0.f, 0.99f);
	Vision.RelevancyRangeCm = Vision.EffectiveRangeCm + FMath::Max(0.f, Profile->RelevancyMarginCm);
	return Vision;
}

float ValhallaAtmosphere::GetEffectiveVisionRange(const AValhallaPlayerState* State, FName ZoneId)
{
	return ResolveVision(State, ZoneId).EffectiveRangeCm;
}

float ValhallaAtmosphere::ResolveCameraMaxArm(const FValhallaAtmosphereProfile* Profile, const AValhallaPlayerState* /*State*/)
{
	// Per zone today. A per-class camera modifier (a ranger's wider view,
	// B-20a) would scale this by something read off the player state.
	return (Profile && Profile->CameraMaxArmCm > 0.f) ? Profile->CameraMaxArmCm : 0.f;
}

// ─────────────────────────────────────────────────────────────────────────────
//  FValhallaAtmosphereState
// ─────────────────────────────────────────────────────────────────────────────

FValhallaAtmosphereState FValhallaAtmosphereState::Lerp(const FValhallaAtmosphereState& A, const FValhallaAtmosphereState& B, float Alpha)
{
	FValhallaAtmosphereState Out;
	Out.HeightFogDensity = FMath::Lerp(A.HeightFogDensity, B.HeightFogDensity, Alpha);
	Out.HeightFogStartCm = FMath::Lerp(A.HeightFogStartCm, B.HeightFogStartCm, Alpha);
	Out.HeightFogColor = FMath::Lerp(A.HeightFogColor, B.HeightFogColor, Alpha);
	Out.SunIntensity = FMath::Lerp(A.SunIntensity, B.SunIntensity, Alpha);
	Out.FillScale = FMath::Lerp(A.FillScale, B.FillScale, Alpha);
	Out.SkyIntensity = FMath::Lerp(A.SkyIntensity, B.SkyIntensity, Alpha);
	Out.GradeTint = FMath::Lerp(A.GradeTint, B.GradeTint, Alpha);
	Out.VisionClearRadiusCm = FMath::Lerp(A.VisionClearRadiusCm, B.VisionClearRadiusCm, Alpha);
	Out.VisionFadeWidthCm = FMath::Lerp(A.VisionFadeWidthCm, B.VisionFadeWidthCm, Alpha);
	Out.VisionFogColor = FMath::Lerp(A.VisionFogColor, B.VisionFogColor, Alpha);
	Out.VisionFogStrength = FMath::Lerp(A.VisionFogStrength, B.VisionFogStrength, Alpha);
	Out.FirelightStrength = FMath::Lerp(A.FirelightStrength, B.FirelightStrength, Alpha);
	Out.FirelightRangeCm = FMath::Lerp(A.FirelightRangeCm, B.FirelightRangeCm, Alpha);
	Out.CameraMaxArmCm = FMath::Lerp(A.CameraMaxArmCm, B.CameraMaxArmCm, Alpha);
	return Out;
}

bool FValhallaAtmosphereState::Equals(const FValhallaAtmosphereState& Other) const
{
	constexpr float Small = 1.e-4f;
	constexpr float Cm = 0.01f;
	return FMath::IsNearlyEqual(HeightFogDensity, Other.HeightFogDensity, Small)
		&& FMath::IsNearlyEqual(HeightFogStartCm, Other.HeightFogStartCm, Cm)
		&& HeightFogColor.Equals(Other.HeightFogColor, Small)
		&& FMath::IsNearlyEqual(SunIntensity, Other.SunIntensity, Small)
		&& FMath::IsNearlyEqual(FillScale, Other.FillScale, Small)
		&& FMath::IsNearlyEqual(SkyIntensity, Other.SkyIntensity, Small)
		&& GradeTint.Equals(Other.GradeTint, Small)
		&& FMath::IsNearlyEqual(VisionClearRadiusCm, Other.VisionClearRadiusCm, Cm)
		&& FMath::IsNearlyEqual(VisionFadeWidthCm, Other.VisionFadeWidthCm, Cm)
		&& VisionFogColor.Equals(Other.VisionFogColor, Small)
		&& FMath::IsNearlyEqual(VisionFogStrength, Other.VisionFogStrength, Small)
		&& FMath::IsNearlyEqual(FirelightStrength, Other.FirelightStrength, Small)
		&& FMath::IsNearlyEqual(FirelightRangeCm, Other.FirelightRangeCm, Cm)
		&& FMath::IsNearlyEqual(CameraMaxArmCm, Other.CameraMaxArmCm, Cm);
}

// ─────────────────────────────────────────────────────────────────────────────
//  AValhallaZoneAtmosphere
// ─────────────────────────────────────────────────────────────────────────────

AValhallaZoneAtmosphere::AValhallaZoneAtmosphere()
{
	PrimaryActorTick.bCanEverTick = true;
	// Pre-physics, after the owning controller (a prerequisite set in
	// BeginPlay): the controller has applied this frame's mouse-wheel zoom, and
	// the spring arm, which ticks later, sees the clamped length.
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	bReplicates = false;
	SetHidden(true);
	SetCanBeDamaged(false);

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

AValhallaZoneAtmosphere* AValhallaZoneAtmosphere::EnsureFor(AValhallaPlayerController* Controller)
{
	if (!Controller || !Controller->IsLocalController())
	{
		return nullptr;
	}

	UWorld* World = Controller->GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}

	// One per controller, found the same way AValhallaFogRenderer::EnsureFor
	// finds its own, so the controller's header stays free of it.
	for (AActor* Child : Controller->Children)
	{
		if (AValhallaZoneAtmosphere* Existing = Cast<AValhallaZoneAtmosphere>(Child))
		{
			return Existing;
		}
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Controller;
	SpawnParams.ObjectFlags |= RF_Transient;

	AValhallaZoneAtmosphere* Atmosphere = World->SpawnActor<AValhallaZoneAtmosphere>(
		AValhallaZoneAtmosphere::StaticClass(), FTransform::Identity, SpawnParams);
	if (Atmosphere)
	{
		Atmosphere->OwningController = Controller;
	}
	return Atmosphere;
}

void AValhallaZoneAtmosphere::BeginPlay()
{
	Super::BeginPlay();

	if (!OwningController.IsValid())
	{
		OwningController = Cast<AValhallaPlayerController>(GetOwner());
	}
	if (AValhallaPlayerController* Controller = OwningController.Get())
	{
		AddTickPrerequisiteActor(Controller);
	}
}

void AValhallaZoneAtmosphere::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ShowAllHidden();

	// Destroyed while the world lives on (the controller went away): hand the
	// level back its own look. On a world teardown there is nothing to restore.
	if (EndPlayReason == EEndPlayReason::Destroyed && bHaveBaseline)
	{
		AValhallaPlayerController* Controller = OwningController.Get();
		ApplyState(Baseline, Controller ? Cast<AValhallaCharacter>(Controller->GetPawn()) : nullptr, /*bBlendFinished*/ true);
	}

	Super::EndPlay(EndPlayReason);
}

void AValhallaZoneAtmosphere::CaptureBaseline()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Only the persistent level's actors: that is where lighting_remaster.py
	// puts the one global look. A light in a zone sub-level (a lamp, a fire) is
	// that zone's own and is never touched.
	const ULevel* Persistent = World->PersistentLevel;

	Baseline = FValhallaAtmosphereState();
	Baseline.FillScale = 1.f;
	Baseline.GradeTint = FLinearColor::White;
	Baseline.CameraMaxArmCm = AValhallaCharacter::CameraArmMax;
	Fills.Reset();

	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
	{
		if (It->GetLevel() != Persistent)
		{
			continue;
		}
		if (UExponentialHeightFogComponent* Fog = It->GetComponent())
		{
			HeightFog = Fog;
			Baseline.HeightFogDensity = Fog->FogDensity;
			Baseline.HeightFogStartCm = Fog->StartDistance;
			Baseline.HeightFogColor = Fog->FogInscatteringLuminance;
			break;
		}
	}

	// The sun is the directional light flagged as the atmosphere sun (the
	// "Sun" actor; the cool "Fill" has the flag off). Failing that, the first
	// one that casts shadows, then simply the first.
	TArray<ULightComponent*> Directionals;
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		if (It->GetLevel() == Persistent && It->GetLightComponent())
		{
			Directionals.Add(It->GetLightComponent());
		}
	}

	ULightComponent* SunLight = nullptr;
	for (ULightComponent* Light : Directionals)
	{
		const UDirectionalLightComponent* Directional = Cast<UDirectionalLightComponent>(Light);
		if (Directional && Directional->IsUsedAsAtmosphereSunLight())
		{
			SunLight = Light;
			break;
		}
	}
	for (int32 Index = 0; !SunLight && Index < Directionals.Num(); ++Index)
	{
		if (Directionals[Index]->CastShadows)
		{
			SunLight = Directionals[Index];
		}
	}
	if (!SunLight && Directionals.Num() > 0)
	{
		SunLight = Directionals[0];
	}

	if (SunLight)
	{
		Sun.Light = SunLight;
		Sun.Intensity = SunLight->Intensity;
		Baseline.SunIntensity = SunLight->Intensity;
	}
	for (ULightComponent* Light : Directionals)
	{
		if (Light != SunLight)
		{
			FLightBaseline& Fill = Fills.AddDefaulted_GetRef();
			Fill.Light = Light;
			Fill.Intensity = Light->Intensity;
		}
	}

	for (TActorIterator<ASkyLight> It(World); It; ++It)
	{
		if (It->GetLevel() != Persistent)
		{
			continue;
		}
		if (USkyLightComponent* Sky = It->GetLightComponent())
		{
			SkyLight = Sky;
			Baseline.SkyIntensity = Sky->Intensity;
			break;
		}
	}

	// The grade tint multiplies the global volume's colour gain rather than
	// replacing it, so a tint of white is exactly today's grade.
	BaseColorGain = FVector4(1.0, 1.0, 1.0, 1.0);
	for (TActorIterator<APostProcessVolume> It(World); It; ++It)
	{
		if (It->GetLevel() == Persistent && It->bUnbound && It->bEnabled && It->Settings.bOverride_ColorGain)
		{
			BaseColorGain = It->Settings.ColorGain;
			break;
		}
	}

	bHaveBaseline = true;
	Current = Baseline;
	Target = Baseline;

	UE_LOG(LogValhallaVision, Log,
		TEXT("zone atmosphere baseline in %s: height fog %s (density %.4f, start %.0f), sun %s (%.2f), %d fill light(s), sky light %s (%.2f)"),
		*World->GetName(),
		HeightFog.IsValid() ? TEXT("found") : TEXT("MISSING"), Baseline.HeightFogDensity, Baseline.HeightFogStartCm,
		Sun.Light.IsValid() ? TEXT("found") : TEXT("MISSING"), Baseline.SunIntensity,
		Fills.Num(),
		SkyLight.IsValid() ? TEXT("found") : TEXT("MISSING"), Baseline.SkyIntensity);
}

FValhallaAtmosphereState AValhallaZoneAtmosphere::BuildTarget(const FValhallaAtmosphereProfile* Profile, const FValhallaVision& Vision, const AValhallaPlayerState* State) const
{
	FValhallaAtmosphereState Out = Baseline;

	// Vision fog off, keeping the previous target's radius and colour, so that
	// switching it off fades only its strength instead of also sweeping the
	// circle. This is also stable frame to frame, which matters because a
	// target that differs from the last one restarts the blend.
	Out.VisionClearRadiusCm = Target.VisionClearRadiusCm;
	Out.VisionFadeWidthCm = Target.VisionFadeWidthCm;
	Out.VisionFogColor = Target.VisionFogColor;
	Out.VisionFogStrength = 0.f;
	// The firelight is only ever drawn through the vision fog (PP_Fog
	// multiplies it by the fog's alpha), so it simply keeps its values and
	// fades out with the fog.
	Out.FirelightStrength = Target.FirelightStrength;
	Out.FirelightRangeCm = Target.FirelightRangeCm;

	if (!Profile)
	{
		return Out;
	}

	if (Profile->HeightFogDensity >= 0.f)
	{
		Out.HeightFogDensity = Profile->HeightFogDensity;
	}
	if (Profile->HeightFogStartCm >= 0.f)
	{
		Out.HeightFogStartCm = Profile->HeightFogStartCm;
	}
	if (Profile->bHasFogColor)
	{
		Out.HeightFogColor = Profile->FogColor;
	}

	Out.SunIntensity = Baseline.SunIntensity * Profile->SunIntensityScale;
	Out.FillScale = Profile->SkyLightIntensityScale;
	Out.SkyIntensity = Baseline.SkyIntensity * Profile->SkyLightIntensityScale;

	if (Profile->bHasGradeTint)
	{
		Out.GradeTint = Profile->GradeTint;
	}

	if (Vision.bHasVisionFog)
	{
		// This player's own distances: a ranger's mist starts further out
		// than a wizard's. See ValhallaAtmosphere::ResolveVision.
		Out.VisionClearRadiusCm = Vision.ClearRadiusCm;
		Out.VisionFadeWidthCm = FMath::Max(1.f, Vision.EffectiveRangeCm - Vision.ClearRadiusCm);
		Out.VisionFogColor = Profile->bHasFogColor ? Profile->FogColor : Baseline.HeightFogColor;
		Out.VisionFogStrength = 1.f;
		Out.FirelightStrength = Profile->FirelightGlow;
		Out.FirelightRangeCm = Profile->FirelightRangeCm > 0.f
			? Profile->FirelightRangeCm
			: Vision.EffectiveRangeCm * DefaultFirelightRangeScale;
	}

	const float CameraMax = ValhallaAtmosphere::ResolveCameraMaxArm(Profile, State);
	if (CameraMax > 0.f)
	{
		Out.CameraMaxArmCm = FMath::Clamp(CameraMax, AValhallaCharacter::CameraArmMin, AValhallaCharacter::CameraArmMax);
	}

	return Out;
}

void AValhallaZoneAtmosphere::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	AValhallaPlayerController* Controller = OwningController.Get();
	UWorld* World = GetWorld();
	if (!Controller || !World)
	{
		return;
	}

	if (!bHaveBaseline)
	{
		CaptureBaseline();
	}

	AValhallaCharacter* Pawn = Cast<AValhallaCharacter>(Controller->GetPawn());

	// ── Which zone ──────────────────────────────────────────────────────
	//
	// The pawn's position against the zone volumes in this client's copy of
	// the level — the same local answer AValhallaFogRenderer uses, so the
	// atmosphere switches on the same frame the fog of war does, not a round
	// trip later. Between zones, or with no pawn, the last zone stands.
	if (Pawn)
	{
		if (const UValhallaZoneSubsystem* Zones = World->GetSubsystem<UValhallaZoneSubsystem>())
		{
			if (const FValhallaZoneDef* Zone = Zones->GetZoneAt(Pawn->GetActorLocation()))
			{
				if (Zone->ZoneId != CurrentZoneId)
				{
					UE_LOG(LogValhallaVision, Log, TEXT("zone atmosphere: '%s' -> '%s'%s"),
						CurrentZoneId.IsNone() ? TEXT("-") : *CurrentZoneId.ToString(),
						*Zone->ZoneId.ToString(),
						ValhallaAtmosphere::Find(this, Zone->ZoneId) ? TEXT(" (profile)") : TEXT(" (global look)"));
					CurrentZoneId = Zone->ZoneId;
				}
			}
		}
	}

	if (CurrentZoneId.IsNone())
	{
		// No zone yet (no pawn, or a zoneless test level): the world is
		// already showing its own look, so there is nothing to do.
		return;
	}

	// ── The profile ─────────────────────────────────────────────────────
	//
	// Looked up every frame rather than on the zone change alone: a data hot
	// reload (the web editor's Zones page) changes the profile with no event,
	// and this is one map lookup.
	//
	// The vision distances are this player's own (class range x the zone's
	// scale), for the zone the pawn is standing in.
	const FValhallaAtmosphereProfile* Profile = ValhallaAtmosphere::Find(this, CurrentZoneId);
	const AValhallaPlayerState* LocalState = Pawn ? Pawn->GetValhallaPlayerState() : nullptr;
	const FValhallaVision Vision = ValhallaAtmosphere::ResolveVision(LocalState, CurrentZoneId);
	const FValhallaAtmosphereState NewTarget = BuildTarget(Profile, Vision, LocalState);
	HideLimitCm = Vision.bHasVisionFog ? Vision.EffectiveRangeCm : 0.f;

	if (!bHaveState)
	{
		// The first zone this client knows about is the one it logged in to:
		// arrive in it, rather than fading in from the global look.
		Current = NewTarget;
		Target = NewTarget;
		bHaveState = true;
		bBlending = false;
		ApplyState(Current, Pawn, /*bBlendFinished*/ true);
	}
	else if (!NewTarget.Equals(Target))
	{
		BlendFrom = Current;
		if (BlendFrom.VisionFogStrength <= UE_KINDA_SMALL_NUMBER)
		{
			// Vision fog switching on: start it at its new radius and colour
			// and fade its strength in.
			BlendFrom.VisionClearRadiusCm = NewTarget.VisionClearRadiusCm;
			BlendFrom.VisionFadeWidthCm = NewTarget.VisionFadeWidthCm;
			BlendFrom.VisionFogColor = NewTarget.VisionFogColor;
			BlendFrom.FirelightStrength = NewTarget.FirelightStrength;
			BlendFrom.FirelightRangeCm = NewTarget.FirelightRangeCm;
		}
		Target = NewTarget;
		BlendElapsed = 0.f;
		bBlending = true;
	}

	bool bBlendFinished = !bBlending;
	if (bBlending)
	{
		BlendElapsed += DeltaSeconds;
		const float Alpha = FMath::Clamp(BlendElapsed / BlendSeconds, 0.f, 1.f);
		Current = FValhallaAtmosphereState::Lerp(BlendFrom, Target, FMath::SmoothStep(0.f, 1.f, Alpha));
		if (Alpha >= 1.f)
		{
			Current = Target;
			bBlending = false;
			bBlendFinished = true;
		}
	}

	ApplyState(Current, Pawn, bBlendFinished);

	// ── Hide beyond the fog ─────────────────────────────────────────────
	HideAccumulator += DeltaSeconds;
	if (HideAccumulator >= HideIntervalSeconds)
	{
		HideAccumulator = 0.f;
		UpdateHiddenActors(Pawn, HideLimitCm);
	}
}

void AValhallaZoneAtmosphere::ApplyState(const FValhallaAtmosphereState& State, AValhallaCharacter* Pawn, bool bBlendFinished)
{
	// Every setter below returns early when the value has not changed, so
	// calling them each frame costs nothing once a blend has settled.

	// ── World lighting (the persistent level's, shared by every zone) ────
	if (UExponentialHeightFogComponent* Fog = HeightFog.Get())
	{
		Fog->SetFogDensity(State.HeightFogDensity);
		Fog->SetStartDistance(State.HeightFogStartCm);
		Fog->SetFogInscatteringColor(State.HeightFogColor);
	}
	if (ULightComponent* SunLight = Sun.Light.Get())
	{
		SunLight->SetIntensity(State.SunIntensity);
	}
	for (const FLightBaseline& Fill : Fills)
	{
		if (ULightComponent* Light = Fill.Light.Get())
		{
			Light->SetIntensity(Fill.Intensity * State.FillScale);
		}
	}
	if (USkyLightComponent* Sky = SkyLight.Get())
	{
		Sky->SetIntensity(State.SkyIntensity);
	}

	// ── Grade tint, on the player camera ─────────────────────────────────
	//
	// The camera's own post-process settings sit on top of the level's
	// volumes, so an override here wins without touching the volume. The
	// override is switched off again once the tint is back to white, which
	// leaves the camera exactly as it was before B-06.
	UCameraComponent* Camera = Pawn ? Pawn->GetTopDownCamera() : nullptr;
	if (UCameraComponent* Previous = TintedCamera.Get(); Previous && Previous != Camera)
	{
		Previous->PostProcessSettings.bOverride_ColorGain = false;
		TintedCamera = nullptr;
	}
	if (Camera)
	{
		const bool bTinted = !State.GradeTint.Equals(FLinearColor::White, 0.001f);
		if (bTinted || (!bBlendFinished && TintedCamera.Get() == Camera))
		{
			Camera->PostProcessSettings.bOverride_ColorGain = true;
			Camera->PostProcessSettings.ColorGain = FVector4(
				BaseColorGain.X * State.GradeTint.R,
				BaseColorGain.Y * State.GradeTint.G,
				BaseColorGain.Z * State.GradeTint.B,
				BaseColorGain.W);
			TintedCamera = Camera;
		}
		else if (TintedCamera.Get() == Camera)
		{
			Camera->PostProcessSettings.bOverride_ColorGain = false;
			TintedCamera = nullptr;
		}
	}

	// ── Vision fog (PP_Fog, through the fog renderer) ────────────────────
	if (AValhallaFogRenderer* FogRenderer = FindFogRenderer())
	{
		const FVector Location = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
		FogRenderer->SetVisionFog(
			FVector2D(Location.X, Location.Y),
			State.VisionClearRadiusCm,
			State.VisionFadeWidthCm,
			State.VisionFogColor,
			Pawn ? State.VisionFogStrength : 0.f);
		FogRenderer->SetFirelight(State.FirelightStrength, State.FirelightRangeCm);
	}

	// ── Camera zoom limit ───────────────────────────────────────────────
	//
	// Only when the zone asks for one: at the character's own limit this does
	// nothing, so `valhalla.DebugCameraDistance` still pulls the camera out for
	// screenshots in a zone without a profile.
	if (Pawn && State.CameraMaxArmCm < AValhallaCharacter::CameraArmMax - 0.5f)
	{
		if (USpringArmComponent* Boom = Pawn->GetCameraBoom())
		{
			if (Boom->TargetArmLength > State.CameraMaxArmCm)
			{
				Boom->TargetArmLength = State.CameraMaxArmCm;
			}
		}
	}
}

void AValhallaZoneAtmosphere::UpdateHiddenActors(const APawn* OwnPawn, float LimitCm)
{
	UWorld* World = GetWorld();
	if (!World || !OwnPawn || LimitCm <= 0.f)
	{
		ShowAllHidden();
		return;
	}

	const FVector Origin = OwnPawn->GetActorLocation();
	const double LimitSq = FMath::Square(static_cast<double>(LimitCm));

	auto Consider = [this, OwnPawn, &Origin, LimitSq](AActor* Actor)
	{
		if (!Actor || Actor == OwnPawn)
		{
			return;
		}

		const bool bBeyond = FVector::DistSquared2D(Actor->GetActorLocation(), Origin) > LimitSq;
		const TWeakObjectPtr<AActor> Key(Actor);
		if (bBeyond)
		{
			// Never take over an actor something else hid.
			if (!Actor->IsHidden())
			{
				Actor->SetActorHiddenInGame(true);
				HiddenByFog.Add(Key);
			}
		}
		else if (HiddenByFog.Contains(Key))
		{
			Actor->SetActorHiddenInGame(false);
			HiddenByFog.Remove(Key);
		}
	};

	for (TActorIterator<AValhallaNPC> It(World); It; ++It)
	{
		Consider(*It);
	}
	for (TActorIterator<AValhallaCharacter> It(World); It; ++It)
	{
		Consider(*It);
	}
	for (TActorIterator<AValhallaLootBag> It(World); It; ++It)
	{
		Consider(*It);
	}

	// Actors destroyed (or culled by relevancy) while hidden.
	for (auto It = HiddenByFog.CreateIterator(); It; ++It)
	{
		if (!It->IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

void AValhallaZoneAtmosphere::ShowAllHidden()
{
	for (const TWeakObjectPtr<AActor>& Weak : HiddenByFog)
	{
		if (AActor* Actor = Weak.Get())
		{
			Actor->SetActorHiddenInGame(false);
		}
	}
	HiddenByFog.Reset();
}

AValhallaFogRenderer* AValhallaZoneAtmosphere::FindFogRenderer() const
{
	const AValhallaPlayerController* Controller = OwningController.Get();
	if (!Controller)
	{
		return nullptr;
	}
	for (AActor* Child : Controller->Children)
	{
		if (AValhallaFogRenderer* Renderer = Cast<AValhallaFogRenderer>(Child))
		{
			return Renderer;
		}
	}
	return nullptr;
}
