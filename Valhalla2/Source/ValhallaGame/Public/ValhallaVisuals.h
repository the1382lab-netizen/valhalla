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
class USkeletalMeshComponent;
class UStaticMeshComponent;
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
/** Which body every character is drawn with (`valhalla.Visual.BodyProfile`). */
enum class EValhallaBodyProfile : uint8
{
	/** SK_Valhalla_Body (B-15 Wave 2 glTF), with its own armour and hair. */
	Legacy = 0,
	/** The Valhalla MetaHuman base: nude (underwear), armour and hair come from the paperdoll. */
	MetaHuman = 1,
};

/** How a held prop sits on an external body (see UValhallaVisuals::AttachHeldProp). */
enum class EValhallaGrip : uint8
{
	/** Sword, mace, dagger, totem: right fist, blade forward-down, flat outward. */
	OneHand,
	/** Staff: right fist, shaft upright, head up. */
	Staff,
	/** Bow: left fist, limbs upright, belly forward. */
	Bow,
	/** Shield: on the left forearm, face outward, top up. */
	Shield,
};

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
	/** Per-weapon auto-attacks (2026-09-23). `Attack` is the unarmed one. */
	AttackSword,
	AttackDagger,
	AttackMace,
	AttackStaff,
	/** A faster locomotion cycle, chosen over Walk by speed. */
	Jog,
	/** Static stance poses laid over everything by the hand layer. */
	PoseGripRight,
	PoseGripLeft,
	PoseShieldArm,
	/** A bow skill with a cast time: draw and hold at full draw, then loose. */
	BowDraw,
	BowRelease,
	/** A spell with a cast time: gather (looped while the bar runs), then push. */
	CastChannel,
	CastRelease,
	/** The same with a staff in hand; and the staff's instant cast. */
	CastChannelStaff,
	CastReleaseStaff,
	CastStaff,
	/** B-15 A-030 (2026-09-23): /sit (held on its last frame), the chat emotes, */
	Sit,
	EmoteWave,
	EmoteCheer,
	EmoteBow,
	/** the two-handed swing (weaponStyle "greatsword"), and the defender's */
	Attack2H,
	/** reaction to a blocked or dodged blow. */
	Block,
	Dodge,
};

