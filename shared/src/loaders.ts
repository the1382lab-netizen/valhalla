/**
 * JSON data file loaders with validation.
 * Used by both game client (via fetch) and game server (via fs.readFileSync)
 * to load content from JSON data files exported by the editor.
 */

import type { ItemTemplate } from './items.js';
import type { SkillTemplate } from './skills.js';
import type { ClassTemplate } from './classes.js';
import type { ZoneConfig } from './maps.js';
import { validateZoneAtmosphere } from './validation.js';
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

export interface UIConfigFileData extends UIConfig {}

/**
 * Parse and validate items JSON data.
 * Returns the items record or null if invalid.
 */
export function loadItemsFromJson(json: unknown): Record<string, ItemTemplate> | null {
  try {
    const data = json as ItemsFileData;
    if (!data || typeof data !== 'object' || !data.items) {
      console.warn('[Loader] Invalid items JSON: missing "items" field');
      return null;
    }
    return data.items;
  } catch (e) {
    console.warn('[Loader] Failed to parse items JSON:', e);
    return null;
  }
}

/**
 * Parse and validate skills JSON data.
 * Returns an object with skills and classSkills, or null if invalid.
 */
export function loadSkillsFromJson(json: unknown): { skills: Record<string, SkillTemplate>; classSkills: Record<string, string[]> } | null {
  try {
    const data = json as SkillsFileData;
    if (!data || typeof data !== 'object' || !data.skills || !data.classSkills) {
      console.warn('[Loader] Invalid skills JSON: missing "skills" or "classSkills" field');
      return null;
    }
    return { skills: data.skills, classSkills: data.classSkills };
  } catch (e) {
    console.warn('[Loader] Failed to parse skills JSON:', e);
    return null;
  }
}

/**
 * Parse and validate classes JSON data.
 * Returns the classes record or null if invalid.
 */
export function loadClassesFromJson(json: unknown): { classes: Record<string, ClassTemplate>; classColors: Record<string, number> } | null {
  try {
    const data = json as ClassesFileData;
    if (!data || typeof data !== 'object' || !data.classes) {
      console.warn('[Loader] Invalid classes JSON: missing "classes" field');
      return null;
    }
    return { classes: data.classes, classColors: data.classColors || {} };
  } catch (e) {
    console.warn('[Loader] Failed to parse classes JSON:', e);
    return null;
  }
}

/**
 * Parse and validate zones JSON data.
 */
export function loadZonesFromJson(json: unknown): Record<string, ZoneConfig> | null {
  try {
    const data = json as ZonesFileData;
    if (!data || typeof data !== 'object' || !data.zones) {
      console.warn('[Loader] Invalid zones JSON');
      return null;
    }
    // B-06: a bad atmosphere is reported, not fatal — the game clamps and
    // falls back to today's look for anything it cannot use.
    for (const [zoneId, zone] of Object.entries(data.zones)) {
      for (const problem of validateZoneAtmosphere(zone?.atmosphere)) {
        console.warn(`[Loader] zones.json/${zoneId}: ${problem}`);
      }
    }
    return data.zones;
  } catch (e) {
    console.warn('[Loader] Failed to parse zones JSON:', e);
    return null;
  }
}

/**
 * Parse and validate NPC templates JSON data.
 */
export function loadNPCTemplatesFromJson(json: unknown): Record<string, NPCTemplate> | null {
  try {
    const data = json as NPCTemplatesFileData;
    if (!data || typeof data !== 'object' || !data.templates) {
      console.warn('[Loader] Invalid NPC templates JSON');
      return null;
    }
    return data.templates;
  } catch (e) {
    console.warn('[Loader] Failed to parse NPC templates JSON:', e);
    return null;
  }
}

/**
 * Parse and validate loot tables JSON data.
 */
export function loadLootTablesFromJson(json: unknown): Record<string, LootTable> | null {
  try {
    const data = json as LootTablesFileData;
    if (!data || typeof data !== 'object' || !data.tables) {
      console.warn('[Loader] Invalid loot tables JSON');
      return null;
    }
    return data.tables;
  } catch (e) {
    console.warn('[Loader] Failed to parse loot tables JSON:', e);
    return null;
  }
}

/**
 * Parse UI config from JSON.
 * Returns the config or null if invalid.
 */
export function loadUIConfigFromJson(json: unknown): UIConfig | null {
  try {
    const data = json as UIConfig;
    if (!data || typeof data !== 'object' || !data.version) {
      console.warn('[Loader] Invalid UI config JSON');
      return null;
    }
    return data;
  } catch (e) {
    console.warn('[Loader] Failed to parse UI config JSON:', e);
    return null;
  }
}
