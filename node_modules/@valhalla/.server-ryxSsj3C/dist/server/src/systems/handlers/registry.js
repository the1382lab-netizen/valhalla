/**
 * Centralized handler registry for skill effects and buff cleanup.
 *
 * Individual handler files import and call registerEffectHandler / registerBuffCleanup
 * as a side-effect of being imported, so all handlers are registered by the time
 * the game room starts processing casts.
 */
/** Returns true when the target is an NPCState (has templateId, which PlayerState lacks). */
export function isNpcTarget(t) {
    return 'templateId' in t;
}
// ── Handler Registries ─────────────────────────────────────
const EFFECT_HANDLERS = {};
const BUFF_CLEANUP_HANDLERS = {};
/** Register a custom effect handler for a skill. */
export function registerEffectHandler(skillId, handler) {
    EFFECT_HANDLERS[skillId] = handler;
}
/** Register a cleanup handler called when a buff expires. */
export function registerBuffCleanup(skillId, handler) {
    BUFF_CLEANUP_HANDLERS[skillId] = handler;
}
/** Get the custom effect handler for a skill (if any). */
export function getEffectHandler(skillId) {
    return EFFECT_HANDLERS[skillId];
}
/** Get the buff cleanup handler for a skill (if any). */
export function getBuffCleanup(skillId) {
    return BUFF_CLEANUP_HANDLERS[skillId];
}
//# sourceMappingURL=registry.js.map