import React, { useState, useMemo } from 'react';
import { useEditorStore } from '../../../store/editorStore';

const RARITIES = ['common', 'uncommon', 'rare', 'epic', 'legendary'];
const CATEGORIES = ['weapon', 'armor', 'accessory', 'consumable', 'misc', 'quest'];
const EQUIP_SLOTS = ['head', 'neck', 'shoulders', 'chest', 'hands', 'waist', 'legs', 'feet', 'main_hand', 'off_hand', 'two_hand', 'ring', 'trinket'];
const STAT_TYPES = ['strength', 'dexterity', 'constitution', 'intelligence', 'wisdom', 'charisma'];

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
}

export const ItemEditor: React.FC = () => {
  const items = useEditorStore(s => s.items);
  const selectedItemId = useEditorStore(s => s.selectedItemId);
  const setSelectedItemId = useEditorStore(s => s.setSelectedItemId);
  const updateData = useEditorStore(s => s.updateData);
  const saveSection = useEditorStore(s => s.saveSection);

  const [searchQuery, setSearchQuery] = useState('');

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
                  {STAT_TYPES.map(stat => (
                    <div key={stat} className="stat-row">
                      <div className="stat-label">{stat}</div>
                      <div className="stat-value">
                        <input
                          type="number"
                          value={selectedItem.statBonuses[stat] || 0}
                          onChange={(e) => handleUpdateStatBonus(stat, parseInt(e.target.value, 10) || 0)}
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
