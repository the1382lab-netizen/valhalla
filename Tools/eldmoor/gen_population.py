"""B-06 step 1.9 — Eldmoor population data.

Writes the Eldmoor enemy templates, the goblins, the Harrow's Rest friendly
NPCs, the loot tables and the few new items into shared/data, from the
approved design (Docs/Zones/Eldmoor/EldmoorDesign.md sections 5-7) and Kevin's
1.9 direction (2026-09-24):

* one or two goblins at every camp, in the camp's social group;
* every other enemy outside Ashvane Keep in leather with a mix of weapons;
* every enemy in the keep in plate or chainmail;
* patrols (the routes themselves are on the Unreal spawn points, placed by
  Valhalla2/Plugins/ValhallaTools/Content/Python/valhalla_tools/place_eldmoor_population.py).

The look (leather / chain / plate / goblin) is the NPC Type Blueprint's, not
the template's; LOOKS below is the table the placement script reads.

Re-runnable: it owns the ids it writes (listed in OWNED) and replaces them on
every run, and never touches any other template, table or item. Numbers are a
first pass for Kevin to tune in the web editor:

    hp        = 100 x level x role multiplier
    damage    = 3-4.5 x level x role multiplier (min-max roll)
    xpReward  = 100 x level (named x3)

Run from the repo root:  python Tools/eldmoor/gen_population.py
"""

import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DATA = os.path.join(ROOT, "shared", "data")

# ── Roles: stat multipliers and defaults ──────────────────────────────────
# hp, damage, attack ms, aggro cm, attack type
ROLES = {
    "melee":    (1.0, 1.0, 2400, 250, "melee"),
    "quick":    (0.8, 0.7, 1700, 250, "melee"),   # daggers
    "heavy":    (1.3, 1.15, 2800, 250, "melee"),  # thugs, bruisers, ringleaders, sergeants
    "archer":   (0.8, 0.8, 3200, 450, "ranged"),
    "lookout":  (0.9, 0.8, 3200, 640, "ranged"),  # 10-tile sight (design: C9)
    "caster":   (0.8, 0.8, 2800, 300, "melee"),   # NPC spellcasting doesn't exist yet: staff melee
    "named":    (2.0, 1.4, 2200, 300, "melee"),
    "rare":     (2.5, 1.6, 2200, 300, "melee"),
    "g_scrap":  (0.7, 0.8, 2000, 250, "melee"),   # goblin scrapper
    "g_sneak":  (0.5, 0.6, 1100, 250, "melee"),   # goblin sneak: fragile, fast
    "g_sling":  (0.6, 0.7, 3000, 450, "ranged"),  # goblin slinger (short bow)
}

LEASH_CM = 1500
MOVE_SPEED = 200
GOBLIN_SPEED = 190


def band_loot(level):
    if level <= 5:
        return "loot_eldmoor_4_5"
    if level <= 6:
        return "loot_eldmoor_5_6"
    if level <= 7:
        return "loot_eldmoor_6_7"
    if level <= 8:
        return "loot_eldmoor_7_8"
    if level <= 9:
        return "loot_eldmoor_8_9"
    return "loot_eldmoor_9_10"


# ── Enemy templates ───────────────────────────────────────────────────────
# id: (name, level, role, weapon, faction group or None (a single pull), look, respawn min, extra)
# Looks: leather, leather_bare, leather_named, chain, chain_caster, plate, plate_named, goblin.
E = {}


def enemy(tid, name, level, role, weapon, group, look, respawn_min, **extra):
    E[tid] = dict(name=name, level=level, role=role, weapon=weapon, group=group, look=look,
                  respawn_min=respawn_min, extra=extra)


