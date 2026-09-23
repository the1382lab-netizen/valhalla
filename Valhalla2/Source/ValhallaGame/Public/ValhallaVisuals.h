// Copyright Valhalla 2.0. All Rights Reserved.
//
// Everything that turns a *data* id into an *asset* path lives here, and
// nowhere else. Phase 4c has three separate consumers of that mapping — the
// player character, the NPC, and the debug console commands — and if each of
// them spelled "/Game/Valhalla/Characters/Equipment/SK_" out for itself, a
// renamed folder would break two of the three silently.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ValhallaTypes.h"
#include "ValhallaVisuals.generated.h"

class UAnimSequence;
class USkeletalMesh;
class UStaticMesh;
struct FValhallaItemTemplate;

/**
 * Which equipment asset a slot resolved to, and which piece of art it came
 * from. One line per assignment in the log, which is what the Phase 4c gate is
 * read off.
 */
VALHALLAGAME_API DECLARE_LOG_CATEGORY_EXTERN(LogValhallaVisual, Log, All);

/**
 * Which cycle a body plays when it attacks.
 *
 * The port of `WEAPON_STYLE_ANIM` (shared/src/paperdoll.ts:48). 1.0 picked the
 * attack sprite sheet from the equipped weapon's `weaponStyle` and not from the
 * class, so a wizard holding a sword swings it; that is reproduced exactly.
 */
UENUM()
enum class EValhallaAttackCycle : uint8
{
	/** `A_Attack` — sword, greatsword, mace, or bare hands. */
	Melee,
	/** `A_Shoot` — bow. */
	Shoot,
	/** `A_Cast` — every non-auto-attack skill, whatever is held. (A staff swings with A_Attack since casters melee.) */
	Cast,
};

/**
 * One logical animation, independent of which asset it currently maps to.
 *
 * Deliberately not an FName: the state machine compares these dozens of times
 * a second and a typo'd FName is a bug that only shows up as a T-pose.
 */
UENUM()
enum class EValhallaAnim : uint8
{
	Idle,
	Walk,
	Attack,
	Shoot,
	Cast,
	Hit,
	Death,
};

/**
 * Pure look-ups from game data to content. No state, no world, no side effects
 * except the log line that RESOLVE emits.
 */
