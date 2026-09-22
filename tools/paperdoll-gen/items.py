"""Base bodies, the equipment catalogue, and the outfit presets."""

GOLD = (228, 188, 96)

# ------------------------------------------------------------------ bodies
BODIES = [
    dict(id="body_fair",  label="Fair",  skin=(238, 196, 158), hair=(112, 78, 50), cloth=(132, 116, 98)),
    dict(id="body_tan",   label="Tan",   skin=(208, 160, 118), hair=(84, 60, 44),  cloth=(132, 116, 98)),
    dict(id="body_olive", label="Olive", skin=(184, 140, 102), hair=(58, 44, 36),  cloth=(132, 116, 98)),
    dict(id="body_dark",  label="Dark",  skin=(140, 98, 72),   hair=(46, 36, 32),  cloth=(132, 116, 98)),
    dict(id="body_elder", label="Elder", skin=(232, 194, 160), hair=(208, 204, 194),
         cloth=(132, 116, 98), beard=(208, 204, 194)),
]

# ------------------------------------------------------------------ items
def I(id, slot, label, **kw):
    return dict(id=id, slot=slot, label=label, **kw)

ITEMS = [
    # ---------------------------------------------------------------- helm
    I("helm_iron_open", "helm", "Open Iron Helm", style="open", main=(122, 114, 106)),
    I("helm_iron_full", "helm", "Iron Great Helm", style="full", main=(152, 158, 170),
      trim=GOLD, glow=(240, 120, 80)),
    I("helm_valhalla_horned", "helm", "Horned Helm of Valhalla", style="horned",
      main=(88, 96, 116), trim=GOLD, horn=(226, 218, 196), glow=(244, 132, 74)),
    I("hood_scout", "helm", "Scout's Hood", style="hood", main=(92, 118, 76), trim=(178, 152, 94)),
    I("hood_warden", "helm", "Warden's Hood", style="hood", main=(58, 96, 70), trim=GOLD),
    I("hat_adept", "helm", "Adept's Hat", style="hat", main=(72, 92, 156), trim=GOLD),
    I("hat_archmage", "helm", "Archmage's Hat", style="hat", main=(98, 66, 150), trim=GOLD),
    I("hood_priest", "helm", "Priest's Hood", style="hood", main=(212, 206, 190), trim=GOLD),
    I("mitre_high_cleric", "helm", "High Cleric's Mitre", style="mitre",
      main=(244, 242, 234), trim=GOLD),
    I("circlet_apprentice", "helm", "Apprentice's Circlet", style="circlet",
      main=(160, 150, 130), trim=(198, 180, 120)),

    # ---------------------------------------------------------------- chest
    I("chest_rusted_mail", "chest", "Rusted Mail", main=(122, 114, 106), alt=(98, 90, 84),
      cloth=(132, 66, 54), belt=(92, 64, 42)),
    I("chest_iron_plate", "chest", "Iron Plate", main=(152, 158, 170), alt=(120, 126, 140),
      cloth=(170, 50, 46), belt=(86, 60, 40), trim=GOLD, pauldrons=True),
    I("chest_valhalla_plate", "chest", "Valhalla Plate", main=(88, 96, 116), alt=(66, 74, 92),
      cloth=(150, 34, 42), belt=(70, 48, 34), trim=GOLD, pauldrons=True),
    I("chest_travelers_jerkin", "chest", "Traveler's Jerkin", main=(154, 126, 86),
      alt=(92, 72, 52), cloth=(112, 88, 62), belt=(78, 54, 36)),
    I("chest_scouts_hide", "chest", "Scout's Hide", main=(92, 118, 76), alt=(56, 72, 48),
      cloth=(74, 96, 64), belt=(98, 72, 48), trim=(178, 152, 94)),
    I("chest_wardens_garb", "chest", "Warden's Garb", main=(58, 96, 70), alt=(32, 52, 40),
      cloth=(44, 74, 56), belt=(76, 56, 38), trim=GOLD, pauldrons=True),
    I("chest_apprentice_robe", "chest", "Apprentice Robe", main=(100, 112, 142),
      alt=(82, 92, 120), cloth=(114, 128, 162), belt=(80, 64, 52)),
    I("chest_adept_robe", "chest", "Adept Robe", main=(60, 78, 136), alt=(46, 60, 110),
      cloth=(72, 92, 156), belt=(74, 58, 46), trim=GOLD),
    I("chest_archmage_robe", "chest", "Archmage Robe", main=(80, 54, 126), alt=(60, 40, 98),
      cloth=(98, 66, 150), belt=(70, 54, 44), trim=GOLD),
    I("chest_acolyte_vestment", "chest", "Acolyte Vestment", main=(198, 190, 168),
      alt=(164, 154, 130), cloth=(178, 168, 144), belt=(106, 82, 56)),
    I("chest_priests_chain", "chest", "Priest's Chain", main=(176, 182, 194),
      alt=(136, 142, 156), cloth=(212, 206, 190), belt=(96, 74, 52), trim=GOLD, pauldrons=True),
    I("chest_high_cleric", "chest", "High Cleric Regalia", main=(236, 234, 224),
      alt=(190, 188, 176), cloth=(244, 242, 234), belt=(104, 82, 58), trim=GOLD, pauldrons=True),

    # ---------------------------------------------------------------- legs
    I("legs_rusted_mail", "legs", "Rusted Leggings", style="pants", main=(98, 90, 84)),
    I("legs_iron_plate", "legs", "Iron Greaves", style="pants", main=(120, 126, 140), trim=GOLD),
    I("legs_valhalla", "legs", "Valhalla Greaves", style="pants", main=(66, 74, 92), trim=GOLD),
    I("legs_travelers", "legs", "Traveler's Trousers", style="pants", main=(92, 72, 52)),
    I("legs_scouts", "legs", "Scout's Leggings", style="pants", main=(56, 72, 48)),
    I("legs_wardens", "legs", "Warden's Leggings", style="pants", main=(32, 52, 40)),
    I("legs_apprentice_robe", "legs", "Apprentice Skirts", style="robe", main=(114, 128, 162)),
    I("legs_adept_robe", "legs", "Adept Skirts", style="robe", main=(72, 92, 156), trim=GOLD),
    I("legs_archmage_robe", "legs", "Archmage Skirts", style="robe", main=(98, 66, 150), trim=GOLD),
    I("legs_acolyte_robe", "legs", "Acolyte Skirts", style="robe", main=(178, 168, 144)),
    I("legs_priests", "legs", "Priest's Leggings", style="pants", main=(136, 142, 156), trim=GOLD),
    I("legs_high_cleric_robe", "legs", "High Cleric Skirts", style="robe",
      main=(244, 242, 234), trim=GOLD),

    # ---------------------------------------------------------------- gloves
    I("gloves_worn_leather", "gloves", "Worn Leather Gloves", main=(92, 64, 42)),
    I("gloves_iron_gauntlets", "gloves", "Iron Gauntlets", main=(120, 126, 140), trim=GOLD),
    I("gloves_valhalla_gauntlets", "gloves", "Valhalla Gauntlets", main=(66, 74, 92), trim=GOLD),
    I("gloves_ranger_bracers", "gloves", "Ranger's Bracers", main=(78, 54, 36)),
    I("gloves_cloth_wraps", "gloves", "Cloth Wraps", main=(96, 80, 66)),
    I("gloves_blessed", "gloves", "Blessed Gloves", main=(200, 196, 182), trim=GOLD),

    # ---------------------------------------------------------------- boots
    I("boots_worn", "boots", "Worn Boots", main=(92, 64, 42)),
    I("boots_iron_sabatons", "boots", "Iron Sabatons", main=(120, 126, 140)),
    I("boots_valhalla_sabatons", "boots", "Valhalla Sabatons", main=(66, 74, 92), trim=GOLD),
    I("boots_ranger", "boots", "Ranger's Boots", main=(78, 54, 36)),
    I("boots_cloth_slippers", "boots", "Cloth Slippers", main=(96, 80, 66)),
    I("boots_blessed", "boots", "Blessed Boots", main=(200, 196, 182), trim=GOLD),

    # ---------------------------------------------------------------- mainhand
    I("sword_iron", "mainhand", "Iron Sword", style="sword", metal=(178, 182, 190),
      wood=(92, 64, 42), trim=(190, 170, 120)),
    I("sword_steel", "mainhand", "Steel Sword", style="sword", metal=(214, 222, 234),
      wood=(86, 60, 40), trim=GOLD),
    I("greatsword_valhalla", "mainhand", "Valhalla Greatsword", style="greatsword",
      metal=(220, 228, 242), wood=(70, 48, 34), trim=GOLD),
    I("bow_hunting", "mainhand", "Hunting Bow", style="bow", wood=(130, 92, 56),
      metal=(188, 192, 200)),
    I("bow_scouts", "mainhand", "Scout's Bow", style="bow", wood=(120, 84, 50),
      metal=(196, 200, 208)),
    I("bow_warden_longbow", "mainhand", "Warden's Longbow", style="bow", wood=(98, 68, 42),
      metal=(208, 214, 222), trim=GOLD),
    I("staff_apprentice", "mainhand", "Apprentice Staff", style="staff", wood=(122, 88, 56),
      glow=(142, 202, 246)),
    I("staff_adept", "mainhand", "Adept Staff", style="staff", wood=(116, 82, 52),
      glow=(122, 182, 255), trim=GOLD),
    I("staff_archmage", "mainhand", "Archmage Staff", style="staff", wood=(104, 74, 48),
      glow=(198, 134, 255), trim=GOLD),
    I("mace_wooden", "mainhand", "Wooden Mace", style="mace", metal=(196, 200, 208),
      wood=(126, 90, 56)),
    I("mace_priests", "mainhand", "Priest's Mace", style="mace", metal=(206, 214, 226),
      wood=(120, 86, 54), trim=GOLD),
    I("mace_radiant", "mainhand", "Radiant Mace", style="mace", metal=(206, 214, 226),
      wood=(112, 80, 50), trim=GOLD, glow=(255, 246, 200)),

    # ---------------------------------------------------------------- offhand
    I("shield_buckler_iron", "offhand", "Iron Buckler", style="buckler", main=(98, 90, 84)),
    I("shield_kite_iron", "offhand", "Iron Kite Shield", style="kite", main=(120, 126, 140),
      trim=GOLD),
    I("shield_tower_valhalla", "offhand", "Valhalla Tower Shield", style="tower",
      main=(66, 74, 92), trim=GOLD),
    I("shield_buckler_priest", "offhand", "Priest's Buckler", style="buckler",
      main=(136, 142, 156), trim=GOLD),
    I("shield_kite_blessed", "offhand", "Blessed Kite Shield", style="kite",
      main=(190, 188, 176), trim=GOLD),

    # ---------------------------------------------------------------- back
    I("cape_valhalla", "back", "Valhalla Cape", style="cape", main=(154, 36, 44), trim=GOLD),
    I("cloak_warden", "back", "Warden's Cloak", style="cloak", main=(34, 60, 46), trim=GOLD),
    I("mantle_archmage", "back", "Archmage Mantle", style="cloak", main=(64, 42, 102), trim=GOLD),
    I("mantle_high_cleric", "back", "High Cleric Mantle", style="cloak",
      main=(240, 238, 230), trim=GOLD),
    I("quiver_rangers", "back", "Ranger's Quiver", style="quiver", main=(88, 62, 40),
      trim=(206, 196, 170)),
]

