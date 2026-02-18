/**
 * One-time export script: reads hardcoded TypeScript catalogs and writes JSON data files.
 * Run with: npx tsx shared/scripts/export-to-json.ts
 */
import * as fs from 'fs';
import * as path from 'path';
import { fileURLToPath } from 'url';
import { ITEM_CATALOG } from '../src/items.js';
import { SKILL_CATALOG, CLASS_SKILLS } from '../src/skills.js';
import { CLASS_TEMPLATES, CLASS_COLORS } from '../src/classes.js';
import { ZONE_REGISTRY } from '../src/maps.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const DATA_DIR = path.resolve(__dirname, '../data');

function writeJson(filename: string, data: any) {
  const filePath = path.join(DATA_DIR, filename);
  fs.writeFileSync(filePath, JSON.stringify(data, null, 2) + '\n');
  console.log(`  ✓ ${filename} (${Object.keys(data).length - 1} entries)`);
}

console.log('Exporting game data to JSON...\n');

// Items
writeJson('items.json', {
  version: '1.0.0',
  items: ITEM_CATALOG,
});

// Skills + class-skill mappings
writeJson('skills.json', {
  version: '1.0.0',
  skills: SKILL_CATALOG,
  classSkills: CLASS_SKILLS,
});

// Classes
writeJson('classes.json', {
  version: '1.0.0',
  classes: CLASS_TEMPLATES,
  classColors: CLASS_COLORS,
});

// Zones
writeJson('zones.json', {
  version: '1.0.0',
  zones: ZONE_REGISTRY,
});

// NPC Templates (empty initial)
writeJson('npc-templates.json', {
  version: '1.0.0',
  templates: {},
});

// Loot Tables (empty initial)
writeJson('loot-tables.json', {
  version: '1.0.0',
  tables: {},
});

console.log('\nDone! JSON data files are in shared/data/');
