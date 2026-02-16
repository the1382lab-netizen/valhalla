import { Client, Room, Callbacks } from '@colyseus/sdk';
import { SERVER_URL, ROOM_NAME, InputPayload, MessageType } from '@valhalla/shared';

export interface CollisionGridData {
  grid: number[];
  width: number;
  height: number;
  tileSize: number;
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

  constructor() {
    this.client = new Client(SERVER_URL);
  }

  get sessionId(): string | undefined {
    return this.room?.sessionId;
  }

  async connect(): Promise<void> {
    try {
      this.room = await this.client.joinOrCreate(ROOM_NAME);
      console.log(`[Network] Connected as ${this.room.sessionId}`);

      // Listen for collision grid
      this.room.onMessage('collisionGrid', (data: CollisionGridData) => {
        console.log('[Network] Received collision grid');
        this.onCollisionGrid?.(data);
      });

      // Use Colyseus 0.17 Callbacks API for state change listeners
      const callbacks = Callbacks.get(this.room);

      callbacks.onAdd('players', (player: any, key: any) => {
        const sessionId = key as string;
        this.onPlayerAdd?.(player, sessionId);

        callbacks.onChange(player, () => {
          this.onPlayerChange?.(player, sessionId);
        });
      });

      callbacks.onRemove('players', (_player: any, key: any) => {
        this.onPlayerRemove?.(key as string);
      });
    } catch (err) {
      console.error('[Network] Connection failed:', err);
      throw err;
    }
  }

  sendInput(input: InputPayload): void {
    this.room?.send(MessageType.INPUT, input);
  }

  disconnect(): void {
    this.room?.leave();
  }
}
