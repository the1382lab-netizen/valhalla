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
    // Projectile callbacks
    onProjectileAdd = null;
    onProjectileRemove = null;
    onProjectileChange = null;
    // Combat event callbacks
    onPlayerHit = null;
    onPlayerDied = null;
    onPlayerRespawned = null;
    onMeleeAttack = null;
    // Visibility callbacks
    onVisibility = null;
    constructor() {
        this.client = new Client(SERVER_URL);
    }
    get sessionId() {
        return this.room?.sessionId;
    }
    async connect() {
        try {
            this.room = await this.client.joinOrCreate(ROOM_NAME);
            console.log(`[Network] Connected as ${this.room.sessionId}`);
            // Listen for collision grid
            this.room.onMessage('collisionGrid', (data) => {
                console.log('[Network] Received collision grid');
                this.onCollisionGrid?.(data);
            });
            // Listen for visibility updates
            this.room.onMessage('visibility', (data) => {
                this.onVisibility?.(data);
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
            // Use Colyseus 0.17 Callbacks API for state change listeners
            const callbacks = Callbacks.get(this.room);
            callbacks.onAdd('players', (player, key) => {
                const sessionId = key;
                this.onPlayerAdd?.(player, sessionId);
                callbacks.onChange(player, () => {
                    this.onPlayerChange?.(player, sessionId);
                });
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
        }
        catch (err) {
            console.error('[Network] Connection failed:', err);
            throw err;
        }
    }
    sendInput(input) {
        this.room?.send(MessageType.INPUT, input);
    }
    disconnect() {
        this.room?.leave();
    }
}
//# sourceMappingURL=NetworkClient.js.map