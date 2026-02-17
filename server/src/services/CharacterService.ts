/**
 * Character persistence service: create, load, save, delete characters.
 * Uses raw sql.js queries (no ORM).
 */

import { getDb, saveToDisk } from '../db/index.js';
import {
  ClassId,
  ALL_CLASS_IDS,
  computeDerivedStats,
  ItemId,
  EquipSlotType,
  MAX_CHARACTERS_PER_USER,
  ZoneId,
  ZONE_REGISTRY,
} from '@valhalla/shared';

// ── Types ───────────────────────────────────────────────────

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
}

// ── Starter Equipment by Class ──────────────────────────────

const STARTER_WEAPONS: Record<string, ItemId> = {
  [ClassId.WARRIOR]: ItemId.IRON_SWORD,
  [ClassId.CLERIC]: ItemId.IRON_MACE,
  [ClassId.RANGER]: ItemId.SHORT_BOW,
  [ClassId.ROGUE]: ItemId.IRON_DAGGER,
  [ClassId.SHAMAN]: ItemId.BONE_TOTEM,
  [ClassId.WIZARD]: ItemId.OAK_STAFF,
};

// ── Helper: query single row ────────────────────────────────

function queryOne(sql: string, params: any[] = []): Record<string, any> | null {
  const db = getDb();
  const result = db.exec(sql, params);
  if (result.length === 0 || result[0].values.length === 0) return null;
  const columns = result[0].columns;
  const values = result[0].values[0];
  const row: Record<string, any> = {};
  for (let i = 0; i < columns.length; i++) {
    row[columns[i]] = values[i];
  }
  return row;
}

function queryAll(sql: string, params: any[] = []): Record<string, any>[] {
  const db = getDb();
  const result = db.exec(sql, params);
  if (result.length === 0) return [];
  const columns = result[0].columns;
  return result[0].values.map((values: any[]) => {
    const row: Record<string, any> = {};
    for (let i = 0; i < columns.length; i++) {
      row[columns[i]] = values[i];
    }
    return row;
  });
}

// ── Service Functions ───────────────────────────────────────

/**
 * Create a new character with starter equipment and inventory.
 * @throws Error if name is taken, invalid class, or max characters reached.
 */
export function createCharacter(userId: number, name: string, classId: string): CharacterSummary {
  const db = getDb();

  // Validate class
  if (!ALL_CLASS_IDS.includes(classId as ClassId)) {
    throw new Error(`Invalid class: ${classId}`);
  }

  // Validate name
  const trimmedName = name.trim();
  if (trimmedName.length < 2 || trimmedName.length > 20) {
    throw new Error('Character name must be 2–20 characters.');
  }
  if (!/^[a-zA-Z][a-zA-Z0-9_ ]*$/.test(trimmedName)) {
    throw new Error('Character name must start with a letter and contain only letters, numbers, spaces, and underscores.');
  }

  // Check max characters per user
  const existingChars = queryAll('SELECT id FROM characters WHERE user_id = ?', [userId]);
  if (existingChars.length >= MAX_CHARACTERS_PER_USER) {
    throw new Error(`Maximum of ${MAX_CHARACTERS_PER_USER} characters per account.`);
  }

  // Check name uniqueness
  const nameTaken = queryOne('SELECT id FROM characters WHERE name = ?', [trimmedName]);
  if (nameTaken) {
    throw new Error('Character name already taken.');
  }

  // Compute initial stats
  const stats = computeDerivedStats(classId as ClassId, 1);
  const now = Date.now();
  const startZone = ZONE_REGISTRY[ZoneId.GRASSLANDS];
  const spawnX = startZone.defaultSpawn.x;
  const spawnY = startZone.defaultSpawn.y;

  // Insert character
  db.run(
    `INSERT INTO characters (user_id, name, class_id, level, xp, hp, mana, position_x, position_y, zone_id, alive, created_at, updated_at)
     VALUES (?, ?, ?, 1, 0, ?, ?, ?, ?, ?, 1, ?, ?)`,
    [userId, trimmedName, classId, stats.maxHp, stats.maxMana, spawnX, spawnY, ZoneId.GRASSLANDS, now, now],
  );

  const charIdResult = db.exec('SELECT last_insert_rowid() as id');
  const charId = charIdResult[0].values[0][0] as number;

  // Insert starter weapon into equipment
  const weapon = STARTER_WEAPONS[classId] ?? ItemId.IRON_SWORD;
  db.run(
    'INSERT INTO character_equipment (character_id, slot_type, item_id) VALUES (?, ?, ?)',
    [charId, EquipSlotType.WEAPON, weapon],
  );

  // Insert starter inventory items (potions)
  db.run(
    'INSERT INTO inventory_items (character_id, slot_index, item_id, quantity) VALUES (?, ?, ?, ?)',
    [charId, 0, ItemId.HEALTH_POTION, 5],
  );
  db.run(
    'INSERT INTO inventory_items (character_id, slot_index, item_id, quantity) VALUES (?, ?, ?, ?)',
    [charId, 1, ItemId.MANA_POTION, 3],
  );

  saveToDisk();

  return { id: charId, name: trimmedName, classId, level: 1 };
}

