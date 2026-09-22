import Phaser from 'phaser';
import {
  INTERPOLATION_BUFFER_MS,
  lerp,
  ClassId,
  orthoToIso,
  isoToOrtho,
  ORTHO_TILE_SIZE,
  DEFAULT_EQUIP_SPRITE_CONFIG,
  EQUIP_SLOT_TO_PAPERDOLL,
  dirFromOrthoVector,
  paperdollAnimKey,
  paperdollTextureKey,
  paperdollFrame,
} from '@valhalla/shared';
import type { EquipSlotType, PaperdollAnim, PaperdollDir } from '@valhalla/shared';
import { ClientDataManager } from './ClientDataManager.js';
import { PaperdollRegistry, parsePaperdollAnimKey } from './PaperdollRegistry.js';

export const ENTITY_DEPTH_BASE = 600_000;
export const NAMEPLATE_DEPTH   = 800_000;
export const UI_DEPTH_BASE     = 1_000_000;


// ── Character sprite helpers ─────────────────────────────────
//
// Two sprite systems coexist: the original LPC sheets (64x64, 9 frames, four
// cardinal directions) and the paperdoll pack (64x64, 20 frames, eight screen
// directions, one sheet per equipment layer). The game speaks the paperdoll
// vocabulary everywhere; these helpers translate down to LPC when a character
// has no paperdoll body.

/** Cycle names: paperdoll -> LPC. */
const LPC_ANIM: Record<PaperdollAnim, string> = {
  idle: 'idle', walk: 'walk', attack: 'melee', shoot: 'ranged', cast: 'cast',
};

/** Facings: paperdoll -> LPC, vertical axis winning diagonals as before. */
const LPC_DIR: Record<PaperdollDir, string> = {
  s: 'down', se: 'down', sw: 'down',
  n: 'up', ne: 'up', nw: 'up',
  e: 'right', w: 'left',
};

/**
 * Vertical offset from a sprite's origin to just above its head.
 *
 * LPC sprites use Phaser's centred origin; paperdoll sprites are anchored at
 * the feet so they stand on the tile they occupy. Anything that floats above a
 * character (nameplate, HP bar) has to read the origin rather than assume one.
 */
export function headroom(sprite: Phaser.GameObjects.Sprite): number {
  return sprite.displayHeight * sprite.originY + 12;
}

/**
 * Normalise an animation name to a paperdoll cycle.
 *
 * Combat still speaks the old LPC words in places ('melee', 'ranged'); this is
 * the single boundary where they become cycles, so no call site has to be
 * hunted down and renamed.
 */
export function toPaperdollAnim(name: string): PaperdollAnim {
  switch (name) {
    case 'melee': return 'attack';
    case 'ranged': return 'shoot';
    case 'idle': case 'walk': case 'attack': case 'shoot': case 'cast':
      return name;
    default: return 'attack';
  }
}

/** True when this sprite is drawn from the paperdoll pack. */
export function isPaperdollSprite(sprite: Phaser.GameObjects.Sprite): boolean {
  return sprite.texture.key.startsWith('pd_');
}

/** The paperdoll layer id behind a sprite, e.g. `pd_body_tan` -> `body_tan`. */
export function paperdollLayerOf(sprite: Phaser.GameObjects.Sprite): string {
  return sprite.texture.key.slice(3);
}

/**
 * Base character sprite. Uses the paperdoll body when the pack is loaded and
 * the body exists, otherwise the class LPC sheet, otherwise the fallback body.
 */
export function createCharacterSprite(
  scene: Phaser.Scene,
  x: number, y: number,
  classId: string,
  bodyId?: string,
): Phaser.GameObjects.Sprite {
  const pd = PaperdollRegistry.instance;
  if (pd.ready) {
    const resolved = pd.resolveBody(bodyId || ClientDataManager.instance.getClass(classId)?.bodyId);
    if (resolved && scene.textures.exists(paperdollTextureKey(resolved))) {
      const sprite = scene.add.sprite(
        x, y, paperdollTextureKey(resolved), paperdollFrame('idle', 's', 0),
      );
      sprite.setOrigin(pd.originX, pd.originY);
      return sprite;
    }
  }
  const walkTexture = scene.textures.exists(`class_${classId}_walk`)
    ? `class_${classId}_walk` : 'player_walk';
  // Frame 18 = row 2 (down), col 0 = idle facing down
  return scene.add.sprite(x, y, walkTexture, 18);
}

/**
 * The animation key for a character sprite, in whichever system it belongs to.
 * Returns null when the animation does not exist, so callers can skip the play.
 */
export function characterAnimKey(
  scene: Phaser.Scene,
  sprite: Phaser.GameObjects.Sprite,
  classId: string,
  anim: PaperdollAnim,
  dir: PaperdollDir,
): string | null {
  const key = isPaperdollSprite(sprite)
    ? paperdollAnimKey(paperdollLayerOf(sprite), anim, dir)
    : `${classId}_${LPC_ANIM[anim]}_${LPC_DIR[dir]}`;
  return scene.anims.exists(key) ? key : null;
}

/** Play a cycle on a character sprite. No-op when that animation is missing. */
export function playCharacterAnim(
  scene: Phaser.Scene,
  sprite: Phaser.GameObjects.Sprite,
  classId: string,
  anim: PaperdollAnim,
  dir: PaperdollDir,
  ignoreIfPlaying = true,
): void {
  const key = characterAnimKey(scene, sprite, classId, anim, dir);
  if (!key) return;
  if (sprite.anims.getName() !== key || !sprite.anims.isPlaying) {
    sprite.play(key, ignoreIfPlaying);
  }
}

/** Equipment overlay sprites layered on top of the character. */
interface EquipmentOverlay {
  slot: string;      // equip slot key (e.g. 'weapon', 'helm')
  itemId: string;    // the item ID that produced this overlay
  sprite: Phaser.GameObjects.Sprite;
  /** Paperdoll layer id when this overlay is a paperdoll layer, else undefined. */
  layerId?: string;
  /** Paperdoll composite slot ('mainhand', 'chest', ...), else undefined. */
  pdSlot?: string;
}

