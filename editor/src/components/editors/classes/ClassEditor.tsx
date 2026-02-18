import React, { useMemo, useState } from 'react';
import { useEditorStore } from '../../../store/editorStore';
import { StatBlockEditor, StatBlock } from '../../shared/StatBlockEditor';

interface ClassTemplate {
  id: string;
  name: string;
  description: string;
  baseStats: StatBlock;
  statsPerLevel: StatBlock;
  allowedArmor: 'cloth' | 'leather' | 'mail' | 'plate';
  baseSpeed: number;
  canUseMana: boolean;
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
};

const ARMOR_TYPES = ['cloth', 'leather', 'mail', 'plate'] as const;

export const ClassEditor: React.FC = () => {
  const { classes, skills, selectedClassId, setSelectedClassId, updateData, saveSection } = useEditorStore();
  const [activeTab, setActiveTab] = useState<'properties' | 'baseStats' | 'perLevelGrowth' | 'preview' | 'skills'>(
    'properties'
  );

  const classesData = classes.data.classes;
  const classSkillsData = skills.data.classSkills;
  const skillsData = skills.data.skills;

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
