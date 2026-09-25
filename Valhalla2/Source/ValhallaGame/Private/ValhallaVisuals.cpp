// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaVisuals.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "ValhallaDataSubsystem.h"

DEFINE_LOG_CATEGORY(LogValhallaVisual);

static TAutoConsoleVariable<int32> CVarBodyProfile(
	TEXT("valhalla.Visual.BodyProfile"),
	1,
	TEXT("Which body characters use. 0: the Valhalla body (SK_Valhalla_Body) with its armour and hair. ")
	TEXT("1 (default): the Valhalla MetaHuman base (nude; wears paperdoll pieces built for its skeleton). ")
	TEXT("Read when a character initialises; change it before starting PIE."),
	ECVF_Default);

namespace
{
	/** The data subsystem, from anything with a world. Null outside a game. */
	const UValhallaDataSubsystem* FindData(const UObject* WorldContext)
	{
		const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContext);
		return GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
	}

	/**
	 * The five 1.0 bodies. Authored sRGB, converted once here.
	 *
	 * `body_elder` is the odd one: the hex is the *skin*, and the grey hair that
	 * goes with it is a separate decision — see BodyHasGreyHair.
	 */
	FColor SkinSrgbForBodyId(const FString& BodyId)
	{
		if (BodyId == TEXT("body_tan"))   { return FColor(0xC9, 0x92, 0x5F); }
		if (BodyId == TEXT("body_olive")) { return FColor(0xB0, 0x8A, 0x5A); }
		if (BodyId == TEXT("body_dark"))  { return FColor(0x7A, 0x4B, 0x2A); }
		if (BodyId == TEXT("body_elder")) { return FColor(0xD9, 0xC3, 0xB0); }
		// body_fair, and anything the data does not name.
		return FColor(0xE0, 0xB1, 0x93);
	}
}