/**
 * Get a summary list of all characters for a user.
 */
export function getCharactersByUser(userId: number): CharacterSummary[] {
  const rows = queryAll(
    'SELECT id, name, class_id, level FROM characters WHERE user_id = ?',
    [userId],
  );
  return rows.map(row => ({
    id: row.id as number,
    name: row.name as string,
    classId: row.class_id as string,
    level: row.level as number,
  }));
}

/**
 * Load full character data including inventory and equipment.
 * Verifies ownership by userId.
 * @throws Error if character not found or not owned by user.
 */
export function loadCharacter(characterId: number, userId: number): LoadedCharacter {
  // Load character row
  const char = queryOne(
    'SELECT * FROM characters WHERE id = ? AND user_id = ?',
    [characterId, userId],
  );

  if (!char) {
    throw new Error('Character not found or access denied.');
  }

  // Load inventory
  const invRows = queryAll(
    'SELECT slot_index, item_id, quantity FROM inventory_items WHERE character_id = ?',
    [characterId],
  );
  const inventory: InventorySlotData[] = invRows.map(row => ({
    slotIndex: row.slot_index as number,
    itemId: row.item_id as string,
    quantity: row.quantity as number,
  }));

  // Load equipment
  const equipRows = queryAll(
    'SELECT slot_type, item_id FROM character_equipment WHERE character_id = ?',
    [characterId],
  );
  const equipment: EquipmentData[] = equipRows.map(row => ({
    slotType: row.slot_type as string,
    itemId: row.item_id as string,
  }));

  return {
    id: char.id as number,
    userId: char.user_id as number,
    name: char.name as string,
    classId: char.class_id as string,
    level: char.level as number,
    xp: char.xp as number,
    hp: char.hp as number,
    mana: char.mana as number,
    positionX: char.position_x as number,
    positionY: char.position_y as number,
    zoneId: char.zone_id as string,
    alive: !!(char.alive as number),
    inventory,
    equipment,
  };
}

/**
 * Save character state to the database.
 * Replaces inventory and equipment rows atomically.
 */
export function saveCharacter(characterId: number, data: SaveCharacterData): void {
  const db = getDb();

  // Update character fields (including zone_id for position persistence)
  db.run(
    `UPDATE characters SET hp = ?, mana = ?, xp = ?, level = ?,
     position_x = ?, position_y = ?, zone_id = ?, alive = ?, updated_at = ?
     WHERE id = ?`,
    [data.hp, data.mana, data.xp, data.level,
     data.positionX, data.positionY, data.zoneId, data.alive ? 1 : 0, Date.now(),
     characterId],
  );

  // Replace inventory: delete all then reinsert
  db.run('DELETE FROM inventory_items WHERE character_id = ?', [characterId]);
  for (const slot of data.inventory) {
    db.run(
      'INSERT INTO inventory_items (character_id, slot_index, item_id, quantity) VALUES (?, ?, ?, ?)',
      [characterId, slot.slotIndex, slot.itemId, slot.quantity],
    );
  }

  // Replace equipment: delete all then reinsert
  db.run('DELETE FROM character_equipment WHERE character_id = ?', [characterId]);
  for (const equip of data.equipment) {
    db.run(
      'INSERT INTO character_equipment (character_id, slot_type, item_id) VALUES (?, ?, ?)',
      [characterId, equip.slotType, equip.itemId],
    );
  }

  saveToDisk();
}

/**
 * Delete a character and its inventory/equipment.
 * @throws Error if character not found or not owned by user.
 */
export function deleteCharacter(characterId: number, userId: number): void {
  const db = getDb();

  const char = queryOne(
    'SELECT id FROM characters WHERE id = ? AND user_id = ?',
    [characterId, userId],
  );

  if (!char) {
    throw new Error('Character not found or access denied.');
  }

  // Delete inventory and equipment first, then character
  db.run('DELETE FROM inventory_items WHERE character_id = ?', [characterId]);
  db.run('DELETE FROM character_equipment WHERE character_id = ?', [characterId]);
  db.run('DELETE FROM characters WHERE id = ?', [characterId]);

  saveToDisk();
}
