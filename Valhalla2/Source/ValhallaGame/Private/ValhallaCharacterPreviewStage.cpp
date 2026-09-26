// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaCharacterPreviewStage.h"

#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "ValhallaBackendSubsystem.h"
#include "ValhallaCharacter.h"

DEFINE_LOG_CATEGORY(LogValhallaPreview);

namespace
{
	TAutoConsoleVariable<float> CVarPreviewExposureBias(
		TEXT("valhalla.Preview.ExposureBias"), 0.f,
		TEXT("B-08a: character select preview exposure compensation (EV; manual exposure)."), ECVF_Default);

	TAutoConsoleVariable<float> CVarPreviewKeyLight(
		TEXT("valhalla.Preview.KeyLight"), 60.f,
		TEXT("B-08a: character select preview key light, candelas (warm, front left, above)."), ECVF_Default);

	TAutoConsoleVariable<float> CVarPreviewFillLight(
		TEXT("valhalla.Preview.FillLight"), 20.f,
		TEXT("B-08a: character select preview fill light, candelas (front right)."), ECVF_Default);

	TAutoConsoleVariable<float> CVarPreviewRimLight(
		TEXT("valhalla.Preview.RimLight"), 90.f,
		TEXT("B-08a: character select preview rim light, candelas (gold, behind)."), ECVF_Default);

	TAutoConsoleVariable<float> CVarPreviewDistance(
		TEXT("valhalla.Preview.Distance"), 520.f,
		TEXT("B-08a: character select preview camera distance, cm."), ECVF_Default);

	UPointLightComponent* MakeLight(AActor* Owner, USceneComponent* Parent, const TCHAR* Name, const FVector& Offset, const FColor& Colour, bool bShadows)
	{
		UPointLightComponent* Light = Owner->CreateDefaultSubobject<UPointLightComponent>(Name);
		Light->SetupAttachment(Parent);
		Light->SetRelativeLocation(Offset);
		Light->SetLightColor(Colour);
		Light->SetIntensityUnits(ELightUnits::Candelas);
		Light->SetAttenuationRadius(900.f);
		Light->SetCastShadows(bShadows);
		Light->SetMobility(EComponentMobility::Movable);
		return Light;
	}

	void SetShowFlag(FEngineShowFlags& Flags, const TCHAR* Name, bool bOn)
	{
		const int32 Index = FEngineShowFlags::FindIndexByName(Name);
		if (Index != INDEX_NONE)
		{
			Flags.SetSingleFlag(Index, bOn);
		}
	}
}

AValhallaCharacterPreviewStage::AValhallaCharacterPreviewStage()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;
	bReplicates = false;
	SetCanBeDamaged(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;

	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(Root);
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetVolumetricFog(false);
	Capture->ShowFlags.SetMotionBlur(false);
	SetShowFlag(Capture->ShowFlags, TEXT("LumenGlobalIllumination"), false);
	SetShowFlag(Capture->ShowFlags, TEXT("LumenReflections"), false);
	SetShowFlag(Capture->ShowFlags, TEXT("Grain"), false);

	// Around the character, which stands at the stage's origin facing +X.
	KeyLight = MakeLight(this, Root, TEXT("KeyLight"), FVector(170.f, -150.f, 150.f), FColor(255, 226, 190), true);
	FillLight = MakeLight(this, Root, TEXT("FillLight"), FVector(180.f, 190.f, 40.f), FColor(200, 214, 255), false);
	RimLight = MakeLight(this, Root, TEXT("RimLight"), FVector(-160.f, 60.f, 170.f), FColor(255, 205, 120), false);
}

void AValhallaCharacterPreviewStage::BeginPlay()
{
	Super::BeginPlay();

	RenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("PreviewTarget"));
	RenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
	RenderTarget->ClearColor = FLinearColor::Black;
	RenderTarget->bAutoGenerateMips = false;
	RenderTarget->InitAutoFormat(TargetWidth, TargetHeight);
	RenderTarget->UpdateResourceImmediate(true);
	Capture->TextureTarget = RenderTarget;

	ApplyTuning();
}

void AValhallaCharacterPreviewStage::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Clear();
	Super::EndPlay(EndPlayReason);
}

