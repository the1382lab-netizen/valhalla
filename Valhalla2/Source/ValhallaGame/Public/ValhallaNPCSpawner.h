// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ValhallaNPCPatrolRules.h"
#include "ValhallaNPCSpawner.generated.h"

class AValhallaNPC;
class UArrowComponent;
class UBillboardComponent;
class USkeletalMeshComponent;
class UValhallaPatrolRouteComponent;

/**
 * A placeable NPC spawn point: one spawn point, one NPC.
 *
 * Drop it into a zone's gameplay sublevel (`L_<Zone>_Gameplay`) from the Place
 * Actors panel, pick an **NPC Type** (a `BP_NPC_*` Blueprint from
 * `/Game/Valhalla/NPCs`), and it spawns one instance of that type standing on
 * the ground where the spawn point is, facing where its arrow points. The
 * editor viewport shows a preview of the NPC so the level reads as it will
 * play.
 *
 * **Respawn is EverQuest-style.** When its NPC dies the countdown starts: the
 * corpse lies for CorpseSeconds, then decays, and when the respawn time is up a
 * fresh instance of the type spawns. While its NPC is alive nothing happens.
 * The respawn time is the template's `respawnMs` (set per NPC template in the
 * web editor) unless this spawn point overrides it with RespawnSeconds.
 *
 * Server-only in every meaningful sense: it spawns nothing on a client and is
 * not itself replicated. The NPC it makes is.
 *
 * **Idle movement (B-10 part 2).** Left alone, the NPC stands on its spot. A
 * spawn point can instead give it a patrol route (Patrol Points, walked Loop
 * or PingPong with a pause at each stop), make it follow another spawn point's
 * NPC (a pair walking together), or let it roam a radius round its spot. Only
 * one of the three applies: a leader beats a route, a route beats roaming.
 * All of it is the NPC's *idle* layer: a fight, the leash and the walk back
 * work exactly as before, and afterwards it picks the route up again at the
 * stop it was heading for. The editor viewport draws the route, the link to a
 * leader and the roam circle.
 *
 * **Rare spawns.** Each spawn rolls Rare Chance once; on a hit this cycle
 * spawns the Rare NPC Type / Rare Template instead (EverQuest's placeholder
 * spawn: Castellan Vane in the lieutenant's chair).
 *
 * Build > Map Check lists every spawn point with no NPC type, an unknown
 * template, its NPC's capsule stuck inside level geometry, or a patrol, pair
 * or rare setting that cannot work (the same list `valhalla.CheckZones` logs
 * in a running game).
 */
UCLASS(Blueprintable, meta = (DisplayName = "NPC Spawn Point"))
class VALHALLAGAME_API AValhallaNPCSpawner : public AActor
{
	GENERATED_BODY()

public:
	AValhallaNPCSpawner();

	//~ Begin AActor interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
#if WITH_EDITOR
	virtual void PostLoad() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void CheckForErrors() override;
#endif
	//~ End AActor interface

	// ── What to spawn ───────────────────────────────────────────────────