/** One past the last EValhallaAnim, for tables indexed by it. */
constexpr int32 ValhallaAnimCount = static_cast<int32>(EValhallaAnim::Dodge) + 1;

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

	/** `A_Idle` … `A_Death`, by logical name, for the active body profile's skeleton. */
	static FString AnimPath(EValhallaAnim Anim);

	// ── Body profiles (B-15, 2026-09-23) ────────────────────────────────
	//
	// Kevin's call: a MetaHuman base body — a nude (underwear) MetaHuman that
	// wears the game's own armour and hair once those are rebuilt for its
	// skeleton. Its clips were retargeted from Epic's Paragon: Gideon
	// (Saved/ClaudeOps/mh_retarget.py); Gideon itself was removed as a body
	// and archived outside the repo on 2026-09-23.
	//
	// `valhalla.Visual.BodyProfile` picks the body: 0 legacy, 1 MetaHuman
	// (default). It is read when a character is constructed and
	// initialised, so change it before starting PIE (the editor's class
	// defaults pick it up on the next editor start).

	static EValhallaBodyProfile ActiveBodyProfile();

	/** True for any profile other than Legacy: a body with its own skeleton, props hung from calibrated bone grips. */
	static bool UseExternalBody() { return ActiveBodyProfile() != EValhallaBodyProfile::Legacy; }

	/** The legacy Valhalla body (B-15 Wave 2 glTF import). */
	static FString LegacyBodyMeshPath() { return TEXT("/Game/Valhalla/Characters/Body/SK_Valhalla_Body"); }

	/**
	 * The MetaHuman base, assembled with the Optimized/Medium pipeline
	 * (Saved/ClaudeOps/mh_build.py): a neck-down body (8.6k verts at LOD0, 3
	 * LODs) and a separate face (4.1k verts, 3 LODs) with baked materials.
	 */
	static FString MetaHumanBodyMeshPath() { return TEXT("/Game/Valhalla/Characters/MetaHuman/Build/Medium/MHC_ValhallaBase/Body/SKM_MHC_ValhallaBase_BodyMesh"); }
	static FString MetaHumanHeadMeshPath() { return TEXT("/Game/Valhalla/Characters/MetaHuman/Build/Medium/MHC_ValhallaBase/Face/SKM_MHC_ValhallaBase_FaceMesh"); }

	/** The separate head the active body needs, or empty when the body mesh includes its own. */
	static FString ActiveHeadMeshPath() { return ActiveBodyProfile() == EValhallaBodyProfile::MetaHuman ? MetaHumanHeadMeshPath() : FString(); }

	/**
	 * Puts the active profile's head (if it has a separate one) on a follower
	 * component attached to the body, following it by leader pose — the face
	 * skeleton shares the body's bone names down to `head`, and its facial
	 * joints hold their neutral pose. Clears the component otherwise.
	 */
	static bool ApplyActiveHead(USkeletalMeshComponent* Head, USkeletalMeshComponent* Body);

	// ── Skinned art per body (B-15 Wave 2 rework, 2026-09-23) ───────────
	//
	// Armour and hair are skinned to one skeleton each: the Fable originals
	// live under Characters/Equipment and Characters/Hair (SK_Valhalla_Skeleton),
	// their MetaHuman rebuilds under Characters/MetaHuman/Equipment and
	// Characters/MetaHuman/Hair (metahuman_base_skel), with the same names.

	/** Root of the active body's skinned equipment and hair folders. */
	static FString SkinnedArtRoot()
	{
		return ActiveBodyProfile() == EValhallaBodyProfile::MetaHuman
			? FString(TEXT("/Game/Valhalla/Characters/MetaHuman"))
			: FString(CharactersRoot());
	}

	/** `<root>/Hair/<Name>` for the active body. */
	static FString HairMeshPath(const TCHAR* Name) { return FString::Printf(TEXT("%s/Hair/%s"), *SkinnedArtRoot(), Name); }

	/** `<root>/Equipment/<Name>` for the active body. */
	static FString EquipmentMeshPath(const TCHAR* Name) { return FString::Printf(TEXT("%s/Equipment/%s"), *SkinnedArtRoot(), Name); }

	/**
	 * A piece picked for the other body — an NPC Blueprint authored before the
	 * switch still names Characters/Equipment/SK_chest_… — swapped for its
	 * same-named rebuild for the active body when there is one; otherwise the
	 * piece itself (CanFollowBody then decides whether it is drawn).
	 */
	static USkeletalMesh* PieceForActiveBody(USkeletalMesh* Piece);

	/**
	 * The path PieceForActiveBody would swap a piece at `Path` to (package path,
	 * no object name), or `Path` unchanged when it needs no swap. B-27: the
	 * preloader loads the swapped piece, not the one the Blueprint names.
	 */
	static FString PiecePathForActiveBody(const FString& Path);

	/**
	 * The body mesh the active profile uses. Constructors load this (not a
	 * hard-coded path) so a placed or previewed character in the editor shows
	 * the same body PIE does.
	 */
	static FString ActiveBodyMeshPath();

	/**
	 * Uniform scale the active body is drawn at. Each external body is scaled
	 * to the 122 cm character height (ArtBible §2): the MetaHuman base is
	 * 180.3 cm (122 / 180.3 = 0.677). 1 for the legacy body.
	 */
	static float ActiveBodyScale();

	/**
	 * Puts the active profile's mesh and scale on a character's body. Call
	 * from PostInitializeComponents, before the anim component starts.
	 * Returns true when an external body is in use.
	 */
	static bool ApplyActiveBody(USkeletalMeshComponent* Body, float ExtraScale = 1.f);

	/**
	 * True when a skinned follower (armour piece, hair) can follow this body:
	 * it must be weighted to the same skeleton. Pieces built for another rig
	 * are left off rather than drawn exploded.
	 */
	static bool CanFollowBody(const USkeletalMesh* Piece, const USkeletalMeshComponent* Body);

	/**
	 * Re-hangs a held prop (weapon or offhand) on the external body's hand or
	 * forearm bone with the grip calibrated for that body (gid_calib.py on the
	 * live idle pose), at a scale that undoes the body's own. Returns false
	 * (and does nothing) for the legacy body, whose sockets are used instead.
	 */
	static bool AttachHeldProp(UStaticMeshComponent* Prop, USkeletalMeshComponent* Body, EValhallaGrip Grip);

	/** The grip a weapon-slot item uses: its `weaponStyle` decides (bow, staff, else one-handed). */
	static EValhallaGrip GripForWeapon(const UObject* WorldContext, FName WeaponItemId);

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
	 * The auto-attack a held weapon plays, by `weaponStyle`: sword and
	 * greatsword slash, dagger stabs, mace (and totem) chops overhead, staff
	 * thrusts two-handed, bow draws and looses (`Shoot`); nothing held punches.
	 */
	static EValhallaAnim AttackAnimForWeapon(const UObject* WorldContext, FName WeaponItemId);

	/**
	 * Root-motion speed of the walk and jog clips on the external body,
	 * unscaled cm/s (measured on the retargeted MH_MF_Unarmed_* clips). The
	 * anim component divides the owner's speed by these x the body's scale to
	 * play the cycle at the rate that keeps the feet planted.
	 */
	static constexpr float WalkClipSpeed = 290.f;
	static constexpr float JogClipSpeed = 581.f;

	/**
	 * The same for the legacy body's A_Walk and A_Run (its jog slot), from
	 * how Blender assets/scripts/wave2_anims.py keys them: the planted foot
	 * travels 2 x reach in stance x cycle seconds — A_Walk 0.34 m in 0.30 s,
	 * A_Run 0.52 m in 0.187 s.
	 */
	static constexpr float LegacyWalkClipSpeed = 113.3f;
	static constexpr float LegacyJogClipSpeed = 278.6f;

	/** Unscaled cm/s of the active body's walk (Walk) or jog (Jog) cycle. */
	static float LocomotionClipSpeed(EValhallaAnim Cycle);

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
