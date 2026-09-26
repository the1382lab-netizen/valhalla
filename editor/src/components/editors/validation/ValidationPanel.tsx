/**
 * B-13: the Validation page. Runs the shared cross-reference check
 * (shared/src/validation.ts — the same rules as `npm run validate` and the git
 * pre-commit hook) over the editor's in-memory data, so unsaved edits are
 * checked too, with the on-disk context (mesh files, icons and the exported
 * Unreal references) fetched from the editor server.
 */
import React, { useCallback, useEffect, useMemo, useState } from 'react';
import {
  summarizeIssues,
  validateGameData,
  type UnrealRefs,
  type ValidationCategory,
  type ValidationIssue,
  type ValidationSeverity,
} from '@valhalla/shared';
import { useEditorStore } from '../../../store/editorStore';

interface ValidationContext {
  meshFiles: string[] | null;
  iconFiles: string[] | null;
  classIconFiles?: string[] | null;
  unrealRefs: UnrealRefs | null;
  problems: { file: string; message: string }[];
}

const CATEGORY_LABELS: Record<ValidationCategory, string> = {
  items: 'Items', skills: 'Skills', classes: 'Classes', npcs: 'NPCs',
  loot: 'Loot tables', zones: 'Zones', unreal: 'Unreal (NPC Types, spawn points, zone actors)',
};

const SEVERITY_STYLE: Record<ValidationSeverity, { color: string; bg: string; label: string }> = {
  error: { color: '#ff4444', bg: 'rgba(255, 68, 68, 0.1)', label: 'error' },
  warning: { color: '#ffdd88', bg: 'rgba(255, 221, 136, 0.1)', label: 'warning' },
  info: { color: '#88aaff', bg: 'rgba(136, 170, 255, 0.08)', label: 'note' },
};

/** Where to jump for an issue's record, when the editor has a page for it. */
const SECTION_FOR: Partial<Record<ValidationCategory, 'items' | 'skills' | 'classes' | 'npcs' | 'loot'>> = {
  items: 'items', skills: 'skills', classes: 'classes', npcs: 'npcs', loot: 'loot',
};

