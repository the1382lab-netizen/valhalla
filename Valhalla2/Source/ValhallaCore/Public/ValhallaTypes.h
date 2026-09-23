// Copyright Valhalla 2.0. All Rights Reserved.
//
// Data model ported from Valhalla 1.0 TypeScript. Field names follow UE
// PascalCase conventions; the JSON keys they map to are the camelCase names in
// shared/data/*.json and are documented on each property.
//
// Source of truth for shapes:
//   shared/src/classes.ts      (StatBlock, StartingItem, ClassTemplate)
//   shared/src/stats.ts        (ResolvedStats)
//   shared/src/items.ts        (ItemTemplate + enums)
//   shared/src/skills.ts       (SkillTemplate + enums)
//   shared/src/npcs.ts         (NPCTemplate)
//   shared/src/loot-tables.ts  (LootEntry, LootTable)
//   shared/src/maps.ts         (ZoneConfig)
//
// Data-table fields are float (JSON source precision); the deterministic damage structs below use double, which UE5 reflects as FDoubleProperty; the
// deterministic math in ValhallaStats promotes to double internally.

#pragma once

#include "CoreMinimal.h"
#include "ValhallaTypes.generated.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Enums
// ─────────────────────────────────────────────────────────────────────────────

/** classes.ts:14 — `ArmorType`. The heaviest armor a class may wear. */
UENUM(BlueprintType)
enum class EValhallaArmorType : uint8
{
	Cloth		UMETA(DisplayName = "Cloth"),
	Leather		UMETA(DisplayName = "Leather"),
	Mail		UMETA(DisplayName = "Mail"),
	Plate		UMETA(DisplayName = "Plate"),
};

/** items.ts:120 — `ItemCategory`. */
UENUM(BlueprintType)
enum class EValhallaItemCategory : uint8
{
	Equipment	UMETA(DisplayName = "Equipment"),
	Consumable	UMETA(DisplayName = "Consumable"),
	Quest		UMETA(DisplayName = "Quest"),
	Misc		UMETA(DisplayName = "Misc"),
};

/** items.ts:127 — `ItemRarity`. */
UENUM(BlueprintType)
enum class EValhallaItemRarity : uint8
{
	Common		UMETA(DisplayName = "Common"),
	Uncommon	UMETA(DisplayName = "Uncommon"),
	Rare		UMETA(DisplayName = "Rare"),
	Epic		UMETA(DisplayName = "Epic"),
	Legendary	UMETA(DisplayName = "Legendary"),
};

/** items.ts:66 — `EquipSlotType`. `None` means the item is not equippable. */
UENUM(BlueprintType)
enum class EValhallaEquipSlot : uint8
{
	None		UMETA(DisplayName = "None"),
	Weapon		UMETA(DisplayName = "Weapon"),
	Offhand		UMETA(DisplayName = "Offhand"),
	Helm		UMETA(DisplayName = "Helm"),
	Chest		UMETA(DisplayName = "Chest"),
	Legs		UMETA(DisplayName = "Legs"),
	Boots		UMETA(DisplayName = "Boots"),
	Gloves		UMETA(DisplayName = "Gloves"),
	Back		UMETA(DisplayName = "Back"),
	Ring		UMETA(DisplayName = "Ring"),
};

/** skills.ts:71 — `ResourceType`. */
UENUM(BlueprintType)
enum class EValhallaResourceType : uint8
{
	None		UMETA(DisplayName = "None"),
	Mana		UMETA(DisplayName = "Mana"),
	Energy		UMETA(DisplayName = "Energy"),
};

/** skills.ts:77 — `SkillTargetType`. */
UENUM(BlueprintType)
enum class EValhallaSkillTargetType : uint8
{
	Self			UMETA(DisplayName = "Self"),
	SingleEnemy		UMETA(DisplayName = "Single Enemy"),
	SingleAlly		UMETA(DisplayName = "Single Ally"),
	AoeGround		UMETA(DisplayName = "AoE Ground"),
	AoeSelf			UMETA(DisplayName = "AoE Self"),
	Cone			UMETA(DisplayName = "Cone"),
	PassiveToggle	UMETA(DisplayName = "Passive Toggle"),
};

