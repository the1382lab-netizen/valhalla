// Copyright Valhalla 2.0. All Rights Reserved.
//
// Hand-written JSON -> USTRUCT conversion for the seven Valhalla 1.0 data
// files. Every file has the shape
//
//     { "version": "1.0.0", "<collection>": { "<id>": { … } } }
//
// with classes.json carrying an extra `classColors` map and skills.json an
// extra `classSkills` map.
//
// Rules this file follows without exception:
//   * a missing or mistyped field never throws and never aborts the load;
//   * a missing REQUIRED field logs one warning naming the file, the record
//     and the field, and leaves the struct default in place;
//   * an unknown extra field is ignored silently — 1.0's editor is free to add
//     fields ahead of the port.

#include "ValhallaDataSubsystem.h"

#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Json.h"
#include "JsonUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "ValhallaConstants.h"
#include "ValhallaCore.h"
#include "ValhallaDataSettings.h"

namespace
{
	// ── Field readers ────────────────────────────────────────────────────
	//
	// The Opt* helpers return false when the field is absent; the Require*
	// helpers additionally warn. `Context` is "<file>/<record>" so a warning
	// pins down the exact record a designer needs to fix.

	bool OptString(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, FString& Out)
	{
		return Obj.IsValid() && Obj->TryGetStringField(Key, Out);
	}

	bool OptName(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, FName& Out)
	{
		FString Temp;
		if (OptString(Obj, Key, Temp) && !Temp.IsEmpty())
		{
			Out = FName(*Temp);
			return true;
		}
		return false;
	}

	bool OptNumber(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, double& Out)
	{
		return Obj.IsValid() && Obj->TryGetNumberField(Key, Out);
	}

	bool OptFloat(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, float& Out)
	{
		double Temp = 0.0;
		if (OptNumber(Obj, Key, Temp))
		{
			Out = static_cast<float>(Temp);
			return true;
		}
		return false;
	}

	bool OptInt(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, int32& Out)
	{
		double Temp = 0.0;
		if (OptNumber(Obj, Key, Temp))
		{
			// The 1.0 editor writes packed colors well inside int32 range, but
			// round rather than truncate so 16777214.9999 does not become …13.
			Out = static_cast<int32>(FMath::RoundToDouble(Temp));
			return true;
		}
		return false;
	}

	bool OptBool(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, bool& Out)
	{
		return Obj.IsValid() && Obj->TryGetBoolField(Key, Out);
	}

	void WarnMissing(const FString& Context, const TCHAR* Key)
	{
		UE_LOG(LogValhallaCore, Warning, TEXT("[ValhallaData] %s: missing required field '%s' — using default."),
			*Context, Key);
	}

	bool RequireString(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, FString& Out, const FString& Context)
	{
		if (OptString(Obj, Key, Out)) { return true; }
		WarnMissing(Context, Key);
		return false;
	}

	bool RequireName(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, FName& Out, const FString& Context)
	{
		if (OptName(Obj, Key, Out)) { return true; }
		WarnMissing(Context, Key);
		return false;
	}

	bool RequireFloat(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, float& Out, const FString& Context)
	{
		if (OptFloat(Obj, Key, Out)) { return true; }
		WarnMissing(Context, Key);
		return false;
	}

	bool RequireInt(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, int32& Out, const FString& Context)
	{
		if (OptInt(Obj, Key, Out)) { return true; }
		WarnMissing(Context, Key);
		return false;
	}

	bool RequireBool(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, bool& Out, const FString& Context)
	{
		if (OptBool(Obj, Key, Out)) { return true; }
		WarnMissing(Context, Key);
		return false;
	}

	/** Reads a nested object field. Returns an invalid pointer when absent. */
	TSharedPtr<FJsonObject> OptObject(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* Found = nullptr;
		if (Obj.IsValid() && Obj->TryGetObjectField(Key, Found) && Found != nullptr)
		{
			return *Found;
		}
		return nullptr;
	}

	/** Reads an array field. Returns nullptr when absent. */
	const TArray<TSharedPtr<FJsonValue>>* OptArray(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key)
	{
		const TArray<TSharedPtr<FJsonValue>>* Found = nullptr;
		if (Obj.IsValid() && Obj->TryGetArrayField(Key, Found))
		{
			return Found;
		}
		return nullptr;
	}

	/** Reads an array of strings into FNames, skipping non-string entries. */
	void OptNameArray(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, TArray<FName>& Out)
	{
		if (const TArray<TSharedPtr<FJsonValue>>* Arr = OptArray(Obj, Key))
		{
			Out.Reserve(Arr->Num());
			for (const TSharedPtr<FJsonValue>& Value : *Arr)
			{
				if (Value.IsValid() && Value->Type == EJson::String)
				{
					const FString AsString = Value->AsString();
					if (!AsString.IsEmpty())
					{
						Out.Add(FName(*AsString));
					}
				}
			}
		}
	}

	/** Reads an array of strings, skipping non-string entries. */
	void OptStringArray(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, TArray<FString>& Out)
	{
		if (const TArray<TSharedPtr<FJsonValue>>* Arr = OptArray(Obj, Key))
		{
			Out.Reserve(Arr->Num());
			for (const TSharedPtr<FJsonValue>& Value : *Arr)
			{
				if (Value.IsValid() && Value->Type == EJson::String)
				{
					Out.Add(Value->AsString());
				}
			}
		}
	}

