/**
 * Character persistence service: create, load, save, delete characters.
 * Uses raw sql.js queries (no ORM).
 */

import { getDb, saveToDisk } from '../db/index.js';
import {
  ClassId,
  ALL_CLASS_IDS,
  computeDerivedStats,
  EquipSlotType,
  MAX_CHARACTERS_PER_USER,
  ZoneId,
  ZONE_REGISTRY,
  CLASS_TEMPLATES,
  ITEM_CATALOG,
  DEFAULT_BODY_ID,
} from '@valhalla/shared';
import { DataManager } from '../systems/DataManager.js';

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


// ── Helper: default paperdoll body for a class ──────────────

function defaultBodyId(classId: string): string {
  const dm = (() => { try { return DataManager.instance; } catch { return null; } })();
  const template = (dm?.classes[classId]) ?? CLASS_TEMPLATES[classId as ClassId];
  return template?.bodyId || DEFAULT_BODY_ID;
}

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
  // Prefer DataManager (loads from editor JSON), fall back to hardcoded
  const dm = (() => { try { return DataManager.instance; } catch { return null; } })();
  const startZone = dm ? dm.zones[ZoneId.GRASSLANDS] : ZONE_REGISTRY[ZoneId.GRASSLANDS];
  const spawnX = startZone.defaultSpawn.x;
  const spawnY = startZone.defaultSpawn.y;

  // Insert character
  db.run(
    `INSERT INTO characters (user_id, name, class_id, body_id, level, xp, hp, mana, position_x, position_y, zone_id, alive, created_at, updated_at)
     VALUES (?, ?, ?, ?, 1, 0, ?, ?, ?, ?, ?, 1, ?, ?)`,
    [userId, trimmedName, classId, defaultBodyId(classId), stats.maxHp, stats.maxMana, spawnX, spawnY, ZoneId.GRASSLANDS, now, now],
  );

  const charIdResult = db.exec('SELECT last_insert_rowid() as id');
  const charId = charIdResult[0].values[0][0] as number;

  // Insert starting items from the class template (data-driven; falls back to hardcoded CLASS_TEMPLATES)
  const classTemplate = (dm?.classes[classId]) ?? CLASS_TEMPLATES[classId as ClassId];
  const startingItems = classTemplate?.startingItems ?? [];
  let inventorySlot = 0;
  for (const startingItem of startingItems) {
    if (startingItem.equipped) {
      // Look up the item's equipSlot from the item catalog
      const itemTemplate = dm
        ? dm.getItem(startingItem.itemId)
        : (ITEM_CATALOG as Record<string, typeof ITEM_CATALOG[keyof typeof ITEM_CATALOG]>)[startingItem.itemId];
      const slotType = itemTemplate?.equipSlot ?? EquipSlotType.WEAPON;
      db.run(
        'INSERT INTO character_equipment (character_id, slot_type, item_id) VALUES (?, ?, ?)',
        [charId, slotType, startingItem.itemId],
      );
    } else {
      db.run(
        'INSERT INTO inventory_items (character_id, slot_index, item_id, quantity) VALUES (?, ?, ?, ?)',
        [charId, inventorySlot, startingItem.itemId, startingItem.quantity],
      );
      inventorySlot++;
    }
  }

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

  // Load action bar
  const actionBarRows = queryAll(
    'SELECT slot_index, skill_id FROM character_action_bar WHERE character_id = ? ORDER BY slot_index',
    [characterId],
  );
  const actionBar: string[] = ['', '', '', '', '', '', '', ''];
  for (const row of actionBarRows) {
    const idx = row.slot_index as number;
    if (idx >= 0 && idx < 8) {
      actionBar[idx] = row.skill_id as string;
    }
  }

  return {
    id: char.id as number,
    userId: char.user_id as number,
    name: char.name as string,
    classId: char.class_id as string,
    bodyId: (char.body_id as string) || defaultBodyId(char.class_id as string),
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
    actionBar,
  };
}

/**
 * Return the owning user id for a character, or null if the character does not
 * exist. Used by the server-to-server routes, which are trusted and therefore
 * check existence rather than ownership.
 */
export function getCharacterOwner(characterId: number): number | null {
  const row = queryOne('SELECT user_id FROM characters WHERE id = ?', [characterId]);
  return row ? (row.user_id as number) : null;
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

  // Replace action bar: delete all then reinsert non-empty slots
  db.run('DELETE FROM character_action_bar WHERE character_id = ?', [characterId]);
  if (data.actionBar) {
    for (let i = 0; i < data.actionBar.length; i++) {
      if (data.actionBar[i]) {
        db.run(
          'INSERT INTO character_action_bar (character_id, slot_index, skill_id) VALUES (?, ?, ?)',
          [characterId, i, data.actionBar[i]],
        );
      }
    }
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
  // B-21: explicit, like the two above. ON DELETE CASCADE cannot be relied on:
  // sql.js's export() (saveToDisk) resets PRAGMA foreign_keys to OFF.
  db.run('DELETE FROM character_settings WHERE character_id = ?', [characterId]);
  db.run('DELETE FROM characters WHERE id = ?', [characterId]);

  saveToDisk();
}