interface RemotePlayerData {
  sprite: Phaser.GameObjects.Sprite;
  nameText: Phaser.GameObjects.Text;
  nameBg: Phaser.GameObjects.Graphics;
  hpBar: Phaser.GameObjects.Graphics;
  overlays: EquipmentOverlay[];
  targetX: number;
  targetY: number;
  previousX: number;
  previousY: number;
  targetAngle: number;
  lastUpdateTime: number;
  hp: number;
  maxHp: number;
  alive: boolean;
  classId: string;
  level: number;
  characterName: string;
  lastDir: string;
  isPlayingActionAnim: boolean;
}

/** Lightweight buff descriptor received from the server's NpcBuffInfo schema. */
export interface NpcBuffData {
  skillId: string;
  expiresAt: number;
  dotDamagePerSec: number;
}

interface NPCData {
  sprite: Phaser.GameObjects.Sprite;
  nameText: Phaser.GameObjects.Text;
  nameBg: Phaser.GameObjects.Graphics;
  hpBar: Phaser.GameObjects.Graphics;
  targetX: number;
  targetY: number;
  previousX: number;
  previousY: number;
  targetAngle: number;
  lastUpdateTime: number;
  hp: number;
  maxHp: number;
  alive: boolean;
  npcType: string;
  name: string;
  level: number;
  /** Active debuffs/buffs synced from the server. */
  buffs: NpcBuffData[];
}

// ── Public Target Info Types ──────────────────────────────

export interface PlayerTargetInfo {
  characterName: string;
  classId: string;
  level: number;
  hp: number;
  maxHp: number;
  alive: boolean;
}

export interface NpcTargetInfo {
  name: string;
  level: number;
  hp: number;
  maxHp: number;
  alive: boolean;
  npcType: string;
  /** Active debuffs visible in the target pane. */
  buffs: NpcBuffData[];
}

interface LootBagData {
  sprite: Phaser.GameObjects.Sprite;
  zoneId: string;
  x: number; // ortho world X
  y: number; // ortho world Y
}

interface ProjectileData {
  sprite: Phaser.GameObjects.Sprite;
  targetX: number;
  targetY: number;
  previousX: number;
  previousY: number;
  lastUpdateTime: number;
}

/**
 * Manages rendering and interpolation for remote player entities,
 * projectiles, HP bars, class labels, and combat visual effects.
 */
/** Spell projectile visual data (interpolated, direction-aware) */
interface SpellProjectileData {
  sprite: Phaser.GameObjects.Graphics;
  targetX: number;
  targetY: number;
  previousX: number;
  previousY: number;
  angle: number; // radians — used to orient the projectile gfx
  lastUpdateTime: number;
  skillId: string;
}

export class EntityRenderer {
  private scene: Phaser.Scene;
  private remotePlayers: Map<string, RemotePlayerData> = new Map();
  private npcs: Map<string, NPCData> = new Map();
  private projectiles: Map<string, ProjectileData> = new Map();
  private spellProjectiles: Map<string, SpellProjectileData> = new Map();
  private lootBags: Map<string, LootBagData> = new Map();

  /** Called when a remote player sprite is left-clicked. */
  public onPlayerClick?: (sessionId: string) => void;
  /** Called when an NPC sprite is left-clicked. */
  public onNpcClick?: (npcId: string) => void;
  /** Called when an NPC sprite is right-clicked. */
  public onNpcRightClick?: (npcId: string) => void;
  /** Called when a loot bag sprite is clicked (button: 0=left, 2=right). */
  public onBagClick?: (bagId: string, button: number) => void;

  constructor(scene: Phaser.Scene) {
    this.scene = scene;
  }

  // ── Remote Players ────────────────────────────────────────

  addRemotePlayer(sessionId: string, x: number, y: number, classId: string = 'warrior', level: number = 1, characterName: string = '', bodyId: string = ''): void {
    // Convert ortho world coords to ISO screen coords
    const isoPos = orthoToIso(x, y);
    const sprite = createCharacterSprite(this.scene, isoPos.x, isoPos.y, classId, bodyId);
    sprite.setDepth(ENTITY_DEPTH_BASE);
    sprite.rotation = 0;

    // Make sprite clickable for targeting
    sprite.setInteractive({ useHandCursor: false });
    sprite.on('pointerdown', () => {
      this.onPlayerClick?.(sessionId);
    });

    // Name label: "CharName Lv.X" or fallback to "ClassName Lv.X"
    const displayName = characterName || (ClientDataManager.instance.getClass(classId)?.name ?? classId);
    const labelText = `${displayName} Lv.${level}`;

    // Nameplate background
    const nameBg = this.scene.add.graphics();
    nameBg.setDepth(NAMEPLATE_DEPTH);

    const nameText = this.scene.add.text(isoPos.x, isoPos.y - headroom(sprite), labelText, {
      fontSize: '12px',
      fontStyle: 'bold',
      color: '#ffffff',
      stroke: '#000000',
      strokeThickness: 3,
    });
    nameText.setOrigin(0.5, 1);
    nameText.setDepth(NAMEPLATE_DEPTH + 1);

    const hpBar = this.scene.add.graphics();
    hpBar.setDepth(NAMEPLATE_DEPTH + 2);

    this.remotePlayers.set(sessionId, {
      sprite,
      nameText,
      nameBg,
      hpBar,
      overlays: [],
      targetX: x,
      targetY: y,
      previousX: x,
      previousY: y,
      targetAngle: 0,
      lastUpdateTime: Date.now(),
      hp: 100,
      maxHp: 100,
      alive: true,
      classId,
      level,
      characterName,
      lastDir: 's',
      isPlayingActionAnim: false,
    });
  }

  removeRemotePlayer(sessionId: string): void {
    const data = this.remotePlayers.get(sessionId);
    if (data) {
      data.sprite.destroy();
      data.nameText.destroy();
      data.nameBg.destroy();
      data.hpBar.destroy();
      for (const overlay of data.overlays) overlay.sprite.destroy();
      data.overlays.length = 0;
      this.remotePlayers.delete(sessionId);
    }
  }

