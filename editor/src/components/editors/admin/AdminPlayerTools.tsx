/**
 * MMO admin tools for the Live Dashboard: the player list (grouped by zone),
 * the per-player right-click menu, a generic action dialog and the inspect
 * window. Everything goes through the 2.0 admin API's /player-action,
 * /player-inspect and /broadcast routes (see ValhallaAdminServer.h).
 */
import React, { useCallback, useEffect, useState } from 'react';
import { playerAction, postAdmin } from './adminApi';

// ── Types ───────────────────────────────────────────────────

export interface AdminPlayer {
  sessionId: string;
  name: string;
  classId: string;
  level: number;
  x: number;
  y: number;
  hp: number;
  maxHp: number;
  mana: number;
  maxMana: number;
  alive: boolean;
  zoneId?: string;
  godMode?: boolean;
  frozen?: boolean;
  mutedSeconds?: number;
}

export interface MenuItem {
  label: string;
  action: () => void;
  danger?: boolean;
}

export interface FieldSpec {
  key: string;
  label: string;
  type: 'text' | 'number' | 'select' | 'textarea';
  options?: { value: string; label: string }[];
  default?: string | number;
  min?: number;
  max?: number;
}

export interface ActionDialogSpec {
  title: string;
  description?: string;
  fields: FieldSpec[];
  submitLabel: string;
  danger?: boolean;
  onSubmit: (values: Record<string, string>) => Promise<unknown> | void;
}

// ── Menu ────────────────────────────────────────────────────

export interface PlayerMenuContext {
  allPlayers: AdminPlayer[];
  items: Record<string, any>;
  openDialog: (spec: ActionDialogSpec) => void;
  inspect: (player: AdminPlayer) => void;
  teleport: (player: AdminPlayer) => void;
}

