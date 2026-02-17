import { InputPayload, MapDataPayload } from '@valhalla/shared';
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
export declare class NetworkClient {
    private client;
    private room;
    onStateChange: ((state: any) => void) | null;
    onPlayerAdd: ((player: any, sessionId: string) => void) | null;
    onPlayerRemove: ((sessionId: string) => void) | null;
    onPlayerChange: ((player: any, sessionId: string) => void) | null;
    onCollisionGrid: ((data: CollisionGridData) => void) | null;
    onMapData: ((data: MapDataPayload) => void) | null;
    onZoneChange: ((data: {
        zoneId: string;
        spawnX: number;
        spawnY: number;
    }) => void) | null;
    onProjectileAdd: ((proj: any, id: string) => void) | null;
    onProjectileRemove: ((id: string) => void) | null;
    onProjectileChange: ((proj: any, id: string) => void) | null;
    onPlayerHit: ((data: PlayerHitData) => void) | null;
    onPlayerDied: ((data: PlayerDiedData) => void) | null;
    onPlayerRespawned: ((data: PlayerRespawnedData) => void) | null;
    onMeleeAttack: ((data: MeleeAttackData) => void) | null;
    onMissed: ((data: CombatFeedbackData) => void) | null;
    onDodged: ((data: CombatFeedbackData) => void) | null;
    onBlocked: ((data: CombatFeedbackData) => void) | null;
    onInventoryChange: ((inventory: any[]) => void) | null;
    onEquipmentChange: ((equipment: Record<string, string>) => void) | null;
    constructor();
    get sessionId(): string | undefined;
    connect(options?: {
        characterId?: number;
        token?: string;
    }): Promise<void>;
    sendInput(input: InputPayload): void;
    sendEquipItem(slotIndex: number): void;
    sendUnequipItem(slotType: string, targetIndex?: number): void;
    sendDropItem(source: 'inventory' | 'equipment', slotIndex?: number, slotType?: string): void;
    sendSwapInventory(fromIndex: number, toIndex: number): void;
    disconnect(): void;
}
//# sourceMappingURL=NetworkClient.d.ts.map