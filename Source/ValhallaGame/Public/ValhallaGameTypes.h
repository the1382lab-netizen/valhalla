// Copyright Valhalla 2.0. All Rights Reserved.
//
// Gameplay-layer types that need a UWorld or a network connection to mean
// anything. Pure rules types stay in ValhallaCore/ValhallaTypes.h.

#pragma once

#include "CoreMinimal.h"
#include "ValhallaTypes.h"
#include "ValhallaGameTypes.generated.h"

class AActor;
class UWorld;

/**
 * One buff, debuff, DoT or HoT currently riding on a character or an NPC.
 *
 * Server-only: this never leaves the authority. Phase 2b adds a separate,
 * minimal replicated view on AValhallaNPC (SyncedBuffs) for the client's target
 * pane — the numbers below are combat inputs and clients must not see them.
 *
 * Mirrors `ActiveBuff` in server/src/schema/PlayerState.ts:6.
 */
USTRUCT(BlueprintType)
struct VALHALLAGAME_API FValhallaActiveBuff
{
	GENERATED_BODY()

	/** Skill that applied this effect — key into UValhallaDataSubsystem::FindSkill. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Buffs")
	FName SkillId;

	/**
	 * Who applied it. 1.0 keyed buffs on `casterId` (a Colyseus session id) so the
	 * same DoT from two rogues stacked separately; the identity of an actor is the
	 * closest 2.0 equivalent, and a weak pointer means a caster who disconnects
	 * mid-DoT does not keep their pawn alive.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Buffs")
	TWeakObjectPtr<AActor> Caster;

	/**
	 * Server time, in seconds, at which this effect was applied. This is the
	 * DoT/HoT tick clock's origin, exactly like 1.0's `appliedAt`.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Buffs")
	double AppliedAt = 0.0;

	/**
	 * Server time, in seconds, at which this effect drops off. Compared against
	 * AValhallaGameState::GetServerTime(), not FPlatformTime — the 1.0 server used
	 * Date.now() milliseconds; 2.0 uses the replicated world clock so the client
	 * can render a countdown from the same number.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Buffs")
	double ExpiresAt = 0.0;

	/**
	 * Server time of this buff's last DoT/HoT tick. 1.0 kept this in a separate
	 * `dotTickTracker` map keyed by "target:skill:caster"; carrying it on the buff
	 * itself makes the stale-entry bug NPCSystem.tickNpcBuffs:423 works around
	 * impossible, because the field dies with the buff.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Buffs")
	double LastTickAt = 0.0;

	/** SkillTemplate.dotDamagePerSec, already multiplied out for the caster. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Buffs")
	float DotDamagePerSec = 0.f;

	/** SkillTemplate.hotHealPerSec, already multiplied out for the caster. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Buffs")
	float HotHealPerSec = 0.f;

	/** Current stack count. Only grows past 1 when StackingMode is Stack. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Buffs")
	int32 Stacks = 1;
};

/**
 * The 1.0 broadcast message set, as one enum.
 *
 * GameRoom broadcast a different Colyseus message type per event
 * (GameRoom.ts:939-1141). Unreal has no message bus, so the same information
 * travels as one replicated struct with a kind tag: it is one multicast RPC
 * instead of fourteen, and the client log can print anything it does not have a
 * specific presentation for yet.
 *
 * The names are the 1.0 `MessageType` spellings on purpose — grepping the two
 * codebases for `npcDied` should find both ends of the same event.
 */
UENUM(BlueprintType)
enum class EValhallaCombatEventKind : uint8
{
	/** GameRoom MessageType.PLAYER_HIT — a player took damage. */
	PlayerHit		UMETA(DisplayName = "playerHit"),
	/** MessageType.NPC_HIT. */
	NpcHit			UMETA(DisplayName = "npcHit"),
	/** MessageType.MISSED — the attacker's hit roll failed. */
	Missed			UMETA(DisplayName = "missed"),
	/** MessageType.DODGED. */
	Dodged			UMETA(DisplayName = "dodged"),
	/** MessageType.BLOCKED — damage was halved, not negated. */
	Blocked			UMETA(DisplayName = "blocked"),
	/** MessageType.NPC_DIED — carries the xp reward in Amount. */
	NpcDied			UMETA(DisplayName = "npcDied"),
	/** MessageType.PLAYER_DIED. */
	PlayerDied		UMETA(DisplayName = "playerDied"),
	/** MessageType.PLAYER_RESPAWNED. */
	PlayerRespawned	UMETA(DisplayName = "playerRespawned"),
	/** MessageType.BUFF_APPLIED — Amount is the duration in ms. */
	BuffApplied		UMETA(DisplayName = "buffApplied"),
	/** MessageType.BUFF_REMOVED. */
	BuffRemoved		UMETA(DisplayName = "buffRemoved"),
	/** MessageType.XP_GAINED — caster-private. */
	XpGained		UMETA(DisplayName = "xpGained"),
	/** MessageType.LEVEL_UP — caster-private; Amount is the new level. */
	LevelUp			UMETA(DisplayName = "levelUp"),
	/** MessageType.SKILL_STARTED — Amount is castTimeMs. */
	SkillStarted	UMETA(DisplayName = "skillStarted"),
	/** MessageType.SKILL_EFFECT — a heal, or a generic effect with no better kind. */
	SkillEffect		UMETA(DisplayName = "skillEffect"),
	/** MessageType.SKILL_INTERRUPTED. */
	SkillInterrupted UMETA(DisplayName = "skillInterrupted"),
	/** MessageType.SKILL_FAILED — caster-private; Text carries the reason. */
	SkillFailed		UMETA(DisplayName = "skillFailed"),
	/** MessageType.SPELL_IMPACT — Location and Amount (radius) drive the VFX. */
	SpellImpact		UMETA(DisplayName = "spellImpact"),
	/** MessageType.AUTO_ATTACK_STARTED. */
	AutoAttackStarted UMETA(DisplayName = "autoAttackStarted"),
	/** MessageType.AUTO_ATTACK_STOPPED. */
	AutoAttackStopped UMETA(DisplayName = "autoAttackStopped"),
	/**
	 * MessageType.ZONE_CHANGE (types.ts:114) — a player walked through a
	 * portal. `Target` is the pawn, `Location` is where it came out and `Text`
	 * is "<fromZone> -> <toZone>".
	 *
	 * 1.0 sent this to the transitioning client alone, together with a full
	 * `MAP_DATA` payload, because that client had to rebuild its tile map from
	 * scratch. 2.0 has no tile map to rebuild — every zone is already streamed
	 * in — so the event carries no map and goes to everybody, which is what
	 * lets a bystander's client know that somebody left.
	 */
	ZoneChanged		UMETA(DisplayName = "zoneChange"),
};

/**
 * One thing that happened in combat, as the client is told about it.
 *
 * Kept deliberately small and flat: it crosses the wire on an unreliable
 * multicast several times a second in a fight, and a struct of pointers and
 * strings would cost far more than the information is worth. Nothing here is
 * authoritative — the client uses it to draw a floating number and write a
 * combat-log line, and the numbers it would need to predict an outcome
 * (defence, dodge rating) are not in it.
 */
USTRUCT(BlueprintType)
struct VALHALLAGAME_API FValhallaCombatEvent
{
	GENERATED_BODY()

