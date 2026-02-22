import React, { useState, useMemo, useEffect } from 'react';
import { useEditorStore } from '../../../store/editorStore';

const RARITIES = ['common', 'uncommon', 'rare', 'epic', 'legendary'];
const CATEGORIES = ['weapon', 'armor', 'accessory', 'consumable', 'misc', 'quest'];
const EQUIP_SLOTS = ['weapon', 'helm', 'chest', 'legs', 'boots', 'ring'];
const STAT_TYPES: { key: string; label: string; step: string }[] = [
  { key: 'hp',              label: 'HP',               step: '1'    },
  { key: 'mana',            label: 'Mana',             step: '1'    },
  { key: 'strength',        label: 'Strength',         step: '1'    },
  { key: 'stamina',         label: 'Stamina',          step: '1'    },
  { key: 'dexterity',       label: 'Dexterity',        step: '1'    },
  { key: 'intelligence',    label: 'Intelligence',     step: '1'    },
  { key: 'wisdom',          label: 'Wisdom',           step: '1'    },
  { key: 'physicalResist',  label: 'Physical Resist',  step: '1'    },
  { key: 'spellResist',     label: 'Spell Resist',     step: '1'    },
  { key: 'critChance',      label: 'Crit Chance',      step: '0.01' },
  { key: 'critDamage',      label: 'Crit Damage',      step: '0.01' },
  { key: 'physicalDefense', label: 'Physical Defense', step: '1'    },
  { key: 'blockRating',     label: 'Block Rating',     step: '0.01' },
  { key: 'dodgeRating',     label: 'Dodge Rating',     step: '0.01' },
];

interface EquipSpriteConfig {
  frameWidth: number;
  frameHeight: number;
  framesPerRow: number;
  rows: number;
}

interface ItemTemplate {
  id: string;
  name: string;
  description: string;
  category: string;
  rarity: string;
  equipSlot?: string;
  stackable: boolean;
  maxStack: number;
  statBonuses: Record<string, number>;
  equipSpriteSheet?: string;
  equipSpriteConfig?: EquipSpriteConfig;
  inventoryIcon?: string;
}

const DEFAULT_SPRITE_CONFIG: EquipSpriteConfig = {
  frameWidth: 64,
  frameHeight: 64,
  framesPerRow: 9,
  rows: 4,
};

