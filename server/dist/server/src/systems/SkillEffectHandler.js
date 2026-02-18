/**
 * Extensible skill effect system.
 *
 * Each skill can register a custom EffectHandler. If none is registered,
 * the defaultEffect() handles standard damage/healing/buff patterns.
 * This gives us hook points to add unique per-skill mechanics later
 * without refactoring the core casting engine.
 */
import { SkillId, SkillCategory, FIREBALL_PROJECTILE_SPEED, FIREBALL_AOE_RADIUS, FIREBALL_PROJECTILE_RADIUS, PLAYER_COLLISION_RADIUS, } from '@valhalla/shared';
import { SpellProjectileState } from '../schema/SpellProjectileState.js';
// ── Handler Registry ───────────────────────────────────────
/**
 * Custom per-skill handlers. Register specific skill logic here.
 * If a skill has no registered handler, defaultEffect() is used.
 */
const EFFECT_HANDLERS = {};
/**
 * Register a custom effect handler for a skill.
 */
export function registerEffectHandler(skillId, handler) {
    EFFECT_HANDLERS[skillId] = handler;
}
// ── Execute Effect ─────────────────────────────────────────
/**
 * Execute the effect of a completed skill cast.
 * Looks up a custom handler first, then falls back to default.
 */
export function executeSkillEffect(caster, target, skill, allPlayers, now, ctx) {
    const handler = EFFECT_HANDLERS[skill.id];
    if (handler) {
        return handler(caster, target, skill, allPlayers, now, ctx);
    }
    return defaultEffect(caster, target, skill, allPlayers, now, ctx);
}
// ── Default Effect Handler ─────────────────────────────────
function defaultEffect(caster, target, skill, allPlayers, now, _ctx) {
    const events = [];
    // ── Damage skills ──
    if (skill.baseDamage) {
        const targets = getAffectedTargets(caster, target, skill, allPlayers);
        for (const t of targets) {
            if (!t.alive)
                continue;
            const [min, max] = skill.baseDamage;
            const rawDamage = min + Math.random() * (max - min);
            // Scale with primary stat
            const statValue = getStatValue(caster, skill.scalingStat);
            const scaledDamage = rawDamage + statValue * 0.8;
            // Simple crit check
            const isCrit = Math.random() < (caster.stats?.critChance ?? 0.05);
            const critMult = isCrit ? 1 + (caster.stats?.critDamage ?? 0.5) : 1;
            const finalDamage = Math.round(scaledDamage * critMult);
            t.hp = Math.max(0, t.hp - finalDamage);
            if (t.hp <= 0) {
                t.alive = false;
            }
            events.push({ type: 'damage', targetId: t.id, damage: finalDamage, isCrit });
            // Apply DoT if present
            if (skill.dotDamagePerSec && skill.buffDurationMs) {
                applyBuff(t, {
                    skillId: skill.id,
                    casterId: caster.id,
                    appliedAt: now,
                    expiresAt: now + skill.buffDurationMs,
                    dotDamagePerSec: skill.dotDamagePerSec,
                });
                events.push({ type: 'debuff', targetId: t.id, skillId: skill.id, durationMs: skill.buffDurationMs });
            }
        }
    }
    // ── Healing skills ──
    if (skill.baseHealing) {
        const healTarget = target ?? caster;
        if (healTarget.alive) {
            const [min, max] = skill.baseHealing;
            const rawHeal = min + Math.random() * (max - min);
            const statValue = getStatValue(caster, skill.scalingStat);
            const scaledHeal = rawHeal + statValue * 0.9;
            const finalHeal = Math.round(scaledHeal);
            healTarget.hp = Math.min(healTarget.maxHp, healTarget.hp + finalHeal);
            events.push({ type: 'heal', targetId: healTarget.id, amount: finalHeal });
        }
        // Apply HoT if present
        if (skill.hotHealPerSec && skill.buffDurationMs) {
            applyBuff(healTarget, {
                skillId: skill.id,
                casterId: caster.id,
                appliedAt: now,
                expiresAt: now + skill.buffDurationMs,
                hotHealPerSec: skill.hotHealPerSec,
            });
            events.push({ type: 'buff', targetId: healTarget.id, skillId: skill.id, durationMs: skill.buffDurationMs });
        }
    }
    // ── Buff/debuff-only skills (no damage or healing) ──
    if (!skill.baseDamage && !skill.baseHealing && skill.buffDurationMs) {
        const buffTarget = skill.category === SkillCategory.DEBUFF
            ? (target ?? caster)
            : (skill.targetType === 'self' ? caster : (target ?? caster));
        applyBuff(buffTarget, {
            skillId: skill.id,
            casterId: caster.id,
            appliedAt: now,
            expiresAt: now + skill.buffDurationMs,
        });
        const eventType = skill.category === SkillCategory.DEBUFF ? 'debuff' : 'buff';
        events.push({ type: eventType, targetId: buffTarget.id, skillId: skill.id, durationMs: skill.buffDurationMs });
    }
    return events;
}
// ── Helpers ────────────────────────────────────────────────
function getStatValue(player, statName) {
    if (!player.stats)
        return 0;
    return player.stats[statName] ?? 0;
}
function applyBuff(player, buff) {
    // Remove existing buff of same skill from same caster (refresh)
    player.activeBuffs = player.activeBuffs.filter(b => !(b.skillId === buff.skillId && b.casterId === buff.casterId));
    player.activeBuffs.push(buff);
}
/**
 * Determine which players are affected by a skill based on target type.
 */
