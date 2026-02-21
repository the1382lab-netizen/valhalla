import { Client, Room, Callbacks } from '@colyseus/sdk';
import { SERVER_URL, ROOM_NAME, InputPayload, MessageType, MapDataPayload, ChatMessagePayload, SpellImpactPayload, PartyMemberInfo, PartyUpdatePayload } from '@valhalla/shared';

/** @deprecated Use MapDataPayload instead */
export interface CollisionGridData {
  grid: number[];
  width: number;
  height: number;
  tileSize: number;
}

export interface PlayerHitData {
  targetId: string;
  attackerId: string;
  damage: number;
  remainingHp: number;
  isCrit?: boolean;
  blocked?: boolean;
}

export interface PlayerDiedData {
  targetId: string;
  killerId: string;
}

export interface PlayerRespawnedData {
  playerId: string;
}

export interface MeleeAttackData {
  attackerId: string;
  angle: number;
}

export interface CombatFeedbackData {
  targetId: string;
  attackerId: string;
}

/**
 * Manages the Colyseus connection to the server.
 */
export class NetworkClient {
  private client: Client;
  private room: Room | null = null;

  // Callbacks
  onStateChange: ((state: any) => void) | null = null;
  onPlayerAdd: ((player: any, sessionId: string) => void) | null = null;
  onPlayerRemove: ((sessionId: string) => void) | null = null;
  onPlayerChange: ((player: any, sessionId: string) => void) | null = null;
  onCollisionGrid: ((data: CollisionGridData) => void) | null = null;
  onMapData: ((data: MapDataPayload) => void) | null = null;
  onZoneChange: ((data: { zoneId: string; spawnX: number; spawnY: number }) => void) | null = null;

  // Projectile callbacks
  onProjectileAdd: ((proj: any, id: string) => void) | null = null;
  onProjectileRemove: ((id: string) => void) | null = null;
  onProjectileChange: ((proj: any, id: string) => void) | null = null;

  // Spell projectile callbacks (e.g. Fireball)
  onSpellProjectileAdd: ((proj: any, id: string) => void) | null = null;
  onSpellProjectileRemove: ((id: string) => void) | null = null;
  onSpellProjectileChange: ((proj: any, id: string) => void) | null = null;

  // Spell impact VFX callback
  onSpellImpact: ((data: SpellImpactPayload) => void) | null = null;

  // NPC callbacks
  onNpcAdd: ((npc: any, id: string) => void) | null = null;
  onNpcRemove: ((id: string) => void) | null = null;
  onNpcChange: ((npc: any, id: string) => void) | null = null;

  // Combat event callbacks
  onPlayerHit: ((data: PlayerHitData) => void) | null = null;
  onPlayerDied: ((data: PlayerDiedData) => void) | null = null;
  onPlayerRespawned: ((data: PlayerRespawnedData) => void) | null = null;
  onMeleeAttack: ((data: MeleeAttackData) => void) | null = null;
  onMissed: ((data: CombatFeedbackData) => void) | null = null;
  onDodged: ((data: CombatFeedbackData) => void) | null = null;
  onBlocked: ((data: CombatFeedbackData) => void) | null = null;

  // NPC combat callbacks
  onNpcHit: ((data: { targetId: string; attackerId: string; damage: number; remainingHp: number; isCrit?: boolean; blocked?: boolean }) => void) | null = null;
  onNpcDied: ((data: { targetId: string; killerId: string; xpReward: number }) => void) | null = null;

  // Inventory & equipment callbacks
  onInventoryChange: ((inventory: any[]) => void) | null = null;
  onEquipmentChange: ((equipment: Record<string, string>) => void) | null = null;

