/**
 * DataManager — loads game content from editor-produced JSON data files.
 *
 * Reads JSON from shared/data/ at startup with fallback to hardcoded
 * TypeScript catalogs. This bridges the editor → game server pipeline:
 * edits made in the game editor are saved to JSON files, and the server
 * picks them up on next restart.
 *
 * Usage: call DataManager.initialize() once at room creation,
 * then use DataManager.instance to access catalogs.
 */

import { readFileSync, existsSync } from 'fs';
import { resolve, dirname } from 'path';
import { fileURLToPath } from 'url';
import {
  ItemTemplate,
  SkillTemplate,
  ClassTemplate,
  ZoneConfig,
  NPCTemplate,
  LootTable,
  loadItemsFromJson,
  loadSkillsFromJson,
  loadClassesFromJson,
  loadZonesFromJson,
  loadNPCTemplatesFromJson,
  loadLootTablesFromJson,
  // Hardcoded fallbacks
  ITEM_CATALOG,
  SKILL_CATALOG,
  CLASS_SKILLS,
  CLASS_TEMPLATES,
  ZONE_REGISTRY,
} from '@valhalla/shared';

// Resolve the shared/data/ directory relative to this file
// server/src/systems/DataManager.ts → up 4 = valhalla/ → shared/data/
const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const DATA_DIR = resolve(__dirname, '..', '..', '..', 'shared', 'data');

export class DataManager {
  private static _instance: DataManager | null = null;

  // ── Loaded catalogs ──
  private _items: Record<string, ItemTemplate> = {};
  private _skills: Record<string, SkillTemplate> = {};
  private _classSkills: Record<string, string[]> = {};
  private _classes: Record<string, ClassTemplate> = {};
  private _zones: Record<string, ZoneConfig> = {};
  private _npcTemplates: Record<string, NPCTemplate> = {};
  private _lootTables: Record<string, LootTable> = {};

  /** Whether data was loaded from JSON (true) or hardcoded fallback (false). */
  private _loadedFromJson: Record<string, boolean> = {};

  private constructor() {}

  static get instance(): DataManager {
    if (!DataManager._instance) {
      throw new Error('[DataManager] Not initialized. Call DataManager.initialize() first.');
    }
    return DataManager._instance;
  }

  /**
   * Initialize the DataManager. Loads all JSON data files from shared/data/.
   * Falls back to hardcoded catalogs for any that fail to load.
   */
  static initialize(): DataManager {
    const dm = new DataManager();
    dm.loadAll();
    DataManager._instance = dm;
    return dm;
  }

  // ── Public Accessors ──

  get items(): Record<string, ItemTemplate> { return this._items; }
  get skills(): Record<string, SkillTemplate> { return this._skills; }
  get classSkills(): Record<string, string[]> { return this._classSkills; }
  get classes(): Record<string, ClassTemplate> { return this._classes; }
  get zones(): Record<string, ZoneConfig> { return this._zones; }
  get npcTemplates(): Record<string, NPCTemplate> { return this._npcTemplates; }
  get lootTables(): Record<string, LootTable> { return this._lootTables; }

  /** Get a single item template by ID. */
  getItem(id: string): ItemTemplate | undefined {
    return this._items[id];
  }

  /** Get a single skill template by ID. */
  getSkill(id: string): SkillTemplate | undefined {
    return this._skills[id];
  }

  /** Get the list of skill IDs for a class. */
  getClassSkills(classId: string): string[] {
    return this._classSkills[classId] ?? [];
  }

  /** Check if a zone ID is valid (exists in loaded data). */
  isValidZone(zoneId: string): boolean {
    return zoneId in this._zones;
  }

  // ── Loading ──

  private loadAll(): void {
    this.loadItems();
    this.loadSkills();
    this.loadClasses();
    this.loadZones();
    this.loadNPCTemplates();
    this.loadLootTables();
    this.logSummary();
  }

  private loadJsonFile(filename: string): unknown | null {
    const filePath = resolve(DATA_DIR, filename);
    if (!existsSync(filePath)) {
      console.warn(`[DataManager] File not found: ${filePath}`);
      return null;
    }
    try {
      return JSON.parse(readFileSync(filePath, 'utf-8'));
    } catch (err) {
      console.warn(`[DataManager] Failed to parse ${filename}:`, err);
      return null;
    }
  }

