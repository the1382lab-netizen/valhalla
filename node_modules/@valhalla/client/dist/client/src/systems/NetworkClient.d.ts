import { InputPayload } from '@valhalla/shared';
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
export interface VisibilityData {
    polygon: {
        x: number;
        y: number;
    }[];
    visiblePlayers: string[];
    visibleProjectiles: string[];
}
/**
 * Manages the Colyseus connection to the server.
 */
export declare class NetworkClient {
    private client;
    private room;
    onStateChange: ((state: any) => void) | null;
    onPlayerAdd: ((player: any, sessionId: string) => void) | null;
    onPlayerRemove: ((sessionId: string) => void) | null;
    onPlayerChange: ((player: any, sessionId: string) => void) | null;
    onCollisionGrid: ((data: CollisionGridData) => void) | null;
    onProjectileAdd: ((proj: any, id: string) => void) | null;
    onProjectileRemove: ((id: string) => void) | null;
    onProjectileChange: ((proj: any, id: string) => void) | null;
    onPlayerHit: ((data: PlayerHitData) => void) | null;
    onPlayerDied: ((data: PlayerDiedData) => void) | null;
    onPlayerRespawned: ((data: PlayerRespawnedData) => void) | null;
    onMeleeAttack: ((data: MeleeAttackData) => void) | null;
    onVisibility: ((data: VisibilityData) => void) | null;
    constructor();
    get sessionId(): string | undefined;
    connect(): Promise<void>;
    sendInput(input: InputPayload): void;
    disconnect(): void;
}
//# sourceMappingURL=NetworkClient.d.ts.map