/** skills.ts:87 — `SkillCategory`. */
UENUM(BlueprintType)
enum class EValhallaSkillCategory : uint8
{
	Offensive	UMETA(DisplayName = "Offensive"),
	Defensive	UMETA(DisplayName = "Defensive"),
	Healing		UMETA(DisplayName = "Healing"),
	Buff		UMETA(DisplayName = "Buff"),
	Debuff		UMETA(DisplayName = "Debuff"),
	Utility		UMETA(DisplayName = "Utility"),
};

/** skills.ts:124 — `stackingMode`. `Replace` is the documented default. */
UENUM(BlueprintType)
enum class EValhallaStackingMode : uint8
{
	Replace		UMETA(DisplayName = "Replace"),
	Stack		UMETA(DisplayName = "Stack"),
	Extend		UMETA(DisplayName = "Extend"),
};

/** npcs.ts:13 — `NPCTemplate.type`. */
UENUM(BlueprintType)
enum class EValhallaNPCType : uint8
{
	Enemy		UMETA(DisplayName = "Enemy"),
	Npc			UMETA(DisplayName = "NPC"),
};

/** npcs.ts:19 — `NPCTemplate.behaviorType`. */
UENUM(BlueprintType)
enum class EValhallaNPCBehavior : uint8
{
	Passive		UMETA(DisplayName = "Passive"),
	Aggressive	UMETA(DisplayName = "Aggressive"),
	Patrol		UMETA(DisplayName = "Patrol"),
	Stationary	UMETA(DisplayName = "Stationary"),
	Fleeing		UMETA(DisplayName = "Fleeing"),
};

/** Outcome of one pass through the damage pipeline (CombatSystem.ts:343). */
UENUM(BlueprintType)
enum class EValhallaDamageOutcome : uint8
{
	/** The attack never connected — attacker's hit roll failed. */
	Miss		UMETA(DisplayName = "Miss"),
	/** The target dodged; no damage, nothing else is rolled. */
	Dodge		UMETA(DisplayName = "Dodge"),
	/** The attack landed. Check bBlocked / bCrit for the details. */
	Hit			UMETA(DisplayName = "Hit"),
};

// ─────────────────────────────────────────────────────────────────────────────
//  Stats
// ─────────────────────────────────────────────────────────────────────────────

/**
 * classes.ts:21 — `StatBlock`. Used both for a class's level-1 base stats and
 * for its per-level growth. Rates (CritChance, CritDamage, BlockRating,
 * DodgeRating) are decimals: 0.05 == 5%.
 */
USTRUCT(BlueprintType)
struct VALHALLACORE_API FValhallaStatBlock
{
	GENERATED_BODY()

	/** JSON `hp`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Stats")
	float Hp = 0.f;

	/** JSON `mana`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Stats")
	float Mana = 0.f;

	/** JSON `strength`. Scales physical damage. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Stats")
	float Strength = 0.f;

	/** JSON `stamina`. Scales the energy pool and its regen. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Stats")
	float Stamina = 0.f;

	/** JSON `dexterity`. Scales hit chance and attack speed. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Stats")
	float Dexterity = 0.f;

	/** JSON `intelligence`. Scales spell damage and mana regen. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Stats")
	float Intelligence = 0.f;

	/** JSON `wisdom`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Stats")
	float Wisdom = 0.f;

	/** JSON `physicalResist`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Stats")
	float PhysicalResist = 0.f;

	/** JSON `spellResist`. Doubles as magical defense in the damage pipeline. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Stats")
	float SpellResist = 0.f;

	/** JSON `critChance`. Decimal, 0.05 == 5%. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Stats")
	float CritChance = 0.f;

	/** JSON `critDamage`. Decimal bonus, 0.5 == +50% on a crit. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Stats")
	float CritDamage = 0.f;

	/** JSON `physicalDefense`. Physical defense in the damage pipeline. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Stats")
	float PhysicalDefense = 0.f;

	/** JSON `blockRating`. Decimal chance; a block halves damage. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Stats")
	float BlockRating = 0.f;

	/** JSON `dodgeRating`. Decimal chance; a dodge negates the hit. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Stats")
	float DodgeRating = 0.f;
};

/**
 * stats.ts:11 — `ResolvedStats extends StatBlock`. The full stat block the
 * server uses for all combat and movement math, produced by
 * UValhallaStatsLibrary::ComputeDerivedStats.
 */
USTRUCT(BlueprintType)
struct VALHALLACORE_API FValhallaResolvedStats : public FValhallaStatBlock
{
	GENERATED_BODY()