# Southern Downs (C1 is Kevin's own Wood's Edge Bandits; only its goblin is added)
enemy("bandit_cutpurse", "a bandit cutpurse", 4, "quick", "iron_dagger", "bandits", "leather", 6)
enemy("bandit_archer", "a bandit archer", 5, "archer", "short_bow", "bandits", "leather", 6)
enemy("bandit_thug", "a bandit thug", 5, "heavy", "iron_mace", "bandits", "leather_bare", 6)
enemy("wayside_footpad", "a wayside footpad", 4, "quick", "iron_dagger", None, "leather", 8, behaviorType="patrol")
enemy("bandit_scout", "a bandit scout", 5, "archer", "short_bow", None, "leather", 8, behaviorType="patrol")
# Mistwater Vale
enemy("outlaw_footpad", "an outlaw footpad", 5, "quick", "iron_dagger", None, "leather", 8)
enemy("outlaw_bowman", "an outlaw bowman", 6, "archer", "short_bow", "outlaws", "leather", 8)
enemy("outlaw_ringleader", "an outlaw ringleader", 6, "heavy", "iron_sword", "outlaws", "leather_bare", 8)
enemy("outlaw_courier", "an outlaw courier", 6, "melee", "iron_sword", None, "leather", 10, behaviorType="patrol")
enemy("poacher", "a poacher", 5, "quick", "iron_dagger", "poachers", "leather", 8)
enemy("poacher_bowman", "a poacher bowman", 6, "archer", "short_bow", None, "leather", 8)
enemy("poacher_houndmaster", "a poacher houndmaster", 6, "heavy", "iron_mace", "poachers", "leather_bare", 8)
# Thornwood
enemy("poacher_skinner", "a poacher skinner", 6, "quick", "iron_dagger", None, "leather_bare", 10)
enemy("poacher_captain", "a poacher captain", 6, "heavy", "iron_sword", "poachers", "leather_named", 10)
enemy("named_merrik_sallow", "Merrik Sallow, the Poacher-King", 7, "named", "iron_sword", "poachers", "leather_named", 20)
enemy("poacher_tracker", "a poacher tracker", 6, "archer", "short_bow", None, "leather", 10, behaviorType="patrol")
# (C6 Deadfall reuses the outlaws; its sleepers get a 6-tile aggro range on the spawn, see the placement script)
# Kingsbarrow
enemy("cultist_acolyte", "a cultist acolyte", 6, "melee", "bone_totem", None, "leather", 10)
enemy("cultist_zealot", "a cultist zealot", 7, "melee", "bone_totem", "cultists", "leather_bare", 10)
enemy("cultist_bloodpriest", "a cultist bloodpriest", 7, "caster", "oak_staff", "cultists", "leather_bare", 10)
enemy("barrow_hierophant", "a barrow hierophant", 6, "caster", "oak_staff", "cultists", "leather_named", 10)
enemy("named_selwen_marr", "Hierophant Selwen Marr", 7, "named", "oak_staff", "cultists", "leather_named", 20)
enemy("cultist_procession", "a cultist of the procession", 7, "melee", "bone_totem", "procession", "leather", 12, behaviorType="patrol")
# Greyfell Highlands
enemy("brigand", "a brigand", 7, "melee", "iron_sword", "brigands", "leather", 12)
enemy("brigand_crossbowman", "a brigand crossbowman", 8, "archer", "short_bow", None, "leather", 12)
enemy("brigand_bruiser", "a brigand bruiser", 8, "heavy", "iron_mace", "brigands", "leather_bare", 12)
enemy("brigand_chief", "a brigand chief", 7, "heavy", "iron_sword", "brigands", "leather_named", 12)
enemy("named_grimald_hask", "Grimald Hask", 8, "named", "iron_sword", "brigands", "leather_named", 25)
enemy("brigand_lookout", "a brigand lookout", 7, "lookout", "short_bow", None, "leather", 12)
enemy("brigand_runner", "a brigand runner", 8, "quick", "iron_dagger", None, "leather", 12, behaviorType="patrol")
enemy("highland_scavenger", "a highland scavenger", 7, "quick", "iron_dagger", None, "leather_bare", 12, behaviorType="patrol")
# Burnt Steadings and the keep's approach (renegades outside the keep wear leather: Kevin's rule as given)
enemy("renegade_forager", "a renegade forager", 8, "melee", "iron_sword", "foragers", "leather", 12)
enemy("renegade_forager_archer", "a renegade forager archer", 8, "archer", "short_bow", None, "leather", 12)
enemy("renegade_forager_sergeant", "a renegade sergeant", 9, "heavy", "iron_mace", "foragers", "leather_named", 12)
enemy("renegade_patrol", "a renegade patrol", 9, "melee", "iron_sword", "renegade_patrol", "leather", 15, behaviorType="patrol")
# Ashvane Keep: chain for the rank and file, plate for the hard men and the named
enemy("renegade_footman", "a renegade footman", 8, "melee", "iron_sword", "ashvane", "chain", 15)
enemy("renegade_archer", "a renegade archer", 8, "archer", "short_bow", "ashvane", "chain", 15)
enemy("renegade_quartermaster", "a renegade quartermaster", 8, "melee", "iron_mace", None, "chain", 15)
enemy("renegade_penitent", "a renegade penitent", 8, "melee", "iron_mace", None, "chain", 15)
enemy("renegade_corporal", "a renegade corporal", 9, "heavy", "iron_sword", "ashvane", "plate", 15)
enemy("renegade_hallguard", "a renegade hallguard", 9, "heavy", "iron_mace", "ashvane_hall", "plate", 15)
enemy("renegade_steward", "a renegade steward", 9, "caster", "oak_staff", "ashvane_hall", "chain_caster", 15)
enemy("renegade_lieutenant", "a renegade lieutenant", 9, "heavy", "iron_sword", None, "plate_named", 28)
enemy("named_ordric_vane", "Castellan Ordric Vane", 10, "rare", "iron_sword", None, "plate_named", 28, lootTableId="loot_ordric_vane")
enemy("renegade_armsmaster", "a renegade armsmaster", 9, "heavy", "iron_sword", None, "plate", 15)
enemy("renegade_signaller", "a renegade signaller", 9, "archer", "short_bow", None, "plate", 15)
enemy("ashvane_zealot", "an Ashvane zealot", 9, "melee", "iron_mace", "ashvane_chapel", "chain_caster", 15)
enemy("named_kell_draven", "Sergeant-at-Arms Kell Draven", 9, "named", "iron_sword", "ashvane", "plate_named", 15)
enemy("named_mael_corvane", "Chaplain Mael Corvane", 9, "named", "oak_staff", "ashvane_chapel", "plate_named", 15)
enemy("named_brann_coll", "Gatewarden Brann Coll", 9, "named", "iron_mace", None, "plate_named", 15)
enemy("renegade_gaoler", "a renegade gaoler", 10, "heavy", "iron_mace", "ashvane_cellar", "chain", 20)
enemy("ashvane_alchemist", "an Ashvane alchemist", 10, "caster", "oak_staff", "ashvane_cellar", "chain_caster", 20)
enemy("named_warden_hesk", "Warden Hesk", 10, "named", "iron_sword", "ashvane_cellar", "plate_named", 20)