	/**
	 * The NPC type: a Blueprint of AValhallaNPC, e.g. `BP_NPC_Orc`. Its Class
	 * Defaults name the npc-templates.json entry it plays and hold its look.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Spawn", meta = (DisplayName = "NPC Type"))
	TSubclassOf<AValhallaNPC> NPCClass;

	/**
	 * Play a different npc-templates.json entry than the NPC type's own — the
	 * same orc body as a level 5 "Orc Captain", say. Empty uses the type's
	 * Default Template Id.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Spawn", meta = (DisplayName = "Template Override", GetOptions = "GetNPCTemplateOptions"))
	FName TemplateId;

	/** A name for this one NPC ("Bjorn the Trader"), or empty for the type's. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Spawn", meta = (DisplayName = "Name Override"))
	FString DebugLabel;

	// ── Respawn ─────────────────────────────────────────────────────────

	/**
	 * Seconds from this NPC's death to the next one spawning. 0 uses the
	 * template's `respawnMs`, which the web editor sets per NPC template.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Spawn", meta = (DisplayName = "Respawn Time Override", ClampMin = "0.0", Units = "Seconds"))
	float RespawnSeconds = 0.f;

	/** Off for a one-time spawn (the admin API's "Spawn NPC here"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Spawn")
	bool bRespawn = true;

	/**
	 * How long the body lies before it decays, seconds. Always gone before the
	 * next spawn. Loot is a separate bag and is not affected.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Spawn", meta = (ClampMin = "0.5", Units = "Seconds"))
	float CorpseSeconds = 30.f;

	// ── Patrol (B-10 part 2) ────────────────────────────────────────────

	/**
	 * The patrol route after the spawn point itself. The spawn point is always
	 * stop 0 (where the NPC stands up); these are stops 1, 2, ... in order.
	 * Relative to the spawn point: drag the diamond handles in the viewport
	 * (select the spawn point) and keep them on the floor, like the spawn
	 * point. One point is "walk out there and back". Empty: no route.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Patrol", meta = (MakeEditWidget = true,
		ToolTip = "Patrol stops after the spawn point (which is always stop 0). Relative to the spawn point: select it and drag the diamond handles. One point = walk out and back. Empty = no route."))
	TArray<FVector> PatrolPoints;

	/**
	 * How the route is walked. Loop: 0, 1, ..., last, 0, 1, ... (round a camp).
	 * PingPong: 0, 1, ..., last, ..., 1, 0, 1, ... (back and forth along a road
	 * or a wall). None keeps the points but ignores them.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Patrol", meta = (
		ToolTip = "Loop: spawn point, 1, 2, ..., last, spawn point, ... PingPong: out to the last point and back the same way. None: ignore the points."))
	EValhallaPatrolMode PatrolMode = EValhallaPatrolMode::PingPong;

	/** Shortest pause at each stop (and between roams), seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Patrol", meta = (ClampMin = "0.0", Units = "Seconds",
		ToolTip = "Shortest pause at each patrol stop or roam goal. Each pause is a random time between the min and the max."))
	float PatrolPauseMinSeconds = 5.f;

	/** Longest pause at each stop (and between roams), seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Patrol", meta = (ClampMin = "0.0", Units = "Seconds",
		ToolTip = "Longest pause at each patrol stop or roam goal."))
	float PatrolPauseMaxSeconds = 10.f;

	/**
	 * Walking pace on the route, while roaming and while following: a
	 * fraction of the template's move speed. 0.5 is a walk; a fight is always
	 * at full speed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Patrol", meta = (ClampMin = "0.1", ClampMax = "1.0", UIMin = "0.1", UIMax = "1.0",
		ToolTip = "Walking pace on the route, roaming and following, as a fraction of the template's move speed (0.5 = a walk). Fights are always full speed."))
	float PatrolSpeedFraction = 0.5f;

	/**
	 * Pairs: this spawn point's NPC follows that spawn point's NPC, about
	 * 1.2 m behind it, stopping when it stops, instead of walking its own
	 * route or roaming. While the leader is fighting the follower holds where
	 * it is (it joins the fight through social aggro, set on the template);
	 * while the leader is dead it waits at its own spawn point. Must be in the
	 * same level as this one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Patrol", meta = (DisplayName = "Follow Spawn Point",
		ToolTip = "Pairs: this NPC walks about 1.2 m behind the NPC of the spawn point picked here, instead of its own route. If that NPC is dead it waits at its own spawn point. Same level only."))
	TObjectPtr<AValhallaNPCSpawner> FollowSpawner;

	/**
	 * Roaming: when idle it walks to random reachable points within this
	 * radius of its spawn point, pausing between walks. 0 = off. Ignored when
	 * it has a patrol route or a leader.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Patrol", meta = (ClampMin = "0.0", Units = "Centimeters",
		ToolTip = "Roaming: walk to random reachable points within this radius of the spawn point, pausing between walks. 0 = off. Ignored with a patrol route or a Follow Spawn Point."))
	float WanderRadius = 0.f;

	// ── Rare spawn ──────────────────────────────────────────────────────

	/**
	 * The rare NPC type. Empty uses the normal NPC Type (with the Rare
	 * Template below). Its look is its own, as for the normal type.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Rare", meta = (DisplayName = "Rare NPC Type",
		ToolTip = "The NPC type spawned on a rare roll. Empty = the normal NPC Type playing the Rare Template."))
	TSubclassOf<AValhallaNPC> RareNPCClass;

	/**
	 * The npc-templates.json entry the rare spawn plays. Empty uses the Rare
	 * NPC Type's own Default Template Id.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Rare", meta = (DisplayName = "Rare Template", GetOptions = "GetNPCTemplateOptions",
		ToolTip = "The npc-templates.json entry the rare spawn plays. Empty = the Rare NPC Type's Default Template Id."))
	FName RareTemplateId;

	/**
	 * Chance, 0-1, that a spawn is the rare one instead. Rolled once per spawn
	 * (the first and every respawn); the rare replaces the normal NPC for that
	 * cycle. The respawn time is still this spawn point's. 0 = never.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|Rare", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0",
		ToolTip = "Chance (0-1) that a spawn is the rare NPC instead of the normal one. Rolled once per spawn; the respawn time stays this spawn point's. 0 = never."))
	float RareChance = 0.f;

	// ── Runtime (server) ────────────────────────────────────────────────

	/** The NPC this spawn point currently owns, alive or a corpse. May be null. */
	AValhallaNPC* GetSpawnedNPC() const { return SpawnedNPC.Get(); }