	/**
	 * Reads a `[min, max]` tuple. Returns false when the field is absent or is
	 * not a two-element numeric array, in which case Min/Max are untouched.
	 */
	bool OptNumberPair(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, float& OutMin, float& OutMax)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = OptArray(Obj, Key);
		if (Arr == nullptr || Arr->Num() < 2)
		{
			return false;
		}
		const TSharedPtr<FJsonValue>& MinValue = (*Arr)[0];
		const TSharedPtr<FJsonValue>& MaxValue = (*Arr)[1];
		if (!MinValue.IsValid() || !MaxValue.IsValid())
		{
			return false;
		}
		OutMin = static_cast<float>(MinValue->AsNumber());
		OutMax = static_cast<float>(MaxValue->AsNumber());
		return true;
	}

	/** Reads an enum field through a Valhalla::Parse* function, warning on a bad value. */
	template <typename EnumType, typename ParseFn>
	void OptEnum(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, EnumType& Out, ParseFn Parse, const FString& Context)
	{
		FString Raw;
		if (!OptString(Obj, Key, Raw))
		{
			return;
		}
		if (!Parse(Raw, Out))
		{
			UE_LOG(LogValhallaCore, Warning, TEXT("[ValhallaData] %s: unrecognised value '%s' for '%s' — using default."),
				*Context, *Raw, Key);
		}
	}

	/** Same, but also warns when the field is absent entirely. */
	template <typename EnumType, typename ParseFn>
	void RequireEnum(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, EnumType& Out, ParseFn Parse, const FString& Context)
	{
		FString Raw;
		if (!OptString(Obj, Key, Raw))
		{
			WarnMissing(Context, Key);
			return;
		}
		if (!Parse(Raw, Out))
		{
			UE_LOG(LogValhallaCore, Warning, TEXT("[ValhallaData] %s: unrecognised value '%s' for '%s' — using default."),
				*Context, *Raw, Key);
		}
	}

	/**
	 * Reads a StatBlock. `bPartial` covers Partial<StatBlock> uses (item stat
	 * bonuses, NPC stat overrides) where absent fields legitimately mean zero.
	 */
	void ParseStatBlock(const TSharedPtr<FJsonObject>& Obj, FValhallaStatBlock& Out, bool bPartial, const FString& Context)
	{
		if (!Obj.IsValid())
		{
			if (!bPartial)
			{
				UE_LOG(LogValhallaCore, Warning, TEXT("[ValhallaData] %s: stat block missing — using zeros."), *Context);
			}
			return;
		}

		auto Field = [&](const TCHAR* Key, float& Target)
		{
			if (!OptFloat(Obj, Key, Target) && !bPartial)
			{
				WarnMissing(Context, Key);
			}
		};

		Field(TEXT("hp"),              Out.Hp);
		Field(TEXT("mana"),            Out.Mana);
		Field(TEXT("strength"),        Out.Strength);
		Field(TEXT("stamina"),         Out.Stamina);
		Field(TEXT("dexterity"),       Out.Dexterity);
		Field(TEXT("intelligence"),    Out.Intelligence);
		Field(TEXT("wisdom"),          Out.Wisdom);
		Field(TEXT("physicalResist"),  Out.PhysicalResist);
		Field(TEXT("spellResist"),     Out.SpellResist);
		Field(TEXT("critChance"),      Out.CritChance);
		Field(TEXT("critDamage"),      Out.CritDamage);
		Field(TEXT("physicalDefense"), Out.PhysicalDefense);
		Field(TEXT("blockRating"),     Out.BlockRating);
		Field(TEXT("dodgeRating"),     Out.DodgeRating);
	}

	// ── File loading ─────────────────────────────────────────────────────

	/**
	 * Read <Root>/<FileName> and hand back the parsed root object.
	 * Logs an error and returns an invalid pointer on any failure.
	 */
	TSharedPtr<FJsonObject> LoadJsonFile(const FString& Root, const TCHAR* FileName)
	{
		const FString FullPath = FPaths::Combine(Root, FileName);

		FString RawText;
		if (!FFileHelper::LoadFileToString(RawText, *FullPath))
		{
			UE_LOG(LogValhallaCore, Error, TEXT("[ValhallaData] Could not read '%s'."), *FullPath);
			return nullptr;
		}

		TSharedPtr<FJsonObject> Root_ = nullptr;
		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(RawText);
		if (!FJsonSerializer::Deserialize(Reader, Root_) || !Root_.IsValid())
		{
			UE_LOG(LogValhallaCore, Error, TEXT("[ValhallaData] '%s' is not valid JSON: %s"),
				*FullPath, *Reader->GetErrorMessage());
			return nullptr;
		}

		return Root_;
	}

	/**
	 * Pull the `{ id: {...} }` collection out of a loaded file.
	 * Returns an invalid pointer (and logs) when the collection key is absent.
	 */
	TSharedPtr<FJsonObject> GetCollection(const TSharedPtr<FJsonObject>& FileRoot, const TCHAR* CollectionKey, const TCHAR* FileName)
	{
		TSharedPtr<FJsonObject> Collection = OptObject(FileRoot, CollectionKey);
		if (!Collection.IsValid())
		{
			UE_LOG(LogValhallaCore, Error, TEXT("[ValhallaData] '%s' has no '%s' object."), FileName, CollectionKey);
		}
		return Collection;
	}

	// ── Per-record converters ────────────────────────────────────────────

	void ParseClass(FName Id, const TSharedPtr<FJsonObject>& Obj, FValhallaClassTemplate& Out)
	{
		const FString Context = FString::Printf(TEXT("classes.json/%s"), *Id.ToString());

		Out.Id = Id;
		OptName(Obj, TEXT("id"), Out.Id);	// the map key wins if `id` disagrees
		RequireString(Obj, TEXT("name"), Out.Name, Context);
		OptString(Obj, TEXT("description"), Out.Description);

		ParseStatBlock(OptObject(Obj, TEXT("baseStats")), Out.BaseStats, /*bPartial*/ false, Context + TEXT("/baseStats"));
		ParseStatBlock(OptObject(Obj, TEXT("statsPerLevel")), Out.StatsPerLevel, /*bPartial*/ false, Context + TEXT("/statsPerLevel"));

		RequireEnum(Obj, TEXT("allowedArmor"), Out.AllowedArmor, &Valhalla::ParseArmorType, Context);
		RequireFloat(Obj, TEXT("baseSpeed"), Out.BaseSpeed, Context);
		RequireBool(Obj, TEXT("canUseMana"), Out.bCanUseMana, Context);
		RequireFloat(Obj, TEXT("baseMeleeAttackSpeedMs"), Out.BaseMeleeAttackSpeedMs, Context);
		OptFloat(Obj, TEXT("baseRangedAttackSpeedMs"), Out.BaseRangedAttackSpeedMs);

		// Per-class base auto-attack damage (2026-09-24). Optional, with the old
		// global constants as the fallback, so a data file without them plays
		// exactly as before.
		Out.BaseMeleeDamage = static_cast<float>(Valhalla::BaseMeleeDamage);
		OptFloat(Obj, TEXT("baseMeleeDamage"), Out.BaseMeleeDamage);
		Out.BaseRangedDamage = static_cast<float>(Valhalla::BaseRangedDamage);
		OptFloat(Obj, TEXT("baseRangedDamage"), Out.BaseRangedDamage);

		// New in 2.0 (present in classes.json since Phase 1b). Optional read with
		// a constant fallback so older data files still load.
		Out.VisionRange = static_cast<float>(Valhalla::DefaultVisionRange);
		OptFloat(Obj, TEXT("visionRange"), Out.VisionRange);

		OptString(Obj, TEXT("bodyId"), Out.BodyId);

		if (const TArray<TSharedPtr<FJsonValue>>* Arr = OptArray(Obj, TEXT("startingItems")))
		{
			Out.StartingItems.Reserve(Arr->Num());
			for (const TSharedPtr<FJsonValue>& Value : *Arr)
			{
				const TSharedPtr<FJsonObject>* EntryObj = nullptr;
				if (!Value.IsValid() || !Value->TryGetObject(EntryObj) || EntryObj == nullptr)
				{
					continue;
				}

				FValhallaStartingItem Entry;
				if (!OptName(*EntryObj, TEXT("itemId"), Entry.ItemId))
				{
					UE_LOG(LogValhallaCore, Warning, TEXT("[ValhallaData] %s: startingItems entry has no itemId — skipped."), *Context);
					continue;
				}
				if (!OptInt(*EntryObj, TEXT("quantity"), Entry.Quantity))
				{
					Entry.Quantity = 1;
				}
				OptBool(*EntryObj, TEXT("equipped"), Entry.bEquipped);
				Out.StartingItems.Add(Entry);
			}
		}
	}

	void ParseItem(FName Id, const TSharedPtr<FJsonObject>& Obj, FValhallaItemTemplate& Out)
	{
		const FString Context = FString::Printf(TEXT("items.json/%s"), *Id.ToString());

		Out.Id = Id;
		OptName(Obj, TEXT("id"), Out.Id);
		RequireString(Obj, TEXT("name"), Out.Name, Context);
		OptString(Obj, TEXT("description"), Out.Description);

		RequireEnum(Obj, TEXT("category"), Out.Category, &Valhalla::ParseItemCategory, Context);
		OptEnum(Obj, TEXT("equipSlot"), Out.EquipSlot, &Valhalla::ParseEquipSlot, Context);
		RequireEnum(Obj, TEXT("rarity"), Out.Rarity, &Valhalla::ParseItemRarity, Context);

		RequireBool(Obj, TEXT("stackable"), Out.bStackable, Context);
		if (!OptInt(Obj, TEXT("maxStack"), Out.MaxStack))
		{
			Out.MaxStack = 1;
		}

		if (const TSharedPtr<FJsonObject> Bonuses = OptObject(Obj, TEXT("statBonuses")))
		{
			Out.bHasStatBonuses = true;
			ParseStatBlock(Bonuses, Out.StatBonuses, /*bPartial*/ true, Context + TEXT("/statBonuses"));
		}

		Out.bHasAttackSpeed = OptFloat(Obj, TEXT("attackSpeedMs"), Out.AttackSpeedMs);
		OptFloat(Obj, TEXT("attackDamage"), Out.AttackDamage);
		OptFloat(Obj, TEXT("minDamage"), Out.MinDamage);
		OptFloat(Obj, TEXT("maxDamage"), Out.MaxDamage);
		OptBool(Obj, TEXT("isRangedWeapon"), Out.bIsRangedWeapon);

		OptString(Obj, TEXT("inventoryIcon"), Out.InventoryIcon);
		OptString(Obj, TEXT("spriteId"), Out.SpriteId);
		// 2.0 only, and optional: an item that names no mesh falls back to the
		// sprite grouping. See FValhallaItemTemplate::GetArtId.
		OptString(Obj, TEXT("meshId"), Out.MeshId);
		OptName(Obj, TEXT("weaponStyle"), Out.WeaponStyle);
	}

	void ParseSkill(FName Id, const TSharedPtr<FJsonObject>& Obj, FValhallaSkillTemplate& Out)
	{
		const FString Context = FString::Printf(TEXT("skills.json/%s"), *Id.ToString());

		Out.Id = Id;
		OptName(Obj, TEXT("id"), Out.Id);
		RequireString(Obj, TEXT("name"), Out.Name, Context);
		OptString(Obj, TEXT("description"), Out.Description);

		// `classId` is legitimately null for skills every class gets, so a null
		// here is data, not an error: NAME_None means "available to all".
		OptName(Obj, TEXT("classId"), Out.ClassId);

		RequireInt(Obj, TEXT("levelRequired"), Out.LevelRequired, Context);
		RequireEnum(Obj, TEXT("resourceType"), Out.ResourceType, &Valhalla::ParseResourceType, Context);
		OptFloat(Obj, TEXT("resourceCost"), Out.ResourceCost);
		OptFloat(Obj, TEXT("castTimeMs"), Out.CastTimeMs);
		OptFloat(Obj, TEXT("cooldownMs"), Out.CooldownMs);
		OptFloat(Obj, TEXT("range"), Out.Range);
		RequireEnum(Obj, TEXT("targetType"), Out.TargetType, &Valhalla::ParseSkillTargetType, Context);
		RequireEnum(Obj, TEXT("category"), Out.Category, &Valhalla::ParseSkillCategory, Context);
		OptInt(Obj, TEXT("iconColor"), Out.IconColor);
		OptString(Obj, TEXT("iconAbbrev"), Out.IconAbbrev);
		OptName(Obj, TEXT("scalingStat"), Out.ScalingStat);

		Out.bHasBaseDamage  = OptNumberPair(Obj, TEXT("baseDamage"),  Out.BaseDamageMin,  Out.BaseDamageMax);
		Out.bHasBaseHealing = OptNumberPair(Obj, TEXT("baseHealing"), Out.BaseHealingMin, Out.BaseHealingMax);

		OptFloat(Obj, TEXT("buffDurationMs"), Out.BuffDurationMs);
		OptFloat(Obj, TEXT("dotDamagePerSec"), Out.DotDamagePerSec);
		OptFloat(Obj, TEXT("hotHealPerSec"), Out.HotHealPerSec);
		OptFloat(Obj, TEXT("aoeRadius"), Out.AoeRadius);

		if (const TSharedPtr<FJsonObject> Projectile = OptObject(Obj, TEXT("projectile")))
		{
			Out.bHasProjectile = true;
			OptFloat(Projectile, TEXT("speed"),  Out.ProjectileSpeed);
			OptFloat(Projectile, TEXT("radius"), Out.ProjectileRadius);
		}

		OptName(Obj, TEXT("cooldownGroup"), Out.CooldownGroup);
		OptEnum(Obj, TEXT("stackingMode"), Out.StackingMode, &Valhalla::ParseStackingMode, Context);
		if (!OptInt(Obj, TEXT("maxStacks"), Out.MaxStacks))
		{
			Out.MaxStacks = 1;
		}
		OptBool(Obj, TEXT("isAutoAttack"), Out.bIsAutoAttack);
		OptBool(Obj, TEXT("requiresWeapon"), Out.bRequiresWeapon);
		OptName(Obj, TEXT("castAnimation"), Out.CastAnimation);
		OptString(Obj, TEXT("effectNotes"), Out.EffectNotes);
	}

	void ParseNPCTemplate(FName Id, const TSharedPtr<FJsonObject>& Obj, FValhallaNPCTemplate& Out)
	{
		const FString Context = FString::Printf(TEXT("npc-templates.json/%s"), *Id.ToString());

		Out.Id = Id;
		OptName(Obj, TEXT("id"), Out.Id);
		RequireString(Obj, TEXT("name"), Out.Name, Context);
		OptString(Obj, TEXT("description"), Out.Description);

		RequireEnum(Obj, TEXT("type"), Out.Type, &Valhalla::ParseNPCType, Context);
		OptName(Obj, TEXT("role"), Out.Role);
		RequireInt(Obj, TEXT("level"), Out.Level, Context);

		// `stats` is a Partial<StatBlock> and is usually `{}` in practice.
		ParseStatBlock(OptObject(Obj, TEXT("stats")), Out.Stats, /*bPartial*/ true, Context + TEXT("/stats"));

		RequireFloat(Obj, TEXT("hp"), Out.Hp, Context);
		OptFloat(Obj, TEXT("mana"), Out.Mana);
		OptName(Obj, TEXT("lootTableId"), Out.LootTableId);
		OptName(Obj, TEXT("weaponId"), Out.WeaponId);
		RequireEnum(Obj, TEXT("behaviorType"), Out.BehaviorType, &Valhalla::ParseNPCBehavior, Context);

		// npcs.ts:21 — defaults to true for enemies, false for friendly NPCs.
		Out.bCanAggro = (Out.Type == EValhallaNPCType::Enemy);
		OptBool(Obj, TEXT("canAggro"), Out.bCanAggro);

		OptFloat(Obj, TEXT("aggroRange"), Out.AggroRange);

		// B-10 social aggro. A blank group means "this template only"; a blank
		// or zero range means the aggro range.
		OptBool(Obj, TEXT("canSocialAggro"), Out.bCanSocialAggro);
		OptName(Obj, TEXT("socialGroup"), Out.SocialGroup);
		if (Out.SocialGroup.IsNone())
		{
			Out.SocialGroup = Out.Id;
		}
		OptFloat(Obj, TEXT("socialRange"), Out.SocialRange);
		if (Out.SocialRange <= 0.f)
		{
			Out.SocialRange = Out.AggroRange;
		}

		// npcs.ts:24 — leashRange defaults to aggroRange * 3.
		Out.LeashRange = Out.AggroRange * 3.f;
		OptFloat(Obj, TEXT("leashRange"), Out.LeashRange);

		// npcs.ts:26-32 — these defaults are the ones the 1.0 server applies.
		Out.Damage = 5.f;
		OptFloat(Obj, TEXT("damage"), Out.Damage);
		OptFloat(Obj, TEXT("minDamage"), Out.MinDamage);
		OptFloat(Obj, TEXT("maxDamage"), Out.MaxDamage);
		Out.AttackSpeedMs = 1500.f;
		OptFloat(Obj, TEXT("attackSpeed"), Out.AttackSpeedMs);
		// A blank or zero range means the default for the attack type: 40 to
		// swing, 600 to shoot (the player's Ranged Attack reach).
		OptEnum(Obj, TEXT("attackType"), Out.AttackType, &Valhalla::ParseNPCAttackType, Context);
		Out.AttackRange = 0.f;
		OptFloat(Obj, TEXT("attackRange"), Out.AttackRange);
		if (Out.AttackRange <= 0.f)
		{
			Out.AttackRange = Out.AttackType == EValhallaNPCAttackType::Ranged ? 600.f : 40.f;
		}
		Out.MoveSpeed = 60.f;
		OptFloat(Obj, TEXT("moveSpeed"), Out.MoveSpeed);

		RequireFloat(Obj, TEXT("respawnMs"), Out.RespawnMs, Context);
		OptNameArray(Obj, TEXT("skills"), Out.Skills);
		OptInt(Obj, TEXT("spriteColor"), Out.SpriteColor);
		if (!OptFloat(Obj, TEXT("spriteSize"), Out.SpriteSize))
		{
			Out.SpriteSize = 1.f;
		}
		OptInt(Obj, TEXT("xpReward"), Out.XpReward);
		OptStringArray(Obj, TEXT("dialogue"), Out.Dialogue);
		OptNameArray(Obj, TEXT("vendorInventory"), Out.VendorInventory);
	}

	void ParseLootTable(FName Id, const TSharedPtr<FJsonObject>& Obj, FValhallaLootTable& Out)
	{
		const FString Context = FString::Printf(TEXT("loot-tables.json/%s"), *Id.ToString());

		Out.Id = Id;
		OptName(Obj, TEXT("id"), Out.Id);
		RequireString(Obj, TEXT("name"), Out.Name, Context);

		const TArray<TSharedPtr<FJsonValue>>* Arr = OptArray(Obj, TEXT("entries"));
		if (Arr == nullptr)
		{
			WarnMissing(Context, TEXT("entries"));
			return;
		}

		Out.Entries.Reserve(Arr->Num());
		for (const TSharedPtr<FJsonValue>& Value : *Arr)
		{
			const TSharedPtr<FJsonObject>* EntryObj = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(EntryObj) || EntryObj == nullptr)
			{
				continue;
			}

			FValhallaLootEntry Entry;
			if (!OptName(*EntryObj, TEXT("itemId"), Entry.ItemId))
			{
				UE_LOG(LogValhallaCore, Warning, TEXT("[ValhallaData] %s: loot entry has no itemId — skipped."), *Context);
				continue;
			}
			OptFloat(*EntryObj, TEXT("weight"), Entry.Weight);
			OptFloat(*EntryObj, TEXT("dropChance"), Entry.DropChance);
			if (!OptInt(*EntryObj, TEXT("minQuantity"), Entry.MinQuantity)) { Entry.MinQuantity = 1; }
			if (!OptInt(*EntryObj, TEXT("maxQuantity"), Entry.MaxQuantity)) { Entry.MaxQuantity = 1; }
			Out.Entries.Add(Entry);
		}
	}

	/**
	 * A `#rrggbb` (sRGB) colour, as linear. False when absent; false with a
	 * warning when present but not six hex digits after a '#'.
	 */
	bool OptHexColour(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, FLinearColor& Out, const FString& Context)
	{
		FString Text;
		if (!OptString(Obj, Key, Text))
		{
			return false;
		}
		Text.TrimStartAndEndInline();

		bool bValid = Text.Len() == 7 && Text[0] == TEXT('#');
		for (int32 Index = 1; bValid && Index < Text.Len(); ++Index)
		{
			bValid = FChar::IsHexDigit(Text[Index]);
		}
		if (!bValid)
		{
			UE_LOG(LogValhallaCore, Warning, TEXT("[ValhallaData] %s: '%s' is '%s', not a #rrggbb colour — ignored."),
				*Context, Key, *Text);
			return false;
		}

		Out = FLinearColor::FromSRGBColor(FColor::FromHex(Text));
		return true;
	}

	/** An optional number that must be finite and >= 0. Left untouched (with a warning) otherwise. */
	void OptNonNegative(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, float& Out, const FString& Context)
	{
		float Value = 0.f;
		if (!OptFloat(Obj, Key, Value))
		{
			return;
		}
		if (!FMath::IsFinite(Value) || Value < 0.f)
		{
			UE_LOG(LogValhallaCore, Warning, TEXT("[ValhallaData] %s: '%s' must be a number >= 0 — ignored."),
				*Context, Key);
			return;
		}
		Out = Value;
	}

	/** maps.ts `ZoneAtmosphere` (B-06). Every field optional; see FValhallaAtmosphereProfile for the defaults. */
	void ParseZoneAtmosphere(const TSharedPtr<FJsonObject>& Obj, FValhallaAtmosphereProfile& Out, const FString& Context)
	{
		OptNonNegative(Obj, TEXT("visionScale"), Out.VisionScale, Context);
		OptNonNegative(Obj, TEXT("visionClearFraction"), Out.VisionClearFraction, Context);
		OptNonNegative(Obj, TEXT("relevancyMarginCm"), Out.RelevancyMarginCm, Context);
		if (Out.VisionClearFraction >= 1.f)
		{
			UE_LOG(LogValhallaCore, Warning,
				TEXT("[ValhallaData] %s: visionClearFraction %.3f must be below 1 — using %.3f."),
				*Context, Out.VisionClearFraction, FValhallaAtmosphereProfile::DefaultVisionClearFraction);
			Out.VisionClearFraction = FValhallaAtmosphereProfile::DefaultVisionClearFraction;
		}
		Out.bHasFogColor = OptHexColour(Obj, TEXT("fogColor"), Out.FogColor, Context);
		OptNonNegative(Obj, TEXT("heightFogDensity"), Out.HeightFogDensity, Context);
		OptNonNegative(Obj, TEXT("heightFogStartCm"), Out.HeightFogStartCm, Context);
		OptNonNegative(Obj, TEXT("sunIntensityScale"), Out.SunIntensityScale, Context);
		OptNonNegative(Obj, TEXT("skyLightIntensityScale"), Out.SkyLightIntensityScale, Context);
		Out.bHasGradeTint = OptHexColour(Obj, TEXT("gradeTint"), Out.GradeTint, Context);
		OptNonNegative(Obj, TEXT("cameraMaxArmCm"), Out.CameraMaxArmCm, Context);
		OptNonNegative(Obj, TEXT("firelightGlow"), Out.FirelightGlow, Context);
		OptNonNegative(Obj, TEXT("firelightRangeCm"), Out.FirelightRangeCm, Context);

		// The pre-follow-up absolute fields: say so rather than silently ignore.
		for (const TCHAR* Retired : { TEXT("visionClearRadiusCm"), TEXT("visionFadeWidthCm"), TEXT("netRelevancyRadiusCm") })
		{
			if (Obj.IsValid() && Obj->HasField(Retired))
			{
				UE_LOG(LogValhallaCore, Warning,
					TEXT("[ValhallaData] %s: '%s' is no longer used — vision is visionScale x the class range now."),
					*Context, Retired);
			}
		}
	}

	void ParseZone(FName Id, const TSharedPtr<FJsonObject>& Obj, FValhallaZoneConfig& Out)
	{
		const FString Context = FString::Printf(TEXT("zones.json/%s"), *Id.ToString());

		Out.Id = Id;
		OptName(Obj, TEXT("id"), Out.Id);
		RequireString(Obj, TEXT("name"), Out.Name, Context);
		RequireString(Obj, TEXT("mapFile"), Out.MapFile, Context);

		// B-06. Before defaultSpawn, whose absence returns early: a zone with a
		// broken spawn still keeps its look.
		if (const TSharedPtr<FJsonObject> Atmosphere = OptObject(Obj, TEXT("atmosphere")))
		{
			Out.bHasAtmosphere = true;
			ParseZoneAtmosphere(Atmosphere, Out.Atmosphere, Context + TEXT("/atmosphere"));
		}

		const TSharedPtr<FJsonObject> Spawn = OptObject(Obj, TEXT("defaultSpawn"));
		if (!Spawn.IsValid())
		{
			WarnMissing(Context, TEXT("defaultSpawn"));
			return;
		}
		if (!OptFloat(Spawn, TEXT("x"), Out.DefaultSpawnX)) { WarnMissing(Context, TEXT("defaultSpawn.x")); }
		if (!OptFloat(Spawn, TEXT("y"), Out.DefaultSpawnY)) { WarnMissing(Context, TEXT("defaultSpawn.y")); }
	}

	/**
	 * Walk a `{ id: {...} }` collection, converting each record with Converter.
	 * Non-object members are skipped with a warning rather than aborting.
	 */
	template <typename RecordType, typename ConverterFn>
	void ParseCollection(const TSharedPtr<FJsonObject>& Collection, TMap<FName, RecordType>& OutMap, ConverterFn Converter, const TCHAR* FileName)
	{
		if (!Collection.IsValid())
		{
			return;
		}

		OutMap.Reserve(Collection->Values.Num());
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Collection->Values)
		{
			const TSharedPtr<FJsonObject>* RecordObj = nullptr;
			if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(RecordObj) || RecordObj == nullptr)
			{
				UE_LOG(LogValhallaCore, Warning, TEXT("[ValhallaData] %s: entry '%s' is not an object — skipped."),
					FileName, *Pair.Key);
				continue;
			}

			const FName Id(*Pair.Key);
			RecordType Record;
			Converter(Id, *RecordObj, Record);
			OutMap.Add(Id, MoveTemp(Record));
		}
	}

	/** Sorted key list, so callers and tests get a stable order out of a TMap. */
	template <typename ValueType>
	TArray<FName> SortedKeys(const TMap<FName, ValueType>& Map)
	{
		TArray<FName> Keys;
		Map.GetKeys(Keys);
		Keys.Sort(FNameLexicalLess());
		return Keys;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  FValhallaDataTables
// ─────────────────────────────────────────────────────────────────────────────

void FValhallaDataTables::Reset()
{
	Classes.Reset();
	ClassColors.Reset();
	Items.Reset();
	Skills.Reset();
	ClassSkills.Reset();
	NPCTemplates.Reset();
	LootTables.Reset();
	Zones.Reset();
	UiConfig.Reset();
	UIConfigTyped = FValhallaUIConfig();
	FailedFiles.Reset();
}

// ─────────────────────────────────────────────────────────────────────────────
//  UValhallaDataSubsystem
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	TAutoConsoleVariable<int32> CVarForceDownloadedData(
		TEXT("valhalla.Data.ForceDownload"),
		0,
		TEXT("1 makes game clients read their downloaded copy of the game data (Saved/Data, synced from the backend's /api/data) ")
		TEXT("even when the repo's shared/data is on disk, which is how a packaged client works. Read when a game instance starts."),
		ECVF_Default);
}

void UValhallaDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ChooseDataSource();
	LoadAll();
}

FString UValhallaDataSubsystem::GetDownloadedDataDir()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Data")));
}

FString UValhallaDataSubsystem::GetBundledDataDir()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data")));
}

bool UValhallaDataSubsystem::HasAllDataFiles(const FString& Dir)
{
	if (Dir.IsEmpty())
	{
		return false;
	}
	for (const FString& Name : GetDataFilenames())
	{
		if (!FPaths::FileExists(FPaths::Combine(Dir, Name)))
		{
			return false;
		}
	}
	return true;
}

void UValhallaDataSubsystem::SetDataRootOverride(const FString& InRoot)
{
	DataRootOverride = InRoot;
}

void UValhallaDataSubsystem::ChooseDataSource()
{
	const UValhallaDataSettings* Settings = UValhallaDataSettings::Get();
	const FString RepoRoot = Settings ? Settings->GetResolvedDataRoot() : UValhallaDataSettings::ResolveDataRoot(FString());

	const UGameInstance* GameInstance = GetGameInstance();
	const bool bDedicatedServer = GameInstance && GameInstance->IsDedicatedServerInstance();
	const bool bForce = CVarForceDownloadedData.GetValueOnGameThread() != 0;

	// The server always reads the files it is authoritative for; so does any
	// client that can see them, unless the download path is being tested.
	if (bDedicatedServer || (!bForce && HasAllDataFiles(RepoRoot)))
	{
		DataRootOverride.Reset();
		return;
	}

	// A client without the repo. Seed Saved/Data so there is always a complete
	// set to start from — the build's staged copy first, the repo (only
	// reachable when forced) second — and let SyncGameData bring it up to date.
	const FString Downloaded = GetDownloadedDataDir();
	if (!HasAllDataFiles(Downloaded))
	{
		const FString Seed = HasAllDataFiles(GetBundledDataDir()) ? GetBundledDataDir()
			: (HasAllDataFiles(RepoRoot) ? RepoRoot : FString());
		if (!Seed.IsEmpty())
		{
			IFileManager::Get().MakeDirectory(*Downloaded, /*Tree=*/true);
			for (const FString& Name : GetDataFilenames())
			{
				IFileManager::Get().Copy(*FPaths::Combine(Downloaded, Name), *FPaths::Combine(Seed, Name));
			}
			UE_LOG(LogValhallaCore, Log, TEXT("[ValhallaData] Seeded the downloaded-data folder '%s' from '%s'."), *Downloaded, *Seed);
		}
		else
		{
			UE_LOG(LogValhallaCore, Warning,
				TEXT("[ValhallaData] No game data on this machine yet ('%s' and '%s' are both incomplete); waiting for the first download from the backend."),
				*Downloaded, *GetBundledDataDir());
		}
	}

	DataRootOverride = Downloaded;
	UE_LOG(LogValhallaCore, Log, TEXT("[ValhallaData] Client reads the downloaded copy in '%s'%s."),
		*Downloaded, bForce ? TEXT(" (valhalla.Data.ForceDownload)") : TEXT(""));
}

