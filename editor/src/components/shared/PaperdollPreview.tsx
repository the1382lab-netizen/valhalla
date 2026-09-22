import React, { useEffect, useRef, useState } from 'react';

/**
 * Live paperdoll preview: the real body sheet with the real item layer drawn
 * over it, animating, so you can see what a piece of gear actually looks like
 * on a character instead of guessing from a corner of its sheet.
 */

const BASE = '/assets/sprites/paperdoll';

export interface PaperdollManifest {
  cell: number;
  originX: number;
  originY: number;
  directions: string[];
  animations: { name: string; start: number; frames: number }[];
  slots: string[];
  layerOrder: Record<string, string[]>;
  bodies: { id: string; label: string; sheet: string }[];
  items: { id: string; label: string; slot: string; sheet: string; style?: string | null }[];
}

let manifestPromise: Promise<PaperdollManifest | null> | null = null;

/** Fetched once and shared by every preview on the page. */
export function usePaperdollManifest(): PaperdollManifest | null {
  const [manifest, setManifest] = useState<PaperdollManifest | null>(null);
  useEffect(() => {
    if (!manifestPromise) {
      manifestPromise = fetch(`${BASE}/manifest.json`)
        .then(r => (r.ok ? r.json() : null))
        .catch(() => null);
    }
    let live = true;
    manifestPromise.then(m => { if (live) setManifest(m); });
    return () => { live = false; };
  }, []);
  return manifest;
}

const imgCache = new Map<string, HTMLImageElement>();
function sheetImage(sheet: string): HTMLImageElement {
  let im = imgCache.get(sheet);
  if (!im) {
    im = new Image();
    im.src = `${BASE}/${sheet}`;
    imgCache.set(sheet, im);
  }
  return im;
}

interface Props {
  manifest: PaperdollManifest | null;
  /** Layer id to feature, e.g. `chest_iron_plate`. */
  layerId?: string;
  bodyId?: string;
  anim?: string;
  dir?: string;
  scale?: number;
  /** Draw the body underneath. Off gives you the bare layer. */
  showBody?: boolean;
}

export const PaperdollPreview: React.FC<Props> = ({
  manifest, layerId, bodyId = 'body_tan', anim = 'walk', dir = 'se',
  scale = 3, showBody = true,
}) => {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const frameRef = useRef(0);

  useEffect(() => {
    if (!manifest) return;
    const cv = canvasRef.current;
    if (!cv) return;
    const ctx = cv.getContext('2d');
    if (!ctx) return;

    const cell = manifest.cell;
    cv.width = cell * scale;
    cv.height = cell * scale;
    ctx.imageSmoothingEnabled = false;

    const cfg = manifest.animations.find(a => a.name === anim) ?? manifest.animations[0];
    const row = Math.max(0, manifest.directions.indexOf(dir));
    const body = manifest.bodies.find(b => b.id === bodyId) ?? manifest.bodies[0];
    const item = layerId ? manifest.items.find(i => i.id === layerId) : undefined;

    let raf = 0;
    let last = performance.now();
    const step = 1000 / (anim === 'idle' ? 2 : anim === 'walk' ? 10 : 12);

    const draw = (t: number) => {
      if (t - last > step) {
        last = t;
        frameRef.current = (frameRef.current + 1) % cfg.frames;
      }
      const col = cfg.start + frameRef.current;
      ctx.clearRect(0, 0, cv.width, cv.height);

      // Order matters only for `back`; everything else is pre-occluded.
      const order = manifest.layerOrder[dir] ?? [];
      const itemFirst = item ? order.indexOf(item.slot) < order.indexOf('body') : false;

      const drawSheet = (sheet: string) => {
        const im = sheetImage(sheet);
        if (!im.complete || im.naturalWidth === 0) return;
        ctx.drawImage(im, col * cell, row * cell, cell, cell, 0, 0, cv.width, cv.height);
      };

      if (item && itemFirst) drawSheet(item.sheet);
      if (showBody && body) drawSheet(body.sheet);
      if (item && !itemFirst) drawSheet(item.sheet);

      raf = requestAnimationFrame(draw);
    };
    raf = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(raf);
  }, [manifest, layerId, bodyId, anim, dir, scale, showBody]);

  if (!manifest) {
    return (
      <div style={{
        width: 64 * scale, height: 64 * scale, border: '1px solid #444', borderRadius: 4,
        background: '#1b1b1b', color: '#666', fontSize: 11, display: 'flex',
        alignItems: 'center', justifyContent: 'center', textAlign: 'center', padding: 8,
      }}>
        paperdoll manifest not found
      </div>
    );
  }

  return (
    <canvas
      ref={canvasRef}
      style={{
        imageRendering: 'pixelated',
        border: '1px solid #444',
        borderRadius: 4,
        background: 'radial-gradient(ellipse at 50% 78%, #343b47 0%, #232830 62%)',
      }}
    />
  );
};
