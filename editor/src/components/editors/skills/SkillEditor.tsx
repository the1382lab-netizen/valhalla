import React, { useMemo, useState } from 'react';
import { useEditorStore } from '../../../store/editorStore';

interface SkillTemplate {
  id: string;
  name: string;
  description: string;
  classId: string;
  levelRequired: number;
  resourceType: 'mana' | 'energy' | 'none';
  resourceCost: number;
  castTimeMs: number;
  cooldownMs: number;
  range: number;
  targetType: 'self' | 'singleEnemy' | 'singleAlly' | 'aoeGround' | 'aoeSelf' | 'cone' | 'passiveToggle';
  category: 'offensive' | 'defensive' | 'healing' | 'buff' | 'debuff' | 'utility';
  iconColor: number;
  iconAbbrev: string;
  scalingStat: string;
  baseDamage?: [number, number];
  baseHealing?: [number, number];
  buffDurationMs?: number;
  dotDamagePerSec?: number;
  hotHealPerSec?: number;
  /** Blast radius in pixels for AOE_GROUND spells (e.g. Fireball, Meteor). */
  aoeRadius?: number;
  /** Projectile metadata for projectile-based skills. */
  projectile?: { speed: number; radius: number };
  /** Cooldown group ID — skills sharing a group share a cooldown. */
  cooldownGroup?: string;
  /** Buff stacking mode. */
  stackingMode?: 'replace' | 'stack' | 'extend';
  /** Max stacks (when stackingMode is 'stack'). */
  maxStacks?: number;
  /** Whether this skill is an auto-attack (melee or ranged). */
  isAutoAttack?: boolean;
  /** Whether this skill requires a weapon to be equipped. */
  requiresWeapon?: boolean;
  /** Client animation while casting: 'bow' draws and holds the bow, then looses. */
  castAnimation?: 'bow';
  effectNotes: string;
}

const DEFAULT_SKILL: Omit<SkillTemplate, 'id'> = {
  name: 'New Skill',
  description: '',
  classId: 'warrior',
  levelRequired: 1,
  resourceType: 'mana',
  resourceCost: 0,
  castTimeMs: 0,
  cooldownMs: 0,
  range: 0,
  targetType: 'singleEnemy',
  category: 'offensive',
  iconColor: 0xff4488,
  iconAbbrev: 'SK',
  scalingStat: 'strength',
  effectNotes: '',
};

const RESOURCE_TYPES = ['mana', 'energy', 'none'] as const;
const TARGET_TYPES = ['self', 'singleEnemy', 'singleAlly', 'aoeGround', 'aoeSelf', 'cone', 'passiveToggle'] as const;
const CATEGORIES = ['offensive', 'defensive', 'healing', 'buff', 'debuff', 'utility'] as const;

/**
 * Reconcile classSkills arrays against the current skill definitions.
 *
 * Rules:
 *  - Skills that no longer exist are removed from all class arrays.
 *  - Skills whose classId changed are moved to their new class array (and
 *    removed from any old ones).
 *  - Skills whose classId is set but missing from their class array are
 *    appended to it.
 *  - Skills with no classId (e.g. shared skills like melee_attack) are left
 *    wherever ClassEditor placed them — we don't touch those entries.
 */
function syncClassSkills(
  skills: Record<string, SkillTemplate>,
  existingClassSkills: Record<string, string[]>,
): Record<string, string[]> {
  // Step 1: copy + clean existing arrays
  const result: Record<string, string[]> = {};
  for (const [classId, skillIds] of Object.entries(existingClassSkills)) {
    result[classId] = skillIds.filter((id) => {
      const skill = skills[id];
      if (!skill) return false;                            // deleted — drop
      if (skill.classId && skill.classId !== classId) return false; // moved — drop
      return true;
    });
  }

  // Step 2: ensure every skill with a classId appears in its class array
  for (const [skillId, skill] of Object.entries(skills)) {
    if (!skill.classId) continue;
    if (!result[skill.classId]) result[skill.classId] = [];
    if (!result[skill.classId].includes(skillId)) {
      result[skill.classId].push(skillId);
    }
  }

  return result;
}

