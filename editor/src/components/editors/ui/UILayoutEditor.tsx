import React, { useState } from 'react';
import { useEditorStore } from '../../../store/editorStore';

interface UIConfig {
  hpMana?: {
    x?: number;
    y?: number;
    width?: number;
    height?: number;
    backgroundColor?: string;
    borderColor?: string;
    textColor?: string;
    fontSize?: number;
    fontFamily?: string;
    alpha?: number;
    cornerRadius?: number;
  };
  energy?: {
    x?: number;
    y?: number;
    width?: number;
    height?: number;
    backgroundColor?: string;
    barColor?: string;
    textColor?: string;
    fontSize?: number;
    fontFamily?: string;
    alpha?: number;
  };
  actionBar?: {
    x?: number;
    y?: number;
    width?: number;
    height?: number;
    backgroundColor?: string;
    borderColor?: string;
    slotSize?: number;
    columns?: number;
    rows?: number;
    alpha?: number;
  };
  chat?: {
    x?: number;
    y?: number;
    width?: number;
    height?: number;
    backgroundColor?: string;
    textColor?: string;
    fontSize?: number;
    fontFamily?: string;
    alpha?: number;
    maxMessages?: number;
  };
  inventory?: {
    x?: number;
    y?: number;
    width?: number;
    height?: number;
    backgroundColor?: string;
    borderColor?: string;
    slotSize?: number;
    columns?: number;
    alpha?: number;
  };
  castBar?: {
    x?: number;
    y?: number;
    width?: number;
    height?: number;
    backgroundColor?: string;
    barColor?: string;
    textColor?: string;
    fontSize?: number;
    fontFamily?: string;
    alpha?: number;
  };
  nameplates?: {
    offsetX?: number;
    offsetY?: number;
    width?: number;
    height?: number;
    fontSize?: number;
    fontFamily?: string;
    healthBarColor?: string;
    manaBarColor?: string;
    alpha?: number;
  };
  deathScreen?: {
    backgroundColor?: string;
    textColor?: string;
    fontSize?: number;
    fontFamily?: string;
    buttonColor?: string;
    buttonTextColor?: string;
    alpha?: number;
  };
}

type TabType = 'hpMana' | 'energy' | 'actionBar' | 'chat' | 'inventory' | 'castBar' | 'nameplates' | 'deathScreen';

const TABS: { key: TabType; label: string }[] = [
  { key: 'hpMana', label: 'HP/Mana' },
  { key: 'energy', label: 'Energy' },
  { key: 'actionBar', label: 'Action Bar' },
  { key: 'chat', label: 'Chat' },
  { key: 'inventory', label: 'Inventory' },
  { key: 'castBar', label: 'Cast Bar' },
  { key: 'nameplates', label: 'Nameplates' },
  { key: 'deathScreen', label: 'Death Screen' },
];