/** Every admin action for one player, in the order an MMO GM reaches for them. */
export function buildPlayerMenu(p: AdminPlayer, ctx: PlayerMenuContext): MenuItem[] {
  const id = p.sessionId;
  const itemOptions = Object.entries(ctx.items)
    .map(([value, t]) => ({ value, label: `${(t as any).name || value} (${value})` }))
    .sort((a, b) => a.label.localeCompare(b.label));
  const others = ctx.allPlayers.filter(o => o.sessionId !== id);

  const menu: MenuItem[] = [
    { label: `Inspect ${p.name}…`, action: () => ctx.inspect(p) },
    { label: 'Send private message…', action: () => ctx.openDialog({
      title: `Message ${p.name}`, submitLabel: 'Send',
      fields: [{ key: 'text', label: 'Message (shown as [Admin])', type: 'textarea' }],
      onSubmit: v => playerAction(id, 'message', { text: v.text }),
    }) },
    // ── Movement
    { label: 'Teleport to location…', action: () => ctx.teleport(p) },
  ];
  if (others.length > 0) {
    menu.push({ label: 'Send to another player…', action: () => ctx.openDialog({
      title: `Send ${p.name} to…`, submitLabel: 'Teleport',
      fields: [{ key: 'target', label: 'Player', type: 'select',
        options: others.map(o => ({ value: o.sessionId, label: `${o.name} (${o.zoneId ?? '?'})` })) }],
      onSubmit: v => playerAction(id, 'teleport-to-player', { targetSessionId: v.target }),
    }) });
    menu.push({ label: 'Summon another player here…', action: () => ctx.openDialog({
      title: `Bring a player to ${p.name}`, submitLabel: 'Summon',
      fields: [{ key: 'who', label: 'Player', type: 'select',
        options: others.map(o => ({ value: o.sessionId, label: `${o.name} (${o.zoneId ?? '?'})` })) }],
      onSubmit: v => playerAction(v.who, 'teleport-to-player', { targetSessionId: id }),
    }) });
  }
  menu.push(
    { label: 'Unstuck (zone default spawn)', action: () => playerAction(id, 'unstuck') },
    { label: p.frozen ? 'Unfreeze' : 'Freeze', action: () => playerAction(id, 'freeze', { enabled: !p.frozen }) },
    // ── Vitals
    ...(p.alive
      ? [
        { label: 'Full heal', action: () => { playerAction(id, 'heal'); } },
        { label: 'Set HP / mana…', action: () => ctx.openDialog({
          title: `Set vitals for ${p.name}`, submitLabel: 'Apply',
          description: 'Leave a field blank to keep it. HP 0 kills the player. "Mana" is energy for energy classes.',
          fields: [
            { key: 'hp', label: `HP (max ${p.maxHp})`, type: 'number', default: p.hp, min: 0, max: p.maxHp },
            { key: 'mana', label: `Mana/Energy (max ${p.maxMana})`, type: 'number', default: p.mana, min: 0, max: p.maxMana },
          ],
          onSubmit: v => playerAction(id, 'set-vitals', {
            ...(v.hp !== '' ? { hp: Number(v.hp) } : {}),
            ...(v.mana !== '' ? { mana: Number(v.mana) } : {}),
          }),
        }) },
      ]
      : [{ label: 'Resurrect in place', action: () => { playerAction(id, 'resurrect'); } }]),
    { label: p.godMode ? 'God mode: turn off' : 'God mode: turn on', action: () => playerAction(id, 'god-mode', { enabled: !p.godMode }) },
    { label: 'Reset cooldowns', action: () => playerAction(id, 'reset-cooldowns') },
    { label: 'Clear buffs / debuffs', action: () => playerAction(id, 'clear-buffs') },
    // ── Items & progression
    { label: 'Give item…', action: () => ctx.openDialog({
      title: `Give an item to ${p.name}`, submitLabel: 'Give',
      description: 'Goes straight into their inventory. Items must be saved in the editor first.',
      fields: [
        { key: 'itemId', label: 'Item', type: 'select', options: itemOptions },
        { key: 'quantity', label: 'Quantity', type: 'number', default: 1, min: 1, max: 999 },
      ],
      onSubmit: v => playerAction(id, 'give-item', { itemId: v.itemId, quantity: Number(v.quantity) || 1 }),
    }) },
    { label: 'Set level…', action: () => ctx.openDialog({
      title: `Set ${p.name}'s level`, submitLabel: 'Set level',
      description: 'XP resets to 0 and pools refill.',
      fields: [{ key: 'level', label: 'Level (1-25)', type: 'number', default: p.level, min: 1, max: 25 }],
      onSubmit: v => playerAction(id, 'set-level', { level: Number(v.level) }),
    }) },
    { label: 'Grant XP…', action: () => ctx.openDialog({
      title: `Grant XP to ${p.name}`, submitLabel: 'Grant',
      fields: [{ key: 'amount', label: 'XP', type: 'number', default: 100, min: 1 }],
      onSubmit: v => playerAction(id, 'grant-xp', { amount: Number(v.amount) }),
    }) },
    // ── Moderation
    (p.mutedSeconds ?? 0) > 0
      ? { label: 'Unmute', action: () => playerAction(id, 'mute', { minutes: 0 }) }
      : { label: 'Mute chat…', action: () => ctx.openDialog({
        title: `Mute ${p.name}`, submitLabel: 'Mute',
        fields: [{ key: 'minutes', label: 'Minutes', type: 'number', default: 10, min: 1 }],
        onSubmit: v => playerAction(id, 'mute', { minutes: Number(v.minutes) }),
      }) },
    { label: 'Force save', action: () => playerAction(id, 'save') },
  );
  if (p.alive) {
    menu.push({ label: 'Kill', danger: true, action: () => ctx.openDialog({
      title: `Kill ${p.name}?`, submitLabel: 'Kill', danger: true, fields: [],
      onSubmit: () => playerAction(id, 'kill'),
    }) });
  }
  menu.push({ label: 'Kick…', danger: true, action: () => ctx.openDialog({
    title: `Kick ${p.name}`, submitLabel: 'Kick', danger: true,
    description: 'Their character is saved on the way out.',
    fields: [{ key: 'reason', label: 'Reason (shown to the player)', type: 'text' }],
    onSubmit: v => postAdmin('kick-player', { sessionId: id, reason: v.reason }, { label: `kick ${p.name}` }),
  }) });
  return menu;
}

