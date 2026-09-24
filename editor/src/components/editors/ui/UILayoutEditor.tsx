import React, { useState } from 'react';
import { useEditorStore } from '../../../store/editorStore';

// Must match UIConfig / DEFAULT_UI_CONFIG in shared/src/ui-config.ts (the editor
// keeps its own copy, as the other pages do, so it builds without shared/dist).
interface UIConfig {
  version: string;
  chat: { maxMessages: number; visibleLines: number };
  inventory: { cols: number; rows: number };
  nameplates: {
    fontSize: string;
    fontWeight: string;
    color: string;
    strokeColor: string;
    strokeThickness: number;
    bgColor: string;
    bgAlpha: number;
    yOffset: number;
    bgPaddingX: number;
    bgPaddingY: number;
    bgRadius: number;
  };
}

const DEFAULT_UI_CONFIG: UIConfig = {
  version: '1.1.0',
  chat: { maxMessages: 50, visibleLines: 9 },
  inventory: { cols: 8, rows: 4 },
  nameplates: {
    fontSize: '12px',
    fontWeight: 'bold',
    color: '#ffffff',
    strokeColor: '#000000',
    strokeThickness: 3,
    bgColor: '#000000',
    bgAlpha: 0.5,
    yOffset: -44,
    bgPaddingX: 4,
    bgPaddingY: 2,
    bgRadius: 3,
  },
};

/**
 * shared/data/ui-config.json — what is left of it after B-07 step 4.
 *
 * The game HUD's layout (every panel's place and size, the bars, the action
 * bar, cast bar, chat box, inventory and character panels, the death overlay
 * and their colours) moved into the WBP_GameHUD Widget Blueprint in Unreal. This
 * page edits the few numbers the C++ HUD still reads at runtime: the inventory
 * grid, the chat's line counts and the nameplates. A save reaches a running
 * game through the HUD's file watcher (or `valhalla.ReloadUI`).
 */

type Section = 'inventory' | 'chat' | 'nameplates';

const TABS: { key: Section; label: string }[] = [
  { key: 'inventory', label: 'Inventory' },
  { key: 'chat', label: 'Chat' },
  { key: 'nameplates', label: 'Nameplates' },
];

/** The stored config over the defaults, section by section, so a partial file still edits. */
function withDefaults(data: Partial<UIConfig> | null | undefined): UIConfig {
  const d = data ?? {};
  return {
    version: d.version ?? DEFAULT_UI_CONFIG.version,
    chat: { ...DEFAULT_UI_CONFIG.chat, ...(d.chat ?? {}) },
    inventory: { ...DEFAULT_UI_CONFIG.inventory, ...(d.inventory ?? {}) },
    nameplates: { ...DEFAULT_UI_CONFIG.nameplates, ...(d.nameplates ?? {}) },
  };
}

/** "12px" or 12 -> 12. */
function px(value: string | number): number {
  const n = parseFloat(String(value));
  return Number.isFinite(n) ? n : 12;
}

function clampInt(value: string, min: number, max: number, fallback: number): number {
  const n = parseInt(value, 10);
  return Number.isFinite(n) ? Math.min(max, Math.max(min, n)) : fallback;
}

function hexToRgba(hex: string, alpha: number): string {
  const h = hex.replace('#', '');
  const full = h.length === 3 ? h.split('').map(c => c + c).join('') : h;
  const v = parseInt(full, 16);
  if (full.length !== 6 || Number.isNaN(v)) return `rgba(0,0,0,${alpha})`;
  return `rgba(${(v >> 16) & 255},${(v >> 8) & 255},${v & 255},${alpha})`;
}

const NumberField: React.FC<{
  label: string;
  value: number;
  step?: number;
  min?: number;
  max?: number;
  hint?: string;
  onChange: (value: number) => void;
}> = ({ label, value, step = 1, min, max, hint, onChange }) => (
  <div className="form-group">
    <label className="form-label">{label}</label>
    <input
      className="form-input"
      type="number"
      value={value}
      step={step}
      min={min}
      max={max}
      onChange={(e) => {
        const n = step < 1 ? parseFloat(e.target.value) : parseInt(e.target.value, 10);
        if (Number.isFinite(n)) onChange(n);
      }}
    />
    {hint && <div style={{ fontSize: 11, color: 'var(--text-muted)', marginTop: 4 }}>{hint}</div>}
  </div>
);