export const ValidationPanel: React.FC = () => {
  const items = useEditorStore(s => s.items);
  const skills = useEditorStore(s => s.skills);
  const classes = useEditorStore(s => s.classes);
  const npcTemplates = useEditorStore(s => s.npcTemplates);
  const lootTables = useEditorStore(s => s.lootTables);
  const zones = useEditorStore(s => s.zones);
  const setActiveSection = useEditorStore(s => s.setActiveSection);
  const setSelectedItemId = useEditorStore(s => s.setSelectedItemId);
  const setSelectedSkillId = useEditorStore(s => s.setSelectedSkillId);
  const setSelectedClassId = useEditorStore(s => s.setSelectedClassId);
  const setSelectedNpcId = useEditorStore(s => s.setSelectedNpcId);
  const setSelectedLootTableId = useEditorStore(s => s.setSelectedLootTableId);

  const [context, setContext] = useState<ValidationContext | null>(null);
  const [contextError, setContextError] = useState<string | null>(null);
  const [category, setCategory] = useState<ValidationCategory | null>(null);
  const [showNotes, setShowNotes] = useState(false);

  const loadContext = useCallback(async () => {
    try {
      const res = await fetch('/api/validate/context');
      const json = await res.json();
      if (!res.ok) throw new Error(json?.error || `HTTP ${res.status}`);
      setContext(json);
      setContextError(null);
    } catch (err: any) {
      setContext(null);
      setContextError(err?.message || 'request failed');
    }
  }, []);
  useEffect(() => { loadContext(); }, [loadContext]);

  const issues: ValidationIssue[] = useMemo(() => {
    const found = validateGameData({
      items: items.data,
      skills: skills.data,
      classes: classes.data,
      npcTemplates: npcTemplates.data,
      lootTables: lootTables.data,
      zones: zones.data,
      meshFiles: context?.meshFiles ?? undefined,
      iconFiles: context?.iconFiles ?? undefined,
      classIconFiles: context?.classIconFiles ?? undefined,
      unrealRefs: context?.unrealRefs ?? null,
    });
    const fileProblems: ValidationIssue[] = (context?.problems ?? []).map(p => ({
      severity: 'error', category: p.file.startsWith('maps/') ? 'unreal' : 'items', id: p.file, message: p.message,
    }));
    return [...fileProblems, ...found];
  }, [items.data, skills.data, classes.data, npcTemplates.data, lootTables.data, zones.data, context]);

  const summary = summarizeIssues(issues);
  const unsaved = [items, skills, classes, npcTemplates, lootTables, zones].some(s => s.isDirty);
  const visible = issues.filter(i => (showNotes || i.severity !== 'info') && (!category || i.category === category));
  const counts = useMemo(() => {
    const c: Partial<Record<ValidationCategory, number>> = {};
    for (const i of issues) if (showNotes || i.severity !== 'info') c[i.category] = (c[i.category] ?? 0) + 1;
    return c;
  }, [issues, showNotes]);

  const open = (issue: ValidationIssue) => {
    const section = SECTION_FOR[issue.category];
    if (!section) return;
    if (section === 'items' && items.data[issue.id]) setSelectedItemId(issue.id);
    if (section === 'skills' && skills.data.skills[issue.id]) setSelectedSkillId(issue.id);
    if (section === 'classes' && classes.data.classes[issue.id]) setSelectedClassId(issue.id);
    if (section === 'npcs' && npcTemplates.data[issue.id]) setSelectedNpcId(issue.id);
    if (section === 'loot' && lootTables.data[issue.id]) setSelectedLootTableId(issue.id);
    setActiveSection(section);
  };

  return (
    <div className="panel">
      <div className="panel-header">
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 12 }}>
          <span>Validation</span>
          <button className="btn btn-ghost" onClick={loadContext} title="Re-read meshes, icons and Unreal references from disk">
            ⟳ Refresh files
          </button>
        </div>
        <div style={{ fontSize: 12, color: 'var(--text-muted)', marginBottom: 10 }}>
          The same checks as <code>npm run validate</code> and the git pre-commit hook, run on what's in the editor
          {unsaved ? ', including unsaved changes' : ''}. Errors mean the game will misbehave; warnings mean it works but
          probably not as intended. Click an issue to open it.
        </div>
        {contextError && (
          <div style={{ color: '#e66', fontSize: 12, marginBottom: 8 }}>
            Could not read the files on disk ({contextError}); meshes, icons and Unreal references are not checked.
          </div>
        )}
        <div style={{ display: 'flex', gap: 16, padding: 12, background: 'rgba(0,0,0,0.2)', borderRadius: 4, marginBottom: 12 }}>
          <div>
            <div className="form-label" style={{ marginBottom: 4, color: SEVERITY_STYLE.error.color }}>Errors</div>
            <div style={{ fontSize: 20, fontWeight: 'bold', color: SEVERITY_STYLE.error.color }}>{summary.errors}</div>
          </div>
          <div>
            <div className="form-label" style={{ marginBottom: 4, color: SEVERITY_STYLE.warning.color }}>Warnings</div>
            <div style={{ fontSize: 20, fontWeight: 'bold', color: SEVERITY_STYLE.warning.color }}>{summary.warnings}</div>
          </div>
          <label style={{ marginLeft: 'auto', alignSelf: 'center', fontSize: 12, color: 'var(--text-secondary)', display: 'flex', gap: 6, alignItems: 'center' }}>
            <input type="checkbox" checked={showNotes} onChange={e => setShowNotes(e.target.checked)} />
            show notes ({summary.infos})
          </label>
        </div>
      </div>

      <div className="panel-body">
        {summary.errors === 0 && summary.warnings === 0 && !showNotes ? (
          <div className="empty-state">
            <div className="empty-state-icon">✅</div>
            <p>No problems found.</p>
          </div>
        ) : (
          <>
            <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap', marginBottom: 16 }}>
              <button className={`btn ${!category ? 'btn-primary' : 'btn-ghost'}`} style={{ fontSize: 12 }} onClick={() => setCategory(null)}>
                All ({Object.values(counts).reduce((a, b) => a + (b ?? 0), 0)})
              </button>
              {(Object.keys(CATEGORY_LABELS) as ValidationCategory[]).filter(c => counts[c]).map(c => (
                <button key={c} className={`btn ${category === c ? 'btn-primary' : 'btn-ghost'}`} style={{ fontSize: 12 }} onClick={() => setCategory(c)}>
                  {CATEGORY_LABELS[c]} ({counts[c]})
                </button>
              ))}
            </div>

            <div style={{ overflowY: 'auto', maxHeight: 640 }}>
              {visible.length === 0 ? (
                <div style={{ padding: 16, textAlign: 'center', color: 'var(--text-muted)' }}>No issues in this category</div>
              ) : visible.map((issue, idx) => {
                const style = SEVERITY_STYLE[issue.severity];
                const clickable = !!SECTION_FOR[issue.category];
                return (
                  <div key={idx} onClick={() => clickable && open(issue)}
                    style={{
                      padding: 10, marginBottom: 6, background: style.bg, borderRadius: 4,
                      border: `1px solid ${style.color}`, borderLeft: `4px solid ${style.color}`,
                      cursor: clickable ? 'pointer' : 'default',
                    }}>
                    <div style={{ display: 'flex', gap: 8, alignItems: 'center', marginBottom: 4 }}>
                      <span style={{ background: style.color, color: '#000', fontSize: 10, padding: '1px 6px', borderRadius: 3, textTransform: 'uppercase', fontWeight: 'bold' }}>
                        {style.label}
                      </span>
                      <span style={{ background: 'rgba(255,255,255,0.1)', fontSize: 10, padding: '1px 6px', borderRadius: 3 }}>
                        {CATEGORY_LABELS[issue.category]}
                      </span>
                      <code style={{ fontSize: 11, color: '#aaa' }}>{issue.id}</code>
                    </div>
                    <div style={{ fontSize: 13, lineHeight: 1.5 }}>{issue.message}</div>
                  </div>
                );
              })}
            </div>
          </>
        )}
      </div>
    </div>
  );
};
