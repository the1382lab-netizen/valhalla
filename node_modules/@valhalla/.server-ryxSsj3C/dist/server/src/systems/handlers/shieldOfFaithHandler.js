/**
 * Shield of Faith effect handler + buff cleanup.
 *
 * Wraps the target player in an absorbing shield.
 * Shield HP = 30 + wisdom * 1.5 (rounds to nearest int).
 * Overwrites any existing shield on the target (refresh semantics).
 */
import { SkillId } from '@valhalla/shared';
import { registerEffectHandler, registerBuffCleanup, isNpcTarget, } from './registry.js';
import { getStatValue } from './utils.js';
import { applyBuff } from '../SkillEffectHandler.js';
function shieldOfFaithHandler(caster, target, skill, _allPlayers, now, _ctx) {
    const shieldTarget = (target && !isNpcTarget(target)) ? target : caster;
    if (!shieldTarget.alive)
        return [];
    const wisdom = getStatValue(caster, 'wisdom');
    const shieldAmount = Math.round(30 + wisdom * 1.5);
    shieldTarget.shieldHp = shieldAmount;
    applyBuff(shieldTarget, {
        skillId: skill.id,
        casterId: caster.id,
        appliedAt: now,
        expiresAt: now + (skill.buffDurationMs ?? 15000),
        stacks: 1,
    });
    return [{
            type: 'buff',
            targetId: shieldTarget.id,
            skillId: skill.id,
            durationMs: skill.buffDurationMs ?? 15000,
        }];
}
registerEffectHandler(SkillId.CLERIC_SHIELD_OF_FAITH, shieldOfFaithHandler);
// Cleanup: when the buff expires naturally, clear the shield HP
registerBuffCleanup(SkillId.CLERIC_SHIELD_OF_FAITH, (player) => {
    player.shieldHp = 0;
});
//# sourceMappingURL=shieldOfFaithHandler.js.map