FString UValhallaVisuals::AnimPath(EValhallaAnim Anim)
{
	const EValhallaBodyProfile Profile = ActiveBodyProfile();
	if (Profile != EValhallaBodyProfile::Legacy)
	{
		// The MetaHuman base (2026-09-23): locomotion, hit and death from
		// Epic's UE5 Manny set retargeted onto it (MH_MM_*/MH_MF_*); the
		// weapon attacks, casts, bow draw and stance poses hand-keyed on it
		// (MH_Attack_*, MH_Cast_*, MH_Bow_*, MHP_*, Tools/anim_authoring/author.py).
		const TCHAR* Clip = TEXT("MH_MM_Idle");
		switch (Anim)
		{
		case EValhallaAnim::Idle:          Clip = TEXT("MH_MM_Idle");                  break;
		case EValhallaAnim::Walk:          Clip = TEXT("MH_MF_Unarmed_Walk_Fwd");      break;
		case EValhallaAnim::Jog:           Clip = TEXT("MH_MF_Unarmed_Jog_Fwd");       break;
		case EValhallaAnim::Attack:        Clip = TEXT("MH_MM_Attack_01");             break;
		case EValhallaAnim::AttackSword:   Clip = TEXT("MH_Attack_Sword");             break;
		case EValhallaAnim::AttackDagger:  Clip = TEXT("MH_Attack_Dagger");            break;
		case EValhallaAnim::AttackMace:    Clip = TEXT("MH_Attack_Mace");              break;
		case EValhallaAnim::AttackStaff:   Clip = TEXT("MH_Attack_Staff");             break;
		case EValhallaAnim::Shoot:         Clip = TEXT("MH_Attack_Bow");               break;
		case EValhallaAnim::Cast:          Clip = TEXT("MH_Cast_Instant");             break;
		case EValhallaAnim::CastStaff:     Clip = TEXT("MH_Cast_Instant_Staff");       break;
		case EValhallaAnim::CastChannel:   Clip = TEXT("MH_Cast_Channel");             break;
		case EValhallaAnim::CastRelease:   Clip = TEXT("MH_Cast_Release");             break;
		case EValhallaAnim::CastChannelStaff: Clip = TEXT("MH_Cast_Channel_Staff");    break;
		case EValhallaAnim::CastReleaseStaff: Clip = TEXT("MH_Cast_Release_Staff");    break;
		case EValhallaAnim::BowDraw:       Clip = TEXT("MH_Bow_Draw");                 break;
		case EValhallaAnim::BowRelease:    Clip = TEXT("MH_Bow_Release");              break;
		case EValhallaAnim::Hit:           Clip = TEXT("MH_MM_HitReact_Front_Lgt_01"); break;
		case EValhallaAnim::Death:         Clip = TEXT("MH_MM_Death_Front_01");        break;
		case EValhallaAnim::PoseGripRight: Clip = TEXT("MHP_GripR");                   break;
		case EValhallaAnim::PoseGripLeft:  Clip = TEXT("MHP_GripL");                   break;
		case EValhallaAnim::PoseShieldArm: Clip = TEXT("MHP_ShieldArm");               break;
		case EValhallaAnim::Sit:           Clip = TEXT("MH_Sit");                      break;
		case EValhallaAnim::EmoteWave:     Clip = TEXT("MH_Emote_Wave");               break;
		case EValhallaAnim::EmoteCheer:    Clip = TEXT("MH_Emote_Cheer");              break;
		case EValhallaAnim::EmoteBow:      Clip = TEXT("MH_Emote_Bow");                break;
		case EValhallaAnim::Attack2H:      Clip = TEXT("MH_Attack_2H");                break;
		case EValhallaAnim::Block:         Clip = TEXT("MH_Block");                    break;
		case EValhallaAnim::Dodge:         Clip = TEXT("MH_Dodge");                    break;
		}
		return FString::Printf(TEXT("/Game/Valhalla/Characters/MetaHuman/Animations/%s"), Clip);
	}

	// Legacy body: its seven clips; the newer slots fall back to the nearest
	// one, and the stance poses do not exist for it. Its jog is A_Run.
	switch (Anim)
	{
	case EValhallaAnim::AttackSword:
	case EValhallaAnim::AttackDagger:
	case EValhallaAnim::AttackMace:
	case EValhallaAnim::AttackStaff:   Anim = EValhallaAnim::Attack; break;
	case EValhallaAnim::BowDraw:
	case EValhallaAnim::BowRelease:    Anim = EValhallaAnim::Shoot;  break;
	case EValhallaAnim::CastChannel:
	case EValhallaAnim::CastRelease:
	case EValhallaAnim::CastChannelStaff:
	case EValhallaAnim::CastReleaseStaff:
	case EValhallaAnim::CastStaff:     Anim = EValhallaAnim::Cast;   break;
	case EValhallaAnim::PoseGripRight:
	case EValhallaAnim::PoseGripLeft:
	case EValhallaAnim::PoseShieldArm: return FString();
	default: break;
	}

	const TCHAR* Name = TEXT("A_Idle");
	switch (Anim)
	{
	case EValhallaAnim::Idle:   Name = TEXT("A_Idle");   break;
	case EValhallaAnim::Walk:   Name = TEXT("A_Walk");   break;
	case EValhallaAnim::Jog:    Name = TEXT("A_Run");    break;
	case EValhallaAnim::Attack: Name = TEXT("A_Attack"); break;
	case EValhallaAnim::Shoot:  Name = TEXT("A_Shoot");  break;
	case EValhallaAnim::Cast:   Name = TEXT("A_Cast");   break;
	case EValhallaAnim::Hit:    Name = TEXT("A_Hit");    break;
	case EValhallaAnim::Death:  Name = TEXT("A_Death");  break;
	case EValhallaAnim::Sit:        Name = TEXT("A_Sit");         break;
	case EValhallaAnim::EmoteWave:  Name = TEXT("A_Emote_Wave");  break;
	case EValhallaAnim::EmoteCheer: Name = TEXT("A_Emote_Cheer"); break;
	case EValhallaAnim::EmoteBow:   Name = TEXT("A_Emote_Bow");   break;
	case EValhallaAnim::Attack2H:   Name = TEXT("A_Attack2H");    break;
	case EValhallaAnim::Block:      Name = TEXT("A_Block");       break;
	case EValhallaAnim::Dodge:      Name = TEXT("A_Dodge");       break;
	default: break;
	}

	return FString::Printf(TEXT("%s/Animations/%s"), CharactersRoot(), Name);
}

float UValhallaVisuals::LocomotionClipSpeed(EValhallaAnim Cycle)
{
	const bool bJog = Cycle == EValhallaAnim::Jog;
	if (UseExternalBody())
	{
		return bJog ? JogClipSpeed : WalkClipSpeed;
	}
	return bJog ? LegacyJogClipSpeed : LegacyWalkClipSpeed;
}

