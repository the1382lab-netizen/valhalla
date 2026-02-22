/**
 * Handler barrel — imports all individual handler files for their side-effect
 * registrations, and re-exports the registry API.
 *
 * To add a new skill handler:
 * 1. Create a new file in this directory (e.g. mySkillHandler.ts)
 * 2. Import and call registerEffectHandler() and/or registerBuffCleanup()
 * 3. Add an import line below
 */
export { registerEffectHandler, registerBuffCleanup, getEffectHandler, getBuffCleanup, type EffectHandler, type BuffCleanupHandler, type SkillEffectContext, type SkillEvent, type SkillDamageEvent, type SkillHealEvent, type SkillBuffEvent, type SkillDebuffEvent, type SkillMissEvent, type CombatTarget, isNpcTarget, } from './registry.js';
export { getStatValue } from './utils.js';
import './fireballHandler.js';
import './magicMissileHandler.js';
import './tauntHandler.js';
import './shieldOfFaithHandler.js';
import './backstabHandler.js';
import './poisonBladeHandler.js';
//# sourceMappingURL=index.d.ts.map