	/** stats.ts:58 — equals Hp after level scaling. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Stats")
	float MaxHp = 0.f;

	/** stats.ts:59 — equals Mana after level scaling. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Stats")
	float MaxMana = 0.f;

	/** stats.ts:50 — 0 for casters, otherwise 100 + Stamina * 2. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Stats")
	float MaxEnergy = 0.f;

	/** stats.ts:51 — 0 for casters, otherwise Stamina * 0.25 (per second). */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Stats")
	float EnergyRegenRate = 0.f;

	/** stats.ts:54 — 0 for non-casters, otherwise 1.25 + Intelligence * 0.1. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Stats")
	float ManaRegenRate = 0.f;

	/** stats.ts:63 — copied straight from the class template's BaseSpeed. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Stats")
	float Speed = 0.f;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Classes
// ─────────────────────────────────────────────────────────────────────────────

/** classes.ts:44 — `StartingItem`. One entry of a class's starting loadout. */
USTRUCT(BlueprintType)
struct VALHALLACORE_API FValhallaStartingItem
{
	GENERATED_BODY()

	/** JSON `itemId` — key into the item catalog. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Classes")
	FName ItemId;

	/** JSON `quantity`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Classes")
	int32 Quantity = 1;

	/** JSON `equipped` — true goes to equipment, false to the inventory. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Classes")
	bool bEquipped = false;
};

/** classes.ts:50 — `ClassTemplate`. One playable class's balance data. */
USTRUCT(BlueprintType)
struct VALHALLACORE_API FValhallaClassTemplate
{
	GENERATED_BODY()

	/** JSON `id` — e.g. `warrior`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Classes")
	FName Id;

	/** JSON `name`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Classes")
	FString Name;

	/** JSON `description`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Classes")
	FString Description;

	/** JSON `baseStats` — level 1 values. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Classes")
	FValhallaStatBlock BaseStats;

	/** JSON `statsPerLevel` — added once per level gained past 1. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Classes")
	FValhallaStatBlock StatsPerLevel;

	/** JSON `allowedArmor`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Classes")
	EValhallaArmorType AllowedArmor = EValhallaArmorType::Cloth;

	/** JSON `baseSpeed` — 1.0 pixels per second (Phase 3 maps this to cm/s). */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Classes")
	float BaseSpeed = 0.f;

	/** JSON `canUseMana` — false means the class runs on energy instead. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Classes")
	bool bCanUseMana = false;

	/** JSON `baseMeleeAttackSpeedMs` — unarmed swing interval, before dexterity. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Classes")
	float BaseMeleeAttackSpeedMs = 0.f;

	/** JSON `baseRangedAttackSpeedMs` — only meaningful for the Ranger. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Classes")
	float BaseRangedAttackSpeedMs = 0.f;

	/** JSON `startingItems` — optional; empty when the key is absent. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Classes")
	TArray<FValhallaStartingItem> StartingItems;

	/** JSON `bodyId` — optional default paperdoll body. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Classes")
	FString BodyId;

	/**
	 * JSON `visionRange` — new in 2.0, no 1.0 equivalent. How far this class can
	 * see through the Phase 5 line-of-sight system, in 1.0 pixels. Defaults to
	 * Valhalla::DefaultVisionRange (1200) when the key is absent.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Classes")
	float VisionRange = 1200.f;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Items
// ─────────────────────────────────────────────────────────────────────────────

/** items.ts:163 — `ItemTemplate`. One entry of the item catalog. */
USTRUCT(BlueprintType)
struct VALHALLACORE_API FValhallaItemTemplate
{
	GENERATED_BODY()