EValhallaBodyProfile UValhallaVisuals::ActiveBodyProfile()
{
	const int32 Value = CVarBodyProfile.GetValueOnAnyThread();
	return Value == 1 ? EValhallaBodyProfile::MetaHuman : EValhallaBodyProfile::Legacy;
}

FString UValhallaVisuals::ActiveBodyMeshPath()
{
	switch (ActiveBodyProfile())
	{
	case EValhallaBodyProfile::MetaHuman: return MetaHumanBodyMeshPath();
	default:                              return LegacyBodyMeshPath();
	}
}

float UValhallaVisuals::ActiveBodyScale()
{
	switch (ActiveBodyProfile())
	{
	case EValhallaBodyProfile::MetaHuman: return 122.f / 180.3f;
	default:                              return 1.f;
	}
}

bool UValhallaVisuals::ApplyActiveBody(USkeletalMeshComponent* Body, float ExtraScale)
{
	if (!Body || !UseExternalBody())
	{
		return false;
	}

	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *ActiveBodyMeshPath());
	if (!Mesh)
	{
		UE_LOG(LogValhallaVisual, Warning, TEXT("Body missing at %s; keeping the current body."), *ActiveBodyMeshPath());
		return false;
	}

	if (Body->GetSkeletalMeshAsset() != Mesh)
	{
		Body->SetSkeletalMeshAsset(Mesh);
	}
	// Feet at the origin like the Valhalla body, facing +Y, so MeshZOffset and
	// MeshYaw carry over unchanged; only the size differs.
	Body->SetRelativeScale3D(FVector(ActiveBodyScale() * ExtraScale));
	return true;
}

bool UValhallaVisuals::ApplyActiveHead(USkeletalMeshComponent* Head, USkeletalMeshComponent* Body)
{
	if (!Head)
	{
		return false;
	}

	const FString Path = ActiveHeadMeshPath();
	USkeletalMesh* Mesh = Path.IsEmpty() ? nullptr : LoadObject<USkeletalMesh>(nullptr, *Path);
	if (!Path.IsEmpty() && !Mesh)
	{
		UE_LOG(LogValhallaVisual, Warning, TEXT("Head missing at %s."), *Path);
	}

	if (Head->GetSkeletalMeshAsset() != Mesh)
	{
		Head->SetSkeletalMeshAsset(Mesh);
	}
	// Re-linked after any mesh swap: SetSkeletalMeshAsset drops the link.
	Head->SetLeaderPoseComponent(Mesh ? Body : nullptr);
	return Mesh != nullptr;
}

USkeletalMesh* UValhallaVisuals::PieceForActiveBody(USkeletalMesh* Piece)
{
	if (!Piece)
	{
		return nullptr;
	}

	static const FString LegacyRoot = TEXT("/Game/Valhalla/Characters/");
	static const FString MetaHumanRoot = TEXT("/Game/Valhalla/Characters/MetaHuman/");

	const FString Path = Piece->GetPathName();
	const bool bIsMetaHumanPiece = Path.StartsWith(MetaHumanRoot);
	const bool bWantMetaHuman = ActiveBodyProfile() == EValhallaBodyProfile::MetaHuman;
	if (bIsMetaHumanPiece == bWantMetaHuman)
	{
		return Piece;
	}

	FString Relative;
	if (bIsMetaHumanPiece)
	{
		Relative = Path.RightChop(MetaHumanRoot.Len());
	}
	else if (Path.StartsWith(LegacyRoot))
	{
		Relative = Path.RightChop(LegacyRoot.Len());
	}
	if (!(Relative.StartsWith(TEXT("Equipment/")) || Relative.StartsWith(TEXT("Hair/"))))
	{
		return Piece;
	}

	const FString Swapped = (bWantMetaHuman ? MetaHumanRoot : LegacyRoot) + Relative;
	USkeletalMesh* Rebuilt = LoadObject<USkeletalMesh>(nullptr, *Swapped, nullptr, LOAD_NoWarn | LOAD_Quiet);
	return Rebuilt ? Rebuilt : Piece;
}

bool UValhallaVisuals::CanFollowBody(const USkeletalMesh* Piece, const USkeletalMeshComponent* Body)
{
	const USkeletalMesh* BodyMeshAsset = Body ? Body->GetSkeletalMeshAsset() : nullptr;
	return Piece && BodyMeshAsset && Piece->GetSkeleton() == BodyMeshAsset->GetSkeleton();
}