# Goblins: one or two per camp, joining the camp's fight (its social group).
GOBLIN_KINDS = {
    "scrapper": ("g_scrap", "a goblin scrapper", ["iron_dagger", "bone_totem"]),
    "sneak":    ("g_sneak", "a goblin sneak", ["iron_dagger"]),
    "slinger":  ("g_sling", "a goblin slinger", ["short_bow"]),
}
# camp: (level, camp social group, [kinds])
CAMP_GOBLINS = {
    "C1":  (4, "Bandits", ["scrapper"]),         # Kevin's C1 bandits use the group "Bandits"
    "C2":  (4, "bandits", ["scrapper", "sneak"]),
    "C3":  (5, "outlaws", ["scrapper", "slinger"]),
    "C4":  (5, "poachers", ["scrapper"]),
    "C5":  (6, "poachers", ["sneak", "slinger"]),
    "C6":  (5, "outlaws", ["sneak"]),
    "C7":  (6, "cultists", ["scrapper"]),
    "C8":  (7, "brigands", ["scrapper", "slinger"]),
    "C9":  (7, "brigand_lookouts", ["sneak"]),
    "C10": (8, "foragers", ["scrapper", "sneak"]),
}


def goblin_id(kind, camp):
    return "goblin_%s_%s" % (kind, camp.lower())


