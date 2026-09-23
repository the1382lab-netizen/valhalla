// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaLootBag.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "ValhallaCombatLibrary.h"
#include "ValhallaConstants.h"
#include "ValhallaDataSubsystem.h"
#include "ValhallaInventoryLibrary.h"
#include "ValhallaVisibilitySubsystem.h"

namespace
{
	/** A unit sphere, scaled down. The engine ships it, so the bag needs no art. */
	const TCHAR* BagMeshPath = TEXT("/Engine/BasicShapes/Sphere.Sphere");

	/** The same tint material the character and NPC proxies use. */
	const TCHAR* ProxyMaterialPath = TEXT("/Game/Valhalla/Materials/M_ClassProxy");

	/** The `ClassColor` parameter on M_ClassProxy. */
	const FName ClassColorParameter(TEXT("ClassColor"));

	/** Sack brown. Distinct from every class colour and from the enemy red. */
	const FLinearColor BagColour(0.20f, 0.11f, 0.04f);

	/** The engine sphere is 100 cm across; a loot bag is about a third of a player. */
	constexpr float BagMeshScale = 0.32f;
}

AValhallaLootBag::AValhallaLootBag()
{
	PrimaryActorTick.bCanEverTick = false;

	ProxyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProxyMesh"));
	RootComponent = ProxyMesh;
	ProxyMesh->SetRelativeScale3D(FVector(BagMeshScale));
	ProxyMesh->SetGenerateOverlapEvents(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> BagMeshAsset(BagMeshPath);
	if (BagMeshAsset.Succeeded())
	{
		ProxyMesh->SetStaticMesh(BagMeshAsset.Object);
	}

	// A bag is a click target and nothing else: it must answer the cursor trace,
	// and it must never push a character around or block a projectile.
	ProxyMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ProxyMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	ProxyMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// The click target: bigger than the bag, invisible, and the only thing in
	// the game on the Interact channel. Attached with an absolute scale so the
	// proxy's 0.32 does not shrink it.
	ClickTarget = CreateDefaultSubobject<USphereComponent>(TEXT("ClickTarget"));
	ClickTarget->SetupAttachment(ProxyMesh);
	ClickTarget->SetUsingAbsoluteScale(true);
	ClickTarget->InitSphereRadius(ClickRadius);
	ClickTarget->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ClickTarget->SetCollisionResponseToAllChannels(ECR_Ignore);
	ClickTarget->SetCollisionResponseToChannel(InteractChannel, ECR_Block);
	ClickTarget->SetGenerateOverlapEvents(false);
	ClickTarget->SetHiddenInGame(true);

	bReplicates = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(5.f);

	// Bags do not move, and a client that cannot see one has no use for its
	// contents. Relevancy culling is the 2.0 equivalent of 1.0 only syncing the
	// lootBags map to clients in the zone.
	// Phase 5 replaces the 2000 cm cull with line of sight, which subsumes it:
	// the override below tests the viewer's own vision range first, and 2000 cm
	// was never the right number for a ranger anyway.
	bAlwaysRelevant = false;
	NetCullDistanceSquared = 1.0e12f;
}

bool AValhallaLootBag::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& /*SrcLocation*/) const
{
	return UValhallaVisibilitySubsystem::IsRelevantForViewer(this, RealViewer, ViewTarget);
}

void AValhallaLootBag::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AValhallaLootBag, Items);
	DOREPLIFETIME(AValhallaLootBag, OwnerName);
	DOREPLIFETIME(AValhallaLootBag, CreatedAt);
}

void AValhallaLootBag::BeginPlay()
{
	Super::BeginPlay();

	if (ProxyMesh && ProxyMesh->GetStaticMesh())
	{
		UMaterialInterface* Source = LoadObject<UMaterialInterface>(nullptr, ProxyMaterialPath);
		if (!Source)
		{
			Source = ProxyMesh->GetMaterial(0);
		}
		if (Source)
		{
			ProxyMaterial = UMaterialInstanceDynamic::Create(Source, this);
			ProxyMesh->SetMaterial(0, ProxyMaterial);
			// M_ClassProxy's parameter is called ClassColor; a bag is not a class,
			// but the material is just a tint and reusing it costs nothing.
			ProxyMaterial->SetVectorParameterValue(ClassColorParameter, BagColour);
		}
	}

	if (!HasAuthority())
	{
		return;
	}

	// LootBagSystem.ts:124 — five minutes from creation, and a merge does not
	// extend it. SetLifeSpan is the engine's version of the same sweep.
	SetLifeSpan(static_cast<float>(Valhalla::LootBagDespawnMs / 1000.0));
}

