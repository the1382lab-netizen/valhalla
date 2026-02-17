import { Room, Client } from '@colyseus/core';
import { GameState } from '../schema/GameState.js';
import { PlayerState, InventorySlotState } from '../schema/PlayerState.js';
import { CollisionSystem } from '../systems/CollisionSystem.js';
import { MovementSystem } from '../systems/MovementSystem.js';
import { CombatSystem, CombatEvent } from '../systems/CombatSystem.js';
import { MapManager } from '../systems/MapManager.js';
import {
  InputPayload,
  MessageType,
  SERVER_TICK_RATE,
  ClassId,
  EquipSlotType,
  SAVE_INTERVAL_MS,
  computeDerivedStats,
  ZoneId,
  ZoneConnection,
} from '@valhalla/shared';
import { equipItem, unequipItem, unequipItemToSlot, dropInventoryItem, dropEquippedItem, swapInventorySlots } from '../systems/InventorySystem.js';
import { verifyToken, JwtPayload } from '../services/AuthService.js';
import {
  loadCharacter,
  saveCharacter,
  LoadedCharacter,
  SaveCharacterData,
} from '../services/CharacterService.js';

/** Tracking data per connected session. */
interface SessionData {
  characterId: number;
  userId: number;
  username: string;
}

/** Cached per-zone data for multi-zone support within a single room. */
interface ZoneCacheEntry {
  collision: CollisionSystem;
  connections: ZoneConnection[];
  respawnPoint: { x: number; y: number };
}

export class GameRoom extends Room<{ state: GameState }> {
  private movement!: MovementSystem;
  private combat!: CombatSystem;
  private mapManager!: MapManager;
  private inputQueues: Map<string, InputPayload[]> = new Map();

  /** Maps sessionId → persistent character data for save/load. */
  private sessionData: Map<string, SessionData> = new Map();

  /** Interval handle for periodic saves. */
  private saveInterval: ReturnType<typeof setInterval> | null = null;

  /** Default zone for new players. */
  private defaultZoneId: string = ZoneId.GRASSLANDS;

  /** Per-zone collision, connections, and respawn caches. */
  private zoneCache: Map<string, ZoneCacheEntry> = new Map();

  onCreate(): void {
    this.setState(new GameState());

    // Initialize map system
    this.mapManager = new MapManager();

    // Pre-load the default zone
    const defaultEntry = this.loadZoneCache(this.defaultZoneId);
    this.movement = new MovementSystem(defaultEntry.collision);
    this.combat = new CombatSystem(defaultEntry.collision);

    // Set the simulation interval (server tick)
    this.setSimulationInterval((dt) => this.update(dt), 1000 / SERVER_TICK_RATE);

    // Periodic save — every 30 seconds, persist all active players
    this.saveInterval = setInterval(() => this.saveAllPlayers(), SAVE_INTERVAL_MS);

    // Listen for player input messages
    this.onMessage(MessageType.INPUT, (client: Client, input: InputPayload) => {
      const queue = this.inputQueues.get(client.sessionId);
      if (queue) {
        // Cap the queue to prevent flooding
        if (queue.length < 30) {
          queue.push(input);
        }
      }
    });

    // Listen for equip/unequip messages
    this.onMessage(MessageType.EQUIP_ITEM, (client: Client, data: { slotIndex: number }) => {
      const player = this.state.players.get(client.sessionId);
      if (player && player.alive) {
        equipItem(player, data.slotIndex);
      }
    });

    this.onMessage(MessageType.UNEQUIP_ITEM, (client: Client, data: { slotType: string; targetIndex?: number }) => {
      const player = this.state.players.get(client.sessionId);
      if (player && player.alive) {
        if (data.targetIndex !== undefined && data.targetIndex >= 0) {
          unequipItemToSlot(player, data.slotType as EquipSlotType, data.targetIndex);
        } else {
          unequipItem(player, data.slotType as EquipSlotType);
        }
      }
    });

    // Drop item (from inventory or equipment)
    this.onMessage(MessageType.DROP_ITEM, (client: Client, data: { source: string; slotIndex?: number; slotType?: string }) => {
      const player = this.state.players.get(client.sessionId);
      if (!player || !player.alive) return;

      if (data.source === 'inventory' && data.slotIndex !== undefined) {
        dropInventoryItem(player, data.slotIndex);
      } else if (data.source === 'equipment' && data.slotType) {
        dropEquippedItem(player, data.slotType as EquipSlotType);
      }
    });

    // Swap inventory slots
    this.onMessage(MessageType.SWAP_INVENTORY, (client: Client, data: { fromIndex: number; toIndex: number }) => {
      const player = this.state.players.get(client.sessionId);
      if (!player || !player.alive) return;
      swapInventorySlots(player, data.fromIndex, data.toIndex);
    });

    console.log(`[GameRoom] Room created. Default zone: ${this.defaultZoneId}, Tick: ${SERVER_TICK_RATE}Hz`);
  }