namespace
{
	struct FGripFrame
	{
		const TCHAR* Bone;
		FVector Location;
		FRotator Rotation;   // pitch, yaw, roll
		float PropScale;     // prop size relative to its modelled size
	};

	/**
	 * Calibrated in PIE on each body's idle pose (Saved/ClaudeOps/gid_calib.py):
	 * each prop is placed in world space the way a person carries it, then
	 * expressed relative to the bone it rides on so the animation carries it.
	 * Props are modelled for the 122 cm character at 1.0; the shield is drawn
	 * at 0.75 (the kite was sized for the old chunky body).
	 */
	FGripFrame GripFrame(EValhallaBodyProfile /*Profile*/, EValhallaGrip Grip)
	{
		// MetaHuman base, calibrated on the Manny idle with the stance poses
		// (Tools/anim_authoring/author.py, which also keys every attack against
		// these frames — change one and re-run it). 2026-09-23.
		switch (Grip)
		{
		case EValhallaGrip::Staff:  return { TEXT("hand_r"), FVector(-6.94f, 2.42f, -0.91f), FRotator(61.93f, 118.25f, 31.58f), 1.00f };
		case EValhallaGrip::Bow:    return { TEXT("hand_l"), FVector(6.95f, -2.49f, -1.27f), FRotator(-19.81f, -96.16f, 169.32f), 1.00f };
		case EValhallaGrip::Shield: return { TEXT("lowerarm_l"), FVector(11.75f, -3.37f, 2.57f), FRotator(22.96f, -174.59f, 24.76f), 0.75f };
		default:                    return { TEXT("hand_r"), FVector(-6.94f, 2.42f, -0.91f), FRotator(42.57f, 17.09f, 106.28f), 1.00f };
		}
	}
}

bool UValhallaVisuals::AttachHeldProp(UStaticMeshComponent* Prop, USkeletalMeshComponent* Body, EValhallaGrip Grip)
{
	if (!Prop || !Body || !UseExternalBody())
	{
		return false;
	}

	const FGripFrame Frame = GripFrame(ActiveBodyProfile(), Grip);
	Prop->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Frame.Bone);
	Prop->SetRelativeTransform(FTransform(Frame.Rotation, Frame.Location, FVector(Frame.PropScale / ActiveBodyScale())));
	return true;
}

EValhallaGrip UValhallaVisuals::GripForWeapon(const UObject* WorldContext, FName WeaponItemId)
{
	const UValhallaDataSubsystem* Data = FindData(WorldContext);
	const FValhallaItemTemplate* Item = (Data && !WeaponItemId.IsNone()) ? Data->FindItem(WeaponItemId) : nullptr;
	if (Item && Item->WeaponStyle == TEXT("bow"))
	{
		return EValhallaGrip::Bow;
	}
	if (Item && Item->WeaponStyle == TEXT("staff"))
	{
		return EValhallaGrip::Staff;
	}
	return EValhallaGrip::OneHand;
}

FString UValhallaVisuals::EquipmentAssetPath(const FValhallaItemTemplate& Item, bool& OutIsSkeletal)
{
	OutIsSkeletal = true;

	// `meshId` when the item names one, `spriteId` otherwise — the whole of the
	// rule is FValhallaItemTemplate::GetArtId, and this is its only consumer.
	// Everything below is unchanged from the spriteId-only version: the id is
	// still a bare name that the prefix and folder are built around, so an item
	// that adds a `meshId` picks up `SM_<meshId>` / `SK_<meshId>` and nothing
	// else about its art resolution moves.
	const FString& ArtId = Item.GetArtId();

	if (ArtId.IsEmpty())
	{
		// Rings. 1.0 had no paperdoll layer for them either — a ring is a stat
		// bonus you cannot see, and that is a data fact, not a missing asset.
		return FString();
	}

	// Weapon and offhand are the two slots held rather than worn, and the only
	// two exported as rigid static meshes. Everything else is skinned to the
	// shared armature and rides the body's pose.
	const bool bHeld = Item.EquipSlot == EValhallaEquipSlot::Weapon
		|| Item.EquipSlot == EValhallaEquipSlot::Offhand;

	OutIsSkeletal = !bHeld;

	return bHeld
		? FString::Printf(TEXT("%s/Weapons/SM_%s"), CharactersRoot(), *ArtId)
		: FString::Printf(TEXT("%s/Equipment/SK_%s"), *SkinnedArtRoot(), *ArtId);
}