void UValhallaDataSubsystem::Deinitialize()
{
	Tables.Reset();
	bLoaded = false;
	Super::Deinitialize();
}

bool UValhallaDataSubsystem::LoadAll()
{
	const UValhallaDataSettings* Settings = UValhallaDataSettings::Get();
	LoadedDataRoot = !DataRootOverride.IsEmpty() ? DataRootOverride
		: (Settings ? Settings->GetResolvedDataRoot() : UValhallaDataSettings::ResolveDataRoot(FString()));

	UE_LOG(LogValhallaCore, Log, TEXT("[ValhallaData] Loading from '%s'."), *LoadedDataRoot);

	Tables.Reset();
	const bool bSuccess = LoadTablesFromRoot(LoadedDataRoot, Tables);
	bLoaded = true;	// partial data is still usable; IsLoaded means "we tried".

	// B-13: remember exactly which bytes were loaded, for the editor's
	// live-vs-disk check. Hashed right after parsing; a save landing between
	// the two is caught by the hot-reload watcher two seconds later anyway.
	LoadedFileHashes.Reset();
	LoadedAtUtc = FDateTime::UtcNow();
	for (const FString& Name : GetDataFilenames())
	{
		TArray<uint8> Bytes;
		if (FFileHelper::LoadFileToArray(Bytes, *FPaths::Combine(LoadedDataRoot, Name)))
		{
			uint8 Digest[20];
			FSHA1::HashBuffer(Bytes.GetData(), static_cast<uint64>(Bytes.Num()), Digest);
			LoadedFileHashes.Add(Name, BytesToHex(Digest, 20).ToLower());
		}
	}

	UE_LOG(LogValhallaCore, Log,
		TEXT("[ValhallaData] Loaded %d classes, %d skills, %d items, %d NPC templates, %d loot tables, %d zones.%s"),
		Tables.Classes.Num(),
		Tables.Skills.Num(),
		Tables.Items.Num(),
		Tables.NPCTemplates.Num(),
		Tables.LootTables.Num(),
		Tables.Zones.Num(),
		bSuccess ? TEXT("") : TEXT(" (some files failed — see errors above)"));

	return bSuccess;
}

