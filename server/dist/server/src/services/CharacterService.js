/**
 * Character persistence service: create, load, save, delete characters.
 * Uses raw sql.js queries (no ORM).
 */
import { getDb, saveToDisk } from '../db/index.js';
import { ALL_CLASS_IDS, computeDerivedStats, EquipSlotType, MAX_CHARACTERS_PER_USER, ZoneId, ZONE_REGISTRY, CLASS_TEMPLATES, ITEM_CATALOG, DEFAULT_BODY_ID, } from '@valhalla/shared';
import { DataManager } from '../systems/DataManager.js';
// ── Helper: default paperdoll body for a class ──────────────
function defaultBodyId(classId) {
    const dm = (() => { try {
        return DataManager.instance;
    }
    catch {
        return null;
    } })();
    const template = (dm?.classes[classId]) ?? CLASS_TEMPLATES[classId];
    return template?.bodyId || DEFAULT_BODY_ID;
}
// ── Helper: query single row ────────────────────────────────
function queryOne(sql, params = []) {
    const db = getDb();
    const result = db.exec(sql, params);
    if (result.length === 0 || result[0].values.length === 0)
        return null;
    const columns = result[0].columns;
    const values = result[0].values[0];
    const row = {};
    for (let i = 0; i < columns.length; i++) {
        row[columns[i]] = values[i];
    }
    return row;
}
function queryAll(sql, params = []) {
    const db = getDb();
    const result = db.exec(sql, params);
    if (result.length === 0)
        return [];
    const columns = result[0].columns;
    return result[0].values.map((values) => {
        const row = {};
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
export function createCharacter(userId, name, classId) {
    const db = getDb();
    // Validate class
    if (!ALL_CLASS_IDS.includes(classId)) {
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
    const stats = computeDerivedStats(classId, 1);
    const now = Date.now();
    // Prefer DataManager (loads from editor JSON), fall back to hardcoded
    const dm = (() => { try {
        return DataManager.instance;
    }
    catch {
        return null;
    } })();
    const startZone = dm ? dm.zones[ZoneId.GRASSLANDS] : ZONE_REGISTRY[ZoneId.GRASSLANDS];
    const spawnX = startZone.defaultSpawn.x;
    const spawnY = startZone.defaultSpawn.y;
    // Insert character
    db.run(`INSERT INTO characters (user_id, name, class_id, body_id, level, xp, hp, mana, position_x, position_y, zone_id, alive, created_at, updated_at)
     VALUES (?, ?, ?, ?, 1, 0, ?, ?, ?, ?, ?, 1, ?, ?)`, [userId, trimmedName, classId, defaultBodyId(classId), stats.maxHp, stats.maxMana, spawnX, spawnY, ZoneId.GRASSLANDS, now, now]);
    const charIdResult = db.exec('SELECT last_insert_rowid() as id');
    const charId = charIdResult[0].values[0][0];
    // Insert starting items from the class template (data-driven; falls back to hardcoded CLASS_TEMPLATES)
    const classTemplate = (dm?.classes[classId]) ?? CLASS_TEMPLATES[classId];
    const startingItems = classTemplate?.startingItems ?? [];
    let inventorySlot = 0;
    for (const startingItem of startingItems) {
        if (startingItem.equipped) {
            // Look up the item's equipSlot from the item catalog
            const itemTemplate = dm
                ? dm.getItem(startingItem.itemId)
                : ITEM_CATALOG[startingItem.itemId];
            const slotType = itemTemplate?.equipSlot ?? EquipSlotType.WEAPON;
            db.run('INSERT INTO character_equipment (character_id, slot_type, item_id) VALUES (?, ?, ?)', [charId, slotType, startingItem.itemId]);
        }
        else {
            db.run('INSERT INTO inventory_items (character_id, slot_index, item_id, quantity) VALUES (?, ?, ?, ?)', [charId, inventorySlot, startingItem.itemId, startingItem.quantity]);
            inventorySlot++;
        }
    }
    saveToDisk();
    return { id: charId, name: trimmedName, classId, level: 1 };
}
/**
 * Get a summary list of all characters for a user.
 */
export function getCharactersByUser(userId) {
    const rows = queryAll('SELECT id, name, class_id, level FROM characters WHERE user_id = ?', [userId]);
    return rows.map(row => ({
        id: row.id,
        name: row.name,
        classId: row.class_id,
        level: row.level,
    }));
}
/**
 * Load full character data including inventory and equipment.
 * Verifies ownership by userId.
 * @throws Error if character not found or not owned by user.
 */
export function loadCharacter(characterId, userId) {
    // Load character row
    const char = queryOne('SELECT * FROM characters WHERE id = ? AND user_id = ?', [characterId, userId]);
    if (!char) {
        throw new Error('Character not found or access denied.');
    }
    // Load inventory
    const invRows = queryAll('SELECT slot_index, item_id, quantity FROM inventory_items WHERE character_id = ?', [characterId]);
    const inventory = invRows.map(row => ({
        slotIndex: row.slot_index,
        itemId: row.item_id,
        quantity: row.quantity,
    }));
    // Load equipment
    const equipRows = queryAll('SELECT slot_type, item_id FROM character_equipment WHERE character_id = ?', [characterId]);
    const equipment = equipRows.map(row => ({
        slotType: row.slot_type,
        itemId: row.item_id,
    }));
    // Load action bar
    const actionBarRows = queryAll('SELECT slot_index, skill_id FROM character_action_bar WHERE character_id = ? ORDER BY slot_index', [characterId]);
    const actionBar = ['', '', '', '', '', '', '', ''];
    for (const row of actionBarRows) {
        const idx = row.slot_index;
        if (idx >= 0 && idx < 8) {
            actionBar[idx] = row.skill_id;
        }
    }
    return {
        id: char.id,
        userId: char.user_id,
        name: char.name,
        classId: char.class_id,
        bodyId: char.body_id || defaultBodyId(char.class_id),
        level: char.level,
        xp: char.xp,
        hp: char.hp,
        mana: char.mana,
        positionX: char.position_x,
        positionY: char.position_y,
        zoneId: char.zone_id,
        alive: !!char.alive,
        inventory,
        equipment,
        actionBar,
    };
}
/**
 * Save character state to the database.
 * Replaces inventory and equipment rows atomically.
 */
export function saveCharacter(characterId, data) {
    const db = getDb();
    // Update character fields (including zone_id for position persistence)
    db.run(`UPDATE characters SET hp = ?, mana = ?, xp = ?, level = ?,
     position_x = ?, position_y = ?, zone_id = ?, alive = ?, updated_at = ?
     WHERE id = ?`, [data.hp, data.mana, data.xp, data.level,
        data.positionX, data.positionY, data.zoneId, data.alive ? 1 : 0, Date.now(),
        characterId]);
    // Replace inventory: delete all then reinsert
    db.run('DELETE FROM inventory_items WHERE character_id = ?', [characterId]);
    for (const slot of data.inventory) {
        db.run('INSERT INTO inventory_items (character_id, slot_index, item_id, quantity) VALUES (?, ?, ?, ?)', [characterId, slot.slotIndex, slot.itemId, slot.quantity]);
    }
    // Replace equipment: delete all then reinsert
    db.run('DELETE FROM character_equipment WHERE character_id = ?', [characterId]);
    for (const equip of data.equipment) {
        db.run('INSERT INTO character_equipment (character_id, slot_type, item_id) VALUES (?, ?, ?)', [characterId, equip.slotType, equip.itemId]);
    }
    // Replace action bar: delete all then reinsert non-empty slots
    db.run('DELETE FROM character_action_bar WHERE character_id = ?', [characterId]);
    if (data.actionBar) {
        for (let i = 0; i < data.actionBar.length; i++) {
            if (data.actionBar[i]) {
                db.run('INSERT INTO character_action_bar (character_id, slot_index, skill_id) VALUES (?, ?, ?)', [characterId, i, data.actionBar[i]]);
            }
        }
    }
    saveToDisk();
}
/**
 * Delete a character and its inventory/equipment.
 * @throws Error if character not found or not owned by user.
 */
export function deleteCharacter(characterId, userId) {
    const db = getDb();
    const char = queryOne('SELECT id FROM characters WHERE id = ? AND user_id = ?', [characterId, userId]);
    if (!char) {
        throw new Error('Character not found or access denied.');
    }
    // Delete inventory and equipment first, then character
    db.run('DELETE FROM inventory_items WHERE character_id = ?', [characterId]);
    db.run('DELETE FROM character_equipment WHERE character_id = ?', [characterId]);
    db.run('DELETE FROM characters WHERE id = ?', [characterId]);
    saveToDisk();
}
//# sourceMappingURL=CharacterService.js.map