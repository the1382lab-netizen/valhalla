// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ValhallaCharacterPreviewStage.generated.h"

class AValhallaCharacter;
class UPointLightComponent;
class USceneCaptureComponent2D;
class USceneComponent;
class UTextureRenderTarget2D;
struct FValhallaCharacterSummary;

VALHALLAGAME_API DECLARE_LOG_CATEGORY_EXTERN(LogValhallaPreview, Log, All);

/**
 * B-08a: the character select screen's preview.
 *
 * A small lit stage the front-end controller spawns out of sight in L_FrontEnd
 * (nothing else in that map is ever drawn: the UI covers the screen). It holds
 * one AValhallaCharacter, dressed from the character list's summary with the
 * same code the game dresses players with (ApplyPreviewLoadout), standing still
 * and facing the camera (Kevin, 2026-09-25: no turning, no dragging), and a
 * scene capture that draws only that character into a render target the
 * screen shows on its right-hand side.
 *
 * The character is respawned for each selection rather than re-dressed, so no
 * state from the last one (hair, stance, a slot the new one leaves empty) can
 * leak across. Nothing here replicates; the front end has no server.
 *
 * Tuning without a rebuild: valhalla.Preview.ExposureBias, .KeyLight, .FillLight,
 * .RimLight (candelas), .Distance (cm).
 */
UCLASS(NotPlaceable, Transient)
class VALHALLAGAME_API AValhallaCharacterPreviewStage : public AActor
{
	GENERATED_BODY()

public:
	AValhallaCharacterPreviewStage();

	/** Render target size: a 2:3 portrait, enough for the character at full height. */
	static constexpr int32 TargetWidth = 800;
	static constexpr int32 TargetHeight = 1200;

	/** Show this character (dressed, idle, facing the camera). Same id twice is a no-op. */
	void ShowCharacter(const FValhallaCharacterSummary& Summary);

	/** Remove the character and stop capturing (nothing selected, or a panel covers the preview). */
	void Clear();

	/** What the screen shows. Made in BeginPlay; never null after it. */
	UTextureRenderTarget2D* GetRenderTarget() const { return RenderTarget; }

	/** The character on the stage, if any (tests, logs). */
	AValhallaCharacter* GetCharacter() const { return Character; }

	//~ Begin AActor interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	//~ End AActor interface

protected:
	/** Aim the capture at the character from the front, framing it head to toe. */
	void FrameCharacter();

	/** Apply the tuning cvars to the lights and the capture's exposure. */
	void ApplyTuning();

	UPROPERTY(VisibleAnywhere, Category = "Valhalla|Preview")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Valhalla|Preview")
	TObjectPtr<USceneCaptureComponent2D> Capture;

	UPROPERTY(VisibleAnywhere, Category = "Valhalla|Preview")
	TObjectPtr<UPointLightComponent> KeyLight;

	UPROPERTY(VisibleAnywhere, Category = "Valhalla|Preview")
	TObjectPtr<UPointLightComponent> FillLight;

	UPROPERTY(VisibleAnywhere, Category = "Valhalla|Preview")
	TObjectPtr<UPointLightComponent> RimLight;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	UPROPERTY(Transient)
	TObjectPtr<AValhallaCharacter> Character;

	/** The character id on stage, so a repeat ShowCharacter is free. */
	int32 ShownCharacterId = INDEX_NONE;
};
