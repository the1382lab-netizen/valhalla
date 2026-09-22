import { PAPERDOLL_ANIMS, PAPERDOLL_DIRECTIONS, PAPERDOLL_MANIFEST_URL, PAPERDOLL_ASSET_BASE, paperdollAnimKey, paperdollTextureKey, paperdollFrame, DEFAULT_BODY_ID, } from '@valhalla/shared';
/**
 * Paperdoll layer registry.
 *
 * A character is a base body sprite plus one overlay sprite per equipped item,
 * all playing the same animation key in lockstep. Occlusion is baked into each
 * item sheet, so the runtime never sorts anything — it only needs the composite
 * order, and the only layer that moves is `back` (a cloak hangs behind you
 * facing south and in front of your armour facing north).
 */
export class PaperdollRegistry {
    static _instance = null;
    manifest = null;
    itemsById = new Map();
    bodiesById = new Map();
    /** direction -> slot -> index in the composite order */
    orderIndex = new Map();
    static get instance() {
        if (!PaperdollRegistry._instance)
            PaperdollRegistry._instance = new PaperdollRegistry();
        return PaperdollRegistry._instance;
    }
    /** True once the manifest has been parsed. Everything no-ops until then. */
    get ready() { return this.manifest !== null; }
    get cell() { return this.manifest?.cell ?? 64; }
    get originY() { return this.manifest?.originY ?? 0.9167; }
    get originX() { return this.manifest?.originX ?? 0.5; }
    get all() { return this.manifest; }
    getItem(layerId) { return this.itemsById.get(layerId); }
    hasBody(bodyId) { return this.bodiesById.has(bodyId); }
    /** Resolve a character's body, falling back through class default to the pack default. */
    resolveBody(bodyId) {
        if (!this.manifest)
            return null;
        if (bodyId && this.bodiesById.has(bodyId))
            return bodyId;
        if (this.bodiesById.has(DEFAULT_BODY_ID))
            return DEFAULT_BODY_ID;
        return this.manifest.bodies[0]?.id ?? null;
    }
    /**
     * Where this slot sits in the composite order for a direction. Used as a
     * fractional depth offset above the body, so layers stack correctly without
     * bleeding into the next entity's depth band.
     */
    depthOffsetFor(dir, slot) {
        const perDir = this.orderIndex.get(dir);
        const idx = perDir?.get(slot);
        if (idx === undefined)
            return 0.5;
        // 9 entries max; keep every offset strictly inside (0, 1)
        return (idx + 1) / 16;
    }
    // ── Loading ──────────────────────────────────────────────────────────
    /**
     * Queue the manifest, then the sheets it references, then register the
     * animations. Resolves once every layer is ready to play.
     *
     * `neededItemIds` limits the item sheets to those the item catalog actually
     * references, so an unused half of the pack is never downloaded.
     */
    load(scene, neededItemIds) {
        return new Promise((resolve) => {
            scene.load.json('paperdoll_manifest', PAPERDOLL_MANIFEST_URL);
            scene.load.once('complete', () => {
                const manifest = scene.cache.json.get('paperdoll_manifest');
                if (!manifest) {
                    console.warn('[Paperdoll] manifest missing — falling back to LPC sprites');
                    resolve();
                    return;
                }
                this.manifest = manifest;
                for (const b of manifest.bodies)
                    this.bodiesById.set(b.id, b);
                for (const i of manifest.items)
                    this.itemsById.set(i.id, i);
                for (const dir of manifest.directions) {
                    const order = manifest.layerOrder[dir] ?? [];
                    const m = new Map();
                    order.forEach((slot, i) => m.set(slot, i));
                    this.orderIndex.set(dir, m);
                }
                this.loadSheets(scene, neededItemIds).then(resolve);
            });
            scene.load.start();
        });
    }
    loadSheets(scene, neededItemIds) {
        const m = this.manifest;
        const wanted = [
            ...m.bodies,
            ...m.items.filter(i => neededItemIds.has(i.id)),
        ];
        let queued = 0;
        for (const entry of wanted) {
            const key = paperdollTextureKey(entry.id);
            if (scene.textures.exists(key))
                continue;
            scene.load.atlas(key, `${PAPERDOLL_ASSET_BASE}/${entry.sheet}`, `${PAPERDOLL_ASSET_BASE}/${entry.atlas}`);
            queued++;
        }
        if (queued === 0) {
            this.registerAnims(scene, wanted);
            return Promise.resolve();
        }
        return new Promise((resolve) => {
            scene.load.once('complete', () => {
                this.registerAnims(scene, wanted);
                console.log(`[Paperdoll] ${wanted.length} layers ready (${m.bodies.length} bodies, ${wanted.length - m.bodies.length} items)`);
                resolve();
            });
            scene.load.start();
        });
    }
    /** 40 animations per layer: five cycles x eight directions. */
    registerAnims(scene, entries) {
        for (const entry of entries) {
            const texture = paperdollTextureKey(entry.id);
            if (!scene.textures.exists(texture))
                continue;
            for (const animName of Object.keys(PAPERDOLL_ANIMS)) {
                const cfg = PAPERDOLL_ANIMS[animName];
                for (const dir of PAPERDOLL_DIRECTIONS) {
                    const key = paperdollAnimKey(entry.id, animName, dir);
                    if (scene.anims.exists(key))
                        continue;
                    scene.anims.create({
                        key,
                        frames: Array.from({ length: cfg.frames }, (_, i) => ({
                            key: texture, frame: paperdollFrame(animName, dir, i),
                        })),
                        frameRate: cfg.frameRate,
                        repeat: cfg.repeat,
                    });
                }
            }
        }
    }
}
// ── Animation key helpers ───────────────────────────────────────────────
//
// Paperdoll keys are `pd_<layerId>_<anim>_<dir>`. Layer ids contain
// underscores, so parse from the right, never the left.
const DIR_SET = new Set(PAPERDOLL_DIRECTIONS);
const ANIM_SET = new Set(Object.keys(PAPERDOLL_ANIMS));
export function parsePaperdollAnimKey(key) {
    if (!key.startsWith('pd_'))
        return null;
    const parts = key.split('_');
    if (parts.length < 4)
        return null;
    const dir = parts[parts.length - 1];
    const anim = parts[parts.length - 2];
    if (!DIR_SET.has(dir) || !ANIM_SET.has(anim))
        return null;
    return {
        layerId: parts.slice(1, parts.length - 2).join('_'),
        anim: anim,
        dir: dir,
    };
}
//# sourceMappingURL=PaperdollRegistry.js.map