  updateRemotePlayerTarget(sessionId: string, x: number, y: number, aimAngle: number): void {
    const data = this.remotePlayers.get(sessionId);
    if (!data) return;

    // Snapshot current ISO sprite position and convert back to ortho
    const orthoPos = isoToOrtho(data.sprite.x, data.sprite.y);
    data.previousX = orthoPos.x;
    data.previousY = orthoPos.y;
    data.targetX = x;
    data.targetY = y;
    data.targetAngle = aimAngle;
    data.lastUpdateTime = Date.now();
  }

  updateRemotePlayerHp(sessionId: string, hp: number, maxHp: number, alive: boolean, level?: number): void {
    const data = this.remotePlayers.get(sessionId);
    if (!data) return;

    data.hp = hp;
    data.maxHp = maxHp;
    data.alive = alive;
    data.sprite.setVisible(alive);
    data.nameText.setVisible(alive);
    data.nameBg.setVisible(alive);

    // Update level if changed
    if (level !== undefined && level !== data.level) {
      data.level = level;
      const displayName = data.characterName || (ClientDataManager.instance.getClass(data.classId)?.name ?? data.classId);
      data.nameText.setText(`${displayName} Lv.${level}`);
    }
  }

  // ── Equipment Overlays ───────────────────────────────────

  /**
   * Update equipment overlays for a remote player.
   * @param equipment - Record of slot → itemId (empty string = nothing equipped)
   */
  updateRemotePlayerEquipment(sessionId: string, equipment: Record<string, string>): void {
    const data = this.remotePlayers.get(sessionId);
    if (!data) return;
    this.syncOverlays(data.overlays, equipment, data.sprite);
  }

  /**
   * Create/update/remove overlay sprites to match the given equipment map.
   * Works for both remote players and can be called externally for the local player.
   */
  syncOverlays(
    overlays: EquipmentOverlay[],
    equipment: Record<string, string>,
    baseSprite: Phaser.GameObjects.Sprite,
  ): void {
    const dm = ClientDataManager.instance;
    const pd = PaperdollRegistry.instance;

    // Which slots want an overlay, and which layer id (if any) draws it
    const desired = new Map<string, { itemId: string; layerId?: string; pdSlot?: string }>();
    for (const [slot, itemId] of Object.entries(equipment)) {
      if (!itemId) continue;
      const item = dm.getItem(itemId);
      if (!item) continue;

      // Paperdoll wins when the item declares a layer and the pack has it
      if (pd.ready && item.spriteId && pd.getItem(item.spriteId)
          && this.scene.textures.exists(paperdollTextureKey(item.spriteId))) {
        const pdSlot = EQUIP_SLOT_TO_PAPERDOLL[slot as EquipSlotType];
        if (pdSlot) {
          desired.set(slot, { itemId, layerId: item.spriteId, pdSlot });
          continue;
        }
      }
      // Otherwise fall back to the LPC overlay sheets
      if (item.equipSpriteSheet || item.meleeSpriteSheet || item.rangedSpriteSheet || item.castSpriteSheet) {
        desired.set(slot, { itemId });
      }
    }

    // Drop overlays whose slot emptied or whose item changed
    for (let i = overlays.length - 1; i >= 0; i--) {
      const ov = overlays[i];
      const want = desired.get(ov.slot);
      if (!want || want.itemId !== ov.itemId || want.layerId !== ov.layerId) {
        ov.sprite.destroy();
        overlays.splice(i, 1);
      }
    }

    // Create the new ones
    const existingSlots = new Set(overlays.map(o => o.slot));
    for (const [slot, want] of desired) {
      if (existingSlots.has(slot)) continue;

      if (want.layerId) {
        const textureKey = paperdollTextureKey(want.layerId);
        const overlay = this.scene.add.sprite(
          baseSprite.x, baseSprite.y, textureKey, paperdollFrame('idle', 's', 0),
        );
        overlay.setOrigin(baseSprite.originX, baseSprite.originY);
        overlay.setDepth(baseSprite.depth + pd.depthOffsetFor('s', want.pdSlot!));
        overlays.push({ slot, itemId: want.itemId, sprite: overlay, layerId: want.layerId, pdSlot: want.pdSlot });
        continue;
      }

      const item = dm.getItem(want.itemId);
      if (!item) continue;
      const initialSheet = item.equipSpriteSheet || item.meleeSpriteSheet || item.rangedSpriteSheet || item.castSpriteSheet;
      if (!initialSheet) continue;
      const textureKey = `equip_sheet_${initialSheet}`;
      if (!this.scene.textures.exists(textureKey)) continue;

      const config = item.equipSpriteConfig ?? DEFAULT_EQUIP_SPRITE_CONFIG;
      // Start on frame 18 (row 2 = down, col 0) to match the base sprite idle-down
      const startFrame = Math.min(2 * config.framesPerRow, config.framesPerRow * config.rows - 1);
      const overlay = this.scene.add.sprite(baseSprite.x, baseSprite.y, textureKey, startFrame);
      overlay.setDepth(baseSprite.depth + 0.5);
      overlays.push({ slot, itemId: want.itemId, sprite: overlay });
    }
  }

  /**
   * Extract the animation type and direction from a class-prefixed animation key.
   * e.g. "warrior_melee_down" → { animType: "melee", dir: "down" }
   * e.g. "warrior_idle_up" → { animType: "idle", dir: "up" }
   */
  private parseAnimKey(animKey: string): { animType: string; dir: string } | null {
    // Paperdoll: pd_{layerId}_{anim}_{dir}, layer ids contain underscores
    const pd = parsePaperdollAnimKey(animKey);
    if (pd) return { animType: pd.anim, dir: pd.dir };
    // LPC: {classId}_{animType}_{dir}
    const match = animKey.match(/^[^_]+_(\w+)_(up|down|left|right)$/);
    if (match) return { animType: match[1], dir: match[2] };
    return null;
  }

