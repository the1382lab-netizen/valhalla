import React, { useState, useMemo } from 'react';
import { useEditorStore } from '../../../store/editorStore';

interface StatBlock {
  hp: number;
  mana: number;
  strength: number;
  stamina: number;
  dexterity: number;
  intelligence: number;
  wisdom: number;
  physicalResist: number;
  spellResist: number;
  critChance: number;
  critDamage: number;
  physicalDefense: number;
  blockRating: number;
  dodgeRating: number;
}

interface ClassTemplate {
  id: string;
  name: string;
  baseStats: StatBlock;
  statsPerLevel: StatBlock;
  allowedArmor: string;
  baseSpeed: number;
  canUseMana: boolean;
}

interface SkillTemplate {
  id: string;
  name: string;
  classId: string;
  levelRequired: number;
  castTimeMs: number;
  cooldownMs: number;
  resourceType: string;
  resourceCost: number;
  baseDamage?: [number, number];
  baseHealing?: [number, number];
  scalingStat: string;
  category: string;
}

const STAT_DISPLAY_KEYS: (keyof StatBlock)[] = [
  'hp', 'mana', 'strength', 'stamina', 'dexterity', 'intelligence', 'wisdom',
  'physicalDefense', 'critChance', 'blockRating', 'dodgeRating'
];

const STAT_LABELS: Record<keyof StatBlock, string> = {
  hp: 'HP',
  mana: 'Mana',
  strength: 'Str',
  stamina: 'Sta',
  dexterity: 'Dex',
  intelligence: 'Int',
  wisdom: 'Wis',
  physicalResist: 'Phys Resist',
  spellResist: 'Spell Resist',
  critChance: 'Crit%',
  critDamage: 'Crit Dmg',
  physicalDefense: 'Phys Def',
  blockRating: 'Block%',
  dodgeRating: 'Dodge%',
};

