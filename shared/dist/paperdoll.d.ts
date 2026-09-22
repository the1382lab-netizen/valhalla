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
export declare const PAPERDOLL_DIRECTIONS: readonly ["s", "se", "e", "ne", "n", "nw", "w", "sw"];
export type PaperdollDir = (typeof PAPERDOLL_DIRECTIONS)[number];
/** Column layout, identical in every sheet. */
export declare const PAPERDOLL_ANIMS: {
    readonly idle: {
        readonly start: 0;
        readonly frames: 2;
        readonly frameRate: 2;
        readonly repeat: -1;
    };
    readonly walk: {
        readonly start: 2;
        readonly frames: 6;
        readonly frameRate: 10;
        readonly repeat: -1;
    };
    readonly attack: {
        readonly start: 8;
        readonly frames: 4;
        readonly frameRate: 12;
        readonly repeat: 0;
    };
    readonly shoot: {
        readonly start: 12;
        readonly frames: 4;
        readonly frameRate: 12;
        readonly repeat: 0;
    };
    readonly cast: {
        readonly start: 16;
        readonly frames: 4;
        readonly frameRate: 8;
        readonly repeat: 0;
    };
};
export type PaperdollAnim = keyof typeof PAPERDOLL_ANIMS;
/** Visual layer slots, bottom to top. `body` is the base, the rest are items. */
export declare const PAPERDOLL_SLOTS: readonly ["back", "legs", "boots", "chest", "gloves", "helm", "offhand", "mainhand"];
export type PaperdollSlot = (typeof PAPERDOLL_SLOTS)[number];
/** How a weapon looks and, from that, which attack cycle it plays. */
export declare enum WeaponStyle {
    SWORD = "sword",
    GREATSWORD = "greatsword",
    MACE = "mace",
    BOW = "bow",
    STAFF = "staff"
}
export declare const WEAPON_STYLE_ANIM: Record<WeaponStyle, PaperdollAnim>;
/** The attack cycle for a weapon style; melee swing when nothing is equipped. */
export declare function attackAnimFor(style?: string | null): PaperdollAnim;
/** A screen-space vector to a sheet row. +y is down the screen (south). */
export declare function dirFromScreenVector(dx: number, dy: number): PaperdollDir | null;
/**
 * A movement/aim vector in ORTHOGONAL world space to a sheet row.
 *
 * All game state is stored in ortho coordinates and only projected to iso when
 * a sprite position is assigned, so a facing must be projected the same way
 * before it can pick a row: the iso transform is linear, so applying it to the
 * delta is exactly `orthoToIso(dx, dy)` with the translation dropped.
 */
export declare function dirFromOrthoVector(dx: number, dy: number): PaperdollDir | null;
/** Frame name in the generated atlases, e.g. `walk_se_03`. */
export declare function paperdollFrame(anim: PaperdollAnim, dir: PaperdollDir, i: number): string;
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
    animations: {
        name: PaperdollAnim;
        start: number;
        frames: number;
    }[];
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
        id: string;
        label: string;
        cls: string;
        tier: number;
        body: string;
        slots: Partial<Record<PaperdollSlot, string>>;
    }[];
}
export declare const PAPERDOLL_MANIFEST_URL = "assets/sprites/paperdoll/manifest.json";
export declare const PAPERDOLL_ASSET_BASE = "assets/sprites/paperdoll";
/** Texture key for a paperdoll layer. Namespaced so it can't collide with LPC. */
export declare function paperdollTextureKey(layerId: string): string;
/** Animation key for a paperdoll layer. */
export declare function paperdollAnimKey(layerId: string, anim: PaperdollAnim, dir: PaperdollDir): string;
/** Default body when a character has not chosen one. */
export declare const DEFAULT_BODY_ID = "body_tan";
//# sourceMappingURL=paperdoll.d.ts.map