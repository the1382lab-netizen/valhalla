/**
 * Database connection singleton for Valhalla.
 * Uses sql.js (pure JS/WASM SQLite) — no native compilation required.
 *
 * Call initDatabase() once at server startup.
 * Then use getDb() anywhere to get the sql.js Database instance.
 * The database is persisted to disk on every write via saveToDisk().
 */
import type { Database as SqlJsDatabase } from 'sql.js';
/**
 * Initialize the SQLite database and create tables.
 * sql.js loads the WASM binary, then we either open an existing DB file
 * or create a new one in memory and persist it.
 */
export declare function initDatabase(filePath?: string): Promise<void>;
/**
 * Get the sql.js Database instance.
 * Throws if the database hasn't been initialized yet.
 */
export declare function getDb(): SqlJsDatabase;
/**
 * Persist the in-memory database to disk.
 * Call after any write operations.
 */
export declare function saveToDisk(): void;
/**
 * Close the database connection gracefully.
 */
export declare function closeDatabase(): void;
//# sourceMappingURL=index.d.ts.map