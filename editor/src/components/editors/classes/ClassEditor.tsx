import React, { useMemo, useState } from 'react';
import { useEditorStore } from '../../../store/editorStore';

/** Skin tones Unreal knows (UValhallaVisuals::SkinSrgbForBodyId), with the swatch it tints the body. */
const BODY_TONES: { id: string; label: string; color: string }[] = [
  { id: 'body_fair',  label: 'Fair',  color: '#E0B193' },
  { id: 'body_tan',   label: 'Tan',   color: '#C9925F' },
  { id: 'body_olive', label: 'Olive', color: '#B08A5A' },
  { id: 'body_dark',  label: 'Dark',  color: '#7A4B2A' },
  { id: 'body_elder', label: 'Elder (grey hair)', color: '#D9C3B0' },
];
import { StatBlockEditor, StatBlock } from '../../shared/StatBlockEditor';

interface StartingItem {
  itemId: string;
  quantity: number;
  equipped: boolean;
}

interface ClassTemplate {
  id: string;
  name: string;
  description: string;
  baseStats: StatBlock;
  statsPerLevel: StatBlock;
  allowedArmor: 'cloth' | 'leather' | 'mail' | 'plate';
  baseSpeed: number;
  canUseMana: boolean;
  baseMeleeAttackSpeedMs: number;
  baseRangedAttackSpeedMs: number;
  /** Line-of-sight vision range in UE units (cm) */
  visionRange: number;
  startingItems?: StartingItem[];
  bodyId?: string;
}

const DEFAULT_CLASS: Omit<ClassTemplate, 'id'> = {
  name: 'New Class',
  description: '',
  baseStats: {
    hp: 100,
    mana: 50,
    strength: 10,
    stamina: 10,
    dexterity: 10,
    intelligence: 10,
    wisdom: 10,
    physicalResist: 0,
    spellResist: 0,
    critChance: 0,
    critDamage: 100,
    physicalDefense: 5,
    blockRating: 0,
    dodgeRating: 0,
  },
  statsPerLevel: {
    hp: 10,
    mana: 5,
    strength: 1,
    stamina: 1,
    dexterity: 1,
    intelligence: 1,
    wisdom: 1,
    physicalResist: 0,
    spellResist: 0,
    critChance: 0,
    critDamage: 0,
    physicalDefense: 0,
    blockRating: 0,
    dodgeRating: 0,
  },
  allowedArmor: 'cloth',
  baseSpeed: 5.5,
  canUseMana: true,
  baseMeleeAttackSpeedMs: 2000,
  baseRangedAttackSpeedMs: 0,
  visionRange: 1200,
};

const ARMOR_TYPES = ['cloth', 'leather', 'mail', 'plate'] as const;