export const SkillEditor: React.FC = () => {
  const { skills, classes, selectedSkillId, setSelectedSkillId, updateData, markDirty, saveSection } = useEditorStore();
  const [searchQuery, setSearchQuery] = useState('');
  const [filterTab, setFilterTab] = useState<string>('all');

  const skillsData = skills.data.skills;
  const classesData = classes.data.classes;
  const classList = useMemo(() => Object.keys(classesData).sort(), [classesData]);

  /** All unique cooldown group IDs currently defined across all skills — used for datalist suggestions. */
  const cooldownGroups = useMemo(() => {
    const groups = new Set<string>();
    for (const skill of Object.values(skillsData)) {
      if (skill?.cooldownGroup) groups.add(skill.cooldownGroup);
    }
    return Array.from(groups).sort();
  }, [skillsData]);

  const filteredSkills = useMemo(() => {
    const ids = Object.keys(skillsData);
    let filtered = ids.filter((id) => id.toLowerCase().includes(searchQuery.toLowerCase()));

    if (filterTab !== 'all') {
      filtered = filtered.filter((id) => skillsData[id]?.classId === filterTab);
    }

    return filtered.sort();
  }, [skillsData, searchQuery, filterTab]);

  const selectedSkill = selectedSkillId ? skillsData[selectedSkillId] : null;

  const handleSelectSkill = (id: string) => {
    setSelectedSkillId(id);
  };

  const handleCreateSkill = () => {
    const newId = `skill_${Date.now()}`;
    const newSkill: SkillTemplate = {
      id: newId,
      ...DEFAULT_SKILL,
    };
    const newSkills = { ...skillsData, [newId]: newSkill };
    updateData('skills', { ...skills.data, skills: newSkills });
    setSelectedSkillId(newId);
  };

  const handleDuplicateSkill = () => {
    if (!selectedSkill) return;
    const newId = `skill_${Date.now()}`;
    const newSkill: SkillTemplate = {
      ...selectedSkill,
      id: newId,
      name: `${selectedSkill.name} (Copy)`,
    };
    const newSkills = { ...skillsData, [newId]: newSkill };
    updateData('skills', { ...skills.data, skills: newSkills });
    setSelectedSkillId(newId);
  };

  const handleDeleteSkill = () => {
    if (!selectedSkill) return;
    const { [selectedSkillId!]: _, ...remaining } = skillsData;
    // Also purge the deleted skill from all classSkills arrays immediately.
    const syncedClassSkills = syncClassSkills(remaining, skills.data.classSkills);
    updateData('skills', { skills: remaining, classSkills: syncedClassSkills });
    setSelectedSkillId(null);
  };

  const handleUpdateSkill = (updates: Partial<SkillTemplate>) => {
    if (!selectedSkill) return;
    const updated: SkillTemplate = { ...selectedSkill, ...updates };
    const newSkills = { ...skillsData, [selectedSkillId!]: updated };
    updateData('skills', { ...skills.data, skills: newSkills });
  };

  const handleSave = async () => {
    // Reconcile classSkills against current skill definitions before writing to disk.
    const syncedClassSkills = syncClassSkills(skillsData, skills.data.classSkills);
    updateData('skills', { ...skills.data, classSkills: syncedClassSkills });
    await saveSection('skills');
  };

  const formatTime = (ms: number) => (ms / 1000).toFixed(2);
  const parseTime = (seconds: string) => Math.round(parseFloat(seconds) * 1000);

  return (
    <div className="split-horizontal">
      {/* Left Panel: Skill List */}
      <div className="panel" style={{ display: 'flex', flexDirection: 'column', overflow: 'hidden' }}>
        <div className="panel-header">Skills ({filteredSkills.length})</div>
        <div className="panel-body" style={{ display: 'flex', flexDirection: 'column', flex: 1, overflow: 'hidden' }}>
          <div className="search-box">
            <span className="search-icon">🔍</span>
            <input
              type="text"
              placeholder="Search skills..."
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
            />
          </div>

          {/* Class Tabs */}
          <div className="tabs" style={{ marginBottom: 12 }}>
            <div
              className={`tab ${filterTab === 'all' ? 'active' : ''}`}
              onClick={() => setFilterTab('all')}
            >
              All
            </div>
            {classList.map((classId) => (
              <div
                key={classId}
                className={`tab ${filterTab === classId ? 'active' : ''}`}
                onClick={() => setFilterTab(classId)}
              >
                {classId}
              </div>
            ))}
          </div>

          {/* Skills Table — scrollable */}
          <div style={{ flex: 1, overflowY: 'auto', marginBottom: 12 }}>
          <table className="data-table" style={{ width: '100%' }}>
            <thead>
              <tr>
                <th>Name</th>
                <th>Lvl</th>
                <th>Cost</th>
                <th>Cast</th>
                <th>CD</th>
                <th>Cat</th>
              </tr>
            </thead>
            <tbody>
              {filteredSkills.length === 0 ? (
                <tr>
                  <td colSpan={6} style={{ textAlign: 'center', color: 'var(--text-muted)' }}>
                    No skills found
                  </td>
                </tr>
              ) : (
                filteredSkills.map((id) => {
                  const skill = skillsData[id];
                  if (!skill) return null;
                  const isSelected = selectedSkillId === id;
                  return (
                    <tr
                      key={id}
                      className={isSelected ? 'selected' : ''}
                      onClick={() => handleSelectSkill(id)}
                    >
                      <td>{skill.name}</td>
                      <td>{skill.levelRequired}</td>
                      <td>{skill.resourceCost}</td>
                      <td>{formatTime(skill.castTimeMs)}s</td>
                      <td>{formatTime(skill.cooldownMs)}s</td>
                      <td>{skill.category.substring(0, 3)}</td>
                    </tr>
                  );
                })
              )}
            </tbody>
          </table>
          </div>

          {/* Action Buttons */}
          <div style={{ display: 'flex', gap: 8 }}>
            <button className="btn btn-primary" onClick={handleCreateSkill}>
              New
            </button>
            <button className="btn btn-ghost" onClick={handleDuplicateSkill} disabled={!selectedSkill}>
              Duplicate
            </button>
            <button className="btn btn-danger" onClick={handleDeleteSkill} disabled={!selectedSkill}>
              Delete
            </button>
          </div>
        </div>
      </div>

      {/* Right Panel: Skill Editor */}
      <div className="panel" style={{ overflow: 'auto' }}>
        <div className="panel-header">
          {selectedSkill ? `Edit: ${selectedSkill.name}` : 'Select a skill to edit'}
        </div>
        <div className="panel-body">
          {selectedSkill ? (
            <form onSubmit={(e) => { e.preventDefault(); }}>
              {/* Icon Preview */}
              <div className="form-group" style={{ marginBottom: 16 }}>
                <label className="form-label">Icon Preview</label>
                <div
                  style={{
                    display: 'flex',
                    alignItems: 'center',
                    gap: 12,
                  }}
                >
                  <div
                    style={{
                      width: 48,
                      height: 48,
                      backgroundColor: `#${selectedSkill.iconColor.toString(16).padStart(6, '0')}`,
                      borderRadius: 4,
                      display: 'flex',
                      alignItems: 'center',
                      justifyContent: 'center',
                      color: '#fff',
                      fontWeight: 'bold',
                      fontSize: 14,
                      border: '1px solid var(--border-color)',
                    }}
                  >
                    {selectedSkill.iconAbbrev}
                  </div>
                  <div style={{ flex: 1 }}>
                    <div className="form-row">
                      <div className="form-group">
                        <label className="form-label">Color (Hex)</label>
                        <input
                          type="text"
                          className="form-input"
                          value={selectedSkill.iconColor.toString(16).toUpperCase().padStart(6, '0')}
                          onChange={(e) => {
                            const hex = e.target.value.replace(/^#/, '');
                            const color = parseInt(hex, 16) || 0;
                            handleUpdateSkill({ iconColor: color });
                          }}
                          placeholder="FF0000"
                        />
                      </div>
                      <div className="form-group">
                        <label className="form-label">Abbreviation</label>
                        <input
                          type="text"
                          className="form-input"
                          maxLength={3}
                          value={selectedSkill.iconAbbrev}
                          onChange={(e) => handleUpdateSkill({ iconAbbrev: e.target.value.toUpperCase() })}
                        />
                      </div>
                    </div>
                  </div>
                </div>
              </div>

              {/* Basic Info */}
              <div className="form-group">
                <label className="form-label">Skill Name</label>
                <input
                  type="text"
                  className="form-input"
                  value={selectedSkill.name}
                  onChange={(e) => handleUpdateSkill({ name: e.target.value })}
                />
              </div>

              <div className="form-group">
                <label className="form-label">Description</label>
                <textarea
                  className="form-input"
                  value={selectedSkill.description}
                  onChange={(e) => handleUpdateSkill({ description: e.target.value })}
                  rows={3}
                />
              </div>

              <div className="form-row">
                <div className="form-group">
                  <label className="form-label">Class</label>
                  <select
                    className="form-select"
                    value={selectedSkill.classId || ''}
                    onChange={(e) => handleUpdateSkill({ classId: e.target.value || null as any })}
                  >
                    <option value="">All Classes</option>
                    {classList.map((cid) => (
                      <option key={cid} value={cid}>
                        {cid}
                      </option>
                    ))}
                  </select>
                </div>
                <div className="form-group">
                  <label className="form-label">Level Required</label>
                  <input
                    type="number"
                    className="form-input"
                    min={1}
                    value={selectedSkill.levelRequired}
                    onChange={(e) => handleUpdateSkill({ levelRequired: parseInt(e.target.value, 10) || 1 })}
                  />
                </div>
              </div>

              {/* Resources */}
              <div className="form-row">
                <div className="form-group">
                  <label className="form-label">Resource Type</label>
                  <select
                    className="form-select"
                    value={selectedSkill.resourceType}
                    onChange={(e) => handleUpdateSkill({ resourceType: e.target.value as any })}
                  >
                    {RESOURCE_TYPES.map((type) => (
                      <option key={type} value={type}>
                        {type}
                      </option>
                    ))}
                  </select>
                </div>
                <div className="form-group">
                  <label className="form-label">Resource Cost</label>
                  <input
                    type="number"
                    className="form-input"
                    min={0}
                    value={selectedSkill.resourceCost}
                    onChange={(e) => handleUpdateSkill({ resourceCost: parseInt(e.target.value, 10) || 0 })}
                  />
                </div>
              </div>

              {/* Timing */}
              <div className="form-row">
                <div className="form-group">
                  <label className="form-label">Cast Time (Seconds)</label>
                  <input
                    type="number"
                    className="form-input"
                    min={0}
                    step={0.1}
                    value={formatTime(selectedSkill.castTimeMs)}
                    onChange={(e) => handleUpdateSkill({ castTimeMs: parseTime(e.target.value) })}
                  />
                </div>
                <div className="form-group">
                  <label className="form-label">Cooldown (Seconds)</label>
                  <input
                    type="number"
                    className="form-input"
                    min={0}
                    step={0.1}
                    value={formatTime(selectedSkill.cooldownMs)}
                    onChange={(e) => handleUpdateSkill({ cooldownMs: parseTime(e.target.value) })}
                  />
                </div>
              </div>

              {/* Targeting */}
              <div className="form-row">
                <div className="form-group">
                  <label className="form-label">Target Type</label>
                  <select
                    className="form-select"
                    value={selectedSkill.targetType}
                    onChange={(e) => handleUpdateSkill({ targetType: e.target.value as any })}
                  >
                    {TARGET_TYPES.map((type) => (
                      <option key={type} value={type}>
                        {type}
                      </option>
                    ))}
                  </select>
                </div>
                <div className="form-group">
                  <label className="form-label">Range</label>
                  <input
                    type="number"
                    className="form-input"
                    min={0}
                    value={selectedSkill.range}
                    onChange={(e) => handleUpdateSkill({ range: parseInt(e.target.value, 10) || 0 })}
                  />
                </div>
              </div>

              {/* AoE Radius — only for ground-targeted skills */}
              {selectedSkill.targetType === 'aoeGround' && (
                <div className="form-group">
                  <label className="form-label">AoE Radius (px)</label>
                  <input
                    type="number"
                    className="form-input"
                    min={1}
                    value={selectedSkill.aoeRadius || 96}
                    onChange={(e) => handleUpdateSkill({ aoeRadius: parseInt(e.target.value, 10) || 96 })}
                  />
                </div>
              )}

              {/* Category and Scaling */}
              <div className="form-row">
                <div className="form-group">
                  <label className="form-label">Category</label>
                  <select
                    className="form-select"
                    value={selectedSkill.category}
                    onChange={(e) => handleUpdateSkill({ category: e.target.value as any })}
                  >
                    {CATEGORIES.map((cat) => (
                      <option key={cat} value={cat}>
                        {cat}
                      </option>
                    ))}
                  </select>
                </div>
                <div className="form-group">
                  <label className="form-label">Scaling Stat</label>
                  <input
                    type="text"
                    className="form-input"
                    value={selectedSkill.scalingStat}
                    onChange={(e) => handleUpdateSkill({ scalingStat: e.target.value })}
                    placeholder="e.g., strength, intelligence"
                  />
                </div>
              </div>

              {/* Damage/Healing */}
              <div className="form-row">
                <div className="form-group">
                  <label className="form-label">Base Damage (Min)</label>
                  <input
                    type="number"
                    className="form-input"
                    min={0}
                    value={selectedSkill.baseDamage?.[0] || 0}
                    onChange={(e) => {
                      const val = parseInt(e.target.value, 10) || 0;
                      const max = selectedSkill.baseDamage?.[1] || val;
                      handleUpdateSkill({ baseDamage: [val, max] });
                    }}
                  />
                </div>
                <div className="form-group">
                  <label className="form-label">Base Damage (Max)</label>
                  <input
                    type="number"
                    className="form-input"
                    min={0}
                    value={selectedSkill.baseDamage?.[1] || 0}
                    onChange={(e) => {
                      const val = parseInt(e.target.value, 10) || 0;
                      const min = selectedSkill.baseDamage?.[0] || 0;
                      handleUpdateSkill({ baseDamage: [min, val] });
                    }}
                  />
                </div>
              </div>

              <div className="form-row">
                <div className="form-group">
                  <label className="form-label">Base Healing (Min)</label>
                  <input
                    type="number"
                    className="form-input"
                    min={0}
                    value={selectedSkill.baseHealing?.[0] || 0}
                    onChange={(e) => {
                      const val = parseInt(e.target.value, 10) || 0;
                      const max = selectedSkill.baseHealing?.[1] || val;
                      handleUpdateSkill({ baseHealing: [val, max] });
                    }}
                  />
                </div>
                <div className="form-group">
                  <label className="form-label">Base Healing (Max)</label>
                  <input
                    type="number"
                    className="form-input"
                    min={0}
                    value={selectedSkill.baseHealing?.[1] || 0}
                    onChange={(e) => {
                      const val = parseInt(e.target.value, 10) || 0;
                      const min = selectedSkill.baseHealing?.[0] || 0;
                      handleUpdateSkill({ baseHealing: [min, val] });
                    }}
                  />
                </div>
              </div>

              {/* Optional Effects */}
              <div className="form-row">
                <div className="form-group">
                  <label className="form-label">Buff Duration (ms)</label>
                  <input
                    type="number"
                    className="form-input"
                    min={0}
                    value={selectedSkill.buffDurationMs || 0}
                    onChange={(e) => handleUpdateSkill({ buffDurationMs: parseInt(e.target.value, 10) || 0 })}
                  />
                </div>
                <div className="form-group">
                  <label className="form-label">DOT Damage/Sec</label>
                  <input
                    type="number"
                    className="form-input"
                    min={0}
                    step={0.1}
                    value={selectedSkill.dotDamagePerSec || 0}
                    onChange={(e) => handleUpdateSkill({ dotDamagePerSec: parseFloat(e.target.value) || 0 })}
                  />
                </div>
              </div>

              <div className="form-group">
                <label className="form-label">HOT Heal/Sec</label>
                <input
                  type="number"
                  className="form-input"
                  min={0}
                  step={0.1}
                  value={selectedSkill.hotHealPerSec || 0}
                  onChange={(e) => handleUpdateSkill({ hotHealPerSec: parseFloat(e.target.value) || 0 })}
                />
              </div>

              {/* Projectile (only for aoeGround) */}
              {selectedSkill.targetType === 'aoeGround' && (
                <div className="form-row">
                  <div className="form-group">
                    <label className="form-label">Projectile Speed</label>
                    <input
                      type="number"
                      className="form-input"
                      min={0}
                      value={selectedSkill.projectile?.speed || 0}
                      onChange={(e) => {
                        const speed = parseInt(e.target.value, 10) || 0;
                        const radius = selectedSkill.projectile?.radius || 10;
                        handleUpdateSkill({ projectile: speed > 0 ? { speed, radius } : undefined });
                      }}
                    />
                  </div>
                  <div className="form-group">
                    <label className="form-label">Projectile Radius</label>
                    <input
                      type="number"
                      className="form-input"
                      min={1}
                      value={selectedSkill.projectile?.radius || 10}
                      onChange={(e) => {
                        const radius = parseInt(e.target.value, 10) || 10;
                        const speed = selectedSkill.projectile?.speed || 0;
                        handleUpdateSkill({ projectile: speed > 0 ? { speed, radius } : undefined });
                      }}
                    />
                  </div>
                </div>
              )}

              {/* Cooldown Group — combobox: suggest existing groups, allow new ones */}
              <div className="form-group">
                <label className="form-label">Cooldown Group</label>
                <input
                  type="text"
                  className="form-input"
                  list="cooldown-group-list"
                  value={selectedSkill.cooldownGroup || ''}
                  onChange={(e) => handleUpdateSkill({ cooldownGroup: e.target.value || undefined })}
                  placeholder="e.g., warrior_shouts (leave empty for none)"
                />
                <datalist id="cooldown-group-list">
                  {cooldownGroups.map((g) => (
                    <option key={g} value={g} />
                  ))}
                </datalist>
                {selectedSkill.cooldownGroup && (
                  <div style={{ marginTop: 4, fontSize: 11, color: 'var(--text-muted)' }}>
                    Shares cooldown with:{' '}
                    {Object.values(skillsData)
                      .filter((s) => s?.cooldownGroup === selectedSkill.cooldownGroup && s.id !== selectedSkill.id)
                      .map((s) => s!.name)
                      .join(', ') || 'no other skills yet'}
                  </div>
                )}
              </div>

              {/* Buff Stacking */}
              {selectedSkill.buffDurationMs && selectedSkill.buffDurationMs > 0 ? (
                <div className="form-row">
                  <div className="form-group">
                    <label className="form-label">Stacking Mode</label>
                    <select
                      className="form-select"
                      value={selectedSkill.stackingMode || 'replace'}
                      onChange={(e) => handleUpdateSkill({ stackingMode: e.target.value as any })}
                    >
                      <option value="replace">Replace</option>
                      <option value="stack">Stack</option>
                      <option value="extend">Extend</option>
                    </select>
                  </div>
                  {selectedSkill.stackingMode === 'stack' && (
                    <div className="form-group">
                      <label className="form-label">Max Stacks</label>
                      <input
                        type="number"
                        className="form-input"
                        min={1}
                        value={selectedSkill.maxStacks || 1}
                        onChange={(e) => handleUpdateSkill({ maxStacks: parseInt(e.target.value, 10) || 1 })}
                      />
                    </div>
                  )}
                </div>
              ) : null}

              {/* Auto-Attack Flags */}
              <div className="form-row">
                <div className="form-group">
                  <label className="form-label form-checkbox-row">
                    <input
                      type="checkbox"
                      checked={selectedSkill.isAutoAttack || false}
                      onChange={(e) => handleUpdateSkill({ isAutoAttack: e.target.checked || undefined })}
                    />
                    <span>Auto-Attack Skill</span>
                  </label>
                </div>
                <div className="form-group">
                  <label className="form-label form-checkbox-row">
                    <input
                      type="checkbox"
                      checked={selectedSkill.requiresWeapon || false}
                      onChange={(e) => handleUpdateSkill({ requiresWeapon: e.target.checked || undefined })}
                    />
                    <span>Requires Weapon</span>
                  </label>
                </div>
                <div className="form-group">
                  <label className="form-label form-checkbox-row">
                    <input
                      type="checkbox"
                      checked={selectedSkill.castAnimation === 'bow'}
                      onChange={(e) => handleUpdateSkill({ castAnimation: e.target.checked ? 'bow' : undefined })}
                    />
                    <span>Bow Draw While Casting</span>
                  </label>
                </div>
              </div>

              {/* Effect Notes */}
              <div className="form-group">
                <label className="form-label">Effect Notes</label>
                <textarea
                  className="form-input"
                  value={selectedSkill.effectNotes}
                  onChange={(e) => handleUpdateSkill({ effectNotes: e.target.value })}
                  rows={3}
                />
              </div>

              {/* Save Button */}
              <div style={{ marginTop: 16 }}>
                <button className="btn btn-primary" onClick={handleSave}>
                  Save Changes
                </button>
              </div>
            </form>
          ) : (
            <div className="empty-state">
              <div className="empty-state-icon">✨</div>
              <p>Select or create a skill to edit</p>
            </div>
          )}
        </div>
      </div>
    </div>
  );
};
