import { Room } from '@colyseus/core';
import { GameState } from '../schema/GameState.js';
import { PlayerState, InventorySlotState } from '../schema/PlayerState.js';
import { CollisionSystem } from '../systems/CollisionSystem.js';
import { MovementSystem } from '../systems/MovementSystem.js';
import { CombatSystem } from '../systems/CombatSystem.js';
import { MapManager } from '../systems/MapManager.js';
import { SkillSystem } from '../systems/SkillSystem.js';
import { DataManager } from '../systems/DataManager.js';
import { NPCSystem } from '../systems/NPCSystem.js';
import { SpellProjectileSystem } from '../systems/SpellProjectileSystem.js';
import { MessageType, SERVER_TICK_RATE, EquipSlotType, SAVE_INTERVAL_MS, computeDerivedStats, ZoneId, ACTION_BAR_SLOTS, hasRangedAttack, } from '@valhalla/shared';
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
        /** Default zone for new players. */
        this.defaultZoneId = ZoneId.GRASSLANDS;
        /** Per-zone collision, connections, and respawn caches. */
        this.zoneCache = new Map();
    }
    onCreate() {
        this.setState(new GameState());
        // Initialize data manager — loads JSON data files from editor
        DataManager.initialize();
        // Initialize map system
        this.mapManager = new MapManager();
        // Pre-load the default zone
        const defaultEntry = this.loadZoneCache(this.defaultZoneId);
        this.movement = new MovementSystem(defaultEntry.collision);
        this.combat = new CombatSystem(defaultEntry.collision);
        this.spellProjectileSystem = new SpellProjectileSystem(defaultEntry.collision);
        this.skillSystem = new SkillSystem();
        this.npcSystem = new NPCSystem();
        // Spawn NPCs for all known zones
        for (const zoneId of Object.keys(DataManager.instance.zones)) {
            this.loadZoneCache(zoneId); // ensure zone is loaded
            this.npcSystem.spawnZone(zoneId, this.mapManager, this.state.npcs);
        }
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
        // ── Skill System Messages ──
        // Cast a skill
        this.onMessage(MessageType.CAST_SKILL, (client, data) => {
            const player = this.state.players.get(client.sessionId);
            if (!player)
                return;
            const now = Date.now();
            const events = this.skillSystem.tryStartCast(player, data.skillId, data.targetId ?? null, this.state.players, now, this.state.spellProjectiles, data.groundX ?? null, data.groundY ?? null);
            this.broadcastSkillEvents(events, client);
        });
        // Cancel an active cast
        this.onMessage(MessageType.CANCEL_CAST, (client) => {
            const player = this.state.players.get(client.sessionId);
            if (!player)
                return;
            const events = this.skillSystem.cancelCast(player, Date.now());
            this.broadcastSkillEvents(events, client);
        });
        // Set action bar slots
        this.onMessage(MessageType.SET_ACTION_BAR, (client, data) => {
            const player = this.state.players.get(client.sessionId);
            if (!player)
                return;
            // Validate: each skill must belong to the player's class (or be empty)
            const classSkills = DataManager.instance.getClassSkills(player.classId);
            const validated = [];
            for (let i = 0; i < ACTION_BAR_SLOTS; i++) {
                const skillId = data.slots?.[i] ?? '';
                if (skillId === '' || classSkills.includes(skillId)) {
                    validated.push(skillId);
                }
                else {
                    validated.push(''); // Invalid skill — clear the slot
                }
            }
            player.actionBar = validated;
        });
        // ── Chat ──────────────────────────────────────────────────
        this.onMessage(MessageType.CHAT_MESSAGE, (client, data) => {
            const player = this.state.players.get(client.sessionId);
            if (!player)
                return;
            // Validate
            if (!data.message || typeof data.message !== 'string')
                return;
            const trimmed = data.message.trim().slice(0, 200);
            if (trimmed.length === 0)
                return;
            const payload = {
                channel: data.channel,
                senderName: player.characterName,
                message: trimmed,
                timestamp: Date.now(),
                targetName: data.targetName,
            };
            if (data.channel === 'world') {
                this.broadcast(MessageType.CHAT_MESSAGE, payload);
            }
            else if (data.channel === 'general') {
                // Send only to players in the same zone
                const senderZone = player.zoneId;
                for (const c of this.clients) {
                    const p = this.state.players.get(c.sessionId);
                    if (p?.zoneId === senderZone) {
                        c.send(MessageType.CHAT_MESSAGE, payload);
                    }
                }
            }
            else if (data.channel === 'whisper') {
                if (!data.targetName)
                    return;
                // Find target client by character name
                let targetClient = null;
                for (const c of this.clients) {
                    const p = this.state.players.get(c.sessionId);
                    if (p?.characterName === data.targetName) {
                        targetClient = c;
                        break;
                    }
                }
                if (!targetClient) {
                    // Player not found — send system error to sender only
                    client.send(MessageType.CHAT_MESSAGE, {
                        channel: 'system',
                        senderName: '',
                        message: `Player "${data.targetName}" is not online.`,
                        timestamp: Date.now(),
                    });
                    return;
                }
                // Deliver to both sender and recipient
                client.send(MessageType.CHAT_MESSAGE, payload);
                if (targetClient.sessionId !== client.sessionId) {
                    targetClient.send(MessageType.CHAT_MESSAGE, payload);
                }
            }
        });
        console.log(`[GameRoom] Room created. Default zone: ${this.defaultZoneId}, Tick: ${SERVER_TICK_RATE}Hz`);
    }
    /**
     * Lazily load and cache a zone's collision, connections, and respawn point.
     */
    loadZoneCache(zoneId) {
        const existing = this.zoneCache.get(zoneId);
        if (existing)
            return existing;
        const mapData = this.mapManager.loadZone(zoneId);
        const entry = {
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
    getPlayerZone(player) {
        return this.loadZoneCache(player.zoneId || this.defaultZoneId);
    }
    /**
     * Find a safe (non-colliding) position near the given coordinates.
     * Uses a spiral search pattern, expanding outward by one tile at a time.
     * Returns the original position if already safe, or the zone respawn as last resort.
     */
    findSafeSpawn(x, y, collision, fallback) {
        if (!collision.isCircleBlocked(x, y)) {
            return { x, y };
        }
        const ts = collision.getTileSize();
        // Spiral outward up to 10 tiles away
        for (let dist = 1; dist <= 10; dist++) {
            for (let dx = -dist; dx <= dist; dx++) {
                for (let dy = -dist; dy <= dist; dy++) {
                    // Only check the ring at this distance, not interior
                    if (Math.abs(dx) !== dist && Math.abs(dy) !== dist)
                        continue;
                    const testX = x + dx * ts;
                    const testY = y + dy * ts;
                    if (!collision.isCircleBlocked(testX, testY)) {
                        return { x: testX, y: testY };
                    }
                }
            }
        }
        // Couldn't find safe spot nearby — use zone respawn point
        console.warn(`[GameRoom] Could not find safe spawn near (${x}, ${y}), using zone respawn`);
        return fallback;
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
        // Restore saved zone — fall back to default if unknown/missing/invalid
        const savedZone = charData.zoneId;
        const isValidZone = savedZone && DataManager.instance.isValidZone(savedZone);
        player.zoneId = isValidZone ? savedZone : this.defaultZoneId;
        // Compute stats from class + level
        const stats = computeDerivedStats(charData.classId, charData.level);
        player.stats = stats;
        player.maxHp = stats.maxHp;
        player.maxMana = stats.maxMana;
        player.speed = stats.speed;
        // Restore vitals (clamped to max)
        player.hp = Math.min(charData.hp, stats.maxHp);
        player.mana = Math.min(charData.mana, stats.maxMana);
        player.maxEnergy = stats.maxEnergy;
        player.energy = stats.maxEnergy; // Energy starts full on login
        player.alive = charData.alive;
        // Restore position
        player.x = charData.positionX;
        player.y = charData.positionY;
        // Load the player's zone collision data
        const playerZone = this.getPlayerZone(player);
        // If the character was dead, respawn them fresh at zone spawn
        if (!player.alive) {
            player.alive = true;
            player.hp = stats.maxHp;
            player.mana = stats.maxMana;
            player.x = playerZone.respawnPoint.x;
            player.y = playerZone.respawnPoint.y;
        }
        // Validate restored position — nudge out of walls if stuck
        const safePos = this.findSafeSpawn(player.x, player.y, playerZone.collision, playerZone.respawnPoint);
        player.x = safePos.x;
        player.y = safePos.y;
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
        // Send full map data to the client for their zone
        const mapPayload = this.mapManager.getMapDataForClient(player.zoneId);
        client.send(MessageType.MAP_DATA, mapPayload);
        // Restore action bar from DB and send to client
        if (charData.actionBar && charData.actionBar.length > 0) {
            player.actionBar = charData.actionBar;
        }
        client.send(MessageType.ACTION_BAR_DATA, { slots: player.actionBar });
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
        player.maxEnergy = stats.maxEnergy;
        player.energy = stats.maxEnergy;
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
            // Get the collision system for this player's zone
            const zoneEntry = this.getPlayerZone(player);
            for (const input of queue) {
                // Only process movement if alive
                if (player.alive) {
                    this.movement.processInput(player, input, dtSec, zoneEntry.collision);
                }
                // Handle fire input — only Rangers support a ranged basic attack
                if (input.fire && player.alive && hasRangedAttack(player.classId)) {
                    const proj = this.combat.tryFire(player, now);
                    if (proj) {
                        this.state.projectiles.set(proj.id, proj);
                    }
                }
                // Handle melee input
                if (input.melee && player.alive) {
                    const events = this.combat.tryMelee(player, this.state.players, this.state.npcs, this.npcSystem, now);
                    this.broadcastCombatEvents(events);
                }
            }
            // Clear the queue
            queue.length = 0;
        });
        // 2. Update projectiles (movement + collision)
        const { toRemove, events } = this.combat.updateProjectiles(this.state.projectiles, this.state.players, this.state.npcs, this.npcSystem, dtSec, now);
        // Remove destroyed projectiles
        for (const id of toRemove) {
            this.state.projectiles.delete(id);
        }
        // Broadcast combat events (hits, kills)
        this.broadcastCombatEvents(events);
        // 3. Spell projectile update (Fireball and other spell projectiles)
        const { toRemove: spellToRemove, events: spellEvents } = this.spellProjectileSystem.update(this.state.spellProjectiles, this.state.players, this.state.npcs, this.npcSystem, dtSec, now);
        for (const id of spellToRemove) {
            this.state.spellProjectiles.delete(id);
        }
        this.broadcastSpellProjectileEvents(spellEvents);
        // 4. Skill system update (cast progression, energy regen, buff ticking)
        const skillEvents = this.skillSystem.update(this.state.players, dtSec, now, this.state.spellProjectiles);
        this.broadcastSkillEvents(skillEvents);
        // 5. Check respawns — handle per-player zone respawn points
        const respawnEvents = this.checkRespawnsMultiZone(now);
        this.broadcastCombatEvents(respawnEvents);
        // 6. Update NPC system (aggro, movement, respawns, NPC attacks)
        const npcEvents = this.npcSystem.update(dtSec, now, this.state.players, this.state.npcs, (zoneId) => this.zoneCache.get(zoneId)?.collision ?? null);
        this.broadcastCombatEvents(npcEvents);
        // 7. Check zone transitions (portal triggers)
        this.checkZoneTransitions();
    }
    /**
     * Check if any player has walked into a zone portal trigger rect.
     * On transition: loads the target zone, updates player state, and sends new map data to the client.
     */
    checkZoneTransitions() {
        this.state.players.forEach((player, sessionId) => {
            if (!player.alive)
                return;
            const zoneEntry = this.getPlayerZone(player);
            if (zoneEntry.connections.length === 0)
                return;
            for (const portal of zoneEntry.connections) {
                const rect = portal.triggerRect;
                if (player.x >= rect.x &&
                    player.x <= rect.x + rect.width &&
                    player.y >= rect.y &&
                    player.y <= rect.y + rect.height) {
                    const client = this.clients.find(c => c.sessionId === sessionId);
                    if (!client)
                        break;
                    const fromZone = player.zoneId;
                    const toZone = portal.targetZone;
                    // Load the target zone and find a safe spawn position
                    const targetEntry = this.loadZoneCache(toZone);
                    const safeSpawn = this.findSafeSpawn(portal.targetSpawn.x, portal.targetSpawn.y, targetEntry.collision, targetEntry.respawnPoint);
                    player.zoneId = toZone;
                    player.x = safeSpawn.x;
                    player.y = safeSpawn.y;
                    // Send the full map data for the new zone so the client can rebuild its tile map
                    const mapPayload = this.mapManager.getMapDataForClient(toZone);
                    client.send(MessageType.MAP_DATA, mapPayload);
                    // Also send the zone change notification (with spawn coords for immediate repositioning)
                    client.send(MessageType.ZONE_CHANGE, {
                        zoneId: toZone,
                        spawnX: safeSpawn.x,
                        spawnY: safeSpawn.y,
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
    checkRespawnsMultiZone(now) {
        const events = [];
        this.state.players.forEach((player) => {
            if (player.alive)
                return;
            if (player.respawnAt === 0 || now < player.respawnAt)
                return;
            const zone = this.getPlayerZone(player);
            player.alive = true;
            player.hp = player.maxHp;
            player.mana = player.maxMana;
            player.respawnAt = 0;
            player.invulnerableUntil = now + 6000; // extra i-frames on respawn
            const safeRespawn = this.findSafeSpawn(zone.respawnPoint.x, zone.respawnPoint.y, zone.collision, zone.respawnPoint);
            player.x = safeRespawn.x;
            player.y = safeRespawn.y;
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
    broadcastCombatEvents(events) {
        for (const event of events) {
            this.broadcast(event.type, event.data);
        }
    }
    /**
     * Send spell projectile events to all clients.
     * playerHit/playerDied/npcHit/npcDied use the same message types as combat.
     * spellImpact is a new VFX event the client uses to play the explosion.
     */
    broadcastSpellProjectileEvents(events) {
        for (const event of events) {
            if (event.type === 'spellImpact') {
                this.broadcast(MessageType.SPELL_IMPACT, event.data);
            }
            else {
                // playerHit, playerDied, npcHit, npcDied — reuse existing message types
                this.broadcast(event.type, event.data);
            }
        }
    }
    /**
     * Send skill system events to relevant clients.
     * Some events go only to the caster, others are broadcast.
     */
    broadcastSkillEvents(events, sourceClient) {
        for (const event of events) {
            switch (event.type) {
                case 'castFailed':
                    // Only tell the caster about failures
                    if (sourceClient) {
                        sourceClient.send(MessageType.SKILL_FAILED, { reason: event.reason });
                    }
                    break;
                case 'castStarted':
                    // Broadcast so others can see cast bars
                    this.broadcast(MessageType.SKILL_STARTED, {
                        casterId: event.casterId,
                        skillId: event.skillId,
                        castTimeMs: event.castTimeMs,
                    });
                    break;
                case 'castComplete':
                    // Broadcast skill effects (damage numbers, heals, etc.)
                    for (const fx of event.events) {
                        this.broadcast(MessageType.SKILL_EFFECT, {
                            casterId: event.casterId,
                            skillId: event.skillId,
                            ...fx,
                        });
                    }
                    break;
                case 'castInterrupted':
                    this.broadcast(MessageType.SKILL_INTERRUPTED, {
                        casterId: event.casterId,
                        skillId: event.skillId,
                    });
                    break;
                case 'buffExpired':
                    this.broadcast(MessageType.BUFF_REMOVED, {
                        targetId: event.targetId,
                        skillId: event.skillId,
                    });
                    break;
                case 'dotTick':
                    this.broadcast(MessageType.SKILL_EFFECT, {
                        type: 'damage',
                        targetId: event.targetId,
                        skillId: event.skillId,
                        damage: event.damage,
                        isCrit: false,
                    });
                    break;
                case 'hotTick':
                    this.broadcast(MessageType.SKILL_EFFECT, {
                        type: 'heal',
                        targetId: event.targetId,
                        skillId: event.skillId,
                        amount: event.heal,
                    });
                    break;
            }
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
                zoneId: player.zoneId || this.defaultZoneId,
                alive: player.alive,
                inventory,
                equipment,
                actionBar: player.actionBar,
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