void AValhallaCharacterPreviewStage::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ApplyTuning();
}

void AValhallaCharacterPreviewStage::ApplyTuning()
{
	KeyLight->SetIntensity(CVarPreviewKeyLight.GetValueOnGameThread());
	FillLight->SetIntensity(CVarPreviewFillLight.GetValueOnGameThread());
	RimLight->SetIntensity(CVarPreviewRimLight.GetValueOnGameThread());

	FPostProcessSettings& Post = Capture->PostProcessSettings;
	Post.bOverride_AutoExposureMethod = true;
	Post.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	Post.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Post.AutoExposureApplyPhysicalCameraExposure = false;
	Post.bOverride_AutoExposureBias = true;
	Post.AutoExposureBias = CVarPreviewExposureBias.GetValueOnGameThread();
	Capture->PostProcessBlendWeight = 1.f;

	if (Character)
	{
		FrameCharacter();
	}
}

void AValhallaCharacterPreviewStage::ShowCharacter(const FValhallaCharacterSummary& Summary)
{
	if (Character && ShownCharacterId == Summary.Id)
	{
		Capture->bCaptureEveryFrame = true;
		return;
	}

	Clear();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	Params.Owner = this;

	// Facing +X, which is where the capture stands: the character looks at the camera.
	Character = World->SpawnActor<AValhallaCharacter>(AValhallaCharacter::StaticClass(), GetActorLocation(), FRotator::ZeroRotator, Params);
	if (!Character)
	{
		UE_LOG(LogValhallaPreview, Warning, TEXT("could not spawn the preview character for '%s'."), *Summary.Name);
		return;
	}

	// A mannequin, not a pawn: no falling, no collision, nothing to possess it.
	if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		Movement->DisableMovement();
		Movement->GravityScale = 0.f;
	}
	Character->SetActorEnableCollision(false);

	Character->ApplyPreviewLoadout(Summary.Equipment, Summary.BodyId);
	ShownCharacterId = Summary.Id;

	Capture->ShowOnlyActors.Reset();
	Capture->ShowOnlyActors.Add(Character);
	FrameCharacter();
	Capture->bCaptureEveryFrame = true;

	int32 Pieces = 0;
	for (const FName& Item : Summary.Equipment)
	{
		Pieces += Item.IsNone() ? 0 : 1;
	}
	UE_LOG(LogValhallaPreview, Log, TEXT("preview: %s (%s lv%d), %d equipped item(s)."),
		*Summary.Name, *Summary.ClassId.ToString(), Summary.Level, Pieces);
}

void AValhallaCharacterPreviewStage::Clear()
{
	if (Character)
	{
		Character->Destroy();
		Character = nullptr;
	}
	ShownCharacterId = INDEX_NONE;

	if (Capture)
	{
		Capture->bCaptureEveryFrame = false;
		Capture->ShowOnlyActors.Reset();
	}
	if (RenderTarget && GetWorld())
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, RenderTarget, FLinearColor::Black);
	}
}

void AValhallaCharacterPreviewStage::FrameCharacter()
{
	if (!Character || !Capture)
	{
		return;
	}

	const float HalfHeight = Character->GetCapsuleComponent()
		? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
		: 90.f;
	const float Distance = FMath::Max(100.f, CVarPreviewDistance.GetValueOnGameThread());

	// Head to toe with room: the frame is 18% taller than the capsule and
	// centred a touch below its middle, so hats clear the top and the feet
	// stand inside the bottom edge, on the screen's floor glow.
	const float FrameHeight = 2.f * HalfHeight * 1.18f;
	const float Aspect = static_cast<float>(TargetWidth) / static_cast<float>(TargetHeight);
	const float HalfWidth = FrameHeight * Aspect * 0.5f;
	Capture->FOVAngle = FMath::RadiansToDegrees(2.f * FMath::Atan(HalfWidth / Distance));

	const FVector Focus = Character->GetActorLocation() + FVector(0.f, 0.f, HalfHeight * 0.02f);
	Capture->SetWorldLocationAndRotation(Focus + FVector(Distance, 0.f, 0.f), FRotator(0.f, 180.f, 0.f));
}
