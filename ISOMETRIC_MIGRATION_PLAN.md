# Orthogonal → Isometric Map Conversion Plan

## Strategy

Keep all game logic (movement, collision, networking) in **orthogonal grid space** on the server. The isometric transform is a **client-side rendering concern only**. This means the server barely changes, and we avoid breaking the authoritative movement/collision systems.

The key insight: `localX`/`localY` and all server-side positions continue to be orthogonal "game world" coordinates. The client applies an isometric projection when placing sprites on screen, and an inverse projection when converting mouse/screen positions back to game coordinates.

---

## Phase 1: Shared Coordinate Utilities

**File: `shared/src/utils.ts`**

Add four new functions (the rest of the file stays the same):

```ts
/** Convert orthogonal game coords to isometric screen coords */
export function orthoToIso(x: number, y: number): { x: number; y: number } {
  return {
    x: (x - y),          // simplified: (x - y) * (tileW/2) / (tileW/2)
    y: (x + y) / 2,      // simplified: (x + y) * (tileH/2) / (tileW/2)
  };
}

/** Convert isometric screen coords back to orthogonal game coords */
export function isoToOrtho(isoX: number, isoY: number): { x: number; y: number } {
  return {
    x: (isoX / 2 + isoY),    // inverse of the above
    y: (isoY - isoX / 2),
  };
}

/** Convert tile grid (col, row) to isometric pixel position (center of diamond) */
export function tileToIso(tileX: number, tileY: number, tileW: number, tileH: number): { x: number; y: number } {
  return {
    x: (tileX - tileY) * (tileW / 2),
    y: (tileX + tileY) * (tileH / 2),
  };
}

/** Convert isometric screen pixel back to tile grid (col, row) */
export function isoToTile(isoX: number, isoY: number, tileW: number, tileH: number): { x: number; y: number } {
  return {
    x: Math.floor(isoX / tileW + isoY / tileH),
    y: Math.floor(isoY / tileH - isoX / tileW),
  };
}
```

**File: `shared/src/constants.ts`**

Add isometric tile dimensions (isometric tiles are typically 2:1 ratio):

```ts
export const ISO_TILE_WIDTH = 128;   // diamond width in pixels
export const ISO_TILE_HEIGHT = 64;   // diamond height in pixels
```

Export the new functions from `shared/src/index.ts`.

---

## Phase 2: Client Tile Rendering

**File: `client/src/scenes/GameScene.ts` — `buildTileMapFromData()`**

Change sprite placement from:
```ts
x * tileSize + tileSize / 2,
y * tileSize + tileSize / 2,
```
to:
```ts
const iso = tileToIso(x, y, ISO_TILE_WIDTH, ISO_TILE_HEIGHT);
// sprite placed at iso.x, iso.y (centered on diamond)
```

Change **depth sorting** from flat `layerDepth` to isometric depth:
```ts
sprite.setDepth(layerDepth * 1000 + (x + y));
// Tiles with higher (x + y) are "closer" to camera, drawn on top
```

Same changes in `buildTileMapLegacy()`.

**Camera bounds**: The isometric map is diamond-shaped. For a map of W×H tiles:
- Min X: `tileToIso(0, H-1).x` (left corner of diamond)
- Max X: `tileToIso(W-1, 0).x` (right corner)
- Min Y: `tileToIso(0, 0).y` (top corner)
- Max Y: `tileToIso(W-1, H-1).y` (bottom corner)

Update `cameras.main.setBounds(...)` accordingly.

---

## Phase 3: Entity Positioning (Sprites on Screen)

All entities (local player, remote players, NPCs, projectiles) store positions in orthogonal game-world coordinates. The rendering layer projects them to isometric screen space.

**File: `client/src/scenes/GameScene.ts`**

Everywhere `this.playerSprite.x = this.localX` appears, change to:
```ts
const isoPos = orthoToIso(this.localX, this.localY);
this.playerSprite.setPosition(isoPos.x, isoPos.y);
```

Same for player depth:
```ts
this.playerSprite.setDepth(10 * 1000 + (this.localX + this.localY));
```

**File: `client/src/systems/EntityRenderer.ts`**

In `addRemotePlayer()`, `update()` (interpolation loop), and all NPC/projectile rendering — apply `orthoToIso()` when setting sprite.x/sprite.y from targetX/targetY. The stored `targetX`/`targetY`/`previousX`/`previousY` remain in orthogonal space; the projection happens at render time only:

```ts
const renderX = lerp(data.previousX, data.targetX, t);
const renderY = lerp(data.previousY, data.targetY, t);
const iso = orthoToIso(renderX, renderY);
data.sprite.x = iso.x;
data.sprite.y = iso.y;
```

The aim line, HP bars, name plates, combat text — all positioned relative to the sprite's screen position, so they automatically move with it.

---

## Phase 4: Input Rotation

In an isometric view, pressing "W" (up) should move the player toward the **top of the screen**, which in orthogonal space is actually northwest (−X, −Y). We need to rotate the WASD input vector by 45°.

**Option A (recommended — client-side rotation):** Rotate in `applyInputLocally()` and on the server in `MovementSystem.processInput()`. Both places build the direction vector identically, so the change is the same in both:

**File: `client/src/scenes/GameScene.ts` — `applyInputLocally()`**
**File: `server/src/systems/MovementSystem.ts` — `processInput()`**

After building `mx`/`my` from WASD flags, rotate by 45°:
```ts
let mx = 0, my = 0;
if (input.up) my -= 1;
if (input.down) my += 1;
if (input.left) mx -= 1;
if (input.right) mx += 1;

// Rotate 45° for isometric feel
const cos45 = Math.SQRT1_2; // ~0.707
const rotX = (mx - my) * cos45;
const rotY = (mx + my) * cos45;

const dir = normalise(rotX, rotY);
```