UCLASS()
class VALHALLAGAME_API UValhallaVisuals : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ── Content roots ───────────────────────────────────────────────────

	/** Where the imported character art lives. */
	static const TCHAR* CharactersRoot() { return TEXT("/Game/Valhalla/Characters"); }

	/** The one skeleton every skeletal mesh in the project is bound to. */
	static FString SkeletonPath() { return FString(CharactersRoot()) + TEXT("/SK_Valhalla_Skeleton"); }

	/** The body mesh. There is exactly one; the skin tint is what varies. */
	static FString BodyMeshPath() { return FString(CharactersRoot()) + TEXT("/Body/SK_Valhalla_Body"); }

	/** `A_Idle` … `A_Death`, by logical name. */
	static FString AnimPath(EValhallaAnim Anim);

	/**
	 * The asset for one equipped item, or an empty string when the item has no
	 * art (rings) or no template.
	 *
	 * The mapping is `FValhallaItemTemplate::GetArtId` and nothing else:
	 * `meshId` when the item names one, `spriteId` otherwise. The 1.0 paperdoll
	 * keyed its layers on `spriteId` so that six items sharing a look shared a
	 * sprite, and that is still the default — `iron_sword` and `iron_dagger`
	 * are both drawn as `SM_sword_iron` because 1.0 drew both as `sword_iron`.
	 * `meshId` is how an item opts out of a grouping that was right for sprites
	 * and wrong for meshes, without editing the field the 1.0 client reads.
	 *
	 * @param OutIsSkeletal  True when the path names a USkeletalMesh (worn
	 *                       armour), false when it names a UStaticMesh (a
	 *                       weapon or a shield that hangs off a socket).
	 */
	static FString EquipmentAssetPath(const FValhallaItemTemplate& Item, bool& OutIsSkeletal);

	/** As above, resolving the item id through the data subsystem first. */
	static FString EquipmentAssetPathForItem(const UObject* WorldContext, FName ItemId, bool& OutIsSkeletal);

	// ── Appearance ──────────────────────────────────────────────────────

	/**
	 * `bodyId` -> skin tint, linear.
	 *
	 * The five 1.0 bodies, in the hex the art was authored against. An unknown
	 * or empty id is `body_fair`, which is what a 1.0 character with no body
	 * chosen rendered as.
	 */
	static FLinearColor SkinTintForBodyId(const FString& BodyId);

	/** True for `body_elder`, the one body whose hair is grey rather than its own colour. */
	static bool BodyHasGreyHair(const FString& BodyId);

	// ── Animation selection ─────────────────────────────────────────────

	/** `weaponStyle` -> attack cycle. Nothing equipped swings a fist, i.e. Melee. */
	static EValhallaAttackCycle AttackCycleForWeaponStyle(FName WeaponStyle);

	/** The cycle for whatever weapon a player state currently has equipped. */
	static EValhallaAttackCycle AttackCycleForEquippedWeapon(const UObject* WorldContext, FName WeaponItemId);

	/** The animation an attack cycle plays. */
	static EValhallaAnim AnimForAttackCycle(EValhallaAttackCycle Cycle);

	/**
	 * The animation a skill plays when it goes off.
	 *
	 * Offensive, debuff, healing and buff skills are spellcasting and play
	 * `A_Cast`; an auto-attack plays whatever the weapon plays; anything else
	 * (utility) is a gesture and also casts. A skill flagged `isAutoAttack` is
	 * the swing itself and never casts, which is why the flag is checked first.
	 */
	static EValhallaAnim AnimForSkill(const FValhallaSkillTemplate& Skill, EValhallaAttackCycle WeaponCycle);

	// ── Mesh placement, measured from the import ────────────────────────

	/**
	 * How far below the capsule centre the mesh origin sits, cm.
	 *
	 * The art is authored feet-at-origin and 120 cm tall — measured after
	 * import: the body mesh's bounds are Z 0..120 with the origin at 60. The
	 * capsule is 60 cm half-height, so dropping the mesh by exactly the half
	 * height puts the feet on the capsule's bottom cap.
	 */
	static constexpr float MeshZOffset = -60.f;

	/**
	 * Yaw applied to the mesh so the model faces the way the actor does, degrees.
	 *
	 * Measured, not assumed: after import `socket_back` sits at component-space
	 * Y = -14, so the model's back is -Y and its forward is +Y. An Unreal actor
	 * faces +X, and rotating local +Y onto world +X is a -90 degree yaw. (This
	 * is the same -90 that Unreal's own ACharacter template uses, for the same
	 * reason — it is what a Y-forward DCC export always needs.)
	 */
	static constexpr float MeshYaw = -90.f;

	/**
	 * Roll applied to anything hung on `socket_offhand_l`, degrees.
	 *
	 * Measured. Every rigid prop is modelled the same way — long axis along
	 * local +Y, flat face normal along local +Z — and `socket_weapon_r` is
	 * oriented to suit that: its local +Y runs up the character, so a sword
	 * stands upright with no correction. `socket_offhand_l` is an *identity*
	 * bone, which puts local +Y along the character's forward and local +Z
	 * straight up, and a shield hung there lies flat on the ground like a
	 * dinner plate. Rolling it -90 stands it on edge with its face outward.
	 *
	 * This is a correction for the bones as exported, not a style choice; if
	 * the armature's offhand bone is ever re-rolled in Blender to match the
	 * weapon bone, this becomes 0 and the art stops needing it.
	 */
	static constexpr float OffhandSocketRoll = -90.f;

	/**
	 * Pitch correction for a bow put in the weapon socket, degrees.
	 *
	 * The bow is modelled to be held across the body in the *offhand* socket
	 * and is exported with its limbs along the socket's up axis. `short_bow` is
	 * a two-handed weapon and so occupies the weapon slot, which means it lands
	 * in the right hand; this turns it to face the way the character is aiming.
	 * 0 everywhere else.
	 */
	static constexpr float BowWeaponSocketPitch = 90.f;

	/** The bone the weapon hangs from. */
	static FName WeaponSocket() { return TEXT("socket_weapon_r"); }

	/** The bone the shield or bow hangs from. */
	static FName OffhandSocket() { return TEXT("socket_offhand_l"); }

	/** The bone a cloak or a slung weapon hangs from. Phase 8 uses it for VFX too. */
	static FName BackSocket() { return TEXT("socket_back"); }

	/** Above the head: nameplates in Phase 8, and `A_Cast`'s glow. */
	static FName HeadTopSocket() { return TEXT("socket_head_top"); }
};
