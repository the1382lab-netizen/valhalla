/**
 * Per-character UI settings (B-21): the game client's HUD layout, style, chat
 * and nameplate options, saved when the player changes them and loaded when
 * the character enters the world.
 *
 * The document is the client's `FValhallaUserUISettings` JSON
 * (Valhalla2/Source/ValhallaCore/Public/ValhallaUserUISettings.h documents the
 * format). The backend does not interpret it: it checks that it is a JSON
 * object no bigger than SETTINGS_MAX_BYTES, stores it verbatim in
 * `character_settings.ui_json`, and stamps `updated_at` (ISO 8601).
 */

import { getDb, saveToDisk } from '../db/index.js';

/** The largest `ui` document accepted, bytes of its JSON text. */
export const SETTINGS_MAX_BYTES = 64 * 1024;

export interface CharacterSettings {
  ui: Record<string, unknown>;
  /** ISO 8601, when the backend last stored it. */
  updatedAt: string;
}

export class SettingsValidationError extends Error {
  constructor(message: string, readonly status: number) {
    super(message);
  }
}

/** True when the character exists and belongs to `userId`. */
export function characterBelongsTo(characterId: number, userId: number): boolean {
  const result = getDb().exec('SELECT 1 FROM characters WHERE id = ? AND user_id = ?', [characterId, userId]);
  return result.length > 0 && result[0].values.length > 0;
}

/** The stored settings, or null when nothing has been saved for the character. */
export function getCharacterSettings(characterId: number): CharacterSettings | null {
  const result = getDb().exec('SELECT ui_json, updated_at FROM character_settings WHERE character_id = ?', [characterId]);
  if (result.length === 0 || result[0].values.length === 0) return null;
  const [uiJson, updatedAt] = result[0].values[0];
  let ui: Record<string, unknown> = {};
  try {
    const parsed = JSON.parse(String(uiJson));
    if (parsed && typeof parsed === 'object' && !Array.isArray(parsed)) ui = parsed;
  } catch {
    // A row that no longer parses reads as empty settings: the client's defaults.
  }
  return { ui, updatedAt: String(updatedAt) };
}

/**
 * Check a PUT body's `ui` and return its JSON text.
 * @throws SettingsValidationError (400 not an object, 413 too big).
 */
export function validateSettingsDocument(ui: unknown): string {
  if (!ui || typeof ui !== 'object' || Array.isArray(ui)) {
    throw new SettingsValidationError('Body must be { "ui": { ... } } with ui a JSON object.', 400);
  }
  const text = JSON.stringify(ui);
  const bytes = Buffer.byteLength(text, 'utf8');
  if (bytes > SETTINGS_MAX_BYTES) {
    throw new SettingsValidationError(`Settings are ${bytes} bytes; the limit is ${SETTINGS_MAX_BYTES}.`, 413);
  }
  return text;
}

/** Insert or replace the character's settings. Returns the new updatedAt. */
export function saveCharacterSettings(characterId: number, uiJson: string): string {
  const updatedAt = new Date().toISOString();
  getDb().run(
    `INSERT INTO character_settings (character_id, ui_json, updated_at) VALUES (?, ?, ?)
     ON CONFLICT(character_id) DO UPDATE SET ui_json = excluded.ui_json, updated_at = excluded.updated_at`,
    [characterId, uiJson, updatedAt],
  );
  saveToDisk();
  return updatedAt;
}
