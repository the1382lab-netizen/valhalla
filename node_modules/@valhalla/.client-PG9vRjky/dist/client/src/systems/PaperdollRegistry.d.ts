import Phaser from 'phaser';
import type { PaperdollManifest, PaperdollAnim, PaperdollDir, PaperdollLayerEntry } from '@valhalla/shared';
/**
 * Paperdoll layer registry.
 *
 * A character is a base body sprite plus one overlay sprite per equipped item,
 * all playing the same animation key in lockstep. Occlusion is baked into each
 * item sheet, so the runtime never sorts anything — it only needs the composite
 * order, and the only layer that moves is `back` (a cloak hangs behind you
 * facing south and in front of your armour facing north).
 */
export declare class PaperdollRegistry {
    private static _instance;
    private manifest;
    private itemsById;
    private bodiesById;
    /** direction -> slot -> index in the composite order */
    private orderIndex;
    static get instance(): PaperdollRegistry;
    /** True once the manifest has been parsed. Everything no-ops until then. */
    get ready(): boolean;
    get cell(): number;
    get originY(): number;
    get originX(): number;
    get all(): PaperdollManifest | null;
    getItem(layerId: string): PaperdollLayerEntry | undefined;
    hasBody(bodyId: string): boolean;
    /** Resolve a character's body, falling back through class default to the pack default. */
    resolveBody(bodyId?: string | null): string | null;
    /**
     * Where this slot sits in the composite order for a direction. Used as a
     * fractional depth offset above the body, so layers stack correctly without
     * bleeding into the next entity's depth band.
     */
    depthOffsetFor(dir: PaperdollDir, slot: string): number;
    /**
     * Queue the manifest, then the sheets it references, then register the
     * animations. Resolves once every layer is ready to play.
     *
     * `neededItemIds` limits the item sheets to those the item catalog actually
     * references, so an unused half of the pack is never downloaded.
     */
    load(scene: Phaser.Scene, neededItemIds: Set<string>): Promise<void>;
    private loadSheets;
    /** 40 animations per layer: five cycles x eight directions. */
    private registerAnims;
}
export interface ParsedPaperdollKey {
    layerId: string;
    anim: PaperdollAnim;
    dir: PaperdollDir;
}
export declare function parsePaperdollAnimKey(key: string): ParsedPaperdollKey | null;
//# sourceMappingURL=PaperdollRegistry.d.ts.map