/**
 * FallbackMapGenerator — generates a ParsedMapData programmatically
 * when no Tiled JSON file is available. Useful for development and testing.
 */

import { ParsedMapData, TileLayerInfo, TilesetInfo, SpawnPointData, ZoneConnection } from './maps.js';

// Tile GIDs matching our embedded tileset (1-indexed)
const TILE = {
  GRASS_LIGHT: 1,
  GRASS_DARK: 2,
  DIRT: 3,
  WATER: 4,
  STONE_WALL: 5,
  WOOD_FENCE: 6,
  TREE: 7,
  STONE_FLOOR: 8,
  PORTAL: 9,
  VOID: 10,
};

/**
 * Generate a fallback map with the given dimensions.
 * Creates a simple outdoor zone with a town area, open fields, some trees, and a lake.
 */
export function generateFallbackMap(
  width: number = 64,
  height: number = 64,
  tileSize: number = 64,
): ParsedMapData {
  const total = width * height;

  // Ground layer — all grass with some variation
  const groundData = new Array(total).fill(TILE.GRASS_LIGHT);
  // Walls/structures layer — starts empty
  const wallsData = new Array(total).fill(0);
  // Collision grid
  const collisionGrid = new Array(total).fill(0);

  const set = (layer: number[], x: number, y: number, gid: number) => {
    if (x >= 0 && x < width && y >= 0 && y < height) {
      layer[y * width + x] = gid;
    }
  };

  const setCollision = (x: number, y: number) => {
    if (x >= 0 && x < width && y >= 0 && y < height) {
      collisionGrid[y * width + x] = 1;
    }
  };

  // ── Perimeter walls ─────────────────────────────────────
  for (let x = 0; x < width; x++) {
    for (let y = 0; y < height; y++) {
      if (x === 0 || x === width - 1 || y === 0 || y === height - 1) {
        set(wallsData, x, y, TILE.STONE_WALL);
        setCollision(x, y);
      }
    }
  }

  // ── Grass variation (scattered dark grass patches) ──────
  const seededRandom = createSeededRandom(12345);
  for (let i = 0; i < total; i++) {
    if (seededRandom() < 0.3) {
      groundData[i] = TILE.GRASS_DARK;
    }
  }

  // ── Dirt paths ──────────────────────────────────────────
  // Horizontal path from town to east
  for (let x = 5; x < width - 5; x++) {
    set(groundData, x, 10, TILE.DIRT);
    set(groundData, x, 11, TILE.DIRT);
  }
  // Vertical path from north to south
  for (let y = 5; y < height - 5; y++) {
    set(groundData, 10, y, TILE.DIRT);
    set(groundData, 11, y, TILE.DIRT);
  }

  // ── Town area (NW corner, stone floor) ──────────────────
  for (let x = 3; x <= 18; x++) {
    for (let y = 3; y <= 18; y++) {
      set(groundData, x, y, TILE.STONE_FLOOR);
    }
  }
  // Town walls (partial enclosure)
  for (let x = 3; x <= 18; x++) {
    set(wallsData, x, 3, TILE.STONE_WALL);
    setCollision(x, 3);
    set(wallsData, x, 18, TILE.STONE_WALL);
    setCollision(x, 18);
  }
  for (let y = 3; y <= 18; y++) {
    set(wallsData, 3, y, TILE.STONE_WALL);
    setCollision(3, y);
    set(wallsData, 18, y, TILE.STONE_WALL);
    setCollision(18, y);
  }
  // Town gates (openings)
  set(wallsData, 10, 18, 0); collisionGrid[18 * width + 10] = 0;
  set(wallsData, 11, 18, 0); collisionGrid[18 * width + 11] = 0;
  set(wallsData, 18, 10, 0); collisionGrid[10 * width + 18] = 0;
  set(wallsData, 18, 11, 0); collisionGrid[11 * width + 18] = 0;

  // ── A building inside town ──────────────────────────────
  for (let x = 5; x <= 9; x++) {
    set(wallsData, x, 5, TILE.STONE_WALL); setCollision(x, 5);
    set(wallsData, x, 8, TILE.STONE_WALL); setCollision(x, 8);
  }
  for (let y = 5; y <= 8; y++) {
    set(wallsData, 5, y, TILE.STONE_WALL); setCollision(5, y);
    set(wallsData, 9, y, TILE.STONE_WALL); setCollision(9, y);
  }
  // Door
  set(wallsData, 7, 8, 0); collisionGrid[8 * width + 7] = 0;

  // ── Forest area (NE) — scattered trees ──────────────────
  for (let x = 35; x <= 58; x++) {
    for (let y = 3; y <= 25; y++) {
      if (seededRandom() < 0.35) {
        set(wallsData, x, y, TILE.TREE);
        setCollision(x, y);
      }
    }
  }

  // ── Lake area (SE) — water tiles ────────────────────────
  for (let x = 38; x <= 55; x++) {
    for (let y = 40; y <= 55; y++) {
      const dx = x - 46.5;
      const dy = y - 47.5;
      if (dx * dx / 64 + dy * dy / 64 < 1) {
        set(groundData, x, y, TILE.WATER);
        setCollision(x, y);
      }
    }
  }

  // ── Cave entrance (south side) — portal area ───────────
  for (let x = 28; x <= 34; x++) {
    set(groundData, x, height - 3, TILE.STONE_FLOOR);
    set(groundData, x, height - 4, TILE.STONE_FLOOR);
  }
  set(wallsData, 28, height - 5, TILE.STONE_WALL); setCollision(28, height - 5);
  set(wallsData, 34, height - 5, TILE.STONE_WALL); setCollision(34, height - 5);
  for (let x = 28; x <= 34; x++) {
    set(wallsData, x, height - 5, TILE.STONE_WALL); setCollision(x, height - 5);
  }
  // Opening in the wall for cave entrance
  set(wallsData, 31, height - 5, 0); collisionGrid[(height - 5) * width + 31] = 0;
  set(wallsData, 30, height - 5, 0); collisionGrid[(height - 5) * width + 30] = 0;

  // ── Some scattered rocks / fences in the grasslands ─────
  for (let i = 0; i < 15; i++) {
    const rx = Math.floor(seededRandom() * (width - 10)) + 5;
    const ry = Math.floor(seededRandom() * (height - 25)) + 20;
    if (collisionGrid[ry * width + rx] === 0 && groundData[ry * width + rx] !== TILE.WATER) {
      set(wallsData, rx, ry, TILE.WOOD_FENCE);
      setCollision(rx, ry);
    }
  }

  // ── Tile layers ─────────────────────────────────────────
  const tileLayers: TileLayerInfo[] = [
    { name: 'ground', data: groundData, width, height, visible: true, opacity: 1 },
    { name: 'walls', data: wallsData, width, height, visible: true, opacity: 1 },
    { name: 'collision', data: collisionGrid.map((c) => (c ? TILE.VOID : 0)), width, height, visible: false, opacity: 1 },
  ];

  // ── Tilesets ────────────────────────────────────────────
  const tilesets: TilesetInfo[] = [
    {
      firstGid: 1,
      name: 'default',
      tileCount: 10,
      tileWidth: tileSize,
      tileHeight: tileSize,
      tileProperties: {},
    },
  ];

  // ── Spawn points ────────────────────────────────────────
  const spawnPoints: SpawnPointData[] = [
    {
      id: 'player_spawn',
      type: 'player_spawn',
      x: 10 * tileSize + tileSize / 2,
      y: 10 * tileSize + tileSize / 2,
      properties: {},
    },
    // Enemy spawns in grasslands
    {
      id: 'enemy_spawn_field_1',
      type: 'enemy_spawn',
      x: 25 * tileSize,
      y: 25 * tileSize,
      width: 10 * tileSize,
      height: 10 * tileSize,
      properties: { enemyType: 'rat', maxCount: 5, respawnMs: 15000 },
    },
    {
      id: 'enemy_spawn_field_2',
      type: 'enemy_spawn',
      x: 20 * tileSize,
      y: 35 * tileSize,
      width: 12 * tileSize,
      height: 8 * tileSize,
      properties: { enemyType: 'wolf', maxCount: 3, respawnMs: 30000 },
    },
    // Enemy spawns in forest
    {
      id: 'enemy_spawn_forest',
      type: 'enemy_spawn',
      x: 35 * tileSize,
      y: 5 * tileSize,
      width: 20 * tileSize,
      height: 18 * tileSize,
      properties: { enemyType: 'spider', maxCount: 6, respawnMs: 20000 },
    },
    // NPC in town
    {
      id: 'npc_merchant',
      type: 'npc_spawn',
      x: 12 * tileSize + tileSize / 2,
      y: 7 * tileSize + tileSize / 2,
      properties: { npcType: 'merchant', name: 'Bjorn the Trader' },
    },
  ];

  // ── Zone connections ────────────────────────────────────
  const zoneConnections: ZoneConnection[] = [
    {
      id: 'cave_entrance',
      triggerRect: {
        x: 30 * tileSize,
        y: (height - 2) * tileSize,
        width: 2 * tileSize,
        height: 1 * tileSize,
      },
      targetZone: 'cave_dungeon' as any,
      targetSpawn: { x: 3 * tileSize + tileSize / 2, y: 3 * tileSize + tileSize / 2 },
    },
  ];

  return {
    width,
    height,
    tileSize,
    collisionGrid,
    spawnPoints,
    zoneConnections,
    tileLayers,
    tilesets,
  };
}

// ── Seeded PRNG for deterministic map generation ─────────────

function createSeededRandom(seed: number): () => number {
  let s = seed;
  return () => {
    s = (s * 1664525 + 1013904223) & 0xffffffff;
    return (s >>> 0) / 0xffffffff;
  };
}