  /**
   * Lazily load and cache a zone's collision, connections, and respawn point.
   */
  private loadZoneCache(zoneId: string): ZoneCacheEntry {
    const existing = this.zoneCache.get(zoneId);
    if (existing) return existing;

    const mapData = this.mapManager.loadZone(zoneId);
    const entry: ZoneCacheEntry = {
      collision: CollisionSystem.fromParsedMap(mapData),
      connections: this.mapManager.getZoneConnections(zoneId),
      respawnPoint: this.mapManager.getPlayerSpawn(zoneId),
    };
    this.zoneCache.set(zoneId, entry);
    return entry;
  }

  /**
   * Get the cached zone entry for a player's current zone.
   */
  private getPlayerZone(player: PlayerState): ZoneCacheEntry {
    return this.loadZoneCache(player.zoneId || this.defaultZoneId);
  }

  /**
   * Colyseus auth hook — validates JWT before allowing the client to join.
   * Returned value is stored on client.auth.
   */
  async onAuth(client: Client, options: { token?: string; characterId?: number }): Promise<JwtPayload> {
    if (!options?.token) {
      throw new Error('Authentication required.');
    }

    const payload = verifyToken(options.token);
    return payload;
  }

  onJoin(client: Client, options: { token?: string; characterId?: number }): void {
    const auth = (client as any).auth as JwtPayload;
    const characterId = options?.characterId;

    if (!characterId) {
      throw new Error('Character ID required.');
    }

    console.log(`[GameRoom] Player joining: ${auth.username} (user=${auth.userId}, char=${characterId})`);

    // ── Duplicate login prevention ──
    // If this user already has an active session, kick the old one
    for (const [existingSessionId, data] of this.sessionData) {
      if (data.userId === auth.userId) {
        const existingClient = this.clients.find(c => c.sessionId === existingSessionId);
        if (existingClient) {
          console.log(`[GameRoom] Kicking duplicate session for user ${auth.userId}`);
          existingClient.send('kicked', { reason: 'Logged in from another location.' });
          existingClient.leave(4001); // Custom close code
        }
        // Clean up old session data
        this.state.players.delete(existingSessionId);
        this.inputQueues.delete(existingSessionId);
        this.sessionData.delete(existingSessionId);
        break;
      }
    }

    // ── Load character from database ──
    let charData: LoadedCharacter;
    try {
      charData = loadCharacter(characterId, auth.userId);
    } catch (err: any) {
      throw new Error(`Failed to load character: ${err.message}`);
    }

    // ── Build PlayerState from loaded data ──
    const player = new PlayerState();
    player.id = client.sessionId;
    player.characterName = charData.name;
    player.classId = charData.classId;
    player.level = charData.level;
    player.xp = charData.xp;
    player.zoneId = this.defaultZoneId;

    // Compute stats from class + level
    const stats = computeDerivedStats(charData.classId as ClassId, charData.level);
    player.stats = stats;
    player.maxHp = stats.maxHp;
    player.maxMana = stats.maxMana;
    player.speed = stats.speed;

    // Restore vitals (clamped to max)
    player.hp = Math.min(charData.hp, stats.maxHp);
    player.mana = Math.min(charData.mana, stats.maxMana);
    player.alive = charData.alive;

    // Restore position
    player.x = charData.positionX;
    player.y = charData.positionY;

    // If the character was dead, respawn them fresh at zone spawn
    const playerZone = this.getPlayerZone(player);
    if (!player.alive) {
      player.alive = true;
      player.hp = stats.maxHp;
      player.mana = stats.maxMana;
      player.x = playerZone.respawnPoint.x;
      player.y = playerZone.respawnPoint.y;
    }

    // ── Restore equipment ──
    for (const equip of charData.equipment) {
      switch (equip.slotType) {
        case EquipSlotType.WEAPON: player.equipWeapon = equip.itemId; break;
        case EquipSlotType.HELM:   player.equipHelm = equip.itemId;   break;
        case EquipSlotType.CHEST:  player.equipChest = equip.itemId;  break;
        case EquipSlotType.LEGS:   player.equipLegs = equip.itemId;   break;
        case EquipSlotType.BOOTS:  player.equipBoots = equip.itemId;  break;
        case EquipSlotType.RING:   player.equipRing = equip.itemId;   break;
      }
    }

    // ── Restore inventory ──
    // Sort by slotIndex to maintain order
    const sortedInv = [...charData.inventory].sort((a, b) => a.slotIndex - b.slotIndex);
    for (const slot of sortedInv) {
      const invSlot = new InventorySlotState();
      invSlot.itemId = slot.itemId;
      invSlot.quantity = slot.quantity;
      player.inventory.push(invSlot);
    }

    // Register player in room state
    this.state.players.set(client.sessionId, player);
    this.inputQueues.set(client.sessionId, []);
    this.sessionData.set(client.sessionId, {
      characterId,
      userId: auth.userId,
      username: auth.username,
    });

    // Send full map data to the client for their zone
    const mapPayload = this.mapManager.getMapDataForClient(player.zoneId);
    client.send(MessageType.MAP_DATA, mapPayload);

    console.log(`[GameRoom] Player ${auth.username} joined as ${charData.classId} "${charData.name}" (Lv.${player.level}, HP:${player.hp}/${player.maxHp})`);
  }

