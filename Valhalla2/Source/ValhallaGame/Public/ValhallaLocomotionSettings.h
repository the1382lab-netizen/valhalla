// Copyright Valhalla 2.0. All Rights Reserved.
//
// Which locomotion cycle a body plays, by absolute ground speed: walk below
// JogSpeed, jog at JogSpeed and above, less a few cm/s of measuring tolerance
// (UValhallaAnimComponent::ChooseGait).
// The same numbers drive every body: players, simulated proxies and NPCs,
// on the MetaHuman and on the legacy body.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ValhallaLocomotionSettings.generated.h"

/**
 * Project Settings > Valhalla > Locomotion. Saved to Config/DefaultGame.ini
 * under [/Script/ValhallaGame.ValhallaLocomotionSettings].
 *
 * Read every tick through GetDefault, so a change in Project Settings shows up
 * in a running PIE session.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Locomotion"))
class VALHALLAGAME_API UValhallaLocomotionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UValhallaLocomotionSettings();

	/**
	 * Ground speed at and above which a body jogs, cm/s. Below it walks.
	 * Exactly this speed jogs (every class but the rogue has baseSpeed 200).
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Gait", meta = (ClampMin = "1", Units = "CentimetersPerSecond"))
	float JogSpeed = 200.f;

	/**
	 * How far under JogSpeed a measured speed still counts as JogSpeed, cm/s:
	 * a body jogs at Speed >= JogSpeed - JogTolerance. 200 is jog; the
	 * tolerance only absorbs the movement component measuring a hair under
	 * MaxWalkSpeed (a character capped at 200 reads 199.x), so every class at
	 * its base speed jogs. Not a lower jog threshold: keep it a few cm/s.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Gait", meta = (ClampMin = "0", Units = "CentimetersPerSecond"))
	float JogTolerance = 4.f;

	/**
	 * How far below the jog threshold (JogSpeed - JogTolerance) a jogging body
	 * has to slow before it drops back to a walk, cm/s, so a speed hovering at
	 * the threshold does not flicker between the two cycles. Going up is never
	 * delayed. Local and server bodies; other clients' views use ProxyHysteresis.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Gait", meta = (ClampMin = "0", Units = "CentimetersPerSecond"))
	float GaitHysteresis = 10.f;

	/**
	 * GaitHysteresis for a simulated proxy (another player or an NPC as seen on
	 * a client), cm/s. Its replicated velocity jitters by far more than 10 cm/s,
	 * so a proxy jogging at 200 would otherwise flicker Walk / Jog. The proxy's
	 * speed is also smoothed (~0.15 s) before the gait is chosen.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Gait", meta = (ClampMin = "0", Units = "CentimetersPerSecond"))
	float ProxyHysteresis = 40.f;

	/** Ground speed at and above which a body is moving at all (idle below it), cm/s. Also cancels an emote. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Gait", meta = (ClampMin = "0", Units = "CentimetersPerSecond"))
	float MovingSpeed = 10.f;

	/**
	 * Play the walk / jog cycle at speed / (the clip's authored speed x the
	 * body's scale), so the planted foot keeps pace with the ground. Off: both
	 * cycles play at their authored rate whatever the speed.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Play Rate")
	bool bMatchPlayRateToSpeed = true;

	/** Slowest the walk / jog cycle plays when matching speed. Below it the feet slide rather than the cycle crawling. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Play Rate", meta = (ClampMin = "0.05", ClampMax = "4", EditCondition = "bMatchPlayRateToSpeed"))
	float MinPlayRate = 0.6f;

	/** Fastest the walk / jog cycle plays when matching speed. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Play Rate", meta = (ClampMin = "0.05", ClampMax = "4", EditCondition = "bMatchPlayRateToSpeed"))
	float MaxPlayRate = 1.6f;
};