export const ClassEditor: React.FC = () => {
  const { classes, skills, items, selectedClassId, setSelectedClassId, updateData, saveSection } = useEditorStore();
  const [activeTab, setActiveTab] = useState<'properties' | 'baseStats' | 'perLevelGrowth' | 'preview' | 'skills' | 'startingItems'>(
    'properties'
  );

  // State for the "add starting item" form
  const [newItemId, setNewItemId] = useState('');
  const [newItemQty, setNewItemQty] = useState(1);
  const [newItemEquipped, setNewItemEquipped] = useState(false);

  const classesData = classes.data.classes;
  const classSkillsData = skills.data.classSkills;
  const skillsData = skills.data.skills;
  const itemCatalog: Record<string, any> = items.data;

  // Sorted list of items for the "add starting item" dropdown
  const itemOptions = useMemo(
    () => Object.values(itemCatalog).sort((a, b) => (a.name || a.id).localeCompare(b.name || b.id)),
    [itemCatalog]
  );

  // Dynamically derive class list from the loaded JSON data
  const classIds = useMemo(() => Object.keys(classesData).sort(), [classesData]);

  const selectedClass = selectedClassId ? classesData[selectedClassId] : null;

  const handleSelectClass = (id: string) => {
    setSelectedClassId(id);
    setActiveTab('properties');
  };

  const handleUpdateClass = (updates: Partial<ClassTemplate>) => {
    if (!selectedClass) return;
    const updated: ClassTemplate = { ...selectedClass, ...updates };
    const newClasses = { ...classesData, [selectedClassId!]: updated };
    updateData('classes', { ...classes.data, classes: newClasses });
  };

  const handleSaveAll = async () => {
    await Promise.all([saveSection('classes'), saveSection('skills')]);
  };

  const computeStatAtLevel = (baseValue: number, perLevelValue: number, level: number) => {
    return baseValue + (level - 1) * perLevelValue;
  };

  const getClassSkills = () => {
    if (!selectedClass) return [];
    const skillIds = classSkillsData[selectedClass.id] || [];
    return skillIds
      .map((id) => skillsData[id])
      .filter(Boolean) as any[];
  };

  // ── Starting Items helpers ──
  const handleAddStartingItem = () => {
    if (!selectedClass || !newItemId) return;
    const existing = selectedClass.startingItems ?? [];
    // Prevent duplicate item entries
    if (existing.some((si: StartingItem) => si.itemId === newItemId)) return;
    const updated: StartingItem[] = [...existing, { itemId: newItemId, quantity: newItemQty, equipped: newItemEquipped }];
    handleUpdateClass({ startingItems: updated });
    setNewItemId('');
    setNewItemQty(1);
    setNewItemEquipped(false);
  };

  const handleRemoveStartingItem = (itemId: string) => {
    if (!selectedClass) return;
    const updated = (selectedClass.startingItems ?? []).filter((si: StartingItem) => si.itemId !== itemId);
    handleUpdateClass({ startingItems: updated });
  };

  const handleUpdateStartingItem = (itemId: string, changes: Partial<StartingItem>) => {
    if (!selectedClass) return;
    const updated = (selectedClass.startingItems ?? []).map((si: StartingItem) =>
      si.itemId === itemId ? { ...si, ...changes } : si
    );
    handleUpdateClass({ startingItems: updated });
  };

  const handleMoveSkill = (index: number, direction: 'up' | 'down') => {
    if (!selectedClass) return;
    const skillIds = [...(classSkillsData[selectedClass.id] || [])];
    if (direction === 'up' && index > 0) {
      [skillIds[index], skillIds[index - 1]] = [skillIds[index - 1], skillIds[index]];
    } else if (direction === 'down' && index < skillIds.length - 1) {
      [skillIds[index], skillIds[index + 1]] = [skillIds[index + 1], skillIds[index]];
    }
    const newClassSkills = { ...classSkillsData, [selectedClass.id]: skillIds };
    updateData('skills', { ...skills.data, classSkills: newClassSkills });
  };

  return (
    <div className="split-horizontal">
      {/* Left Panel: Class List */}
      <div className="panel">
        <div className="panel-header">Classes ({classIds.length})</div>
        <div className="panel-body" style={{ display: 'flex', flexDirection: 'column' }}>
          <table className="data-table" style={{ flex: 1, marginBottom: 12 }}>
            <tbody>
              {classIds.map((classId) => {
                const classTemplate = classesData[classId];
                if (!classTemplate) return null;
                const isSelected = selectedClassId === classId;
                return (
                  <tr
                    key={classId}
                    className={isSelected ? 'selected' : ''}
                    onClick={() => handleSelectClass(classId)}
                  >
                    <td>{classTemplate.name || classId}</td>
                  </tr>
                );
              })}
            </tbody>
          </table>

          {Object.keys(classesData).length === 0 && (
            <div className="empty-state">
              <div className="empty-state-icon">🛡</div>
              <p>No classes loaded yet</p>
            </div>
          )}
        </div>
      </div>

      {/* Right Panel: Class Editor */}
      <div className="panel" style={{ overflow: 'hidden', display: 'flex', flexDirection: 'column' }}>
        <div className="panel-header">
          {selectedClass ? `Edit: ${selectedClass.name}` : 'Select a class to edit'}
        </div>

        {selectedClass ? (
          <>
            {/* Tabs */}
            <div className="tabs" style={{ paddingLeft: 16, paddingRight: 16, borderBottom: '1px solid var(--border-color)' }}>
              <div
                className={`tab ${activeTab === 'properties' ? 'active' : ''}`}
                onClick={() => setActiveTab('properties')}
              >
                Properties
              </div>
              <div
                className={`tab ${activeTab === 'baseStats' ? 'active' : ''}`}
                onClick={() => setActiveTab('baseStats')}
              >
                Base Stats
              </div>
              <div
                className={`tab ${activeTab === 'perLevelGrowth' ? 'active' : ''}`}
                onClick={() => setActiveTab('perLevelGrowth')}
              >
                Per-Level Growth
              </div>
              <div
                className={`tab ${activeTab === 'preview' ? 'active' : ''}`}
                onClick={() => setActiveTab('preview')}
              >
                Preview
              </div>
              <div
                className={`tab ${activeTab === 'skills' ? 'active' : ''}`}
                onClick={() => setActiveTab('skills')}
              >
                Skills
              </div>
              <div
                className={`tab ${activeTab === 'startingItems' ? 'active' : ''}`}
                onClick={() => setActiveTab('startingItems')}
              >
                Starting Items
              </div>
            </div>

            {/* Tab Content */}
            <div className="panel-body" style={{ flex: 1, overflow: 'auto' }}>
              {activeTab === 'properties' && (
                <form>
                  <div className="form-group">
                    <label className="form-label">Class Name</label>
                    <input
                      type="text"
                      className="form-input"
                      value={selectedClass.name}
                      onChange={(e) => handleUpdateClass({ name: e.target.value })}
                    />
                  </div>

                  <div className="form-group">
                    <label className="form-label">Description</label>
                    <textarea
                      className="form-input"
                      value={selectedClass.description}
                      onChange={(e) => handleUpdateClass({ description: e.target.value })}
                      rows={3}
                    />
                  </div>

                  <div className="form-row">
                    <div className="form-group">
                      <label className="form-label">Allowed Armor</label>
                      <select
                        className="form-select"
                        value={selectedClass.allowedArmor}
                        onChange={(e) => handleUpdateClass({ allowedArmor: e.target.value as any })}
                      >
                        {ARMOR_TYPES.map((armor) => (
                          <option key={armor} value={armor}>
                            {armor}
                          </option>
                        ))}
                      </select>
                    </div>
                    <div className="form-group">
                      <label className="form-label">Base Speed</label>
                      <input
                        type="number"
                        className="form-input"
                        step={0.1}
                        value={selectedClass.baseSpeed}
                        onChange={(e) => handleUpdateClass({ baseSpeed: parseFloat(e.target.value) || 5.5 })}
                      />
                    </div>
                    <div className="form-group">
                      <label className="form-label">Vision Range (cm)</label>
                      <input
                        type="number"
                        className="form-input"
                        min={0}
                        step={50}
                        value={selectedClass.visionRange ?? 1200}
                        onChange={(e) => handleUpdateClass({ visionRange: parseInt(e.target.value, 10) || 0 })}
                      />
                    </div>
                  </div>

                  <div className="form-group">
                    <label className="form-label form-checkbox-row">
                      <input
                        type="checkbox"
                        checked={selectedClass.canUseMana}
                        onChange={(e) => handleUpdateClass({ canUseMana: e.target.checked })}
                      />
                      <span>Can Use Mana</span>
                    </label>
                  </div>

                  <div className="form-row">
                    <div className="form-group">
                      <label className="form-label">Base Melee Attack Speed (ms)</label>
                      <input
                        type="number"
                        className="form-input"
                        min={0}
                        step={100}
                        value={selectedClass.baseMeleeAttackSpeedMs ?? 2000}
                        onChange={(e) => handleUpdateClass({ baseMeleeAttackSpeedMs: parseInt(e.target.value, 10) || 0 })}
                      />
                    </div>
                    <div className="form-group">
                      <label className="form-label">Base Ranged Attack Speed (ms)</label>
                      <input
                        type="number"
                        className="form-input"
                        min={0}
                        step={100}
                        value={selectedClass.baseRangedAttackSpeedMs ?? 0}
                        onChange={(e) => handleUpdateClass({ baseRangedAttackSpeedMs: parseInt(e.target.value, 10) || 0 })}
                      />
                    </div>
                  </div>

                  {/* ── Default body (skin tone) ───────────────── */}
                  <div style={{ marginTop: 16, borderTop: '1px solid #444', paddingTop: 12 }}>
                    <label className="form-label" style={{ fontWeight: 'bold', marginBottom: 8 }}>
                      Default Body
                    </label>
                    <div style={{ display: 'flex', gap: 12, alignItems: 'center' }}>
                      <select
                        className="form-select"
                        style={{ flex: 1 }}
                        value={selectedClass.bodyId || ''}
                        onChange={(e) => handleUpdateClass({ bodyId: e.target.value || undefined })}
                      >
                        <option value="">Default (fair)</option>
                        {BODY_TONES.map(b => (
                          <option key={b.id} value={b.id}>{b.label}</option>
                        ))}
                        {selectedClass.bodyId && !BODY_TONES.some(b => b.id === selectedClass.bodyId) && (
                          <option value={selectedClass.bodyId}>{selectedClass.bodyId} (unknown — renders fair)</option>
                        )}
                      </select>
                      <div title="Skin tint in Unreal" style={{
                        width: 28, height: 28, borderRadius: 4, border: '1px solid #555', flexShrink: 0,
                        background: (BODY_TONES.find(b => b.id === selectedClass.bodyId) ?? BODY_TONES[0]).color,
                      }} />
                    </div>
                    <div style={{ fontSize: '0.8em', color: '#888', marginTop: 6 }}>
                      New characters of this class start with this skin tone.
                    </div>
                  </div>
                </form>
              )}

              {activeTab === 'baseStats' && (
                <div>
                  <label className="form-label" style={{ marginBottom: 12 }}>
                    Base Stats
                  </label>
                  <StatBlockEditor
                    value={selectedClass.baseStats}
                    onChange={(stats) => handleUpdateClass({ baseStats: stats })}
                  />
                </div>
              )}

              {activeTab === 'perLevelGrowth' && (
                <div>
                  <label className="form-label" style={{ marginBottom: 12 }}>
                    Stats Per Level
                  </label>
                  <StatBlockEditor
                    value={selectedClass.statsPerLevel}
                    onChange={(stats) => handleUpdateClass({ statsPerLevel: stats })}
                  />
                </div>
              )}

              {activeTab === 'preview' && (
                <div>
                  <label className="form-label" style={{ marginBottom: 12 }}>
                    Stat Progression Preview
                  </label>
                  <table
                    className="data-table"
                    style={{
                      fontSize: '12px',
                    }}
                  >
                    <thead>
                      <tr>
                        <th>Stat</th>
                        <th>Level 1</th>
                        <th>Level 5</th>
                        <th>Level 10</th>
                        <th>Level 15</th>
                        <th>Level 20</th>
                      </tr>
                    </thead>
                    <tbody>
                      {(
                        [
                          'hp',
                          'mana',
                          'strength',
                          'stamina',
                          'dexterity',
                          'intelligence',
                          'wisdom',
                          'physicalDefense',
                          'spellResist',
                          'critChance',
                          'critDamage',
                          'blockRating',
                          'dodgeRating',
                        ] as const
                      ).map((stat) => (
                        <tr key={stat}>
                          <td style={{ fontWeight: 600, textTransform: 'capitalize' }}>{stat}</td>
                          <td>{computeStatAtLevel(selectedClass.baseStats[stat], selectedClass.statsPerLevel[stat], 1)}</td>
                          <td>{computeStatAtLevel(selectedClass.baseStats[stat], selectedClass.statsPerLevel[stat], 5)}</td>
                          <td>{computeStatAtLevel(selectedClass.baseStats[stat], selectedClass.statsPerLevel[stat], 10)}</td>
                          <td>{computeStatAtLevel(selectedClass.baseStats[stat], selectedClass.statsPerLevel[stat], 15)}</td>
                          <td>{computeStatAtLevel(selectedClass.baseStats[stat], selectedClass.statsPerLevel[stat], 20)}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              )}

              {activeTab === 'skills' && (
                <div>
                  <label className="form-label" style={{ marginBottom: 12 }}>
                    Class Skills
                  </label>
                  <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
                    {getClassSkills().length === 0 ? (
                      <div
                        style={{
                          padding: 16,
                          backgroundColor: 'var(--bg-primary)',
                          borderRadius: 4,
                          color: 'var(--text-muted)',
                          textAlign: 'center',
                        }}
                      >
                        No skills assigned to this class
                      </div>
                    ) : (
                      getClassSkills().map((skill, index) => (
                        <div
                          key={`${skill.id}-${index}`}
                          style={{
                            display: 'flex',
                            alignItems: 'center',
                            padding: '8px 12px',
                            backgroundColor: 'var(--bg-primary)',
                            borderRadius: 4,
                            gap: 8,
                          }}
                        >
                          <div
                            style={{
                              width: 28,
                              height: 28,
                              backgroundColor: `#${skill.iconColor.toString(16).padStart(6, '0')}`,
                              borderRadius: 3,
                              display: 'flex',
                              alignItems: 'center',
                              justifyContent: 'center',
                              color: '#fff',
                              fontSize: '10px',
                              fontWeight: 'bold',
                            }}
                          >
                            {skill.iconAbbrev}
                          </div>
                          <div style={{ flex: 1 }}>
                            <div style={{ fontSize: '13px', fontWeight: 500 }}>{skill.name}</div>
                            <div style={{ fontSize: '11px', color: 'var(--text-muted)' }}>
                              Level {skill.levelRequired}
                            </div>
                          </div>
                          <button
                            className="btn btn-ghost"
                            onClick={() => handleMoveSkill(index, 'up')}
                            disabled={index === 0}
                            style={{ padding: '4px 8px', fontSize: '11px' }}
                          >
                            Up
                          </button>
                          <button
                            className="btn btn-ghost"
                            onClick={() => handleMoveSkill(index, 'down')}
                            disabled={index === getClassSkills().length - 1}
                            style={{ padding: '4px 8px', fontSize: '11px' }}
                          >
                            Down
                          </button>
                        </div>
                      ))
                    )}
                  </div>
                </div>
              )}

              {activeTab === 'startingItems' && (
                <div>
                  <label className="form-label" style={{ marginBottom: 8 }}>
                    Starting Items
                  </label>
                  <p style={{ fontSize: '12px', color: 'var(--text-muted)', marginBottom: 16 }}>
                    Items given to new characters of this class. "Auto-equip" items go into equipment slots; others go into inventory.
                  </p>

                  {/* Current starting items list */}
                  <div style={{ display: 'flex', flexDirection: 'column', gap: 6, marginBottom: 16 }}>
                    {(selectedClass.startingItems ?? []).length === 0 ? (
                      <div
                        style={{
                          padding: 16,
                          backgroundColor: 'var(--bg-primary)',
                          borderRadius: 4,
                          color: 'var(--text-muted)',
                          textAlign: 'center',
                        }}
                      >
                        No starting items configured
                      </div>
                    ) : (
                      (selectedClass.startingItems ?? []).map((si: StartingItem) => {
                        const itemData = itemCatalog[si.itemId];
                        const itemName = itemData?.name || si.itemId;
                        return (
                          <div
                            key={si.itemId}
                            style={{
                              display: 'flex',
                              alignItems: 'center',
                              padding: '8px 12px',
                              backgroundColor: 'var(--bg-primary)',
                              borderRadius: 4,
                              gap: 10,
                            }}
                          >
                            <div style={{ flex: 1, fontSize: '13px', fontWeight: 500 }}>{itemName}</div>
                            <div style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
                              <label style={{ fontSize: '12px', color: 'var(--text-muted)' }}>Qty</label>
                              <input
                                type="number"
                                min={1}
                                className="form-input"
                                style={{ width: 60, padding: '2px 6px' }}
                                value={si.quantity}
                                onChange={(e) =>
                                  handleUpdateStartingItem(si.itemId, {
                                    quantity: Math.max(1, parseInt(e.target.value) || 1),
                                  })
                                }
                              />
                            </div>
                            <label
                              style={{ display: 'flex', alignItems: 'center', gap: 4, fontSize: '12px', cursor: 'pointer', whiteSpace: 'nowrap' }}
                            >
                              <input
                                type="checkbox"
                                checked={si.equipped}
                                onChange={(e) => handleUpdateStartingItem(si.itemId, { equipped: e.target.checked })}
                              />
                              Auto-equip
                            </label>
                            <button
                              className="btn btn-danger"
                              onClick={() => handleRemoveStartingItem(si.itemId)}
                              style={{ padding: '3px 8px', fontSize: '11px' }}
                            >
                              Remove
                            </button>
                          </div>
                        );
                      })
                    )}
                  </div>

                  {/* Add new item form */}
                  <div
                    style={{
                      padding: 12,
                      backgroundColor: 'var(--bg-primary)',
                      borderRadius: 4,
                      border: '1px solid var(--border-color)',
                    }}
                  >
                    <label className="form-label" style={{ marginBottom: 8, fontSize: '12px' }}>
                      Add Item
                    </label>
                    <div style={{ display: 'flex', gap: 8, alignItems: 'center', flexWrap: 'wrap' }}>
                      <select
                        className="form-select"
                        style={{ flex: 1, minWidth: 160 }}
                        value={newItemId}
                        onChange={(e) => setNewItemId(e.target.value)}
                      >
                        <option value="">— Select item —</option>
                        {itemOptions.map((item: any) => (
                          <option key={item.id} value={item.id}>
                            {item.name || item.id}
                          </option>
                        ))}
                      </select>
                      <input
                        type="number"
                        min={1}
                        className="form-input"
                        style={{ width: 70 }}
                        value={newItemQty}
                        onChange={(e) => setNewItemQty(Math.max(1, parseInt(e.target.value) || 1))}
                        placeholder="Qty"
                      />
                      <label
                        style={{ display: 'flex', alignItems: 'center', gap: 4, fontSize: '12px', cursor: 'pointer', whiteSpace: 'nowrap' }}
                      >
                        <input
                          type="checkbox"
                          checked={newItemEquipped}
                          onChange={(e) => setNewItemEquipped(e.target.checked)}
                        />
                        Auto-equip
                      </label>
                      <button
                        className="btn btn-primary"
                        onClick={handleAddStartingItem}
                        disabled={!newItemId}
                        style={{ padding: '4px 12px', fontSize: '12px' }}
                      >
                        Add
                      </button>
                    </div>
                  </div>
                </div>
              )}
            </div>

            {/* Save Button */}
            <div
              style={{
                padding: '12px 16px',
                borderTop: '1px solid var(--border-color)',
                display: 'flex',
                justifyContent: 'flex-end',
              }}
            >
              <button className="btn btn-primary" onClick={handleSaveAll}>
                Save All Changes
              </button>
            </div>
          </>
        ) : (
          <div className="panel-body">
            <div className="empty-state">
              <div className="empty-state-icon">🛡</div>
              <p>Select a class to edit</p>
            </div>
          </div>
        )}
      </div>
    </div>
  );
};
