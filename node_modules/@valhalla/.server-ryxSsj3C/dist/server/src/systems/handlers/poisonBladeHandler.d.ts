/**
 * Poison Blade effect handler.
 *
 * Applies a poison DoT to a single enemy target.
 * Damage per second scales with the caster's Dexterity.
 *
 * Both player and NPC targets receive a ticking DoT buff:
 *   - Player targets: tracked in activeBuffs, ticked every second by SkillSystem.tickBuffs().
 *   - NPC targets:    tracked in NPCState.activeBuffs, ticked every second by NPCSystem.tickNpcBuffs().
 *     The NPC's syncedBuffs schema field is updated so clients see the debuff in the target pane.
 */
export {};
//# sourceMappingURL=poisonBladeHandler.d.ts.map