/**
 * Handler barrel — imports all individual handler files for their side-effect
 * registrations, and re-exports the registry API.
 *
 * To add a new skill handler:
 * 1. Create a new file in this directory (e.g. mySkillHandler.ts)
 * 2. Import and call registerEffectHandler() and/or registerBuffCleanup()
 * 3. Add an import line below
 */
// Re-export the public registry API
export { registerEffectHandler, registerBuffCleanup, getEffectHandler, getBuffCleanup, isNpcTarget, } from './registry.js';
// Re-export utils
export { getStatValue } from './utils.js';
// ── Side-effect imports: register all handlers ──
import './fireballHandler.js';
import './magicMissileHandler.js';
import './tauntHandler.js';
import './shieldOfFaithHandler.js';
import './backstabHandler.js';
import './poisonBladeHandler.js';
//# sourceMappingURL=index.js.map