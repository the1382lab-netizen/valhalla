/**
 * ClientDataManager — fetches game content JSON from the server at startup.
 *
 * Loads items, skills, classes, and zones from the server's /api/data/ endpoint
 * (which serves shared/data/*.json). Falls back to hardcoded TypeScript catalogs
 * if the fetch fails, so the game still works even without the editor pipeline.
 *
 * Usage:
 *   await ClientDataManager.initialize(serverUrl);
 *   const item = ClientDataManager.instance.getItem('iron_sword');
 */

import {
  ItemTemplate,
  SkillTemplate,
  ClassTemplate,
  ZoneConfig,
  ITEM_CATALOG,
  SKILL_CATALOG,
  CLASS_SKILLS,
  CLASS_TEMPLATES,
  CLASS_COLORS,
  ZONE_REGISTRY,
  loadItemsFromJson,
  loadSkillsFromJson,
  loadClassesFromJson,
  loadZonesFromJson,
} from '@valhalla/shared';

export class ClientDataManager {
  private static _instance: ClientDataManager | null = null;

  private _items: Record<string, ItemTemplate> = {};
  private _skills: Record<string, SkillTemplate> = {};
  private _classSkills: Record<string, string[]> = {};
  private _classes: Record<string, ClassTemplate> = {};
  private _classColors: Record<string, number> = {};
  private _zones: Record<string, ZoneConfig> = {};

  private constructor() {}

  static get instance(): ClientDataManager {
    if (!ClientDataManager._instance) {
      // Return fallback instance with hardcoded data
      const dm = new ClientDataManager();
      dm._items = { ...ITEM_CATALOG } as Record<string, ItemTemplate>;
      dm._skills = { ...SKILL_CATALOG } as Record<string, SkillTemplate>;
      dm._classSkills = { ...CLASS_SKILLS } as Record<string, string[]>;
      dm._classes = { ...CLASS_TEMPLATES } as Record<string, ClassTemplate>;
      dm._classColors = { ...CLASS_COLORS } as Record<string, number>;
      dm._zones = { ...ZONE_REGISTRY } as Record<string, ZoneConfig>;
      ClientDataManager._instance = dm;
    }
    return ClientDataManager._instance;
  }

  /**
   * Fetch all JSON data files from the server.
   * Call this once during boot, before the game scene starts.
   */
  static async initialize(serverUrl: string): Promise<ClientDataManager> {
    const dm = new ClientDataManager();
    const baseUrl = `${serverUrl}/api/data`;

    // Fetch all data files in parallel
    const [itemsRes, skillsRes, classesRes, zonesRes] = await Promise.allSettled([
      fetch(`${baseUrl}/items.json`).then(r => r.ok ? r.json() : null),
      fetch(`${baseUrl}/skills.json`).then(r => r.ok ? r.json() : null),
      fetch(`${baseUrl}/classes.json`).then(r => r.ok ? r.json() : null),
      fetch(`${baseUrl}/zones.json`).then(r => r.ok ? r.json() : null),
    ]);

    // Items
    const itemsJson = itemsRes.status === 'fulfilled' ? itemsRes.value : null;
    const loadedItems = itemsJson ? loadItemsFromJson(itemsJson) : null;
    dm._items = (loadedItems && Object.keys(loadedItems).length > 0)
      ? loadedItems
      : { ...ITEM_CATALOG } as Record<string, ItemTemplate>;

    // Skills
    const skillsJson = skillsRes.status === 'fulfilled' ? skillsRes.value : null;
    const loadedSkills = skillsJson ? loadSkillsFromJson(skillsJson) : null;
    if (loadedSkills && Object.keys(loadedSkills.skills).length > 0) {
      dm._skills = loadedSkills.skills;
      dm._classSkills = loadedSkills.classSkills;
    } else {
      dm._skills = { ...SKILL_CATALOG } as Record<string, SkillTemplate>;
      dm._classSkills = { ...CLASS_SKILLS } as Record<string, string[]>;
    }

    // Classes
    const classesJson = classesRes.status === 'fulfilled' ? classesRes.value : null;
    const loadedClasses = classesJson ? loadClassesFromJson(classesJson) : null;
    if (loadedClasses && Object.keys(loadedClasses.classes).length > 0) {
      dm._classes = loadedClasses.classes;
      dm._classColors = loadedClasses.classColors;
    } else {
      dm._classes = { ...CLASS_TEMPLATES } as Record<string, ClassTemplate>;
      dm._classColors = { ...CLASS_COLORS } as Record<string, number>;
    }

    // Zones
    const zonesJson = zonesRes.status === 'fulfilled' ? zonesRes.value : null;
    const loadedZones = zonesJson ? loadZonesFromJson(zonesJson) : null;
    dm._zones = (loadedZones && Object.keys(loadedZones).length > 0)
      ? loadedZones
      : { ...ZONE_REGISTRY } as Record<string, ZoneConfig>;

    const sources: string[] = [];
    if (loadedItems) sources.push('items');
    if (loadedSkills) sources.push('skills');
    if (loadedClasses) sources.push('classes');
    if (loadedZones) sources.push('zones');
    console.log(`[ClientDataManager] Loaded from JSON: ${sources.join(', ') || 'none (using hardcoded fallbacks)'}`);

    ClientDataManager._instance = dm;
    return dm;
  }

  // ── Accessors ──

  get items(): Record<string, ItemTemplate> { return this._items; }
  get skills(): Record<string, SkillTemplate> { return this._skills; }
  get classSkills(): Record<string, string[]> { return this._classSkills; }
  get classes(): Record<string, ClassTemplate> { return this._classes; }
  get classColors(): Record<string, number> { return this._classColors; }
  get zones(): Record<string, ZoneConfig> { return this._zones; }

  getItem(id: string): ItemTemplate | undefined { return this._items[id]; }
  getSkill(id: string): SkillTemplate | undefined { return this._skills[id]; }
  getClassSkills(classId: string): string[] { return this._classSkills[classId] ?? []; }
  getClass(classId: string): ClassTemplate | undefined { return this._classes[classId]; }
  getClassColor(classId: string): number | undefined { return this._classColors[classId]; }
}