  /**
   * Sync overlay sprite positions, animations, and visibility with a base sprite.
   * Each overlay can have different sprite sheets per animation type.
   * Called each frame from the update loop and externally for the local player.
   */
  updateOverlayPositions(
    overlays: EquipmentOverlay[],
    baseSprite: Phaser.GameObjects.Sprite,
  ): void {
    const currentAnimKey = baseSprite.anims.getName();
    const parsed = currentAnimKey ? this.parseAnimKey(currentAnimKey) : null;
    const pd = PaperdollRegistry.instance;

    for (const ov of overlays) {
      ov.sprite.x = baseSprite.x;
      ov.sprite.y = baseSprite.y;

      if (!parsed) {
        ov.sprite.setDepth(baseSprite.depth + 0.5);
        ov.sprite.setVisible(baseSprite.visible);
        continue;
      }

      // ── Paperdoll layer ──
      if (ov.layerId && ov.pdSlot) {
        const dir = parsed.dir as PaperdollDir;
        // Composite order is per-direction; only `back` actually moves, but
        // reading it from the manifest keeps the client honest either way.
        ov.sprite.setDepth(baseSprite.depth + pd.depthOffsetFor(dir, ov.pdSlot));
        const key = paperdollAnimKey(ov.layerId, parsed.animType as PaperdollAnim, dir);
        if (!this.scene.anims.exists(key)) { ov.sprite.setVisible(false); continue; }
        ov.sprite.setVisible(baseSprite.visible);
        if (ov.sprite.anims.getName() !== key || !ov.sprite.anims.isPlaying) {
          ov.sprite.play(key, true);
        }
        // Lock the layer to the body's frame so they never drift apart
        const bodyFrame = baseSprite.anims.currentFrame;
        const anim = ov.sprite.anims.currentAnim;
        if (bodyFrame && anim) {
          ov.sprite.anims.setCurrentFrame(anim.frames[bodyFrame.index] ?? anim.frames[0]);
        }
        continue;
      }

      // ── LPC overlay sheet ──
      ov.sprite.setDepth(baseSprite.depth + 0.5);
      const item = ClientDataManager.instance.getItem(ov.itemId);
      if (!item) { ov.sprite.setVisible(false); continue; }

      let sheetFile: string | undefined;
      const at = parsed.animType;
      if (at === 'walk' || at === 'idle') {
        sheetFile = item.equipSpriteSheet;
      } else if (at === 'melee' || at === 'attack') {
        sheetFile = item.meleeSpriteSheet;
      } else if (at === 'ranged' || at === 'shoot') {
        sheetFile = item.rangedSpriteSheet;
      } else if (at === 'cast') {
        sheetFile = item.castSpriteSheet;
      }

      if (!sheetFile) {
        // No sheet for this animation type — hide overlay
        ov.sprite.setVisible(false);
        continue;
      }

      ov.sprite.setVisible(baseSprite.visible);
      const textureKey = `equip_sheet_${sheetFile}`;
      const overlayAnimKey = `${textureKey}_${at}_${parsed.dir}`;

      if (overlayAnimKey && this.scene.anims.exists(overlayAnimKey)) {
        if (ov.sprite.texture.key !== textureKey) {
          ov.sprite.setTexture(textureKey);
        }
        if (ov.sprite.anims.getName() !== overlayAnimKey || !ov.sprite.anims.isPlaying) {
          ov.sprite.play(overlayAnimKey, true);
        }
        if (baseSprite.anims.currentFrame) {
          ov.sprite.anims.setCurrentFrame(
            ov.sprite.anims.currentAnim!.frames[baseSprite.anims.currentFrame.index] ??
            ov.sprite.anims.currentAnim!.frames[0]
          );
        }
      } else {
        ov.sprite.setVisible(false);
      }
    }
  }

  // ── NPCs ─────────────────────────────────────────────────

  addNPC(
    id: string,
    x: number,
    y: number,
    name: string,
    level: number,
    npcType: string,
    spriteColor: number,
    spriteSize: number,
  ): void {
    // Convert ortho world coords to ISO screen coords
    const isoPos = orthoToIso(x, y);
    const sprite = this.scene.add.sprite(isoPos.x, isoPos.y, 'npc_sprite');
    sprite.setDepth(ENTITY_DEPTH_BASE);
    sprite.setTint(spriteColor || 0xff4444);

    // Make sprite clickable for targeting (left) and ranged attack (right)
    sprite.setInteractive({ useHandCursor: false });
    sprite.on('pointerdown', (pointer: Phaser.Input.Pointer) => {
      if (pointer.rightButtonDown()) {
        this.onNpcRightClick?.(id);
      } else {
        this.onNpcClick?.(id);
      }
    });

    // Scale sprite by spriteSize (1 = default 24px, 2 = double, etc.)
    if (spriteSize > 1) {
      sprite.setScale(spriteSize);
    }

    // Nameplate: red for enemies, yellow for friendly NPCs
    const nameColor = npcType === 'enemy' ? '#ff6666' : '#ffee88';
    const labelText = `${name} Lv.${level}`;

    const nameBg = this.scene.add.graphics();
    nameBg.setDepth(NAMEPLATE_DEPTH);

    const nameText = this.scene.add.text(isoPos.x, isoPos.y - headroom(sprite), labelText, {
      fontSize: '12px',
      fontStyle: 'bold',
      color: nameColor,
      stroke: '#000000',
      strokeThickness: 3,
    });
    nameText.setOrigin(0.5, 1);
    nameText.setDepth(NAMEPLATE_DEPTH + 1);

    const hpBar = this.scene.add.graphics();
    hpBar.setDepth(NAMEPLATE_DEPTH + 2);

    this.npcs.set(id, {
      sprite,
      nameText,
      nameBg,
      hpBar,
      targetX: x,
      targetY: y,
      previousX: x,
      previousY: y,
      targetAngle: 0,
      lastUpdateTime: Date.now(),
      hp: 100,
      maxHp: 100,
      alive: true,
      npcType,
      name,
      level,
      buffs: [],
    });
  }

  removeNPC(id: string): void {
    const data = this.npcs.get(id);
    if (data) {
      data.sprite.destroy();
      data.nameText.destroy();
      data.nameBg.destroy();
      data.hpBar.destroy();
      this.npcs.delete(id);
    }
  }

