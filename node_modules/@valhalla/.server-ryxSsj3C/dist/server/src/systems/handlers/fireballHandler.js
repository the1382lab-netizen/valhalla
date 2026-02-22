/**
 * Fireball effect handler.
 *
 * Instead of dealing damage immediately, this spawns a SpellProjectileState
 * that travels toward the ground target. The SpellProjectileSystem handles
 * movement, collision, and AoE detonation each tick.
 */
import { SkillId, FIREBALL_AOE_RADIUS, PLAYER_COLLISION_RADIUS, } from '@valhalla/shared';
import { SpellProjectileState } from '../../schema/SpellProjectileState.js';
import { registerEffectHandler, } from './registry.js';
import { getStatValue } from './utils.js';
let _nextSpellProjId = 0;
function fireballHandler(caster, _target, skill, _allPlayers, _now, ctx) {
    if (!ctx?.spellProjectiles || ctx.groundX == null || ctx.groundY == null) {
        return [];
    }
    const [min, max] = skill.baseDamage;
    const rawDamage = min + Math.random() * (max - min);
    const intel = getStatValue(caster, skill.scalingStat);
    const scaledDamage = rawDamage + intel * 0.8;
    // Use projectile metadata from skill template, falling back to constants
    const projSpeed = skill.projectile?.speed ?? 350;
    const projRadius = skill.projectile?.radius ?? 10;
    const spawnOffset = PLAYER_COLLISION_RADIUS + projRadius + 2;
    const angle = Math.atan2(ctx.groundY - caster.y, ctx.groundX - caster.x);
    const proj = new SpellProjectileState();
    proj.id = `spell_${_nextSpellProjId++}`;
    proj.ownerId = caster.id;
    proj.skillId = skill.id;
    proj.x = caster.x + Math.cos(angle) * spawnOffset;
    proj.y = caster.y + Math.sin(angle) * spawnOffset;
    proj.targetX = ctx.groundX;
    proj.targetY = ctx.groundY;
    proj.speed = projSpeed;
    proj._damage = scaledDamage;
    proj._aoeRadius = skill.aoeRadius ?? FIREBALL_AOE_RADIUS;
    proj._critChance = caster.stats?.critChance ?? 0.05;
    proj._critDamage = caster.stats?.critDamage ?? 0.5;
    proj._attackerDex = caster.stats?.dexterity ?? 10;
    proj._distanceTravelled = 0;
    proj._zoneId = caster.zoneId ?? '';
    ctx.spellProjectiles.set(proj.id, proj);
    return [];
}
registerEffectHandler(SkillId.WIZARD_FIREBALL, fireballHandler);
//# sourceMappingURL=fireballHandler.js.map