for camp, (level, group, kinds) in CAMP_GOBLINS.items():
    for i, kind in enumerate(kinds):
        role, name, weapons = GOBLIN_KINDS[kind]
        tid = goblin_id(kind, camp)
        # Alternate the scrappers' dagger and club from camp to camp.
        E[tid] = dict(name=name, level=level, role=role, weapon=weapons[(i + int(camp[1:])) % len(weapons)], group=group,
                      look="goblin", respawn_min=6 if level <= 5 else (10 if level <= 7 else 12),
                      extra=dict(spriteSize=0.72, moveSpeed=GOBLIN_SPEED))

# ── Friendly outpost NPCs (Harrow's Rest) ────────────────────────────────
FRIENDLY = {
    "npc_harrow_captain": ("Captain Bessa Harrow", "captain", "plate_named", "iron_sword",
                           "Harrow's Rest holds because nobody has told it to fall yet. Stay inside the stakes after dark, and don't cross the bridge alone."),
    "npc_harrow_guard_tam": ("Tam Pellow", "guard", "outpost_guard", "iron_sword",
                             "Road's yours as far as the bridge. Past the bridge, it's theirs."),
    "npc_harrow_guard_iselle": ("Iselle Dunmore", "guard", "outpost_guard", "iron_sword",
                                "Mind the mist in the vale. Things stand in it and don't move until you're close."),
    "npc_harrow_guard_orrin": ("Orrin Blackwater", "guard", "outpost_guard", "iron_mace",
                               "Some nights you can see the Lantern Tower's brazier from here, up on the keep. That's Vane's men laughing at us."),
    "npc_harrow_guard_wyn": ("Wyn Cotter", "guard", "outpost_guard", "iron_sword",
                             "Chased a brigand runner up under the ridge once. There's cold air coming out of the rocks under the tor, cold as a cellar. I didn't stay to learn why."),
    "npc_harrow_smith": ("Hedda Stroud", "blacksmith", "townsfolk", "iron_mace",
                         "Iron's dear this far from a town. Bring me back what the renegades drop and we'll talk."),
    "npc_harrow_armourer": ("Pol Garrick", "armourer", "townsfolk", None,
                            "Ashvane plate, if you can pry it off them. I can make it fit you."),
    "npc_harrow_provisioner": ("Mother Ada Quill", "provisioner", "townsfolk", None,
                               "Bread, salt pork, bandages. Nothing fancy, nothing poisoned, which is more than the cultists can say."),
    "npc_harrow_trader": ("Corwen Ashby", "trader", "townsfolk", None,
                          "I buy anything that isn't still bleeding. Barrow trinkets fetch the best price, if you're brave."),
    "npc_harrow_innkeeper": ("Maud Fenwick", "innkeeper", "townsfolk", None,
                             "A bed's a bed, even one this close to the vale. Don't track the mist in with you."),
}

# ── Items (new) ───────────────────────────────────────────────────────────
NEW_ITEMS = {
    "iron_cuirass": {
        "id": "iron_cuirass", "name": "Iron Cuirass",
        "description": "Breast- and backplate with pauldrons. The heaviest chest armour there is.",
        "category": "equipment", "equipSlot": "chest", "rarity": "uncommon",
        "stackable": False, "maxStack": 1,
        "statBonuses": {"stamina": 4, "physicalDefense": 6, "strength": 1},
        "inventoryIcon": "chest_priests_chain.png", "spriteId": "chest_iron_cuirass",
    },
    "ashvane_signet": {
        "id": "ashvane_signet", "name": "Ashvane Signet",
        "description": "The castellan's seal ring: a black hound on ash grey.",
        "category": "equipment", "equipSlot": "ring", "rarity": "rare",
        "stackable": False, "maxStack": 1,
        "statBonuses": {"strength": 3, "stamina": 3},
        "inventoryIcon": "ring_strength.png",
    },
    "castellans_sword": {
        "id": "castellans_sword", "name": "Castellan's Sword",
        "description": "Ordric Vane's blade, better steel than anything in the outpost.",
        "category": "equipment", "equipSlot": "weapon", "rarity": "rare",
        "stackable": False, "maxStack": 1,
        "statBonuses": {"strength": 5},
        "attackSpeedMs": 1500, "minDamage": 7, "maxDamage": 14,
        "inventoryIcon": "sword_iron.png", "spriteId": "sword_iron", "weaponStyle": "sword",
    },
    "hound_crest_shield": {
        "id": "hound_crest_shield", "name": "Hound-Crested Shield",
        "description": "A kite shield bearing the Ashvane hound.",
        "category": "equipment", "equipSlot": "offhand", "rarity": "rare",
        "stackable": False, "maxStack": 1,
        "statBonuses": {"blockRating": 0.07, "stamina": 3, "physicalDefense": 5},
        "inventoryIcon": "shield_kite_iron.png", "spriteId": "shield_kite_iron",
    },
}

