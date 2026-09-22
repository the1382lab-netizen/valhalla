/**
 * Paperdoll character sprites — shared vocabulary.
 *
 * A character is drawn as a base body sheet plus one sheet per equipped item.
 * Every sheet uses the identical frame grid, so all the layers play the same
 * animation key in lockstep.
 *
 *   assets/sprites/paperdoll/manifest.json   generated index of every layer
 *   assets/sprites/paperdoll/bodies/*.png    base bodies
 *   assets/sprites/paperdoll/items/*.png     equipment layers
 *
 * Regenerate with `tools/paperdoll-gen/generate.sh`.
 */

/** Screen-space facing, in sheet row order. `s` faces the camera. */
export const PAPERDOLL_DIRECTIONS = ['s', 'se', 'e', 'ne', 'n', 'nw', 'w', 'sw'] as const;
export type PaperdollDir = (typeof PAPERDOLL_DIRECTIONS)[number];

/** Column layout, identical in every sheet. */
export const PAPERDOLL_ANIMS = {
  idle:   { start: 0,  frames: 2, frameRate: 2,  repeat: -1 },
  walk:   { start: 2,  frames: 6, frameRate: 10, repeat: -1 },
  attack: { start: 8,  frames: 4, frameRate: 12, repeat: 0  },
  shoot:  { start: 12, frames: 4, frameRate: 12, repeat: 0  },
  cast:   { start: 16, frames: 4, frameRate: 8,  repeat: 0  },
} as const;
export type PaperdollAnim = keyof typeof PAPERDOLL_ANIMS;

/** Visual layer slots, bottom to top. `body` is the base, the rest are items. */
export const PAPERDOLL_SLOTS = [
  'back', 'legs', 'boots', 'chest', 'gloves', 'helm', 'offhand', 'mainhand',
] as const;
export type PaperdollSlot = (typeof PAPERDOLL_SLOTS)[number];

/** How a weapon looks and, from that, which attack cycle it plays. */
export enum WeaponStyle {
  SWORD = 'sword',
  GREATSWORD = 'greatsword',
  MACE = 'mace',
  BOW = 'bow',
  STAFF = 'staff',
}

export const WEAPON_STYLE_ANIM: Record<WeaponStyle, PaperdollAnim> = {
  [WeaponStyle.SWORD]: 'attack',
  [WeaponStyle.GREATSWORD]: 'attack',
  [WeaponStyle.MACE]: 'attack',
  [WeaponStyle.BOW]: 'shoot',
  [WeaponStyle.STAFF]: 'cast',
};

/** The attack cycle for a weapon style; melee swing when nothing is equipped. */
export function attackAnimFor(style?: string | null): PaperdollAnim {
  return WEAPON_STYLE_ANIM[style as WeaponStyle] ?? 'attack';
}

// ── Facing ────────────────────────────────────────────────────────────────
//
// THE one place a direction is decided. Everything that needs a facing — the
// local player, remote players, combat, NPCs — goes through here, so they can
// never disagree with each other again.

const DIR_ORDER: PaperdollDir[] = ['e', 'se', 's', 'sw', 'w', 'nw', 'n', 'ne'];

/** A screen-space vector to a sheet row. +y is down the screen (south). */
export function dirFromScreenVector(dx: number, dy: number): PaperdollDir | null {
  if (dx === 0 && dy === 0) return null;
  const i = Math.round(Math.atan2(dy, dx) / (Math.PI / 4));
  return DIR_ORDER[((i % 8) + 8) % 8];
}

/**
 * A movement/aim vector in ORTHOGONAL world space to a sheet row.
 *
 * All game state is stored in ortho coordinates and only projected to iso when
 * a sprite position is assigned, so a facing must be projected the same way
 * before it can pick a row: the iso transform is linear, so applying it to the
 * delta is exactly `orthoToIso(dx, dy)` with the translation dropped.
 */
export function dirFromOrthoVector(dx: number, dy: number): PaperdollDir | null {
  return dirFromScreenVector(dx - dy, (dx + dy) / 2);
}

/** Frame name in the generated atlases, e.g. `walk_se_03`. */
export function paperdollFrame(anim: PaperdollAnim, dir: PaperdollDir, i: number): string {
  return `${anim}_${dir}_${String(i).padStart(2, '0')}`;
}

// ── Manifest ──────────────────────────────────────────────────────────────

export interface PaperdollLayerEntry {
  id: string;
  label: string;
  sheet: string;
  atlas: string;
  /** items only */
  slot?: PaperdollSlot;
  style?: string | null;
  icon?: string;
}

export interface PaperdollManifest {
  version: number;
  /** Frame size in px; sheets are 20 columns x 8 rows of these. */
  cell: number;
  /** Sprite origin so the character's feet land on the tile it occupies. */
  originX: number;
  originY: number;
  directions: PaperdollDir[];
  animations: { name: PaperdollAnim; start: number; frames: number }[];
  slots: PaperdollSlot[];
  /**
   * Composite order per direction. Occlusion is baked into each item sheet, so
   * this only ever moves `back`: a cloak hangs behind you facing south and in
   * front of your armour facing north.
   */
  layerOrder: Record<PaperdollDir, string[]>;
  bodies: PaperdollLayerEntry[];
  items: PaperdollLayerEntry[];
  presets: {
    id: string; label: string; cls: string; tier: number;
    body: string; slots: Partial<Record<PaperdollSlot, string>>;
  }[];
}

export const PAPERDOLL_MANIFEST_URL = 'assets/sprites/paperdoll/manifest.json';
export const PAPERDOLL_ASSET_BASE = 'assets/sprites/paperdoll';

/** Texture key for a paperdoll layer. Namespaced so it can't collide with LPC. */
export function paperdollTextureKey(layerId: string): string {
  return `pd_${layerId}`;
}

/** Animation key for a paperdoll layer. */
export function paperdollAnimKey(layerId: string, anim: PaperdollAnim, dir: PaperdollDir): string {
  return `pd_${layerId}_${anim}_${dir}`;
}

/** Default body when a character has not chosen one. */
export const DEFAULT_BODY_ID = 'body_tan';