	/** JSON `id` — e.g. `iron_sword`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Items")
	FName Id;

	/** JSON `name`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Items")
	FString Name;

	/** JSON `description`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Items")
	FString Description;

	/** JSON `category`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Items")
	EValhallaItemCategory Category = EValhallaItemCategory::Misc;

	/** JSON `equipSlot` — optional; `None` when the item is not equippable. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Items")
	EValhallaEquipSlot EquipSlot = EValhallaEquipSlot::None;

	/** JSON `rarity`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Items")
	EValhallaItemRarity Rarity = EValhallaItemRarity::Common;

	/** JSON `stackable`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Items")
	bool bStackable = false;

	/** JSON `maxStack`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Items")
	int32 MaxStack = 1;

	/** JSON `statBonuses` — a Partial<StatBlock>; absent keys stay 0. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Items")
	FValhallaStatBlock StatBonuses;

	/** True when JSON `statBonuses` was present at all. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Items")
	bool bHasStatBonuses = false;

	/** JSON `attackSpeedMs` — overrides the class base attack speed when equipped. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Items")
	float AttackSpeedMs = 0.f;

	/** True when JSON `attackSpeedMs` was present. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Items")
	bool bHasAttackSpeed = false;

	/** JSON `attackDamage` — flat bonus added to the base damage constant. Superseded by MinDamage/MaxDamage; used when those are absent. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Items")
	float AttackDamage = 0.f;

	/**
	 * JSON `minDamage` / `maxDamage` — the weapon's damage roll (2.0, EverQuest
	 * style). Each auto-attack adds a uniform roll in [Min, Max] to the base
	 * constant before stat scaling. Absent (Max 0) means a fixed AttackDamage.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Items")
	float MinDamage = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Items")
	float MaxDamage = 0.f;

	/** The damage roll range: [MinDamage, MaxDamage], or AttackDamage for both when no range is set. */
	void GetDamageRange(float& OutMin, float& OutMax) const
	{
		if (MaxDamage > 0.f)
		{
			OutMin = FMath::Min(MinDamage, MaxDamage);
			OutMax = MaxDamage;
		}
		else
		{
			OutMin = OutMax = AttackDamage;
		}
	}

	/** JSON `isRangedWeapon` — enables the ranged auto-attack when equipped. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Items")
	bool bIsRangedWeapon = false;

	/** JSON `inventoryIcon` — icon file name; the HUD loads /Game/Valhalla/UI/Icons/Items/<base name>. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Items")
	FString InventoryIcon;

	/** JSON `spriteId` — the 1.0 paperdoll layer id; the art id when MeshId is empty. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Items")
	FString SpriteId;

	/** JSON `weaponStyle` — sword / greatsword / mace / bow / staff. Weapons only. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Items")
	FName WeaponStyle;

	/**
	 * JSON `meshId` — optional, and the one field in items.json that 1.0 never
	 * had a use for.
	 *
	 * 1.0 drew equipment as sprite layers keyed on `spriteId`, so six items that
	 * looked alike shared one sheet. 2.0 draws meshes, and the two groupings are
	 * not the same: `iron_sword` and `iron_dagger` were one sprite and are two
	 * models. Rather than fork `spriteId` — which would break the 1.0 client
	 * still reading the same file — an item may name its 2.0 art directly here,
	 * and everything that does not stays on the sprite grouping.
	 *
	 * Empty is the normal case. See GetArtId.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Items")
	FString MeshId;

	/**
	 * The id every art lookup goes through: `meshId` when the item names one,
	 * `spriteId` otherwise.
	 *
	 * Lives on the template rather than in ValhallaVisuals so the *rule* is in
	 * ValhallaCore with the data it reads — a world-free fact a unit test can
	 * pin (`Valhalla.Core.Data.MeshIdFallback`) — while the *path* built from it
	 * stays in ValhallaGame with the content. Returns an empty string when the
	 * item has neither, which is a ring: a stat bonus you cannot see.
	 */
	const FString& GetArtId() const { return MeshId.IsEmpty() ? SpriteId : MeshId; }
};

// ─────────────────────────────────────────────────────────────────────────────
//  Skills
// ─────────────────────────────────────────────────────────────────────────────

/** skills.ts:97 — `SkillTemplate`. One entry of the skill catalog. */
USTRUCT(BlueprintType)
struct VALHALLACORE_API FValhallaSkillTemplate
{
	GENERATED_BODY()

