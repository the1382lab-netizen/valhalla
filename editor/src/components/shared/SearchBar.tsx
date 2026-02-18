import React, { useState, useEffect, useRef, useMemo } from 'react';
import { useEditorStore } from '../../store/editorStore';

interface SearchResult {
  id: string;
  name: string;
  type: 'item' | 'skill' | 'class' | 'npc' | 'lootTable';
  section: 'items' | 'skills' | 'classes' | 'npcs' | 'loot';
  description?: string;
}

interface GroupedResults {
  items: SearchResult[];
  skills: SearchResult[];
  classes: SearchResult[];
  npcs: SearchResult[];
  lootTables: SearchResult[];
}

export const SearchBar: React.FC = () => {
  const [isOpen, setIsOpen] = useState(false);
  const [query, setQuery] = useState('');
  const [selectedIndex, setSelectedIndex] = useState(0);
  const inputRef = useRef<HTMLInputElement>(null);

  const itemsData = useEditorStore((s) => s.items.data);
  const skillsData = useEditorStore((s) => s.skills.data);
  const classesData = useEditorStore((s) => s.classes.data);
  const npcsData = useEditorStore((s) => s.npcTemplates.data);
  const lootTablesData = useEditorStore((s) => s.lootTables.data);

  const setActiveSection = useEditorStore((s) => s.setActiveSection);
  const setSelectedItemId = useEditorStore((s) => s.setSelectedItemId);
  const setSelectedSkillId = useEditorStore((s) => s.setSelectedSkillId);
  const setSelectedClassId = useEditorStore((s) => s.setSelectedClassId);
  const setSelectedNpcId = useEditorStore((s) => s.setSelectedNpcId);
  const setSelectedLootTableId = useEditorStore((s) => s.setSelectedLootTableId);

  // Keyboard shortcut to open search
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if ((e.ctrlKey || e.metaKey) && e.key === 'k') {
        e.preventDefault();
        setIsOpen(true);
        setQuery('');
        setSelectedIndex(0);
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, []);

  // Auto-focus input when opened
  useEffect(() => {
    if (isOpen && inputRef.current) {
      inputRef.current.focus();
    }
  }, [isOpen]);

  // Search across all data
  const results = useMemo(() => {
    if (!query.trim()) {
      return { items: [], skills: [], classes: [], npcs: [], lootTables: [] };
    }

    const lowerQuery = query.toLowerCase();
    const grouped: GroupedResults = {
      items: [],
      skills: [],
      classes: [],
      npcs: [],
      lootTables: [],
    };

    // Search items
    Object.entries(itemsData).forEach(([id, item]) => {
      if (
        id.toLowerCase().includes(lowerQuery) ||
        item.name?.toLowerCase().includes(lowerQuery) ||
        item.description?.toLowerCase().includes(lowerQuery)
      ) {
        grouped.items.push({
          id,
          name: item.name || id,
          type: 'item',
          section: 'items',
          description: item.description,
        });
      }
    });

    // Search skills
    if (skillsData.skills) {
      Object.entries(skillsData.skills).forEach(([id, skill]) => {
        if (
          id.toLowerCase().includes(lowerQuery) ||
          skill.name?.toLowerCase().includes(lowerQuery) ||
          skill.description?.toLowerCase().includes(lowerQuery)
        ) {
          grouped.skills.push({
            id,
            name: skill.name || id,
            type: 'skill',
            section: 'skills',
            description: skill.description,
          });
        }
      });
    }

    // Search classes
    if (classesData.classes) {
      Object.entries(classesData.classes).forEach(([id, cls]) => {
        if (
          id.toLowerCase().includes(lowerQuery) ||
          cls.name?.toLowerCase().includes(lowerQuery) ||
          cls.description?.toLowerCase().includes(lowerQuery)
        ) {
          grouped.classes.push({
            id,
            name: cls.name || id,
            type: 'class',
            section: 'classes',
            description: cls.description,
          });
        }
      });
    }

    // Search NPCs
    Object.entries(npcsData).forEach(([id, npc]) => {
      if (
        id.toLowerCase().includes(lowerQuery) ||
        npc.name?.toLowerCase().includes(lowerQuery) ||
        npc.description?.toLowerCase().includes(lowerQuery)
      ) {
        grouped.npcs.push({
          id,
          name: npc.name || id,
          type: 'npc',
          section: 'npcs',
          description: npc.description,
        });
      }
    });

    // Search loot tables
    Object.entries(lootTablesData).forEach(([id, table]) => {
      if (id.toLowerCase().includes(lowerQuery) || table.name?.toLowerCase().includes(lowerQuery)) {
        grouped.lootTables.push({
          id,
          name: table.name || id,
          type: 'lootTable',
          section: 'loot',
        });
      }
    });

    return grouped;
  }, [query, itemsData, skillsData, classesData, npcsData, lootTablesData]);

  // Flatten results for navigation
  const flatResults = useMemo(() => {
    return [
      ...results.items,
      ...results.skills,
      ...results.classes,
      ...results.npcs,
      ...results.lootTables,
    ];
  }, [results]);

  // Handle keyboard navigation
  const handleKeyDown = (e: React.KeyboardEvent<HTMLInputElement>) => {
    switch (e.key) {
      case 'Escape':
        setIsOpen(false);
        break;
      case 'ArrowDown':
        e.preventDefault();
        setSelectedIndex((prev) => (prev + 1) % Math.max(flatResults.length, 1));
        break;
      case 'ArrowUp':
        e.preventDefault();
        setSelectedIndex((prev) => (prev - 1 + Math.max(flatResults.length, 1)) % Math.max(flatResults.length, 1));
        break;
      case 'Enter':
        e.preventDefault();
        if (flatResults.length > 0) {
          handleSelectResult(flatResults[selectedIndex]);
        }
        break;
    }
  };

  const handleSelectResult = (result: SearchResult) => {
    setActiveSection(result.section);

    switch (result.type) {
      case 'item':
        setSelectedItemId(result.id);
        break;
      case 'skill':
        setSelectedSkillId(result.id);
        break;
      case 'class':
        setSelectedClassId(result.id);
        break;
      case 'npc':
        setSelectedNpcId(result.id);
        break;
      case 'lootTable':
        setSelectedLootTableId(result.id);
        break;
    }

    setIsOpen(false);
  };

  const getTypeBadgeColor = (type: SearchResult['type']): string => {
    const colors: Record<SearchResult['type'], string> = {
      item: '#8b7355',
      skill: '#4a90e2',
      class: '#7ed321',
      npc: '#f5a623',
      lootTable: '#bd10e0',
    };
    return colors[type];
  };

  const getTypeLabel = (type: SearchResult['type']): string => {
    const labels: Record<SearchResult['type'], string> = {
      item: 'Item',
      skill: 'Skill',
      class: 'Class',
      npc: 'NPC',
      lootTable: 'Loot Table',
    };
    return labels[type];
  };

  if (!isOpen) return null;

  return (
    <div className="search-overlay" onClick={() => setIsOpen(false)}>
      <div className="search-modal" onClick={(e) => e.stopPropagation()}>
        <div className="search-input-wrapper">
          <input
            ref={inputRef}
            type="text"
            placeholder="Search items, skills, classes, NPCs, loot tables... (Ctrl+K)"
            value={query}
            onChange={(e) => {
              setQuery(e.target.value);
              setSelectedIndex(0);
            }}
            onKeyDown={handleKeyDown}
            className="search-input"
          />
        </div>

        {query.trim() && flatResults.length === 0 && (
          <div className="search-empty">
            <p>No results found for "{query}"</p>
          </div>
        )}

        {query.trim() && flatResults.length > 0 && (
          <div className="search-results">
            {/* Items */}
            {results.items.length > 0 && (
              <div className="search-group">
                <h3 className="search-group-title">Items</h3>
                {results.items.map((result, idx) => (
                  <div
                    key={`${result.type}-${result.id}`}
                    className={`search-result-item ${
                      selectedIndex === idx ? 'selected' : ''
                    }`}
                    onClick={() => handleSelectResult(result)}
                  >
                    <span
                      className="search-type-badge"
                      style={{ backgroundColor: getTypeBadgeColor(result.type) }}
                    >
                      {getTypeLabel(result.type)}
                    </span>
                    <div className="search-result-content">
                      <div className="search-result-name">{result.name}</div>
                      {result.description && (
                        <div className="search-result-description">{result.description}</div>
                      )}
                      <div className="search-result-id">{result.id}</div>
                    </div>
                  </div>
                ))}
              </div>
            )}

            {/* Skills */}
            {results.skills.length > 0 && (
              <div className="search-group">
                <h3 className="search-group-title">Skills</h3>
                {results.skills.map((result, idx) => (
                  <div
                    key={`${result.type}-${result.id}`}
                    className={`search-result-item ${
                      selectedIndex === results.items.length + idx ? 'selected' : ''
                    }`}
                    onClick={() => handleSelectResult(result)}
                  >
                    <span
                      className="search-type-badge"
                      style={{ backgroundColor: getTypeBadgeColor(result.type) }}
                    >
                      {getTypeLabel(result.type)}
                    </span>
                    <div className="search-result-content">
                      <div className="search-result-name">{result.name}</div>
                      {result.description && (
                        <div className="search-result-description">{result.description}</div>
                      )}
                      <div className="search-result-id">{result.id}</div>
                    </div>
                  </div>
                ))}
              </div>
            )}

            {/* Classes */}
            {results.classes.length > 0 && (
              <div className="search-group">
                <h3 className="search-group-title">Classes</h3>
                {results.classes.map((result, idx) => (
                  <div
                    key={`${result.type}-${result.id}`}
                    className={`search-result-item ${
                      selectedIndex ===
                      results.items.length + results.skills.length + idx
                        ? 'selected'
                        : ''
                    }`}
                    onClick={() => handleSelectResult(result)}
                  >
                    <span
                      className="search-type-badge"
                      style={{ backgroundColor: getTypeBadgeColor(result.type) }}
                    >
                      {getTypeLabel(result.type)}
                    </span>
                    <div className="search-result-content">
                      <div className="search-result-name">{result.name}</div>
                      {result.description && (
                        <div className="search-result-description">{result.description}</div>
                      )}
                      <div className="search-result-id">{result.id}</div>
                    </div>
                  </div>
                ))}
              </div>
            )}

            {/* NPCs */}
            {results.npcs.length > 0 && (
              <div className="search-group">
                <h3 className="search-group-title">NPCs</h3>
                {results.npcs.map((result, idx) => (
                  <div
                    key={`${result.type}-${result.id}`}
                    className={`search-result-item ${
                      selectedIndex ===
                      results.items.length +
                        results.skills.length +
                        results.classes.length +
                        idx
                        ? 'selected'
                        : ''
                    }`}
                    onClick={() => handleSelectResult(result)}
                  >
                    <span
                      className="search-type-badge"
                      style={{ backgroundColor: getTypeBadgeColor(result.type) }}
                    >
                      {getTypeLabel(result.type)}
                    </span>
                    <div className="search-result-content">
                      <div className="search-result-name">{result.name}</div>
                      {result.description && (
                        <div className="search-result-description">{result.description}</div>
                      )}
                      <div className="search-result-id">{result.id}</div>
                    </div>
                  </div>
                ))}
              </div>
            )}

            {/* Loot Tables */}
            {results.lootTables.length > 0 && (
              <div className="search-group">
                <h3 className="search-group-title">Loot Tables</h3>
                {results.lootTables.map((result, idx) => (
                  <div
                    key={`${result.type}-${result.id}`}
                    className={`search-result-item ${
                      selectedIndex ===
                      results.items.length +
                        results.skills.length +
                        results.classes.length +
                        results.npcs.length +
                        idx
                        ? 'selected'
                        : ''
                    }`}
                    onClick={() => handleSelectResult(result)}
                  >
                    <span
                      className="search-type-badge"
                      style={{ backgroundColor: getTypeBadgeColor(result.type) }}
                    >
                      {getTypeLabel(result.type)}
                    </span>
                    <div className="search-result-content">
                      <div className="search-result-name">{result.name}</div>
                      {result.description && (
                        <div className="search-result-description">{result.description}</div>
                      )}
                      <div className="search-result-id">{result.id}</div>
                    </div>
                  </div>
                ))}
              </div>
            )}
          </div>
        )}

        {!query.trim() && (
          <div className="search-empty">
            <p>Start typing to search...</p>
            <div className="search-shortcuts">
              <p>Press ESC to close</p>
              <p>Use arrow keys to navigate</p>
              <p>Press ENTER to select</p>
            </div>
          </div>
        )}
      </div>

      <style>{`
        .search-overlay {
          position: fixed;
          top: 0;
          left: 0;
          right: 0;
          bottom: 0;
          background: rgba(0, 0, 0, 0.5);
          display: flex;
          align-items: center;
          justify-content: center;
          z-index: 1000;
          backdrop-filter: blur(2px);
        }

        .search-modal {
          background: #1e1e1e;
          border-radius: 8px;
          box-shadow: 0 20px 60px rgba(0, 0, 0, 0.8);
          width: 90%;
          max-width: 600px;
          max-height: 80vh;
          display: flex;
          flex-direction: column;
          color: #e0e0e0;
        }

        .search-input-wrapper {
          padding: 16px;
          border-bottom: 1px solid #333;
        }

        .search-input {
          width: 100%;
          padding: 12px 16px;
          font-size: 16px;
          background: #2a2a2a;
          border: 1px solid #444;
          border-radius: 6px;
          color: #e0e0e0;
          outline: none;
          transition: border-color 0.2s;
        }

        .search-input:focus {
          border-color: #666;
          background: #333;
        }

        .search-results {
          flex: 1;
          overflow-y: auto;
          padding: 8px 0;
        }

        .search-group {
          padding: 8px 0;
        }

        .search-group-title {
          padding: 8px 16px;
          font-size: 12px;
          font-weight: 600;
          text-transform: uppercase;
          color: #888;
          margin: 0;
        }

        .search-result-item {
          padding: 12px 16px;
          display: flex;
          align-items: flex-start;
          gap: 12px;
          cursor: pointer;
          transition: background-color 0.15s;
          border-left: 3px solid transparent;
        }

        .search-result-item:hover {
          background-color: #2a2a2a;
        }

        .search-result-item.selected {
          background-color: #333;
          border-left-color: #4a90e2;
        }

        .search-type-badge {
          padding: 2px 8px;
          border-radius: 4px;
          font-size: 11px;
          font-weight: 600;
          color: white;
          white-space: nowrap;
          margin-top: 2px;
        }

        .search-result-content {
          flex: 1;
          min-width: 0;
        }

        .search-result-name {
          font-weight: 500;
          color: #e0e0e0;
          margin-bottom: 4px;
          white-space: nowrap;
          overflow: hidden;
          text-overflow: ellipsis;
        }

        .search-result-description {
          font-size: 12px;
          color: #999;
          margin-bottom: 4px;
          white-space: nowrap;
          overflow: hidden;
          text-overflow: ellipsis;
        }

        .search-result-id {
          font-size: 11px;
          color: #666;
          font-family: monospace;
          white-space: nowrap;
          overflow: hidden;
          text-overflow: ellipsis;
        }

        .search-empty {
          padding: 48px 24px;
          text-align: center;
          color: #888;
        }

        .search-empty p {
          margin: 0 0 12px 0;
          font-size: 14px;
        }

        .search-shortcuts {
          margin-top: 20px;
          padding-top: 20px;
          border-top: 1px solid #333;
          font-size: 12px;
          color: #666;
        }

        .search-shortcuts p {
          margin: 4px 0;
        }
      `}</style>
    </div>
  );
};