  updateNPCTarget(id: string, x: number, y: number, aimAngle: number): void {
    const data = this.npcs.get(id);
    if (!data) return;

    // Snapshot current ISO sprite position and convert back to ortho
    const orthoPos = isoToOrtho(data.sprite.x, data.sprite.y);
    data.previousX = orthoPos.x;
    data.previousY = orthoPos.y;
    data.targetX = x;
    data.targetY = y;
    data.targetAngle = aimAngle;
    data.lastUpdateTime = Date.now();
  }

  updateNPCHp(id: string, hp: number, maxHp: number, alive: boolean): void {
    const data = this.npcs.get(id);
    if (!data) return;

    data.hp = hp;
    data.maxHp = maxHp;
    data.alive = alive;
    data.sprite.setVisible(alive);
    data.nameText.setVisible(alive);
    data.nameBg.setVisible(alive);
  }

  /** Update the cached active buff list for an NPC (from syncedBuffs schema changes). */
  updateNPCBuffs(id: string, buffs: NpcBuffData[]): void {
    const data = this.npcs.get(id);
    if (!data) return;
    data.buffs = buffs;
  }

  hasNPC(id: string): boolean {
    return this.npcs.has(id);
  }

  getNPCPosition(id: string): { x: number; y: number } | null {
    const data = this.npcs.get(id);
    if (!data) return null;
    return { x: data.sprite.x, y: data.sprite.y };
  }

  // ── Loot Bags ────────────────────────────────────────────

  addLootBag(bagId: string, x: number, y: number, zoneId: string): void {
    if (this.lootBags.has(bagId)) return;

    const isoPos = orthoToIso(x, y);
    const sprite = this.scene.add.sprite(isoPos.x, isoPos.y, 'loot_bag');
    sprite.setDepth(ENTITY_DEPTH_BASE - 1); // slightly below entities
    sprite.setInteractive({ useHandCursor: true });

    sprite.on('pointerdown', (pointer: Phaser.Input.Pointer) => {
      this.onBagClick?.(bagId, pointer.button);
    });

    this.lootBags.set(bagId, { sprite, zoneId, x, y });
  }

  removeLootBag(bagId: string): void {
    const data = this.lootBags.get(bagId);
    if (data) {
      data.sprite.destroy();
      this.lootBags.delete(bagId);
    }
  }

  hasLootBag(bagId: string): boolean {
    return this.lootBags.has(bagId);
  }

  getLootBagPosition(bagId: string): { x: number; y: number } | null {
    const data = this.lootBags.get(bagId);
    if (!data) return null;
    return { x: data.sprite.x, y: data.sprite.y };
  }

  /** Get the ortho world position of a loot bag. */
  getLootBagOrthoPosition(bagId: string): { x: number; y: number } | null {
    const data = this.lootBags.get(bagId);
    if (!data) return null;
    return { x: data.x, y: data.y };
  }

  // ── Projectiles ───────────────────────────────────────────

  addProjectile(id: string, x: number, y: number): void {
    // Convert ortho world coords to ISO screen coords
    const isoPos = orthoToIso(x, y);
    const sprite = this.scene.add.sprite(isoPos.x, isoPos.y, 'projectile');
    sprite.setDepth(ENTITY_DEPTH_BASE + 50);

    this.projectiles.set(id, {
      sprite,
      targetX: x,
      targetY: y,
      previousX: x,
      previousY: y,
      lastUpdateTime: Date.now(),
    });
  }

  removeProjectile(id: string): void {
    const data = this.projectiles.get(id);
    if (data) {
      data.sprite.destroy();
      this.projectiles.delete(id);
    }
  }

  updateProjectileTarget(id: string, x: number, y: number): void {
    const data = this.projectiles.get(id);
    if (!data) return;

    // Snapshot current ISO sprite position and convert back to ortho
    const orthoPos = isoToOrtho(data.sprite.x, data.sprite.y);
    data.previousX = orthoPos.x;
    data.previousY = orthoPos.y;
    data.targetX = x;
    data.targetY = y;
    data.lastUpdateTime = Date.now();
  }

  // ── Spell Projectiles (e.g. Fireball) ─────────────────────

  addSpellProjectile(id: string, x: number, y: number, targetX: number, targetY: number, skillId: string): void {
    // Draw at (0, 0) relative to the Graphics object, then position the object in world space.
    // This lets us move the fireball by setting gfx.x / gfx.y, and lets Phaser tweens
    // scale/rotate around the correct world-space origin.
    const gfx = this.drawFireball();
    // Convert ortho world coords to ISO screen coords
    const isoPos = orthoToIso(x, y);
    gfx.x = isoPos.x;
    gfx.y = isoPos.y;

    this.spellProjectiles.set(id, {
      sprite: gfx,
      targetX: x,
      targetY: y,
      previousX: x,
      previousY: y,
      angle: Math.atan2(targetY - y, targetX - x),
      lastUpdateTime: Date.now(),
      skillId,
    });
  }

  updateSpellProjectileTarget(id: string, x: number, y: number): void {
    const data = this.spellProjectiles.get(id);
    if (!data) return;

    // sprite.x / sprite.y now correctly return the Graphics world position (ISO screen coords)
    // Convert back to ortho for storage
    const orthoPos = isoToOrtho(data.sprite.x, data.sprite.y);
    data.previousX = orthoPos.x;
    data.previousY = orthoPos.y;

    data.targetX = x;
    data.targetY = y;
    data.lastUpdateTime = Date.now();
  }

  removeSpellProjectile(id: string): void {
    const data = this.spellProjectiles.get(id);
    if (data) {
      data.sprite.destroy();
      this.spellProjectiles.delete(id);
    }
  }

