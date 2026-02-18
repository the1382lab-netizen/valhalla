import React, { useState, useMemo } from 'react';
import { useEditorStore } from '../../../store/editorStore';

interface LootEntry {
  itemId: string;
  weight: number;
  dropChance: number;
  minQuantity: number;
  maxQuantity: number;
}

interface LootTable {
  id: string;
  name: string;
  entries: LootEntry[];
}

export const LootTableEditor: React.FC = () => {
  const lootTables = useEditorStore(s => s.lootTables);
  const items = useEditorStore(s => s.items);
  const selectedLootTableId = useEditorStore(s => s.selectedLootTableId);
  const setSelectedLootTableId = useEditorStore(s => s.setSelectedLootTableId);
  const updateData = useEditorStore(s => s.updateData);
  const saveSection = useEditorStore(s => s.saveSection);

  const [searchQuery, setSearchQuery] = useState('');

  const lootTableList = useMemo(() => {
    const ids = Object.keys(lootTables.data);
    return ids
      .filter(id => {
        const table = lootTables.data[id];
        return (
          table.name.toLowerCase().includes(searchQuery.toLowerCase()) ||
          id.toLowerCase().includes(searchQuery.toLowerCase())
        );
      })
      .sort();
  }, [lootTables.data, searchQuery]);

  const selectedTable: LootTable | null = selectedLootTableId && lootTables.data[selectedLootTableId]
    ? lootTables.data[selectedLootTableId]
    : null;

  const availableItems = useMemo(() => {
    return Object.keys(items.data || {}).sort();
  }, [items.data]);

  const handleSelectTable = (id: string) => {
    setSelectedLootTableId(id);
  };

  const handleNewTable = () => {
    const newId = `loot_${Date.now()}`;
    const newTable: LootTable = {
      id: newId,
      name: 'New Loot Table',
      entries: [],
    };
    const updatedTables = { ...lootTables.data, [newId]: newTable };
    updateData('lootTables', updatedTables);
    setSelectedLootTableId(newId);
  };

  const handleDeleteTable = () => {
    if (!selectedTable) return;
    const updatedTables = { ...lootTables.data };
    delete updatedTables[selectedLootTableId!];
    updateData('lootTables', updatedTables);
    setSelectedLootTableId(null);
  };

  const handleUpdateTable = (updates: Partial<LootTable>) => {
    if (!selectedTable) return;
    const updatedTables = {
      ...lootTables.data,
      [selectedLootTableId!]: { ...selectedTable, ...updates },
    };
    updateData('lootTables', updatedTables);
  };

  const handleAddEntry = () => {
    if (!selectedTable) return;
    const newEntry: LootEntry = {
      itemId: availableItems[0] || 'unknown',
      weight: 1,
      dropChance: 1.0,
      minQuantity: 1,
      maxQuantity: 1,
    };
    const newEntries = [...selectedTable.entries, newEntry];
    handleUpdateTable({ entries: newEntries });
  };

  const handleRemoveEntry = (index: number) => {
    if (!selectedTable) return;
    const newEntries = selectedTable.entries.filter((_, i) => i !== index);
    handleUpdateTable({ entries: newEntries });
  };

  const handleUpdateEntry = (index: number, updates: Partial<LootEntry>) => {
    if (!selectedTable) return;
    const newEntries = [...selectedTable.entries];
    newEntries[index] = { ...newEntries[index], ...updates };
    handleUpdateTable({ entries: newEntries });
  };

  const calculateEffectiveDropRate = (entry: LootEntry): number => {
    return entry.weight * entry.dropChance;
  };

  const totalWeight = selectedTable
    ? selectedTable.entries.reduce((sum, e) => sum + e.weight, 0)
    : 0;

  const getTotalDropProbability = (entry: LootEntry): number => {
    if (totalWeight === 0) return 0;
    const dropRate = entry.weight * entry.dropChance;
    return dropRate;
  };

  const handleSave = async () => {
    try {
      await saveSection('lootTables');
    } catch (err) {
      console.error('Save failed:', err);
    }
  };

  return (
    <div className="split-horizontal">
      <div className="panel">
        <div className="panel-header">Loot Tables ({lootTableList.length})</div>
        <div className="panel-body">
          <div className="search-box">
            <span className="search-icon">🔍</span>
            <input
              type="text"
              placeholder="Search loot tables..."
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
            />
          </div>

          <div style={{ marginBottom: 12, display: 'flex', gap: 6 }}>
            <button className="btn btn-primary" onClick={handleNewTable}>+ New</button>
            <button className="btn btn-danger" onClick={handleDeleteTable} disabled={!selectedTable}>
              Delete
            </button>
          </div>

          <table className="data-table">
            <thead>
              <tr>
                <th>Name</th>
                <th>Entries</th>
              </tr>
            </thead>
            <tbody>
              {lootTableList.map(id => {
                const table = lootTables.data[id];
                return (
                  <tr
                    key={id}
                    className={selectedLootTableId === id ? 'selected' : ''}
                    onClick={() => handleSelectTable(id)}
                  >
                    <td>{table.name}</td>
                    <td>{table.entries.length}</td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
      </div>

      <div className="panel">
        <div className="panel-header">Loot Table Details</div>
        <div className="panel-body" style={{ overflowY: 'auto', maxHeight: 'calc(100vh - 120px)' }}>
          {selectedTable ? (
            <>
              <div className="form-group">
                <label className="form-label">Name</label>
                <input
                  className="form-input"
                  type="text"
                  value={selectedTable.name}
                  onChange={(e) => handleUpdateTable({ name: e.target.value })}
                />
              </div>

              <div style={{ marginBottom: 16 }}>
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 12 }}>
                  <label className="form-label" style={{ marginBottom: 0 }}>
                    Entries ({selectedTable.entries.length})
                  </label>
                  <button className="btn btn-primary" onClick={handleAddEntry} style={{ padding: '4px 8px', fontSize: 12 }}>
                    + Add Entry
                  </button>
                </div>

                {selectedTable.entries.length === 0 ? (
                  <div
                    style={{
                      border: '1px dashed var(--border)',
                      borderRadius: 4,
                      padding: 16,
                      textAlign: 'center',
                      color: 'var(--text-muted)',
                    }}
                  >
                    No entries. Click "Add Entry" to create one.
                  </div>
                ) : (
                  <div style={{ overflowX: 'auto' }}>
                    <table className="data-table" style={{ width: '100%' }}>
                      <thead>
                        <tr>
                          <th style={{ width: '25%' }}>Item</th>
                          <th style={{ width: '12%' }}>Weight</th>
                          <th style={{ width: '15%' }}>Drop Chance</th>
                          <th style={{ width: '12%' }}>Min Qty</th>
                          <th style={{ width: '12%' }}>Max Qty</th>
                          <th style={{ width: '14%' }}>Rate</th>
                          <th style={{ width: '10%' }}>Action</th>
                        </tr>
                      </thead>
                      <tbody>
                        {selectedTable.entries.map((entry, index) => {
                          const effectiveRate = getTotalDropProbability(entry);
                          return (
                            <tr key={index}>
                              <td>
                                <select
                                  className="form-select"
                                  value={entry.itemId}
                                  onChange={(e) =>
                                    handleUpdateEntry(index, { itemId: e.target.value })
                                  }
                                  style={{ width: '100%' }}
                                >
                                  {availableItems.map(itemId => (
                                    <option key={itemId} value={itemId}>
                                      {items.data[itemId]?.name || itemId}
                                    </option>
                                  ))}
                                </select>
                              </td>
                              <td>
                                <input
                                  type="number"
                                  min="0"
                                  step="0.1"
                                  value={entry.weight}
                                  onChange={(e) =>
                                    handleUpdateEntry(index, {
                                      weight: parseFloat(e.target.value) || 0,
                                    })
                                  }
                                  style={{
                                    width: '100%',
                                    padding: '4px 6px',
                                    border: '1px solid var(--border)',
                                    borderRadius: 3,
                                  }}
                                />
                              </td>
                              <td>
                                <div style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
                                  <input
                                    type="number"
                                    min="0"
                                    max="1"
                                    step="0.01"
                                    value={entry.dropChance}
                                    onChange={(e) =>
                                      handleUpdateEntry(index, {
                                        dropChance: parseFloat(e.target.value) || 0,
                                      })
                                    }
                                    style={{
                                      width: '100%',
                                      padding: '4px 6px',
                                      border: '1px solid var(--border)',
                                      borderRadius: 3,
                                    }}
                                  />
                                  <span style={{ fontSize: 11, color: 'var(--text-muted)', minWidth: 30 }}>
                                    {(entry.dropChance * 100).toFixed(0)}%
                                  </span>
                                </div>
                              </td>
                              <td>
                                <input
                                  type="number"
                                  min="0"
                                  value={entry.minQuantity}
                                  onChange={(e) =>
                                    handleUpdateEntry(index, {
                                      minQuantity: parseInt(e.target.value, 10) || 0,
                                    })
                                  }
                                  style={{
                                    width: '100%',
                                    padding: '4px 6px',
                                    border: '1px solid var(--border)',
                                    borderRadius: 3,
                                  }}
                                />
                              </td>
                              <td>
                                <input
                                  type="number"
                                  min="0"
                                  value={entry.maxQuantity}
                                  onChange={(e) =>
                                    handleUpdateEntry(index, {
                                      maxQuantity: parseInt(e.target.value, 10) || 1,
                                    })
                                  }
                                  style={{
                                    width: '100%',
                                    padding: '4px 6px',
                                    border: '1px solid var(--border)',
                                    borderRadius: 3,
                                  }}
                                />
                              </td>
                              <td style={{ fontSize: 12, color: 'var(--text-accent)' }}>
                                ~{(effectiveRate * 100).toFixed(1)}%
                              </td>
                              <td>
                                <button
                                  className="btn btn-danger"
                                  onClick={() => handleRemoveEntry(index)}
                                  style={{ padding: '4px 6px', fontSize: 12 }}
                                >
                                  Remove
                                </button>
                              </td>
                            </tr>
                          );
                        })}
                      </tbody>
                    </table>
                  </div>
                )}
              </div>

              <button className="btn btn-primary" onClick={handleSave} style={{ width: '100%', marginTop: 16 }}>
                💾 Save Loot Table
              </button>
            </>
          ) : (
            <div className="empty-state">
              <div className="empty-state-icon">💰</div>
              <p>Select a loot table or create a new one</p>
            </div>
          )}
        </div>
      </div>
    </div>
  );
};
