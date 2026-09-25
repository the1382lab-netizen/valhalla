// Copyright Valhalla 2.0. All Rights Reserved.
//
// String -> enum parsing. The JSON files store the raw TypeScript string union
// values, so every parser matches them case-insensitively and returns false
// (leaving Out untouched) when the value is not recognised. Callers log a
// warning and keep the struct's default rather than failing the whole load.

#include "ValhallaTypes.h"

namespace
{
	/** Case-insensitive compare against a TCHAR literal. */
	FORCEINLINE bool Is(const FString& In, const TCHAR* Literal)
	{
		return In.Equals(Literal, ESearchCase::IgnoreCase);
	}
}

namespace Valhalla
{
	bool ParseArmorType(const FString& In, EValhallaArmorType& Out)
	{
		if (Is(In, TEXT("cloth")))   { Out = EValhallaArmorType::Cloth;   return true; }
		if (Is(In, TEXT("leather"))) { Out = EValhallaArmorType::Leather; return true; }
		if (Is(In, TEXT("mail")))    { Out = EValhallaArmorType::Mail;    return true; }
		if (Is(In, TEXT("plate")))   { Out = EValhallaArmorType::Plate;   return true; }
		return false;
	}

	bool ParseItemCategory(const FString& In, EValhallaItemCategory& Out)
	{
		if (Is(In, TEXT("equipment")))  { Out = EValhallaItemCategory::Equipment;  return true; }
		if (Is(In, TEXT("consumable"))) { Out = EValhallaItemCategory::Consumable; return true; }
		if (Is(In, TEXT("quest")))      { Out = EValhallaItemCategory::Quest;      return true; }
		if (Is(In, TEXT("misc")))       { Out = EValhallaItemCategory::Misc;       return true; }
		return false;
	}

	bool ParseItemRarity(const FString& In, EValhallaItemRarity& Out)
	{
		if (Is(In, TEXT("common")))    { Out = EValhallaItemRarity::Common;    return true; }
		if (Is(In, TEXT("uncommon")))  { Out = EValhallaItemRarity::Uncommon;  return true; }
		if (Is(In, TEXT("rare")))      { Out = EValhallaItemRarity::Rare;      return true; }
		if (Is(In, TEXT("epic")))      { Out = EValhallaItemRarity::Epic;      return true; }
		if (Is(In, TEXT("legendary"))) { Out = EValhallaItemRarity::Legendary; return true; }
		return false;
	}

	bool ParseEquipSlot(const FString& In, EValhallaEquipSlot& Out)
	{
		if (Is(In, TEXT("weapon")))  { Out = EValhallaEquipSlot::Weapon;  return true; }
		if (Is(In, TEXT("offhand"))) { Out = EValhallaEquipSlot::Offhand; return true; }
		if (Is(In, TEXT("helm")))    { Out = EValhallaEquipSlot::Helm;    return true; }
		if (Is(In, TEXT("chest")))   { Out = EValhallaEquipSlot::Chest;   return true; }
		if (Is(In, TEXT("legs")))    { Out = EValhallaEquipSlot::Legs;    return true; }
		if (Is(In, TEXT("boots")))   { Out = EValhallaEquipSlot::Boots;   return true; }
		if (Is(In, TEXT("gloves")))  { Out = EValhallaEquipSlot::Gloves;  return true; }
		if (Is(In, TEXT("back")))    { Out = EValhallaEquipSlot::Back;    return true; }
		if (Is(In, TEXT("ring")))    { Out = EValhallaEquipSlot::Ring;    return true; }
		return false;
	}

	bool ParseResourceType(const FString& In, EValhallaResourceType& Out)
	{
		if (Is(In, TEXT("mana")))   { Out = EValhallaResourceType::Mana;   return true; }
		if (Is(In, TEXT("energy"))) { Out = EValhallaResourceType::Energy; return true; }
		if (Is(In, TEXT("none")))   { Out = EValhallaResourceType::None;   return true; }
		return false;
	}

