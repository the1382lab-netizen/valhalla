/**
 * Node-only half of B-13: read everything validateGameData needs from a repo
 * checkout. Not exported from index.ts (the web editor's browser bundle must
 * not pull in `fs`); import it by path from Node code.
 */
import { existsSync, readdirSync, readFileSync } from 'fs';
import { join } from 'path';
import {
  loadClassesFromJson,
  loadItemsFromJson,
  loadLootTablesFromJson,
  loadNPCTemplatesFromJson,
  loadSkillsFromJson,
  loadZonesFromJson,
} from './loaders.js';
import type { UnrealRefs, ValidationInput } from './validation.js';

/** A JSON file failed to read or parse; reported as an error by the caller. */
export interface LoadProblem {
  file: string;
  message: string;
}

function readJson(path: string, problems: LoadProblem[], label: string): unknown {
  if (!existsSync(path)) {
    problems.push({ file: label, message: 'file is missing' });
    return null;
  }
  try {
    return JSON.parse(readFileSync(path, 'utf8'));
  } catch (err: any) {
    problems.push({ file: label, message: `is not valid JSON: ${err?.message ?? err}` });
    return null;
  }
}

function listDir(dir: string, pattern: RegExp): string[] | undefined {
  if (!existsSync(dir)) return undefined;
  return readdirSync(dir).filter(f => pattern.test(f));
}

/**
 * Everything under `repoRoot`: shared/data/*.json, Import/Characters/Equipment and
 * Import/Characters/MetaHuman/Equipment,
 * Import/UI/Icons and maps/unreal-refs.json.
 */
export function loadValidationInput(repoRoot: string): { input: ValidationInput; problems: LoadProblem[] } {
  const problems: LoadProblem[] = [];
  const data = join(repoRoot, 'shared', 'data');
  const need = <T>(file: string, loader: (json: unknown) => T | null, fallback: T): T => {
    const json = readJson(join(data, file), problems, `shared/data/${file}`);
    if (json === null) return fallback;
    const parsed = loader(json);
    if (parsed === null) {
      problems.push({ file: `shared/data/${file}`, message: 'does not have the expected shape' });
      return fallback;
    }
    return parsed;
  };

  let unrealRefs: UnrealRefs | null = null;
  const refsPath = join(repoRoot, 'maps', 'unreal-refs.json');
  if (existsSync(refsPath)) {
    unrealRefs = readJson(refsPath, problems, 'maps/unreal-refs.json') as UnrealRefs | null;
  }

  const input: ValidationInput = {
    items: need('items.json', loadItemsFromJson, {}),
    skills: need('skills.json', loadSkillsFromJson, { skills: {}, classSkills: {} }),
    classes: need('classes.json', loadClassesFromJson, { classes: {}, classColors: {} }),
    npcTemplates: need('npc-templates.json', loadNPCTemplatesFromJson, {}),
    lootTables: need('loot-tables.json', loadLootTablesFromJson, {}),
    zones: need('zones.json', loadZonesFromJson, {}),
    // The MetaHuman is the only body the game uses (Kevin, 2026-09-25): new armour
    // exists only as Import/Characters/MetaHuman/Equipment/*.fbx, so both folders count.
    meshFiles: [
      ...(listDir(join(repoRoot, 'Import', 'Characters', 'Equipment'), /\.(glb|gltf)$/i) ?? []),
      ...(listDir(join(repoRoot, 'Import', 'Characters', 'MetaHuman', 'Equipment'), /\.fbx$/i) ?? []),
    ],
    iconFiles: listDir(join(repoRoot, 'Import', 'UI', 'Icons'), /\.png$/i),
    unrealRefs,
  };
  return { input, problems };
}
