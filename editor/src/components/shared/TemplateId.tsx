/**
 * Template ids in the web editor: the ID field at the top of a details panel
 * (copyable, and renamable when nothing references it) and the "New / Copy"
 * prompt that asks for a name and derives the id from it.
 *
 * Ids are the JSON map keys (npc-templates.json, items.json, skills.json,
 * loot-tables.json) that other data and Unreal Blueprints point at, so they are
 * lowercase snake_case like `iron_dagger` or `merchant_bjorn`.
 */
import React, { useEffect, useState } from 'react';
import { overlayStyle, panelStyle, titleStyle } from '../editors/admin/AdminPlayerTools';

export const ID_PATTERN = /^[a-z][a-z0-9_]*$/;

const monoStyle: React.CSSProperties = { fontFamily: 'Consolas, Menlo, monospace' };
const hintStyle: React.CSSProperties = { fontSize: 12, color: 'var(--text-muted)', marginTop: 4 };
const errorStyle: React.CSSProperties = { fontSize: 12, color: 'var(--danger)', marginTop: 4 };

/** "Orc Grunt (copy)" -> "orc_grunt_copy". */
export function slugifyId(name: string): string {
  return name.toLowerCase().trim().replace(/\s+/g, '_').replace(/[^a-z0-9_]/g, '')
    .replace(/_+/g, '_').replace(/^_|_$/g, '');
}

/** Why `id` can't be used, or null. `currentId` is the entry's own id (ignored as a clash). */
export function idError(id: string, taken: string[], currentId?: string): string | null {
  if (!id) return 'Enter an id';
  if (!ID_PATTERN.test(id)) return 'Lowercase letters, digits and _ only, starting with a letter';
  const lower = id.toLowerCase();
  if (taken.some(t => t !== currentId && t.toLowerCase() === lower)) return `"${id}" is already used`;
  return null;
}

/** `base`, or `base_2`, `base_3`... whichever is free. */
function freeId(base: string, taken: string[]): string {
  const used = new Set(taken.map(t => t.toLowerCase()));
  if (!used.has(base)) return base;
  let n = 2;
  while (used.has(`${base}_${n}`)) n++;
  return `${base}_${n}`;
}

/** Re-key `map` from oldId to newId in place (same position), keeping the object's `id` in sync. */
export function renameKey<T extends object>(map: Record<string, T>, oldId: string, newId: string): Record<string, T> {
  return Object.fromEntries(Object.entries(map).map(([k, v]) =>
    k === oldId ? [newId, { ...v, id: newId }] : [k, v]));
}

/** "a, b, c and 4 more" */
export function listRefs(refs: string[], max = 4): string {
  return refs.length <= max ? refs.join(', ') : `${refs.slice(0, max).join(', ')} and ${refs.length - max} more`;
}

/** The "New" / "Copy" prompt: a name, and the id derived from it (editable). Cancel creates nothing. */
export const NewIdDialog: React.FC<{
  title: string;
  initialName: string;
  /** Default id; while the id is untouched, editing the name re-derives it. */
  initialId?: string;
  taken: string[];
  onCancel: () => void;
  onCreate: (name: string, id: string) => void;
}> = ({ title, initialName, initialId, taken, onCancel, onCreate }) => {
  const [name, setName] = useState(initialName);
  const [id, setId] = useState(() => freeId(initialId ?? slugifyId(initialName), taken));
  const [idTouched, setIdTouched] = useState(false);
  const error = idError(id, taken);
  const submit = () => { if (!error && name.trim()) onCreate(name.trim(), id); };

  return (
    <div style={overlayStyle} onClick={onCancel}>
      <div style={{ ...panelStyle, width: 380 }} onClick={e => e.stopPropagation()}
        onKeyDown={e => { if (e.key === 'Enter') submit(); if (e.key === 'Escape') onCancel(); }}>
        <h3 style={titleStyle}>{title}</h3>
        <div className="form-group" style={{ marginBottom: 0 }}>
          <label className="form-label">Name</label>
          <input className="form-input" type="text" value={name} autoFocus
            onFocus={e => e.target.select()}
            onChange={e => {
              setName(e.target.value);
              if (!idTouched) setId(freeId(slugifyId(e.target.value), taken));
            }} />
        </div>
        <div className="form-group" style={{ marginBottom: 0 }}>
          <label className="form-label">ID</label>
          <input className="form-input" type="text" value={id} style={monoStyle}
            onChange={e => { setId(e.target.value); setIdTouched(true); }} />
          {error ? <div style={errorStyle}>{error}</div>
            : <div style={hintStyle}>The id can't be changed once other data uses it.</div>}
        </div>
        <div style={{ display: 'flex', justifyContent: 'flex-end', gap: 8 }}>
          <button className="btn btn-ghost" onClick={onCancel}>Cancel</button>
          <button className="btn btn-primary" disabled={!!error || !name.trim()} onClick={submit}>Create</button>
        </div>
      </div>
    </div>
  );
};

/**
 * The ID field at the top of a details panel. Editable (commit on Enter or
 * blur, Escape reverts) only when `onRename` is given and `referencedBy` is empty.
 */
export const IdField: React.FC<{
  id: string;
  hint: string;
  taken: string[];
  /** What in the shared data points at this id; non-empty locks the field. */
  referencedBy?: string[];
  /** Shown under an editable field, e.g. references outside the shared data. */
  renameNote?: string;
  onRename?: (newId: string) => void;
}> = ({ id, hint, taken, referencedBy = [], renameNote, onRename }) => {
  const [draft, setDraft] = useState(id);
  const [copied, setCopied] = useState(false);
  useEffect(() => { setDraft(id); }, [id]);
  useEffect(() => {
    if (!copied) return;
    const t = setTimeout(() => setCopied(false), 1200);
    return () => clearTimeout(t);
  }, [copied]);

  const editable = !!onRename && referencedBy.length === 0;
  const error = draft === id ? null : idError(draft, taken, id);
  const commit = () => {
    if (draft === id) return;
    if (error) setDraft(id);
    else onRename!(draft);
  };
  const copy = () => {
    navigator.clipboard?.writeText(id).then(() => setCopied(true), () => setCopied(false));
  };

  return (
    <div className="form-group">
      <label className="form-label">ID</label>
      <div style={{ display: 'flex', gap: 6 }}>
        <input className="form-input" type="text" value={draft} disabled={!editable} spellCheck={false}
          style={{ ...monoStyle, flex: 1, background: 'var(--bg-primary)', color: 'var(--text-secondary)' }}
          onChange={e => setDraft(e.target.value)}
          onBlur={commit}
          onKeyDown={e => {
            if (e.key === 'Enter') commit();
            if (e.key === 'Escape') setDraft(id);
          }} />
        <button className="btn btn-ghost" type="button" onClick={copy} title="Copy the id to the clipboard"
          style={{ border: '1px solid var(--border-color)', minWidth: 64, justifyContent: 'center' }}>
          {copied ? 'Copied' : 'Copy'}
        </button>
      </div>
      {error && <div style={errorStyle}>{error}. Leaving the field keeps {id}.</div>}
      <div style={hintStyle}>{hint}</div>
      {onRename && referencedBy.length > 0 && (
        <div style={hintStyle}>Referenced by: {listRefs(referencedBy)}, so it can't be renamed here.</div>
      )}
      {editable && renameNote && <div style={hintStyle}>{renameNote}</div>}
    </div>
  );
};
