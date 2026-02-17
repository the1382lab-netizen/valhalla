import { Client, Room, Callbacks } from '@colyseus/sdk';
import { SERVER_URL, ROOM_NAME, InputPayload, MessageType } from '@valhalla/shared';

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

  // Projectile callbacks
  onProjectileAdd: ((proj: any, id: string) => void) | null = null;
  onProjectileRemove: ((id: string) => void) | null = null;
  onProjectileChange: ((proj: any, id: string) => void) | null = null;

  // Combat event callbacks
  onPlayerHit: ((data: PlayerHitData) => void) | null = null;
  onPlayerDied: ((data: PlayerDiedData) => void) | null = null;
  onPlayerRespawned: ((data: PlayerRespawnedData) => void) | null = null;
  onMeleeAttack: ((data: MeleeAttackData) => void) | null = null;
  onMissed: ((data: CombatFeedbackData) => void) | null = null;
  onDodged: ((data: CombatFeedbackData) => void) | null = null;
  onBlocked: ((data: CombatFeedbackData) => void) | null = null;

  // Inventory & equipment callbacks
  onInventoryChange: ((inventory: any[]) => void) | null = null;
  onEquipmentChange: ((equipment: Record<string, string>) => void) | null = null;

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

      // Listen for collision grid
      this.room.onMessage('collisionGrid', (data: CollisionGridData) => {
        console.log('[Network] Received collision grid');
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
        });

        if (isLocal) {
          // Fire once immediately with current state
          fireInventoryChange!();
          fireEquipmentChange!();

          // Listen for add/remove on the inventory ArraySchema
          if (player.inventory) {
            callbacks.onAdd(player.inventory, () => {
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

  sendUnequipItem(slotType: string): void {
    this.room?.send(MessageType.UNEQUIP_ITEM, { slotType });
  }

  disconnect(): void {
    this.room?.leave();
  }
}