  private loadItems(): void {
    const json = this.loadJsonFile('items.json');
    if (json) {
      const loaded = loadItemsFromJson(json);
      if (loaded && Object.keys(loaded).length > 0) {
        this._items = loaded;
        this._loadedFromJson['items'] = true;
        return;
      }
    }
    // Fallback to hardcoded
    this._items = { ...ITEM_CATALOG } as Record<string, ItemTemplate>;
    this._loadedFromJson['items'] = false;
  }

  private loadSkills(): void {
    const json = this.loadJsonFile('skills.json');
    if (json) {
      const loaded = loadSkillsFromJson(json);
      if (loaded && Object.keys(loaded.skills).length > 0) {
        this._skills = loaded.skills;
        this._classSkills = loaded.classSkills;
        this._loadedFromJson['skills'] = true;
        return;
      }
    }
    // Fallback to hardcoded
    this._skills = { ...SKILL_CATALOG } as Record<string, SkillTemplate>;
    this._classSkills = { ...CLASS_SKILLS } as Record<string, string[]>;
    this._loadedFromJson['skills'] = false;
  }

  private loadClasses(): void {
    const json = this.loadJsonFile('classes.json');
    if (json) {
      const loaded = loadClassesFromJson(json);
      if (loaded && Object.keys(loaded.classes).length > 0) {
        this._classes = loaded.classes;
        this._loadedFromJson['classes'] = true;
        return;
      }
    }
    // Fallback to hardcoded
    this._classes = { ...CLASS_TEMPLATES } as Record<string, ClassTemplate>;
    this._loadedFromJson['classes'] = false;
  }

  private loadZones(): void {
    const json = this.loadJsonFile('zones.json');
    if (json) {
      const loaded = loadZonesFromJson(json);
      if (loaded && Object.keys(loaded).length > 0) {
        this._zones = loaded;
        this._loadedFromJson['zones'] = true;
        return;
      }
    }
    // Fallback to hardcoded
    this._zones = { ...ZONE_REGISTRY } as Record<string, ZoneConfig>;
    this._loadedFromJson['zones'] = false;
  }

  private loadNPCTemplates(): void {
    const json = this.loadJsonFile('npc-templates.json');
    if (json) {
      const loaded = loadNPCTemplatesFromJson(json);
      if (loaded) {
        this._npcTemplates = loaded;
        this._loadedFromJson['npcTemplates'] = true;
        return;
      }
    }
    this._npcTemplates = {};
    this._loadedFromJson['npcTemplates'] = false;
  }

  private loadLootTables(): void {
    const json = this.loadJsonFile('loot-tables.json');
    if (json) {
      const loaded = loadLootTablesFromJson(json);
      if (loaded) {
        this._lootTables = loaded;
        this._loadedFromJson['lootTables'] = true;
        return;
      }
    }
    this._lootTables = {};
    this._loadedFromJson['lootTables'] = false;
  }

  private logSummary(): void {
    const entries = [
      `Items: ${Object.keys(this._items).length}`,
      `Skills: ${Object.keys(this._skills).length}`,
      `Classes: ${Object.keys(this._classes).length}`,
      `Zones: ${Object.keys(this._zones).length}`,
      `NPCs: ${Object.keys(this._npcTemplates).length}`,
      `Loot Tables: ${Object.keys(this._lootTables).length}`,
    ];
    const sources = Object.entries(this._loadedFromJson)
      .filter(([, v]) => v)
      .map(([k]) => k);
    console.log(`[DataManager] Loaded: ${entries.join(', ')}`);
    if (sources.length > 0) {
      console.log(`[DataManager] From JSON: ${sources.join(', ')}`);
    }
    const fallbacks = Object.entries(this._loadedFromJson)
      .filter(([, v]) => !v)
      .map(([k]) => k);
    if (fallbacks.length > 0) {
      console.log(`[DataManager] Using hardcoded fallback: ${fallbacks.join(', ')}`);
    }
  }
}
