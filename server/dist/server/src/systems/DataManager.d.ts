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
import { ItemTemplate, SkillTemplate, ClassTemplate, ZoneConfig, NPCTemplate, LootTable } from '@valhalla/shared';
export declare class DataManager {
    private static _instance;
    private _items;
    private _skills;
    private _classSkills;
    private _classes;
    private _zones;
    private _npcTemplates;
    private _lootTables;
    /** Whether data was loaded from JSON (true) or hardcoded fallback (false). */
    private _loadedFromJson;
    private constructor();
    static get instance(): DataManager;
    /**
     * Initialize the DataManager. Loads all JSON data files from shared/data/.
     * Falls back to hardcoded catalogs for any that fail to load.
     */
    static initialize(): DataManager;
    get items(): Record<string, ItemTemplate>;
    get skills(): Record<string, SkillTemplate>;
    get classSkills(): Record<string, string[]>;
    get classes(): Record<string, ClassTemplate>;
    get zones(): Record<string, ZoneConfig>;
    get npcTemplates(): Record<string, NPCTemplate>;
    get lootTables(): Record<string, LootTable>;
    /** Get a single item template by ID. */
    getItem(id: string): ItemTemplate | undefined;
    /** Get a single skill template by ID. */
    getSkill(id: string): SkillTemplate | undefined;
    /** Get the list of skill IDs for a class. */
    getClassSkills(classId: string): string[];
    /** Check if a zone ID is valid (exists in loaded data). */
    isValidZone(zoneId: string): boolean;
    private loadAll;
    private loadJsonFile;
    private loadItems;
    private loadSkills;
    private loadClasses;
    private loadZones;
    private loadNPCTemplates;
    private loadLootTables;
    private logSummary;
}
//# sourceMappingURL=DataManager.d.ts.map