	/** Which 1.0 message this is. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	EValhallaCombatEventKind Kind = EValhallaCombatEventKind::SkillEffect;

	/** Who it happened to — a character or an NPC. Null for world events. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	TObjectPtr<AActor> Target = nullptr;

	/** Who caused it. Null when nothing did (a buff expiring of old age). */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	TObjectPtr<AActor> Instigator = nullptr;

	/** The skill responsible, if any. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	FName SkillId;

	/** Damage, heal, xp, duration-ms, radius or level — see the kind's comment. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	float Amount = 0.f;

	/** The target's HP after the event, for nameplates that missed a delta. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	float RemainingHp = 0.f;

	/** Where it happened. Used by SpellImpact and by floating numbers on a corpse. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	FVector_NetQuantize Location = FVector::ZeroVector;

	/** True when the crit roll succeeded. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	bool bCrit = false;

	/** True when the block roll succeeded. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	bool bBlocked = false;

	/** True when the amount is a heal rather than damage. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	bool bHeal = false;

	/** Free text. Only SkillFailed uses it, and only to name the reason. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Combat")
	FString Text;
};

/**
 * Everything a skill handler is handed when its skill goes off.
 *
 * The port of `SkillEffectContext` (handlers/registry.ts:28) plus the arguments
 * 1.0 passed alongside it. It is a plain struct, not a UObject, and it is only
 * ever a stack temporary: a handler must not keep it.
 */
USTRUCT(BlueprintType)
struct VALHALLAGAME_API FValhallaSkillContext
{
	GENERATED_BODY()

	/** The AValhallaCharacter that cast the skill. Never null on a real cast. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	TObjectPtr<AActor> Caster = nullptr;

	/** The selected target, or null for self / ground-targeted skills. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	TObjectPtr<AActor> Target = nullptr;

	/**
	 * `ctx.groundX` / `ctx.groundY` — where the caster was aiming, in world
	 * space. Ground-targeted skills land here; everything else ignores it.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	FVector AimPoint = FVector::ZeroVector;

	/** AValhallaGameState::GetServerTime(), in seconds. 1.0's `now`, in its own units. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	double ServerTime = 0.0;

	/** The skill that fired. A copy, because the data tables may reload under us. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	FValhallaSkillTemplate Skill;

	/** The world the cast happened in. Handlers that spawn actors need it. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Skills")
	TObjectPtr<UWorld> World = nullptr;
};