  onLeave(client: Client): void {
    const data = this.sessionData.get(client.sessionId);

    // Save character state on disconnect
    if (data) {
      const player = this.state.players.get(client.sessionId);
      if (player) {
        this.savePlayer(client.sessionId, player, data.characterId);
        console.log(`[GameRoom] Saved and disconnected: ${data.username} (char=${data.characterId})`);
      }
    }

    this.state.players.delete(client.sessionId);
    this.inputQueues.delete(client.sessionId);
    this.sessionData.delete(client.sessionId);
  }

  /**
   * Compute and apply derived stats from class + level.
   * Call this on level-up and (later) gear changes.
   */
  applyStats(player: PlayerState): void {
    const stats = computeDerivedStats(player.classId as ClassId, player.level);
    player.stats = stats;
    player.maxHp = stats.maxHp;
    player.hp = stats.maxHp; // Full heal on stat recompute
    player.maxMana = stats.maxMana;
    player.mana = stats.maxMana;
    player.speed = stats.speed;
  }

  /**
   * Main server update loop — processes inputs, combat, projectiles, respawns, zone transitions.
   */
  private update(dt: number): void {
    const dtSec = dt / 1000;
    const now = Date.now();

    // 1. Process all queued inputs for every player
    this.state.players.forEach((player, sessionId) => {
      const queue = this.inputQueues.get(sessionId);
      if (!queue || queue.length === 0) return;

      // Get the collision system for this player's zone
      const zoneEntry = this.getPlayerZone(player);

      for (const input of queue) {
        // Only process movement if alive
        if (player.alive) {
          this.movement.processInput(player, input, dtSec, zoneEntry.collision);
        }

        // Handle fire input
        if (input.fire && player.alive) {
          const proj = this.combat.tryFire(player, now);
          if (proj) {
            this.state.projectiles.set(proj.id, proj);
          }
        }

        // Handle melee input
        if (input.melee && player.alive) {
          const events = this.combat.tryMelee(player, this.state.players, now);
          this.broadcastCombatEvents(events);
        }
      }

      // Clear the queue
      queue.length = 0;
    });

    // 2. Update projectiles (movement + collision)
    const { toRemove, events } = this.combat.updateProjectiles(
      this.state.projectiles,
      this.state.players,
      dtSec,
      now,
    );

    // Remove destroyed projectiles
    for (const id of toRemove) {
      this.state.projectiles.delete(id);
    }

    // Broadcast combat events (hits, kills)
    this.broadcastCombatEvents(events);

    // 3. Check respawns — handle per-player zone respawn points
    const respawnEvents = this.checkRespawnsMultiZone(now);
    this.broadcastCombatEvents(respawnEvents);

    // 4. Check zone transitions (portal triggers)
    this.checkZoneTransitions();
  }

