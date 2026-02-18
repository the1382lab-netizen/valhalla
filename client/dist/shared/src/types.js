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
})(MessageType || (MessageType = {}));
//# sourceMappingURL=types.js.map