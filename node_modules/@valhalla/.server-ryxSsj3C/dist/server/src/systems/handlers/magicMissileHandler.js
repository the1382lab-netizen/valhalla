/**
 * Magic Missile effect handler.
 *
 * Deals instant arcane damage to the selected single enemy target.
 * Unlike default attacks this spell ALWAYS hits — no dodge or miss roll.
 * Critical strikes still occur normally.
 */
import { SkillId } from '@valhalla/shared';
import { registerEffectHandler, isNpcTarget, } from './registry.js';
import { getStatValue } from './utils.js';
function magicMissileHandler(caster, target, skill, _allPlayers, now, ctx) {
    if (!target || !target.alive || target.id === caster.id)
        return [];
    const [min, max] = skill.baseDamage;
    const rawDamage = min + Math.random() * (max - min);
    const intel = getStatValue(caster, skill.scalingStat);
    const scaledDamage = rawDamage + intel * 0.8;
    const isCrit = Math.random() < (caster.stats?.critChance ?? 0.05);
    const critMult = isCrit ? 1 + (caster.stats?.critDamage ?? 0.5) : 1;
    const finalDamage = Math.round(scaledDamage * critMult);
    if (isNpcTarget(target) && ctx?.damageNpc) {
        const { xpReward } = ctx.damageNpc(target.id, finalDamage, caster.id, now);
        if (xpReward > 0) {
            ctx?.awardXP ? ctx.awardXP(caster.id, xpReward) : (caster.xp = (caster.xp ?? 0) + xpReward);
        }
    }
    else {
        target.hp = Math.max(0, target.hp - finalDamage);
        if (target.hp <= 0) {
            target.alive = false;
        }
    }
    return [{ type: 'damage', targetId: target.id, damage: finalDamage, isCrit }];
}
registerEffectHandler(SkillId.WIZARD_MAGIC_MISSILE, magicMissileHandler);
//# sourceMappingURL=magicMissileHandler.js.map