// ── Action dialog ───────────────────────────────────────────

export const ActionDialog: React.FC<{ spec: ActionDialogSpec; onClose: () => void }> = ({ spec, onClose }) => {
  const [values, setValues] = useState<Record<string, string>>(() => {
    const init: Record<string, string> = {};
    for (const f of spec.fields) {
      init[f.key] = f.default !== undefined ? String(f.default) : (f.type === 'select' && f.options?.length ? f.options[0].value : '');
    }
    return init;
  });
  const [busy, setBusy] = useState(false);

  const submit = async () => {
    setBusy(true);
    try { await spec.onSubmit(values); } finally { setBusy(false); onClose(); }
  };

  return (
    <div style={overlayStyle} onClick={onClose}>
      <div style={{ ...panelStyle, width: 380 }} onClick={e => e.stopPropagation()}
        onKeyDown={e => { if (e.key === 'Enter' && !(e.target as HTMLElement).matches('textarea')) submit(); if (e.key === 'Escape') onClose(); }}>
        <h3 style={titleStyle}>{spec.title}</h3>
        {spec.description && <div style={{ fontSize: 11, color: 'var(--text-muted)' }}>{spec.description}</div>}
        {spec.fields.map((f, i) => (
          <label key={f.key} style={{ display: 'flex', flexDirection: 'column', gap: 4, fontSize: 11, color: 'var(--text-secondary)' }}>
            {f.label}
            {f.type === 'select' ? (
              <select className="form-input" value={values[f.key]} autoFocus={i === 0}
                onChange={e => setValues(v => ({ ...v, [f.key]: e.target.value }))}>
                {(f.options ?? []).map(o => <option key={o.value} value={o.value}>{o.label}</option>)}
              </select>
            ) : f.type === 'textarea' ? (
              <textarea className="form-input" rows={3} value={values[f.key]} autoFocus={i === 0} maxLength={200}
                onChange={e => setValues(v => ({ ...v, [f.key]: e.target.value }))} />
            ) : (
              <input className="form-input" type={f.type} min={f.min} max={f.max} value={values[f.key]} autoFocus={i === 0}
                onChange={e => setValues(v => ({ ...v, [f.key]: e.target.value }))} />
            )}
          </label>
        ))}
        <div style={{ display: 'flex', justifyContent: 'flex-end', gap: 8 }}>
          <button className="btn btn-ghost" onClick={onClose}>Cancel</button>
          <button className={spec.danger ? 'btn btn-danger' : 'btn btn-primary'} disabled={busy} onClick={submit}>
            {spec.submitLabel}
          </button>
        </div>
      </div>
    </div>
  );
};

// ── Players panel ───────────────────────────────────────────

