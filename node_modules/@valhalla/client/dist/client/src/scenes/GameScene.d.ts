import Phaser from 'phaser';
/**
 * Main gameplay scene.
 * Handles tile map rendering, local player with client-side prediction,
 * remote player interpolation, projectile rendering, HP, mana, death/respawn.
 */
export declare class GameScene extends Phaser.Scene {
    private network;
    private inputManager;
    private entityRenderer;
    private playerSprite;
    private aimLine;
    private localX;
    private localY;
    private localHp;
    private localMaxHp;
    private localMana;
    private localMaxMana;
    private localAlive;
    private localSpeed;
    private localClassId;
    private localLevel;
    private hpBarGfx;
    private classHudText;
    private deathOverlay;
    private deathText;
    private respawnTimer;
    private pendingInputs;
    private lastServerSeq;
    private collisionGrid;
    private collisionMapW;
    private collisionMapH;
    private mapTileSize;
    private mapWidthPx;
    private mapHeightPx;
    private tileSprites;
    private currentZoneId;
    private connected;
    private statusText;
    private selectedClassId;
    private inventoryOpen;
    private inventoryItems;
    private localEquipment;
    private localXp;
    private invContainer;
    private panelGfx;
    private invSlotTexts;
    private invQtyTexts;
    private invTooltipText;
    private invTitleText;
    private charInfoText;
    private charEquipTexts;
    private charStatTexts;
    private charBarsGfx;
    private dragging;
    private dragSource;
    private dragGhost;
    private dragGhostBg;
    private panelStartX;
    private panelY;
    private invX;
    private charX;
    private highlightGfx;
    private equipSlotYPositions;
    constructor();
    private characterId;
    private authToken;
    init(data?: {
        classId?: string;
        characterId?: number;
        token?: string;
    }): void;
    create(): void;
    private setupNetworkCallbacks;
    /**
     * Get the world position for a given player (local or remote).
     */
    private getCombatTextPosition;
    private showRemoteDamageFlash;
    private showRemoteMeleeSlash;
    private createLocalPlayer;
    /**
     * Build the visual tilemap from the collision grid data.
     */
    /** GID → texture key mapping for the default tileset. */
    private static readonly GID_TEXTURE_MAP;
    /**
     * Build multi-layer tile map from server MapDataPayload.
     * Renders each tile layer in order, with proper depth sorting.
     */
    private buildTileMapFromData;
    /**
     * Legacy tile map builder (old collision-grid-only format).
     */
    private buildTileMapLegacy;
    private showDeathScreen;
    private hideDeathScreen;
    private drawLocalHud;
    private readonly INV_COLS;
    private readonly INV_ROWS;
    private readonly SLOT_SIZE;
    private readonly SLOT_GAP;
    private readonly CHAR_PANEL_W;
    private readonly PANEL_GAP;
    private get invPanelW();
    private readonly PANEL_H;
    private get invPanelH();
    private get totalPanelW();
    private createInventoryPanel;
    private createCharacterPanelContent;
    private setupDragAndDrop;
    /**
     * Update tooltip text based on what slot the pointer is hovering.
     */
    private updateHoverTooltip;
    private startDrag;
    private endDrag;
    /**
     * Get the inventory slot index at a given screen position, or -1 if none.
     */
    private getInventorySlotAt;
    /**
     * Get the equipment slot type at a given screen position, or null if none.
     */
    private getEquipSlotAt;
    /**
     * Check if a point is inside either panel.
     */
    private isInsidePanels;
    /**
     * Draw a highlight rectangle over the slot being hovered during drag.
     */
    private updateDragHighlight;
    /**
     * Handle the drop action based on where the pointer was released.
     */
    private handleDrop;
    private toggleInventory;
    private renderCharacterPanel;
    private renderInventorySlots;
    update(_time: number, delta: number): void;
    /**
     * Apply input locally for client-side prediction.
     * Uses the synced speed value from the server (class-specific).
     */
    private applyInputLocally;
    /**
     * Client-side collision resolution (mirrors server logic).
     */
    private resolveCollisionLocal;
    /**
     * Server reconciliation: when we get authoritative state, reapply
     * any inputs the server hasn't processed yet.
     */
    private reconcile;
    /**
     * Draw a short aim line from the player in the aim direction.
     */
    private drawAimLine;
}
//# sourceMappingURL=GameScene.d.ts.map