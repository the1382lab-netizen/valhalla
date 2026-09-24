import { create } from 'zustand';

export type EditorSection =
  | 'items' | 'skills' | 'classes'
  | 'npcs' | 'loot'
  | 'zones' | 'ui'
  | 'maps'
  | 'balance' | 'validation'
  | 'admin';

interface DataSection<T> {
  data: T;
  isDirty: boolean;
  lastSaved: number | null;
}

/** B-13: the result of checking the saved files after a Save (shown in the status bar). */
export interface SavedValidation {
  errors: number;
  warnings: number;
  checkedAt: number;
  /** Set when the check itself could not run. */
  error?: string;
}

interface EditorState {
  // Navigation
  activeSection: EditorSection;
  setActiveSection: (section: EditorSection) => void;

  // Data sections
  items: DataSection<Record<string, any>>;
  skills: DataSection<{ skills: Record<string, any>; classSkills: Record<string, string[]> }>;
  classes: DataSection<{ classes: Record<string, any>; classColors: Record<string, number> }>;
  zones: DataSection<Record<string, any>>;
  npcTemplates: DataSection<Record<string, any>>;
  lootTables: DataSection<Record<string, any>>;
  uiConfig: DataSection<any>;

  /** B-13: the last check of the saved files, run after every Save. */
  savedValidation: SavedValidation | null;
  validateSaved: () => Promise<void>;

  // Actions
  loadAll: () => Promise<void>;
  saveSection: (section: string) => Promise<void>;
  saveAll: () => Promise<void>;
  updateData: (section: string, data: any) => void;
  markDirty: (section: string) => void;
  importAll: (data: any) => void;

  // Selection state (used by individual editors)
  selectedItemId: string | null;
  setSelectedItemId: (id: string | null) => void;
  selectedSkillId: string | null;
  setSelectedSkillId: (id: string | null) => void;
  selectedClassId: string | null;
  setSelectedClassId: (id: string | null) => void;
  selectedNpcId: string | null;
  setSelectedNpcId: (id: string | null) => void;
  selectedLootTableId: string | null;
  setSelectedLootTableId: (id: string | null) => void;
  selectedZoneId: string | null;
  setSelectedZoneId: (id: string | null) => void;
}

const emptyDataSection = <T>(data: T): DataSection<T> => ({
  data,
  isDirty: false,
  lastSaved: null,
});

async function fetchJson(url: string) {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`Failed to fetch ${url}: ${res.status}`);
  return res.json();
}

async function putJson(url: string, data: any) {
  const res = await fetch(url, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(data),
  });
  if (!res.ok) throw new Error(`Failed to save ${url}: ${res.status}`);
  return res.json();
}

/** Set while saveAll runs, so its sections are checked once at the end rather than after each file. */
let batchingSaves = false;

