/**
 * Server-authoritative skill/spell casting system.
 *
 * Handles: cast validation, cast progression, interrupts,
 * cooldown management, energy regen, and buff/DoT/HoT ticking.
 *
 * Client sends intent → server validates → server executes.
 */
import { SkillId, ResourceType, MELEE_RANGE, PLAYER_COLLISION_RADIUS, INVULNERABILITY_MS, RESPAWN_TIME_MS, computeAutoAttackSpeed, computePhysicalDamage, BASE_MELEE_DAMAGE, BASE_RANGED_DAMAGE, rollHit, rollDodge, rollBlock, rollCrit, applyDefenseReduction, } from '@valhalla/shared';
import { applyShieldAbsorption } from '../schema/PlayerState.js';
import { executeSkillEffect } from './SkillEffectHandler.js';
import { getBuffCleanup } from './handlers/index.js';
import { DataManager } from './DataManager.js';
// ── SkillSystem ────────────────────────────────────────────
export class SkillSystem {
    constructor() {
        this.activeCasts = new Map();
        this.dotTickTracker = new Map(); // "buffKey" → last tick time
    }
    /**
     * Attempt to start casting a skill.
     * Validates all preconditions. Returns result events.
     *
     * @param groundX  World X of the ground target — required for AOE_GROUND skills
     * @param groundY  World Y of the ground target — required for AOE_GROUND skills
     * @param allNPCs  NPC map — enables single-target skills to target NPCs
     */
    tryStartCast(caster, skillId, targetId, allPlayers, now, spellProjectiles, groundX, groundY, allNPCs, npcDelegate, awardXPDelegate, isPartyMemberDelegate) {
        const skill = DataManager.instance.getSkill(skillId);
        if (!skill) {
            return [{ type: 'castFailed', casterId: caster.id, reason: 'Unknown skill' }];
        }
        // ── Validation pipeline ──
        const failReason = this.validateCast(caster, skill, targetId, allPlayers, now, groundX, groundY, allNPCs);
        if (failReason) {
            return [{ type: 'castFailed', casterId: caster.id, reason: failReason }];
        }
        // Resolve target (checks players first, then NPCs)
        const target = this.resolveTarget(targetId, allPlayers, allNPCs);
        const ctx = {
            spellProjectiles,
            groundX: groundX ?? null,
            groundY: groundY ?? null,
            allNPCs,
            damageNpc: npcDelegate ? (id, dmg, attId, t) => npcDelegate.damageNPC(id, dmg, attId, t) : undefined,
            tauntNpc: npcDelegate ? (id, pId, bonus) => npcDelegate.tauntNpc(id, pId, bonus) : undefined,
            awardXP: awardXPDelegate ?? undefined,
            isPartyMember: isPartyMemberDelegate,
        };
        if (skill.castTimeMs === 0) {
            // ── Instant cast ──
            return this.executeInstantCast(caster, target, skill, allPlayers, now, ctx);
        }
        else {
            // ── Start channeled/cast-time cast ──
            return this.startTimedCast(caster, skill, targetId, now, groundX ?? null, groundY ?? null);
        }
    }
    /**
     * Cancel an active cast (manual cancel or movement interrupt).
     */
    cancelCast(player, now) {
        const cast = this.activeCasts.get(player.id);
        if (!cast)
            return [];
        this.activeCasts.delete(player.id);
        player.castingSkillId = '';
        player.castingStartedAt = 0;
        player.castingDurationMs = 0;
        return [{ type: 'castInterrupted', casterId: player.id, skillId: cast.skillId }];
    }
    /**
     * Main update tick. Call every server frame.
     * - Completes casts that have finished their cast time
     * - Ticks energy regen for non-caster classes
     * - Ticks DoTs and HoTs
     * - Expires finished buffs
     * - Checks for movement interrupts
     *
     * @param spellProjectiles  Room's spell projectile map — passed to effect handlers that spawn projectiles
     * @param allNPCs           NPC map — allows timed skill completions to resolve NPC targets
     */
    update(allPlayers, dt, now, spellProjectiles, allNPCs, npcDelegate, awardXPDelegate, isPartyMemberDelegate) {
        const events = [];
        // ── Complete casts ──
        for (const [playerId, cast] of this.activeCasts) {
            const player = allPlayers.get(playerId);
            if (!player || !player.alive) {
                this.activeCasts.delete(playerId);
                player && this.clearCastingState(player);
                continue;
            }
            // Check movement interrupt
            const dx = player.x - cast.startX;
            const dy = player.y - cast.startY;
            if (dx * dx + dy * dy > 4) { // > 2px movement threshold
                this.activeCasts.delete(playerId);
                this.clearCastingState(player);
                events.push({ type: 'castInterrupted', casterId: playerId, skillId: cast.skillId });
                continue;
            }
            // Check if cast completed
            if (now >= cast.startedAt + cast.durationMs) {
                this.activeCasts.delete(playerId);
                this.clearCastingState(player);
                const skill = DataManager.instance.getSkill(cast.skillId);
                if (skill) {
                    const target = this.resolveTarget(cast.targetId, allPlayers, allNPCs);
                    // Re-validate single-target skills at fire time: the target may have
                    // died, disconnected, or been invalidated during the cast window.
                    if (skill.targetType === 'singleEnemy' || skill.targetType === 'singleAlly') {
                        if (!target || !target.alive) {
                            events.push({ type: 'castFailed', casterId: player.id, reason: 'Target is no longer valid' });
                            continue;
                        }
                        if (skill.targetType === 'singleAlly' && cast.targetId && !allPlayers.get(cast.targetId)) {
                            events.push({ type: 'castFailed', casterId: player.id, reason: 'Invalid target' });
                            continue;
                        }
                        if (skill.targetType === 'singleEnemy' && cast.targetId && allPlayers.get(cast.targetId)) {
                            events.push({ type: 'castFailed', casterId: player.id, reason: 'Invalid target' });
                            continue;
                        }
                    }
                    const ctx = {
                        spellProjectiles,
                        groundX: cast.groundX,
                        groundY: cast.groundY,
                        allNPCs,
                        damageNpc: npcDelegate ? (id, dmg, attId, t) => npcDelegate.damageNPC(id, dmg, attId, t) : undefined,
                        tauntNpc: npcDelegate ? (id, pId, bonus) => npcDelegate.tauntNpc(id, pId, bonus) : undefined,
                        awardXP: awardXPDelegate ?? undefined,
                        isPartyMember: isPartyMemberDelegate,
                    };
                    const castEvents = this.completeCast(player, target, skill, allPlayers, now, ctx);
                    events.push(...castEvents);
                }
            }
        }
        // ── Energy regen + buff ticking ──
        allPlayers.forEach(player => {
            if (!player.alive)
                return;
            // Energy regen for non-casters
            if (player.stats && player.maxEnergy > 0) {
                const regenAmount = player.stats.energyRegenRate * dt;
                player.energy = Math.min(player.maxEnergy, player.energy + regenAmount);
            }
            // Mana regen for casters (scales with Intelligence)
            if (player.stats && player.maxMana > 0 && player.stats.manaRegenRate > 0) {
                const regenAmount = player.stats.manaRegenRate * dt;
                player.mana = Math.min(player.maxMana, player.mana + regenAmount);
            }
            // Tick buffs (DoTs, HoTs, expirations)
            const buffEvents = this.tickBuffs(player, dt, now);
            events.push(...buffEvents);
        });
        return events;
    }
    // ── Auto-Attack System ───────────────────────────────────
    /**
     * Start auto-attacking a target.
     */
    startAutoAttack(player, skillId, targetId, allPlayers, allNPCs, now = Date.now()) {
        const events = [];
        const skill = DataManager.instance.getSkill(skillId);
        if (!skill || !skill.isAutoAttack) {
            return [{ type: 'castFailed', casterId: player.id, reason: 'Invalid auto-attack skill' }];
        }
        if (!player.alive) {
            return [{ type: 'castFailed', casterId: player.id, reason: 'You are dead' }];
        }
        // Class check: MELEE_ATTACK has classId null (all classes), RANGED_ATTACK has classId RANGER
        if (skill.classId !== null) {
            const classSkills = DataManager.instance.getClassSkills(player.classId);
            if (!classSkills.includes(skillId)) {
                return [{ type: 'castFailed', casterId: player.id, reason: 'Your class cannot use this skill' }];
            }
        }
        // Weapon requirement check (ranged attack requires a ranged weapon)
        if (skill.requiresWeapon) {
            const weaponId = player.equipWeapon;
            if (!weaponId) {
                return [{ type: 'castFailed', casterId: player.id, reason: 'Requires a weapon' }];
            }
            const weapon = DataManager.instance.getItem(weaponId);
            if (!weapon || !weapon.isRangedWeapon) {
                return [{ type: 'castFailed', casterId: player.id, reason: 'Requires a ranged weapon' }];
            }
        }
        // Validate target exists and is alive
        const target = this.resolveTarget(targetId, allPlayers, allNPCs);
        if (!target || !target.alive) {
            return [{ type: 'castFailed', casterId: player.id, reason: 'Invalid target' }];
        }
        // Cancel any active cast
        if (this.activeCasts.has(player.id)) {
            const castEvents = this.cancelCast(player, now);
            events.push(...castEvents);
        }
        // Set auto-attack state
        player.autoAttackActive = true;
        player.autoAttackSkillId = skillId;
        player.autoAttackTargetId = targetId;
        player.nextAutoAttackAt = now; // attack immediately on first activation
        events.push({
            type: 'autoAttackStarted',
            playerId: player.id,
            skillId,
            targetId,
        });
        return events;
    }
    /**
     * Stop auto-attacking.
     */
    stopAutoAttack(player) {
        if (!player.autoAttackActive)
            return [];
        player.autoAttackActive = false;
        player.autoAttackSkillId = '';
        player.autoAttackTargetId = '';
        player.nextAutoAttackAt = 0;
        return [{ type: 'autoAttackStopped', playerId: player.id }];
    }
    /**
     * Process all auto-attacking players. Called every server tick.
     */
    updateAutoAttacks(allPlayers, allNPCs, npcDelegate, now, isPartyMember, awardXPDelegate) {
        const events = [];
        allPlayers.forEach((player, _playerId) => {
            if (!player.autoAttackActive)
                return;
            if (!player.alive) {
                events.push(...this.stopAutoAttack(player));
                return;
            }
            const skill = DataManager.instance.getSkill(player.autoAttackSkillId);
            if (!skill) {
                events.push(...this.stopAutoAttack(player));
                return;
            }
            // Resolve target
            const target = this.resolveTarget(player.autoAttackTargetId, allPlayers, allNPCs);
            if (!target || !target.alive) {
                events.push(...this.stopAutoAttack(player));
                return;
            }
            // Check same zone
            if (target.zoneId !== player.zoneId) {
                events.push(...this.stopAutoAttack(player));
                return;
            }
            // Check range
            const dx = target.x - player.x;
            const dy = target.y - player.y;
            const dist = Math.sqrt(dx * dx + dy * dy);
            const maxRange = skill.id === SkillId.RANGED_ATTACK ? skill.range : MELEE_RANGE + PLAYER_COLLISION_RADIUS;
            if (dist > maxRange) {
                // Out of range — don't stop, but don't attack (player may move back in range)
                return;
            }
            // Pause auto-attack while the player is mid-cast.
            // Reset the timer so the attack fires immediately when the cast completes.
            if (player.castingSkillId) {
                player.nextAutoAttackAt = now;
                return;
            }
            // Check timing
            if (now < player.nextAutoAttackAt)
                return;
            // Compute attack speed
            const classTemplate = DataManager.instance.classes[player.classId];
            const stats = player.stats;
            const dex = stats?.dexterity ?? 10;
            let baseAttackSpeedMs;
            const weaponId = player.equipWeapon;
            const weapon = weaponId ? DataManager.instance.getItem(weaponId) : undefined;
            if (skill.id === SkillId.RANGED_ATTACK) {
                // Ranged: use weapon attack speed (weapon is required)
                baseAttackSpeedMs = weapon?.attackSpeedMs ?? classTemplate?.baseRangedAttackSpeedMs ?? 2000;
            }
            else {
                // Melee: use weapon speed if available, otherwise class base
                baseAttackSpeedMs = weapon?.attackSpeedMs ?? classTemplate?.baseMeleeAttackSpeedMs ?? 1800;
            }
            const effectiveSpeed = computeAutoAttackSpeed(baseAttackSpeedMs, dex);
            // Auto-face the target
            player.aimAngle = Math.atan2(dy, dx);
            // Include weapon's flat attack damage bonus in the base damage constant
            const weaponAttackDamage = weapon?.attackDamage ?? 0;
            // Execute the attack
            if (skill.id === SkillId.RANGED_ATTACK) {
                // ── Ranged instant-hit ──
                const rawDamage = computePhysicalDamage(stats?.strength ?? 10, BASE_RANGED_DAMAGE + weaponAttackDamage);
                const hitEvents = this.executeAutoAttackOnTarget(player, target, rawDamage, 'physical', dex, stats?.critChance ?? 0.05, stats?.critDamage ?? 0.5, now, allPlayers, allNPCs, npcDelegate, isPartyMember, awardXPDelegate);
                events.push(...hitEvents);
            }
            else {
                // ── Melee attack ──
                const rawDamage = computePhysicalDamage(stats?.strength ?? 10, BASE_MELEE_DAMAGE + weaponAttackDamage);
                const hitEvents = this.executeAutoAttackOnTarget(player, target, rawDamage, 'physical', dex, stats?.critChance ?? 0.05, stats?.critDamage ?? 0.5, now, allPlayers, allNPCs, npcDelegate, isPartyMember, awardXPDelegate);
                events.push(...hitEvents);
            }
            // Set next attack time
            player.nextAutoAttackAt = now + effectiveSpeed;
        });
        return events;
    }
    /**
     * Execute a single auto-attack hit against a target (player or NPC).
     */
    executeAutoAttackOnTarget(attacker, target, rawDamage, damageType, attackerDex, attackerCritChance, attackerCritDamage, now, allPlayers, allNPCs, npcDelegate, isPartyMember, awardXPDelegate) {
        const events = [];
        // Check if target is a player (has invulnerableUntil) or NPC
        const isPlayerTarget = allPlayers.get(target.id) !== undefined;
        if (isPlayerTarget) {
            const playerTarget = target;
            // Party check — no friendly fire
            if (isPartyMember?.(attacker.id, playerTarget.id))
                return events;
            // Invulnerability check
            if (now < playerTarget.invulnerableUntil)
                return events;
            // 1. Hit roll
            if (!rollHit(attackerDex)) {
                events.push({
                    type: 'autoAttackHit',
                    attackerId: attacker.id,
                    targetId: playerTarget.id,
                    damage: 0,
                    damageType,
                    isCrit: false,
                    isBlock: false,
                    isDodge: false,
                    isMiss: true,
                });
                return events;
            }
            // 2. Dodge roll
            const tStats = playerTarget.stats;
            if (rollDodge(tStats?.dodgeRating ?? 0)) {
                events.push({
                    type: 'autoAttackHit',
                    attackerId: attacker.id,
                    targetId: playerTarget.id,
                    damage: 0,
                    damageType,
                    isCrit: false,
                    isBlock: false,
                    isDodge: true,
                    isMiss: false,
                });
                return events;
            }
            // 3. Crit roll
            const crit = rollCrit(attackerCritChance, attackerCritDamage);
            let damage = rawDamage * crit.multiplier;
            // 4. Block roll
            let blocked = false;
            if (rollBlock(tStats?.blockRating ?? 0)) {
                damage *= 0.5;
                blocked = true;
            }
            // 5. Defense reduction
            const defense = damageType === 'physical'
                ? (tStats?.physicalDefense ?? 0)
                : (tStats?.spellResist ?? 0);
            damage = applyDefenseReduction(damage, defense);
            damage = Math.max(1, Math.floor(damage));
            // Apply shield absorption
            damage = applyShieldAbsorption(playerTarget, damage);
            // Apply damage
            playerTarget.hp -= damage;
            playerTarget.invulnerableUntil = now + INVULNERABILITY_MS;
            events.push({
                type: 'autoAttackHit',
                attackerId: attacker.id,
                targetId: playerTarget.id,
                damage,
                damageType,
                isCrit: crit.isCrit,
                isBlock: blocked,
                isDodge: false,
                isMiss: false,
            });
            // Death check
            if (playerTarget.hp <= 0) {
                playerTarget.hp = 0;
                playerTarget.alive = false;
                playerTarget.respawnAt = now + RESPAWN_TIME_MS;
                // Stop auto-attack on the now-dead target
                // (will be caught next tick by alive check)
            }
        }
        else {
            // NPC target
            if (!npcDelegate)
                return events;
            const npcTarget = target;
            const { died, xpReward } = npcDelegate.damageNPC(npcTarget.id, rawDamage, attacker.id, now);
            events.push({
                type: 'autoAttackHit',
                attackerId: attacker.id,
                targetId: npcTarget.id,
                damage: rawDamage,
                damageType,
                isCrit: false,
                isBlock: false,
                isDodge: false,
                isMiss: false,
                skillId: attacker.autoAttackSkillId,
                npcDied: died,
                xpReward: died ? xpReward : 0,
            });
            if (died && awardXPDelegate) {
                awardXPDelegate(attacker.id, xpReward);
            }
        }
        return events;
    }
    // ── Private Methods ──────────────────────────────────────
    validateCast(caster, skill, targetId, allPlayers, now, groundX, groundY, allNPCs) {
        // Alive check
        if (!caster.alive)
            return 'You are dead';
        // Class check — skills with classId: null are available to all classes
        if (skill.classId !== null) {
            const classSkills = DataManager.instance.getClassSkills(caster.classId);
            if (!classSkills || !classSkills.includes(skill.id)) {
                return 'Your class cannot use this skill';
            }
        }
        // Level check
        if (caster.level < skill.levelRequired) {
            return `Requires level ${skill.levelRequired}`;
        }
        // Already casting check
        if (this.activeCasts.has(caster.id)) {
            return 'Already casting';
        }
        // Cooldown check (includes cooldown group check)
        const cdExpiry = caster.skillCooldowns.get(skill.id);
        if (cdExpiry && now < cdExpiry) {
            const remaining = Math.ceil((cdExpiry - now) / 1000);
            return `On cooldown (${remaining}s)`;
        }
        // Also check if any skill in the same cooldown group is on cooldown
        if (skill.cooldownGroup) {
            const classSkills = DataManager.instance.getClassSkills(caster.classId);
            for (const otherSkillId of classSkills) {
                if (otherSkillId === skill.id)
                    continue;
                const otherSkill = DataManager.instance.getSkill(otherSkillId);
                if (otherSkill?.cooldownGroup === skill.cooldownGroup) {
                    const otherCd = caster.skillCooldowns.get(otherSkillId);
                    if (otherCd && now < otherCd) {
                        const remaining = Math.ceil((otherCd - now) / 1000);
                        return `On cooldown (${remaining}s) — shared with ${otherSkill.name}`;
                    }
                }
            }
        }
        // Resource check
        if (skill.resourceType === ResourceType.MANA) {
            if (caster.mana < skill.resourceCost)
                return 'Not enough mana';
        }
        else if (skill.resourceType === ResourceType.ENERGY) {
            if (caster.energy < skill.resourceCost)
                return 'Not enough energy';
        }
        // Range check for ground-targeted AoE skills
        if (skill.targetType === 'aoeGround') {
            if (groundX == null || groundY == null) {
                return 'No target position provided';
            }
            if (skill.range > 0) {
                const dx = groundX - caster.x;
                const dy = groundY - caster.y;
                if (dx * dx + dy * dy > skill.range * skill.range) {
                    return 'Out of range';
                }
            }
        }
        // Target requirement check for single-target skills
        if (skill.targetType === 'singleEnemy' || skill.targetType === 'singleAlly') {
            if (!targetId)
                return 'No target selected';
            const target = this.resolveTarget(targetId, allPlayers, allNPCs);
            if (!target)
                return 'Invalid target';
            if (!target.alive)
                return 'Target is dead';
            // singleAlly must resolve to a player (not an NPC)
            if (skill.targetType === 'singleAlly' && !allPlayers.get(targetId)) {
                return 'Invalid target';
            }
            // singleEnemy must resolve to an NPC (not a player)
            if (skill.targetType === 'singleEnemy' && allPlayers.get(targetId)) {
                return 'Invalid target';
            }
        }
        // Range check (for single-target skills)
        if (targetId && skill.range > 0 && skill.targetType !== 'aoeGround') {
            const target = this.resolveTarget(targetId, allPlayers, allNPCs);
            if (target) {
                const dx = target.x - caster.x;
                const dy = target.y - caster.y;
                if (dx * dx + dy * dy > skill.range * skill.range) {
                    return 'Out of range';
                }
                // Zone check
                if (target.zoneId !== caster.zoneId) {
                    return 'Target is in a different zone';
                }
            }
        }
        return null; // All checks passed
    }
    /**
     * Resolve a target ID against the player map and (optionally) the NPC map.
     * Returns the first matching entity, or null if not found.
     */
    resolveTarget(targetId, allPlayers, allNPCs) {
        if (!targetId)
            return null;
        const player = allPlayers.get(targetId);
        if (player)
            return player;
        const npc = allNPCs?.get(targetId);
        return npc ?? null;
    }
    executeInstantCast(caster, target, skill, allPlayers, now, ctx) {
        // Deduct resource
        this.deductResource(caster, skill);
        // Start cooldown (+ cooldown group)
        this.applyCooldown(caster, skill, now);
        // Execute effect
        const skillEvents = executeSkillEffect(caster, target, skill, allPlayers, now, ctx);
        return [
            { type: 'castStarted', casterId: caster.id, skillId: skill.id, castTimeMs: 0 },
            { type: 'castComplete', casterId: caster.id, skillId: skill.id, events: skillEvents },
        ];
    }
    startTimedCast(caster, skill, targetId, now, groundX = null, groundY = null) {
        // Store active cast (including ground target for AOE_GROUND spells)
        this.activeCasts.set(caster.id, {
            playerId: caster.id,
            skillId: skill.id,
            targetId,
            startedAt: now,
            durationMs: skill.castTimeMs,
            startX: caster.x,
            startY: caster.y,
            groundX,
            groundY,
        });
        // Set synced casting state (drives client cast bar)
        caster.castingSkillId = skill.id;
        caster.castingStartedAt = now;
        caster.castingDurationMs = skill.castTimeMs;
        return [
            { type: 'castStarted', casterId: caster.id, skillId: skill.id, castTimeMs: skill.castTimeMs },
        ];
    }
    completeCast(caster, target, skill, allPlayers, now, ctx) {
        // Deduct resource
        this.deductResource(caster, skill);
        // Start cooldown (+ cooldown group)
        this.applyCooldown(caster, skill, now);
        // Execute effect
        const skillEvents = executeSkillEffect(caster, target, skill, allPlayers, now, ctx);
        return [
            { type: 'castComplete', casterId: caster.id, skillId: skill.id, events: skillEvents },
        ];
    }
    /**
     * Apply cooldown for a skill. If the skill belongs to a cooldown group,
     * all other skills in that group also go on cooldown.
     */
    applyCooldown(caster, skill, now) {
        if (skill.cooldownMs <= 0)
            return;
        const cdExpiry = now + skill.cooldownMs;
        caster.skillCooldowns.set(skill.id, cdExpiry);
        // If this skill is in a cooldown group, put all group members on cooldown too
        if (skill.cooldownGroup) {
            const classSkills = DataManager.instance.getClassSkills(caster.classId);
            for (const otherSkillId of classSkills) {
                if (otherSkillId === skill.id)
                    continue;
                const otherSkill = DataManager.instance.getSkill(otherSkillId);
                if (otherSkill?.cooldownGroup === skill.cooldownGroup) {
                    // Use this skill's cooldown expiry for all group members
                    caster.skillCooldowns.set(otherSkillId, cdExpiry);
                }
            }
        }
    }
    deductResource(caster, skill) {
        if (skill.resourceType === ResourceType.MANA) {
            caster.mana = Math.max(0, caster.mana - skill.resourceCost);
        }
        else if (skill.resourceType === ResourceType.ENERGY) {
            caster.energy = Math.max(0, caster.energy - skill.resourceCost);
        }
    }
    clearCastingState(player) {
        player.castingSkillId = '';
        player.castingStartedAt = 0;
        player.castingDurationMs = 0;
    }
    tickBuffs(player, dt, now) {
        const events = [];
        const remaining = [];
        for (const buff of player.activeBuffs) {
            // Check expiration
            if (now >= buff.expiresAt) {
                // Run registered cleanup handler (if any) — replaces hardcoded per-skill if-else
                const cleanup = getBuffCleanup(buff.skillId);
                if (cleanup)
                    cleanup(player, buff);
                events.push({ type: 'buffExpired', targetId: player.id, skillId: buff.skillId });
                continue;
            }
            // Tick DoT (scaled by stacks)
            if (buff.dotDamagePerSec && buff.dotDamagePerSec > 0) {
                const tickKey = `${player.id}:${buff.skillId}:${buff.casterId}`;
                const lastTick = this.dotTickTracker.get(tickKey) ?? buff.appliedAt;
                if (now - lastTick >= 1000) {
                    const stacks = buff.stacks ?? 1;
                    const damage = Math.round(buff.dotDamagePerSec * stacks);
                    player.hp = Math.max(0, player.hp - damage);
                    if (player.hp <= 0)
                        player.alive = false;
                    this.dotTickTracker.set(tickKey, now);
                    events.push({ type: 'dotTick', targetId: player.id, skillId: buff.skillId, damage });
                }
            }
            // Tick HoT (scaled by stacks)
            if (buff.hotHealPerSec && buff.hotHealPerSec > 0) {
                const tickKey = `${player.id}:hot:${buff.skillId}:${buff.casterId}`;
                const lastTick = this.dotTickTracker.get(tickKey) ?? buff.appliedAt;
                if (now - lastTick >= 1000) {
                    const stacks = buff.stacks ?? 1;
                    const heal = Math.round(buff.hotHealPerSec * stacks);
                    player.hp = Math.min(player.maxHp, player.hp + heal);
                    this.dotTickTracker.set(tickKey, now);
                    events.push({ type: 'hotTick', targetId: player.id, skillId: buff.skillId, heal });
                }
            }
            remaining.push(buff);
        }
        player.activeBuffs = remaining;
        return events;
    }
}
//# sourceMappingURL=SkillSystem.js.map