  // Skill system callbacks
  onSkillStarted: ((data: { casterId: string; skillId: string; castTimeMs: number }) => void) | null = null;
  onSkillEffect: ((data: any) => void) | null = null;
  onSkillFailed: ((data: { reason: string }) => void) | null = null;
  onSkillInterrupted: ((data: { casterId: string; skillId: string }) => void) | null = null;
  onBuffApplied: ((data: { targetId: string; skillId: string; durationMs: number }) => void) | null = null;
  onBuffRemoved: ((data: { targetId: string; skillId: string }) => void) | null = null;
  onActionBarData: ((data: { slots: string[] }) => void) | null = null;

  // Loot bag callbacks
  onLootBagAdd: ((bag: any, bagId: string) => void) | null = null;
  onLootBagRemove: ((bagId: string) => void) | null = null;
  onLootBagChange: ((bag: any, bagId: string) => void) | null = null;
  // Direct server confirmation that a loot action succeeded — bypasses schema callbacks
  onLootSuccess: ((bagId: string) => void) | null = null;

  // Live reference to the local player's schema (set once on join, used for refreshInventory)
  private localPlayerRef: any = null;

  // Chat callback
  onChatMessage: ((data: ChatMessagePayload) => void) | null = null;

  // Level-up callback
  onLevelUp: ((newLevel: number) => void) | null = null;

  // Party callbacks
  onPartyUpdate: ((members: PartyMemberInfo[]) => void) | null = null;

  constructor() {
    this.client = new Client(SERVER_URL);
  }

  get sessionId(): string | undefined {
    return this.room?.sessionId;
  }