  /**
   * Play a fireball explosion at the given world position.
   * Called when the server sends a SPELL_IMPACT event.
   *
   * Graphics are drawn at (0, 0) in local space; the objects are positioned
   * at (x, y) in world space. Phaser's tween then scales around (x, y),
   * not around the world origin.
   */
  showSpellImpact(x: number, y: number, radius: number, _skillId: string): void {
    // Outer blast ring — positioned at the detonation point
    const ring = this.scene.add.graphics();
    ring.setDepth(ENTITY_DEPTH_BASE + 10);
    ring.x = x;
    ring.y = y;
    ring.lineStyle(4, 0xff6600, 1);
    ring.fillStyle(0xff4400, 0.55);
    ring.strokeCircle(0, 0, radius);
    ring.fillCircle(0, 0, radius);

    // Inner bright core — also centred on the detonation point
    const core = this.scene.add.graphics();
    core.setDepth(ENTITY_DEPTH_BASE + 11);
    core.x = x;
    core.y = y;
    core.fillStyle(0xffee00, 0.9);
    core.fillCircle(0, 0, radius * 0.3);

    // Animate: expand + fade — scales around the Graphics origin, which is now (x, y)
    this.scene.tweens.add({
      targets: ring,
      alpha: 0,
      scaleX: 1.4,
      scaleY: 1.4,
      duration: 350,
      ease: 'Cubic.Out',
      onComplete: () => ring.destroy(),
    });

    this.scene.tweens.add({
      targets: core,
      alpha: 0,
      scaleX: 1.8,
      scaleY: 1.8,
      duration: 200,
      ease: 'Cubic.Out',
      onComplete: () => core.destroy(),
    });
  }

  /**
   * Draw the fireball visual at local origin (0, 0).
   * The caller is responsible for positioning the returned Graphics object
   * in world space by setting gfx.x / gfx.y.
   */
  private drawFireball(): Phaser.GameObjects.Graphics {
    const gfx = this.scene.add.graphics();
    gfx.setDepth(ENTITY_DEPTH_BASE + 50);
    // Outer glow
    gfx.fillStyle(0xff6600, 0.5);
    gfx.fillCircle(0, 0, 14);
    // Core
    gfx.fillStyle(0xffdd00, 1);
    gfx.fillCircle(0, 0, 8);
    return gfx;
  }

  // ── Melee Visual Effect ───────────────────────────────────

  showMeleeSlash(x: number, y: number, angle: number, isIso: boolean = false): void {
    // x,y are in screen/sprite space (ISO screen coords when isIso, ortho world coords otherwise).
    // `angle` is always in orthogonal world space (calculated from mouse vs player in ortho).
    // For ISO we must convert the ortho direction vector into ISO screen space so the
    // offset and rotation match the direction the player is actually facing on screen.
    let offsetX: number;
    let offsetY: number;
    let rotation: number;

    if (isIso) {
      // Transform ortho unit vector → ISO screen direction
      // orthoToIso formula: isoX = orthoX - orthoY,  isoY = (orthoX + orthoY) / 2
      const cosA = Math.cos(angle);
      const sinA = Math.sin(angle);
      const isoDx = cosA - sinA;
      const isoDy = (cosA + sinA) / 2;
      // Normalise so the visual offset is a consistent 30 px regardless of angle
      const isoLen = Math.hypot(isoDx, isoDy) || 1;
      offsetX = (isoDx / isoLen) * 30;
      offsetY = (isoDy / isoLen) * 30;
      rotation = Math.atan2(isoDy, isoDx);
    } else {
      offsetX = Math.cos(angle) * 30;
      offsetY = Math.sin(angle) * 30;
      rotation = angle;
    }

    const slash = this.scene.add.sprite(x + offsetX, y + offsetY, 'melee_slash');
    slash.setDepth(ENTITY_DEPTH_BASE + 9);
    slash.setRotation(rotation);
    slash.setAlpha(0.9);

    // Fade out and destroy
    this.scene.tweens.add({
      targets: slash,
      alpha: 0,
      scaleX: 1.5,
      scaleY: 1.5,
      duration: 200,
      onComplete: () => slash.destroy(),
    });
  }

  // ── Damage Flash Effect ───────────────────────────────────

  showDamageFlash(x: number, y: number, damage: number, isCrit?: boolean): void {
    const text = isCrit ? `-${damage}!` : `-${damage}`;
    const fontSize = isCrit ? '22px' : '16px';
    const color = isCrit ? '#ffaa00' : '#ff4444';

    const dmgText = this.scene.add.text(x, y - 20, text, {
      fontSize,
      color,
      stroke: '#000000',
      strokeThickness: 3,
      fontStyle: 'bold',
    });
    dmgText.setOrigin(0.5, 0.5);
    dmgText.setDepth(NAMEPLATE_DEPTH + 50);

    this.scene.tweens.add({
      targets: dmgText,
      y: y - 60,
      alpha: 0,
      duration: isCrit ? 1000 : 800,
      ease: 'Power2',
      onComplete: () => dmgText.destroy(),
    });
  }

  // ── Combat Feedback Text (Miss / Dodge / Block) ──────────

  showCombatText(x: number, y: number, label: string, color: string): void {
    const txt = this.scene.add.text(x, y - 20, label, {
      fontSize: '14px',
      color,
      stroke: '#000000',
      strokeThickness: 3,
      fontStyle: 'bold',
    });
    txt.setOrigin(0.5, 0.5);
    txt.setDepth(NAMEPLATE_DEPTH + 50);

    this.scene.tweens.add({
      targets: txt,
      y: y - 55,
      alpha: 0,
      duration: 700,
      ease: 'Power2',
      onComplete: () => txt.destroy(),
    });
  }

  // ── HP Bar Drawing ────────────────────────────────────────

  private drawHpBar(gfx: Phaser.GameObjects.Graphics, x: number, y: number, hp: number, maxHp: number, top?: number): void {
    gfx.clear();

    const barWidth = 40;
    const barHeight = 5;
    const barX = x - barWidth / 2;
    const barY = top ?? (y - 34);

    // Background (dark)
    gfx.fillStyle(0x000000, 0.6);
    gfx.fillRect(barX - 1, barY - 1, barWidth + 2, barHeight + 2);

    // HP fill
    const hpRatio = Math.max(0, hp / maxHp);
    const fillColor = hpRatio > 0.5 ? 0x44ff44 : hpRatio > 0.25 ? 0xffaa00 : 0xff4444;
    gfx.fillStyle(fillColor, 1);
    gfx.fillRect(barX, barY, barWidth * hpRatio, barHeight);
  }