	bool ParseSkillTargetType(const FString& In, EValhallaSkillTargetType& Out)
	{
		if (Is(In, TEXT("self")))          { Out = EValhallaSkillTargetType::Self;          return true; }
		if (Is(In, TEXT("singleEnemy")))   { Out = EValhallaSkillTargetType::SingleEnemy;   return true; }
		if (Is(In, TEXT("singleAlly")))    { Out = EValhallaSkillTargetType::SingleAlly;    return true; }
		if (Is(In, TEXT("aoeGround")))     { Out = EValhallaSkillTargetType::AoeGround;     return true; }
		if (Is(In, TEXT("aoeSelf")))       { Out = EValhallaSkillTargetType::AoeSelf;       return true; }
		if (Is(In, TEXT("cone")))          { Out = EValhallaSkillTargetType::Cone;          return true; }
		if (Is(In, TEXT("passiveToggle"))) { Out = EValhallaSkillTargetType::PassiveToggle; return true; }
		return false;
	}

	bool ParseSkillCategory(const FString& In, EValhallaSkillCategory& Out)
	{
		if (Is(In, TEXT("offensive"))) { Out = EValhallaSkillCategory::Offensive; return true; }
		if (Is(In, TEXT("defensive"))) { Out = EValhallaSkillCategory::Defensive; return true; }
		if (Is(In, TEXT("healing")))   { Out = EValhallaSkillCategory::Healing;   return true; }
		if (Is(In, TEXT("buff")))      { Out = EValhallaSkillCategory::Buff;      return true; }
		if (Is(In, TEXT("debuff")))    { Out = EValhallaSkillCategory::Debuff;    return true; }
		if (Is(In, TEXT("utility")))   { Out = EValhallaSkillCategory::Utility;   return true; }
		return false;
	}

	bool ParseStackingMode(const FString& In, EValhallaStackingMode& Out)
	{
		if (Is(In, TEXT("replace"))) { Out = EValhallaStackingMode::Replace; return true; }
		if (Is(In, TEXT("stack")))   { Out = EValhallaStackingMode::Stack;   return true; }
		if (Is(In, TEXT("extend")))  { Out = EValhallaStackingMode::Extend;  return true; }
		return false;
	}

	bool ParseNPCType(const FString& In, EValhallaNPCType& Out)
	{
		if (Is(In, TEXT("enemy"))) { Out = EValhallaNPCType::Enemy; return true; }
		if (Is(In, TEXT("npc")))   { Out = EValhallaNPCType::Npc;   return true; }
		return false;
	}

	bool ParseNPCBehavior(const FString& In, EValhallaNPCBehavior& Out)
	{
		if (Is(In, TEXT("passive")))    { Out = EValhallaNPCBehavior::Passive;    return true; }
		if (Is(In, TEXT("aggressive"))) { Out = EValhallaNPCBehavior::Aggressive; return true; }
		if (Is(In, TEXT("patrol")))     { Out = EValhallaNPCBehavior::Patrol;     return true; }
		if (Is(In, TEXT("stationary"))) { Out = EValhallaNPCBehavior::Stationary; return true; }
		if (Is(In, TEXT("fleeing")))    { Out = EValhallaNPCBehavior::Fleeing;    return true; }
		return false;
	}

	bool ParseNPCAttackType(const FString& In, EValhallaNPCAttackType& Out)
	{
		if (Is(In, TEXT("melee")))  { Out = EValhallaNPCAttackType::Melee;  return true; }
		if (Is(In, TEXT("ranged"))) { Out = EValhallaNPCAttackType::Ranged; return true; }
		return false;
	}

	bool ParseDamageOutcome(const FString& In, EValhallaDamageOutcome& Out)
	{
		if (Is(In, TEXT("miss")))   { Out = EValhallaDamageOutcome::Miss;  return true; }
		if (Is(In, TEXT("dodge")))  { Out = EValhallaDamageOutcome::Dodge; return true; }
		if (Is(In, TEXT("dodged"))) { Out = EValhallaDamageOutcome::Dodge; return true; }
		if (Is(In, TEXT("missed"))) { Out = EValhallaDamageOutcome::Miss;  return true; }
		if (Is(In, TEXT("hit")))    { Out = EValhallaDamageOutcome::Hit;   return true; }
		return false;
	}

	FString DamageOutcomeToString(EValhallaDamageOutcome In)
	{
		switch (In)
		{
		case EValhallaDamageOutcome::Miss:  return TEXT("miss");
		case EValhallaDamageOutcome::Dodge: return TEXT("dodge");
		case EValhallaDamageOutcome::Hit:   return TEXT("hit");
		default:                            return TEXT("unknown");
		}
	}
}