function getAffectedTargets(caster, target, skill, allPlayers) {
    switch (skill.targetType) {
        case 'singleEnemy':
            return target && target.id !== caster.id ? [target] : [];
        case 'aoeSelf': {
            // All enemies within skill.range of caster
            const targets = [];
            allPlayers.forEach(p => {
                if (p.id === caster.id || !p.alive || p.zoneId !== caster.zoneId)
                    return;
                const dx = p.x - caster.x;
                const dy = p.y - caster.y;
                if (dx * dx + dy * dy <= skill.range * skill.range) {
                    targets.push(p);
                }
            });
            return targets;
        }
        case 'cone': {
            // Enemies in a cone in front of caster
            const targets = [];
            const coneHalfAngle = Math.PI / 4; // 45° half-cone
            allPlayers.forEach(p => {
                if (p.id === caster.id || !p.alive || p.zoneId !== caster.zoneId)
                    return;
                const dx = p.x - caster.x;
                const dy = p.y - caster.y;
                const distSq = dx * dx + dy * dy;
                if (distSq > skill.range * skill.range)
                    return;
                const angleToTarget = Math.atan2(dy, dx);
                let angleDiff = Math.abs(angleToTarget - caster.aimAngle);
                if (angleDiff > Math.PI)
                    angleDiff = 2 * Math.PI - angleDiff;
                if (angleDiff <= coneHalfAngle) {
                    targets.push(p);
                }
            });
            return targets;
        }
        case 'aoeGround':
            // Ground-targeted AoE — handled by custom per-skill handlers that spawn spell projectiles.
            // The default effect handler doesn't apply damage here; see e.g. fireballHandler below.
            return [];
        default:
            return target ? [target] : [];
    }
}
// ── Fireball Handler ───────────────────────────────────────
let _nextSpellProjId = 0;
/**
 * Fireball effect handler.
 *
 * Instead of dealing damage immediately, this spawns a SpellProjectileState
 * that travels toward the ground target. The SpellProjectileSystem handles
 * movement, collision, and AoE detonation each tick.
 */
function fireballHandler(caster, _target, skill, _allPlayers, _now, ctx) {
    if (!ctx?.spellProjectiles || ctx.groundX == null || ctx.groundY == null) {
        // Fallback: no projectile map or no target position — skip silently
        return [];
    }
    // Roll damage (will be applied at detonation)
    const [min, max] = skill.baseDamage;
    const rawDamage = min + Math.random() * (max - min);
    const intel = getStatValue(caster, skill.scalingStat);
    const scaledDamage = rawDamage + intel * 0.8;
    // Spawn offset: start the fireball just ahead of the caster
    const spawnOffset = PLAYER_COLLISION_RADIUS + FIREBALL_PROJECTILE_RADIUS + 2;
    const angle = Math.atan2(ctx.groundY - caster.y, ctx.groundX - caster.x);
    const proj = new SpellProjectileState();
    proj.id = `spell_${_nextSpellProjId++}`;
    proj.ownerId = caster.id;
    proj.skillId = skill.id;
    proj.x = caster.x + Math.cos(angle) * spawnOffset;
    proj.y = caster.y + Math.sin(angle) * spawnOffset;
    proj.targetX = ctx.groundX;
    proj.targetY = ctx.groundY;
    proj.speed = FIREBALL_PROJECTILE_SPEED;
    // Server-only payload for detonation
    proj._damage = scaledDamage;
    proj._aoeRadius = FIREBALL_AOE_RADIUS;
    proj._critChance = caster.stats?.critChance ?? 0.05;
    proj._critDamage = caster.stats?.critDamage ?? 0.5;
    proj._attackerDex = caster.stats?.dexterity ?? 10;
    proj._distanceTravelled = 0;
    proj._zoneId = caster.zoneId ?? '';
    ctx.spellProjectiles.set(proj.id, proj);
    // Return no immediate skill events — damage fires on detonation
    return [];
}
// Register the Fireball handler
registerEffectHandler(SkillId.WIZARD_FIREBALL, fireballHandler);
//# sourceMappingURL=SkillEffectHandler.js.map