export const PlayersPanel: React.FC<{
  zones: Record<string, { players: AdminPlayer[] }>;
  zoneNames: Record<string, string>;
  selectedSessionId: string | null;
  onSelect: (zoneId: string, player: AdminPlayer) => void;
  onMenu: (e: React.MouseEvent, player: AdminPlayer) => void;
}> = ({ zones, zoneNames, selectedSessionId, onSelect, onMenu }) => {
  const zoneIds = Object.keys(zones).sort();
  const total = zoneIds.reduce((n, z) => n + zones[z].players.length, 0);
  return (
    <div style={{
      width: 250, flexShrink: 0, overflow: 'auto', borderLeft: '1px solid var(--border-color)',
      background: 'var(--bg-secondary)', fontSize: 12,
    }}>
      <div style={{ padding: '8px 10px', fontWeight: 600, color: 'var(--text-primary)', borderBottom: '1px solid var(--border-color)' }}>
        Players ({total})
        <div style={{ fontSize: 10, fontWeight: 400, color: 'var(--text-muted)' }}>Right-click a player for admin actions</div>
      </div>
      {zoneIds.map(zoneId => (
        <div key={zoneId}>
          <div style={{ padding: '6px 10px', fontSize: 11, color: 'var(--text-muted)', background: 'var(--bg-tertiary)' }}>
            {zoneNames[zoneId] || zoneId} · {zones[zoneId].players.length}
          </div>
          {zones[zoneId].players.length === 0 && (
            <div style={{ padding: '4px 10px 8px', fontSize: 11, color: 'var(--text-muted)' }}>nobody here</div>
          )}
          {zones[zoneId].players.map(p => {
            const hpPct = p.maxHp > 0 ? Math.max(0, Math.min(1, p.hp / p.maxHp)) : 0;
            const selected = p.sessionId === selectedSessionId;
            return (
              <div key={p.sessionId}
                onClick={() => onSelect(zoneId, p)}
                onContextMenu={e => { e.preventDefault(); onMenu(e, p); }}
                style={{
                  padding: '6px 10px', cursor: 'pointer', borderBottom: '1px solid rgba(51,51,85,0.25)',
                  background: selected ? 'var(--accent-dim)' : 'transparent',
                }}>
                <div style={{ display: 'flex', justifyContent: 'space-between', gap: 6 }}>
                  <span style={{ color: p.alive ? 'var(--text-primary)' : 'var(--text-muted)', fontWeight: 500 }}>
                    {p.name}{!p.alive && ' (dead)'}
                  </span>
                  <span style={{ color: 'var(--text-muted)', fontSize: 10 }}>Lv {p.level} {p.classId}</span>
                </div>
                <div style={{ height: 4, background: '#331515', borderRadius: 2, marginTop: 4 }}>
                  <div style={{ width: `${hpPct * 100}%`, height: '100%', background: '#d33', borderRadius: 2 }} />
                </div>
                {(p.godMode || p.frozen || (p.mutedSeconds ?? 0) > 0) && (
                  <div style={{ display: 'flex', gap: 4, marginTop: 4 }}>
                    {p.godMode && <span style={badgeStyle('#c9a227')}>GOD</span>}
                    {p.frozen && <span style={badgeStyle('#3a8bd6')}>FROZEN</span>}
                    {(p.mutedSeconds ?? 0) > 0 && <span style={badgeStyle('#888')}>MUTED {Math.ceil((p.mutedSeconds ?? 0) / 60)}m</span>}
                  </div>
                )}
              </div>
            );
          })}
        </div>
      ))}
    </div>
  );
};

// ── Inspect dialog ──────────────────────────────────────────

