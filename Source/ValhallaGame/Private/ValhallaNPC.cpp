// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaNPC.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "ValhallaAnimComponent.h"
#include "ValhallaCharacter.h"
#include "ValhallaCombatLibrary.h"
#include "ValhallaConstants.h"
#include "ValhallaGame.h"
#include "ValhallaGameMode.h"
#include "ValhallaNPCSpawner.h"
#include "ValhallaPlayerState.h"
#include "ValhallaVisibilitySubsystem.h"
#include "ValhallaVisuals.h"

namespace
{
	/** The `BaseColor` parameter on M_ValhallaToon. */
	const FName BaseColorParameter(TEXT("BaseColor"));

	/** Section 0 of the body mesh is `M_Skin`, which is what the tint replaces. */
	constexpr int32 SkinMaterialIndex = 0;

	/**
	 * Enemies are a green-grey whatever their template's spriteColor says.
	 *
	 * All three placeholder templates ship spriteColor 0xFFFFFF, which would make
	 * every enemy an identical white figure indistinguishable from a player. The
	 * "Test Enemy" green-grey is the one thing that makes the grey-box readable
	 * at a glance; a template that authors a real colour gets it instead.
	 */
	const FColor DefaultEnemySrgb(0x6F, 0x8A, 0x63);

	/** The fixed kit a placeholder enemy wears, by `spriteId`. */
	const TCHAR* EnemyChestAsset = TEXT("/Game/Valhalla/Characters/Equipment/SK_chest_priests_chain");
	const TCHAR* EnemyHelmAsset = TEXT("/Game/Valhalla/Characters/Equipment/SK_helm_iron_full");

	/** NPCSystem.ts:941 — DoTs tick once a second. */
	constexpr double BuffTickIntervalSeconds = 1.0;

	/** NPCSystem.ts:500 — the fallback when a template omits aggroRange. */
	constexpr float DefaultAggroRange = 200.f;
}

AValhallaNPC::AValhallaNPC()
{
	// Ticked by AValhallaGameState, in GameRoom.update's order. See the header.
	PrimaryActorTick.bCanEverTick = false;

	GetCapsuleComponent()->InitCapsuleSize(CapsuleRadius, CapsuleHalfHeight);

	// An NPC faces where it is going, so unlike the player it *does* orient to
	// movement — 1.0 set `npc.aimAngle = atan2(dy, dx)` every chase tick, which
	// is the same thing said differently.
	bUseControllerRotationYaw = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.f, 540.f, 0.f);
	Movement->MaxAcceleration = 4096.f;
	Movement->BrakingDecelerationWalking = 4096.f;
	Movement->MaxWalkSpeed = 60.f;
	Movement->GetNavAgentPropertiesRef().bCanJump = false;
	Movement->GetNavAgentPropertiesRef().bCanCrouch = false;

	JumpMaxCount = 0;

	// ── Body ────────────────────────────────────────────────────────────
	// The same rig, the same skeleton and the same animations as a player, with
	// a fixed set of worn pieces instead of a replicated paperdoll. An enemy is
	// a person, and Phase 4c's whole point is that it should look like one.
	BodyMesh = GetMesh();
	BodyMesh->SetRelativeLocation(FVector(0.f, 0.f, UValhallaVisuals::MeshZOffset));
	BodyMesh->SetRelativeRotation(FRotator(0.f, UValhallaVisuals::MeshYaw, 0.f));
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->SetCollisionProfileName(TEXT("NoCollision"));
	BodyMesh->SetGenerateOverlapEvents(false);
	BodyMesh->SetVisibility(true);

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> BodyAsset(
		TEXT("/Game/Valhalla/Characters/Body/SK_Valhalla_Body"));
	if (BodyAsset.Succeeded())
	{
		BodyMesh->SetSkeletalMeshAsset(BodyAsset.Object);
	}

	ChestMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("ChestMesh"));
	ChestMesh->SetupAttachment(BodyMesh);
	HelmMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("HelmMesh"));
	HelmMesh->SetupAttachment(BodyMesh);

	for (USkeletalMeshComponent* Follower : { ChestMesh.Get(), HelmMesh.Get() })
	{
		Follower->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Follower->SetCollisionProfileName(TEXT("NoCollision"));
		Follower->SetGenerateOverlapEvents(false);
		Follower->SetCastShadow(false);
		Follower->bUseAttachParentBound = true;
		Follower->SetLeaderPoseComponent(BodyMesh);
	}

	AnimComponent = CreateDefaultSubobject<UValhallaAnimComponent>(TEXT("AnimComponent"));

	bReplicates = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(20.f);

	// An NPC is a click target, so it must block the cursor trace even though it
	// never blocks a pawn's movement channel in any interesting way.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// ── Phase 5 relevancy ───────────────────────────────────────────────
	//
	// An NPC behind a wall is the case the whole phase exists for: until now a
	// client was sent every NPC in the level and could read their positions and
	// health straight out of memory. The cull distance is left enormous so that
	// the line-of-sight override below is the only thing that decides.
	bAlwaysRelevant = false;
	NetCullDistanceSquared = 1.0e12f;
}

