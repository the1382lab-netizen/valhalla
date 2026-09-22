// ── Messages ────────────────────────────────────────────────
export var MessageType;
(function (MessageType) {
    MessageType["INPUT"] = "input";
    MessageType["PLAYER_HIT"] = "playerHit";
    MessageType["PLAYER_DIED"] = "playerDied";
    MessageType["PLAYER_RESPAWNED"] = "playerRespawned";
    MessageType["MELEE_ATTACK"] = "meleeAttack";
    MessageType["MISSED"] = "missed";
    MessageType["DODGED"] = "dodged";
    MessageType["BLOCKED"] = "blocked";
    MessageType["INVENTORY_ACTION"] = "inventoryAction";
    MessageType["EQUIP_ITEM"] = "equipItem";
    MessageType["UNEQUIP_ITEM"] = "unequipItem";
    MessageType["DROP_ITEM"] = "dropItem";
    MessageType["SWAP_INVENTORY"] = "swapInventory";
    MessageType["MAP_DATA"] = "mapData";
    MessageType["ZONE_CHANGE"] = "zoneChange";
    // ── Skills & Spells ──
    MessageType["CAST_SKILL"] = "castSkill";
    MessageType["CANCEL_CAST"] = "cancelCast";
    MessageType["SKILL_STARTED"] = "skillStarted";
    MessageType["SKILL_EFFECT"] = "skillEffect";
    MessageType["SKILL_FAILED"] = "skillFailed";
    MessageType["SKILL_INTERRUPTED"] = "skillInterrupted";
    MessageType["BUFF_APPLIED"] = "buffApplied";
    MessageType["BUFF_REMOVED"] = "buffRemoved";
    MessageType["SET_ACTION_BAR"] = "setActionBar";
    MessageType["ACTION_BAR_DATA"] = "actionBarData";
    // ── Spell Projectiles ──
    MessageType["SPELL_IMPACT"] = "spellImpact";
    // ── NPC Combat ──
    MessageType["NPC_HIT"] = "npcHit";
    MessageType["NPC_DIED"] = "npcDied";
    // ── Chat ──
    MessageType["CHAT_MESSAGE"] = "chatMessage";
    // ── Leveling ──
    MessageType["LEVEL_UP"] = "levelUp";
    // ── Loot ──
    MessageType["LOOT_ITEM"] = "lootItem";
    MessageType["LOOT_ALL"] = "lootAll";
    // Server → client confirmation: refresh loot panel + inventory immediately
    MessageType["LOOT_SUCCESS"] = "lootSuccess";
    // ── Auto-Attack ──
    MessageType["START_AUTO_ATTACK"] = "startAutoAttack";
    MessageType["STOP_AUTO_ATTACK"] = "stopAutoAttack";
    MessageType["AUTO_ATTACK_HIT"] = "autoAttackHit";
    MessageType["AUTO_ATTACK_STARTED"] = "autoAttackStarted";
    MessageType["AUTO_ATTACK_STOPPED"] = "autoAttackStopped";
    // ── XP ──
    MessageType["XP_GAINED"] = "xpGained";
    // ── Party ──
    MessageType["PARTY_INVITE"] = "partyInvite";
    MessageType["PARTY_ACCEPT"] = "partyAccept";
    MessageType["PARTY_DECLINE"] = "partyDecline";
    MessageType["PARTY_LEAVE"] = "partyLeave";
    MessageType["PARTY_UPDATE"] = "partyUpdate";
    MessageType["PARTY_KICKED"] = "partyKicked";
})(MessageType || (MessageType = {}));
//# sourceMappingURL=types.js.map