bool UValhallaDataSubsystem::ReloadUIConfig()
{
	const FString Path = GetUIConfigPath();

	FString RawText;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(RawText, *Path))
	{
		UE_LOG(LogValhallaCore, Warning, TEXT("[ValhallaData] ReloadUIConfig: could not read '%s'."), *Path);
		return false;
	}

	const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(RawText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		// Keep the previous layout: a half-saved file mid-edit must not blank the HUD.
		UE_LOG(LogValhallaCore, Warning, TEXT("[ValhallaData] ReloadUIConfig: '%s' is not valid JSON; keeping the old layout."), *Path);
		return false;
	}

	Tables.UiConfig = Root;
	FValhallaUIConfig::Parse(Root, Tables.UIConfigTyped);
	UE_LOG(LogValhallaCore, Log, TEXT("[ValhallaData] ui-config.json reloaded (version %s)."), *Tables.UIConfigTyped.Version);
	return true;
}

FString UValhallaDataSubsystem::GetUIConfigPath() const
{
	FString Root = !DataRootOverride.IsEmpty() ? DataRootOverride : LoadedDataRoot;
	if (Root.IsEmpty())
	{
		const UValhallaDataSettings* Settings = UValhallaDataSettings::Get();
		Root = Settings ? Settings->GetResolvedDataRoot() : UValhallaDataSettings::ResolveDataRoot(FString());
	}
	return FPaths::Combine(Root, TEXT("ui-config.json"));
}