FString UValhallaVisuals::EquipmentAssetPathForItem(const UObject* WorldContext, FName ItemId, bool& OutIsSkeletal)
{
	OutIsSkeletal = true;

	if (ItemId.IsNone())
	{
		return FString();
	}

	const UValhallaDataSubsystem* Data = FindData(WorldContext);
	const FValhallaItemTemplate* Item = Data ? Data->FindItem(ItemId) : nullptr;
	if (!Item)
	{
		UE_LOG(LogValhallaVisual, Warning, TEXT("unknown item '%s'"), *ItemId.ToString());
		return FString();
	}

	return EquipmentAssetPath(*Item, OutIsSkeletal);
}

FLinearColor UValhallaVisuals::SkinTintForBodyId(const FString& BodyId)
{
	return FLinearColor::FromSRGBColor(SkinSrgbForBodyId(BodyId));
}

bool UValhallaVisuals::BodyHasGreyHair(const FString& BodyId)
{
	return BodyId == TEXT("body_elder");
}

EValhallaAttackCycle UValhallaVisuals::AttackCycleForWeaponStyle(FName WeaponStyle)
{
	// paperdoll.ts:48 WEAPON_STYLE_ANIM, entry for entry.
	if (WeaponStyle == TEXT("bow"))   { return EValhallaAttackCycle::Shoot; }
	// A staff is swung like any melee weapon now that casters melee (2.0,
	// 2026-09-22); 1.0 mapped it to the cast cycle because its casters' basic
	// attack was a spell.
	// sword, greatsword, mace, and `attackAnimFor`'s fallback for no weapon.
	return EValhallaAttackCycle::Melee;
}

EValhallaAttackCycle UValhallaVisuals::AttackCycleForEquippedWeapon(const UObject* WorldContext, FName WeaponItemId)
{
	if (WeaponItemId.IsNone())
	{
		return EValhallaAttackCycle::Melee;
	}

	const UValhallaDataSubsystem* Data = FindData(WorldContext);
	const FValhallaItemTemplate* Item = Data ? Data->FindItem(WeaponItemId) : nullptr;
	return Item ? AttackCycleForWeaponStyle(Item->WeaponStyle) : EValhallaAttackCycle::Melee;
}

EValhallaAnim UValhallaVisuals::AttackAnimForWeapon(const UObject* WorldContext, FName WeaponItemId)
{
	if (WeaponItemId.IsNone())
	{
		return EValhallaAnim::Attack;
	}
	const UValhallaDataSubsystem* Data = FindData(WorldContext);
	const FValhallaItemTemplate* Item = Data ? Data->FindItem(WeaponItemId) : nullptr;
	if (!Item)
	{
		return EValhallaAnim::AttackSword;
	}
	const FName Style = Item->WeaponStyle;
	if (Style == TEXT("bow"))    { return EValhallaAnim::Shoot; }
	if (Style == TEXT("staff"))  { return EValhallaAnim::AttackStaff; }
	if (Style == TEXT("mace"))   { return EValhallaAnim::AttackMace; }
	if (Style == TEXT("dagger")) { return EValhallaAnim::AttackDagger; }
	if (Style == TEXT("greatsword")) { return EValhallaAnim::Attack2H; }
	return EValhallaAnim::AttackSword;   // sword, and anything new
}

EValhallaAnim UValhallaVisuals::AnimForAttackCycle(EValhallaAttackCycle Cycle)
{
	switch (Cycle)
	{
	case EValhallaAttackCycle::Shoot: return EValhallaAnim::Shoot;
	case EValhallaAttackCycle::Cast:  return EValhallaAnim::Cast;
	default:                          return EValhallaAnim::Attack;
	}
}

EValhallaAnim UValhallaVisuals::AnimForSkill(const FValhallaSkillTemplate& Skill, EValhallaAttackCycle WeaponCycle)
{
	if (Skill.bIsAutoAttack)
	{
		// The auto-attack is the weapon swinging; the weapon decides how.
		return AnimForAttackCycle(WeaponCycle);
	}

	switch (Skill.Category)
	{
	case EValhallaSkillCategory::Offensive:
	case EValhallaSkillCategory::Debuff:
	case EValhallaSkillCategory::Healing:
	case EValhallaSkillCategory::Buff:
		return EValhallaAnim::Cast;

	default:
		// Defensive and utility. 1.0 had no separate cycle for either, and a
		// raised-hands gesture reads correctly for both.
		return EValhallaAnim::Cast;
	}
}