bool AValhallaNPC::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& /*SrcLocation*/) const
{
	return UValhallaVisibilitySubsystem::IsRelevantForViewer(this, RealViewer, ViewTarget);
}

void AValhallaNPC::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AValhallaNPC, Hp);
	DOREPLIFETIME(AValhallaNPC, MaxHp);
	DOREPLIFETIME(AValhallaNPC, Level);
	DOREPLIFETIME(AValhallaNPC, DisplayName);
	DOREPLIFETIME(AValhallaNPC, bAlive);
	DOREPLIFETIME(AValhallaNPC, TemplateId);
	DOREPLIFETIME(AValhallaNPC, SyncedBuffs);

	// The threat table is not here and never will be. Knowing who an NPC is
	// about to switch to is a tactical advantage the server does not give away.
}

void AValhallaNPC::BeginPlay()
{
	Super::BeginPlay();

	if (AnimComponent)
	{
		AnimComponent->SetBodyMesh(BodyMesh);
	}

	ApplyAppearance();
	OnRep_Alive();
}

void AValhallaNPC::ApplyAppearance()
{
	if (!BodyMesh || !BodyMesh->GetSkeletalMeshAsset())
	{
		return;
	}

	// `spriteSize` scales the whole body, exactly as it scaled the 1.0 sprite.
	const float SpriteSize = Template.SpriteSize > 0.f ? Template.SpriteSize : 1.f;
	BodyMesh->SetRelativeScale3D(FVector(SpriteSize));
	// The mesh sits on the capsule bottom whatever it is scaled to, so the
	// offset scales with it rather than leaving a large enemy's feet buried.
	BodyMesh->SetRelativeLocation(FVector(0.f, 0.f, UValhallaVisuals::MeshZOffset * SpriteSize));

	if (!TintMaterial)
	{
		if (UMaterialInterface* Source = BodyMesh->GetMaterial(SkinMaterialIndex))
		{
			TintMaterial = UMaterialInstanceDynamic::Create(Source, this);
			BodyMesh->SetMaterial(SkinMaterialIndex, TintMaterial);
		}
	}

	if (TintMaterial)
	{
		FColor Srgb = DefaultEnemySrgb;
		// 0xFFFFFF is the editor's "no colour chosen" default, so treat it as
		// unauthored rather than as a deliberate white.
		if (Template.SpriteColor != 0 && Template.SpriteColor != 0xFFFFFF)
		{
			Srgb = FColor(
				static_cast<uint8>((Template.SpriteColor >> 16) & 0xFF),
				static_cast<uint8>((Template.SpriteColor >> 8) & 0xFF),
				static_cast<uint8>(Template.SpriteColor & 0xFF));
		}
		TintMaterial->SetVectorParameterValue(BaseColorParameter, FLinearColor::FromSRGBColor(Srgb));
	}

	// The fixed kit. Not driven by data because an NPC has no inventory and no
	// equipment slots to replicate — a template that wants a different look in
	// Phase 8 gets `spriteId` fields of its own rather than a paperdoll.
	struct FPiece { USkeletalMeshComponent* Component; const TCHAR* Asset; };
	const FPiece Pieces[] = { { ChestMesh, EnemyChestAsset }, { HelmMesh, EnemyHelmAsset } };

	for (const FPiece& Piece : Pieces)
	{
		if (!Piece.Component || Piece.Component->GetSkeletalMeshAsset())
		{
			continue;
		}

		USkeletalMesh* LoadedMesh = LoadObject<USkeletalMesh>(nullptr, Piece.Asset);
		if (!LoadedMesh)
		{
			UE_LOG(LogValhallaVisual, Warning, TEXT("npc asset=%s MISSING"), Piece.Asset);
			continue;
		}

		Piece.Component->SetSkeletalMeshAsset(LoadedMesh);
		// Re-linked after the mesh swap: SetSkeletalMeshAsset drops the link.
		Piece.Component->SetLeaderPoseComponent(BodyMesh);
		UE_LOG(LogValhallaVisual, Log, TEXT("npc=%s asset=%s"), *DisplayName, Piece.Asset);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Spawning
// ─────────────────────────────────────────────────────────────────────────────

void AValhallaNPC::InitializeFromTemplate(const FValhallaNPCTemplate& InTemplate, const FVector& InHomeLocation, AValhallaNPCSpawner* InSpawner)
{
	if (!HasAuthority())
	{
		return;
	}

	Template = InTemplate;
	HomeLocation = InHomeLocation;
	Spawner = InSpawner;

	TemplateId = InTemplate.Id;
	DisplayName = InTemplate.Name;
	Level = FMath::Max(1, InTemplate.Level);
	MaxHp = InTemplate.Hp;
	Hp = InTemplate.Hp;
	bAlive = true;

	// NPCSystem.ts:94 — leashRange defaults to three times the aggro range, so a
	// template that only tunes aggro still gets a sensible chase limit.
	LeashRange = InTemplate.LeashRange > 0.f
		? InTemplate.LeashRange
		: (InTemplate.AggroRange > 0.f ? InTemplate.AggroRange : DefaultAggroRange) * 3.f;

	// 1.0 pixels per second become cm per second unchanged.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->MaxWalkSpeed = InTemplate.MoveSpeed > 0.f ? InTemplate.MoveSpeed : 60.f;
	}

	ThreatTable.Reset();
	ActiveBuffs.Reset();
	SyncedBuffs.Reset();
	AggroTarget = nullptr;
	RespawnAt = 0.0;
	LastAttackTime = 0.0;

	UE_LOG(LogValhallaGame, Log, TEXT("NPC '%s' (%s) spawned at %s: hp=%.0f lv%d aggro=%.0f leash=%.0f speed=%.0f dmg=%.0f every %.0f ms, xp=%d"),
		*DisplayName, *TemplateId.ToString(), *InHomeLocation.ToCompactString(),
		MaxHp, Level, InTemplate.AggroRange, LeashRange, InTemplate.MoveSpeed,
		InTemplate.Damage, InTemplate.AttackSpeedMs, InTemplate.XpReward);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Visibility
// ─────────────────────────────────────────────────────────────────────────────

void AValhallaNPC::OnRep_Alive()
{
	// `A_Death` plays once and holds its last frame, so a corpse lies where it
	// fell until the respawn timer stands it back up — which is also what makes
	// a loot bag land next to a body rather than next to nothing. Still no
	// ragdoll: the import pipeline generates no physics asset, and an authored
	// death is the only one that looks identical on every client.
	if (AnimComponent)
	{
		AnimComponent->SetDead(!bAlive);
	}
	SetActorEnableCollision(bAlive);

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		if (!bAlive)
		{
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
		}
		else
		{
			Movement->SetMovementMode(MOVE_Walking);
		}
	}
}

void AValhallaNPC::OnRep_Hp()
{
	// Nothing to do yet: AValhallaHUD reads Hp directly when it draws a
	// nameplate. The hook exists so Phase 8's damage flash has somewhere to go.
}

// ─────────────────────────────────────────────────────────────────────────────
//  Damage, threat and death — NPCSystem.ts:279 / 306
// ─────────────────────────────────────────────────────────────────────────────

int32 AValhallaNPC::ApplyDamageFromAttacker(AActor* Attacker, int32 Damage, double Now, bool& bOutDied)
{
	bOutDied = false;

	if (!HasAuthority() || !bAlive || Damage <= 0)
	{
		return 0;
	}

	Hp = FMath::Max(0.f, Hp - Damage);
	LastAttacker = Attacker;

	// NPCSystem.ts:286 — damage dealt is threat generated, and this is the only
	// place threat is created by combat. A `canAggro: false` NPC takes the damage
	// but never builds a table, so it never fights back.
	const bool bCanAggro = Template.bCanAggro || Template.Type == EValhallaNPCType::Enemy;
	if (bCanAggro && Attacker)
	{
		float& Threat = ThreatTable.FindOrAdd(Attacker);
		Threat += Damage;
	}

	if (Hp <= 0.f)
	{
		bOutDied = true;
		Die(Attacker, Now);
		return Template.XpReward;
	}

	return 0;
}

void AValhallaNPC::Die(AActor* Killer, double Now)
{
	if (!HasAuthority() || !bAlive)
	{
		return;
	}

	bAlive = false;
	Hp = 0.f;
	RespawnAt = Now + Template.RespawnMs / 1000.0;
	AggroTarget = nullptr;
	ThreatTable.Reset();
	ActiveBuffs.Reset();
	SyncedBuffs.Reset();

	OnRep_Alive();

	FValhallaCombatEvent Event;
	Event.Kind = EValhallaCombatEventKind::NpcDied;
	Event.Target = this;
	Event.Instigator = Killer;
	Event.Amount = static_cast<float>(Template.XpReward);
	Event.RemainingHp = 0.f;
	Event.Location = GetActorLocation();
	UValhallaCombatLibrary::BroadcastCombatEvent(this, Event);

	UE_LOG(LogValhallaCombat, Log, TEXT("npcDied %s killed by %s, xpReward=%d, respawn in %.0f ms"),
		*DisplayName, *UValhallaCombatLibrary::GetDisplayName(Killer), Template.XpReward, Template.RespawnMs);

	// GameRoom.broadcastCombatEvents:942 — loot and XP are the game mode's job,
	// not the NPC's. Phase 2c's party split and loot tables hang off this hook.
	if (AValhallaGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AValhallaGameMode>() : nullptr)
	{
		GameMode->OnNPCKilled(this, Killer);
	}
}

void AValhallaNPC::Respawn()
{
	if (!HasAuthority() || bAlive)
	{
		return;
	}

	bAlive = true;
	Hp = MaxHp;
	RespawnAt = 0.0;
	AggroTarget = nullptr;
	ThreatTable.Reset();
	ActiveBuffs.Reset();
	SyncedBuffs.Reset();

	SetActorLocation(HomeLocation, false, nullptr, ETeleportType::TeleportPhysics);
	OnRep_Alive();

	UE_LOG(LogValhallaCombat, Log, TEXT("%s respawned at %s with %.0f hp."),
		*DisplayName, *HomeLocation.ToCompactString(), MaxHp);
}

void AValhallaNPC::ReapplyTemplate(const FValhallaNPCTemplate& NewTemplate)
{
	if (!HasAuthority())
	{
		return;
	}

	// Taken before MaxHp moves. A dead NPC has Hp 0 and stays on 0.
	const float HpFraction = MaxHp > 0.f ? FMath::Clamp(Hp / MaxHp, 0.f, 1.f) : 1.f;

	Template = NewTemplate;

	TemplateId = NewTemplate.Id;
	DisplayName = NewTemplate.Name;
	Level = FMath::Max(1, NewTemplate.Level);
	MaxHp = NewTemplate.Hp;
	Hp = bAlive ? FMath::Max(1.f, MaxHp * HpFraction) : 0.f;

	// Same defaulting as InitializeFromTemplate — NPCSystem.ts:94.
	LeashRange = NewTemplate.LeashRange > 0.f
		? NewTemplate.LeashRange
		: (NewTemplate.AggroRange > 0.f ? NewTemplate.AggroRange : DefaultAggroRange) * 3.f;

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->MaxWalkSpeed = NewTemplate.MoveSpeed > 0.f ? NewTemplate.MoveSpeed : 60.f;
	}

	ApplyAppearance();
}

