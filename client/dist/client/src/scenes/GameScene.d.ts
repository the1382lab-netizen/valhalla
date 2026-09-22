import Phaser from 'phaser';
export declare class GameScene extends Phaser.Scene {
    private network;
    private inputManager;
    private entityRenderer;
    private playerSprite;
    private aimLine;
    private localX;
    private localY;
    /** Visual-only position that lerps toward localX/Y each frame to absorb reconcile jitter. */
    private renderX;
    private renderY;
    /** True when the current zone uses isometric rendering. */
    private isIso;
    /** Last facing direction for idle animation. */
    private lastFacingDir;
    /** True while a one-shot or looping action animation is playing on the local player. */
    private isPlayingActionAnim;
    private localHp;
    private localMaxHp;
    private localShieldHp;
    private localMana;
    private localMaxMana;
    private localAlive;
    private localSpeed;
    private localClassId;
    /** Paperdoll base body for the local character. */
    private localBodyId;
    private localLevel;
    private localCharacterName;
    private hpBarGfx;
    private classHudText;
    private deathOverlay;
    private deathText;
    private respawnTimer;
    private pendingInputs;
    /** Last inputSeq value acknowledged by the server — used to skip redundant reconciles. */
    private _lastServerSeq;
    private lastServerSeq;
    private collisionGrid;
    private collisionMapW;
    private collisionMapH;
    private mapTileSize;
    private mapWidthPx;
    private mapHeightPx;
    private tileSprites;
    private currentZoneId;
    private remotePlayerZones;
    private remotePlayerCache;
    private visibleProjectiles;
    private visibleSpellProjectiles;
    private npcZones;
    private npcCache;
    private visibleNpcs;
    private lootBagZones;
    private lootBagCache;
    private visibleLootBags;
    private lootPanelOpen;
    private currentLootBagId;
    private lootPanelContainer;
    private lootPanelGfx;
    private lootPanelSlotTexts;
    private lootPanelQtyTexts;
    private lootPanelSlotIcons;
    private lootPanelTitleText;
    /** Cached loot panel items from the latest server state. */
    private lootPanelItems;
    private readonly LOOT_COLS;
    private readonly LOOT_ROWS;
    private readonly LOOT_SLOT_SIZE;
    private readonly LOOT_SLOT_GAP;
    private readonly LOOT_PANEL_PAD;
    private chatOffset;
    private actionBarOffset;
    private invOffset;
    private skillsOffset;
    private combatLogOffset;
    private hudDragTarget;
    private hudDragStartMouse;
    private hudDragStartOffset;
    private actionBarScreenRect;
    private skillsPaneScreenRect;
    private connected;
    private statusText;
    private selectedClassId;
    private inventoryOpen;
    private inventoryItems;
    private localEquipment;
    /** Overlay sprites for local player equipped items (managed by EntityRenderer helpers). */
    private localEquipOverlays;
    private localXp;
    private invContainer;
    private invDimBg;
    private skillsDimBg;
    private panelGfx;
    private invSlotTexts;
    private invSlotIcons;
    private invQtyTexts;
    private invTooltipText;
    private invTitleText;
    private charInfoText;
    private charEquipTexts;
    private charStatTexts;
    private charBarsGfx;
    private itemDetailPanelOpen;
    private itemDetailContainer;
    private itemDetailGfx;
    private _itemDetailDynamic;
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
    private actionBar;
    private actionBarContainer;
    private actionBarGfx;
    private actionBarSlotTexts;
    private actionBarKeyTexts;
    private actionBarCooldownGfx;
    private actionBarCooldownTexts;
    private castBarContainer;
    private castBarGfx;
    private castBarText;
    private localCastingSkillId;
    private localCastingStartedAt;
    private localCastingDurationMs;
    private localAutoAttackActive;
    private localAutoAttackSkillId;
    private localEnergy;
    private localMaxEnergy;
    private skillsPaneOpen;
    private skillsPaneContainer;
    private skillsPaneGfx;
    private skillsPaneSkillRows;
    private skillDragging;
    private skillDragId;
    private skillDragGhost;
    private skillDragGhostBg;
    private abDragging;
    private abDragSourceSlot;
    private abDragGhost;
    private abDragGhostBg;
    private skillTooltipContainer;
    private skillTooltipBg;
    private skillTooltipText;
    private skillCooldowns;
    private chatContainer;
    private chatBgGfx;
    private chatInputBgGfx;
    private chatMessageTexts;
    private chatInputDisplay;
    private chatChannelLabel;
    /** Full message history (newest last). */
    private chatMessages;
    private chatInputActive;
    private chatInputText;
    /** Default send channel. Whispers are always triggered by /w prefix. */
    private chatCurrentChannel;
    private chatScrollOffset;
    private chatScrollGfx;
    /** ID of the currently targeted entity (player sessionId or NPC id), or null. */
    private currentTargetId;
    /** Type of the currently targeted entity. */
    private currentTargetType;
    /**
     * Set to true when an entity sprite was just clicked, so the global
     * pointer-down handler (which moves the player) can skip that frame.
     */
    private entityClickConsumed;
    private targetOffset;
    private targetNameplateContainer;
    private targetNameplateBg;
    private targetNameplateTitleText;
    private targetNameplateNameText;
    private targetNameplateLevelText;
    private targetNameplateHpBar;
    private targetNameplateHpText;
    private targetNameplateBuffsGfx;
    private targetNameplateBuffsText;
    private targetNameplateTitleHandle;
    private partyMembers;
    private partyMemberData;
    private remotePlayerShieldHp;
    private localBuffs;
    private buffsOffset;
    private buffsPanelContainer;
    private buffsPanelBg;
    private buffsPanelRowGfx;
    private buffsPanelAbbrTexts;
    private buffsPanelNameTexts;
    private buffsPanelTimerTexts;
    private buffsPanelTitleHandle;
    private buffsPanelTitleText;
    private partyOffset;
    private partyPanelContainer;
    private partyPanelBg;
    private partyPanelTexts;
    private partyPanelHpBars;
    private partyPanelManaBars;
    private partyPanelTitleHandle;
    private partySlotSessionIds;
    private optionsMenuOpen;
    private optionsMenuContainer;
    private optionsDimBg;
    private optionsButtons;
    private optionsCloseText;
    /** Index of the currently hovered options button (-1 = none, -2 = close btn). */
    private optionsHoveredIdx;
    private hudEditMode;
    private hudEditLabel;
    private hudEditSubLabel;
    /** Per-panel visibility (used in normal gameplay, toggled in edit mode). */
    private panelVisibility;
    /** Per-panel opacity multiplier (0.5, 0.75, 1.0). */
    private panelOpacity;
    /** Per-panel scale (0.8, 1.0, 1.2). */
    private panelScale;
    private hudEditPanelControls;
    private readonly CHAT_MAX_W;
    private readonly CHAT_H;
    private readonly CHAT_BOTTOM_MARGIN;
    private readonly CHAT_LINE_H;
    private readonly CHAT_PAD;
    private readonly CHAT_INPUT_H;
    private readonly CHAT_MAX_MESSAGES;
    private readonly CHAT_VISIBLE_LINES;
    private readonly CHAT_SCROLLBAR_W;
    /** Left edge of chat panel — 12px gap right of the HP/mana bar (barX=16 + barWidth=200 + border=4 + gap=12) */
    private readonly CHAT_LEFT_X;
    /** Tracks current effective width so word-wrap is only recalculated on change */
    private chatEffectiveW;
    private readonly CL_W;
    private readonly CL_H;
    private readonly CL_LINE_H;
    private readonly CL_PAD;
    private readonly CL_TITLE_H;
    private readonly CL_SCROLLBAR_W;
    private readonly CL_MAX_MESSAGES;
    private CL_VISIBLE_LINES;
    private combatLogContainer;
    private combatLogBgGfx;
    private combatLogScrollGfx;
    private combatLogTitleText;
    private combatLogMessageTexts;
    private combatLogMessages;
    private combatLogScrollOffset;
    private combatLogScrollDragging;
    private combatLogScrollDragStartY;
    private combatLogScrollDragStartOffset;
    private combatLogFilters;
    private combatLogContextMenuOpen;
    private combatLogContextMenuPos;
    private combatLogContextGfx;
    private combatLogContextTexts;
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
     * Determine the 4-direction movement direction string from WASD input.
     * Maps directly from world-space movement intent (W=up, S=down, A=left, D=right)
     * so that the sprite row matches the key pressed, regardless of ISO projection.
     * For diagonals the vertical axis takes priority (up/down over left/right).
     * Returns null if not moving.
     */
    private getMovementDir;
    /**
     * Play an action animation (melee, ranged, cast) on the local player sprite.
     * Melee/ranged play once then return to idle; cast loops until stopped.
     */
    private playActionAnimation;
    /** Stop any looping action animation (e.g. cast) and return to idle. */
    private stopActionAnimation;
    /**
     * Get the facing direction from an attacker toward a target screen position.
     * Returns 'up', 'down', 'left', or 'right'.
     */
    private getDirectionToTarget;
    /**
     * Build the visual tilemap from the collision grid data.
     */
    /** GID → texture key mapping for all tilesets. */
    private static readonly GID_TEXTURE_MAP;
    /**
     * Build multi-layer tile map from server MapDataPayload.
     * Renders each tile layer in order, with proper depth sorting.
     */
    /** Depth stride between tile layers for painter's-algorithm sorting. */
    private static readonly LAYER_DEPTH_STRIDE;
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
    private loadHudLayout;
    private saveHudLayout;
    private readonly OPTS_PANEL_W;
    private readonly OPTS_PANEL_H;
    private readonly OPTS_BTN_W;
    private readonly OPTS_BTN_H;
    private readonly OPTS_BTN_GAP;
    private readonly OPTS_TITLE_H;
    private readonly OPTS_BUTTONS;
    /** Return the screen rect for the options panel. */
    private getOptionsPanelRect;
    /** Return the screen rect for a specific options button by index. */
    private getOptionsButtonRect;
    /** Return the screen rect for the close (✕) button. */
    private getOptionsCloseRect;
    private createOptionsMenu;
    /**
     * (Re)build the visual elements inside the options menu container.
     * Called once at creation and can be called again on resize.
     */
    private rebuildOptionsMenuVisuals;
    private toggleOptionsMenu;
    private handleOptionsAction;
    private returnToCharacterSelect;
    private logoutToLoginScreen;
    private resetHudLayout;
    private createHudEditModeUI;
    /** Map panel keys to their Phaser containers. */
    private getPanelContainerMap;
    private enterHudEditMode;
    private exitHudEditMode;
    /**
     * Apply the stored panelVisibility, panelOpacity and panelScale to the actual containers.
     * Called when loading layout, exiting edit mode, or resetting layout.
     */
    private applyPanelCustomisation;
    /**
     * Build floating control strips for each panel during HUD edit mode.
     * Each panel gets: [Eye] toggle visibility, [O] cycle opacity, [S] cycle scale
     */
    private buildHudEditControls;
    /**
     * Phaser lifecycle — called when the scene is stopped or replaced (e.g. logging out,
     * returning to character select). Saves HUD layout as a safety net so positions are
     * never lost even if the player leaves without completing a drag gesture.
     */
    shutdown(): void;
    /**
     * Single global pointer handler that detects drags on each panel's title bar
     * and moves the panel by updating its offset. Registered once in create().
     */
    private setupHudDragHandlers;
    private getTargetNameplateDefaultX;
    private getTargetNameplateDefaultY;
    private createTargetNameplate;
    private createPartyPanel;
    private createBuffsPanel;
    private updateBuffsPanel;
    private updatePartyPanel;
    private updateTargetNameplate;
    private setTarget;
    private clearTarget;
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
    private createChatPanel;
    private repositionChatPanel;
    private createCombatLogPanel;
    /** Get the default screen position of the combat log panel (top-right area). */
    private getCombatLogDefaultPos;
    /** Get the chat panel rect (for scroll / drag hit-testing). */
    private getChatPanelRect;
    /** Get the combat log panel rect (for drag detection). */
    private getCombatLogPanelRect;
    /** Resolve an entity ID (player/NPC session or state ID) to a display name. */
    private getCombatEntityName;
    /** Check if a given entity is the local player or a party member. */
    private isLocalOrParty;
    /** Push a combat log entry. Respects max message cap. */
    private pushCombatLog;
    /** Get filtered messages for display. */
    private getFilteredCombatLogMessages;
    private drawCombatLogPanel;
    private readonly CL_FILTER_LABELS;
    private openCombatLogContextMenu;
    private closeCombatLogContextMenu;
    private drawCombatLogContextMenu;
    /** Handle a click inside the context menu. Returns true if consumed. */
    private handleCombatLogContextClick;
    private drawChatPanel;
    private activateChatInput;
    private cancelChatInput;
    private submitChatInput;
    private receiveChatMessage;
    private pushChat;
    private pushSystemChat;
    private readonly AB_SLOT_SIZE;
    private readonly AB_SLOT_GAP;
    private readonly AB_PADDING;
    private createActionBar;
    private drawActionBar;
    /**
     * Get the action bar slot index at a given screen position, or -1.
     */
    private getActionBarSlotAt;
    private createCastBar;
    private drawCastBar;
    private createSkillsPane;
    private toggleSkillsPane;
    private renderSkillsPane;
    private showSkillTooltip;
    private hideSkillTooltip;
    private setupSkillsPaneDrag;
    private get lootPanelW();
    private get lootPanelH();
    private createLootPanel;
    private openLootPanel;
    private closeLootPanel;
    private refreshLootPanelItems;
    private renderLootPanel;
    /** Get the loot panel slot at a given screen position (absolute, not offset-adjusted). */
    private getLootSlotAt;
    /** Check if a screen position is inside the loot panel. */
    private isInsideLootPanel;
    /** Check if a screen position hits the loot panel close button. */
    private isLootPanelCloseBtn;
    /** Check if a screen position hits the "Loot All" button. */
    private isLootAllBtn;
    /** Create the item detail panel container (hidden on startup). */
    private createItemDetailPanel;
    /**
     * Populate and show the item detail panel for the given item ID.
     * Shift+clicking an inventory or equipment slot calls this.
     */
    private showItemDetail;
    /** Returns true if the screen position is over the item detail panel's close button. */
    private isItemDetailCloseBtn;
    /** Hide the item detail panel and clean up its dynamic objects. */
    private closeItemDetailPanel;
}
//# sourceMappingURL=GameScene.d.ts.map