bool UValhallaDataSubsystem::Reload()
{
	UE_LOG(LogValhallaCore, Log, TEXT("[ValhallaData] Reloading."));
	return LoadAll();
}

const TArray<FString>& UValhallaDataSubsystem::GetDataFilenames()
{
	// In LoadTablesFromRoot's own order, so a reader can check the two lists
	// against each other at a glance.
	static const TArray<FString> Filenames = {
		TEXT("classes.json"),
		TEXT("items.json"),
		TEXT("skills.json"),
		TEXT("npc-templates.json"),
		TEXT("loot-tables.json"),
		TEXT("zones.json"),
		TEXT("ui-config.json"),
	};
	return Filenames;
}

bool UValhallaDataSubsystem::LoadTablesFromRoot(const FString& ResolvedDataRoot, FValhallaDataTables& OutTables)
{
	bool bAllFilesLoaded = true;

	auto NoteFailure = [&OutTables, &bAllFilesLoaded](const TCHAR* FileName)
	{
		OutTables.FailedFiles.AddUnique(FileName);
		bAllFilesLoaded = false;
	};

	// ── classes.json ────────────────────────────────────────────────────
	if (const TSharedPtr<FJsonObject> File = LoadJsonFile(ResolvedDataRoot, TEXT("classes.json")))
	{
		ParseCollection(GetCollection(File, TEXT("classes"), TEXT("classes.json")), OutTables.Classes, &ParseClass, TEXT("classes.json"));

		// `classColors` is a flat id -> packed color map, not a record table.
		if (const TSharedPtr<FJsonObject> Colors = OptObject(File, TEXT("classColors")))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Colors->Values)
			{
				double Packed = 0.0;
				if (Pair.Value.IsValid() && Pair.Value->TryGetNumber(Packed))
				{
					OutTables.ClassColors.Add(FName(*Pair.Key), static_cast<int32>(FMath::RoundToDouble(Packed)));
				}
			}
		}
	}
	else
	{
		NoteFailure(TEXT("classes.json"));
	}

	// ── items.json ──────────────────────────────────────────────────────
	if (const TSharedPtr<FJsonObject> File = LoadJsonFile(ResolvedDataRoot, TEXT("items.json")))
	{
		ParseCollection(GetCollection(File, TEXT("items"), TEXT("items.json")), OutTables.Items, &ParseItem, TEXT("items.json"));
	}
	else
	{
		NoteFailure(TEXT("items.json"));
	}

	// ── skills.json ─────────────────────────────────────────────────────
	if (const TSharedPtr<FJsonObject> File = LoadJsonFile(ResolvedDataRoot, TEXT("skills.json")))
	{
		ParseCollection(GetCollection(File, TEXT("skills"), TEXT("skills.json")), OutTables.Skills, &ParseSkill, TEXT("skills.json"));

		// `classSkills` maps a class id to the ordered skill ids it may learn.
		if (const TSharedPtr<FJsonObject> ClassSkills = OptObject(File, TEXT("classSkills")))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : ClassSkills->Values)
			{
				const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
				if (!Pair.Value.IsValid() || !Pair.Value->TryGetArray(Arr) || Arr == nullptr)
				{
					UE_LOG(LogValhallaCore, Warning, TEXT("[ValhallaData] skills.json: classSkills['%s'] is not an array — skipped."), *Pair.Key);
					continue;
				}

				TArray<FName>& SkillIds = OutTables.ClassSkills.FindOrAdd(FName(*Pair.Key));
				SkillIds.Reserve(Arr->Num());
				for (const TSharedPtr<FJsonValue>& Value : *Arr)
				{
					if (Value.IsValid() && Value->Type == EJson::String)
					{
						const FString AsString = Value->AsString();
						if (!AsString.IsEmpty())
						{
							SkillIds.Add(FName(*AsString));
						}
					}
				}
			}
		}
	}
	else
	{
		NoteFailure(TEXT("skills.json"));
	}

	// ── npc-templates.json ──────────────────────────────────────────────
	if (const TSharedPtr<FJsonObject> File = LoadJsonFile(ResolvedDataRoot, TEXT("npc-templates.json")))
	{
		ParseCollection(GetCollection(File, TEXT("templates"), TEXT("npc-templates.json")), OutTables.NPCTemplates, &ParseNPCTemplate, TEXT("npc-templates.json"));
	}
	else
	{
		NoteFailure(TEXT("npc-templates.json"));
	}

	// ── loot-tables.json ────────────────────────────────────────────────
	if (const TSharedPtr<FJsonObject> File = LoadJsonFile(ResolvedDataRoot, TEXT("loot-tables.json")))
	{
		ParseCollection(GetCollection(File, TEXT("tables"), TEXT("loot-tables.json")), OutTables.LootTables, &ParseLootTable, TEXT("loot-tables.json"));
	}
	else
	{
		NoteFailure(TEXT("loot-tables.json"));
	}

	// ── zones.json ──────────────────────────────────────────────────────
	if (const TSharedPtr<FJsonObject> File = LoadJsonFile(ResolvedDataRoot, TEXT("zones.json")))
	{
		ParseCollection(GetCollection(File, TEXT("zones"), TEXT("zones.json")), OutTables.Zones, &ParseZone, TEXT("zones.json"));
	}
	else
	{
		NoteFailure(TEXT("zones.json"));
	}

	// ── ui-config.json ──────────────────────────────────────────────────
	// Stored raw. Phase 8 owns the UMG mapping; converting it now would only
	// bake in a guess about what the widgets need.
	if (const TSharedPtr<FJsonObject> File = LoadJsonFile(ResolvedDataRoot, TEXT("ui-config.json")))
	{
		OutTables.UiConfig = File;
		if (!FValhallaUIConfig::Parse(File, OutTables.UIConfigTyped))
		{
			UE_LOG(LogValhallaCore, Warning,
				TEXT("[ValhallaData] ui-config.json is missing one or more of its three sections (chat, inventory, nameplates); defaults fill the gaps."));
		}
	}
	else
	{
		NoteFailure(TEXT("ui-config.json"));
	}

	return bAllFilesLoaded;
}

