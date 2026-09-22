/**
 * Character persistence service: create, load, save, delete characters.
 * Uses raw sql.js queries (no ORM).
 */
export interface CharacterSummary {
    id: number;
    name: string;
    classId: string;
    level: number;
}
export interface InventorySlotData {
    slotIndex: number;
    itemId: string;
    quantity: number;
}
export interface EquipmentData {
    slotType: string;
    itemId: string;
}
export interface LoadedCharacter {
    id: number;
    userId: number;
    name: string;
    classId: string;
    /** Paperdoll base body, e.g. 'body_tan'. Falls back to the class default. */
    bodyId: string;
    level: number;
    xp: number;
    hp: number;
    mana: number;
    positionX: number;
    positionY: number;
    zoneId: string;
    alive: boolean;
    inventory: InventorySlotData[];
    equipment: EquipmentData[];
    actionBar: string[];
}
export interface SaveCharacterData {
    hp: number;
    mana: number;
    xp: number;
    level: number;
    positionX: number;
    positionY: number;
    zoneId: string;
    alive: boolean;
    inventory: InventorySlotData[];
    equipment: EquipmentData[];
    actionBar: string[];
}
/**
 * Create a new character with starter equipment and inventory.
 * @throws Error if name is taken, invalid class, or max characters reached.
 */
export declare function createCharacter(userId: number, name: string, classId: string): CharacterSummary;
/**
 * Get a summary list of all characters for a user.
 */
export declare function getCharactersByUser(userId: number): CharacterSummary[];
/**
 * Load full character data including inventory and equipment.
 * Verifies ownership by userId.
 * @throws Error if character not found or not owned by user.
 */
export declare function loadCharacter(characterId: number, userId: number): LoadedCharacter;
/**
 * Save character state to the database.
 * Replaces inventory and equipment rows atomically.
 */
export declare function saveCharacter(characterId: number, data: SaveCharacterData): void;
/**
 * Delete a character and its inventory/equipment.
 * @throws Error if character not found or not owned by user.
 */
export declare function deleteCharacter(characterId: number, userId: number): void;
//# sourceMappingURL=CharacterService.d.ts.map