/**
 * JSON data file loaders with validation.
 * Used by both game client (via fetch) and game server (via fs.readFileSync)
 * to load content from JSON data files exported by the editor.
 */
/**
 * Parse and validate items JSON data.
 * Returns the items record or null if invalid.
 */
export function loadItemsFromJson(json) {
    try {
        const data = json;
        if (!data || typeof data !== 'object' || !data.items) {
            console.warn('[Loader] Invalid items JSON: missing "items" field');
            return null;
        }
        return data.items;
    }
    catch (e) {
        console.warn('[Loader] Failed to parse items JSON:', e);
        return null;
    }
}
/**
 * Parse and validate skills JSON data.
 * Returns an object with skills and classSkills, or null if invalid.
 */
export function loadSkillsFromJson(json) {
    try {
        const data = json;
        if (!data || typeof data !== 'object' || !data.skills || !data.classSkills) {
            console.warn('[Loader] Invalid skills JSON: missing "skills" or "classSkills" field');
            return null;
        }
        return { skills: data.skills, classSkills: data.classSkills };
    }
    catch (e) {
        console.warn('[Loader] Failed to parse skills JSON:', e);
        return null;
    }
}
/**
 * Parse and validate classes JSON data.
 * Returns the classes record or null if invalid.
 */
export function loadClassesFromJson(json) {
    try {
        const data = json;
        if (!data || typeof data !== 'object' || !data.classes) {
            console.warn('[Loader] Invalid classes JSON: missing "classes" field');
            return null;
        }
        return { classes: data.classes, classColors: data.classColors || {} };
    }
    catch (e) {
        console.warn('[Loader] Failed to parse classes JSON:', e);
        return null;
    }
}
/**
 * Parse and validate zones JSON data.
 */
export function loadZonesFromJson(json) {
    try {
        const data = json;
        if (!data || typeof data !== 'object' || !data.zones) {
            console.warn('[Loader] Invalid zones JSON');
            return null;
        }
        return data.zones;
    }
    catch (e) {
        console.warn('[Loader] Failed to parse zones JSON:', e);
        return null;
    }
}
/**
 * Parse and validate NPC templates JSON data.
 */
export function loadNPCTemplatesFromJson(json) {
    try {
        const data = json;
        if (!data || typeof data !== 'object' || !data.templates) {
            console.warn('[Loader] Invalid NPC templates JSON');
            return null;
        }
        return data.templates;
    }
    catch (e) {
        console.warn('[Loader] Failed to parse NPC templates JSON:', e);
        return null;
    }
}
/**
 * Parse and validate loot tables JSON data.
 */
export function loadLootTablesFromJson(json) {
    try {
        const data = json;
        if (!data || typeof data !== 'object' || !data.tables) {
            console.warn('[Loader] Invalid loot tables JSON');
            return null;
        }
        return data.tables;
    }
    catch (e) {
        console.warn('[Loader] Failed to parse loot tables JSON:', e);
        return null;
    }
}
/**
 * Parse UI config from JSON.
 * Returns the config or null if invalid.
 */
export function loadUIConfigFromJson(json) {
    try {
        const data = json;
        if (!data || typeof data !== 'object' || !data.version) {
            console.warn('[Loader] Invalid UI config JSON');
            return null;
        }
        return data;
    }
    catch (e) {
        console.warn('[Loader] Failed to parse UI config JSON:', e);
        return null;
    }
}
//# sourceMappingURL=loaders.js.map