// ── C++ accessors ───────────────────────────────────────────────────────────

const FValhallaClassTemplate* UValhallaDataSubsystem::FindClass(FName ClassId) const
{
	return Tables.Classes.Find(ClassId);
}

const FValhallaItemTemplate* UValhallaDataSubsystem::FindItem(FName ItemId) const
{
	return Tables.Items.Find(ItemId);
}

const FValhallaSkillTemplate* UValhallaDataSubsystem::FindSkill(FName SkillId) const
{
	return Tables.Skills.Find(SkillId);
}

const FValhallaNPCTemplate* UValhallaDataSubsystem::FindNPCTemplate(FName NPCId) const
{
	return Tables.NPCTemplates.Find(NPCId);
}

const FValhallaLootTable* UValhallaDataSubsystem::FindLootTable(FName LootTableId) const
{
	return Tables.LootTables.Find(LootTableId);
}

const FValhallaZoneConfig* UValhallaDataSubsystem::FindZone(FName ZoneId) const
{
	return Tables.Zones.Find(ZoneId);
}

// ── Blueprint accessors ─────────────────────────────────────────────────────

bool UValhallaDataSubsystem::GetClassTemplate(FName ClassId, FValhallaClassTemplate& OutClass) const
{
	if (const FValhallaClassTemplate* Found = FindClass(ClassId))
	{
		OutClass = *Found;
		return true;
	}
	OutClass = FValhallaClassTemplate();
	return false;
}

