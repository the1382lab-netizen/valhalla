/**
 * Database connection singleton for Valhalla.
 * Uses sql.js (pure JS/WASM SQLite) — no native compilation required.
 *
 * Call initDatabase() once at server startup.
 * Then use getDb() anywhere to get the sql.js Database instance.
 * The database is persisted to disk on every write via saveToDisk().
 */

import initSqlJs from 'sql.js';
import type { Database as SqlJsDatabase } from 'sql.js';
import fs from 'fs';
import path from 'path';

let db: SqlJsDatabase | null = null;
let dbPath: string = 'valhalla.db';

/**
 * Initialize the SQLite database and create tables.
 * sql.js loads the WASM binary, then we either open an existing DB file
 * or create a new one in memory and persist it.
 */
export async function initDatabase(filePath: string = 'valhalla.db'): Promise<void> {
  if (db) return; // Already initialized

  dbPath = path.resolve(filePath);

  const SQL = await initSqlJs();

  // Load existing database file if it exists
  if (fs.existsSync(dbPath)) {
    const fileBuffer = fs.readFileSync(dbPath);
    db = new SQL.Database(fileBuffer);
    console.log(`[DB] Loaded existing database from ${dbPath}`);
  } else {
    db = new SQL.Database();
    console.log(`[DB] Created new database at ${dbPath}`);
  }

  // Enable foreign keys
  db.run('PRAGMA foreign_keys = ON;');

  // Create tables if they don't exist
  db.run(`
    CREATE TABLE IF NOT EXISTS users (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      username TEXT NOT NULL UNIQUE,
      password_hash TEXT NOT NULL,
      created_at INTEGER NOT NULL
    );
  `);

  db.run(`
    CREATE TABLE IF NOT EXISTS characters (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      user_id INTEGER NOT NULL REFERENCES users(id),
      name TEXT NOT NULL UNIQUE,
      class_id TEXT NOT NULL,
      level INTEGER NOT NULL DEFAULT 1,
      xp INTEGER NOT NULL DEFAULT 0,
      hp INTEGER NOT NULL,
      mana INTEGER NOT NULL,
      position_x REAL NOT NULL,
      position_y REAL NOT NULL,
      zone_id TEXT NOT NULL DEFAULT 'grasslands',
      alive INTEGER NOT NULL DEFAULT 1,
      created_at INTEGER NOT NULL,
      updated_at INTEGER NOT NULL
    );
  `);

  db.run(`
    CREATE TABLE IF NOT EXISTS inventory_items (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
      slot_index INTEGER NOT NULL,
      item_id TEXT NOT NULL,
      quantity INTEGER NOT NULL DEFAULT 1
    );
  `);

  db.run(`
    CREATE TABLE IF NOT EXISTS character_equipment (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
      slot_type TEXT NOT NULL,
      item_id TEXT NOT NULL
    );
  `);

  db.run(`
    CREATE TABLE IF NOT EXISTS character_action_bar (
      character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
      slot_index INTEGER NOT NULL,
      skill_id TEXT NOT NULL,
      PRIMARY KEY (character_id, slot_index)
    );
  `);

  // ── Migrations for databases created before a column existed ──
  // SQLite has no `ADD COLUMN IF NOT EXISTS`, so check the table info first.
  const charCols = db.exec('PRAGMA table_info(characters);');
  const charColNames = charCols.length
    ? charCols[0].values.map(row => String(row[1]))
    : [];
  if (!charColNames.includes('body_id')) {
    db.run("ALTER TABLE characters ADD COLUMN body_id TEXT NOT NULL DEFAULT '';");
    console.log('[db] migrated: characters.body_id');
  }

  // Create indexes
  db.run('CREATE INDEX IF NOT EXISTS idx_characters_user_id ON characters(user_id);');
  db.run('CREATE INDEX IF NOT EXISTS idx_inventory_character_id ON inventory_items(character_id);');
  db.run('CREATE INDEX IF NOT EXISTS idx_equipment_character_id ON character_equipment(character_id);');
  db.run('CREATE INDEX IF NOT EXISTS idx_action_bar_character_id ON character_action_bar(character_id);');

  // ── Migrations ──
  // Fix legacy 'main' zone_id to 'grasslands'
  db.run("UPDATE characters SET zone_id = 'grasslands' WHERE zone_id = 'main'");

  saveToDisk();
  console.log(`[DB] SQLite database initialized (sql.js WASM)`);
}

/**
 * Get the sql.js Database instance.
 * Throws if the database hasn't been initialized yet.
 */
export function getDb(): SqlJsDatabase {
  if (!db) {
    throw new Error('Database not initialized. Call initDatabase() first.');
  }
  return db;
}

/**
 * Persist the in-memory database to disk.
 * Call after any write operations.
 */
export function saveToDisk(): void {
  if (!db) return;
  const data = db.export();
  const buffer = Buffer.from(data);
  fs.writeFileSync(dbPath, buffer);
}

/**
 * Close the database connection gracefully.
 */
export function closeDatabase(): void {
  if (db) {
    saveToDisk();
    db.close();
    db = null;
    console.log('[DB] Database connection closed.');
  }
}
