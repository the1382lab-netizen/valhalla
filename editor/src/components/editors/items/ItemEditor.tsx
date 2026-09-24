import React, { useState, useMemo, useEffect } from 'react';
import { useEditorStore } from '../../../store/editorStore';

const RARITIES = ['common', 'uncommon', 'rare', 'epic', 'legendary'];
// Must match ItemCategory in shared/src/items.ts
const CATEGORIES = ['equipment', 'consumable', 'quest', 'misc'];
// Must match EquipSlotType in shared/src/items.ts
const EQUIP_SLOTS = ['weapon', 'offhand', 'helm', 'chest', 'legs', 'boots', 'gloves', 'back', 'ring'];
const WEAPON_STYLES = ['sword', 'dagger', 'greatsword', 'mace', 'bow', 'staff'];
/** Equip slots with no mesh on the character. */
const SLOTS_WITHOUT_MESH = new Set(['ring']);
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
  /** Icon file in Import/UI/Icons, e.g. sword_iron.png. */
  inventoryIcon?: string;
  /** 1.0 paperdoll layer id; still the art id fallback when meshId is empty. */
  spriteId?: string;
  /** Valhalla 2.0 art id — UE resolves the mesh by meshId if set, else spriteId. */
  meshId?: string;
  weaponStyle?: string;
  attackSpeedMs?: number;
  attackDamage?: number;
  /** Damage roll range (2.0): each auto-attack rolls between these, before stat scaling. */
  minDamage?: number;
  maxDamage?: number;
  isRangedWeapon?: boolean;
}