  /**
   * Check if any player has walked into a zone portal trigger rect.
   * On transition: loads the target zone, updates player state, and sends new map data to the client.
   */
  private checkZoneTransitions(): void {
    this.state.players.forEach((player, sessionId) => {
      if (!player.alive) return;

      const zoneEntry = this.getPlayerZone(player);
      if (zoneEntry.connections.length === 0) return;

      for (const portal of zoneEntry.connections) {
        const rect = portal.triggerRect;
        if (
          player.x >= rect.x &&
          player.x <= rect.x + rect.width &&
          player.y >= rect.y &&
          player.y <= rect.y + rect.height
        ) {
          const client = this.clients.find(c => c.sessionId === sessionId);
          if (!client) break;

          const fromZone = player.zoneId;
          const toZone = portal.targetZone;

          // Ensure the target zone is loaded
          this.loadZoneCache(toZone);

          // Move player to the target zone
          player.zoneId = toZone;
          player.x = portal.targetSpawn.x;
          player.y = portal.targetSpawn.y;

          // Send the full map data for the new zone so the client can rebuild its tile map
          const mapPayload = this.mapManager.getMapDataForClient(toZone);
          client.send(MessageType.MAP_DATA, mapPayload);

          // Also send the zone change notification (with spawn coords for immediate repositioning)
          client.send(MessageType.ZONE_CHANGE, {
            zoneId: toZone,
            spawnX: portal.targetSpawn.x,
            spawnY: portal.targetSpawn.y,
          });

          console.log(`[GameRoom] Player ${sessionId} zone transition: ${fromZone} → ${toZone} via "${portal.id}"`);
          break; // Only one portal per tick
        }
      }
    });
  }

  /**
   * Multi-zone respawn: each dead player respawns at their zone's spawn point.
   */
  private checkRespawnsMultiZone(now: number): CombatEvent[] {
    const events: CombatEvent[] = [];

    this.state.players.forEach((player) => {
      if (player.alive) return;
      if (player.respawnAt === 0 || now < player.respawnAt) return;

      const zone = this.getPlayerZone(player);

      player.alive = true;
      player.hp = player.maxHp;
      player.mana = player.maxMana;
      player.respawnAt = 0;
      player.invulnerableUntil = now + 6000; // extra i-frames on respawn

      player.x = zone.respawnPoint.x;
      player.y = zone.respawnPoint.y;

      events.push({
        type: 'playerRespawned',
        data: { playerId: player.id },
      });
    });

    return events;
  }

  /**
   * Send combat events to all clients.
   */
  private broadcastCombatEvents(events: CombatEvent[]): void {
    for (const event of events) {
      this.broadcast(event.type, event.data);
    }
  }

  // ── Persistence Helpers ──────────────────────────────────

  /**
   * Extract current player state and save to database.
   */
  private savePlayer(sessionId: string, player: PlayerState, characterId: number): void {
    try {
      // Build inventory data from ArraySchema
      const inventory = [];
      for (let i = 0; i < player.inventory.length; i++) {
        const slot = player.inventory[i];
        inventory.push({
          slotIndex: i,
          itemId: slot.itemId,
          quantity: slot.quantity,
        });
      }

      // Build equipment data
      const equipment = [];
      if (player.equipWeapon) equipment.push({ slotType: EquipSlotType.WEAPON, itemId: player.equipWeapon });
      if (player.equipHelm)   equipment.push({ slotType: EquipSlotType.HELM,   itemId: player.equipHelm });
      if (player.equipChest)  equipment.push({ slotType: EquipSlotType.CHEST,  itemId: player.equipChest });
      if (player.equipLegs)   equipment.push({ slotType: EquipSlotType.LEGS,   itemId: player.equipLegs });
      if (player.equipBoots)  equipment.push({ slotType: EquipSlotType.BOOTS,  itemId: player.equipBoots });
      if (player.equipRing)   equipment.push({ slotType: EquipSlotType.RING,   itemId: player.equipRing });

      const data: SaveCharacterData = {
        hp: player.hp,
        mana: player.mana,
        xp: player.xp,
        level: player.level,
        positionX: player.x,
        positionY: player.y,
        alive: player.alive,
        inventory,
        equipment,
      };

      saveCharacter(characterId, data);
    } catch (err: any) {
      console.error(`[GameRoom] Failed to save player ${sessionId}: ${err.message}`);
    }
  }

  /**
   * Save all connected players. Called periodically by the save interval.
   */
  private saveAllPlayers(): void {
    let saved = 0;
    this.state.players.forEach((player, sessionId) => {
      const data = this.sessionData.get(sessionId);
      if (data) {
        this.savePlayer(sessionId, player, data.characterId);
        saved++;
      }
    });
    if (saved > 0) {
      console.log(`[GameRoom] Periodic save: ${saved} player(s) saved.`);
    }
  }

  onDispose(): void {
    // Save all players on room dispose
    this.saveAllPlayers();

    // Clear the save interval
    if (this.saveInterval) {
      clearInterval(this.saveInterval);
      this.saveInterval = null;
    }

    console.log('[GameRoom] Room disposed.');
  }
}
