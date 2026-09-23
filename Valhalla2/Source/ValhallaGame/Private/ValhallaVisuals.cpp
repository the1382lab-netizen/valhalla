// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaVisuals.h"

#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "ValhallaDataSubsystem.h"

DEFINE_LOG_CATEGORY(LogValhallaVisual);

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
	const TCHAR* Name = TEXT("A_Idle");
	switch (Anim)
	{
	case EValhallaAnim::Idle:   Name = TEXT("A_Idle");   break;
	case EValhallaAnim::Walk:   Name = TEXT("A_Walk");   break;
	case EValhallaAnim::Attack: Name = TEXT("A_Attack"); break;
	case EValhallaAnim::Shoot:  Name = TEXT("A_Shoot");  break;
	case EValhallaAnim::Cast:   Name = TEXT("A_Cast");   break;
	case EValhallaAnim::Hit:    Name = TEXT("A_Hit");    break;
	case EValhallaAnim::Death:  Name = TEXT("A_Death");  break;
	}

	return FString::Printf(TEXT("%s/Animations/%s"), CharactersRoot(), Name);
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
		: FString::Printf(TEXT("%s/Equipment/SK_%s"), CharactersRoot(), *ArtId);
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
