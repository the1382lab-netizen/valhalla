import React, { useState, useMemo } from 'react';
import { useEditorStore } from '../../../store/editorStore';

interface NPCTemplate {
  id: string;
  name: string;
  description: string;
  type: 'enemy' | 'npc';
  level: number;
  stats: Partial<Record<string, number>>;
  hp: number;
  mana?: number;
  lootTableId?: string;
  behaviorType: 'passive' | 'aggressive' | 'patrol' | 'stationary' | 'fleeing';
  canAggro?: boolean;
  aggroRange?: number;
  leashRange?: number;
  damage?: number;
  attackSpeed?: number;
  attackRange?: number;
  moveSpeed?: number;
  respawnMs: number;
  skills: string[];
  spriteColor: number;
  spriteSize: number;
  xpReward: number;
  dialogue?: string[];
  vendorInventory?: string[];
}

const BEHAVIOR_TYPES = ['passive', 'aggressive', 'patrol', 'stationary', 'fleeing'];

const numberToHex = (num: number): string => {
  return '#' + num.toString(16).padStart(6, '0');
};

const hexToNumber = (hex: string): number => {
  return parseInt(hex.replace('#', ''), 16);
};

export const NPCEditor: React.FC = () => {
  const npcTemplates = useEditorStore(s => s.npcTemplates);
  const items = useEditorStore(s => s.items);
  const skills = useEditorStore(s => s.skills);
  const lootTables = useEditorStore(s => s.lootTables);
  const selectedNpcId = useEditorStore(s => s.selectedNpcId);
  const setSelectedNpcId = useEditorStore(s => s.setSelectedNpcId);
  const updateData = useEditorStore(s => s.updateData);
  const saveSection = useEditorStore(s => s.saveSection);

  const [searchQuery, setSearchQuery] = useState('');
  const [skillDropdownOpen, setSkillDropdownOpen] = useState(false);
  const [newSkillFilter, setNewSkillFilter] = useState('');

  const npcList = useMemo(() => {
    const ids = Object.keys(npcTemplates.data);
    return ids
      .filter(id => {
        const npc = npcTemplates.data[id];
        return (
          npc.name.toLowerCase().includes(searchQuery.toLowerCase()) ||
          id.toLowerCase().includes(searchQuery.toLowerCase())
        );
      })
      .sort();
  }, [npcTemplates.data, searchQuery]);

  const selectedNpc: NPCTemplate | null = selectedNpcId && npcTemplates.data[selectedNpcId]
    ? npcTemplates.data[selectedNpcId]
    : null;

  const availableSkills = useMemo(() => {
    return Object.keys(skills.data.skills || {}).sort();
  }, [skills.data.skills]);

  const filteredAvailableSkills = useMemo(() => {
    return availableSkills.filter(
      id => id.toLowerCase().includes(newSkillFilter.toLowerCase()) &&
        (!selectedNpc || !selectedNpc.skills.includes(id))
    );
  }, [availableSkills, newSkillFilter, selectedNpc]);

  const availableItems = useMemo(() => {
    return Object.keys(items.data || {}).sort();
  }, [items.data]);

  const availableLootTables = useMemo(() => {
    return Object.keys(lootTables.data || {}).sort();
  }, [lootTables.data]);

  const handleSelectNpc = (id: string) => {
    setSelectedNpcId(id);
  };

  const handleNewNpc = () => {
    const newId = `npc_${Date.now()}`;
    const newNpc: NPCTemplate = {
      id: newId,
      name: 'New NPC',
      description: '',
      type: 'npc',
      level: 1,
      stats: {},
      hp: 10,
      mana: 0,
      lootTableId: undefined,
      behaviorType: 'passive',
      aggroRange: undefined,
      respawnMs: 60000,
      skills: [],
      spriteColor: 0xffffff,
      spriteSize: 1,
      xpReward: 0,
      dialogue: [],
      vendorInventory: [],
    };
    const updatedNpcs = { ...npcTemplates.data, [newId]: newNpc };
    updateData('npcTemplates', updatedNpcs);
    setSelectedNpcId(newId);
  };

  const handleDuplicateNpc = () => {
    if (!selectedNpc) return;
    const newId = `npc_${Date.now()}`;
    const duplicated: NPCTemplate = {
      ...selectedNpc,
      id: newId,
      name: `${selectedNpc.name} (copy)`,
    };
    const updatedNpcs = { ...npcTemplates.data, [newId]: duplicated };
    updateData('npcTemplates', updatedNpcs);
    setSelectedNpcId(newId);
  };

  const handleDeleteNpc = () => {
    if (!selectedNpc) return;
    const updatedNpcs = { ...npcTemplates.data };
    delete updatedNpcs[selectedNpcId!];
    updateData('npcTemplates', updatedNpcs);
    setSelectedNpcId(null);
  };

  const handleUpdateNpc = (updates: Partial<NPCTemplate>) => {
    if (!selectedNpc) return;
    const updatedNpcs = {
      ...npcTemplates.data,
      [selectedNpcId!]: { ...selectedNpc, ...updates },
    };
    updateData('npcTemplates', updatedNpcs);
  };

  const handleAddSkill = (skillId: string) => {
    if (!selectedNpc || selectedNpc.skills.includes(skillId)) return;
    const newSkills = [...selectedNpc.skills, skillId];
    handleUpdateNpc({ skills: newSkills });
    setNewSkillFilter('');
  };

  const handleRemoveSkill = (skillId: string) => {
    if (!selectedNpc) return;
    const newSkills = selectedNpc.skills.filter(s => s !== skillId);
    handleUpdateNpc({ skills: newSkills });
  };

  const handleUpdateDialogue = (text: string) => {
    if (!selectedNpc) return;
    const lines = text.split('\n').filter(line => line.trim());
    handleUpdateNpc({ dialogue: lines });
  };

  const handleUpdateVendorInventory = (itemId: string, add: boolean) => {
    if (!selectedNpc) return;
    const current = selectedNpc.vendorInventory || [];
    const updated = add
      ? [...current, itemId]
      : current.filter(id => id !== itemId);
    handleUpdateNpc({ vendorInventory: updated });
  };

  const handleSave = async () => {
    try {
      await saveSection('npcTemplates');
    } catch (err) {
      console.error('Save failed:', err);
    }
  };

  return (
    <div className="split-horizontal">
      <div className="panel">
        <div className="panel-header">NPCs & Enemies ({npcList.length})</div>
        <div className="panel-body">
          <div className="search-box">
            <span className="search-icon">🔍</span>
            <input
              type="text"
              placeholder="Search NPCs..."
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
            />
          </div>

          <div style={{ marginBottom: 12, display: 'flex', gap: 6 }}>
            <button className="btn btn-primary" onClick={handleNewNpc}>+ New</button>
            <button className="btn btn-ghost" onClick={handleDuplicateNpc} disabled={!selectedNpc}>
              Copy
            </button>
            <button className="btn btn-danger" onClick={handleDeleteNpc} disabled={!selectedNpc}>
              Delete
            </button>
          </div>

          <table className="data-table">
            <thead>
              <tr>
                <th>Name</th>
                <th>Type</th>
                <th>Level</th>
                <th>Behavior</th>
              </tr>
            </thead>
            <tbody>
              {npcList.map(id => {
                const npc = npcTemplates.data[id];
                return (
                  <tr
                    key={id}
                    className={selectedNpcId === id ? 'selected' : ''}
                    onClick={() => handleSelectNpc(id)}
                  >
                    <td>{npc.name}</td>
                    <td>{npc.type}</td>
                    <td>{npc.level}</td>
                    <td>{npc.behaviorType}</td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
      </div>

      <div className="panel">
        <div className="panel-header">NPC Details</div>
        <div className="panel-body" style={{ overflowY: 'auto', maxHeight: 'calc(100vh - 120px)' }}>
          {selectedNpc ? (
            <>
              <div className="form-group">
                <label className="form-label">Name</label>
                <input
                  className="form-input"
                  type="text"
                  value={selectedNpc.name}
                  onChange={(e) => handleUpdateNpc({ name: e.target.value })}
                />
              </div>

              <div className="form-group">
                <label className="form-label">Description</label>
                <textarea
                  className="form-input"
                  value={selectedNpc.description}
                  onChange={(e) => handleUpdateNpc({ description: e.target.value })}
                  rows={3}
                />
              </div>

              <div className="form-row">
                <div className="form-group">
                  <label className="form-label">Type</label>
                  <select
                    className="form-select"
                    value={selectedNpc.type}
                    onChange={(e) => handleUpdateNpc({ type: e.target.value as 'enemy' | 'npc' })}
                  >
                    <option value="enemy">Enemy</option>
                    <option value="npc">NPC</option>
                  </select>
                </div>

                <div className="form-group">
                  <label className="form-label">Level</label>
                  <input
                    className="form-input"
                    type="number"
                    min="1"
                    value={selectedNpc.level}
                    onChange={(e) => handleUpdateNpc({ level: parseInt(e.target.value, 10) || 1 })}
                  />
                </div>
              </div>

              <div className="form-row">
                <div className="form-group">
                  <label className="form-label">HP</label>
                  <input
                    className="form-input"
                    type="number"
                    min="1"
                    value={selectedNpc.hp}
                    onChange={(e) => handleUpdateNpc({ hp: parseInt(e.target.value, 10) || 1 })}
                  />
                </div>

                <div className="form-group">
                  <label className="form-label">Mana</label>
                  <input
                    className="form-input"
                    type="number"
                    min="0"
                    value={selectedNpc.mana || 0}
                    onChange={(e) => handleUpdateNpc({ mana: parseInt(e.target.value, 10) || 0 })}
                  />
                </div>
              </div>

              <div className="form-row">
                <div className="form-group">
                  <label className="form-label">Behavior Type</label>
                  <select
                    className="form-select"
                    value={selectedNpc.behaviorType}
                    onChange={(e) =>
                      handleUpdateNpc({
                        behaviorType: e.target.value as any,
                      })
                    }
                  >
                    {BEHAVIOR_TYPES.map(type => (
                      <option key={type} value={type}>
                        {type}
                      </option>
                    ))}
                  </select>
                </div>

                <div className="form-group">
                  <label className="form-label">Aggro Range</label>
                  <input
                    className="form-input"
                    type="number"
                    min="0"
                    value={selectedNpc.aggroRange || 0}
                    onChange={(e) => handleUpdateNpc({ aggroRange: parseInt(e.target.value, 10) || 0 })}
                  />
                </div>
              </div>

              <div className="form-row">
                <div className="form-group">
                  <label className="form-label">
                    <input
                      type="checkbox"
                      checked={selectedNpc.canAggro ?? (selectedNpc.type === 'enemy')}
                      onChange={(e) => handleUpdateNpc({ canAggro: e.target.checked })}
                      style={{ marginRight: 6 }}
                    />
                    Can Aggro
                  </label>
                  <div style={{ fontSize: 12, color: 'var(--text-muted)', marginTop: 4 }}>
                    Default: on for enemies, off for friendly NPCs
                  </div>
                </div>

                <div className="form-group">
                  <label className="form-label">Leash Range</label>
                  <input
                    className="form-input"
                    type="number"
                    min="0"
                    value={selectedNpc.leashRange || 0}
                    onChange={(e) => handleUpdateNpc({ leashRange: parseInt(e.target.value, 10) || 0 })}
                  />
                  <div style={{ fontSize: 12, color: 'var(--text-muted)', marginTop: 4 }}>
                    0 = auto (aggroRange × 3)
                  </div>
                </div>
              </div>

              <div className="form-row">
                <div className="form-group">
                  <label className="form-label">Damage</label>
                  <input
                    className="form-input"
                    type="number"
                    min="0"
                    value={selectedNpc.damage || 0}
                    onChange={(e) => handleUpdateNpc({ damage: parseInt(e.target.value, 10) || 0 })}
                  />
                </div>

                <div className="form-group">
                  <label className="form-label">Attack Speed (ms)</label>
                  <input
                    className="form-input"
                    type="number"
                    min="0"
                    value={selectedNpc.attackSpeed || 0}
                    onChange={(e) => handleUpdateNpc({ attackSpeed: parseInt(e.target.value, 10) || 0 })}
                  />
                  <div style={{ fontSize: 12, color: 'var(--text-muted)', marginTop: 4 }}>
                    0 = default (1500ms)
                  </div>
                </div>
              </div>

              <div className="form-row">
                <div className="form-group">
                  <label className="form-label">Attack Range (px)</label>
                  <input
                    className="form-input"
                    type="number"
                    min="0"
                    value={selectedNpc.attackRange || 0}
                    onChange={(e) => handleUpdateNpc({ attackRange: parseInt(e.target.value, 10) || 0 })}
                  />
                  <div style={{ fontSize: 12, color: 'var(--text-muted)', marginTop: 4 }}>
                    0 = default (40px)
                  </div>
                </div>

                <div className="form-group">
                  <label className="form-label">Move Speed (px/s)</label>
                  <input
                    className="form-input"
                    type="number"
                    min="0"
                    value={selectedNpc.moveSpeed || 0}
                    onChange={(e) => handleUpdateNpc({ moveSpeed: parseInt(e.target.value, 10) || 0 })}
                  />
                  <div style={{ fontSize: 12, color: 'var(--text-muted)', marginTop: 4 }}>
                    0 = default (60 px/s)
                  </div>
                </div>
              </div>

              <div className="form-row">
                <div className="form-group">
                  <label className="form-label">Respawn (ms)</label>
                  <input
                    className="form-input"
                    type="number"
                    min="0"
                    value={selectedNpc.respawnMs}
                    onChange={(e) => handleUpdateNpc({ respawnMs: parseInt(e.target.value, 10) || 60000 })}
                  />
                  <div style={{ fontSize: 12, color: 'var(--text-muted)', marginTop: 4 }}>
                    {(selectedNpc.respawnMs / 1000).toFixed(1)}s
                  </div>
                </div>

                <div className="form-group">
                  <label className="form-label">XP Reward</label>
                  <input
                    className="form-input"
                    type="number"
                    min="0"
                    value={selectedNpc.xpReward}
                    onChange={(e) => handleUpdateNpc({ xpReward: parseInt(e.target.value, 10) || 0 })}
                  />
                </div>
              </div>

              <div className="form-row">
                <div className="form-group">
                  <label className="form-label">Sprite Color</label>
                  <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                    <input
                      className="form-input"
                      type="text"
                      value={numberToHex(selectedNpc.spriteColor)}
                      onChange={(e) => {
                        try {
                          const num = hexToNumber(e.target.value);
                          handleUpdateNpc({ spriteColor: num });
                        } catch {}
                      }}
                      style={{ flex: 1 }}
                    />
                    <div
                      className="color-swatch"
                      style={{
                        backgroundColor: numberToHex(selectedNpc.spriteColor),
                        width: 32,
                        height: 32,
                        borderRadius: 4,
                        border: '1px solid var(--border)',
                      }}
                    />
                  </div>
                </div>

                <div className="form-group">
                  <label className="form-label">Sprite Size</label>
                  <input
                    className="form-input"
                    type="number"
                    min="0.1"
                    step="0.1"
                    value={selectedNpc.spriteSize}
                    onChange={(e) => handleUpdateNpc({ spriteSize: parseFloat(e.target.value) || 1 })}
                  />
                </div>
              </div>

              <div className="form-group">
                <label className="form-label">Loot Table</label>
                <select
                  className="form-select"
                  value={selectedNpc.lootTableId || ''}
                  onChange={(e) => handleUpdateNpc({ lootTableId: e.target.value || undefined })}
                >
                  <option value="">None</option>
                  {availableLootTables.map(id => (
                    <option key={id} value={id}>
                      {lootTables.data[id]?.name || id}
                    </option>
                  ))}
                </select>
              </div>

              <div className="form-group">
                <label className="form-label">Skills</label>
                <div
                  style={{
                    border: '1px solid var(--border)',
                    borderRadius: 4,
                    padding: 8,
                    minHeight: 32,
                    marginBottom: 8,
                  }}
                >
                  {selectedNpc.skills.length === 0 ? (
                    <div style={{ color: 'var(--text-muted)', fontSize: 12 }}>No skills assigned</div>
                  ) : (
                    <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6 }}>
                      {selectedNpc.skills.map(skillId => (
                        <div
                          key={skillId}
                          className="badge"
                          style={{
                            display: 'flex',
                            alignItems: 'center',
                            gap: 6,
                            paddingRight: 6,
                          }}
                        >
                          <span>{skillId}</span>
                          <button
                            style={{
                              background: 'none',
                              border: 'none',
                              cursor: 'pointer',
                              padding: 0,
                              color: 'inherit',
                              fontSize: 14,
                            }}
                            onClick={() => handleRemoveSkill(skillId)}
                          >
                            ×
                          </button>
                        </div>
                      ))}
                    </div>
                  )}
                </div>

                <div style={{ position: 'relative' }}>
                  <input
                    className="form-input"
                    type="text"
                    placeholder="Search skills to add..."
                    value={newSkillFilter}
                    onChange={(e) => setNewSkillFilter(e.target.value)}
                    onFocus={() => setSkillDropdownOpen(true)}
                  />
                  {skillDropdownOpen && newSkillFilter && (
                    <div
                      style={{
                        position: 'absolute',
                        top: '100%',
                        left: 0,
                        right: 0,
                        background: 'var(--bg-secondary)',
                        border: '1px solid var(--border)',
                        borderTop: 'none',
                        borderRadius: '0 0 4px 4px',
                        maxHeight: 200,
                        overflowY: 'auto',
                        zIndex: 10,
                      }}
                    >
                      {filteredAvailableSkills.length === 0 ? (
                        <div style={{ padding: 8, color: 'var(--text-muted)', fontSize: 12 }}>
                          No matching skills
                        </div>
                      ) : (
                        filteredAvailableSkills.slice(0, 10).map(skillId => (
                          <div
                            key={skillId}
                            style={{
                              padding: '6px 8px',
                              cursor: 'pointer',
                              borderBottom: '1px solid var(--border)',
                            }}
                            onClick={() => {
                              handleAddSkill(skillId);
                              setSkillDropdownOpen(false);
                            }}
                            onMouseEnter={(e) => {
                              (e.currentTarget as HTMLElement).style.backgroundColor =
                                'var(--bg-tertiary)';
                            }}
                            onMouseLeave={(e) => {
                              (e.currentTarget as HTMLElement).style.backgroundColor = 'transparent';
                            }}
                          >
                            {skillId}
                          </div>
                        ))
                      )}
                    </div>
                  )}
                </div>
              </div>

              <div className="form-group">
                <label className="form-label">Dialogue</label>
                <textarea
                  className="form-input"
                  placeholder="One line per entry"
                  value={(selectedNpc.dialogue || []).join('\n')}
                  onChange={(e) => handleUpdateDialogue(e.target.value)}
                  rows={4}
                />
              </div>

              {selectedNpc.type === 'npc' && (
                <div className="form-group">
                  <label className="form-label">Vendor Inventory</label>
                  <div
                    style={{
                      border: '1px solid var(--border)',
                      borderRadius: 4,
                      padding: 8,
                      maxHeight: 150,
                      overflowY: 'auto',
                      marginBottom: 8,
                    }}
                  >
                    {availableItems.map(itemId => {
                      const isSelected = (selectedNpc.vendorInventory || []).includes(itemId);
                      return (
                        <label key={itemId} className="form-checkbox-row" style={{ marginBottom: 6 }}>
                          <input
                            type="checkbox"
                            checked={isSelected}
                            onChange={(e) => handleUpdateVendorInventory(itemId, e.target.checked)}
                          />
                          <span>{items.data[itemId]?.name || itemId}</span>
                        </label>
                      );
                    })}
                  </div>
                </div>
              )}

              <button className="btn btn-primary" onClick={handleSave} style={{ width: '100%', marginTop: 16 }}>
                💾 Save NPC
              </button>
            </>
          ) : (
            <div className="empty-state">
              <div className="empty-state-icon">👹</div>
              <p>Select an NPC or create a new one</p>
            </div>
          )}
        </div>
      </div>
    </div>
  );
};