	/** JSON `id` — e.g. `wizard_fireball`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	FName Id;

	/** JSON `name`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	FString Name;

	/** JSON `description`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	FString Description;

	/** JSON `classId` — NAME_None when null, i.e. available to every class. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	FName ClassId;

	/** JSON `levelRequired`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	int32 LevelRequired = 1;

	/** JSON `resourceType`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	EValhallaResourceType ResourceType = EValhallaResourceType::None;

	/** JSON `resourceCost`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	float ResourceCost = 0.f;

	/** JSON `castTimeMs` — 0 means instant. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	float CastTimeMs = 0.f;

	/** JSON `cooldownMs`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	float CooldownMs = 0.f;

	/** JSON `range` — 1.0 pixels; 0 means self or melee. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	float Range = 0.f;

	/** JSON `targetType`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	EValhallaSkillTargetType TargetType = EValhallaSkillTargetType::Self;

	/** JSON `category`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	EValhallaSkillCategory Category = EValhallaSkillCategory::Utility;

	/** JSON `iconColor` — packed 0xRRGGBB, stored as an int for exactness. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	int32 IconColor = 0;

	/** JSON `iconAbbrev` — 2-3 characters for the procedural icon. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	FString IconAbbrev;

	/** JSON `scalingStat` — the StatBlock field that scales this skill. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	FName ScalingStat;

	/** JSON `baseDamage[0]` — the `[min, max]` array's first element. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	float BaseDamageMin = 0.f;

	/** JSON `baseDamage[1]`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	float BaseDamageMax = 0.f;

	/** True when JSON `baseDamage` was present. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	bool bHasBaseDamage = false;

	/** JSON `baseHealing[0]`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	float BaseHealingMin = 0.f;

	/** JSON `baseHealing[1]`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	float BaseHealingMax = 0.f;

	/** True when JSON `baseHealing` was present. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	bool bHasBaseHealing = false;

	/** JSON `buffDurationMs`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	float BuffDurationMs = 0.f;

	/** JSON `dotDamagePerSec`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	float DotDamagePerSec = 0.f;

	/** JSON `hotHealPerSec`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	float HotHealPerSec = 0.f;

	/** JSON `aoeRadius` — blast radius in 1.0 pixels for AoE ground spells. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	float AoeRadius = 0.f;

	/** True when the optional JSON `projectile` object was present. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	bool bHasProjectile = false;

	/** JSON `projectile.speed` — 1.0 pixels per second. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	float ProjectileSpeed = 0.f;

	/** JSON `projectile.radius` — 1.0 pixels. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	float ProjectileRadius = 0.f;

	/** JSON `cooldownGroup` — skills sharing a group share a cooldown. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	FName CooldownGroup;

	/** JSON `stackingMode` — defaults to Replace. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	EValhallaStackingMode StackingMode = EValhallaStackingMode::Replace;

	/** JSON `maxStacks` — only meaningful when StackingMode is Stack. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	int32 MaxStacks = 1;

	/** JSON `isAutoAttack` — repeats at the player's attack speed. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	bool bIsAutoAttack = false;

	/** JSON `requiresWeapon`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	bool bRequiresWeapon = false;

	/** JSON `effectNotes` — human-readable design summary. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	FString EffectNotes;
};

// ─────────────────────────────────────────────────────────────────────────────
//  NPCs
// ─────────────────────────────────────────────────────────────────────────────

/** npcs.ts:9 — `NPCTemplate`. Authored by the 1.0 game editor. */
USTRUCT(BlueprintType)
struct VALHALLACORE_API FValhallaNPCTemplate
{
	GENERATED_BODY()

