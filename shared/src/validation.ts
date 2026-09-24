/**
 * B-13: one cross-reference check over all game content.
 *
 * Pure (no file system), so the same rules run in the command-line tool
 * (shared/scripts/validate-data.ts, the git pre-commit hook), in the web
 * editor's server after every Save, and on its Validation page.
 *
 * Input is the parsed data (the shapes the shared loaders return) plus
 * optional context the caller could find on disk: the equipment mesh files,
 * the icon files, the zone overlays and the Unreal-side references exported by
 * `valhalla_tools/export_unreal_refs.py`. A check whose context is missing is
 * skipped and says so with an `info` issue, never a false error.
 *
 * Severity: `error` means the game will misbehave (a missing item, template or
 * zone); `warning` means it works but probably not as intended (an ignored
 * field, a mechanic that isn't built yet); `info` is about the check itself.
 */

// ── B-06: zone atmosphere ─────────────────────────────────────────────

/** The numeric ZoneAtmosphere fields, for editors and validators. */
export const ZONE_ATMOSPHERE_NUMBER_FIELDS = [
  'visionScale',
  'visionClearFraction',
  'relevancyMarginCm',
  'heightFogDensity',
  'heightFogStartCm',
  'sunIntensityScale',
  'skyLightIntensityScale',
  'cameraMaxArmCm',
  'firelightGlow',
  'firelightRangeCm',
] as const;

/** The colour ZoneAtmosphere fields (`#rrggbb`). */
export const ZONE_ATMOSPHERE_COLOR_FIELDS = ['fogColor', 'gradeTint'] as const;

/**
 * B-06: check a zone's `atmosphere` object (the ZoneAtmosphere type is in
 * maps.ts). The single source of these rules: validateGameData below, the
 * shared loader and the web editor's Zones page all call it. Returns human-readable problems; an
 * empty list means it is valid (a missing atmosphere is valid).
 */
export function validateZoneAtmosphere(atmosphere: unknown): string[] {
  const errors: string[] = [];
  if (atmosphere === undefined) return errors;
  if (atmosphere === null || typeof atmosphere !== 'object' || Array.isArray(atmosphere)) {
    return ['atmosphere must be an object'];
  }
  const a = atmosphere as Record<string, unknown>;
  const known = new Set<string>(['notes', ...ZONE_ATMOSPHERE_NUMBER_FIELDS, ...ZONE_ATMOSPHERE_COLOR_FIELDS]);
  const retired = new Set<string>(['visionClearRadiusCm', 'visionFadeWidthCm', 'netRelevancyRadiusCm']);
  for (const key of Object.keys(a)) {
    if (retired.has(key)) {
      errors.push(`atmosphere.${key} is no longer used: vision is visionScale x the class range (with visionClearFraction and relevancyMarginCm)`);
    } else if (!known.has(key)) {
      errors.push(`atmosphere.${key} is not a known field`);
    }
  }
  if (a.notes !== undefined && typeof a.notes !== 'string') {
    errors.push('atmosphere.notes must be text');
  }
  for (const key of ZONE_ATMOSPHERE_NUMBER_FIELDS) {
    const v = a[key];
    if (v === undefined) continue;
    if (typeof v !== 'number' || !Number.isFinite(v) || v < 0) {
      errors.push(`atmosphere.${key} must be a number >= 0`);
    }
  }
  for (const key of ZONE_ATMOSPHERE_COLOR_FIELDS) {
    const v = a[key];
    if (v === undefined) continue;
    if (typeof v !== 'string' || !/^#[0-9a-fA-F]{6}$/.test(v)) {
      errors.push(`atmosphere.${key} must be a colour like #8a9486`);
    }
  }
  if (typeof a.visionScale === 'number' && a.visionScale === 0) {
    errors.push('atmosphere.visionScale 0 would blind every player: leave it out for no vision fog');
  }
  if (typeof a.visionClearFraction === 'number' && a.visionClearFraction >= 1) {
    errors.push('atmosphere.visionClearFraction must be below 1 (the part of the vision range that stays clear)');
  }
  if (a.visionScale === undefined && (a.visionClearFraction !== undefined || a.relevancyMarginCm !== undefined)) {
    errors.push('atmosphere.visionClearFraction / relevancyMarginCm do nothing without visionScale');
  }
  return errors;
}