  // ── Update Loop ───────────────────────────────────────────

  /**
   * Interpolate remote player and projectile positions each frame.
   */
  update(): void {
    const now = Date.now();

    // Remote players
    this.remotePlayers.forEach((data) => {
      if (!data.alive) {
        data.hpBar.clear();
        return;
      }

      const elapsed = now - data.lastUpdateTime;
      const t = Math.min(elapsed / INTERPOLATION_BUFFER_MS, 1);

      // Lerp in ortho space
      const orthoX = lerp(data.previousX, data.targetX, t);
      const orthoY = lerp(data.previousY, data.targetY, t);

      // Convert lerped ortho result to ISO for sprite position
      const isoPos = orthoToIso(orthoX, orthoY);
      data.sprite.x = isoPos.x;
      data.sprite.y = isoPos.y;
      data.sprite.rotation = 0;

      // Directional animation: only walk while actively interpolating toward a new position.
      // Once t >= 1 the sprite has reached its destination — show idle even if the last
      // known delta was non-zero (which would otherwise keep the walk animation running
      // indefinitely after the player stops moving).
      const dx = data.targetX - data.previousX;
      const dy = data.targetY - data.previousY;
      const isMoving = t < 1 && (Math.abs(dx) > 0.5 || Math.abs(dy) > 0.5);

      // If playing an action animation, movement cancels it
      if (data.isPlayingActionAnim && isMoving) {
        data.isPlayingActionAnim = false;
      }

      if (data.isPlayingActionAnim) {
        // Let the action animation play through without interruption
      } else if (isMoving) {
        // Facing comes from the same helper the local player uses, so what a
        // player sees themselves doing matches what everyone else sees.
        const dir = dirFromOrthoVector(dx, dy);
        if (dir) data.lastDir = dir;
        playCharacterAnim(this.scene, data.sprite, data.classId, 'walk', data.lastDir as PaperdollDir);
      } else {
        // Idle — either at destination or no meaningful movement delta
        playCharacterAnim(this.scene, data.sprite, data.classId, 'idle', data.lastDir as PaperdollDir);
      }

      // Dynamic depth based on ortho position
      data.sprite.setDepth(ENTITY_DEPTH_BASE + Math.floor(orthoX / ORTHO_TILE_SIZE) + Math.floor(orthoY / ORTHO_TILE_SIZE));

      data.nameText.x = data.sprite.x;
      const head = data.sprite.y - headroom(data.sprite);
      data.nameText.y = head;

      // Draw nameplate background
      data.nameBg.clear();
      const textW = data.nameText.width;
      const textH = data.nameText.height;
      const bgX = data.sprite.x - textW / 2 - 4;
      const bgY = head - textH - 1;
      data.nameBg.fillStyle(0x000000, 0.5);
      data.nameBg.fillRoundedRect(bgX, bgY, textW + 8, textH + 4, 3);

      // Draw HP bar
      this.drawHpBar(data.hpBar, data.sprite.x, data.sprite.y, data.hp, data.maxHp, head + 10);

      // Sync equipment overlays
      this.updateOverlayPositions(data.overlays, data.sprite);
    });

    // NPCs — same interpolation as remote players
    this.npcs.forEach((data) => {
      if (!data.alive) {
        data.hpBar.clear();
        return;
      }

      const npcElapsed = now - data.lastUpdateTime;
      const nt = Math.min(npcElapsed / INTERPOLATION_BUFFER_MS, 1);

      // Lerp in ortho space
      const orthoX = lerp(data.previousX, data.targetX, nt);
      const orthoY = lerp(data.previousY, data.targetY, nt);

      // Convert lerped ortho result to ISO for sprite position
      const isoPos = orthoToIso(orthoX, orthoY);
      data.sprite.x = isoPos.x;
      data.sprite.y = isoPos.y;
      data.sprite.rotation = data.targetAngle;

      // Dynamic depth based on ortho position
      data.sprite.setDepth(ENTITY_DEPTH_BASE + Math.floor(orthoX / ORTHO_TILE_SIZE) + Math.floor(orthoY / ORTHO_TILE_SIZE));

      data.nameText.x = data.sprite.x;
      const head = data.sprite.y - headroom(data.sprite);
      data.nameText.y = head;

      // Draw nameplate background
      data.nameBg.clear();
      const npcTextW = data.nameText.width;
      const npcTextH = data.nameText.height;
      const npcBgX = data.sprite.x - npcTextW / 2 - 4;
      const npcBgY = head - npcTextH - 1;
      data.nameBg.fillStyle(0x000000, 0.5);
      data.nameBg.fillRoundedRect(npcBgX, npcBgY, npcTextW + 8, npcTextH + 4, 3);

      // Draw HP bar
      this.drawHpBar(data.hpBar, data.sprite.x, data.sprite.y, data.hp, data.maxHp, head + 10);
    });

    // Projectiles — interpolate positions in ortho space, convert to ISO
    this.projectiles.forEach((data) => {
      const elapsed = now - data.lastUpdateTime;
      const t = Math.min(elapsed / INTERPOLATION_BUFFER_MS, 1);

      // Lerp in ortho space
      const orthoX = lerp(data.previousX, data.targetX, t);
      const orthoY = lerp(data.previousY, data.targetY, t);

      // Convert lerped ortho result to ISO for sprite position
      const isoPos = orthoToIso(orthoX, orthoY);
      data.sprite.x = isoPos.x;
      data.sprite.y = isoPos.y;
    });

    // Spell projectiles — interpolate by moving the Graphics object's world position.
    // The circles are drawn at (0, 0) in local space, so moving .x/.y is all we need.
    // Interpolate in ortho space, convert to ISO.
    this.spellProjectiles.forEach((data) => {
      const elapsed = now - data.lastUpdateTime;
      const t = Math.min(elapsed / INTERPOLATION_BUFFER_MS, 1);

      // Lerp in ortho space
      const orthoX = lerp(data.previousX, data.targetX, t);
      const orthoY = lerp(data.previousY, data.targetY, t);

      // Convert lerped ortho result to ISO for sprite position
      const isoPos = orthoToIso(orthoX, orthoY);
      data.sprite.x = isoPos.x;
      data.sprite.y = isoPos.y;
    });
  }

