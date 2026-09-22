// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaNPCSpawner.h"

#include "Components/BillboardComponent.h"
#include "Engine/World.h"
#include "ValhallaDataSubsystem.h"
#include "ValhallaGame.h"
#include "ValhallaNPC.h"

AValhallaNPCSpawner::AValhallaNPCSpawner()
{
	PrimaryActorTick.bCanEverTick = false;

	// Not replicated: the spawner is a piece of level authoring, and the things
	// it makes replicate on their own.
	bReplicates = false;

	Marker = CreateDefaultSubobject<UBillboardComponent>(TEXT("Marker"));
	SetRootComponent(Marker);
	Marker->SetHiddenInGame(true);
}

void AValhallaNPCSpawner::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		SpawnGroup();
	}
}

FVector AValhallaNPCSpawner::ComputeHomeLocation(int32 Index) const
{
	const FVector Origin = GetActorLocation();

	if (Count <= 1 || SpawnRadius <= 0.f)
	{
		return Origin;
	}

	// An even ring rather than a random scatter: deterministic, so the same
	// level always produces the same encounter, and evenly spaced, so three
	// enemies do not spawn inside each other and shove each other apart on the
	// first tick.
	const float AngleRadians = (2.f * PI * Index) / static_cast<float>(Count);
	return Origin + FVector(
		FMath::Cos(AngleRadians) * SpawnRadius,
		FMath::Sin(AngleRadians) * SpawnRadius,
		0.f);
}

void AValhallaNPCSpawner::SpawnGroup()
{
	UWorld* World = GetWorld();
	if (!World || Count <= 0)
	{
		return;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	const UValhallaDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
	if (!Data)
	{
		UE_LOG(LogValhallaGame, Error, TEXT("%s: no data subsystem; nothing spawned."), *GetName());
		return;
	}

	const FValhallaNPCTemplate* Template = Data->FindNPCTemplate(TemplateId);
	if (!Template)
	{
		// NPCSystem.ts:80 warned and skipped rather than failing. A designer's
		// typo should cost one empty spawner, not the level.
		UE_LOG(LogValhallaGame, Warning, TEXT("%s: unknown NPC template '%s'; nothing spawned."),
			*GetName(), *TemplateId.ToString());
		return;
	}

	SpawnedNPCs.Reset();
	SpawnedNPCs.Reserve(Count);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector HomeLocation = ComputeHomeLocation(Index);

		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		// AdjustIfPossibleButAlwaysSpawn: a spawner dropped slightly inside a
		// wall should nudge its NPCs clear rather than silently spawn nothing.
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		AValhallaNPC* Npc = World->SpawnActor<AValhallaNPC>(AValhallaNPC::StaticClass(), HomeLocation, FRotator::ZeroRotator, SpawnParams);
		if (!Npc)
		{
			UE_LOG(LogValhallaGame, Warning, TEXT("%s: NPC %d failed to spawn at %s."), *GetName(), Index, *HomeLocation.ToCompactString());
			continue;
		}

		// The *settled* location is the home, not the requested one: if the
		// collision handling moved it, its leash anchor has to move with it or
		// it would leash the moment it took a step.
		Npc->InitializeFromTemplate(*Template, Npc->GetActorLocation(), this);

		// After InitializeFromTemplate, which is what writes the template's own
		// name in. See DebugLabel.
		if (!DebugLabel.IsEmpty())
		{
			Npc->DisplayName = Count > 1
				? FString::Printf(TEXT("%s%d"), *DebugLabel, Index + 1)
				: DebugLabel;
		}

		SpawnedNPCs.Add(Npc);
	}

	UE_LOG(LogValhallaGame, Log, TEXT("%s spawned %d/%d '%s' around %s (radius %.0f)."),
		*GetName(), SpawnedNPCs.Num(), Count, *TemplateId.ToString(),
		*GetActorLocation().ToCompactString(), SpawnRadius);
}
