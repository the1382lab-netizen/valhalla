import React, { useState, useMemo, useEffect } from 'react';
import { useEditorStore } from '../../../store/editorStore';
import { PaperdollPreview, usePaperdollManifest } from '../../shared/PaperdollPreview';

const RARITIES = ['common', 'uncommon', 'rare', 'epic', 'legendary'];
// Must match ItemCategory in shared/src/items.ts
const CATEGORIES = ['equipment', 'consumable', 'quest', 'misc'];
// Must match EquipSlotType in shared/src/items.ts
const EQUIP_SLOTS = ['weapon', 'offhand', 'helm', 'chest', 'legs', 'boots', 'gloves', 'back', 'ring'];
const WEAPON_STYLES = ['sword', 'greatsword', 'mace', 'bow', 'staff'];
/** Equip slot -> paperdoll layer slot; `ring` has no visual layer. */
const SLOT_TO_PAPERDOLL: Record<string, string> = {
  weapon: 'mainhand', offhand: 'offhand', helm: 'helm', chest: 'chest',
  legs: 'legs', boots: 'boots', gloves: 'gloves', back: 'back',
};
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
  meleeSpriteSheet?: string;
  rangedSpriteSheet?: string;
  castSpriteSheet?: string;
  equipSpriteConfig?: EquipSpriteConfig;
  inventoryIcon?: string;
  spriteId?: string;
  /** Valhalla 2.0 art id — UE resolves the mesh by meshId if set, else spriteId. */
  meshId?: string;
  weaponStyle?: string;
  attackSpeedMs?: number;
  attackDamage?: number;
  isRangedWeapon?: boolean;
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
  const [meshIds, setMeshIds] = useState<string[]>([]);
  const [showSpriteConfig, setShowSpriteConfig] = useState(false);
  const [showLegacySprites, setShowLegacySprites] = useState(false);
  const [previewAnim, setPreviewAnim] = useState('walk');
  const [previewDir, setPreviewDir] = useState('se');
  const paperdoll = usePaperdollManifest();

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
    // Valhalla 2.0 art ids (UE equipment meshes)
    fetch('/api/assets/mesh-ids')
      .then(r => r.json())
      .then(data => setMeshIds(data.ids || []))
      .catch(() => setMeshIds([]));
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

              {/* ── Weapon Stats (only for weapons) ──── */}
              {selectedItem.equipSlot === 'weapon' && (
                <>
                  <div className="form-row">
                    <div className="form-group">
                      <label className="form-label">Attack Damage</label>
                      <input
                        className="form-input"
                        type="number"
                        min={0}
                        step={1}
                        value={selectedItem.attackDamage ?? 0}
                        onChange={(e) => {
                          const v = parseInt(e.target.value, 10);
                          handleUpdateItem({ attackDamage: v > 0 ? v : undefined });
                        }}
                      />
                    </div>
                    <div className="form-group">
                      <label className="form-label">Attack Speed (ms)</label>
                      <input
                        className="form-input"
                        type="number"
                        min={0}
                        step={100}
                        value={selectedItem.attackSpeedMs ?? 0}
                        onChange={(e) => handleUpdateItem({ attackSpeedMs: parseInt(e.target.value, 10) || undefined })}
                      />
                    </div>
                  </div>
                  <div className="form-group">
                    <label className="form-label form-checkbox-row">
                      <input
                        type="checkbox"
                        checked={selectedItem.isRangedWeapon || false}
                        onChange={(e) => handleUpdateItem({ isRangedWeapon: e.target.checked || undefined })}
                      />
                      <span>Ranged Weapon</span>
                    </label>
                  </div>
                </>
              )}

              {/* ── Paperdoll layer ── */}
              <div className="form-group">
                <label className="form-label" style={{ fontWeight: 'bold' }}>Appearance</label>
                {!selectedItem.equipSlot && (
                  <div style={{ fontSize: '0.85em', color: '#888' }}>
                    Set an equip slot to give this item an appearance.
                  </div>
                )}
                {selectedItem.equipSlot && !SLOT_TO_PAPERDOLL[selectedItem.equipSlot] && (
                  <div style={{ fontSize: '0.85em', color: '#888' }}>
                    The {selectedItem.equipSlot} slot has no visual layer.
                  </div>
                )}
                {selectedItem.equipSlot && SLOT_TO_PAPERDOLL[selectedItem.equipSlot] && (
                  <div style={{ display: 'flex', gap: 12, alignItems: 'flex-start' }}>
                    <div style={{ flex: 1 }}>
                      <select
                        className="form-select"
                        value={selectedItem.spriteId || ''}
                        onChange={(e) => {
                          const spriteId = e.target.value || undefined;
                          handleUpdateItem({
                            spriteId,
                            // keep the inventory icon in step unless it was set by hand
                            inventoryIcon: spriteId ? `${spriteId}.png` : undefined,
                          });
                        }}
                      >
                        <option value="">None</option>
                        {(paperdoll?.items ?? [])
                          .filter(l => l.slot === SLOT_TO_PAPERDOLL[selectedItem.equipSlot!])
                          .map(l => <option key={l.id} value={l.id}>{l.label}</option>)}
                      </select>

                      {selectedItem.equipSlot === 'weapon' && (
                        <div style={{ marginTop: 6 }}>
                          <label className="form-label" style={{ fontSize: '0.85em' }}>
                            Weapon style &mdash; picks the attack animation
                          </label>
                          <select
                            className="form-select"
                            value={selectedItem.weaponStyle || ''}
                            onChange={(e) => handleUpdateItem({ weaponStyle: e.target.value || undefined })}
                          >
                            <option value="">None (melee swing)</option>
                            {WEAPON_STYLES.map(w => (
                              <option key={w} value={w}>
                                {w} &rarr; {w === 'bow' ? 'shoot' : w === 'staff' ? 'cast' : 'attack'}
                              </option>
                            ))}
                          </select>
                        </div>
                      )}

                      <div style={{ display: 'flex', gap: 6, marginTop: 8 }}>
                        <select className="form-select" style={{ flex: 1 }}
                                value={previewAnim} onChange={e => setPreviewAnim(e.target.value)}>
                          {(paperdoll?.animations ?? []).map(a => (
                            <option key={a.name} value={a.name}>{a.name}</option>
                          ))}
                        </select>
                        <select className="form-select" style={{ flex: 1 }}
                                value={previewDir} onChange={e => setPreviewDir(e.target.value)}>
                          {(paperdoll?.directions ?? []).map(d => (
                            <option key={d} value={d}>{d.toUpperCase()}</option>
                          ))}
                        </select>
                      </div>
                    </div>

                    <PaperdollPreview
                      manifest={paperdoll}
                      layerId={selectedItem.spriteId}
                      anim={previewAnim}
                      dir={previewDir}
                      scale={2}
                    />
                  </div>
                )}
              </div>

              {/* ── Valhalla 2.0 mesh id ── */}
              <div className="form-group">
                <label className="form-label">Mesh ID (Valhalla 2.0)</label>
                <input
                  className="form-input"
                  type="text"
                  list="valhalla2-mesh-ids"
                  placeholder={selectedItem.spriteId || 'defaults to spriteId'}
                  value={selectedItem.meshId || ''}
                  onChange={(e) => handleUpdateItem({ meshId: e.target.value.trim() || undefined })}
                />
                <datalist id="valhalla2-mesh-ids">
                  {meshIds.map(id => <option key={id} value={id} />)}
                </datalist>
                <div style={{ fontSize: '0.85em', color: '#888', marginTop: 4 }}>
                  Defaults to spriteId. Unreal loads <code>SK_&lt;id&gt;.glb</code> / <code>SM_&lt;id&gt;.glb</code> from
                  {' '}<code>Import/Characters/Equipment/</code>.
                  {meshIds.length === 0
                    ? ' Art id list unavailable — check VALHALLA2_IMPORT_DIR on the editor server.'
                    : ` ${meshIds.length} art ids available.`}
                </div>
                {selectedItem.meshId && meshIds.length > 0 && !meshIds.includes(selectedItem.meshId) && (
                  <div style={{ fontSize: '0.85em', color: '#ffdd88', marginTop: 4 }}>
                    ⚠ "{selectedItem.meshId}" is not one of the {meshIds.length} known art ids.
                  </div>
                )}
              </div>

              {/* ── Legacy LPC sheets ── */}
              <div className="form-group">
                <label className="form-label" style={{ fontWeight: 'bold', cursor: 'pointer' }}
                       onClick={() => setShowLegacySprites(v => !v)}>
                  {showLegacySprites ? '\u25be' : '\u25b8'} Legacy LPC sprite sheets
                </label>
                <div style={{ fontSize: '0.8em', color: '#888', marginBottom: 6 }}>
                  Only used when this item has no paperdoll layer above.
                </div>
              </div>
              {showLegacySprites && (
              <div className="form-group">
                <label className="form-label" style={{ fontWeight: 'bold' }}>Equipment Sprite Sheets</label>
                {([
                  ['Walk / Idle', 'equipSpriteSheet'],
                  ['Melee', 'meleeSpriteSheet'],
                  ['Ranged', 'rangedSpriteSheet'],
                  ['Cast', 'castSpriteSheet'],
                ] as [string, string][]).map(([label, field]) => (
                  <div key={field} style={{ marginBottom: 6 }}>
                    <label className="form-label" style={{ fontSize: '0.85em' }}>{label}</label>
                    <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                      <select
                        className="form-select"
                        value={(selectedItem as any)[field] || ''}
                        onChange={(e) => handleUpdateItem({ [field]: e.target.value || undefined })}
                      >
                        <option value="">None</option>
                        {equipmentSprites.map(f => (
                          <option key={f} value={f}>{f}</option>
                        ))}
                      </select>
                      {(selectedItem as any)[field] && (
                        <div style={{
                          width: 32,
                          height: 32,
                          overflow: 'hidden',
                          border: '1px solid #555',
                          borderRadius: 4,
                          background: '#222',
                          flexShrink: 0,
                        }}>
                          <img
                            src={`/assets/sprites/equipment/${(selectedItem as any)[field]}`}
                            alt={`${label} preview`}
                            style={{
                              imageRendering: 'pixelated',
                              width: (selectedItem.equipSpriteConfig?.framesPerRow ?? 9) * (selectedItem.equipSpriteConfig?.frameWidth ?? 64),
                              height: 4 * (selectedItem.equipSpriteConfig?.frameHeight ?? 64),
                              objectFit: 'none',
                              objectPosition: '0 0',
                              maxWidth: 'none',
                            }}
                          />
                        </div>
                      )}
                    </div>
                  </div>
                ))}
                {/* Sprite Config (shared across all sheets) */}
                <button
                  className="btn btn-ghost"
                  style={{ marginTop: 4, fontSize: '0.8em' }}
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