export type ValidationSeverity = 'error' | 'warning' | 'info';

export type ValidationCategory =
  | 'items' | 'skills' | 'classes' | 'npcs' | 'loot' | 'zones' | 'maps' | 'unreal';

export interface ValidationIssue {
  severity: ValidationSeverity;
  category: ValidationCategory;
  /** The record the problem is on (item id, skill id, file name, …). */
  id: string;
  message: string;
}

/** Unreal-side references, from `maps/unreal-refs.json` (export_unreal_refs.py). */
export interface UnrealRefs {
  generatedAt?: string;
  /** Levels whose actors were read; spawn points in other levels were not checked. */
  levels?: string[];
  npcTypes?: { asset: string; defaultTemplateId: string }[];
  spawnPoints?: {
    level: string;
    name: string;
    /** NPC Type blueprint asset name, or '' when unset. */
    npcType: string;
    /** Template Override, or ''. */
    templateOverride: string;
  }[];
}

export interface ValidationInput {
  items: Record<string, any>;
  skills: { skills: Record<string, any>; classSkills: Record<string, string[]> };
  classes: { classes: Record<string, any>; classColors?: Record<string, number> };
  npcTemplates: Record<string, any>;
  lootTables: Record<string, any>;
  zones: Record<string, any>;
  /** maps/overlays-2.0/<zone>.json by zone id (file name without .json). */
  overlays?: Record<string, any>;
  /** File names in Import/Characters/Equipment, e.g. "SK_chest_chainmail.glb". */
  meshFiles?: string[];
  /** File names in Import/UI/Icons, e.g. "sword_iron.png". */
  iconFiles?: string[];
  unrealRefs?: UnrealRefs | null;
}

export interface ValidationSummary {
  errors: number;
  warnings: number;
  infos: number;
}

// ── The vocabulary, kept here so every checker agrees ───────────────────

export const ITEM_CATEGORIES = ['equipment', 'consumable', 'quest', 'misc'] as const;
export const EQUIP_SLOT_IDS = ['weapon', 'offhand', 'helm', 'chest', 'legs', 'boots', 'gloves', 'back', 'ring'] as const;
export const RARITIES = ['common', 'uncommon', 'rare', 'epic', 'legendary'] as const;
export const WEAPON_STYLES = ['sword', 'dagger', 'greatsword', 'mace', 'bow', 'staff'] as const;
export const STAT_KEYS = [
  'hp', 'mana', 'strength', 'stamina', 'dexterity', 'intelligence', 'wisdom',
  'physicalResist', 'spellResist', 'critChance', 'critDamage', 'physicalDefense', 'blockRating', 'dodgeRating',
] as const;
export const SKILL_TARGET_TYPES = ['self', 'singleEnemy', 'singleAlly', 'aoeGround', 'aoeSelf', 'cone', 'passiveToggle'] as const;
export const SKILL_CATEGORIES = ['offensive', 'defensive', 'healing', 'buff', 'debuff', 'utility'] as const;
export const RESOURCE_TYPES = ['mana', 'energy', 'none'] as const;
export const NPC_TYPES = ['enemy', 'npc'] as const;
export const NPC_BEHAVIORS = ['passive', 'aggressive', 'patrol', 'stationary', 'fleeing'] as const;
export const ARMOR_TYPES = ['cloth', 'leather', 'mail', 'plate'] as const;
export const OVERLAY_POINT_TYPES = ['player_spawn', 'enemy_spawn', 'npc_spawn', 'portal', 'zone_entry'] as const;