export const BalanceDashboard: React.FC = () => {
  const classesData = useEditorStore(s => s.classes.data);
  const skillsData = useEditorStore(s => s.skills.data);
  const [activeTab, setActiveTab] = useState<'comparison' | 'dps' | 'overview'>('comparison');
  const [selectedClass, setSelectedClass] = useState<string>('warrior');
  const [selectedLevel, setSelectedLevel] = useState(10);
  const [selectedSkill, setSelectedSkill] = useState<string>('');

  const classes = classesData?.classes || {};
  const classColors = classesData?.classColors || {};
  const skills = skillsData?.skills || {};
  const classSkills = skillsData?.classSkills || {};

  const classList = useMemo(
    () => Object.entries(classes).map(([id, cls]) => ({ ...(cls as ClassTemplate), id })),
    [classes]
  );

  const computeStats = (classId: string, level: number): StatBlock | null => {
    const cls = classes[classId] as ClassTemplate;
    if (!cls) return null;

    const stats: StatBlock = { ...cls.baseStats };
    const growth = cls.statsPerLevel;

    for (const key of STAT_DISPLAY_KEYS) {
      stats[key] = (stats[key] || 0) + (growth[key] || 0) * (level - 1);
    }

    return stats;
  };

  const skillsForClass = useMemo(() => {
    const skillIds = classSkills[selectedClass] || [];
    return skillIds
      .map(id => skills[id])
      .filter(s => s)
      .sort((a: any, b: any) => (a.levelRequired || 0) - (b.levelRequired || 0));
  }, [selectedClass, skills, classSkills]);

  // ─── CLASS COMPARISON TAB ───
  const comparisonData = useMemo(() => {
    const levels = [1, 5, 10, 15, 20];
    const rows: any[] = [];

    classList.forEach(cls => {
      levels.forEach(level => {
        const stats = computeStats(cls.id, level);
        if (stats) {
          rows.push({ classId: cls.id, className: cls.name, level, stats });
        }
      });
    });

    return rows;
  }, [classList]);

  const getHighLow = (statKey: keyof StatBlock, level: number) => {
    const values = classList
      .map(cls => computeStats(cls.id, level))
      .filter(s => s !== null)
      .map(s => (s as StatBlock)[statKey]);

    return {
      high: Math.max(...values),
      low: Math.min(...values),
    };
  };

  // ─── DPS CALCULATOR TAB ───
  const dpsData = useMemo(() => {
    const skill = skills[selectedSkill] as SkillTemplate | undefined;
    if (!skill) return null;

    const classTemplate = classes[selectedClass] as ClassTemplate;
    if (!classTemplate) return null;

    const stats = computeStats(selectedClass, selectedLevel);
    if (!stats) return null;

    const baseDamage = skill.baseDamage || [0, 0];
    const avgBaseDamage = (baseDamage[0] + baseDamage[1]) / 2;

    const scalingStat = stats[skill.scalingStat as keyof StatBlock] || 0;
    const scalingFactor = skill.category === 'offensive' ? 0.8 : 0.9;
    const scaledDamage = avgBaseDamage + (scalingStat * scalingFactor);

    const castTime = skill.castTimeMs;
    const cooldown = skill.cooldownMs;
    const timeBetweenCasts = Math.max(castTime, cooldown);

    const baseCritChance = stats.critChance || 0.05;
    const avgDmgWithCrit = scaledDamage * (1 + baseCritChance * (stats.critDamage || 0.5));

    const dps = (avgDmgWithCrit / timeBetweenCasts) * 1000;

    return {
      skill,
      baseDamage,
      avgBaseDamage,
      scalingStat,
      scaledDamage,
      avgDmgWithCrit,
      dps,
      castTime,
      cooldown,
      resourceCost: skill.resourceCost,
      costPerDamage: skill.resourceCost / scaledDamage,
    };
  }, [selectedClass, selectedLevel, selectedSkill, skills, classes]);

  // ─── SKILL OVERVIEW TAB ───
  const allSkillsData = useMemo(() => {
    return Object.entries(skills)
      .map(([id, skill]) => {
        const sk = skill as SkillTemplate;
        const cls = Object.values(classes).find(c => (c as ClassTemplate).id === sk.classId);
        return {
          ...sk,
          id,
          className: (cls as ClassTemplate)?.name || sk.classId,
        };
      })
      .sort((a, b) => {
        if (a.className !== b.className) return a.className.localeCompare(b.className);
        return (a.levelRequired || 0) - (b.levelRequired || 0);
      });
  }, [skills, classes]);

  const [skillSortKey, setSkillSortKey] = useState<string>('className');
  const [skillSortDesc, setSkillSortDesc] = useState(false);

  const sortedSkills = useMemo(() => {
    let sorted = [...allSkillsData];
    sorted.sort((a, b) => {
      let aVal = (a as any)[skillSortKey];
      let bVal = (b as any)[skillSortKey];

      if (aVal < bVal) return skillSortDesc ? 1 : -1;
      if (aVal > bVal) return skillSortDesc ? -1 : 1;
      return 0;
    });
    return sorted;
  }, [allSkillsData, skillSortKey, skillSortDesc]);

  return (
    <div className="panel">
      <div className="panel-header">Balance Dashboard</div>
      <div className="panel-body">
        <div className="tabs">
          {(['comparison', 'dps', 'overview'] as const).map(tab => (
            <button
              key={tab}
              className={`tab ${activeTab === tab ? 'active' : ''}`}
              onClick={() => setActiveTab(tab)}
            >
              {tab === 'comparison' && '📊 Class Comparison'}
              {tab === 'dps' && '⚡ DPS Calculator'}
              {tab === 'overview' && '📋 Skill Overview'}
            </button>
          ))}
        </div>

        {/* CLASS COMPARISON */}
        {activeTab === 'comparison' && (
          <div style={{ marginTop: 16, overflowX: 'auto' }}>
            <table className="data-table" style={{ minWidth: 1200 }}>
              <thead>
                <tr>
                  <th>Class</th>
                  <th>Level</th>
                  {STAT_DISPLAY_KEYS.map(key => (
                    <th key={key}>{STAT_LABELS[key]}</th>
                  ))}
                </tr>
              </thead>
              <tbody>
                {comparisonData.map((row, i) => (
                  <tr key={i}>
                    <td style={{ fontWeight: 'bold', color: `#${classColors[row.classId]?.toString(16).padStart(6, '0')}` || '#ccc' }}>
                      {row.className}
                    </td>
                    <td>{row.level}</td>
                    {STAT_DISPLAY_KEYS.map(key => {
                      const val = row.stats[key];
                      const { high, low } = getHighLow(key, row.level);

                      let bgColor = 'transparent';
                      if (val === high) bgColor = 'rgba(68, 255, 68, 0.1)';
                      else if (val === low) bgColor = 'rgba(255, 68, 68, 0.1)';

                      const display = key === 'critChance' || key === 'blockRating' || key === 'dodgeRating'
                        ? `${(val * 100).toFixed(1)}%`
                        : val.toFixed(1);

                      return (
                        <td key={key} style={{ backgroundColor: bgColor, textAlign: 'right' }}>
                          {display}
                        </td>
                      );
                    })}
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}

        {/* DPS CALCULATOR */}
        {activeTab === 'dps' && (
          <div style={{ marginTop: 16 }}>
            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr 1fr', gap: 12, marginBottom: 16 }}>
              <div className="form-group">
                <label className="form-label">Class</label>
                <select
                  className="form-select"
                  value={selectedClass}
                  onChange={(e) => {
                    setSelectedClass(e.target.value);
                    setSelectedSkill('');
                  }}
                >
                  {classList.map(cls => (
                    <option key={cls.id} value={cls.id}>{cls.name}</option>
                  ))}
                </select>
              </div>

              <div className="form-group">
                <label className="form-label">Level</label>
                <input
                  className="form-input"
                  type="number"
                  min="1"
                  max="20"
                  value={selectedLevel}
                  onChange={(e) => setSelectedLevel(Math.max(1, Math.min(20, parseInt(e.target.value, 10))))}
                />
              </div>

              <div className="form-group">
                <label className="form-label">Skill</label>
                <select
                  className="form-select"
                  value={selectedSkill}
                  onChange={(e) => setSelectedSkill(e.target.value)}
                >
                  <option value="">-- Select Skill --</option>
                  {skillsForClass.map(skill => (
                    <option key={(skill as any).id} value={(skill as any).id}>
                      {(skill as any).name} (Lvl {(skill as any).levelRequired})
                    </option>
                  ))}
                </select>
              </div>
            </div>

            {dpsData && (
              <div style={{
                background: 'rgba(255,255,255,0.05)',
                border: '1px solid rgba(255,255,255,0.1)',
                borderRadius: 8,
                padding: 16,
              }}>
                <h3 style={{ marginTop: 0, marginBottom: 12 }}>{dpsData.skill.name}</h3>

                <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12 }}>
                  <div>
                    <div className="form-label">Base Damage Range</div>
                    <div style={{ fontSize: 16, fontWeight: 'bold' }}>
                      {dpsData.baseDamage[0]} - {dpsData.baseDamage[1]} avg {dpsData.avgBaseDamage.toFixed(1)}
                    </div>
                  </div>

                  <div>
                    <div className="form-label">Scaling Stat ({dpsData.skill.scalingStat})</div>
                    <div style={{ fontSize: 16, fontWeight: 'bold' }}>
                      {dpsData.scalingStat.toFixed(1)}
                    </div>
                  </div>

                  <div>
                    <div className="form-label">Scaled Damage (with 0.8× modifier)</div>
                    <div style={{ fontSize: 16, fontWeight: 'bold', color: '#88ff88' }}>
                      {dpsData.scaledDamage.toFixed(1)}
                    </div>
                  </div>

                  <div>
                    <div className="form-label">With Crit (avg)</div>
                    <div style={{ fontSize: 16, fontWeight: 'bold', color: '#ffdd88' }}>
                      {dpsData.avgDmgWithCrit.toFixed(1)}
                    </div>
                  </div>

                  <div>
                    <div className="form-label">Effective DPS</div>
                    <div style={{ fontSize: 18, fontWeight: 'bold', color: '#ff8888' }}>
                      {dpsData.dps.toFixed(2)} damage/sec
                    </div>
                  </div>

                  <div>
                    <div className="form-label">Resource Cost per Damage</div>
                    <div style={{ fontSize: 16, fontWeight: 'bold' }}>
                      {dpsData.costPerDamage.toFixed(3)} {dpsData.skill.resourceType}/dmg
                    </div>
                  </div>
                </div>

                <div style={{ marginTop: 16, paddingTop: 16, borderTop: '1px solid rgba(255,255,255,0.1)' }}>
                  <div className="form-label">Cast/Cooldown Times</div>
                  <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr 1fr' }}>
                    <div>
                      <div style={{ fontSize: 12, color: '#aaa' }}>Cast Time</div>
                      <div style={{ fontSize: 14, fontWeight: 'bold' }}>{dpsData.castTime}ms</div>
                    </div>
                    <div>
                      <div style={{ fontSize: 12, color: '#aaa' }}>Cooldown</div>
                      <div style={{ fontSize: 14, fontWeight: 'bold' }}>{dpsData.cooldown}ms</div>
                    </div>
                    <div>
                      <div style={{ fontSize: 12, color: '#aaa' }}>Resource Cost</div>
                      <div style={{ fontSize: 14, fontWeight: 'bold' }}>{dpsData.resourceCost}</div>
                    </div>
                  </div>
                </div>
              </div>
            )}

            {!selectedSkill && (
              <div className="empty-state">
                <p>Select a skill to calculate DPS</p>
              </div>
            )}
          </div>
        )}

        {/* SKILL OVERVIEW */}
        {activeTab === 'overview' && (
          <div style={{ marginTop: 16, overflowX: 'auto' }}>
            <table className="data-table" style={{ minWidth: 1200 }}>
              <thead>
                <tr>
                  {[
                    { key: 'className', label: 'Class' },
                    { key: 'name', label: 'Name' },
                    { key: 'levelRequired', label: 'Lvl' },
                    { key: 'castTimeMs', label: 'Cast' },
                    { key: 'cooldownMs', label: 'CD' },
                    { key: 'resourceCost', label: 'Cost' },
                    { key: 'baseDamage', label: 'Damage' },
                    { key: 'baseHealing', label: 'Healing' },
                    { key: 'category', label: 'Category' },
                  ].map(col => (
                    <th
                      key={col.key}
                      onClick={() => {
                        if (skillSortKey === col.key) {
                          setSkillSortDesc(!skillSortDesc);
                        } else {
                          setSkillSortKey(col.key);
                          setSkillSortDesc(false);
                        }
                      }}
                      style={{ cursor: 'pointer', userSelect: 'none' }}
                    >
                      {col.label} {skillSortKey === col.key && (skillSortDesc ? '▼' : '▲')}
                    </th>
                  ))}
                </tr>
              </thead>
              <tbody>
                {sortedSkills.map(skill => (
                  <tr key={skill.id}>
                    <td>{skill.className}</td>
                    <td>{skill.name}</td>
                    <td>{skill.levelRequired || 1}</td>
                    <td>{skill.castTimeMs}ms</td>
                    <td>{skill.cooldownMs}ms</td>
                    <td>{skill.resourceCost}</td>
                    <td>
                      {skill.baseDamage
                        ? `${skill.baseDamage[0]}-${skill.baseDamage[1]}`
                        : '—'}
                    </td>
                    <td>
                      {skill.baseHealing
                        ? `${skill.baseHealing[0]}-${skill.baseHealing[1]}`
                        : '—'}
                    </td>
                    <td>
                      <span className="badge">{skill.category}</span>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>
    </div>
  );
};
