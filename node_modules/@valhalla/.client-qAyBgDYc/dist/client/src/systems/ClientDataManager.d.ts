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
import { ItemTemplate, SkillTemplate, ClassTemplate, ZoneConfig } from '@valhalla/shared';
export declare class ClientDataManager {
    private static _instance;
    private _items;
    private _skills;
    private _classSkills;
    private _classes;
    private _classColors;
    private _zones;
    private constructor();
    static get instance(): ClientDataManager;
    /**
     * Fetch all JSON data files from the server.
     * Call this once during boot, before the game scene starts.
     */
    static initialize(serverUrl: string): Promise<ClientDataManager>;
    get items(): Record<string, ItemTemplate>;
    get skills(): Record<string, SkillTemplate>;
    get classSkills(): Record<string, string[]>;
    get classes(): Record<string, ClassTemplate>;
    get classColors(): Record<string, number>;
    get zones(): Record<string, ZoneConfig>;
    getItem(id: string): ItemTemplate | undefined;
    getSkill(id: string): SkillTemplate | undefined;
    getClassSkills(classId: string): string[];
    getClass(classId: string): ClassTemplate | undefined;
    getClassColor(classId: string): number | undefined;
}
//# sourceMappingURL=ClientDataManager.d.ts.map