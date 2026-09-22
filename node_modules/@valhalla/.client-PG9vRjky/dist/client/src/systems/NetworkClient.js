import { Client, Callbacks } from '@colyseus/sdk';
import { SERVER_URL, ROOM_NAME, MessageType } from '@valhalla/shared';
/**
 * Manages the Colyseus connection to the server.
 */
export class NetworkClient {
    client;
    room = null;
    // Callbacks
    onStateChange = null;
    onPlayerAdd = null;
    onPlayerRemove = null;
    onPlayerChange = null;
    onCollisionGrid = null;
    onMapData = null;
    onZoneChange = null;
    // Projectile callbacks
    onProjectileAdd = null;
    onProjectileRemove = null;
    onProjectileChange = null;
    // Spell projectile callbacks (e.g. Fireball)
    onSpellProjectileAdd = null;
    onSpellProjectileRemove = null;
    onSpellProjectileChange = null;
    // Spell impact VFX callback
    onSpellImpact = null;
    // NPC callbacks
    onNpcAdd = null;
    onNpcRemove = null;
    onNpcChange = null;
    // Combat event callbacks
    onPlayerHit = null;
    onPlayerDied = null;
    onPlayerRespawned = null;
    onMeleeAttack = null;
    onMissed = null;
    onDodged = null;
    onBlocked = null;
    // NPC combat callbacks
    onNpcHit = null;
    onNpcDied = null;
    // Inventory & equipment callbacks
    onInventoryChange = null;
    onEquipmentChange = null;
    // Skill system callbacks
    onSkillStarted = null;
    onSkillEffect = null;
    onSkillFailed = null;
    onSkillInterrupted = null;
    onBuffApplied = null;
    onBuffRemoved = null;
    onActionBarData = null;
    // Loot bag callbacks
    onLootBagAdd = null;
    onLootBagRemove = null;
    onLootBagChange = null;
    // Direct server confirmation that a loot action succeeded — bypasses schema callbacks
    onLootSuccess = null;
    // Live reference to the local player's schema (set once on join, used for refreshInventory)
    localPlayerRef = null;
    // Chat callback
    onChatMessage = null;
    // Level-up callback
    onLevelUp = null;
    // XP gained callback
    onXpGained = null;
    // Party callbacks
    onPartyUpdate = null;
    constructor() {
        this.client = new Client(SERVER_URL);
    }
    get sessionId() {
        return this.room?.sessionId;
    }
    async connect(options) {
        try {
            this.room = await this.client.joinOrCreate(ROOM_NAME, options);
            console.log(`[Network] Connected as ${this.room.sessionId}`);
            // Listen for map data (new multi-layer system)
            this.room.onMessage(MessageType.MAP_DATA, (data) => {
                console.log(`[Network] Received map data for zone "${data.zoneId}" (${data.width}×${data.height})`);
                this.onMapData?.(data);
                // Also fire legacy callback for backward compatibility
                this.onCollisionGrid?.({
                    grid: data.collisionGrid,
                    width: data.width,
                    height: data.height,
                    tileSize: data.tileSize,
                });
            });
            // Listen for zone change notifications
            this.room.onMessage(MessageType.ZONE_CHANGE, (data) => {
                console.log(`[Network] Zone change → ${data.zoneId}`);
                this.onZoneChange?.(data);
            });
            // Legacy collision grid handler (in case server still sends old format)
            this.room.onMessage('collisionGrid', (data) => {
                console.log('[Network] Received legacy collision grid');
                this.onCollisionGrid?.(data);
            });
            // Combat event messages
            this.room.onMessage(MessageType.PLAYER_HIT, (data) => {
                this.onPlayerHit?.(data);
            });
            this.room.onMessage(MessageType.PLAYER_DIED, (data) => {
                this.onPlayerDied?.(data);
            });
            this.room.onMessage(MessageType.PLAYER_RESPAWNED, (data) => {
                this.onPlayerRespawned?.(data);
            });
            this.room.onMessage(MessageType.MELEE_ATTACK, (data) => {
                this.onMeleeAttack?.(data);
            });
            this.room.onMessage(MessageType.MISSED, (data) => {
                this.onMissed?.(data);
            });
            this.room.onMessage(MessageType.DODGED, (data) => {
                this.onDodged?.(data);
            });
            this.room.onMessage(MessageType.BLOCKED, (data) => {
                this.onBlocked?.(data);
            });
            // NPC combat events
            this.room.onMessage(MessageType.NPC_HIT, (data) => {
                this.onNpcHit?.(data);
            });
            this.room.onMessage(MessageType.NPC_DIED, (data) => {
                this.onNpcDied?.(data);
            });
            // ── Skill system messages ──
            this.room.onMessage(MessageType.SKILL_STARTED, (data) => {
                this.onSkillStarted?.(data);
            });
            this.room.onMessage(MessageType.SKILL_EFFECT, (data) => {
                this.onSkillEffect?.(data);
            });
            this.room.onMessage(MessageType.SKILL_FAILED, (data) => {
                this.onSkillFailed?.(data);
            });
            this.room.onMessage(MessageType.SKILL_INTERRUPTED, (data) => {
                this.onSkillInterrupted?.(data);
            });
            this.room.onMessage(MessageType.BUFF_APPLIED, (data) => {
                this.onBuffApplied?.(data);
            });
            this.room.onMessage(MessageType.BUFF_REMOVED, (data) => {
                this.onBuffRemoved?.(data);
            });
            this.room.onMessage(MessageType.ACTION_BAR_DATA, (data) => {
                this.onActionBarData?.(data);
            });
            // Spell impact VFX
            this.room.onMessage(MessageType.SPELL_IMPACT, (data) => {
                this.onSpellImpact?.(data);
            });
            // Chat messages
            this.room.onMessage(MessageType.CHAT_MESSAGE, (data) => {
                this.onChatMessage?.(data);
            });
            // Loot success — direct server confirmation to force-refresh UI panels.
            // This fires AFTER the state patch has already been applied, so reading
            // the live schema in refreshInventory() gives the correct updated state.
            this.room.onMessage(MessageType.LOOT_SUCCESS, (data) => {
                this.onLootSuccess?.(data.bagId);
            });
            this.room.onMessage(MessageType.LEVEL_UP, (data) => {
                this.onLevelUp?.(data.level);
            });
            this.room.onMessage(MessageType.XP_GAINED, (data) => {
                this.onXpGained?.(data.amount);
            });
            // Party update
            this.room.onMessage(MessageType.PARTY_UPDATE, (data) => {
                this.onPartyUpdate?.(data.members);
            });
            // Use Colyseus 0.17 Callbacks API for state change listeners
            const callbacks = Callbacks.get(this.room);
            callbacks.onAdd('players', (player, key) => {
                const sessionId = key;
                const isLocal = sessionId === this.room?.sessionId;
                this.onPlayerAdd?.(player, sessionId);
                // Helpers for local player sync
                const fireInventoryChange = isLocal ? () => {
                    if (!player.inventory)
                        return;
                    const items = [];
                    for (let i = 0; i < player.inventory.length; i++) {
                        const slot = player.inventory[i];
                        items.push({ itemId: slot.itemId, quantity: slot.quantity });
                    }
                    this.onInventoryChange?.(items);
                } : null;
                const fireEquipmentChange = isLocal ? () => {
                    this.onEquipmentChange?.({
                        weapon: player.equipWeapon ?? '',
                        helm: player.equipHelm ?? '',
                        chest: player.equipChest ?? '',
                        legs: player.equipLegs ?? '',
                        boots: player.equipBoots ?? '',
                        ring: player.equipRing ?? '',
                    });
                } : null;
                callbacks.onChange(player, () => {
                    this.onPlayerChange?.(player, sessionId);
                    // Equipment fields are regular schema props — onChange fires for them
                    fireEquipmentChange?.();
                    // Also refresh inventory on any player change to catch splice-replace swaps
                    fireInventoryChange?.();
                });
                if (isLocal) {
                    // Cache the live schema reference so refreshInventory() can read it later
                    this.localPlayerRef = player;
                    // Fire once immediately with current state
                    fireInventoryChange();
                    fireEquipmentChange();
                    // Listen for add/remove/change on the inventory ArraySchema.
                    // onAdd fires for existing slots too (initial state hydration), so
                    // registering onChange inside onAdd covers both existing and future slots.
                    // This is needed so that stackable-item quantity changes on existing slots
                    // (which only fire slot.onChange, not array.onAdd) trigger a re-render.
                    if (player.inventory) {
                        callbacks.onAdd(player.inventory, (slot) => {
                            if (slot) {
                                callbacks.onChange(slot, () => {
                                    fireInventoryChange();
                                });
                            }
                            fireInventoryChange();
                        });
                        callbacks.onRemove(player.inventory, () => {
                            fireInventoryChange();
                        });
                    }
                }
            });
            callbacks.onRemove('players', (_player, key) => {
                this.onPlayerRemove?.(key);
            });
            // Projectile state sync
            callbacks.onAdd('projectiles', (proj, key) => {
                const projId = key;
                this.onProjectileAdd?.(proj, projId);
                callbacks.onChange(proj, () => {
                    this.onProjectileChange?.(proj, projId);
                });
            });
            callbacks.onRemove('projectiles', (_proj, key) => {
                this.onProjectileRemove?.(key);
            });
            // Spell projectile state sync (Fireball, etc.)
            callbacks.onAdd('spellProjectiles', (proj, key) => {
                const projId = key;
                this.onSpellProjectileAdd?.(proj, projId);
                callbacks.onChange(proj, () => {
                    this.onSpellProjectileChange?.(proj, projId);
                });
            });
            callbacks.onRemove('spellProjectiles', (_proj, key) => {
                this.onSpellProjectileRemove?.(key);
            });
            // NPC state sync
            callbacks.onAdd('npcs', (npc, key) => {
                const npcId = key;
                this.onNpcAdd?.(npc, npcId);
                callbacks.onChange(npc, () => {
                    this.onNpcChange?.(npc, npcId);
                });
            });
            callbacks.onRemove('npcs', (_npc, key) => {
                this.onNpcRemove?.(key);
            });
            // Loot bag state sync
            callbacks.onAdd('lootBags', (bag, key) => {
                const bagId = key;
                this.onLootBagAdd?.(bag, bagId);
                callbacks.onChange(bag, () => {
                    this.onLootBagChange?.(bag, bagId);
                });
                // Listen for item changes within the bag.
                // onChange on individual slots catches partial-quantity updates that
                // don't trigger onAdd/onRemove on the ArraySchema itself.
                if (bag.items) {
                    callbacks.onAdd(bag.items, (slot) => {
                        if (slot) {
                            callbacks.onChange(slot, () => {
                                this.onLootBagChange?.(bag, bagId);
                            });
                        }
                        this.onLootBagChange?.(bag, bagId);
                    });
                    callbacks.onRemove(bag.items, () => {
                        this.onLootBagChange?.(bag, bagId);
                    });
                }
            });
            callbacks.onRemove('lootBags', (_bag, key) => {
                this.onLootBagRemove?.(key);
            });
        }
        catch (err) {
            console.error('[Network] Connection failed:', err);
            throw err;
        }
    }
    sendInput(input) {
        this.room?.send(MessageType.INPUT, input);
    }
    sendEquipItem(slotIndex) {
        this.room?.send(MessageType.EQUIP_ITEM, { slotIndex });
    }
    sendUnequipItem(slotType, targetIndex) {
        this.room?.send(MessageType.UNEQUIP_ITEM, { slotType, targetIndex });
    }
    sendDropItem(source, slotIndex, slotType) {
        this.room?.send(MessageType.DROP_ITEM, { source, slotIndex, slotType });
    }
    sendSwapInventory(fromIndex, toIndex) {
        this.room?.send(MessageType.SWAP_INVENTORY, { fromIndex, toIndex });
    }
    sendCastSkill(skillId, targetId, groundX, groundY) {
        this.room?.send(MessageType.CAST_SKILL, { skillId, targetId, groundX, groundY });
    }
    sendCancelCast() {
        this.room?.send(MessageType.CANCEL_CAST, {});
    }
    sendSetActionBar(slots) {
        this.room?.send(MessageType.SET_ACTION_BAR, { slots });
    }
    /**
     * Force-rebuild inventoryItems from the live Colyseus schema and fire
     * onInventoryChange. Call this when you need a guaranteed UI refresh and
     * can't rely on nested-schema onChange callbacks (e.g. after looting).
     */
    refreshInventory() {
        const player = this.localPlayerRef;
        if (!player?.inventory)
            return;
        const items = [];
        for (let i = 0; i < player.inventory.length; i++) {
            const slot = player.inventory[i];
            items.push({ itemId: slot.itemId, quantity: slot.quantity });
        }
        this.onInventoryChange?.(items);
    }
    sendLootItem(bagId, slotIndex, quantity) {
        this.room?.send(MessageType.LOOT_ITEM, { bagId, slotIndex, quantity });
    }
    sendLootAll(bagId) {
        this.room?.send(MessageType.LOOT_ALL, { bagId });
    }
    sendChatMessage(channel, message, targetName) {
        this.room?.send(MessageType.CHAT_MESSAGE, { channel, message, targetName });
    }
    sendStartAutoAttack(skillId, targetId) {
        this.room?.send(MessageType.START_AUTO_ATTACK, { skillId, targetId });
    }
    sendStopAutoAttack() {
        this.room?.send(MessageType.STOP_AUTO_ATTACK, {});
    }
    sendPartyInvite(targetName) { this.room?.send(MessageType.PARTY_INVITE, { targetName }); }
    sendPartyAccept() { this.room?.send(MessageType.PARTY_ACCEPT, {}); }
    sendPartyDecline() { this.room?.send(MessageType.PARTY_DECLINE, {}); }
    sendPartyLeave() { this.room?.send(MessageType.PARTY_LEAVE, {}); }
    async disconnect() {
        if (this.room) {
            try {
                await this.room.leave();
            }
            catch {
                // Ignore errors — server may have already closed the connection
            }
            this.room = null;
        }
    }
}
//# sourceMappingURL=NetworkClient.js.map