export const ItemEditor: React.FC = () => {
  const items = useEditorStore(s => s.items);
  const selectedItemId = useEditorStore(s => s.selectedItemId);
  const setSelectedItemId = useEditorStore(s => s.setSelectedItemId);
  const updateData = useEditorStore(s => s.updateData);
  const saveSection = useEditorStore(s => s.saveSection);

  const [searchQuery, setSearchQuery] = useState('');
  const [categoryFilter, setCategoryFilter] = useState('all');
  const [meshIds, setMeshIds] = useState<string[]>([]);
  const [iconFiles, setIconFiles] = useState<string[]>([]);

  useEffect(() => {
    fetch('/api/assets/icons')
      .then(r => r.json())
      .then(data => setIconFiles(data.files || []))
      .catch(() => setIconFiles([]));
    // Valhalla 2.0 art ids (UE equipment meshes)
    fetch('/api/assets/mesh-ids')
      .then(r => r.json())
      .then(data => setMeshIds(data.ids || []))
      .catch(() => setMeshIds([]));
  }, []);

  /** Item count per category, plus any category in the data that isn't in CATEGORIES. */
  const categoryCounts = useMemo(() => {
    const counts: Record<string, number> = {};
    for (const cat of CATEGORIES) counts[cat] = 0;
    for (const item of Object.values(items.data)) {
      const cat = item.category || 'misc';
      counts[cat] = (counts[cat] || 0) + 1;
    }
    return counts;
  }, [items.data]);

  const itemList = useMemo(() => {
    const q = searchQuery.trim().toLowerCase();
    return Object.keys(items.data)
      .filter(id => {
        const item = items.data[id];
        if (categoryFilter !== 'all' && (item.category || 'misc') !== categoryFilter) return false;
        if (!q) return true;
        return id.toLowerCase().includes(q) || (item.name || '').toLowerCase().includes(q);
      })
      .sort((a, b) => (items.data[a].name || a).localeCompare(items.data[b].name || b));
  }, [items.data, searchQuery, categoryFilter]);

  const totalItems = Object.keys(items.data).length;

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
    <div className="split-horizontal split-fill">
      <div className="panel">
        <div className="panel-header">
          Items ({itemList.length === totalItems ? totalItems : `${itemList.length} of ${totalItems}`})
        </div>
        <div className="panel-body list-body">
          <div className="list-filters">
            <div className="search-box">
              <span className="search-icon">🔍</span>
              <input
                type="text"
                placeholder="Search name or id..."
                value={searchQuery}
                onChange={(e) => setSearchQuery(e.target.value)}
              />
            </div>
            <select
              className="form-select"
              value={categoryFilter}
              onChange={(e) => setCategoryFilter(e.target.value)}
              title="Filter by category"
            >
              <option value="all">All categories ({totalItems})</option>
              {Object.keys(categoryCounts).map(cat => (
                <option key={cat} value={cat}>{cat} ({categoryCounts[cat]})</option>
              ))}
            </select>
          </div>

          <div style={{ marginBottom: 12, display: 'flex', gap: 6 }}>
            <button className="btn btn-primary" onClick={handleNewItem}>+ New</button>
            <button className="btn btn-ghost" onClick={handleDuplicateItem} disabled={!selectedItem}>Copy</button>
            <button className="btn btn-danger" onClick={handleDeleteItem} disabled={!selectedItem}>Delete</button>
          </div>

          <div className="list-scroll">
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
            {itemList.length === 0 && (
              <div className="list-empty">No items match the search or category filter.</div>
            )}
          </div>
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
                      <label className="form-label" title="Each auto-attack rolls a number in this range and adds it to the base damage before strength scaling. Unarmed is 1–3.">
                        Damage (min – max)
                      </label>
                      <div style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
                        <input
                          className="form-input"
                          type="number"
                          min={0}
                          step={1}
                          value={selectedItem.minDamage ?? selectedItem.attackDamage ?? 0}
                          onChange={(e) => {
                            const v = Math.max(0, parseInt(e.target.value, 10) || 0);
                            const max = Math.max(v, selectedItem.maxDamage ?? selectedItem.attackDamage ?? 0);
                            handleUpdateItem({ minDamage: v, maxDamage: max > 0 ? max : undefined, attackDamage: undefined });
                          }}
                        />
                        <span>–</span>
                        <input
                          className="form-input"
                          type="number"
                          min={0}
                          step={1}
                          value={selectedItem.maxDamage ?? selectedItem.attackDamage ?? 0}
                          onChange={(e) => {
                            const v = Math.max(0, parseInt(e.target.value, 10) || 0);
                            const min = Math.min(v, selectedItem.minDamage ?? selectedItem.attackDamage ?? 0);
                            handleUpdateItem({ minDamage: v > 0 ? min : undefined, maxDamage: v > 0 ? v : undefined, attackDamage: undefined });
                          }}
                        />
                      </div>
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

              {/* ── Appearance (Valhalla 2.0 mesh) ── */}
              <div className="form-group">
                <label className="form-label" style={{ fontWeight: 'bold' }}>Appearance</label>
                {!selectedItem.equipSlot && (
                  <div style={{ fontSize: '0.85em', color: '#888' }}>
                    Set an equip slot to give this item an appearance.
                  </div>
                )}
                {selectedItem.equipSlot && SLOTS_WITHOUT_MESH.has(selectedItem.equipSlot) && (
                  <div style={{ fontSize: '0.85em', color: '#888' }}>
                    The {selectedItem.equipSlot} slot has no mesh on the character.
                  </div>
                )}
                {selectedItem.equipSlot && !SLOTS_WITHOUT_MESH.has(selectedItem.equipSlot) && (
                  <>
                    <label className="form-label" style={{ fontSize: '0.85em' }}>Mesh ID</label>
                    <input
                      className="form-input"
                      type="text"
                      list="valhalla2-mesh-ids"
                      placeholder={selectedItem.spriteId || 'art id, e.g. chest_travelers_jerkin'}
                      value={selectedItem.meshId || ''}
                      onChange={(e) => handleUpdateItem({ meshId: e.target.value.trim() || undefined })}
                    />
                    <datalist id="valhalla2-mesh-ids">
                      {meshIds.map(id => <option key={id} value={id} />)}
                    </datalist>
                    <div style={{ fontSize: '0.85em', color: '#888', marginTop: 4 }}>
                      Unreal loads <code>SK_&lt;id&gt;.glb</code> / <code>SM_&lt;id&gt;.glb</code> from
                      {' '}<code>Import/Characters/Equipment/</code>.
                      {selectedItem.spriteId && !selectedItem.meshId && <> Empty uses the old art id <code>{selectedItem.spriteId}</code>.</>}
                      {meshIds.length === 0
                        ? ' Art id list unavailable — check VALHALLA2_IMPORT_DIR on the editor server.'
                        : ` ${meshIds.length} art ids available.`}
                    </div>
                    {selectedItem.meshId && meshIds.length > 0 && !meshIds.includes(selectedItem.meshId) && (
                      <div style={{ fontSize: '0.85em', color: '#ffdd88', marginTop: 4 }}>
                        ⚠ "{selectedItem.meshId}" is not one of the {meshIds.length} known art ids.
                      </div>
                    )}

                    {selectedItem.equipSlot === 'weapon' && (
                      <div style={{ marginTop: 8 }}>
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
                              {w} &rarr; {w === 'bow' ? 'shoot' : w === 'greatsword' ? 'two-handed attack' : 'attack'}
                            </option>
                          ))}
                        </select>
                      </div>
                    )}
                  </>
                )}
              </div>

              {/* ── Inventory icon ── */}
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
                    {iconFiles.map(f => <option key={f} value={f}>{f}</option>)}
                    {selectedItem.inventoryIcon && !iconFiles.includes(selectedItem.inventoryIcon) && (
                      <option value={selectedItem.inventoryIcon}>{selectedItem.inventoryIcon} (missing)</option>
                    )}
                  </select>
                  {selectedItem.inventoryIcon && (
                    <img
                      src={`/assets/icons/${selectedItem.inventoryIcon}`}
                      alt="Icon preview"
                      style={{ width: 40, height: 40, imageRendering: 'pixelated', border: '1px solid #555', borderRadius: 4, background: '#222' }}
                    />
                  )}
                </div>
                <div style={{ fontSize: '0.85em', color: '#888', marginTop: 4 }}>
                  PNGs in <code>Import/UI/Icons</code>. Unreal uses the imported copy; run <code>import_ui_icons.py</code> after adding a new one.
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