export const ItemEditor: React.FC = () => {
  const items = useEditorStore(s => s.items);
  const selectedItemId = useEditorStore(s => s.selectedItemId);
  const setSelectedItemId = useEditorStore(s => s.setSelectedItemId);
  const updateData = useEditorStore(s => s.updateData);
  const saveSection = useEditorStore(s => s.saveSection);

  const [searchQuery, setSearchQuery] = useState('');
  const [equipmentSprites, setEquipmentSprites] = useState<string[]>([]);
  const [iconFiles, setIconFiles] = useState<string[]>([]);
  const [showSpriteConfig, setShowSpriteConfig] = useState(false);

  // Fetch available sprite sheets and icons from the server
  useEffect(() => {
    fetch('/api/assets/sprites/equipment')
      .then(r => r.json())
      .then(data => setEquipmentSprites(data.files || []))
      .catch(() => setEquipmentSprites([]));
    fetch('/api/assets/sprites/icons')
      .then(r => r.json())
      .then(data => setIconFiles(data.files || []))
      .catch(() => setIconFiles([]));
  }, []);

  const itemList = useMemo(() => {
    const ids = Object.keys(items.data);
    return ids.filter(id => id.toLowerCase().includes(searchQuery.toLowerCase())).sort();
  }, [items.data, searchQuery]);

  const selectedItem: ItemTemplate | null = selectedItemId && items.data[selectedItemId]
    ? items.data[selectedItemId]
    : null;

  const handleSelectItem = (id: string) => {
    setSelectedItemId(id);
  };

  const handleNewItem = () => {
    const newId = `item_${Date.now()}`;
    const newItem: ItemTemplate = {
      id: newId,
      name: 'New Item',
      description: '',
      category: 'misc',
      rarity: 'common',
      equipSlot: undefined,
      stackable: false,
      maxStack: 1,
      statBonuses: {},
    };
    const updatedItems = { ...items.data, [newId]: newItem };
    updateData('items', updatedItems);
    setSelectedItemId(newId);
  };

  const handleDuplicateItem = () => {
    if (!selectedItem) return;
    const newId = `item_${Date.now()}`;
    const duplicated: ItemTemplate = {
      ...selectedItem,
      id: newId,
      name: `${selectedItem.name} (copy)`,
    };
    const updatedItems = { ...items.data, [newId]: duplicated };
    updateData('items', updatedItems);
    setSelectedItemId(newId);
  };

  const handleDeleteItem = () => {
    if (!selectedItem) return;
    const updatedItems = { ...items.data };
    delete updatedItems[selectedItemId!];
    updateData('items', updatedItems);
    setSelectedItemId(null);
  };

  const handleUpdateItem = (updates: Partial<ItemTemplate>) => {
    if (!selectedItem) return;
    const updatedItems = {
      ...items.data,
      [selectedItemId!]: { ...selectedItem, ...updates },
    };
    updateData('items', updatedItems);
  };

  const handleUpdateSpriteConfig = (key: keyof EquipSpriteConfig, value: number) => {
    if (!selectedItem) return;
    const current = selectedItem.equipSpriteConfig || { ...DEFAULT_SPRITE_CONFIG };
    handleUpdateItem({
      equipSpriteConfig: { ...current, [key]: value },
    });
  };

  const handleUpdateStatBonus = (stat: string, value: number) => {
    if (!selectedItem) return;
    const newBonuses = { ...selectedItem.statBonuses };
    if (value === 0) {
      delete newBonuses[stat];
    } else {
      newBonuses[stat] = value;
    }
    handleUpdateItem({ statBonuses: newBonuses });
  };

  const handleSave = async () => {
    try {
      await saveSection('items');
    } catch (err) {
      console.error('Save failed:', err);
    }
  };

  return (
    <div className="split-horizontal">
      <div className="panel">
        <div className="panel-header">Items ({itemList.length})</div>
        <div className="panel-body">
          <div className="search-box">
            <span className="search-icon">🔍</span>
            <input
              type="text"
              placeholder="Search items..."
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
            />
          </div>

          <div style={{ marginBottom: 12, display: 'flex', gap: 6 }}>
            <button className="btn btn-primary" onClick={handleNewItem}>+ New</button>
            <button className="btn btn-ghost" onClick={handleDuplicateItem} disabled={!selectedItem}>Copy</button>
            <button className="btn btn-danger" onClick={handleDeleteItem} disabled={!selectedItem}>Delete</button>
          </div>

          <table className="data-table">
            <thead>
              <tr>
                <th>Name</th>
                <th>Category</th>
              </tr>
            </thead>
            <tbody>
              {itemList.map(id => {
                const item = items.data[id];
                return (
                  <tr
                    key={id}
                    className={selectedItemId === id ? 'selected' : ''}
                    onClick={() => handleSelectItem(id)}
                  >
                    <td>{item.name}</td>
                    <td>{item.category}</td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
      </div>

      <div className="panel">
        <div className="panel-header">Item Details</div>
        <div className="panel-body">
          {selectedItem ? (
            <>
              <div className="form-group">
                <label className="form-label">Name</label>
                <input
                  className="form-input"
                  type="text"
                  value={selectedItem.name}
                  onChange={(e) => handleUpdateItem({ name: e.target.value })}
                />
              </div>

              <div className="form-group">
                <label className="form-label">Description</label>
                <textarea
                  className="form-input"
                  value={selectedItem.description}
                  onChange={(e) => handleUpdateItem({ description: e.target.value })}
                />
              </div>

              <div className="form-row">
                <div className="form-group">
                  <label className="form-label">Category</label>
                  <select
                    className="form-select"
                    value={selectedItem.category}
                    onChange={(e) => handleUpdateItem({ category: e.target.value })}
                  >
                    {CATEGORIES.map(cat => (
                      <option key={cat} value={cat}>{cat}</option>
                    ))}
                  </select>
                </div>

                <div className="form-group">
                  <label className="form-label">Rarity</label>
                  <select
                    className="form-select"
                    value={selectedItem.rarity}
                    onChange={(e) => handleUpdateItem({ rarity: e.target.value })}
                  >
                    {RARITIES.map(r => (
                      <option key={r} value={r}>{r}</option>
                    ))}
                  </select>
                </div>
              </div>

              <div className="form-group">
                <label className="form-label">Equip Slot (optional)</label>
                <select
                  className="form-select"
                  value={selectedItem.equipSlot || ''}
                  onChange={(e) => handleUpdateItem({ equipSlot: e.target.value || undefined })}
                >
                  <option value="">None</option>
                  {EQUIP_SLOTS.map(slot => (
                    <option key={slot} value={slot}>{slot}</option>
                  ))}
                </select>
              </div>

              {/* ── Equipment Sprite Sheet Overlay ──────────── */}
              <div className="form-group">
                <label className="form-label">Equipment Sprite Sheet</label>
                <select
                  className="form-select"
                  value={selectedItem.equipSpriteSheet || ''}
                  onChange={(e) => handleUpdateItem({ equipSpriteSheet: e.target.value || undefined })}
                >
                  <option value="">None</option>
                  {equipmentSprites.map(f => (
                    <option key={f} value={f}>{f}</option>
                  ))}
                </select>
                {selectedItem.equipSpriteSheet && (
                  <div style={{ marginTop: 8 }}>
                    <div style={{
                      display: 'inline-block',
                      width: 64,
                      height: 64,
                      overflow: 'hidden',
                      border: '1px solid #555',
                      borderRadius: 4,
                      background: '#222',
                    }}>
                      <img
                        src={`/assets/sprites/equipment/${selectedItem.equipSpriteSheet}`}
                        alt="Sprite preview"
                        style={{
                          imageRendering: 'pixelated',
                          width: (selectedItem.equipSpriteConfig?.framesPerRow ?? 9) * (selectedItem.equipSpriteConfig?.frameWidth ?? 64),
                          height: (selectedItem.equipSpriteConfig?.rows ?? 4) * (selectedItem.equipSpriteConfig?.frameHeight ?? 64),
                          objectFit: 'none',
                          objectPosition: '0 0',
                          maxWidth: 'none',
                        }}
                      />
                    </div>
                    <button
                      className="btn btn-ghost"
                      style={{ marginLeft: 8, fontSize: '0.8em' }}
                      onClick={() => setShowSpriteConfig(!showSpriteConfig)}
                    >
                      {showSpriteConfig ? '▼ Hide Config' : '▶ Sprite Config'}
                    </button>
                    {showSpriteConfig && (
                      <div className="stat-grid" style={{ marginTop: 8 }}>
                        {(['frameWidth', 'frameHeight', 'framesPerRow', 'rows'] as const).map(key => (
                          <div key={key} className="stat-row">
                            <div className="stat-label">{key}</div>
                            <div className="stat-value">
                              <input
                                type="number"
                                min="1"
                                value={(selectedItem.equipSpriteConfig || DEFAULT_SPRITE_CONFIG)[key]}
                                onChange={(e) => handleUpdateSpriteConfig(key, parseInt(e.target.value, 10) || 1)}
                              />
                            </div>
                          </div>
                        ))}
                      </div>
                    )}
                  </div>
                )}
              </div>

              {/* ── Inventory Icon ──────────────────────────── */}
              <div className="form-group">
                <label className="form-label">Inventory Icon</label>
                <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                  <select
                    className="form-select"
                    style={{ flex: 1 }}
                    value={selectedItem.inventoryIcon || ''}
                    onChange={(e) => handleUpdateItem({ inventoryIcon: e.target.value || undefined })}
                  >
                    <option value="">None</option>
                    {iconFiles.map(f => (
                      <option key={f} value={f}>{f}</option>
                    ))}
                  </select>
                  {selectedItem.inventoryIcon && (
                    <div style={{
                      width: 48,
                      height: 48,
                      border: '1px solid #555',
                      borderRadius: 4,
                      background: '#222',
                      display: 'flex',
                      alignItems: 'center',
                      justifyContent: 'center',
                      flexShrink: 0,
                    }}>
                      <img
                        src={`/assets/sprites/icons/${selectedItem.inventoryIcon}`}
                        alt="Icon preview"
                        style={{
                          maxWidth: 48,
                          maxHeight: 48,
                          imageRendering: 'pixelated',
                        }}
                      />
                    </div>
                  )}
                </div>
              </div>

              <div className="form-row">
                <div className="form-group">
                  <label className="form-label">Stackable</label>
                  <div className="form-checkbox-row">
                    <input
                      type="checkbox"
                      checked={selectedItem.stackable}
                      onChange={(e) => handleUpdateItem({ stackable: e.target.checked })}
                    />
                    <span>{selectedItem.stackable ? 'Yes' : 'No'}</span>
                  </div>
                </div>

                <div className="form-group">
                  <label className="form-label">Max Stack</label>
                  <input
                    className="form-input"
                    type="number"
                    min="1"
                    value={selectedItem.maxStack}
                    onChange={(e) => handleUpdateItem({ maxStack: parseInt(e.target.value, 10) })}
                    disabled={!selectedItem.stackable}
                  />
                </div>
              </div>

              <div className="form-group">
                <label className="form-label">Stat Bonuses</label>
                <div className="stat-grid">
                  {STAT_TYPES.map(({ key, label, step }) => (
                    <div key={key} className="stat-row">
                      <div className="stat-label">{label}</div>
                      <div className="stat-value">
                        <input
                          type="number"
                          step={step}
                          value={selectedItem.statBonuses[key] ?? 0}
                          onChange={(e) => handleUpdateStatBonus(key, parseFloat(e.target.value) || 0)}
                        />
                      </div>
                    </div>
                  ))}
                </div>
              </div>

              <button className="btn btn-primary" onClick={handleSave} style={{ width: '100%', marginTop: 16 }}>
                💾 Save Item
              </button>
            </>
          ) : (
            <div className="empty-state">
              <div className="empty-state-icon">📋</div>
              <p>Select an item or create a new one</p>
            </div>
          )}
        </div>
      </div>
    </div>
  );
};
