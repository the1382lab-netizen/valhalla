export interface InputPayload {
    /** Movement direction flags */
    up: boolean;
    down: boolean;
    left: boolean;
    right: boolean;
    /** Aim angle in radians (0 = right, PI/2 = down) */
    aimAngle: number;
    /** Monotonically increasing input sequence number */
    seq: number;
}
export interface StartAutoAttackPayload {
    skillId: string;
    targetId: string;
}
export interface AutoAttackHitPayload {
    attackerId: string;
    targetId: string;
    damage: number;
    damageType: 'physical' | 'magical';
    isCrit: boolean;
    isBlock: boolean;
    isDodge: boolean;
    isMiss: boolean;
}
export interface IPlayerState {
    id: string;
    x: number;
    y: number;
    aimAngle: number;
    speed: number;
    hp: number;
    maxHp: number;
    mana: number;
    maxMana: number;
    alive: boolean;
    inputSeq: number;
    classId: string;
    level: number;
    xp: number;
}
export interface TileMapData {
    width: number;
    height: number;
    tileSize: number;
    /** 1D collision grid: 0 = passable, 1 = blocked */
    collisionGrid: number[];
}
export interface IProjectileState {
    id: string;
    ownerId: string;
    x: number;
    y: number;
    angle: number;
    speed: number;
    damage: number;
}
export interface ISpellProjectileState {
    id: string;
    ownerId: string;
    skillId: string;
    x: number;
    y: number;
    targetX: number;
    targetY: number;
    speed: number;
}
export interface CastSkillPayload {
    skillId: string;
    targetId?: string;
    /** Ground target position — required for AOE_GROUND skills like Fireball */
    groundX?: number;
    groundY?: number;
}
export interface SpellImpactPayload {
    skillId: string;
    x: number;
    y: number;
    radius: number;
}
export declare enum MessageType {
    INPUT = "input",
    PLAYER_HIT = "playerHit",
    PLAYER_DIED = "playerDied",
    PLAYER_RESPAWNED = "playerRespawned",
    MELEE_ATTACK = "meleeAttack",
    MISSED = "missed",
    DODGED = "dodged",
    BLOCKED = "blocked",
    INVENTORY_ACTION = "inventoryAction",
    EQUIP_ITEM = "equipItem",
    UNEQUIP_ITEM = "unequipItem",
    DROP_ITEM = "dropItem",
    SWAP_INVENTORY = "swapInventory",
    MAP_DATA = "mapData",
    ZONE_CHANGE = "zoneChange",
    CAST_SKILL = "castSkill",
    CANCEL_CAST = "cancelCast",
    SKILL_STARTED = "skillStarted",
    SKILL_EFFECT = "skillEffect",
    SKILL_FAILED = "skillFailed",
    SKILL_INTERRUPTED = "skillInterrupted",
    BUFF_APPLIED = "buffApplied",
    BUFF_REMOVED = "buffRemoved",
    SET_ACTION_BAR = "setActionBar",
    ACTION_BAR_DATA = "actionBarData",
    SPELL_IMPACT = "spellImpact",
    NPC_HIT = "npcHit",
    NPC_DIED = "npcDied",
    CHAT_MESSAGE = "chatMessage",
    LEVEL_UP = "levelUp",
    LOOT_ITEM = "lootItem",
    LOOT_ALL = "lootAll",
    LOOT_SUCCESS = "lootSuccess",
    START_AUTO_ATTACK = "startAutoAttack",
    STOP_AUTO_ATTACK = "stopAutoAttack",
    AUTO_ATTACK_HIT = "autoAttackHit",
    AUTO_ATTACK_STARTED = "autoAttackStarted",
    AUTO_ATTACK_STOPPED = "autoAttackStopped",
    XP_GAINED = "xpGained",
    PARTY_INVITE = "partyInvite",
    PARTY_ACCEPT = "partyAccept",
    PARTY_DECLINE = "partyDecline",
    PARTY_LEAVE = "partyLeave",
    PARTY_UPDATE = "partyUpdate",
    PARTY_KICKED = "partyKicked"
}
export interface PartyMemberInfo {
    sessionId: string;
    characterName: string;
}
export interface PartyUpdatePayload {
    members: PartyMemberInfo[];
}
export interface ChatMessagePayload {
    /** Channel the message was sent on. 'system' is server-generated. */
    channel: 'general' | 'world' | 'whisper' | 'system';
    /** Display name of the sender (characterName). Empty for system messages. */
    senderName: string;
    /** The message text (already validated & trimmed by the server). */
    message: string;
    /** Whisper only: recipient's character name. */
    targetName?: string;
    /** Unix ms timestamp set by server. */
    timestamp: number;
}
//# sourceMappingURL=types.d.ts.map