/** Skills with a hand-written handler in Unreal (ValhallaSkillHandler.cpp); the data-only rules don't apply. */
export const CUSTOM_SKILL_HANDLERS = new Set([
  'wizard_fireball', 'wizard_magic_missile', 'warrior_taunt',
  'cleric_shield_of_faith', 'rogue_backstab', 'rogue_poison_blade',
]);

/** Slots that show a model; weapon/offhand are static meshes (SM_), the rest skinned (SK_). */
const STATIC_MESH_SLOTS = new Set(['weapon', 'offhand']);

const isObj = (v: unknown): v is Record<string, any> => typeof v === 'object' && v !== null && !Array.isArray(v);
const isNum = (v: unknown): v is number => typeof v === 'number' && Number.isFinite(v);
const has = (list: readonly string[], v: unknown) => typeof v === 'string' && list.includes(v);

/** Run every check. Issues come back sorted: errors, then warnings, then info. */
export function validateGameData(input: ValidationInput): ValidationIssue[] {
  const issues: ValidationIssue[] = [];
  const add = (severity: ValidationSeverity, category: ValidationCategory, id: string, message: string) =>
    issues.push({ severity, category, id, message });

  const items = isObj(input.items) ? input.items : {};
  const skills = isObj(input.skills?.skills) ? input.skills.skills : {};
  const classSkills = isObj(input.skills?.classSkills) ? input.skills.classSkills : {};
  const classes = isObj(input.classes?.classes) ? input.classes.classes : {};
  const npcs = isObj(input.npcTemplates) ? input.npcTemplates : {};
  const loot = isObj(input.lootTables) ? input.lootTables : {};
  const zones = isObj(input.zones) ? input.zones : {};

  const itemName = (id: string) => items[id]?.name ? `${items[id].name} (${id})` : id;

  const checkKey = (category: ValidationCategory, key: string, rec: any) => {
    if (!isObj(rec)) {
      add('error', category, key, 'is not an object');
      return false;
    }
    if (rec.id !== undefined && rec.id !== key) add('error', category, key, `id field "${rec.id}" does not match its key "${key}"`);
    return true;
  };

  const checkRange = (category: ValidationCategory, id: string, label: string, min: unknown, max: unknown) => {
    if (min === undefined && max === undefined) return;
    if (!isNum(min) || !isNum(max)) {
      add('error', category, id, `${label} needs both a min and a max number`);
      return;
    }
    if (min < 0 || max < 0) add('error', category, id, `${label} is negative (${min}–${max})`);
    if (min > max) add('error', category, id, `${label} min ${min} is greater than max ${max}`);
  };

  // ── Items ─────────────────────────────────────────────────────────────
  const meshIndex = input.meshFiles ? new Set(input.meshFiles.map(f => f.replace(/\.(glb|gltf)$/i, ''))) : null;
  const iconIndex = input.iconFiles ? new Set(input.iconFiles.map(f => f.toLowerCase())) : null;

  for (const [id, item] of Object.entries(items)) {
    if (!checkKey('items', id, item)) continue;
    if (!item.name) add('error', 'items', id, 'has no name');
    if (!has(ITEM_CATEGORIES, item.category)) add('error', 'items', id, `category "${item.category ?? ''}" is not one of ${ITEM_CATEGORIES.join(', ')}`);
    if (item.rarity !== undefined && !has(RARITIES, item.rarity)) add('warning', 'items', id, `rarity "${item.rarity}" is not one of ${RARITIES.join(', ')}`);

    const slot = item.equipSlot;
    if (item.category === 'equipment') {
      if (!slot) add('error', 'items', id, 'is equipment but has no equip slot');
      else if (!has(EQUIP_SLOT_IDS, slot)) add('error', 'items', id, `equip slot "${slot}" is not one of ${EQUIP_SLOT_IDS.join(', ')}`);
    } else if (slot) {
      add('warning', 'items', id, `has equip slot "${slot}" but its category is "${item.category}", so it can't be equipped`);
    }

    if (item.stackable && !(isNum(item.maxStack) && item.maxStack >= 1)) add('error', 'items', id, 'is stackable but max stack is not 1 or more');

    if (isObj(item.statBonuses)) {
      for (const [stat, value] of Object.entries(item.statBonuses)) {
        if (!has(STAT_KEYS, stat)) add('error', 'items', id, `stat bonus "${stat}" is not a stat`);
        else if (!isNum(value)) add('error', 'items', id, `stat bonus "${stat}" is not a number`);
      }
    }

    if (slot === 'weapon') {
      checkRange('items', id, 'damage', item.minDamage, item.maxDamage);
      if (item.minDamage === undefined && item.maxDamage === undefined && !isNum(item.attackDamage)) {
        add('warning', 'items', id, 'is a weapon with no damage range, so it hits like bare hands (1–3)');
      }
      if (item.attackSpeedMs !== undefined && !(isNum(item.attackSpeedMs) && item.attackSpeedMs > 0)) {
        add('error', 'items', id, 'attack speed must be a positive number of milliseconds');
      }
      if (item.weaponStyle !== undefined && !has(WEAPON_STYLES, item.weaponStyle)) {
        add('error', 'items', id, `weapon style "${item.weaponStyle}" is not one of ${WEAPON_STYLES.join(', ')}`);
      }
      if (item.weaponStyle === 'bow' && !item.isRangedWeapon) add('warning', 'items', id, 'weapon style is bow but "Ranged Weapon" is off');
      if (item.isRangedWeapon && item.weaponStyle !== 'bow') add('warning', 'items', id, '"Ranged Weapon" is on but the weapon style is not bow');
    } else if (item.minDamage !== undefined || item.maxDamage !== undefined) {
      add('warning', 'items', id, 'has a damage range but is not in the weapon slot, so it is ignored');
    }

    if (slot && slot !== 'ring' && has(EQUIP_SLOT_IDS, slot)) {
      const artId: string = item.meshId || item.spriteId || '';
      if (!artId) {
        add('warning', 'items', id, 'has no Mesh ID, so nothing is drawn when it is equipped');
      } else if (meshIndex) {
        const prefix = STATIC_MESH_SLOTS.has(slot) ? 'SM_' : 'SK_';
        if (!meshIndex.has(`${prefix}${artId}`)) {
          const other = prefix === 'SM_' ? 'SK_' : 'SM_';
          add('warning', 'items', id, meshIndex.has(`${other}${artId}`)
            ? `art "${artId}" exists as ${other}${artId} but a ${slot} item needs ${prefix}${artId}`
            : `art "${artId}" has no ${prefix}${artId}.glb in Import/Characters/Equipment`);
        }
      }
    }

    if (item.inventoryIcon && iconIndex && !iconIndex.has(String(item.inventoryIcon).toLowerCase())) {
      add('warning', 'items', id, `inventory icon "${item.inventoryIcon}" is not in Import/UI/Icons`);
    }
  }
  if (!input.meshFiles) add('info', 'items', 'meshes', 'Mesh files were not available, so Mesh IDs were not checked.');
  if (!input.iconFiles) add('info', 'items', 'icons', 'Icon files were not available, so inventory icons were not checked.');

  // ── Classes ───────────────────────────────────────────────────────────
  for (const [id, cls] of Object.entries(classes)) {
    if (!checkKey('classes', id, cls)) continue;
    if (!cls.name) add('error', 'classes', id, 'has no name');
    const hp = cls.baseStats?.hp;
    if (!(isNum(hp) && hp > 0)) add('error', 'classes', id, 'base HP must be more than 0');
    for (const k of ['critChance', 'blockRating', 'dodgeRating'] as const) {
      const v = cls.baseStats?.[k];
      if (isNum(v) && (v < 0 || v > 1)) add('warning', 'classes', id, `base ${k} ${v} is outside 0–1 (it is a fraction: 0.05 = 5%)`);
    }
    if (cls.allowedArmor !== undefined && !has(ARMOR_TYPES, cls.allowedArmor)) {
      add('warning', 'classes', id, `allowed armor "${cls.allowedArmor}" is not one of ${ARMOR_TYPES.join(', ')}`);
    }
    if (Array.isArray(cls.startingItems)) {
      for (const entry of cls.startingItems) {
        const itemId = entry?.itemId;
        if (!items[itemId]) add('error', 'classes', id, `starting item "${itemId}" does not exist`);
        else if (entry.equipped && !items[itemId].equipSlot) add('warning', 'classes', id, `starting item ${itemName(itemId)} is set to auto-equip but has no equip slot`);
        if (entry?.quantity !== undefined && !(Number.isInteger(entry.quantity) && entry.quantity >= 1)) {
          add('error', 'classes', id, `starting item "${itemId}" quantity must be a whole number of 1 or more`);
        }
      }
    }
  }

  // ── Skills ────────────────────────────────────────────────────────────
  for (const [id, skill] of Object.entries(skills)) {
    if (!checkKey('skills', id, skill)) continue;
    if (!skill.name) add('error', 'skills', id, 'has no name');
    if (skill.classId !== null && skill.classId !== undefined && !classes[skill.classId]) {
      add('error', 'skills', id, `class "${skill.classId}" does not exist`);
    }
    if (!has(SKILL_TARGET_TYPES, skill.targetType)) add('error', 'skills', id, `target type "${skill.targetType ?? ''}" is not one of ${SKILL_TARGET_TYPES.join(', ')}`);
    if (!has(SKILL_CATEGORIES, skill.category)) add('error', 'skills', id, `category "${skill.category ?? ''}" is not one of ${SKILL_CATEGORIES.join(', ')}`);
    if (skill.resourceType !== undefined && !has(RESOURCE_TYPES, skill.resourceType)) add('error', 'skills', id, `resource type "${skill.resourceType}" is not one of ${RESOURCE_TYPES.join(', ')}`);
    for (const k of ['resourceCost', 'cooldownMs', 'castTimeMs', 'range', 'levelRequired'] as const) {
      if (skill[k] !== undefined && !(isNum(skill[k]) && skill[k] >= 0)) add('error', 'skills', id, `${k} must be a number of 0 or more`);
    }
    for (const k of ['baseDamage', 'baseHealing'] as const) {
      const v = skill[k];
      if (v === undefined || v === null) continue;
      if (!Array.isArray(v) || v.length !== 2) add('error', 'skills', id, `${k} must be [min, max]`);
      else checkRange('skills', id, k, v[0], v[1]);
    }

    if (!CUSTOM_SKILL_HANDLERS.has(id) && !skill.isAutoAttack) {
      if (skill.targetType === 'aoeGround') {
        add('warning', 'skills', id, 'is ground-targeted (aoeGround), which does nothing without a programmer-written handler');
      }
      if (skill.targetType === 'passiveToggle') {
        add('warning', 'skills', id, 'is a passive toggle, which has no effect without a programmer-written handler');
      }
      const effect = skill.baseDamage || skill.baseHealing || skill.dotDamagePerSec || skill.hotHealPerSec;
      if (!effect && isNum(skill.buffDurationMs) && skill.buffDurationMs > 0 && skill.targetType !== 'aoeGround') {
        add('warning', 'skills', id, 'is a buff with only a duration: it shows an aura but changes no stats (stat buffs need a programmer)');
      }
      if (!effect && !(isNum(skill.buffDurationMs) && skill.buffDurationMs > 0) && skill.targetType !== 'aoeGround' && skill.targetType !== 'passiveToggle') {
        add('warning', 'skills', id, 'has no damage, healing or duration, so casting it does nothing');
      }
      if ((skill.dotDamagePerSec || skill.hotHealPerSec) && !(isNum(skill.buffDurationMs) && skill.buffDurationMs > 0)) {
        add('warning', 'skills', id, 'has a damage/heal per second but no Buff Duration, so the over-time part never runs');
      }
    }
  }

  // classSkills: the class → skill list the action bar is built from
  for (const [classId, list] of Object.entries(classSkills)) {
    if (!classes[classId]) add('error', 'skills', `classSkills.${classId}`, `class "${classId}" does not exist`);
    if (!Array.isArray(list)) {
      add('error', 'skills', `classSkills.${classId}`, 'is not a list');
      continue;
    }
    for (const skillId of list) {
      const skill = skills[skillId];
      if (!skill) {
        add('error', 'skills', `classSkills.${classId}`, `skill "${skillId}" does not exist`);
      } else if (skill.classId && skill.classId !== classId) {
        add('warning', 'skills', `classSkills.${classId}`, `lists ${skillId}, which belongs to class "${skill.classId}"`);
      }
    }
  }
  for (const [id, skill] of Object.entries(skills)) {
    if (isObj(skill) && skill.classId && classes[skill.classId]) {
      const list = classSkills[skill.classId];
      if (Array.isArray(list) && !list.includes(id)) {
        add('warning', 'skills', id, `belongs to ${skill.classId} but is not on that class's skill list (open it in the Skills editor and save)`);
      }
    }
  }

  // ── Loot tables ───────────────────────────────────────────────────────
  for (const [id, table] of Object.entries(loot)) {
    if (!checkKey('loot', id, table)) continue;
    if (!table.name) add('warning', 'loot', id, 'has no name');
    const entries = Array.isArray(table.entries) ? table.entries : [];
    if (!Array.isArray(table.entries)) add('error', 'loot', id, 'has no entries list');
    else if (entries.length === 0) add('warning', 'loot', id, 'is empty, so it never drops anything');
    entries.forEach((entry: any, i: number) => {
      const where = `entry ${i + 1}`;
      if (!items[entry?.itemId]) add('error', 'loot', id, `${where}: item "${entry?.itemId ?? ''}" does not exist`);
      if (!isNum(entry?.dropChance) || entry.dropChance < 0 || entry.dropChance > 1) {
        add('error', 'loot', id, `${where}: drop chance must be between 0 and 1 (0.25 = 25%)`);
      } else if (entry.dropChance === 0) {
        add('warning', 'loot', id, `${where}: drop chance is 0, so ${itemName(entry.itemId)} never drops`);
      }
      const min = entry?.minQuantity ?? 1;
      const max = entry?.maxQuantity ?? min;
      if (!Number.isInteger(min) || !Number.isInteger(max) || min < 1 || max < min) {
        add('error', 'loot', id, `${where}: quantity must be whole numbers with 1 ≤ min ≤ max`);
      }
    });
  }

  // ── NPC templates ─────────────────────────────────────────────────────
  for (const [id, npc] of Object.entries(npcs)) {
    if (!checkKey('npcs', id, npc)) continue;
    if (!npc.name) add('error', 'npcs', id, 'has no name');
    if (npc.type !== undefined && !has(NPC_TYPES, npc.type)) add('error', 'npcs', id, `type "${npc.type}" is not enemy or npc`);
    if (npc.behaviorType !== undefined && !has(NPC_BEHAVIORS, npc.behaviorType)) {
      add('error', 'npcs', id, `behavior "${npc.behaviorType}" is not one of ${NPC_BEHAVIORS.join(', ')}`);
    }
    if (!(isNum(npc.hp) && npc.hp > 0)) add('error', 'npcs', id, 'HP must be more than 0');
    checkRange('npcs', id, 'damage', npc.minDamage, npc.maxDamage);
    if (npc.lootTableId && !loot[npc.lootTableId]) add('error', 'npcs', id, `loot table "${npc.lootTableId}" does not exist`);
    if (npc.weaponId) {
      if (!items[npc.weaponId]) add('error', 'npcs', id, `weapon "${npc.weaponId}" does not exist`);
      else if (items[npc.weaponId].equipSlot !== 'weapon') add('error', 'npcs', id, `weapon ${itemName(npc.weaponId)} is not a weapon-slot item`);
    }
    if (Array.isArray(npc.skills)) {
      for (const s of npc.skills) {
        if (!skills[s]) add('error', 'npcs', id, `skill "${s}" does not exist`);
      }
      if (npc.skills.length > 0) add('warning', 'npcs', id, 'has skills, but NPCs do not cast skills yet');
    }
    if (Array.isArray(npc.vendorInventory)) {
      for (const v of npc.vendorInventory) {
        const itemId = typeof v === 'string' ? v : v?.itemId;
        if (!items[itemId]) add('error', 'npcs', id, `vendor item "${itemId}" does not exist`);
      }
    }
    if (npc.respawnMs !== undefined && !(isNum(npc.respawnMs) && npc.respawnMs >= 0)) add('error', 'npcs', id, 'respawn time must be 0 or more');
  }

  // ── Zones and overlays ────────────────────────────────────────────────
  for (const [id, zone] of Object.entries(zones)) {
    if (!checkKey('zones', id, zone)) continue;
    if (!zone.name) add('error', 'zones', id, 'has no name');
    if (!isObj(zone.defaultSpawn) || !isNum(zone.defaultSpawn.x) || !isNum(zone.defaultSpawn.y)) {
      add('error', 'zones', id, 'default spawn needs a numeric x and y');
    }
    // B-06: the optional per-zone atmosphere (same rules the loader and the Zone editor use).
    for (const problem of validateZoneAtmosphere(zone.atmosphere)) {
      // Ignored-by-the-game problems are warnings; values the game cannot use are errors.
      const mild = problem.includes('is not a known field') || problem.includes('is no longer used') || problem.includes('do nothing without');
      add(mild ? 'warning' : 'error', 'zones', id, problem);
    }
  }

  const overlays = isObj(input.overlays) ? input.overlays : null;
  if (!overlays) {
    add('info', 'maps', 'overlays', 'Zone overlays were not available, so portals and zone entries were not checked.');
  } else {
    const entriesByZone = new Map<string, Set<string>>();
    for (const [zoneId, doc] of Object.entries(overlays)) {
      const ids = new Set<string>();
      for (const sp of Array.isArray(doc?.spawnPoints) ? doc.spawnPoints : []) {
        if (sp?.type === 'zone_entry' && typeof sp.id === 'string') ids.add(sp.id);
      }
      entriesByZone.set(zoneId, ids);
    }
    for (const [zoneId, doc] of Object.entries(overlays)) {
      const file = `${zoneId}.json`;
      if (!zones[zoneId]) add('error', 'maps', file, `overlay for zone "${zoneId}", which is not in zones.json`);
      if (!isObj(doc)) { add('error', 'maps', file, 'is not a JSON object'); continue; }
      if (doc.zoneId !== undefined && doc.zoneId !== zoneId) add('error', 'maps', file, `zoneId "${doc.zoneId}" does not match the file name`);
      const seen = new Set<string>();
      const points = Array.isArray(doc.spawnPoints) ? doc.spawnPoints : [];
      points.forEach((sp: any, i: number) => {
        const where = sp?.id ? `"${sp.id}"` : `point ${i + 1}`;
        if (typeof sp?.id !== 'string' || !sp.id) add('error', 'maps', file, `point ${i + 1} has no id`);
        else if (seen.has(sp.id)) add('error', 'maps', file, `id ${where} is used twice`);
        else seen.add(sp.id);
        if (!has(OVERLAY_POINT_TYPES, sp?.type)) add('error', 'maps', file, `${where}: type "${sp?.type ?? ''}" is not one of ${OVERLAY_POINT_TYPES.join(', ')}`);
        if (sp?.type === 'portal') {
          if (!zones[sp.targetZone]) {
            add('error', 'maps', file, `portal ${where} leads to zone "${sp.targetZone ?? ''}", which does not exist`);
          } else if (sp.targetEntry) {
            const targetEntries = entriesByZone.get(sp.targetZone);
            if (!targetEntries) add('warning', 'maps', file, `portal ${where} leads to "${sp.targetZone}", which has no overlay file to check entry "${sp.targetEntry}" against`);
            else if (!targetEntries.has(sp.targetEntry)) add('error', 'maps', file, `portal ${where} leads to entry "${sp.targetEntry}", which is not a zone_entry in ${sp.targetZone}.json`);
          }
        }
        if (sp?.type === 'zone_entry' && sp.fromZone && !zones[sp.fromZone]) {
          add('error', 'maps', file, `zone entry ${where} comes from zone "${sp.fromZone}", which does not exist`);
        }
        if (sp?.type === 'enemy_spawn' || sp?.type === 'npc_spawn') {
          if (sp.templateId && !npcs[sp.templateId]) add('error', 'maps', file, `${where} names NPC template "${sp.templateId}", which does not exist`);
          add('warning', 'maps', file, `${where} is an ${sp.type}, which the game ignores: place an NPC Spawn Point in Unreal instead`);
        }
      });
    }
  }

  // ── Unreal: NPC Types and spawn points ────────────────────────────────
  const refs = input.unrealRefs;
  if (!refs) {
    add('info', 'unreal', 'unreal-refs', 'NPC Types and spawn points were not checked: run valhalla_tools/export_unreal_refs.py in Unreal to write maps/unreal-refs.json.');
  } else {
    const typeTemplates = new Map<string, string>();
    for (const t of refs.npcTypes ?? []) {
      typeTemplates.set(t.asset, t.defaultTemplateId || '');
      if (!t.defaultTemplateId) add('warning', 'unreal', t.asset, 'NPC Type has no Default Template Id');
      else if (!npcs[t.defaultTemplateId]) add('error', 'unreal', t.asset, `NPC Type's default template "${t.defaultTemplateId}" does not exist`);
    }
    for (const sp of refs.spawnPoints ?? []) {
      const where = `${sp.level}/${sp.name}`;
      if (!sp.npcType) {
        add('error', 'unreal', where, 'spawn point has no NPC Type');
        continue;
      }
      if (sp.templateOverride) {
        if (!npcs[sp.templateOverride]) add('error', 'unreal', where, `Template Override "${sp.templateOverride}" does not exist`);
      } else if (typeTemplates.has(sp.npcType)) {
        const template = typeTemplates.get(sp.npcType);
        if (template && !npcs[template]) add('error', 'unreal', where, `uses ${sp.npcType}, whose template "${template}" does not exist`);
      } else {
        add('warning', 'unreal', where, `NPC Type "${sp.npcType}" was not found among the exported NPC Types`);
      }
    }
    if (refs.generatedAt) {
      add('info', 'unreal', 'unreal-refs', `Unreal references exported ${refs.generatedAt}${refs.levels?.length ? ` from ${refs.levels.join(', ')}` : ''}. Re-export after changing NPC Types or spawn points.`);
    }
  }

  const rank: Record<ValidationSeverity, number> = { error: 0, warning: 1, info: 2 };
  return issues.sort((a, b) => rank[a.severity] - rank[b.severity]);
}

export function summarizeIssues(issues: ValidationIssue[]): ValidationSummary {
  return {
    errors: issues.filter(i => i.severity === 'error').length,
    warnings: issues.filter(i => i.severity === 'warning').length,
    infos: issues.filter(i => i.severity === 'info').length,
  };
}