export const useEditorStore = create<EditorState>((set, get) => ({
  savedValidation: null,
  validateSaved: async () => {
    try {
      const res = await fetch('/api/validate');
      const json = await res.json();
      if (!res.ok) throw new Error(json?.error || `HTTP ${res.status}`);
      set({ savedValidation: { errors: json.summary.errors, warnings: json.summary.warnings, checkedAt: Date.now() } });
    } catch (err: any) {
      set({ savedValidation: { errors: 0, warnings: 0, checkedAt: Date.now(), error: err?.message || 'request failed' } });
    }
  },

  activeSection: 'items',
  setActiveSection: (section) => set({ activeSection: section }),

  items: emptyDataSection({}),
  skills: emptyDataSection({ skills: {}, classSkills: {} }),
  classes: emptyDataSection({ classes: {}, classColors: {} }),
  zones: emptyDataSection({}),
  npcTemplates: emptyDataSection({}),
  lootTables: emptyDataSection({}),
  uiConfig: emptyDataSection(null),

  selectedItemId: null,
  setSelectedItemId: (id) => set({ selectedItemId: id }),
  selectedSkillId: null,
  setSelectedSkillId: (id) => set({ selectedSkillId: id }),
  selectedClassId: null,
  setSelectedClassId: (id) => set({ selectedClassId: id }),
  selectedNpcId: null,
  setSelectedNpcId: (id) => set({ selectedNpcId: id }),
  selectedLootTableId: null,
  setSelectedLootTableId: (id) => set({ selectedLootTableId: id }),
  selectedZoneId: null,
  setSelectedZoneId: (id) => set({ selectedZoneId: id }),

  loadAll: async () => {
    try {
      const [itemsData, skillsData, classesData, zonesData, npcData, lootData, uiData] =
        await Promise.all([
          fetchJson('/api/data/items.json'),
          fetchJson('/api/data/skills.json'),
          fetchJson('/api/data/classes.json'),
          fetchJson('/api/data/zones.json'),
          fetchJson('/api/data/npc-templates.json'),
          fetchJson('/api/data/loot-tables.json'),
          fetchJson('/api/data/ui-config.json'),
        ]);

      const now = Date.now();
      set({
        items: { data: itemsData.items || {}, isDirty: false, lastSaved: now },
        skills: {
          data: {
            skills: skillsData.skills || {},
            classSkills: skillsData.classSkills || {},
          },
          isDirty: false,
          lastSaved: now,
        },
        classes: {
          data: {
            classes: classesData.classes || {},
            classColors: classesData.classColors || {},
          },
          isDirty: false,
          lastSaved: now,
        },
        zones: { data: zonesData.zones || {}, isDirty: false, lastSaved: now },
        npcTemplates: { data: npcData.templates || {}, isDirty: false, lastSaved: now },
        lootTables: { data: lootData.tables || {}, isDirty: false, lastSaved: now },
        uiConfig: { data: uiData, isDirty: false, lastSaved: now },
      });
    } catch (err) {
      console.error('Failed to load editor data:', err);
    }
  },

  updateData: (section, data) => {
    set((state) => ({
      [section]: { ...state[section as keyof EditorState] as DataSection<any>, data, isDirty: true },
    } as any));
  },

  markDirty: (section) => {
    set((state) => ({
      [section]: { ...state[section as keyof EditorState] as DataSection<any>, isDirty: true },
    } as any));
  },

  saveSection: async (section) => {
    const state = get();
    const fileMap: Record<string, { file: string; wrap: (d: any) => any }> = {
      items: { file: 'items.json', wrap: (d) => ({ version: '1.0.0', items: d }) },
      skills: { file: 'skills.json', wrap: (d) => ({ version: '1.0.0', ...d }) },
      classes: { file: 'classes.json', wrap: (d) => ({ version: '1.0.0', ...d }) },
      zones: { file: 'zones.json', wrap: (d) => ({ version: '1.0.0', zones: d }) },
      npcTemplates: { file: 'npc-templates.json', wrap: (d) => ({ version: '1.0.0', templates: d }) },
      lootTables: { file: 'loot-tables.json', wrap: (d) => ({ version: '1.0.0', tables: d }) },
      uiConfig: { file: 'ui-config.json', wrap: (d) => d },
    };

    const mapping = fileMap[section];
    if (!mapping) return;

    const sectionData = (state[section as keyof EditorState] as DataSection<any>).data;
    await putJson(`/api/data/${mapping.file}`, mapping.wrap(sectionData));
    set((s) => ({
      [section]: { ...s[section as keyof EditorState] as DataSection<any>, isDirty: false, lastSaved: Date.now() },
    } as any));
    // B-13: every Save re-checks the files on disk (no need to await it).
    if (!batchingSaves) void get().validateSaved();
  },

  saveAll: async () => {
    const state = get();
    const sections = ['items', 'skills', 'classes', 'zones', 'npcTemplates', 'lootTables', 'uiConfig'];
    batchingSaves = true;
    try {
      for (const section of sections) {
        if ((state[section as keyof EditorState] as DataSection<any>).isDirty) {
          await state.saveSection(section);
        }
      }
    } finally {
      batchingSaves = false;
    }
    void get().validateSaved();
  },

  importAll: (data) => {
    const now = Date.now();
    set({
      items: { data: data.items || {}, isDirty: true, lastSaved: now },
      skills: { data: data.skills || { skills: {}, classSkills: {} }, isDirty: true, lastSaved: now },
      classes: { data: data.classes || { classes: {}, classColors: {} }, isDirty: true, lastSaved: now },
      zones: { data: data.zones || {}, isDirty: true, lastSaved: now },
      npcTemplates: { data: data.npcTemplates || {}, isDirty: true, lastSaved: now },
      lootTables: { data: data.lootTables || {}, isDirty: true, lastSaved: now },
      uiConfig: { data: data.uiConfig || null, isDirty: true, lastSaved: now },
    });
  },
}));
