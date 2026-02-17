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
})(MessageType || (MessageType = {}));
//# sourceMappingURL=types.js.map