ITEM_BY_ID = {i["id"]: i for i in ITEMS}
BODY_BY_ID = {b["id"]: b for b in BODIES}

SLOTS = ["back", "legs", "boots", "chest", "gloves", "helm", "offhand", "mainhand"]

# ------------------------------------------------------------------ presets
def P(id, label, cls, tier, body, **slots):
    return dict(id=id, label=label, cls=cls, tier=tier, body=body, slots=slots)

PRESETS = [
    P("warrior_01_rusted_mail", "Rusted Mail", "warrior", 1, "body_tan",
      helm="helm_iron_open", chest="chest_rusted_mail", legs="legs_rusted_mail",
      gloves="gloves_worn_leather", boots="boots_worn",
      mainhand="sword_iron", offhand="shield_buckler_iron"),
    P("warrior_02_iron_plate", "Iron Plate", "warrior", 2, "body_tan",
      helm="helm_iron_full", chest="chest_iron_plate", legs="legs_iron_plate",
      gloves="gloves_iron_gauntlets", boots="boots_iron_sabatons",
      mainhand="sword_steel", offhand="shield_kite_iron"),
    P("warrior_03_valhalla_plate", "Valhalla Plate", "warrior", 3, "body_tan",
      helm="helm_valhalla_horned", chest="chest_valhalla_plate", legs="legs_valhalla",
      gloves="gloves_valhalla_gauntlets", boots="boots_valhalla_sabatons",
      mainhand="greatsword_valhalla", offhand="shield_tower_valhalla", back="cape_valhalla"),

    P("ranger_01_travelers_leathers", "Traveler's Leathers", "ranger", 1, "body_fair",
      chest="chest_travelers_jerkin", legs="legs_travelers",
      gloves="gloves_ranger_bracers", boots="boots_ranger",
      mainhand="bow_hunting", back="quiver_rangers"),
    P("ranger_02_scouts_hide", "Scout's Hide", "ranger", 2, "body_fair",
      helm="hood_scout", chest="chest_scouts_hide", legs="legs_scouts",
      gloves="gloves_ranger_bracers", boots="boots_ranger",
      mainhand="bow_scouts", back="quiver_rangers"),
    P("ranger_03_wardens_garb", "Warden's Garb", "ranger", 3, "body_fair",
      helm="hood_warden", chest="chest_wardens_garb", legs="legs_wardens",
      gloves="gloves_ranger_bracers", boots="boots_ranger",
      mainhand="bow_warden_longbow", back="cloak_warden"),

    P("wizard_01_apprentice_robe", "Apprentice Robe", "wizard", 1, "body_fair",
      chest="chest_apprentice_robe", legs="legs_apprentice_robe",
      gloves="gloves_cloth_wraps", boots="boots_cloth_slippers", mainhand="staff_apprentice"),
    P("wizard_02_adept_robe", "Adept Robe", "wizard", 2, "body_fair",
      helm="hat_adept", chest="chest_adept_robe", legs="legs_adept_robe",
      gloves="gloves_cloth_wraps", boots="boots_cloth_slippers", mainhand="staff_adept"),
    P("wizard_03_archmage_robe", "Archmage Robe", "wizard", 3, "body_elder",
      helm="hat_archmage", chest="chest_archmage_robe", legs="legs_archmage_robe",
      gloves="gloves_cloth_wraps", boots="boots_cloth_slippers",
      mainhand="staff_archmage", back="mantle_archmage"),

    P("cleric_01_acolyte_vestments", "Acolyte Vestments", "cleric", 1, "body_fair",
      chest="chest_acolyte_vestment", legs="legs_acolyte_robe",
      gloves="gloves_cloth_wraps", boots="boots_cloth_slippers", mainhand="mace_wooden"),
    P("cleric_02_priests_chain", "Priest's Chain", "cleric", 2, "body_fair",
      helm="hood_priest", chest="chest_priests_chain", legs="legs_priests",
      gloves="gloves_blessed", boots="boots_blessed",
      mainhand="mace_priests", offhand="shield_buckler_priest"),
    P("cleric_03_high_cleric_regalia", "High Cleric Regalia", "cleric", 3, "body_fair",
      helm="mitre_high_cleric", chest="chest_high_cleric", legs="legs_high_cleric_robe",
      gloves="gloves_blessed", boots="boots_blessed",
      mainhand="mace_radiant", offhand="shield_kite_blessed", back="mantle_high_cleric"),
]