void AValhallaNPC::AddThreat(AActor* Player, float BonusThreat, bool bForceTarget)
{
	if (!HasAuthority() || !bAlive || !Player)
	{
		return;
	}

	float& Threat = ThreatTable.FindOrAdd(Player);
	Threat += BonusThreat;

	if (bForceTarget)
	{
		AggroTarget = Player;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Buffs — NPCSystem.ts:406
// ─────────────────────────────────────────────────────────────────────────────

void AValhallaNPC::ApplyNPCBuff(const FValhallaActiveBuff& Buff)
{
	if (!HasAuthority())
	{
		return;
	}

	// SkillEffectHandler.ts:213 — NPCs use replace semantics unconditionally. A
	// stacking DoT on an NPC would need the same stackingMode plumbing players
	// have, and no 1.0 skill needs it, so the simple rule is the ported one.
	UValhallaCombatLibrary::ApplyBuff(ActiveBuffs, Buff, EValhallaStackingMode::Replace, 1);
	SyncBuffsToReplicatedView();
}

void AValhallaNPC::SyncBuffsToReplicatedView()
{
	SyncedBuffs.Reset(ActiveBuffs.Num());
	for (const FValhallaActiveBuff& Buff : ActiveBuffs)
	{
		FValhallaNPCBuffInfo Info;
		Info.SkillId = Buff.SkillId;
		Info.ExpiresAt = Buff.ExpiresAt;
		Info.DotDamagePerSec = Buff.DotDamagePerSec;
		SyncedBuffs.Add(Info);
	}
}

void AValhallaNPC::TickBuffs(double Now)
{
	if (ActiveBuffs.Num() == 0)
	{
		return;
	}

	bool bChanged = false;

	for (int32 Index = ActiveBuffs.Num() - 1; Index >= 0; --Index)
	{
		FValhallaActiveBuff& Buff = ActiveBuffs[Index];

		if (Now >= Buff.ExpiresAt)
		{
			ActiveBuffs.RemoveAt(Index);
			bChanged = true;
			continue;
		}

		if (Buff.DotDamagePerSec <= 0.f || (Now - Buff.LastTickAt) < BuffTickIntervalSeconds)
		{
			continue;
		}

		// NPCSystem.ts:428 — advance by exactly one interval, not to Now.
		Buff.LastTickAt += BuffTickIntervalSeconds;

		const int32 DotDamage = FMath::RoundToInt32(static_cast<double>(Buff.DotDamagePerSec) * FMath::Max(1, Buff.Stacks));
		AActor* DotCaster = Buff.Caster.Get();
		const FName DotSkill = Buff.SkillId;

		Hp = FMath::Max(0.f, Hp - DotDamage);

		FValhallaCombatEvent Event;
		Event.Kind = EValhallaCombatEventKind::NpcHit;
		Event.Target = this;
		Event.Instigator = DotCaster;
		Event.SkillId = DotSkill;
		Event.Amount = static_cast<float>(DotDamage);
		Event.RemainingHp = Hp;
		Event.Location = GetActorLocation();
		UValhallaCombatLibrary::BroadcastCombatEvent(this, Event);

		UE_LOG(LogValhallaCombat, Log, TEXT("dot tick %d on %s from '%s'"), DotDamage, *DisplayName, *DotSkill.ToString());

		if (Hp <= 0.f)
		{
			// NPCSystem.ts:460 — a DoT kill credits the caster of the DoT, which
			// is why the caster is carried on the buff at all.
			Die(DotCaster, Now);
			return;
		}
	}

	if (bChanged)
	{
		SyncBuffsToReplicatedView();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Aggro — NPCSystem.ts:493
// ─────────────────────────────────────────────────────────────────────────────

void AValhallaNPC::UpdateAggro(double Now)
{
	// NPCSystem.ts:497 — canAggro defaults to true for enemies, false for
	// friendly NPCs. A shopkeeper does not chase you for walking past.
	const bool bCanAggro = Template.bCanAggro || Template.Type == EValhallaNPCType::Enemy;
	if (!bCanAggro)
	{
		AggroTarget = nullptr;
		return;
	}

	// ── Phase 1: prune (NPCSystem.ts:503) ────────────────────────────────
	// A player who died or disconnected stops being a threat immediately,
	// otherwise the NPC stands there aggroed on a corpse.
	for (auto It = ThreatTable.CreateIterator(); It; ++It)
	{
		AActor* Candidate = It.Key().Get();
		if (!Candidate || !UValhallaCombatLibrary::IsAliveTarget(Candidate))
		{
			It.RemoveCurrent();
		}
	}

	// ── Phase 2: highest threat wins (NPCSystem.ts:511) ──────────────────
	if (ThreatTable.Num() > 0)
	{
		AActor* Best = nullptr;
		float BestThreat = -1.f;
		for (const TPair<TWeakObjectPtr<AActor>, float>& Entry : ThreatTable)
		{
			if (Entry.Value > BestThreat)
			{
				BestThreat = Entry.Value;
				Best = Entry.Key.Get();
			}
		}
		AggroTarget = Best;
		return;
	}

	// ── Phase 3: proximity, only when nobody has hit it yet ──────────────
	const float AggroRange = Template.AggroRange > 0.f ? Template.AggroRange : DefaultAggroRange;
	const double AggroRangeSq = static_cast<double>(AggroRange) * AggroRange;
	const FVector MyLocation = GetActorLocation();

	AActor* Nearest = nullptr;
	double NearestDistSq = AggroRangeSq;

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AValhallaCharacter> It(World); It; ++It)
		{
			AValhallaCharacter* Player = *It;
			if (!Player || !Player->IsAlive())
			{
				continue;
			}

			FVector ToPlayer = Player->GetActorLocation() - MyLocation;
			ToPlayer.Z = 0.0;
			const double DistSq = ToPlayer.SizeSquared2D();
			if (DistSq < NearestDistSq)
			{
				NearestDistSq = DistSq;
				Nearest = Player;
			}
		}
	}

	if (Nearest)
	{
		// NPCSystem.ts:541 — seed 1 threat so a proximity-aggroed player is in
		// the table and can be legitimately out-threatened by someone who hits it.
		ThreatTable.Add(Nearest, 1.f);
		UE_LOG(LogValhallaCombat, Log, TEXT("%s aggro: %s entered aggro range (%.0f cm)."),
			*DisplayName, *UValhallaCombatLibrary::GetDisplayName(Nearest), FMath::Sqrt(NearestDistSq));
	}

	AggroTarget = Nearest;
}

void AValhallaNPC::ResetToHome()
{
	AggroTarget = nullptr;
	ThreatTable.Reset();

	SetActorLocation(HomeLocation, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);

	// NPCSystem.ts:172 — a leashed NPC heals to full. Without it, a player could
	// pull an enemy, run out of leash range, and repeat until it died of
	// attrition without ever being able to fight back.
	Hp = MaxHp;

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
	}

	UE_LOG(LogValhallaCombat, Log, TEXT("%s leashed: reset to %s and healed to %.0f."),
		*DisplayName, *HomeLocation.ToCompactString(), MaxHp);
}

// ─────────────────────────────────────────────────────────────────────────────
//  The state machine — NPCSystem.ts:111
// ─────────────────────────────────────────────────────────────────────────────

void AValhallaNPC::ServerFixedTick(float FixedDeltaSeconds, double Now)
{
	if (!HasAuthority())
	{
		return;
	}

	// ── Respawn (NPCSystem.ts:124) ───────────────────────────────────────
	if (!bAlive)
	{
		if (RespawnAt > 0.0 && Now >= RespawnAt)
		{
			Respawn();
		}
		return;
	}

	// ── Aggro (NPCSystem.ts:150) ─────────────────────────────────────────
	// Only aggressive and patrol NPCs look for targets at all. A stationary or
	// passive one still keeps the threat table it was given by being hit, which
	// is how a passive NPC that is attacked fights back without wandering off.
	if (Template.BehaviorType == EValhallaNPCBehavior::Aggressive || Template.BehaviorType == EValhallaNPCBehavior::Patrol)
	{
		UpdateAggro(Now);
	}

	AActor* Target = AggroTarget.Get();

	if (Target && Template.BehaviorType != EValhallaNPCBehavior::Stationary)
	{
		if (!UValhallaCombatLibrary::IsAliveTarget(Target))
		{
			AggroTarget = nullptr;
			TickBuffs(Now);
			return;
		}

		const FVector MyLocation = GetActorLocation();

		// ── Leash (NPCSystem.ts:164) ─────────────────────────────────────
		// Checked against *its own* distance from home, not the target's: an NPC
		// leashes because it has been led too far, not because you are far away.
		FVector FromHome = MyLocation - HomeLocation;
		FromHome.Z = 0.0;
		if (FromHome.SizeSquared2D() > static_cast<double>(LeashRange) * LeashRange)
		{
			ResetToHome();
			TickBuffs(Now);
			return;
		}

		FVector ToTarget = Target->GetActorLocation() - MyLocation;
		ToTarget.Z = 0.0;
		const double Distance = ToTarget.Size2D();

		// Distances in npc-templates.json are surface-to-surface, because 1.0's
		// entities were points on a plane that could stand on top of each other.
		// In 2.0 both parties are capsules, so two characters can never be closer
		// than the sum of their radii — 60 cm for the stock 30 cm capsules. A
		// template's attackRange of 40 would then be permanently unreachable and
		// the NPC would follow the player around forever without ever swinging.
		// Adding the radii back is what makes the authored numbers mean what they
		// meant in 1.0. (The player's own melee already does this: MELEE_RANGE +
		// PLAYER_COLLISION_RADIUS, SkillSystem.ts:474.)
		const double CapsuleGap = GetCapsuleComponent()->GetScaledCapsuleRadius() + [Target]() -> double
		{
			if (const ACharacter* TargetCharacter = Cast<ACharacter>(Target))
			{
				return TargetCharacter->GetCapsuleComponent()->GetScaledCapsuleRadius();
			}
			return 0.0;
		}();

		// ── Chase (NPCSystem.ts:177) ─────────────────────────────────────
		if (Distance > StopChaseDistance + CapsuleGap)
		{
			// 1.0 wrote the position directly. Going through AddMovementInput
			// means the NPC collides with the world and with the player instead
			// of walking through both, at the same MaxWalkSpeed the template asks
			// for, so the tuning carries over and the behaviour improves.
			AddMovementInput(ToTarget.GetSafeNormal(), 1.f);
		}

		// ── Attack (NPCSystem.ts:200) ────────────────────────────────────
		const double AttackRange = (Template.AttackRange > 0.f ? Template.AttackRange : 40.f) + CapsuleGap;
		const double AttackIntervalSeconds = (Template.AttackSpeedMs > 0.f ? Template.AttackSpeedMs : 1500.f) / 1000.0;

		if (Distance <= AttackRange && Now >= LastAttackTime + AttackIntervalSeconds)
		{
			LastAttackTime = Now;

			const double BaseDamage = FMath::Max(1.f, Template.Damage);

			UE_LOG(LogValhallaCombat, Log, TEXT("%s attacks %s (dist %.0f <= %.0f, every %.0f ms)"),
				*DisplayName, *UValhallaCombatLibrary::GetDisplayName(Target), Distance, AttackRange, Template.AttackSpeedMs);

			// The same pipeline a player's swing goes through, so an NPC's hit
			// can miss, be dodged, be blocked and be absorbed by a shield —
			// which in 1.0 it could not, because NPCSystem subtracted raw HP.
			UValhallaCombatLibrary::ApplyDamage(this, Target, BaseDamage, /*bMagical=*/false, TemplateId);

			if (!UValhallaCombatLibrary::IsAliveTarget(Target))
			{
				AggroTarget = nullptr;
			}
		}
	}
	else if (!Target)
	{
		// ── Walk home (NPCSystem.ts:239) ─────────────────────────────────
		FVector ToHome = HomeLocation - GetActorLocation();
		ToHome.Z = 0.0;
		const double DistanceHome = ToHome.Size2D();

		if (DistanceHome > 2.0)
		{
			AddMovementInput(ToHome.GetSafeNormal(), ReturnSpeedFraction);
		}
	}

	// ── Buffs (NPCSystem.ts:266) ─────────────────────────────────────────
	TickBuffs(Now);
}
