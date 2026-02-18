/**
 * Server-authoritative skill/spell casting system.
 *
 * Handles: cast validation, cast progression, interrupts,
 * cooldown management, energy regen, and buff/DoT/HoT ticking.
 *
 * Client sends intent → server validates → server executes.
 */
import { ResourceType, } from '@valhalla/shared';
import { executeSkillEffect } from './SkillEffectHandler.js';
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
     */
    tryStartCast(caster, skillId, targetId, allPlayers, now) {
        const skill = DataManager.instance.getSkill(skillId);
        if (!skill) {
            return [{ type: 'castFailed', casterId: caster.id, reason: 'Unknown skill' }];
        }
        // ── Validation pipeline ──
        const failReason = this.validateCast(caster, skill, targetId, allPlayers, now);
        if (failReason) {
            return [{ type: 'castFailed', casterId: caster.id, reason: failReason }];
        }
        // Resolve target
        const target = targetId ? allPlayers.get(targetId) ?? null : null;
        if (skill.castTimeMs === 0) {
            // ── Instant cast ──
            return this.executeInstantCast(caster, target, skill, allPlayers, now);
        }
        else {
            // ── Start channeled/cast-time cast ──
            return this.startTimedCast(caster, skill, targetId, now);
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
     */
    update(allPlayers, dt, now) {
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
                    const target = cast.targetId ? allPlayers.get(cast.targetId) ?? null : null;
                    const castEvents = this.completeCast(player, target, skill, allPlayers, now);
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
    // ── Private Methods ──────────────────────────────────────
    validateCast(caster, skill, targetId, allPlayers, now) {
        // Alive check
        if (!caster.alive)
            return 'You are dead';
        // Class check
        const classSkills = DataManager.instance.getClassSkills(caster.classId);
        if (!classSkills || !classSkills.includes(skill.id)) {
            return 'Your class cannot use this skill';
        }
        // Level check
        if (caster.level < skill.levelRequired) {
            return `Requires level ${skill.levelRequired}`;
        }
        // Already casting check
        if (this.activeCasts.has(caster.id)) {
            return 'Already casting';
        }
        // Cooldown check
        const cdExpiry = caster.skillCooldowns.get(skill.id);
        if (cdExpiry && now < cdExpiry) {
            const remaining = Math.ceil((cdExpiry - now) / 1000);
            return `On cooldown (${remaining}s)`;
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
        // Range check (for targeted skills)
        if (targetId && skill.range > 0) {
            const target = allPlayers.get(targetId);
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
    executeInstantCast(caster, target, skill, allPlayers, now) {
        // Deduct resource
        this.deductResource(caster, skill);
        // Start cooldown
        if (skill.cooldownMs > 0) {
            caster.skillCooldowns.set(skill.id, now + skill.cooldownMs);
        }
        // Execute effect
        const skillEvents = executeSkillEffect(caster, target, skill, allPlayers, now);
        return [
            { type: 'castStarted', casterId: caster.id, skillId: skill.id, castTimeMs: 0 },
            { type: 'castComplete', casterId: caster.id, skillId: skill.id, events: skillEvents },
        ];
    }
    startTimedCast(caster, skill, targetId, now) {
        // Store active cast
        this.activeCasts.set(caster.id, {
            playerId: caster.id,
            skillId: skill.id,
            targetId,
            startedAt: now,
            durationMs: skill.castTimeMs,
            startX: caster.x,
            startY: caster.y,
        });
        // Set synced casting state (drives client cast bar)
        caster.castingSkillId = skill.id;
        caster.castingStartedAt = now;
        caster.castingDurationMs = skill.castTimeMs;
        return [
            { type: 'castStarted', casterId: caster.id, skillId: skill.id, castTimeMs: skill.castTimeMs },
        ];
    }
    completeCast(caster, target, skill, allPlayers, now) {
        // Deduct resource
        this.deductResource(caster, skill);
        // Start cooldown
        if (skill.cooldownMs > 0) {
            caster.skillCooldowns.set(skill.id, now + skill.cooldownMs);
        }
        // Execute effect
        const skillEvents = executeSkillEffect(caster, target, skill, allPlayers, now);
        return [
            { type: 'castComplete', casterId: caster.id, skillId: skill.id, events: skillEvents },
        ];
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
                events.push({ type: 'buffExpired', targetId: player.id, skillId: buff.skillId });
                continue;
            }
            // Tick DoT
            if (buff.dotDamagePerSec && buff.dotDamagePerSec > 0) {
                const tickKey = `${player.id}:${buff.skillId}:${buff.casterId}`;
                const lastTick = this.dotTickTracker.get(tickKey) ?? buff.appliedAt;
                if (now - lastTick >= 1000) { // Tick once per second
                    const damage = Math.round(buff.dotDamagePerSec);
                    player.hp = Math.max(0, player.hp - damage);
                    if (player.hp <= 0)
                        player.alive = false;
                    this.dotTickTracker.set(tickKey, now);
                    events.push({ type: 'dotTick', targetId: player.id, skillId: buff.skillId, damage });
                }
            }
            // Tick HoT
            if (buff.hotHealPerSec && buff.hotHealPerSec > 0) {
                const tickKey = `${player.id}:hot:${buff.skillId}:${buff.casterId}`;
                const lastTick = this.dotTickTracker.get(tickKey) ?? buff.appliedAt;
                if (now - lastTick >= 1000) {
                    const heal = Math.round(buff.hotHealPerSec);
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