bool UValhallaDataSubsystem::GetItem(FName ItemId, FValhallaItemTemplate& OutItem) const
{
	if (const FValhallaItemTemplate* Found = FindItem(ItemId))
	{
		OutItem = *Found;
		return true;
	}
	OutItem = FValhallaItemTemplate();
	return false;
}

bool UValhallaDataSubsystem::GetSkill(FName SkillId, FValhallaSkillTemplate& OutSkill) const
{
	if (const FValhallaSkillTemplate* Found = FindSkill(SkillId))
	{
		OutSkill = *Found;
		return true;
	}
	OutSkill = FValhallaSkillTemplate();
	return false;
}

bool UValhallaDataSubsystem::GetNPCTemplate(FName NPCId, FValhallaNPCTemplate& OutTemplate) const
{
	if (const FValhallaNPCTemplate* Found = FindNPCTemplate(NPCId))
	{
		OutTemplate = *Found;
		return true;
	}
	OutTemplate = FValhallaNPCTemplate();
	return false;
}

bool UValhallaDataSubsystem::GetLootTable(FName LootTableId, FValhallaLootTable& OutTable) const
{
	if (const FValhallaLootTable* Found = FindLootTable(LootTableId))
	{
		OutTable = *Found;
		return true;
	}
	OutTable = FValhallaLootTable();
	return false;
}

bool UValhallaDataSubsystem::GetZone(FName ZoneId, FValhallaZoneConfig& OutZone) const
{
	if (const FValhallaZoneConfig* Found = FindZone(ZoneId))
	{
		OutZone = *Found;
		return true;
	}
	OutZone = FValhallaZoneConfig();
	return false;
}

TArray<FName> UValhallaDataSubsystem::GetClassSkills(FName ClassId) const
{
	if (const TArray<FName>* Found = Tables.ClassSkills.Find(ClassId))
	{
		return *Found;
	}
	return TArray<FName>();
}

int32 UValhallaDataSubsystem::GetClassColor(FName ClassId) const
{
	if (const int32* Found = Tables.ClassColors.Find(ClassId))
	{
		return *Found;
	}
	return 0xFFFFFF;
}

TArray<FName> UValhallaDataSubsystem::GetAllClassIds() const       { return SortedKeys(Tables.Classes); }
TArray<FName> UValhallaDataSubsystem::GetAllItemIds() const        { return SortedKeys(Tables.Items); }
TArray<FName> UValhallaDataSubsystem::GetAllSkillIds() const       { return SortedKeys(Tables.Skills); }
TArray<FName> UValhallaDataSubsystem::GetAllNPCTemplateIds() const { return SortedKeys(Tables.NPCTemplates); }
TArray<FName> UValhallaDataSubsystem::GetAllLootTableIds() const   { return SortedKeys(Tables.LootTables); }
TArray<FName> UValhallaDataSubsystem::GetAllZoneIds() const        { return SortedKeys(Tables.Zones); }
