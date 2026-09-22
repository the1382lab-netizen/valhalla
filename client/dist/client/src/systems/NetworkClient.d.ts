import { InputPayload, MapDataPayload, ChatMessagePayload, SpellImpactPayload, PartyMemberInfo } from '@valhalla/shared';
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
    onLootBagAdd: ((bag: any, bagId: string) => void) | null;
    onLootBagRemove: ((bagId: string) => void) | null;
    onLootBagChange: ((bag: any, bagId: string) => void) | null;
    onLootSuccess: ((bagId: string) => void) | null;
    private localPlayerRef;
    onChatMessage: ((data: ChatMessagePayload) => void) | null;
    onLevelUp: ((newLevel: number) => void) | null;
    onXpGained: ((amount: number) => void) | null;
    onPartyUpdate: ((members: PartyMemberInfo[]) => void) | null;
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
    /**
     * Force-rebuild inventoryItems from the live Colyseus schema and fire
     * onInventoryChange. Call this when you need a guaranteed UI refresh and
     * can't rely on nested-schema onChange callbacks (e.g. after looting).
     */
    refreshInventory(): void;
    sendLootItem(bagId: string, slotIndex: number, quantity: number): void;
    sendLootAll(bagId: string): void;
    sendChatMessage(channel: 'general' | 'world' | 'whisper', message: string, targetName?: string): void;
    sendStartAutoAttack(skillId: string, targetId: string): void;
    sendStopAutoAttack(): void;
    sendPartyInvite(targetName: string): void;
    sendPartyAccept(): void;
    sendPartyDecline(): void;
    sendPartyLeave(): void;
    disconnect(): Promise<void>;
}
//# sourceMappingURL=NetworkClient.d.ts.map