const ColourField: React.FC<{ label: string; value: string; onChange: (value: string) => void }> = ({ label, value, onChange }) => (
  <div className="form-group">
    <label className="form-label">{label}</label>
    <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
      <input className="form-input" type="text" value={value} onChange={(e) => onChange(e.target.value)} style={{ flex: 1 }} />
      <input type="color" value={/^#[0-9a-fA-F]{6}$/.test(value) ? value : '#ffffff'} onChange={(e) => onChange(e.target.value)} />
    </div>
  </div>
);

export const UILayoutEditor: React.FC = () => {
  const uiConfig = useEditorStore(s => s.uiConfig);
  const updateData = useEditorStore(s => s.updateData);
  const saveSection = useEditorStore(s => s.saveSection);
  const [activeTab, setActiveTab] = useState<Section>('inventory');

  const config = withDefaults(uiConfig.data as Partial<UIConfig> | null);

  function update<K extends Section>(section: K, updates: Partial<UIConfig[K]>) {
    // Only the three sections are written back: anything else a pre-B-07 file
    // carried (hud, actionBar, castBar, deathOverlay, ...) is dropped on save.
    const next: UIConfig = { ...config, [section]: { ...config[section], ...updates } };
    updateData('uiConfig', next);
  }

  const handleSave = async () => {
    try {
      await saveSection('uiConfig');
    } catch (err) {
      console.error('Save failed:', err);
    }
  };

  const renderInventory = () => {
    const c = config.inventory;
    return (
      <>
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 16, marginBottom: 24 }}>
          <NumberField label="Columns" value={c.cols} min={1} max={16}
            onChange={(v) => update('inventory', { cols: clampInt(String(v), 1, 16, 8) })} />
          <NumberField label="Rows" value={c.rows} min={1} max={16}
            onChange={(v) => update('inventory', { rows: clampInt(String(v), 1, 16, 4) })} />
        </div>
        <div style={{ fontSize: 12, color: 'var(--text-muted)', marginBottom: 12 }}>
          {c.cols * c.rows} cells (the HUD makes at most 64). The server holds 32 inventory slots; cells past that stay empty.
        </div>
        <div style={{ display: 'grid', gridTemplateColumns: `repeat(${c.cols}, 18px)`, gap: 2 }}>
          {Array.from({ length: Math.min(64, c.cols * c.rows) }, (_, i) => (
            <div key={i} style={{ width: 18, height: 18, backgroundColor: '#2a2a3e', border: '1px solid #333355' }} />
          ))}
        </div>
      </>
    );
  };

  const renderChat = () => {
    const c = config.chat;
    return (
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 16 }}>
        <NumberField label="Lines while typing (maxMessages)" value={c.maxMessages} min={1} max={50}
          hint="How many of the last lines the open chat box lists. The client keeps 50."
          onChange={(v) => update('chat', { maxMessages: clampInt(String(v), 1, 50, 50) })} />
        <NumberField label="Lines while idle (visibleLines)" value={c.visibleLines} min={0} max={50}
          hint="How many lines show over the game when the box is closed; each fades 10 s after it arrived."
          onChange={(v) => update('chat', { visibleLines: clampInt(String(v), 0, 50, 9) })} />
      </div>
    );
  };

  const renderNameplates = () => {
    const c = config.nameplates;
    const outline = Math.max(0, c.strokeThickness * 0.5);
    return (
      <>
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 16, marginBottom: 24 }}>
          <NumberField label="Font size (px)" value={px(c.fontSize)} min={6} max={48}
            onChange={(v) => update('nameplates', { fontSize: `${v}px` })} />
          <div className="form-group">
            <label className="form-label">Font weight</label>
            <select className="form-input" value={c.fontWeight} onChange={(e) => update('nameplates', { fontWeight: e.target.value })}>
              <option value="bold">bold</option>
              <option value="normal">normal</option>
            </select>
          </div>
          <ColourField label="Text colour" value={c.color} onChange={(v) => update('nameplates', { color: v })} />
          <ColourField label="Outline colour" value={c.strokeColor} onChange={(v) => update('nameplates', { strokeColor: v })} />
          <NumberField label="Outline thickness" value={c.strokeThickness} min={0} max={8}
            onChange={(v) => update('nameplates', { strokeThickness: v })} />
          <NumberField label="Y offset (px, negative = up)" value={c.yOffset} min={-200} max={200}
            onChange={(v) => update('nameplates', { yOffset: v })} />
          <ColourField label="Background colour" value={c.bgColor} onChange={(v) => update('nameplates', { bgColor: v })} />
          <NumberField label="Background alpha (0-1)" value={c.bgAlpha} min={0} max={1} step={0.05}
            onChange={(v) => update('nameplates', { bgAlpha: Math.min(1, Math.max(0, v)) })} />
          <NumberField label="Background padding X" value={c.bgPaddingX} min={0} max={32}
            onChange={(v) => update('nameplates', { bgPaddingX: v })} />
          <NumberField label="Background padding Y" value={c.bgPaddingY} min={0} max={32}
            onChange={(v) => update('nameplates', { bgPaddingY: v })} />
          <NumberField label="Background corner radius" value={c.bgRadius} min={0} max={16}
            onChange={(v) => update('nameplates', { bgRadius: v })} />
        </div>
        <div style={{ fontSize: 12, color: 'var(--text)' }}>
          <div style={{ marginBottom: 12, fontWeight: 500 }}>Preview:</div>
          <div style={{ backgroundColor: '#3a4a2a', padding: 24, borderRadius: 4, width: 'fit-content' }}>
            <div
              style={{
                backgroundColor: hexToRgba(c.bgColor, c.bgAlpha),
                padding: `${c.bgPaddingY}px ${c.bgPaddingX}px`,
                borderRadius: c.bgRadius,
                textAlign: 'center',
              }}
            >
              <div
                style={{
                  fontSize: px(c.fontSize),
                  fontWeight: c.fontWeight === 'bold' ? 700 : 400,
                  color: c.color,
                  textShadow: outline > 0 ? `0 0 ${outline}px ${c.strokeColor}, 0 0 ${outline}px ${c.strokeColor}` : undefined,
                }}
              >
                Goblin Scout
              </div>
              <div style={{ backgroundColor: '#000000cc', height: 4, width: 60, margin: '2px auto 0' }}>
                <div style={{ backgroundColor: '#ff4444', height: '100%', width: '70%' }} />
              </div>
            </div>
          </div>
        </div>
      </>
    );
  };

  return (
    <div className="panel">
      <div className="panel-header">UI Layout</div>
      <div className="panel-body" style={{ display: 'flex', flexDirection: 'column', height: '100%' }}>
        <div
          style={{
            fontSize: 12,
            color: 'var(--text-muted)',
            border: '1px solid var(--border)',
            borderRadius: 4,
            padding: 10,
            marginBottom: 16,
          }}
        >
          The HUD's layout lives in Unreal: the <code>WBP_GameHUD</code> Widget Blueprint
          (Content/Valhalla/UI/HUD; its Class Defaults hold the cell sizes and bar and chat colours),
          with <code>WBP_HUDSlot</code> and <code>WBP_HUDBar</code> for the cells and bars. This page
          edits the few numbers the game still reads from ui-config.json.
        </div>

        <div className="tabs" style={{ marginBottom: 16 }}>
          {TABS.map(tab => (
            <button
              key={tab.key}
              className={`tab ${activeTab === tab.key ? 'active' : ''}`}
              onClick={() => setActiveTab(tab.key)}
              style={{
                padding: '8px 12px',
                cursor: 'pointer',
                borderBottom: activeTab === tab.key ? '2px solid var(--text-accent)' : '1px solid var(--border)',
                backgroundColor: activeTab === tab.key ? 'var(--bg-tertiary)' : 'transparent',
                color: activeTab === tab.key ? 'var(--text-accent)' : 'var(--text-muted)',
                marginRight: 4,
                borderRadius: '4px 4px 0 0',
                fontSize: 12,
                fontWeight: activeTab === tab.key ? 500 : 400,
              }}
            >
              {tab.label}
            </button>
          ))}
        </div>

        <div style={{ flex: 1, overflowY: 'auto', marginBottom: 16 }}>
          {activeTab === 'inventory' && renderInventory()}
          {activeTab === 'chat' && renderChat()}
          {activeTab === 'nameplates' && renderNameplates()}
        </div>

        <button className="btn btn-primary" onClick={handleSave} style={{ width: '100%' }}>
          Save UI Config
        </button>
      </div>
    </div>
  );
};