# ── Loot tables ───────────────────────────────────────────────────────────
def entry(item, chance, weight=1, qmin=1, qmax=1):
    return {"itemId": item, "weight": weight, "dropChance": chance, "minQuantity": qmin, "maxQuantity": qmax}


LOOT = {
    "loot_eldmoor_4_5": ("Eldmoor 4-5 (Downs)", [
        entry("gold_coin", 0.8, qmin=2, qmax=8), entry("health_potion", 0.15),
        entry("leather_gloves", 0.04), entry("leather_boots", 0.04), entry("leather_helm", 0.04),
        entry("iron_dagger", 0.03), entry("short_bow", 0.02)]),
    "loot_eldmoor_5_6": ("Eldmoor 5-6 (Vale)", [
        entry("gold_coin", 0.8, qmin=3, qmax=10), entry("health_potion", 0.15), entry("mana_potion", 0.08),
        entry("leather_tunic", 0.04), entry("leather_leggings", 0.04), entry("iron_sword", 0.03),
        entry("short_bow", 0.03)]),
    "loot_eldmoor_6_7": ("Eldmoor 6-7 (Thornwood, Kingsbarrow)", [
        entry("gold_coin", 0.8, qmin=4, qmax=12), entry("health_potion", 0.18), entry("mana_potion", 0.1),
        entry("leather_tunic", 0.05), entry("iron_mace", 0.03), entry("oak_staff", 0.03),
        entry("bone_totem", 0.03), entry("ring_of_wisdom", 0.01)]),
    "loot_eldmoor_7_8": ("Eldmoor 7-8 (Highlands)", [
        entry("gold_coin", 0.85, qmin=5, qmax=15), entry("health_potion", 0.2), entry("mana_potion", 0.1),
        entry("iron_helm", 0.04), entry("iron_gauntlets", 0.04), entry("iron_buckler", 0.03),
        entry("ring_of_strength", 0.01)]),
    "loot_eldmoor_8_9": ("Eldmoor 8-9 (Steadings, keep gate)", [
        entry("gold_coin", 0.85, qmin=6, qmax=18), entry("health_potion", 0.2), entry("mana_potion", 0.12),
        entry("chainmail", 0.04), entry("iron_boots", 0.04), entry("iron_greaves", 0.03),
        entry("iron_kite_shield", 0.02)]),
    "loot_eldmoor_9_10": ("Eldmoor 9-10 (Ashvane Keep)", [
        entry("gold_coin", 0.9, qmin=8, qmax=24), entry("health_potion", 0.22), entry("mana_potion", 0.14),
        entry("chainmail", 0.05), entry("iron_greaves", 0.04), entry("iron_cuirass", 0.02),
        entry("iron_kite_shield", 0.03)]),
    "loot_eldmoor_named": ("Eldmoor named", [
        entry("gold_coin", 1.0, qmin=15, qmax=40), entry("health_potion", 0.5),
        entry("iron_cuirass", 0.08), entry("chainmail", 0.15), entry("iron_kite_shield", 0.1),
        entry("ring_of_strength", 0.08), entry("ring_of_wisdom", 0.08)]),
    "loot_ordric_vane": ("Castellan Ordric Vane", [
        entry("gold_coin", 1.0, qmin=40, qmax=80), entry("ashvane_signet", 0.35),
        entry("castellans_sword", 0.3), entry("hound_crest_shield", 0.3), entry("iron_cuirass", 0.25)]),
}