	/** JSON `id` — editor-generated, e.g. `npc_1771431708366`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPCs")
	FName Id;

	/** JSON `name`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPCs")
	FString Name;

	/** JSON `description`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPCs")
	FString Description;

	/** JSON `type`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPCs")
	EValhallaNPCType Type = EValhallaNPCType::Enemy;

	/** JSON `level`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPCs")
	int32 Level = 1;

	/** JSON `stats` — a Partial<StatBlock>; absent keys stay 0. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPCs")
	FValhallaStatBlock Stats;

	/** JSON `hp`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPCs")
	float Hp = 0.f;

	/** JSON `mana` — optional, 0 when absent. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPCs")
	float Mana = 0.f;

	/** JSON `lootTableId` — optional; NAME_None when absent. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPCs")
	FName LootTableId;

	/** JSON `behaviorType`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPCs")
	EValhallaNPCBehavior BehaviorType = EValhallaNPCBehavior::Passive;

	/** JSON `canAggro` — defaults to true for enemies, false for friendly NPCs. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPCs")
	bool bCanAggro = false;

	/** JSON `aggroRange` — 1.0 pixels. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPCs")
	float AggroRange = 0.f;

	/** JSON `leashRange` — defaults to AggroRange * 3 when absent. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPCs")
	float LeashRange = 0.f;

	/** JSON `damage` — base attack damage; 1.0 default is 5. Used as a fixed hit when there is no min/max range. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPCs")
	float Damage = 5.f;

	/**
	 * JSON `minDamage` / `maxDamage` — the NPC's damage roll (2.0), the same
	 * uniform roll a player's weapon makes: each hit is a roll in [Min, Max].
	 * Absent (Max 0) means every hit is `damage`.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPCs")
	float MinDamage = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPCs")
	float MaxDamage = 0.f;

	/** The damage roll range: [MinDamage, MaxDamage], or Damage for both when no range is set. */
	void GetDamageRange(float& OutMin, float& OutMax) const
	{
		if (MaxDamage > 0.f)
		{
			OutMin = FMath::Min(MinDamage, MaxDamage);
			OutMax = MaxDamage;
		}
		else
		{
			OutMin = OutMax = Damage;
		}
	}

	/**
	 * JSON `weaponId` — an items.json weapon the NPC carries (2.0). It is drawn
	 * in the NPC's right hand, picks its attack animation, and each hit adds the
	 * weapon's own damage roll on top of the NPC's. NAME_None: unarmed.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPCs")
	FName WeaponId;

	/** JSON `attackSpeed` — ms between attacks; 1.0 default is 1500. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPCs")
	float AttackSpeedMs = 1500.f;

	/** JSON `attackRange` — 1.0 pixels; 1.0 default is 40. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPCs")
	float AttackRange = 40.f;

	/** JSON `moveSpeed` — 1.0 pixels per second while chasing; default 60. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPCs")
	float MoveSpeed = 60.f;

	/** JSON `respawnMs`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPCs")
	float RespawnMs = 0.f;

	/** JSON `skills` — skill ids this NPC may cast. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPCs")
	TArray<FName> Skills;

	/** JSON `spriteColor` — packed 0xRRGGBB. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPCs")
	int32 SpriteColor = 0;

	/** JSON `spriteSize` — scale multiplier. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPCs")
	float SpriteSize = 1.f;

	/** JSON `xpReward`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPCs")
	int32 XpReward = 0;

	/** JSON `dialogue` — optional lines for friendly NPCs. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPCs")
	TArray<FString> Dialogue;

	/** JSON `vendorInventory` — optional item ids this NPC sells. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|NPCs")
	TArray<FName> VendorInventory;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Loot
// ─────────────────────────────────────────────────────────────────────────────

/** loot-tables.ts:7 — `LootEntry`. One row of a drop table. */
USTRUCT(BlueprintType)
struct VALHALLACORE_API FValhallaLootEntry
{
	GENERATED_BODY()

