/**
 * JSON data file loaders with validation.
 * Used by both game client (via fetch) and game server (via fs.readFileSync)
 * to load content from JSON data files exported by the editor.
 */
import type { ItemTemplate } from './items.js';
import type { SkillTemplate } from './skills.js';
import type { ClassTemplate } from './classes.js';
import type { ZoneConfig } from './maps.js';
import type { UIConfig } from './ui-config.js';
import type { NPCTemplate } from './npcs.js';
import type { LootTable } from './loot-tables.js';
export interface ItemsFileData {
    version: string;
    items: Record<string, ItemTemplate>;
}
export interface SkillsFileData {
    version: string;
    skills: Record<string, SkillTemplate>;
    classSkills: Record<string, string[]>;
}
export interface ClassesFileData {
    version: string;
    classes: Record<string, ClassTemplate>;
    classColors: Record<string, number>;
}
export interface ZonesFileData {
    version: string;
    zones: Record<string, ZoneConfig>;
}
export interface NPCTemplatesFileData {
    version: string;
    templates: Record<string, NPCTemplate>;
}
export interface LootTablesFileData {
    version: string;
    tables: Record<string, LootTable>;
}
export interface UIConfigFileData extends UIConfig {
}
/**
 * Parse and validate items JSON data.
 * Returns the items record or null if invalid.
 */
export declare function loadItemsFromJson(json: unknown): Record<string, ItemTemplate> | null;
/**
 * Parse and validate skills JSON data.
 * Returns an object with skills and classSkills, or null if invalid.
 */
export declare function loadSkillsFromJson(json: unknown): {
    skills: Record<string, SkillTemplate>;
    classSkills: Record<string, string[]>;
} | null;
/**
 * Parse and validate classes JSON data.
 * Returns the classes record or null if invalid.
 */
export declare function loadClassesFromJson(json: unknown): {
    classes: Record<string, ClassTemplate>;
    classColors: Record<string, number>;
} | null;
/**
 * Parse and validate zones JSON data.
 */
export declare function loadZonesFromJson(json: unknown): Record<string, ZoneConfig> | null;
/**
 * Parse and validate NPC templates JSON data.
 */
export declare function loadNPCTemplatesFromJson(json: unknown): Record<string, NPCTemplate> | null;
/**
 * Parse and validate loot tables JSON data.
 */
export declare function loadLootTablesFromJson(json: unknown): Record<string, LootTable> | null;
/**
 * Parse UI config from JSON.
 * Returns the config or null if invalid.
 */
export declare function loadUIConfigFromJson(json: unknown): UIConfig | null;
//# sourceMappingURL=loaders.d.ts.map