  hasPlayer(sessionId: string): boolean {
    return this.remotePlayers.has(sessionId);
  }

  getPlayerPosition(sessionId: string): { x: number; y: number } | null {
    const data = this.remotePlayers.get(sessionId);
    if (!data) return null;
    return { x: data.sprite.x, y: data.sprite.y };
  }

  /**
   * Play an action animation (melee, ranged, cast) on a remote player.
   * animType: 'melee' | 'ranged' | 'cast'
   * direction: optional override; defaults to the player's current lastDir.
   */
  playRemoteActionAnim(sessionId: string, animType: string, direction?: string): void {
    const data = this.remotePlayers.get(sessionId);
    if (!data) return;

    const dir = (direction ?? data.lastDir) as PaperdollDir;
    const anim = toPaperdollAnim(animType);
    const animKey = characterAnimKey(this.scene, data.sprite, data.classId, anim, dir);
    if (!animKey) return;

    data.isPlayingActionAnim = true;
    data.sprite.play(animKey, true);

    if (anim === 'attack' || anim === 'shoot') {
      // One-shot: return to idle when done
      data.sprite.once('animationcomplete', () => {
        data.isPlayingActionAnim = false;
        playCharacterAnim(this.scene, data.sprite, data.classId, 'idle', data.lastDir as PaperdollDir, true);
      });
    }
    // 'cast' loops indefinitely — stopped by movement or stopRemoteActionAnim()
  }

  /** Stop a looping action animation (e.g. cast) on a remote player. */
  stopRemoteActionAnim(sessionId: string): void {
    const data = this.remotePlayers.get(sessionId);
    if (!data || !data.isPlayingActionAnim) return;
    data.isPlayingActionAnim = false;
    playCharacterAnim(this.scene, data.sprite, data.classId, 'idle', data.lastDir as PaperdollDir, true);
  }

  // ── Target Info Getters (for the targeting nameplate) ─────

  getPlayerTargetInfo(sessionId: string): PlayerTargetInfo | null {
    const data = this.remotePlayers.get(sessionId);
    if (!data) return null;
    return {
      characterName: data.characterName,
      classId: data.classId,
      level: data.level,
      hp: data.hp,
      maxHp: data.maxHp,
      alive: data.alive,
    };
  }

  getNpcTargetInfo(id: string): NpcTargetInfo | null {
    const data = this.npcs.get(id);
    if (!data) return null;
    return {
      name: data.name,
      level: data.level,
      hp: data.hp,
      maxHp: data.maxHp,
      alive: data.alive,
      npcType: data.npcType,
      buffs: data.buffs,
    };
  }

  // ── Magic Missile VFX ─────────────────────────────────────

  /**
   * Play the Magic Missile visual effect:
   *   1. A bright arcane burst at the caster's feet (launch flash).
   *   2. A glowing blue-white bolt that travels to the target.
   *   3. An arcane impact burst at the target position.
   *
   * All effects are purely cosmetic Phaser Graphics tweens — no game state
   * is modified here.
   */
  showMagicMissileVFX(fromX: number, fromY: number, toX: number, toY: number): void {
    // ── 1. Cast flash at caster ──
    const castFlash = this.scene.add.graphics();
    castFlash.setDepth(ENTITY_DEPTH_BASE + 12);
    castFlash.x = fromX;
    castFlash.y = fromY;
    // Outer glow
    castFlash.fillStyle(0x6699ff, 0.4);
    castFlash.fillCircle(0, 0, 18);
    // Inner bright core
    castFlash.fillStyle(0xddeeff, 0.95);
    castFlash.fillCircle(0, 0, 8);

    this.scene.tweens.add({
      targets: castFlash,
      alpha: 0,
      scaleX: 2.2,
      scaleY: 2.2,
      duration: 220,
      ease: 'Cubic.Out',
      onComplete: () => castFlash.destroy(),
    });

    // ── 2. Bolt that travels from caster to target ──
    const bolt = this.scene.add.graphics();
    bolt.setDepth(ENTITY_DEPTH_BASE + 12);
    bolt.x = fromX;
    bolt.y = fromY;
    // Outer soft glow
    bolt.fillStyle(0x88aaff, 0.6);
    bolt.fillCircle(0, 0, 7);
    // Bright core
    bolt.fillStyle(0xffffff, 1.0);
    bolt.fillCircle(0, 0, 4);

    // Travel duration is proportional to distance but capped so it never drags
    const dx = toX - fromX;
    const dy = toY - fromY;
    const dist = Math.sqrt(dx * dx + dy * dy);
    const travelMs = Math.min(180, Math.max(60, dist * 0.35));

    this.scene.tweens.add({
      targets: bolt,
      x: toX,
      y: toY,
      duration: travelMs,
      ease: 'Linear',
      onComplete: () => {
        bolt.destroy();

        // ── 3. Impact flash at target ──
        const impact = this.scene.add.graphics();
        impact.setDepth(ENTITY_DEPTH_BASE + 12);
        impact.x = toX;
        impact.y = toY;
        // Outer burst ring
        impact.lineStyle(3, 0x6699ff, 0.9);
        impact.strokeCircle(0, 0, 14);
        // Fill
        impact.fillStyle(0x88ccff, 0.55);
        impact.fillCircle(0, 0, 14);
        // Bright centre
        impact.fillStyle(0xffffff, 0.9);
        impact.fillCircle(0, 0, 6);

        this.scene.tweens.add({
          targets: impact,
          alpha: 0,
          scaleX: 2.4,
          scaleY: 2.4,
          duration: 280,
          ease: 'Cubic.Out',
          onComplete: () => impact.destroy(),
        });
      },
    });
  }
}