	/** JSON `itemId`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Loot")
	FName ItemId;

	/** JSON `weight` — relative weight within the table. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Loot")
	float Weight = 0.f;

	/** JSON `dropChance` — decimal, 1 == always. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Loot")
	float DropChance = 0.f;

	/** JSON `minQuantity`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Loot")
	int32 MinQuantity = 1;

	/** JSON `maxQuantity`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Loot")
	int32 MaxQuantity = 1;
};

/** loot-tables.ts:15 — `LootTable`. */
USTRUCT(BlueprintType)
struct VALHALLACORE_API FValhallaLootTable
{
	GENERATED_BODY()

	/** JSON `id` — editor-generated, e.g. `loot_1771620382459`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Loot")
	FName Id;

	/** JSON `name`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Loot")
	FString Name;

	/** JSON `entries`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Loot")
	TArray<FValhallaLootEntry> Entries;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Zones
// ─────────────────────────────────────────────────────────────────────────────

/** maps.ts:28 — `ZoneConfig`. One playable zone and its Tiled map. */
USTRUCT(BlueprintType)
struct VALHALLACORE_API FValhallaZoneConfig
{
	GENERATED_BODY()

	/** JSON `id` — e.g. `grasslands`. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Zones")
	FName Id;

	/** JSON `name` — player-facing zone name. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Zones")
	FString Name;

	/** JSON `mapFile` — Tiled map filename under shared/maps/. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Zones")
	FString MapFile;

	/** JSON `defaultSpawn.x` — 1.0 pixel space. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Zones")
	float DefaultSpawnX = 0.f;

	/** JSON `defaultSpawn.y` — 1.0 pixel space. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Zones")
	float DefaultSpawnY = 0.f;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Damage pipeline (CombatSystem.ts:343 `applyStatDamage`)
// ─────────────────────────────────────────────────────────────────────────────

/** Everything ResolveDamage needs about the attacker, the target and the hit. */
USTRUCT(BlueprintType)
struct VALHALLACORE_API FValhallaDamageInput
{
	GENERATED_BODY()

	/** The pre-mitigation damage of the swing, spell or projectile. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	double RawDamage = 0.0;

	/** True routes mitigation through SpellResist, false through PhysicalDefense. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	bool bIsMagical = false;

	/** Attacker's dexterity — the only input to the hit roll. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	double AttackerDexterity = 0.0;

	/** Attacker's crit chance, decimal. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	double AttackerCritChance = 0.0;

	/** Attacker's crit damage bonus, decimal (0.5 == +50%). */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	double AttackerCritDamage = 0.0;

	/** Target's dodge rating, decimal. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	double DefenderDodgeRating = 0.0;

	/** Target's block rating, decimal. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	double DefenderBlockRating = 0.0;

	/** Target's physical defense — used when bIsMagical is false. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	double DefenderPhysicalDefense = 0.0;

	/** Target's spell resist — used when bIsMagical is true. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	double DefenderSpellResist = 0.0;

	/** Target's remaining absorb shield (Shield of Faith), in HP. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	double DefenderShieldHp = 0.0;
};

/**
 * The four random draws applyStatDamage would make, supplied by the caller so
 * ResolveDamage stays deterministic and testable. Each value is in [0, 1) and
 * is compared with `<` against its rating, exactly like Math.random() was.
 */
USTRUCT(BlueprintType)
struct VALHALLACORE_API FValhallaDamageRolls
{
	GENERATED_BODY()