  async connect(options?: { characterId?: number; token?: string }): Promise<void> {
    try {
      this.room = await this.client.joinOrCreate(ROOM_NAME, options);
      console.log(`[Network] Connected as ${this.room.sessionId}`);

      // Listen for map data (new multi-layer system)
      this.room.onMessage(MessageType.MAP_DATA, (data: MapDataPayload) => {
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
      this.room.onMessage(MessageType.ZONE_CHANGE, (data: { zoneId: string; spawnX: number; spawnY: number }) => {
        console.log(`[Network] Zone change → ${data.zoneId}`);
        this.onZoneChange?.(data);
      });

      // Legacy collision grid handler (in case server still sends old format)
      this.room.onMessage('collisionGrid', (data: CollisionGridData) => {
        console.log('[Network] Received legacy collision grid');
        this.onCollisionGrid?.(data);
      });

      // Combat event messages
      this.room.onMessage(MessageType.PLAYER_HIT, (data: PlayerHitData) => {
        this.onPlayerHit?.(data);
      });

      this.room.onMessage(MessageType.PLAYER_DIED, (data: PlayerDiedData) => {
        this.onPlayerDied?.(data);
      });

      this.room.onMessage(MessageType.PLAYER_RESPAWNED, (data: PlayerRespawnedData) => {
        this.onPlayerRespawned?.(data);
      });

      this.room.onMessage(MessageType.MELEE_ATTACK, (data: MeleeAttackData) => {
        this.onMeleeAttack?.(data);
      });

      this.room.onMessage(MessageType.MISSED, (data: CombatFeedbackData) => {
        this.onMissed?.(data);
      });

      this.room.onMessage(MessageType.DODGED, (data: CombatFeedbackData) => {
        this.onDodged?.(data);
      });

      this.room.onMessage(MessageType.BLOCKED, (data: CombatFeedbackData) => {
        this.onBlocked?.(data);
      });

      // NPC combat events
      this.room.onMessage(MessageType.NPC_HIT, (data: any) => {
        this.onNpcHit?.(data);
      });

      this.room.onMessage(MessageType.NPC_DIED, (data: any) => {
        this.onNpcDied?.(data);
      });

      // ── Skill system messages ──
      this.room.onMessage(MessageType.SKILL_STARTED, (data: any) => {
        this.onSkillStarted?.(data);
      });

      this.room.onMessage(MessageType.SKILL_EFFECT, (data: any) => {
        this.onSkillEffect?.(data);
      });

      this.room.onMessage(MessageType.SKILL_FAILED, (data: any) => {
        this.onSkillFailed?.(data);
      });

      this.room.onMessage(MessageType.SKILL_INTERRUPTED, (data: any) => {
        this.onSkillInterrupted?.(data);
      });

      this.room.onMessage(MessageType.BUFF_APPLIED, (data: any) => {
        this.onBuffApplied?.(data);
      });

      this.room.onMessage(MessageType.BUFF_REMOVED, (data: any) => {
        this.onBuffRemoved?.(data);
      });

      this.room.onMessage(MessageType.ACTION_BAR_DATA, (data: any) => {
        this.onActionBarData?.(data);
      });

      // Spell impact VFX
      this.room.onMessage(MessageType.SPELL_IMPACT, (data: SpellImpactPayload) => {
        this.onSpellImpact?.(data);
      });

      // Chat messages
      this.room.onMessage(MessageType.CHAT_MESSAGE, (data: ChatMessagePayload) => {
        this.onChatMessage?.(data);
      });

      // Loot success — direct server confirmation to force-refresh UI panels.
      // This fires AFTER the state patch has already been applied, so reading
      // the live schema in refreshInventory() gives the correct updated state.
      this.room.onMessage(MessageType.LOOT_SUCCESS, (data: { bagId: string }) => {
        this.onLootSuccess?.(data.bagId);
      });

      this.room.onMessage(MessageType.LEVEL_UP, (data: { level: number }) => {
        this.onLevelUp?.(data.level);
      });

      // Party update
      this.room.onMessage(MessageType.PARTY_UPDATE, (data: PartyUpdatePayload) => {
        this.onPartyUpdate?.(data.members);
      });

      // Use Colyseus 0.17 Callbacks API for state change listeners
      const callbacks = Callbacks.get(this.room);

      callbacks.onAdd('players', (player: any, key: any) => {
        const sessionId = key as string;
        const isLocal = sessionId === this.room?.sessionId;

        this.onPlayerAdd?.(player, sessionId);

        // Helpers for local player sync
        const fireInventoryChange = isLocal ? () => {
          if (!player.inventory) return;
          const items: any[] = [];
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
          fireInventoryChange!();
          fireEquipmentChange!();

          // Listen for add/remove/change on the inventory ArraySchema.
          // onAdd fires for existing slots too (initial state hydration), so
          // registering onChange inside onAdd covers both existing and future slots.
          // This is needed so that stackable-item quantity changes on existing slots
          // (which only fire slot.onChange, not array.onAdd) trigger a re-render.
          if (player.inventory) {
            callbacks.onAdd(player.inventory, (slot: any) => {
              if (slot) {
                callbacks.onChange(slot, () => {
                  fireInventoryChange!();
                });
              }
              fireInventoryChange!();
            });
            callbacks.onRemove(player.inventory, () => {
              fireInventoryChange!();
            });
          }
        }
      });

      callbacks.onRemove('players', (_player: any, key: any) => {
        this.onPlayerRemove?.(key as string);
      });

      // Projectile state sync
      callbacks.onAdd('projectiles', (proj: any, key: any) => {
        const projId = key as string;
        this.onProjectileAdd?.(proj, projId);

        callbacks.onChange(proj, () => {
          this.onProjectileChange?.(proj, projId);
        });
      });

      callbacks.onRemove('projectiles', (_proj: any, key: any) => {
        this.onProjectileRemove?.(key as string);
      });

      // Spell projectile state sync (Fireball, etc.)
      callbacks.onAdd('spellProjectiles', (proj: any, key: any) => {
        const projId = key as string;
        this.onSpellProjectileAdd?.(proj, projId);

        callbacks.onChange(proj, () => {
          this.onSpellProjectileChange?.(proj, projId);
        });
      });

      callbacks.onRemove('spellProjectiles', (_proj: any, key: any) => {
        this.onSpellProjectileRemove?.(key as string);
      });

      // NPC state sync
      callbacks.onAdd('npcs', (npc: any, key: any) => {
        const npcId = key as string;
        this.onNpcAdd?.(npc, npcId);

        callbacks.onChange(npc, () => {
          this.onNpcChange?.(npc, npcId);
        });
      });

      callbacks.onRemove('npcs', (_npc: any, key: any) => {
        this.onNpcRemove?.(key as string);
      });

      // Loot bag state sync
      callbacks.onAdd('lootBags', (bag: any, key: any) => {
        const bagId = key as string;
        this.onLootBagAdd?.(bag, bagId);

        callbacks.onChange(bag, () => {
          this.onLootBagChange?.(bag, bagId);
        });

        // Listen for item changes within the bag.
        // onChange on individual slots catches partial-quantity updates that
        // don't trigger onAdd/onRemove on the ArraySchema itself.
        if (bag.items) {
          callbacks.onAdd(bag.items, (slot: any) => {
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

      callbacks.onRemove('lootBags', (_bag: any, key: any) => {
        this.onLootBagRemove?.(key as string);
      });
    } catch (err) {
      console.error('[Network] Connection failed:', err);
      throw err;
    }
  }

  sendInput(input: InputPayload): void {
    this.room?.send(MessageType.INPUT, input);
  }

  sendEquipItem(slotIndex: number): void {
    this.room?.send(MessageType.EQUIP_ITEM, { slotIndex });
  }

  sendUnequipItem(slotType: string, targetIndex?: number): void {
    this.room?.send(MessageType.UNEQUIP_ITEM, { slotType, targetIndex });
  }

  sendDropItem(source: 'inventory' | 'equipment', slotIndex?: number, slotType?: string): void {
    this.room?.send(MessageType.DROP_ITEM, { source, slotIndex, slotType });
  }

  sendSwapInventory(fromIndex: number, toIndex: number): void {
    this.room?.send(MessageType.SWAP_INVENTORY, { fromIndex, toIndex });
  }

  sendCastSkill(skillId: string, targetId?: string, groundX?: number, groundY?: number): void {
    this.room?.send(MessageType.CAST_SKILL, { skillId, targetId, groundX, groundY });
  }

  sendCancelCast(): void {
    this.room?.send(MessageType.CANCEL_CAST, {});
  }

  sendSetActionBar(slots: string[]): void {
    this.room?.send(MessageType.SET_ACTION_BAR, { slots });
  }

  /**
   * Force-rebuild inventoryItems from the live Colyseus schema and fire
   * onInventoryChange. Call this when you need a guaranteed UI refresh and
   * can't rely on nested-schema onChange callbacks (e.g. after looting).
   */
  refreshInventory(): void {
    const player = this.localPlayerRef;
    if (!player?.inventory) return;
    const items: any[] = [];
    for (let i = 0; i < player.inventory.length; i++) {
      const slot = player.inventory[i];
      items.push({ itemId: slot.itemId, quantity: slot.quantity });
    }
    this.onInventoryChange?.(items);
  }

  sendLootItem(bagId: string, slotIndex: number, quantity: number): void {
    this.room?.send(MessageType.LOOT_ITEM, { bagId, slotIndex, quantity });
  }

  sendLootAll(bagId: string): void {
    this.room?.send(MessageType.LOOT_ALL, { bagId });
  }

  sendChatMessage(channel: 'general' | 'world' | 'whisper', message: string, targetName?: string): void {
    this.room?.send(MessageType.CHAT_MESSAGE, { channel, message, targetName });
  }

  sendPartyInvite(targetName: string): void { this.room?.send(MessageType.PARTY_INVITE, { targetName }); }
  sendPartyAccept(): void { this.room?.send(MessageType.PARTY_ACCEPT, {}); }
  sendPartyDecline(): void { this.room?.send(MessageType.PARTY_DECLINE, {}); }
  sendPartyLeave(): void { this.room?.send(MessageType.PARTY_LEAVE, {}); }

  disconnect(): void {
    this.room?.leave();
  }
}
