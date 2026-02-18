/**
 * NPC & Enemy Template definitions.
 * Used by the game editor to create NPC/enemy content
 * and by the server to spawn and manage NPCs.
 */
import type { StatBlock } from './classes.js';
export interface NPCTemplate {
    id: string;
    name: string;
    description: string;
    type: 'enemy' | 'npc';
    level: number;
    stats: Partial<StatBlock>;
    hp: number;
    mana?: number;
    lootTableId?: string;
    behaviorType: 'passive' | 'aggressive' | 'patrol' | 'stationary' | 'fleeing';
    aggroRange?: number;
    respawnMs: number;
    skills: string[];
    spriteColor: number;
    spriteSize: number;
    xpReward: number;
    dialogue?: string[];
    vendorInventory?: string[];
}
//# sourceMappingURL=npcs.d.ts.map