export const InspectDialog: React.FC<{ player: AdminPlayer; onClose: () => void }> = ({ player, onClose }) => {
  const [data, setData] = useState<any>(null);

  const refresh = useCallback(async () => {
    const res = await postAdmin('player-inspect', { sessionId: player.sessionId }, { quiet: true, label: `inspect ${player.name}` });
    if (res.ok) setData(res);
  }, [player.sessionId, player.name]);

  useEffect(() => {
    refresh();
    const t = setInterval(refresh, 2000);
    return () => clearInterval(t);
  }, [refresh]);

  return (
    <div style={overlayStyle} onClick={onClose}>
      <div style={{ ...panelStyle, width: 520, maxHeight: '80vh', overflow: 'auto' }} onClick={e => e.stopPropagation()}>
        <div style={{ display: 'flex', justifyContent: 'space-between' }}>
          <h3 style={titleStyle}>{player.name}</h3>
          <button className="btn btn-ghost" onClick={onClose}>Close</button>
        </div>
        {!data ? <div style={{ fontSize: 12, color: 'var(--text-muted)' }}>Loading…</div> : (
          <>
            <div style={{ fontSize: 12, color: 'var(--text-secondary)' }}>
              Lv {data.level} {data.classId} · XP {data.xp} · {data.zoneId} · {data.alive ? 'alive' : 'dead'}
              {data.godMode ? ' · GOD' : ''}{data.frozen ? ' · FROZEN' : ''}{data.mutedSeconds > 0 ? ` · muted ${Math.ceil(data.mutedSeconds / 60)}m` : ''}
            </div>
            <div style={{ fontSize: 12 }}>
              HP {data.hp}/{data.maxHp} · Mana {data.mana}/{data.maxMana} · Energy {data.energy}/{data.maxEnergy}
            </div>
            <Section title="Stats">
              <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: 4, fontSize: 11 }}>
                {Object.entries(data.stats ?? {}).map(([k, v]) => <div key={k}><span style={{ color: 'var(--text-muted)' }}>{k}</span> {String(v)}</div>)}
              </div>
            </Section>
            <Section title="Equipment">
              {Object.keys(data.equipment ?? {}).length === 0 ? <Empty /> : Object.entries(data.equipment).map(([slot, item]) => (
                <div key={slot} style={{ fontSize: 11 }}><span style={{ color: 'var(--text-muted)', display: 'inline-block', width: 70 }}>{slot}</span>{String(item)}</div>
              ))}
            </Section>
            <Section title={`Inventory (${(data.inventory ?? []).length})`}>
              {(data.inventory ?? []).length === 0 ? <Empty /> : data.inventory.map((s: any) => (
                <div key={s.slot} style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', fontSize: 11, padding: '2px 0' }}>
                  <span>#{s.slot} {s.name} {s.quantity > 1 ? `×${s.quantity}` : ''}</span>
                  <button className="btn btn-ghost" style={{ padding: '1px 8px', fontSize: 11 }}
                    onClick={async () => { await playerAction(player.sessionId, 'remove-item', { slot: s.slot, quantity: s.quantity }); refresh(); }}>
                    Remove
                  </button>
                </div>
              ))}
            </Section>
            <Section title="Buffs / debuffs">
              {(data.buffs ?? []).length === 0 ? <Empty /> : data.buffs.map((b: any, i: number) => (
                <div key={i} style={{ fontSize: 11 }}>{b.skillId} · {b.secondsLeft}s left</div>
              ))}
            </Section>
          </>
        )}
      </div>
    </div>
  );
};

const Section: React.FC<{ title: string; children: React.ReactNode }> = ({ title, children }) => (
  <div>
    <div style={{ fontSize: 11, fontWeight: 600, color: 'var(--text-muted)', margin: '6px 0 4px', textTransform: 'uppercase' }}>{title}</div>
    {children}
  </div>
);

const Empty: React.FC = () => <div style={{ fontSize: 11, color: 'var(--text-muted)' }}>none</div>;

// ── Styles ──────────────────────────────────────────────────

const overlayStyle: React.CSSProperties = {
  position: 'fixed', inset: 0, background: 'rgba(0,0,0,0.6)',
  display: 'flex', alignItems: 'center', justifyContent: 'center', zIndex: 300,
};
const panelStyle: React.CSSProperties = {
  background: 'var(--bg-secondary)', border: '1px solid var(--border-color)',
  borderRadius: 8, padding: 20, display: 'flex', flexDirection: 'column', gap: 12,
};
const titleStyle: React.CSSProperties = { margin: 0, fontSize: 14, color: 'var(--text-primary)' };
const badgeStyle = (color: string): React.CSSProperties => ({
  fontSize: 9, fontWeight: 700, padding: '1px 5px', borderRadius: 3, color: '#000', background: color,
});
