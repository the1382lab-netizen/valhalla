import { Room } from '@colyseus/core';
import { GameState } from '../schema/GameState.js';
import { PlayerState, InventorySlotState } from '../schema/PlayerState.js';
import { CollisionSystem } from '../systems/CollisionSystem.js';
import { MovementSystem } from '../systems/MovementSystem.js';
import { CombatSystem } from '../systems/CombatSystem.js';
import { MapManager } from '../systems/MapManager.js';
import { MessageType, SERVER_TICK_RATE, EquipSlotType, SAVE_INTERVAL_MS, computeDerivedStats, ZoneId, } from '@valhalla/shared';
import { equipItem, unequipItem, unequipItemToSlot, dropInventoryItem, dropEquippedItem, swapInventorySlots } from '../systems/InventorySystem.js';
import { verifyToken } from '../services/AuthService.js';
import { loadCharacter, saveCharacter, } from '../services/CharacterService.js';
export class GameRoom extends Room {
    constructor() {
        super(...arguments);
        this.inputQueues = new Map();
        /** Maps sessionId → persistent character data for save/load. */
        this.sessionData = new Map();
        /** Interval handle for periodic saves. */
        this.saveInterval = null;
        /** Current zone ID and spawn point for this room. */
        this.currentZoneId = ZoneId.GRASSLANDS;
        this.respawnPoint = { x: 352, y: 352 };
        /** Zone connections for portal detection. */
        this.zoneConnections = [];
    }
    onCreate() {
        this.setState(new GameState());
        // Initialize map system
        this.mapManager = new MapManager();
        const mapData = this.mapManager.loadZone(this.currentZoneId);
        // Build collision from loaded map
        this.collision = CollisionSystem.fromParsedMap(mapData);
        this.movement = new MovementSystem(this.collision);
        this.combat = new CombatSystem(this.collision);
        // Cache spawn point and zone connections
        this.respawnPoint = this.mapManager.getPlayerSpawn(this.currentZoneId);
        this.zoneConnections = this.mapManager.getZoneConnections(this.currentZoneId);
        // Set the simulation interval (server tick)
        this.setSimulationInterval((dt) => this.update(dt), 1000 / SERVER_TICK_RATE);
        // Periodic save — every 30 seconds, persist all active players
        this.saveInterval = setInterval(() => this.saveAllPlayers(), SAVE_INTERVAL_MS);
        // Listen for player input messages
        this.onMessage(MessageType.INPUT, (client, input) => {
            const queue = this.inputQueues.get(client.sessionId);
            if (queue) {
                // Cap the queue to prevent flooding
                if (queue.length < 30) {
                    queue.push(input);
                }
            }
        });
        // Listen for equip/unequip messages
        this.onMessage(MessageType.EQUIP_ITEM, (client, data) => {
            const player = this.state.players.get(client.sessionId);
            if (player && player.alive) {
                equipItem(player, data.slotIndex);
            }
        });
        this.onMessage(MessageType.UNEQUIP_ITEM, (client, data) => {
            const player = this.state.players.get(client.sessionId);
            if (player && player.alive) {
                if (data.targetIndex !== undefined && data.targetIndex >= 0) {
                    unequipItemToSlot(player, data.slotType, data.targetIndex);
                }
                else {
                    unequipItem(player, data.slotType);
                }
            }
        });
        // Drop item (from inventory or equipment)
        this.onMessage(MessageType.DROP_ITEM, (client, data) => {
            const player = this.state.players.get(client.sessionId);
            if (!player || !player.alive)
                return;
            if (data.source === 'inventory' && data.slotIndex !== undefined) {
                dropInventoryItem(player, data.slotIndex);
            }
            else if (data.source === 'equipment' && data.slotType) {
                dropEquippedItem(player, data.slotType);
            }
        });
        // Swap inventory slots
        this.onMessage(MessageType.SWAP_INVENTORY, (client, data) => {
            const player = this.state.players.get(client.sessionId);
            if (!player || !player.alive)
                return;
            swapInventorySlots(player, data.fromIndex, data.toIndex);
        });
        console.log(`[GameRoom] Room created. Zone: ${this.currentZoneId}, Map: ${mapData.width}×${mapData.height}, Tick: ${SERVER_TICK_RATE}Hz`);
    }
    /**
     * Colyseus auth hook — validates JWT before allowing the client to join.
     * Returned value is stored on client.auth.
     */
    async onAuth(client, options) {
        if (!options?.token) {
            throw new Error('Authentication required.');
        }
        const payload = verifyToken(options.token);
        return payload;
    }
    onJoin(client, options) {
        const auth = client.auth;
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
        let charData;
        try {
            charData = loadCharacter(characterId, auth.userId);
        }
        catch (err) {
            throw new Error(`Failed to load character: ${err.message}`);
        }
        // ── Build PlayerState from loaded data ──
        const player = new PlayerState();
        player.id = client.sessionId;
        player.characterName = charData.name;
        player.classId = charData.classId;
        player.level = charData.level;
        player.xp = charData.xp;
        player.zoneId = this.currentZoneId;
        // Compute stats from class + level
        const stats = computeDerivedStats(charData.classId, charData.level);
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
        if (!player.alive) {
            player.alive = true;
            player.hp = stats.maxHp;
            player.mana = stats.maxMana;
            player.x = this.respawnPoint.x;
            player.y = this.respawnPoint.y;
        }
        // ── Restore equipment ──
        for (const equip of charData.equipment) {
            switch (equip.slotType) {
                case EquipSlotType.WEAPON:
                    player.equipWeapon = equip.itemId;
                    break;
                case EquipSlotType.HELM:
                    player.equipHelm = equip.itemId;
                    break;
                case EquipSlotType.CHEST:
                    player.equipChest = equip.itemId;
                    break;
                case EquipSlotType.LEGS:
                    player.equipLegs = equip.itemId;
                    break;
                case EquipSlotType.BOOTS:
                    player.equipBoots = equip.itemId;
                    break;
                case EquipSlotType.RING:
                    player.equipRing = equip.itemId;
                    break;
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
        // Send full map data to the client (replaces old collisionGrid message)
        const mapPayload = this.mapManager.getMapDataForClient(this.currentZoneId);
        client.send(MessageType.MAP_DATA, mapPayload);
        console.log(`[GameRoom] Player ${auth.username} joined as ${charData.classId} "${charData.name}" (Lv.${player.level}, HP:${player.hp}/${player.maxHp})`);
    }
    onLeave(client) {
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
    applyStats(player) {
        const stats = computeDerivedStats(player.classId, player.level);
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
    update(dt) {
        const dtSec = dt / 1000;
        const now = Date.now();
        // 1. Process all queued inputs for every player
        this.state.players.forEach((player, sessionId) => {
            const queue = this.inputQueues.get(sessionId);
            if (!queue || queue.length === 0)
                return;
            for (const input of queue) {
                // Only process movement if alive
                if (player.alive) {
                    this.movement.processInput(player, input, dtSec);
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
        const { toRemove, events } = this.combat.updateProjectiles(this.state.projectiles, this.state.players, dtSec, now);
        // Remove destroyed projectiles
        for (const id of toRemove) {
            this.state.projectiles.delete(id);
        }
        // Broadcast combat events (hits, kills)
        this.broadcastCombatEvents(events);
        // 3. Check respawns
        const respawnEvents = this.combat.checkRespawns(this.state.players, now, this.respawnPoint);
        this.broadcastCombatEvents(respawnEvents);
        // 4. Check zone transitions (portal triggers)
        this.checkZoneTransitions();
    }
    /**
     * Check if any player has walked into a zone portal trigger rect.
     */
    checkZoneTransitions() {
        if (this.zoneConnections.length === 0)
            return;
        this.state.players.forEach((player, sessionId) => {
            if (!player.alive)
                return;
            for (const portal of this.zoneConnections) {
                const rect = portal.triggerRect;
                if (player.x >= rect.x &&
                    player.x <= rect.x + rect.width &&
                    player.y >= rect.y &&
                    player.y <= rect.y + rect.height) {
                    // Player stepped into a portal!
                    // For now, teleport them to the target spawn within the same zone
                    // (full zone transition to different rooms comes in a later phase)
                    const client = this.clients.find(c => c.sessionId === sessionId);
                    if (client) {
                        // Teleport to target spawn
                        player.x = portal.targetSpawn.x;
                        player.y = portal.targetSpawn.y;
                        // Notify client about the zone change
                        client.send(MessageType.ZONE_CHANGE, {
                            zoneId: portal.targetZone,
                            spawnX: portal.targetSpawn.x,
                            spawnY: portal.targetSpawn.y,
                        });
                        console.log(`[GameRoom] Player ${sessionId} triggered portal "${portal.id}" → ${portal.targetZone}`);
                    }
                    break; // Only one portal per tick
                }
            }
        });
    }
    /**
     * Send combat events to all clients.
     */
    broadcastCombatEvents(events) {
        for (const event of events) {
            this.broadcast(event.type, event.data);
        }
    }
    // ── Persistence Helpers ──────────────────────────────────
    /**
     * Extract current player state and save to database.
     */
    savePlayer(sessionId, player, characterId) {
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
            if (player.equipWeapon)
                equipment.push({ slotType: EquipSlotType.WEAPON, itemId: player.equipWeapon });
            if (player.equipHelm)
                equipment.push({ slotType: EquipSlotType.HELM, itemId: player.equipHelm });
            if (player.equipChest)
                equipment.push({ slotType: EquipSlotType.CHEST, itemId: player.equipChest });
            if (player.equipLegs)
                equipment.push({ slotType: EquipSlotType.LEGS, itemId: player.equipLegs });
            if (player.equipBoots)
                equipment.push({ slotType: EquipSlotType.BOOTS, itemId: player.equipBoots });
            if (player.equipRing)
                equipment.push({ slotType: EquipSlotType.RING, itemId: player.equipRing });
            const data = {
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
        }
        catch (err) {
            console.error(`[GameRoom] Failed to save player ${sessionId}: ${err.message}`);
        }
    }
    /**
     * Save all connected players. Called periodically by the save interval.
     */
    saveAllPlayers() {
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
    onDispose() {
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
//# sourceMappingURL=GameRoom.js.map