bool AValhallaLootBag::IsWithinReach(const AActor* Player) const
{
	if (!Player)
	{
		return false;
	}

	// LootBagSystem.ts:170 — squared distance against LOOT_BAG_PICKUP_RANGE,
	// horizontal only.
	const double DistanceSquared = FVector::DistSquared2D(Player->GetActorLocation(), GetActorLocation());
	return DistanceSquared <= Valhalla::LootBagPickupRange * Valhalla::LootBagPickupRange;
}

void AValhallaLootBag::AddItems(const TArray<FValhallaBagSlot>& NewItems)
{
	if (!HasAuthority() || NewItems.Num() == 0)
	{
		return;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	const UValhallaDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;

	auto FindItem = [Data](FName ItemId) -> const FValhallaItemTemplate*
	{
		return Data ? Data->FindItem(ItemId) : nullptr;
	};

	UValhallaInventoryLibrary::AddItemsToBag(Items, NewItems, FindItem);
}

void AValhallaLootBag::DestroyIfEmpty()
{
	if (HasAuthority() && Items.Num() == 0)
	{
		UE_LOG(LogValhallaInventory, Log, TEXT("lootBag emptied and destroyed at %s"), *GetActorLocation().ToCompactString());
		Destroy();
	}
}

AValhallaLootBag* AValhallaLootBag::FindNearbyBag(UWorld* World, const FVector& Location)
{
	if (!World)
	{
		return nullptr;
	}

	const double RangeSquared = Valhalla::LootBagMergeRange * Valhalla::LootBagMergeRange;

	for (TActorIterator<AValhallaLootBag> It(World); It; ++It)
	{
		AValhallaLootBag* Bag = *It;
		if (!IsValid(Bag))
		{
			continue;
		}

		// LootBagSystem.ts:187 — a full bag is not a merge candidate; the next
		// kill on the same spot opens a second one.
		if (Bag->Items.Num() >= Valhalla::LootBagMaxSlots)
		{
			continue;
		}

		if (FVector::DistSquared2D(Bag->GetActorLocation(), Location) <= RangeSquared)
		{
			return Bag;
		}
	}

	return nullptr;
}

AValhallaLootBag* AValhallaLootBag::SpawnOrMerge(UWorld* World, const FVector& Location, const TArray<FValhallaBagSlot>& NewItems, const FString& InOwnerName)
{
	// LootBagSystem.ts:34 — nothing to drop is not an empty bag, it is no bag.
	if (!World || NewItems.Num() == 0)
	{
		return nullptr;
	}

	if (AValhallaLootBag* Nearby = FindNearbyBag(World, Location))
	{
		Nearby->AddItems(NewItems);

		UE_LOG(LogValhallaInventory, Log, TEXT("lootBag merged %d stack(s) from %s into the bag at %s (now %d slots)"),
			NewItems.Num(), *InOwnerName, *Nearby->GetActorLocation().ToCompactString(), Nearby->Items.Num());

		return Nearby;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FVector SpawnLocation = Location + FVector(0.f, 0.f, GroundOffset);

	AValhallaLootBag* Bag = World->SpawnActor<AValhallaLootBag>(AValhallaLootBag::StaticClass(), SpawnLocation, FRotator::ZeroRotator, SpawnParameters);
	if (!Bag)
	{
		UE_LOG(LogValhallaInventory, Error, TEXT("Failed to spawn a loot bag at %s."), *SpawnLocation.ToCompactString());
		return nullptr;
	}

	Bag->OwnerName = InOwnerName;
	Bag->CreatedAt = UValhallaCombatLibrary::GetServerTime(Bag);
	Bag->AddItems(NewItems);

	FString Contents;
	for (const FValhallaBagSlot& Slot : Bag->Items)
	{
		Contents += FString::Printf(TEXT("%s x%d "), *Slot.ItemId.ToString(), Slot.Quantity);
	}

	UE_LOG(LogValhallaInventory, Log, TEXT("lootBag spawned at %s from %s with %d slot(s): %s"),
		*SpawnLocation.ToCompactString(), *InOwnerName, Bag->Items.Num(), *Contents.TrimEnd());

	return Bag;
}
