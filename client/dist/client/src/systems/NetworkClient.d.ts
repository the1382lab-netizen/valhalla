import { InputPayload, MapDataPayload, ChatMessagePayload, SpellImpactPayload } from '@valhalla/shared';
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
    onSpellProjectileAdd: ((proj: any, id: string) => void) | null;
    onSpellProjectileRemove: ((id: string) => void) | null;
    onSpellProjectileChange: ((proj: any, id: string) => void) | null;
    onSpellImpact: ((data: SpellImpactPayload) => void) | null;
    onNpcAdd: ((npc: any, id: string) => void) | null;
    onNpcRemove: ((id: string) => void) | null;
    onNpcChange: ((npc: any, id: string) => void) | null;
    onPlayerHit: ((data: PlayerHitData) => void) | null;
    onPlayerDied: ((data: PlayerDiedData) => void) | null;
    onPlayerRespawned: ((data: PlayerRespawnedData) => void) | null;
    onMeleeAttack: ((data: MeleeAttackData) => void) | null;
    onMissed: ((data: CombatFeedbackData) => void) | null;
    onDodged: ((data: CombatFeedbackData) => void) | null;
    onBlocked: ((data: CombatFeedbackData) => void) | null;
    onNpcHit: ((data: {
        targetId: string;
        attackerId: string;
        damage: number;
        remainingHp: number;
        isCrit?: boolean;
        blocked?: boolean;
    }) => void) | null;
    onNpcDied: ((data: {
        targetId: string;
        killerId: string;
        xpReward: number;
    }) => void) | null;
    onInventoryChange: ((inventory: any[]) => void) | null;
    onEquipmentChange: ((equipment: Record<string, string>) => void) | null;
    onSkillStarted: ((data: {
        casterId: string;
        skillId: string;
        castTimeMs: number;
    }) => void) | null;
    onSkillEffect: ((data: any) => void) | null;
    onSkillFailed: ((data: {
        reason: string;
    }) => void) | null;
    onSkillInterrupted: ((data: {
        casterId: string;
        skillId: string;
    }) => void) | null;
    onBuffApplied: ((data: {
        targetId: string;
        skillId: string;
        durationMs: number;
    }) => void) | null;
    onBuffRemoved: ((data: {
        targetId: string;
        skillId: string;
    }) => void) | null;
    onActionBarData: ((data: {
        slots: string[];
    }) => void) | null;
    onChatMessage: ((data: ChatMessagePayload) => void) | null;
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
    sendCastSkill(skillId: string, targetId?: string, groundX?: number, groundY?: number): void;
    sendCancelCast(): void;
    sendSetActionBar(slots: string[]): void;
    sendChatMessage(channel: 'general' | 'world' | 'whisper', message: string, targetName?: string): void;
    disconnect(): void;
}
//# sourceMappingURL=NetworkClient.d.ts.map