def build_enemy(tid, e):
    hp_m, dmg_m, atk_ms, aggro, attack_type = ROLES[e["role"]]
    level = e["level"]
    lo = round(3 * level * dmg_m)
    hi = round(4.5 * level * dmg_m)
    named = e["role"] in ("named", "rare")
    t = {
        "id": tid,
        "name": e["name"],
        "description": "Eldmoor (B-06 1.9).",
        "type": "enemy",
        "level": level,
        "stats": {},
        "hp": int(round(100 * level * hp_m / 10.0) * 10),
        "mana": 0,
        "lootTableId": "loot_eldmoor_named" if named else band_loot(level),
        "behaviorType": "aggressive",
        "canAggro": True,
        "aggroRange": aggro,
        "leashRange": LEASH_CM,
        "minDamage": lo,
        "maxDamage": hi,
        "damage": round((lo + hi) / 2),
        "weaponId": e["weapon"],
        "attackSpeed": atk_ms,
        "moveSpeed": MOVE_SPEED,
        "respawnMs": e["respawn_min"] * 60000,
        "skills": [],
        "spriteColor": 16777215,
        "spriteSize": 1,
        "xpReward": 100 * level * (3 if named else 1),
        "dialogue": [],
        "vendorInventory": [],
    }
    if attack_type == "ranged":
        t["attackType"] = "ranged"
    if e["group"]:
        t["canSocialAggro"] = True
        t["socialGroup"] = e["group"]
        t["socialRange"] = 256
    t.update(e["extra"])
    return t


def build_friendly(tid, f):
    name, role, look, weapon, hail = f
    t = {
        "id": tid, "name": name, "description": "Harrow's Rest (B-06 1.9e).",
        "type": "npc", "role": role, "level": 12, "stats": {}, "hp": 3000, "mana": 0,
        "behaviorType": "stationary", "respawnMs": 300000, "skills": [],
        "spriteColor": 16777215, "spriteSize": 1, "xpReward": 0,
        "dialogue": [hail], "vendorInventory": [], "moveSpeed": 180,
    }
    if weapon:
        t["weaponId"] = weapon
    return t


# Every id this script owns (and rewrites on each run). Written for the placement script too.
LOOKS = {tid: e["look"] for tid, e in E.items()}
LOOKS.update({tid: f[2] for tid, f in FRIENDLY.items()})


def load(name):
    with open(os.path.join(DATA, name), encoding="utf-8") as fh:
        return json.load(fh)


def save(name, data, ascii_):
    with open(os.path.join(DATA, name), "w", encoding="utf-8", newline="\n") as fh:
        fh.write(json.dumps(data, indent=2, ensure_ascii=ascii_) + "\n")


def main():
    npcs = load("npc-templates.json")
    templates = npcs["templates"]
    # Goblin ids from an earlier run that this run no longer writes.
    for stale in [k for k in templates if k.startswith("goblin_") and k not in E]:
        del templates[stale]
    for tid, e in E.items():
        templates[tid] = build_enemy(tid, e)
    for tid, f in FRIENDLY.items():
        templates[tid] = build_friendly(tid, f)
    save("npc-templates.json", npcs, True)

    loot = load("loot-tables.json")
    for lid, (name, entries) in LOOT.items():
        loot["tables"][lid] = {"id": lid, "name": name, "entries": entries}
    save("loot-tables.json", loot, True)

    items = load("items.json")
    for iid, item in NEW_ITEMS.items():
        items["items"][iid] = item
    save("items.json", items, False)

    with open(os.path.join(ROOT, "Tools", "eldmoor", "population_looks.json"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write(json.dumps({"looks": LOOKS, "campGoblins": {c: [goblin_id(k, c) for k in ks]
                                                              for c, (lv, g, ks) in CAMP_GOBLINS.items()}},
                            indent=2) + "\n")

    print("templates: %d enemy (%d goblin), %d friendly; loot tables: %d; items: %d"
          % (len(E), sum(1 for e in E.values() if e["look"] == "goblin"), len(FRIENDLY), len(LOOT), len(NEW_ITEMS)))


if __name__ == "__main__":
    sys.exit(main())
