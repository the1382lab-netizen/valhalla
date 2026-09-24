/**
 * Database schema documentation for Valhalla.
 *
 * Tables are created via raw SQL in db/index.ts.
 * This file serves as a reference for the schema structure.
 *
 * -- users --
 * id INTEGER PRIMARY KEY AUTOINCREMENT
 * username TEXT NOT NULL UNIQUE
 * password_hash TEXT NOT NULL
 * created_at INTEGER NOT NULL (unix ms)
 * banned_until INTEGER NULL (unix ms; NULL = not banned; 8.64e15 = permanent)
 * ban_reason TEXT NOT NULL DEFAULT ''
 * banned_by TEXT NOT NULL DEFAULT ''
 *
 * -- characters --
 * id INTEGER PRIMARY KEY AUTOINCREMENT
 * user_id INTEGER NOT NULL REFERENCES users(id)
 * name TEXT NOT NULL UNIQUE
 * class_id TEXT NOT NULL
 * level INTEGER NOT NULL DEFAULT 1
 * xp INTEGER NOT NULL DEFAULT 0
 * hp INTEGER NOT NULL
 * mana INTEGER NOT NULL
 * position_x REAL NOT NULL
 * position_y REAL NOT NULL
 * zone_id TEXT NOT NULL DEFAULT 'main'
 * alive INTEGER NOT NULL DEFAULT 1
 * created_at INTEGER NOT NULL (unix ms)
 * updated_at INTEGER NOT NULL (unix ms)
 *
 * -- inventory_items --
 * id INTEGER PRIMARY KEY AUTOINCREMENT
 * character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE
 * slot_index INTEGER NOT NULL
 * item_id TEXT NOT NULL
 * quantity INTEGER NOT NULL DEFAULT 1
 *
 * -- character_equipment --
 * id INTEGER PRIMARY KEY AUTOINCREMENT
 * character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE
 * slot_type TEXT NOT NULL
 * item_id TEXT NOT NULL
 *
 * -- character_action_bar --
 * character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE
 * slot_index INTEGER NOT NULL
 * skill_id TEXT NOT NULL
 * PRIMARY KEY (character_id, slot_index)
 *
 * -- character_settings -- (B-21)
 * character_id INTEGER PRIMARY KEY REFERENCES characters(id) ON DELETE CASCADE
 * ui_json TEXT NOT NULL (the client's UI settings document, a JSON object, <= 64 KB)
 * updated_at TEXT NOT NULL (ISO 8601, set by the backend on every write)
 */
export {};