export const UILayoutEditor: React.FC = () => {
  const uiConfig = useEditorStore(s => s.uiConfig);
  const updateData = useEditorStore(s => s.updateData);
  const saveSection = useEditorStore(s => s.saveSection);
  const [activeTab, setActiveTab] = useState<TabType>('hpMana');

  const config: UIConfig = uiConfig.data || {};

  const handleUpdateConfig = (tab: TabType, updates: any) => {
    const updatedConfig = {
      ...config,
      [tab]: {
        ...config[tab],
        ...updates,
      },
    };
    updateData('uiConfig', updatedConfig);
  };

  const handleSave = async () => {
    try {
      await saveSection('uiConfig');
    } catch (err) {
      console.error('Save failed:', err);
    }
  };

  const renderPreviewHpMana = () => {
    const c = config.hpMana || {};
    const bgColor = c.backgroundColor || '#1a1a1a';
    const borderColor = c.borderColor || '#444444';
    const barHeight = 16;

    return (
      <div style={{ fontSize: 12, color: 'var(--text)' }}>
        <div style={{ marginBottom: 12, fontWeight: 500 }}>Preview:</div>
        <div
          style={{
            backgroundColor: bgColor,
            border: `2px solid ${borderColor}`,
            borderRadius: `${c.cornerRadius || 4}px`,
            padding: 8,
            width: '100%',
            maxWidth: 300,
            opacity: c.alpha !== undefined ? c.alpha : 1,
          }}
        >
          <div style={{ marginBottom: 6 }}>
            <div style={{ fontSize: 11, color: c.textColor || '#ffffff', marginBottom: 2 }}>HP</div>
            <div style={{ backgroundColor: '#333333', borderRadius: 2, height: barHeight, overflow: 'hidden' }}>
              <div
                style={{
                  backgroundColor: '#00ff00',
                  height: '100%',
                  width: '75%',
                  transition: 'width 0.3s',
                }}
              />
            </div>
          </div>
          <div>
            <div style={{ fontSize: 11, color: c.textColor || '#ffffff', marginBottom: 2 }}>Mana</div>
            <div style={{ backgroundColor: '#333333', borderRadius: 2, height: barHeight, overflow: 'hidden' }}>
              <div
                style={{
                  backgroundColor: '#0099ff',
                  height: '100%',
                  width: '50%',
                  transition: 'width 0.3s',
                }}
              />
            </div>
          </div>
        </div>
      </div>
    );
  };

  const renderPreviewActionBar = () => {
    const c = config.actionBar || {};
    const cols = c.columns || 5;
    const rows = c.rows || 2;
    const slotSize = c.slotSize || 40;

    return (
      <div style={{ fontSize: 12, color: 'var(--text)' }}>
        <div style={{ marginBottom: 12, fontWeight: 500 }}>Preview:</div>
        <div
          style={{
            backgroundColor: c.backgroundColor || '#1a1a1a',
            border: `1px solid ${c.borderColor || '#444444'}`,
            padding: 8,
            borderRadius: 4,
            display: 'grid',
            gridTemplateColumns: `repeat(${cols}, ${slotSize}px)`,
            gap: 4,
            opacity: c.alpha !== undefined ? c.alpha : 1,
          }}
        >
          {Array.from({ length: cols * rows }).map((_, i) => (
            <div
              key={i}
              style={{
                width: slotSize,
                height: slotSize,
                backgroundColor: '#333333',
                border: '1px solid #555555',
                borderRadius: 2,
              }}
            />
          ))}
        </div>
      </div>
    );
  };

  const renderPreviewChat = () => {
    const c = config.chat || {};

    return (
      <div style={{ fontSize: 12, color: 'var(--text)' }}>
        <div style={{ marginBottom: 12, fontWeight: 500 }}>Preview:</div>
        <div
          style={{
            backgroundColor: c.backgroundColor || '#1a1a1a',
            width: '100%',
            maxWidth: 300,
            height: 100,
            borderRadius: 4,
            padding: 8,
            overflow: 'hidden',
            opacity: c.alpha !== undefined ? c.alpha : 1,
            border: '1px solid var(--border)',
            fontSize: c.fontSize || 12,
            color: c.textColor || '#ffffff',
            fontFamily: c.fontFamily || 'monospace',
          }}
        >
          <div style={{ marginBottom: 4 }}>Player: Hello!</div>
          <div style={{ marginBottom: 4 }}>NPC: Welcome!</div>
          <div style={{ color: '#ffff00' }}>System: Player joined.</div>
        </div>
      </div>
    );
  };

  const renderPreviewInventory = () => {
    const c = config.inventory || {};
    const cols = c.columns || 5;
    const slotSize = c.slotSize || 48;

    return (
      <div style={{ fontSize: 12, color: 'var(--text)' }}>
        <div style={{ marginBottom: 12, fontWeight: 500 }}>Preview:</div>
        <div
          style={{
            backgroundColor: c.backgroundColor || '#1a1a1a',
            border: `1px solid ${c.borderColor || '#444444'}`,
            padding: 8,
            borderRadius: 4,
            display: 'grid',
            gridTemplateColumns: `repeat(${cols}, ${slotSize}px)`,
            gap: 4,
            width: 'fit-content',
            opacity: c.alpha !== undefined ? c.alpha : 1,
          }}
        >
          {Array.from({ length: cols * 3 }).map((_, i) => (
            <div
              key={i}
              style={{
                width: slotSize,
                height: slotSize,
                backgroundColor: '#333333',
                border: '1px solid #555555',
                borderRadius: 2,
              }}
            />
          ))}
        </div>
      </div>
    );
  };

  const renderPreviewCastBar = () => {
    const c = config.castBar || {};

    return (
      <div style={{ fontSize: 12, color: 'var(--text)' }}>
        <div style={{ marginBottom: 12, fontWeight: 500 }}>Preview:</div>
        <div
          style={{
            backgroundColor: c.backgroundColor || '#1a1a1a',
            border: '1px solid var(--border)',
            borderRadius: 4,
            padding: 8,
            width: '100%',
            maxWidth: 250,
            opacity: c.alpha !== undefined ? c.alpha : 1,
          }}
        >
          <div style={{ fontSize: 11, color: c.textColor || '#ffffff', marginBottom: 4 }}>
            Fireball
          </div>
          <div style={{ backgroundColor: '#333333', borderRadius: 2, height: 16, overflow: 'hidden' }}>
            <div
              style={{
                backgroundColor: c.barColor || '#ff6600',
                height: '100%',
                width: '60%',
                transition: 'width 0.1s linear',
              }}
            />
          </div>
          <div style={{ fontSize: 10, color: 'var(--text-muted)', marginTop: 4 }}>2.4s / 4.0s</div>
        </div>
      </div>
    );
  };

  const renderNameplatePreview = () => {
    const c = config.nameplates || {};

    return (
      <div style={{ fontSize: 12, color: 'var(--text)' }}>
        <div style={{ marginBottom: 12, fontWeight: 500 }}>Preview:</div>
        <div
          style={{
            backgroundColor: '#1a1a1a',
            border: '1px solid var(--border)',
            borderRadius: 4,
            padding: 12,
            width: 'fit-content',
            opacity: c.alpha !== undefined ? c.alpha : 1,
          }}
        >
          <div
            style={{
              fontSize: c.fontSize || 12,
              fontFamily: c.fontFamily || 'sans-serif',
              color: '#ffffff',
              marginBottom: 4,
            }}
          >
            Enemy Name
          </div>
          <div style={{ backgroundColor: '#333333', borderRadius: 2, height: 6, width: 100, marginBottom: 2 }}>
            <div
              style={{
                backgroundColor: c.healthBarColor || '#00ff00',
                height: '100%',
                width: '75%',
              }}
            />
          </div>
          <div style={{ backgroundColor: '#333333', borderRadius: 2, height: 6, width: 100 }}>
            <div
              style={{
                backgroundColor: c.manaBarColor || '#0099ff',
                height: '100%',
                width: '40%',
              }}
            />
          </div>
        </div>
      </div>
    );
  };

  const renderDeathScreenPreview = () => {
    const c = config.deathScreen || {};

    return (
      <div style={{ fontSize: 12, color: 'var(--text)' }}>
        <div style={{ marginBottom: 12, fontWeight: 500 }}>Preview:</div>
        <div
          style={{
            backgroundColor: c.backgroundColor || '#000000',
            width: '100%',
            maxWidth: 300,
            height: 150,
            borderRadius: 4,
            padding: 16,
            display: 'flex',
            flexDirection: 'column',
            alignItems: 'center',
            justifyContent: 'center',
            opacity: c.alpha !== undefined ? c.alpha : 1,
            border: '1px solid var(--border)',
          }}
        >
          <div
            style={{
              color: c.textColor || '#ff0000',
              fontSize: c.fontSize || 24,
              fontFamily: c.fontFamily || 'sans-serif',
              marginBottom: 16,
              fontWeight: 'bold',
            }}
          >
            YOU DIED
          </div>
          <button
            style={{
              backgroundColor: c.buttonColor || '#ff6600',
              color: c.buttonTextColor || '#ffffff',
              border: 'none',
              borderRadius: 4,
              padding: '8px 16px',
              cursor: 'pointer',
              fontSize: 12,
            }}
          >
            Respawn
          </button>
        </div>
      </div>
    );
  };

  const getTabConfig = (tab: TabType): any => {
    return config[tab] || {};
  };

  const renderTabContent = () => {
    const tabConfig = getTabConfig(activeTab);

    switch (activeTab) {
      case 'hpMana':
        return (
          <>
            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 16, marginBottom: 24 }}>
              <div className="form-group">
                <label className="form-label">X Position</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.x ?? 10}
                  onChange={(e) => handleUpdateConfig('hpMana', { x: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Y Position</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.y ?? 10}
                  onChange={(e) => handleUpdateConfig('hpMana', { y: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Width</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.width ?? 200}
                  onChange={(e) => handleUpdateConfig('hpMana', { width: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Height</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.height ?? 80}
                  onChange={(e) => handleUpdateConfig('hpMana', { height: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Background Color</label>
                <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                  <input
                    className="form-input"
                    type="text"
                    value={tabConfig.backgroundColor ?? '#1a1a1a'}
                    onChange={(e) => handleUpdateConfig('hpMana', { backgroundColor: e.target.value })}
                    style={{ flex: 1 }}
                  />
                  <div
                    style={{
                      width: 32,
                      height: 32,
                      backgroundColor: tabConfig.backgroundColor || '#1a1a1a',
                      borderRadius: 4,
                      border: '1px solid var(--border)',
                    }}
                  />
                </div>
              </div>
              <div className="form-group">
                <label className="form-label">Border Color</label>
                <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                  <input
                    className="form-input"
                    type="text"
                    value={tabConfig.borderColor ?? '#444444'}
                    onChange={(e) => handleUpdateConfig('hpMana', { borderColor: e.target.value })}
                    style={{ flex: 1 }}
                  />
                  <div
                    style={{
                      width: 32,
                      height: 32,
                      backgroundColor: tabConfig.borderColor || '#444444',
                      borderRadius: 4,
                      border: '1px solid var(--border)',
                    }}
                  />
                </div>
              </div>
              <div className="form-group">
                <label className="form-label">Text Color</label>
                <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                  <input
                    className="form-input"
                    type="text"
                    value={tabConfig.textColor ?? '#ffffff'}
                    onChange={(e) => handleUpdateConfig('hpMana', { textColor: e.target.value })}
                    style={{ flex: 1 }}
                  />
                  <div
                    style={{
                      width: 32,
                      height: 32,
                      backgroundColor: tabConfig.textColor || '#ffffff',
                      borderRadius: 4,
                      border: '1px solid var(--border)',
                    }}
                  />
                </div>
              </div>
              <div className="form-group">
                <label className="form-label">Font Size</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.fontSize ?? 14}
                  onChange={(e) => handleUpdateConfig('hpMana', { fontSize: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Font Family</label>
                <input
                  className="form-input"
                  type="text"
                  value={tabConfig.fontFamily ?? 'sans-serif'}
                  onChange={(e) => handleUpdateConfig('hpMana', { fontFamily: e.target.value })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Alpha (0-1)</label>
                <input
                  className="form-input"
                  type="number"
                  min="0"
                  max="1"
                  step="0.1"
                  value={tabConfig.alpha ?? 1}
                  onChange={(e) => handleUpdateConfig('hpMana', { alpha: parseFloat(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Corner Radius</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.cornerRadius ?? 4}
                  onChange={(e) => handleUpdateConfig('hpMana', { cornerRadius: parseInt(e.target.value) })}
                />
              </div>
            </div>
            {renderPreviewHpMana()}
          </>
        );

      case 'energy':
        return (
          <>
            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 16, marginBottom: 24 }}>
              <div className="form-group">
                <label className="form-label">X Position</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.x ?? 10}
                  onChange={(e) => handleUpdateConfig('energy', { x: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Y Position</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.y ?? 100}
                  onChange={(e) => handleUpdateConfig('energy', { y: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Width</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.width ?? 200}
                  onChange={(e) => handleUpdateConfig('energy', { width: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Height</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.height ?? 20}
                  onChange={(e) => handleUpdateConfig('energy', { height: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Background Color</label>
                <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                  <input
                    className="form-input"
                    type="text"
                    value={tabConfig.backgroundColor ?? '#1a1a1a'}
                    onChange={(e) => handleUpdateConfig('energy', { backgroundColor: e.target.value })}
                    style={{ flex: 1 }}
                  />
                  <div
                    style={{
                      width: 32,
                      height: 32,
                      backgroundColor: tabConfig.backgroundColor || '#1a1a1a',
                      borderRadius: 4,
                      border: '1px solid var(--border)',
                    }}
                  />
                </div>
              </div>
              <div className="form-group">
                <label className="form-label">Bar Color</label>
                <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                  <input
                    className="form-input"
                    type="text"
                    value={tabConfig.barColor ?? '#ffff00'}
                    onChange={(e) => handleUpdateConfig('energy', { barColor: e.target.value })}
                    style={{ flex: 1 }}
                  />
                  <div
                    style={{
                      width: 32,
                      height: 32,
                      backgroundColor: tabConfig.barColor || '#ffff00',
                      borderRadius: 4,
                      border: '1px solid var(--border)',
                    }}
                  />
                </div>
              </div>
              <div className="form-group">
                <label className="form-label">Text Color</label>
                <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                  <input
                    className="form-input"
                    type="text"
                    value={tabConfig.textColor ?? '#ffffff'}
                    onChange={(e) => handleUpdateConfig('energy', { textColor: e.target.value })}
                    style={{ flex: 1 }}
                  />
                  <div
                    style={{
                      width: 32,
                      height: 32,
                      backgroundColor: tabConfig.textColor || '#ffffff',
                      borderRadius: 4,
                      border: '1px solid var(--border)',
                    }}
                  />
                </div>
              </div>
              <div className="form-group">
                <label className="form-label">Font Size</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.fontSize ?? 12}
                  onChange={(e) => handleUpdateConfig('energy', { fontSize: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Font Family</label>
                <input
                  className="form-input"
                  type="text"
                  value={tabConfig.fontFamily ?? 'sans-serif'}
                  onChange={(e) => handleUpdateConfig('energy', { fontFamily: e.target.value })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Alpha (0-1)</label>
                <input
                  className="form-input"
                  type="number"
                  min="0"
                  max="1"
                  step="0.1"
                  value={tabConfig.alpha ?? 1}
                  onChange={(e) => handleUpdateConfig('energy', { alpha: parseFloat(e.target.value) })}
                />
              </div>
            </div>
          </>
        );

      case 'actionBar':
        return (
          <>
            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 16, marginBottom: 24 }}>
              <div className="form-group">
                <label className="form-label">X Position</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.x ?? 300}
                  onChange={(e) => handleUpdateConfig('actionBar', { x: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Y Position</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.y ?? 400}
                  onChange={(e) => handleUpdateConfig('actionBar', { y: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Slot Size (px)</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.slotSize ?? 40}
                  onChange={(e) => handleUpdateConfig('actionBar', { slotSize: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Columns</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.columns ?? 5}
                  onChange={(e) => handleUpdateConfig('actionBar', { columns: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Rows</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.rows ?? 2}
                  onChange={(e) => handleUpdateConfig('actionBar', { rows: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Background Color</label>
                <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                  <input
                    className="form-input"
                    type="text"
                    value={tabConfig.backgroundColor ?? '#1a1a1a'}
                    onChange={(e) => handleUpdateConfig('actionBar', { backgroundColor: e.target.value })}
                    style={{ flex: 1 }}
                  />
                  <div
                    style={{
                      width: 32,
                      height: 32,
                      backgroundColor: tabConfig.backgroundColor || '#1a1a1a',
                      borderRadius: 4,
                      border: '1px solid var(--border)',
                    }}
                  />
                </div>
              </div>
              <div className="form-group">
                <label className="form-label">Border Color</label>
                <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                  <input
                    className="form-input"
                    type="text"
                    value={tabConfig.borderColor ?? '#444444'}
                    onChange={(e) => handleUpdateConfig('actionBar', { borderColor: e.target.value })}
                    style={{ flex: 1 }}
                  />
                  <div
                    style={{
                      width: 32,
                      height: 32,
                      backgroundColor: tabConfig.borderColor || '#444444',
                      borderRadius: 4,
                      border: '1px solid var(--border)',
                    }}
                  />
                </div>
              </div>
              <div className="form-group">
                <label className="form-label">Alpha (0-1)</label>
                <input
                  className="form-input"
                  type="number"
                  min="0"
                  max="1"
                  step="0.1"
                  value={tabConfig.alpha ?? 1}
                  onChange={(e) => handleUpdateConfig('actionBar', { alpha: parseFloat(e.target.value) })}
                />
              </div>
            </div>
            {renderPreviewActionBar()}
          </>
        );

      case 'chat':
        return (
          <>
            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 16, marginBottom: 24 }}>
              <div className="form-group">
                <label className="form-label">X Position</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.x ?? 10}
                  onChange={(e) => handleUpdateConfig('chat', { x: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Y Position</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.y ?? 300}
                  onChange={(e) => handleUpdateConfig('chat', { y: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Width</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.width ?? 400}
                  onChange={(e) => handleUpdateConfig('chat', { width: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Height</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.height ?? 150}
                  onChange={(e) => handleUpdateConfig('chat', { height: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Background Color</label>
                <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                  <input
                    className="form-input"
                    type="text"
                    value={tabConfig.backgroundColor ?? '#1a1a1a'}
                    onChange={(e) => handleUpdateConfig('chat', { backgroundColor: e.target.value })}
                    style={{ flex: 1 }}
                  />
                  <div
                    style={{
                      width: 32,
                      height: 32,
                      backgroundColor: tabConfig.backgroundColor || '#1a1a1a',
                      borderRadius: 4,
                      border: '1px solid var(--border)',
                    }}
                  />
                </div>
              </div>
              <div className="form-group">
                <label className="form-label">Text Color</label>
                <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                  <input
                    className="form-input"
                    type="text"
                    value={tabConfig.textColor ?? '#ffffff'}
                    onChange={(e) => handleUpdateConfig('chat', { textColor: e.target.value })}
                    style={{ flex: 1 }}
                  />
                  <div
                    style={{
                      width: 32,
                      height: 32,
                      backgroundColor: tabConfig.textColor || '#ffffff',
                      borderRadius: 4,
                      border: '1px solid var(--border)',
                    }}
                  />
                </div>
              </div>
              <div className="form-group">
                <label className="form-label">Font Size</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.fontSize ?? 12}
                  onChange={(e) => handleUpdateConfig('chat', { fontSize: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Font Family</label>
                <input
                  className="form-input"
                  type="text"
                  value={tabConfig.fontFamily ?? 'monospace'}
                  onChange={(e) => handleUpdateConfig('chat', { fontFamily: e.target.value })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Max Messages</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.maxMessages ?? 50}
                  onChange={(e) => handleUpdateConfig('chat', { maxMessages: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Alpha (0-1)</label>
                <input
                  className="form-input"
                  type="number"
                  min="0"
                  max="1"
                  step="0.1"
                  value={tabConfig.alpha ?? 1}
                  onChange={(e) => handleUpdateConfig('chat', { alpha: parseFloat(e.target.value) })}
                />
              </div>
            </div>
            {renderPreviewChat()}
          </>
        );

      case 'inventory':
        return (
          <>
            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 16, marginBottom: 24 }}>
              <div className="form-group">
                <label className="form-label">X Position</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.x ?? 500}
                  onChange={(e) => handleUpdateConfig('inventory', { x: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Y Position</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.y ?? 100}
                  onChange={(e) => handleUpdateConfig('inventory', { y: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Slot Size (px)</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.slotSize ?? 48}
                  onChange={(e) => handleUpdateConfig('inventory', { slotSize: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Columns</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.columns ?? 5}
                  onChange={(e) => handleUpdateConfig('inventory', { columns: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Background Color</label>
                <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                  <input
                    className="form-input"
                    type="text"
                    value={tabConfig.backgroundColor ?? '#1a1a1a'}
                    onChange={(e) => handleUpdateConfig('inventory', { backgroundColor: e.target.value })}
                    style={{ flex: 1 }}
                  />
                  <div
                    style={{
                      width: 32,
                      height: 32,
                      backgroundColor: tabConfig.backgroundColor || '#1a1a1a',
                      borderRadius: 4,
                      border: '1px solid var(--border)',
                    }}
                  />
                </div>
              </div>
              <div className="form-group">
                <label className="form-label">Border Color</label>
                <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                  <input
                    className="form-input"
                    type="text"
                    value={tabConfig.borderColor ?? '#444444'}
                    onChange={(e) => handleUpdateConfig('inventory', { borderColor: e.target.value })}
                    style={{ flex: 1 }}
                  />
                  <div
                    style={{
                      width: 32,
                      height: 32,
                      backgroundColor: tabConfig.borderColor || '#444444',
                      borderRadius: 4,
                      border: '1px solid var(--border)',
                    }}
                  />
                </div>
              </div>
              <div className="form-group">
                <label className="form-label">Alpha (0-1)</label>
                <input
                  className="form-input"
                  type="number"
                  min="0"
                  max="1"
                  step="0.1"
                  value={tabConfig.alpha ?? 1}
                  onChange={(e) => handleUpdateConfig('inventory', { alpha: parseFloat(e.target.value) })}
                />
              </div>
            </div>
            {renderPreviewInventory()}
          </>
        );

      case 'castBar':
        return (
          <>
            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 16, marginBottom: 24 }}>
              <div className="form-group">
                <label className="form-label">X Position</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.x ?? 250}
                  onChange={(e) => handleUpdateConfig('castBar', { x: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Y Position</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.y ?? 200}
                  onChange={(e) => handleUpdateConfig('castBar', { y: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Width</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.width ?? 250}
                  onChange={(e) => handleUpdateConfig('castBar', { width: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Height</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.height ?? 30}
                  onChange={(e) => handleUpdateConfig('castBar', { height: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Background Color</label>
                <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                  <input
                    className="form-input"
                    type="text"
                    value={tabConfig.backgroundColor ?? '#1a1a1a'}
                    onChange={(e) => handleUpdateConfig('castBar', { backgroundColor: e.target.value })}
                    style={{ flex: 1 }}
                  />
                  <div
                    style={{
                      width: 32,
                      height: 32,
                      backgroundColor: tabConfig.backgroundColor || '#1a1a1a',
                      borderRadius: 4,
                      border: '1px solid var(--border)',
                    }}
                  />
                </div>
              </div>
              <div className="form-group">
                <label className="form-label">Bar Color</label>
                <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                  <input
                    className="form-input"
                    type="text"
                    value={tabConfig.barColor ?? '#ff6600'}
                    onChange={(e) => handleUpdateConfig('castBar', { barColor: e.target.value })}
                    style={{ flex: 1 }}
                  />
                  <div
                    style={{
                      width: 32,
                      height: 32,
                      backgroundColor: tabConfig.barColor || '#ff6600',
                      borderRadius: 4,
                      border: '1px solid var(--border)',
                    }}
                  />
                </div>
              </div>
              <div className="form-group">
                <label className="form-label">Text Color</label>
                <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                  <input
                    className="form-input"
                    type="text"
                    value={tabConfig.textColor ?? '#ffffff'}
                    onChange={(e) => handleUpdateConfig('castBar', { textColor: e.target.value })}
                    style={{ flex: 1 }}
                  />
                  <div
                    style={{
                      width: 32,
                      height: 32,
                      backgroundColor: tabConfig.textColor || '#ffffff',
                      borderRadius: 4,
                      border: '1px solid var(--border)',
                    }}
                  />
                </div>
              </div>
              <div className="form-group">
                <label className="form-label">Font Size</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.fontSize ?? 12}
                  onChange={(e) => handleUpdateConfig('castBar', { fontSize: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Font Family</label>
                <input
                  className="form-input"
                  type="text"
                  value={tabConfig.fontFamily ?? 'sans-serif'}
                  onChange={(e) => handleUpdateConfig('castBar', { fontFamily: e.target.value })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Alpha (0-1)</label>
                <input
                  className="form-input"
                  type="number"
                  min="0"
                  max="1"
                  step="0.1"
                  value={tabConfig.alpha ?? 1}
                  onChange={(e) => handleUpdateConfig('castBar', { alpha: parseFloat(e.target.value) })}
                />
              </div>
            </div>
            {renderPreviewCastBar()}
          </>
        );

      case 'nameplates':
        return (
          <>
            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 16, marginBottom: 24 }}>
              <div className="form-group">
                <label className="form-label">Offset X</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.offsetX ?? 0}
                  onChange={(e) => handleUpdateConfig('nameplates', { offsetX: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Offset Y</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.offsetY ?? -30}
                  onChange={(e) => handleUpdateConfig('nameplates', { offsetY: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Width</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.width ?? 120}
                  onChange={(e) => handleUpdateConfig('nameplates', { width: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Height</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.height ?? 40}
                  onChange={(e) => handleUpdateConfig('nameplates', { height: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Font Size</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.fontSize ?? 12}
                  onChange={(e) => handleUpdateConfig('nameplates', { fontSize: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Font Family</label>
                <input
                  className="form-input"
                  type="text"
                  value={tabConfig.fontFamily ?? 'sans-serif'}
                  onChange={(e) => handleUpdateConfig('nameplates', { fontFamily: e.target.value })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Health Bar Color</label>
                <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                  <input
                    className="form-input"
                    type="text"
                    value={tabConfig.healthBarColor ?? '#00ff00'}
                    onChange={(e) => handleUpdateConfig('nameplates', { healthBarColor: e.target.value })}
                    style={{ flex: 1 }}
                  />
                  <div
                    style={{
                      width: 32,
                      height: 32,
                      backgroundColor: tabConfig.healthBarColor || '#00ff00',
                      borderRadius: 4,
                      border: '1px solid var(--border)',
                    }}
                  />
                </div>
              </div>
              <div className="form-group">
                <label className="form-label">Mana Bar Color</label>
                <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                  <input
                    className="form-input"
                    type="text"
                    value={tabConfig.manaBarColor ?? '#0099ff'}
                    onChange={(e) => handleUpdateConfig('nameplates', { manaBarColor: e.target.value })}
                    style={{ flex: 1 }}
                  />
                  <div
                    style={{
                      width: 32,
                      height: 32,
                      backgroundColor: tabConfig.manaBarColor || '#0099ff',
                      borderRadius: 4,
                      border: '1px solid var(--border)',
                    }}
                  />
                </div>
              </div>
              <div className="form-group">
                <label className="form-label">Alpha (0-1)</label>
                <input
                  className="form-input"
                  type="number"
                  min="0"
                  max="1"
                  step="0.1"
                  value={tabConfig.alpha ?? 1}
                  onChange={(e) => handleUpdateConfig('nameplates', { alpha: parseFloat(e.target.value) })}
                />
              </div>
            </div>
            {renderNameplatePreview()}
          </>
        );

      case 'deathScreen':
        return (
          <>
            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 16, marginBottom: 24 }}>
              <div className="form-group">
                <label className="form-label">Background Color</label>
                <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                  <input
                    className="form-input"
                    type="text"
                    value={tabConfig.backgroundColor ?? '#000000'}
                    onChange={(e) => handleUpdateConfig('deathScreen', { backgroundColor: e.target.value })}
                    style={{ flex: 1 }}
                  />
                  <div
                    style={{
                      width: 32,
                      height: 32,
                      backgroundColor: tabConfig.backgroundColor || '#000000',
                      borderRadius: 4,
                      border: '1px solid var(--border)',
                    }}
                  />
                </div>
              </div>
              <div className="form-group">
                <label className="form-label">Text Color</label>
                <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                  <input
                    className="form-input"
                    type="text"
                    value={tabConfig.textColor ?? '#ff0000'}
                    onChange={(e) => handleUpdateConfig('deathScreen', { textColor: e.target.value })}
                    style={{ flex: 1 }}
                  />
                  <div
                    style={{
                      width: 32,
                      height: 32,
                      backgroundColor: tabConfig.textColor || '#ff0000',
                      borderRadius: 4,
                      border: '1px solid var(--border)',
                    }}
                  />
                </div>
              </div>
              <div className="form-group">
                <label className="form-label">Font Size</label>
                <input
                  className="form-input"
                  type="number"
                  value={tabConfig.fontSize ?? 24}
                  onChange={(e) => handleUpdateConfig('deathScreen', { fontSize: parseInt(e.target.value) })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Font Family</label>
                <input
                  className="form-input"
                  type="text"
                  value={tabConfig.fontFamily ?? 'sans-serif'}
                  onChange={(e) => handleUpdateConfig('deathScreen', { fontFamily: e.target.value })}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Button Color</label>
                <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                  <input
                    className="form-input"
                    type="text"
                    value={tabConfig.buttonColor ?? '#ff6600'}
                    onChange={(e) => handleUpdateConfig('deathScreen', { buttonColor: e.target.value })}
                    style={{ flex: 1 }}
                  />
                  <div
                    style={{
                      width: 32,
                      height: 32,
                      backgroundColor: tabConfig.buttonColor || '#ff6600',
                      borderRadius: 4,
                      border: '1px solid var(--border)',
                    }}
                  />
                </div>
              </div>
              <div className="form-group">
                <label className="form-label">Button Text Color</label>
                <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                  <input
                    className="form-input"
                    type="text"
                    value={tabConfig.buttonTextColor ?? '#ffffff'}
                    onChange={(e) => handleUpdateConfig('deathScreen', { buttonTextColor: e.target.value })}
                    style={{ flex: 1 }}
                  />
                  <div
                    style={{
                      width: 32,
                      height: 32,
                      backgroundColor: tabConfig.buttonTextColor || '#ffffff',
                      borderRadius: 4,
                      border: '1px solid var(--border)',
                    }}
                  />
                </div>
              </div>
              <div className="form-group">
                <label className="form-label">Alpha (0-1)</label>
                <input
                  className="form-input"
                  type="number"
                  min="0"
                  max="1"
                  step="0.1"
                  value={tabConfig.alpha ?? 1}
                  onChange={(e) => handleUpdateConfig('deathScreen', { alpha: parseFloat(e.target.value) })}
                />
              </div>
            </div>
            {renderDeathScreenPreview()}
          </>
        );

      default:
        return null;
    }
  };

  return (
    <div className="panel">
      <div className="panel-header">UI Layout Editor</div>
      <div className="panel-body" style={{ display: 'flex', flexDirection: 'column', height: '100%' }}>
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
          {renderTabContent()}
        </div>

        <button className="btn btn-primary" onClick={handleSave} style={{ width: '100%' }}>
          💾 Save UI Config
        </button>
      </div>
    </div>
  );
};