	/**
	 * The same, as an array. Kept for callers written when a spawner could own
	 * several NPCs (it no longer can).
	 */
	TArray<TWeakObjectPtr<AValhallaNPC>> GetSpawnedNPCs() const;

	/** The template id this spawn point actually plays: the override, else the type's. */
	FName GetEffectiveTemplateId() const;

	/** Respawn seconds after resolving the override against the template. */
	float GetEffectiveRespawnSeconds() const;

	/** Seconds until the next spawn, or 0 when none is pending. */
	float GetSecondsUntilRespawn() const;

	/** Called by AValhallaNPC::Die. Starts the corpse decay and the countdown. */
	void NotifyNPCDied(AValhallaNPC* Npc);

	/**
	 * The admin API's respawn / delete. Clears the corpse (or a live NPC when
	 * bForce) and either spawns a fresh one now or starts the countdown.
	 */
	void RespawnNow();
	void RemoveNPCAndScheduleRespawn();

	/** npc-templates.json ids, for the Template Override dropdown. */
	UFUNCTION()
	TArray<FString> GetNPCTemplateOptions() const;

	// ── Patrol / rare queries (B-10 part 2) ─────────────────────────────

	/** Follow Spawn Point is set to another spawn point: the pair rule wins over route and roaming. */
	bool IsFollowing() const;

	/** The patrol route drives the NPC: a mode, at least one point, and not following. */
	bool HasPatrolRoute() const;

	/** WanderRadius when it applies (no route, no leader), else 0. */
	float GetEffectiveWanderRadius() const;

	/** The route's stops in world space, unprojected: the spawn point, then each Patrol Point. */
	void GetPatrolStopsWorld(TArray<FVector>& OutStops) const;

	/**
	 * What the NPC is handed at spawn. Each route stop is projected onto the
	 * nav mesh (the floor under a hand-placed point, within ~1 m); stop 0 is
	 * Home, the NPC's settled spawn position.
	 */
	FValhallaNPCPatrolSetup BuildPatrolSetup(const FVector& Home) const;

	/** The template a rare spawn plays: Rare Template, else the Rare NPC Type's default. None when there is no rare. */
	FName GetEffectiveRareTemplateId() const;

	/** Whether the NPC standing (or lying) here now is the rare one. */
	bool IsRareSpawned() const { return bRareSpawned; }

	/**
	 * Patrol, pair and rare settings that cannot work: route points off the
	 * nav mesh, a Follow Spawn Point that is this one or spawns nothing, a
	 * follow loop, Rare Chance without a rare template, an unknown rare
	 * template. One human-readable line each, without the actor's name.
	 * `valhalla.CheckZones` logs them and Map Check lists them.
	 */
	void CollectPatrolProblems(TArray<FString>& OutProblems) const;

protected:
	/** Spawn one instance of the type on the ground here. Server only. */
	void SpawnNPC();

	/** Timer body: remove the old corpse if it is still there, then spawn. */
	void HandleRespawnTimer();

	/**
	 * Where the capsule's centre goes: the floor under this spawn point plus
	 * the capsule half-height. The spawn point marks the NPC's *feet*, so
	 * pressing End in the editor to drop it onto the floor is all a designer
	 * has to do.
	 */
	FVector FindGroundedCapsuleCentre(float ScaledHalfHeight) const;

	/** The floor's Z under this spawn point, if a trace finds one. */
	bool TraceFloorZ(double& OutZ) const;

	/** Editor marker, hidden in game. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Spawn")
	TObjectPtr<UBillboardComponent> Marker;

#if WITH_EDITORONLY_DATA
	/** Which way the NPC will face. */
	UPROPERTY()
	TObjectPtr<UArrowComponent> FacingArrow;

	/** Editor-only preview of the NPC type, standing on the floor in its idle. */
	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> PreviewBody;

	/** The active body's separate head (MetaHuman face), a follower of PreviewBody. */
	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> PreviewHead;

	/** The type's armour, followers of PreviewBody. */
	UPROPERTY()
	TArray<TObjectPtr<USkeletalMeshComponent>> PreviewPieces;

	/** Rebuild the preview from NPCClass / TemplateId. */
	void RefreshPreview();

	/** B-10 part 2: the route, the link to a leader and the roam circle, drawn in the viewport. */
	UPROPERTY()
	TObjectPtr<UValhallaPatrolRouteComponent> RouteVisual;

	/** Push the patrol settings into RouteVisual. */
	void RefreshRouteVisual();
#endif

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<AValhallaNPC> SpawnedNPC;

	/** This cycle's NPC is the rare one. */
	bool bRareSpawned = false;

	FTimerHandle RespawnTimer;
};