This is the **one server-side change** — it ensures client prediction and server authority agree on movement direction. The server still operates entirely in orthogonal pixel space; we're just rotating which direction "up" means.

**Mouse aiming** (`InputManager.getInput()`): The aim angle calculation needs the inverse isometric transform. Currently it does:
```ts
const worldPoint = this.scene.cameras.main.getWorldPoint(pointer.x, pointer.y);
const aimAngle = Math.atan2(worldPoint.y - playerWorldY, worldPoint.x - playerWorldX);
```

Change to convert the camera world point (which is in isometric screen space) back to orthogonal:
```ts
const isoPoint = this.scene.cameras.main.getWorldPoint(pointer.x, pointer.y);
const ortho = isoToOrtho(isoPoint.x, isoPoint.y);
const aimAngle = Math.atan2(ortho.y - playerWorldY, ortho.x - playerWorldX);
```

(`playerWorldX`/`playerWorldY` are already in orthogonal space.)

---

## Phase 5: Tile Art (BootScene)

**File: `client/src/scenes/BootScene.ts`**

Change procedural tile textures from 64×64 squares to 128×64 diamond shapes:

```ts
const isoW = 128;
const isoH = 64;

const makeTile = (key: string, fn: (gfx: Phaser.GameObjects.Graphics) => void) => {
  const gfx = this.add.graphics();
  fn(gfx);
  gfx.generateTexture(key, isoW, isoH);
  gfx.destroy();
};
```

Each tile draws a filled diamond polygon instead of a filled rectangle:
```ts
makeTile('tile_grass_light', (g) => {
  g.fillStyle(0x2d5a27, 1);
  g.beginPath();
  g.moveTo(isoW / 2, 0);          // top
  g.lineTo(isoW, isoH / 2);       // right
  g.lineTo(isoW / 2, isoH);       // bottom
  g.lineTo(0, isoH / 2);          // left
  g.closePath();
  g.fill();
  // subtle edge line
  g.lineStyle(1, 0x245020, 0.3);
  g.strokePath();
});
```

Repeat this pattern for all ~20 tile textures. Player/projectile/NPC sprites stay the same shape (circles, etc.) — they don't need to be isometric since they're small symbolic sprites.

---

## Phase 6: Map Editor

**File: `editor/src/components/editors/maps/MapEditor.tsx`**

The canvas rendering loop currently draws orthogonal squares:
```ts
const px = tx * tw + pan.x;
const py = ty * th + pan.y;
ctx.fillRect(px, py, tw - 0.5, th - 0.5);
```

Change to draw isometric diamonds:
```ts
const isoX = (tx - ty) * (tw / 2) + pan.x + offsetX;
const isoY = (tx + ty) * (th / 2) + pan.y;
// draw diamond path
ctx.beginPath();
ctx.moveTo(isoX, isoY - th/2);
ctx.lineTo(isoX + tw/2, isoY);
ctx.lineTo(isoX, isoY + th/2);
ctx.lineTo(isoX - tw/2, isoY);
ctx.closePath();
ctx.fill(); ctx.stroke();
```

The click-to-place logic for spawn points needs `isoToTile()` to convert canvas mouse position back to tile coordinates. Spawn point rendering needs the same isometric projection.

`offsetX` centers the diamond map in the canvas: `offsetX = (mapData.height * tw) / 2`.

---

## Phase 7: Shared Type Updates

**File: `shared/src/types.ts` (or wherever MapDataPayload is defined)**

Add an optional `orientation` field to `MapDataPayload`:
```ts
orientation?: 'orthogonal' | 'isometric';
```

The server sets this when sending map data. The client reads it to decide which rendering path to use. This gives us a clean migration path — old maps can stay orthogonal, new maps can be isometric, and both work.

---

## Summary of Files Changed

| File | Change Scope |
|------|-------------|
| `shared/src/utils.ts` | Add 4 coordinate conversion functions |
| `shared/src/constants.ts` | Add ISO_TILE_WIDTH, ISO_TILE_HEIGHT |
| `shared/src/index.ts` | Export new functions |
| `shared/src/types.ts` | Add orientation to MapDataPayload |
| `client/src/scenes/GameScene.ts` | Tile placement, entity positioning, depth sort, camera bounds, aim line, input rotation |
| `client/src/scenes/BootScene.ts` | Diamond tile textures (all ~20 tiles) |
| `client/src/systems/InputManager.ts` | Inverse-iso mouse aim conversion |
| `client/src/systems/EntityRenderer.ts` | orthoToIso for all sprite positioning |
| `server/src/systems/MovementSystem.ts` | 45° WASD rotation (3 lines) |
| `editor/src/.../MapEditor.tsx` | Isometric grid rendering, click→tile conversion |

**Server collision, networking, map loading, Tiled parser — all untouched.**

---

## Implementation Order

1. **Phase 1** — Shared utils + constants (foundation, no visible change yet)
2. **Phase 5** — Tile art in BootScene (need diamond textures before rendering)
3. **Phase 2** — Tile rendering in GameScene (map appears isometric)
4. **Phase 3** — Entity positioning (players/NPCs appear in correct spots)
5. **Phase 4** — Input rotation (movement feels correct + aim works)
6. **Phase 6** — Map editor (editor matches the new view)
7. **Phase 7** — Shared types (orientation flag for future flexibility)

Each phase can be tested independently before moving to the next.
