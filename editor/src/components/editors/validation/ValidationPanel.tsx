import React, { useState, useEffect, useMemo } from 'react';
import { useEditorStore } from '../../../store/editorStore';

interface ValidationIssue {
  severity: 'error' | 'warning';
  category: 'items' | 'skills' | 'classes' | 'npcs' | 'loot' | 'zones';
  id: string;
  message: string;
}

export const ValidationPanel: React.FC = () => {
  const items = useEditorStore(s => s.items.data);
  const skillsData = useEditorStore(s => s.skills.data);
  const classesData = useEditorStore(s => s.classes.data);
  const npcTemplates = useEditorStore(s => s.npcTemplates.data);
  const lootTables = useEditorStore(s => s.lootTables.data);
  const zones = useEditorStore(s => s.zones.data);

  const [issues, setIssues] = useState<ValidationIssue[]>([]);
  const [isRunning, setIsRunning] = useState(false);
  const [selectedCategory, setSelectedCategory] = useState<string | null>(null);
  /** Valid Valhalla 2.0 art ids (UE equipment meshes). Empty = list unavailable → rule skipped. */
  const [meshIds, setMeshIds] = useState<string[]>([]);

  useEffect(() => {
    fetch('/api/assets/mesh-ids')
      .then(r => r.json())
      .then(data => setMeshIds(data.ids || []))
      .catch(() => setMeshIds([]));
  }, []);

  const validate = () => {
    setIsRunning(true);
    const foundIssues: ValidationIssue[] = [];

    // ─── Validate Items ───
    Object.entries(items || {}).forEach(([itemId, item]: [string, any]) => {
      if (!itemId) {
        foundIssues.push({
          severity: 'error',
          category: 'items',
          id: itemId,
          message: 'Item has empty ID',
        });
      }
      if (!item.name) {
        foundIssues.push({
          severity: 'error',
          category: 'items',
          id: itemId,
          message: `Item "${itemId}" missing name`,
        });
      }
      if (!item.category) {
        foundIssues.push({
          severity: 'warning',
          category: 'items',
          id: itemId,
          message: `Item "${item.name || itemId}" missing category`,
        });
      }

      // ── Valhalla 2.0 art: UE resolves meshId ?? spriteId against the
      // equipment import folder. Skipped when the art id list is unavailable.
      if (meshIds.length > 0 && item.category === 'equipment') {
        const artId: string | undefined = item.meshId || item.spriteId;
        if (!artId) {
          foundIssues.push({
            severity: 'warning',
            category: 'items',
            id: itemId,
            message: `Equipment "${item.name || itemId}" has no meshId or spriteId — Unreal has no mesh to show`,
          });
        } else if (!meshIds.includes(artId)) {
          foundIssues.push({
            severity: 'warning',
            category: 'items',
            id: itemId,
            message: `Equipment "${item.name || itemId}" art id "${artId}" (${item.meshId ? 'meshId' : 'spriteId'}) is not one of the ${meshIds.length} meshes in Import/Characters/Equipment`,
          });
        }
      }
    });

    // ─── Validate Skills ───
    const skillsObj = skillsData?.skills || {};
    const classSkillsObj = skillsData?.classSkills || {};
    const classesObj = classesData?.classes || {};

    Object.entries(skillsObj).forEach(([skillId, skill]: [string, any]) => {
      if (!skillId) {
        foundIssues.push({
          severity: 'error',
          category: 'skills',
          id: skillId,
          message: 'Skill has empty ID',
        });
      }
      if (!skill.name) {
        foundIssues.push({
          severity: 'error',
          category: 'skills',
          id: skillId,
          message: `Skill "${skillId}" missing name`,
        });
      }
      if (!skill.classId) {
        foundIssues.push({
          severity: 'error',
          category: 'skills',
          id: skillId,
          message: `Skill "${skill.name || skillId}" missing classId`,
        });
      } else if (!classesObj[skill.classId]) {
        foundIssues.push({
          severity: 'error',
          category: 'skills',
          id: skillId,
          message: `Skill "${skill.name || skillId}" references invalid class "${skill.classId}"`,
        });
      }
      if (skill.resourceCost < 0) {
        foundIssues.push({
          severity: 'error',
          category: 'skills',
          id: skillId,
          message: `Skill "${skill.name || skillId}" has negative resource cost`,
        });
      }
      if (skill.cooldownMs < 0) {
        foundIssues.push({
          severity: 'error',
          category: 'skills',
          id: skillId,
          message: `Skill "${skill.name || skillId}" has negative cooldown`,
        });
      }
    });

    // Validate classSkills references
    Object.entries(classSkillsObj).forEach(([className, skillIds]: [string, any]) => {
      if (!classesObj[className]) {
        foundIssues.push({
          severity: 'warning',
          category: 'skills',
          id: className,
          message: `classSkills references invalid class "${className}"`,
        });
      }

      if (!Array.isArray(skillIds)) {
        foundIssues.push({
          severity: 'error',
          category: 'skills',
          id: className,
          message: `classSkills[${className}] is not an array`,
        });
        return;
      }

      skillIds.forEach((skillId: string) => {
        if (!skillsObj[skillId]) {
          foundIssues.push({
            severity: 'error',
            category: 'skills',
            id: className,
            message: `classSkills[${className}] references invalid skill "${skillId}"`,
          });
        }
      });
    });

    // ─── Validate Classes ───
    Object.entries(classesObj).forEach(([classId, cls]: [string, any]) => {
      if (!classId) {
        foundIssues.push({
          severity: 'error',
          category: 'classes',
          id: classId,
          message: 'Class has empty ID',
        });
      }
      if (!cls.name) {
        foundIssues.push({
          severity: 'error',
          category: 'classes',
          id: classId,
          message: `Class "${classId}" missing name`,
        });
      }

      // Validate base stats
      if (cls.baseStats) {
        if ((cls.baseStats.hp || 0) <= 0) {
          foundIssues.push({
            severity: 'error',
            category: 'classes',
            id: classId,
            message: `Class "${cls.name || classId}" has invalid base HP (${cls.baseStats.hp})`,
          });
        }
        if ((cls.baseStats.critChance || 0) < 0 || (cls.baseStats.critChance || 0) > 1) {
          foundIssues.push({
            severity: 'warning',
            category: 'classes',
            id: classId,
            message: `Class "${cls.name || classId}" critChance not in [0,1]: ${cls.baseStats.critChance}`,
          });
        }
        if ((cls.baseStats.blockRating || 0) < 0 || (cls.baseStats.blockRating || 0) > 1) {
          foundIssues.push({
            severity: 'warning',
            category: 'classes',
            id: classId,
            message: `Class "${cls.name || classId}" blockRating not in [0,1]: ${cls.baseStats.blockRating}`,
          });
        }
      }

      // Validate vision range (line-of-sight distance in UE units / cm)
      if (cls.visionRange === undefined || cls.visionRange === null || cls.visionRange <= 0) {
        foundIssues.push({
          severity: 'error',
          category: 'classes',
          id: classId,
          message: `Class "${cls.name || classId}" has missing or non-positive visionRange (${cls.visionRange})`,
        });
      } else if (cls.visionRange > 3000) {
        foundIssues.push({
          severity: 'warning',
          category: 'classes',
          id: classId,
          message: `Class "${cls.name || classId}" visionRange is unusually large: ${cls.visionRange} cm`,
        });
      }

      // Validate per-level stats
      if (cls.statsPerLevel) {
        if ((cls.statsPerLevel.hp || 0) < 0) {
          foundIssues.push({
            severity: 'warning',
            category: 'classes',
            id: classId,
            message: `Class "${cls.name || classId}" has negative HP growth`,
          });
        }
      }
    });

    // ─── Validate NPCs ───
    Object.entries(npcTemplates || {}).forEach(([npcId, npc]: [string, any]) => {
      if (!npcId) {
        foundIssues.push({
          severity: 'error',
          category: 'npcs',
          id: npcId,
          message: 'NPC has empty ID',
        });
      }
      if (!npc.name) {
        foundIssues.push({
          severity: 'error',
          category: 'npcs',
          id: npcId,
          message: `NPC "${npcId}" missing name`,
        });
      }

      // Validate loot table reference
      if (npc.lootTableId && !lootTables?.[npc.lootTableId]) {
        foundIssues.push({
          severity: 'error',
          category: 'npcs',
          id: npcId,
          message: `NPC "${npc.name || npcId}" references invalid loot table "${npc.lootTableId}"`,
        });
      }

      // Validate skill references
      if (Array.isArray(npc.skills)) {
        npc.skills.forEach((skillId: string) => {
          if (!skillsObj[skillId]) {
            foundIssues.push({
              severity: 'error',
              category: 'npcs',
              id: npcId,
              message: `NPC "${npc.name || npcId}" references invalid skill "${skillId}"`,
            });
          }
        });
      }

      // Validate stats
      if (npc.stats) {
        if ((npc.stats.hp || 0) <= 0) {
          foundIssues.push({
            severity: 'error',
            category: 'npcs',
            id: npcId,
            message: `NPC "${npc.name || npcId}" has invalid HP (${npc.stats.hp})`,
          });
        }
      }
    });

    // ─── Validate Loot Tables ───
    Object.entries(lootTables || {}).forEach(([tableId, table]: [string, any]) => {
      if (!tableId) {
        foundIssues.push({
          severity: 'error',
          category: 'loot',
          id: tableId,
          message: 'Loot table has empty ID',
        });
      }
      if (!table.name) {
        foundIssues.push({
          severity: 'warning',
          category: 'loot',
          id: tableId,
          message: `Loot table "${tableId}" missing name`,
        });
      }

      // Validate item references
      if (Array.isArray(table.items)) {
        table.items.forEach((entry: any, idx: number) => {
          if (!entry.itemId) {
            foundIssues.push({
              severity: 'error',
              category: 'loot',
              id: tableId,
              message: `Loot table "${table.name || tableId}" item #${idx} missing itemId`,
            });
          } else if (!items?.[entry.itemId]) {
            foundIssues.push({
              severity: 'error',
              category: 'loot',
              id: tableId,
              message: `Loot table "${table.name || tableId}" references invalid item "${entry.itemId}"`,
            });
          }

          if ((entry.weight || 0) <= 0) {
            foundIssues.push({
              severity: 'warning',
              category: 'loot',
              id: tableId,
              message: `Loot table "${table.name || tableId}" item "${entry.itemId}" has invalid weight (${entry.weight})`,
            });
          }
        });
      }
    });

    // ─── Validate Zones ───
    Object.entries(zones || {}).forEach(([zoneId, zone]: [string, any]) => {
      if (!zoneId) {
        foundIssues.push({
          severity: 'error',
          category: 'zones',
          id: zoneId,
          message: 'Zone has empty ID',
        });
      }
      if (!zone.name) {
        foundIssues.push({
          severity: 'error',
          category: 'zones',
          id: zoneId,
          message: `Zone "${zoneId}" missing name`,
        });
      }
      if (!zone.mapFile) {
        foundIssues.push({
          severity: 'error',
          category: 'zones',
          id: zoneId,
          message: `Zone "${zone.name || zoneId}" missing mapFile`,
        });
      }
      if (!zone.defaultSpawn) {
        foundIssues.push({
          severity: 'error',
          category: 'zones',
          id: zoneId,
          message: `Zone "${zone.name || zoneId}" missing defaultSpawn`,
        });
      } else {
        if (typeof zone.defaultSpawn.x !== 'number' || typeof zone.defaultSpawn.y !== 'number') {
          foundIssues.push({
            severity: 'error',
            category: 'zones',
            id: zoneId,
            message: `Zone "${zone.name || zoneId}" defaultSpawn has invalid coordinates`,
          });
        }
      }
    });

    setIssues(foundIssues);
    setIsRunning(false);
  };

  useEffect(() => {
    validate();
  }, [items, skillsData, classesData, npcTemplates, lootTables, zones, meshIds]);

  const issuesByCategory = useMemo(() => {
    const grouped: Record<string, ValidationIssue[]> = {
      items: [],
      skills: [],
      classes: [],
      npcs: [],
      loot: [],
      zones: [],
    };

    issues.forEach(issue => {
      grouped[issue.category].push(issue);
    });

    return grouped;
  }, [issues]);

  const errorCount = issues.filter(i => i.severity === 'error').length;
  const warningCount = issues.filter(i => i.severity === 'warning').length;

  const filteredIssues = selectedCategory
    ? issuesByCategory[selectedCategory as keyof typeof issuesByCategory]
    : issues;

  return (
    <div className="panel">
      <div className="panel-header">
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 12 }}>
          <span>Validation Panel</span>
          <button
            className="btn btn-primary"
            onClick={validate}
            disabled={isRunning}
          >
            {isRunning ? '⏳ Running...' : '▶ Run Validation'}
          </button>
        </div>

        <div style={{
          display: 'flex',
          gap: 16,
          padding: 12,
          background: 'rgba(0,0,0,0.2)',
          borderRadius: 4,
          marginBottom: 12,
        }}>
          <div>
            <div className="form-label" style={{ marginBottom: 4 }}>Total Issues</div>
            <div style={{ fontSize: 20, fontWeight: 'bold' }}>{issues.length}</div>
          </div>
          <div>
            <div className="form-label" style={{ marginBottom: 4, color: '#ff8888' }}>Errors</div>
            <div style={{ fontSize: 20, fontWeight: 'bold', color: '#ff8888' }}>{errorCount}</div>
          </div>
          <div>
            <div className="form-label" style={{ marginBottom: 4, color: '#ffdd88' }}>Warnings</div>
            <div style={{ fontSize: 20, fontWeight: 'bold', color: '#ffdd88' }}>{warningCount}</div>
          </div>
        </div>
      </div>

      <div className="panel-body">
        {issues.length === 0 ? (
          <div className="empty-state">
            <div className="empty-state-icon">✅</div>
            <p>All validations passed!</p>
            <p style={{ fontSize: 12, color: 'var(--text-muted)', marginTop: 8 }}>
              Your game data is consistent and ready to use.
            </p>
          </div>
        ) : (
          <>
            <div style={{ marginBottom: 16 }}>
              <div className="form-label">Filter by Category</div>
              <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap' }}>
                <button
                  className={`btn ${!selectedCategory ? 'btn-primary' : 'btn-ghost'}`}
                  onClick={() => setSelectedCategory(null)}
                  style={{ fontSize: 12 }}
                >
                  All ({issues.length})
                </button>
                {(Object.keys(issuesByCategory) as Array<keyof typeof issuesByCategory>).map(cat => {
                  const count = issuesByCategory[cat].length;
                  return count > 0 ? (
                    <button
                      key={cat}
                      className={`btn ${selectedCategory === cat ? 'btn-primary' : 'btn-ghost'}`}
                      onClick={() => setSelectedCategory(cat)}
                      style={{ fontSize: 12 }}
                    >
                      {cat} ({count})
                    </button>
                  ) : null;
                })}
              </div>
            </div>

            <div style={{ overflowY: 'auto', maxHeight: 600 }}>
              {filteredIssues.length === 0 ? (
                <div style={{ padding: 16, textAlign: 'center', color: 'var(--text-muted)' }}>
                  No issues in this category
                </div>
              ) : (
                filteredIssues.map((issue, idx) => (
                  <div
                    key={idx}
                    style={{
                      padding: 12,
                      marginBottom: 8,
                      background: issue.severity === 'error' ? 'rgba(255, 68, 68, 0.1)' : 'rgba(255, 221, 136, 0.1)',
                      border: `1px solid ${issue.severity === 'error' ? '#ff4444' : '#ffdd88'}`,
                      borderRadius: 4,
                      borderLeft: `4px solid ${issue.severity === 'error' ? '#ff4444' : '#ffdd88'}`,
                    }}
                  >
                    <div style={{ display: 'flex', gap: 8, alignItems: 'flex-start', marginBottom: 4 }}>
                      <span
                        className="badge"
                        style={{
                          background: issue.severity === 'error' ? '#ff4444' : '#ffdd88',
                          color: '#000',
                          fontSize: 11,
                          padding: '2px 6px',
                          borderRadius: 3,
                          textTransform: 'uppercase',
                          fontWeight: 'bold',
                          flexShrink: 0,
                        }}
                      >
                        {issue.severity}
                      </span>
                      <span
                        className="badge"
                        style={{
                          background: 'rgba(255,255,255,0.1)',
                          fontSize: 11,
                          padding: '2px 6px',
                          borderRadius: 3,
                          flexShrink: 0,
                        }}
                      >
                        {issue.category}
                      </span>
                    </div>
                    <div style={{ fontSize: 14, lineHeight: 1.5 }}>{issue.message}</div>
                    {issue.id && (
                      <div style={{ fontSize: 11, color: '#aaa', marginTop: 6 }}>
                        ID: <code style={{ background: '#000', padding: '2px 4px', borderRadius: 2 }}>{issue.id}</code>
                      </div>
                    )}
                  </div>
                ))
              )}
            </div>
          </>
        )}
      </div>
    </div>
  );
};