	/** Compared against ComputeHitChance(AttackerDexterity). */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	double Hit = 0.0;

	/** Compared against DefenderDodgeRating. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	double Dodge = 0.0;

	/** Compared against AttackerCritChance. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	double Crit = 0.0;

	/** Compared against DefenderBlockRating. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	double Block = 0.0;
};

/** The result of one ResolveDamage call. Damage is already floored to an int. */
USTRUCT(BlueprintType)
struct VALHALLACORE_API FValhallaDamageResult
{
	GENERATED_BODY()

	/** Miss, Dodge or Hit. Damage is 0 unless this is Hit. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	EValhallaDamageOutcome Outcome = EValhallaDamageOutcome::Miss;

	/** Damage that should come off the target's HP, after shield absorption. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	int32 Damage = 0;

	/** True when the crit roll succeeded. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	bool bCrit = false;

	/** True when the block roll succeeded — damage was halved, not negated. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	bool bBlocked = false;

	/** How much of the hit the absorb shield swallowed. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	int32 ShieldAbsorbed = 0;

	/** The target's shield HP after absorption. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	double RemainingShieldHp = 0.0;

	/** Ms of invulnerability the target gains from this hit (0 on miss/dodge). */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	double InvulnerabilityMs = 0.0;
};

// ─────────────────────────────────────────────────────────────────────────────
//  String <-> enum parsing (the JSON stores the TypeScript string values)
// ─────────────────────────────────────────────────────────────────────────────

namespace Valhalla
{
	/** Parses `cloth` / `leather` / `mail` / `plate`. Returns false if unknown. */
	VALHALLACORE_API bool ParseArmorType(const FString& In, EValhallaArmorType& Out);

	/** Parses `equipment` / `consumable` / `quest` / `misc`. */
	VALHALLACORE_API bool ParseItemCategory(const FString& In, EValhallaItemCategory& Out);

	/** Parses `common` … `legendary`. */
	VALHALLACORE_API bool ParseItemRarity(const FString& In, EValhallaItemRarity& Out);

	/** Parses `weapon` / `offhand` / `helm` / … / `ring`. */
	VALHALLACORE_API bool ParseEquipSlot(const FString& In, EValhallaEquipSlot& Out);

	/** Parses `mana` / `energy` / `none`. */
	VALHALLACORE_API bool ParseResourceType(const FString& In, EValhallaResourceType& Out);

	/** Parses `self` / `singleEnemy` / `singleAlly` / `aoeGround` / `aoeSelf` / `cone` / `passiveToggle`. */
	VALHALLACORE_API bool ParseSkillTargetType(const FString& In, EValhallaSkillTargetType& Out);

	/** Parses `offensive` / `defensive` / `healing` / `buff` / `debuff` / `utility`. */
	VALHALLACORE_API bool ParseSkillCategory(const FString& In, EValhallaSkillCategory& Out);

	/** Parses `replace` / `stack` / `extend`. */
	VALHALLACORE_API bool ParseStackingMode(const FString& In, EValhallaStackingMode& Out);

	/** Parses `enemy` / `npc`. */
	VALHALLACORE_API bool ParseNPCType(const FString& In, EValhallaNPCType& Out);

	/** Parses `passive` / `aggressive` / `patrol` / `stationary` / `fleeing`. */
	VALHALLACORE_API bool ParseNPCBehavior(const FString& In, EValhallaNPCBehavior& Out);

	/** Parses `miss` / `dodge` / `hit` — used by the fixture-driven tests. */
	VALHALLACORE_API bool ParseDamageOutcome(const FString& In, EValhallaDamageOutcome& Out);

	/** Renders a damage outcome back to its lowercase TypeScript spelling. */
	VALHALLACORE_API FString DamageOutcomeToString(EValhallaDamageOutcome In);
}
