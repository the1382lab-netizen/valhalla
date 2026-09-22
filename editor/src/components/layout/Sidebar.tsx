import React from 'react';
import { useEditorStore, EditorSection } from '../../store/editorStore';

interface NavItem {
  id: EditorSection;
  label: string;
  icon: string;
}

const sections: { label: string; items: NavItem[] }[] = [
  {
    label: 'Content',
    items: [
      { id: 'items', label: 'Items', icon: '⚔' },
      { id: 'skills', label: 'Skills & Spells', icon: '✨' },
      { id: 'classes', label: 'Classes', icon: '🛡' },
      { id: 'npcs', label: 'NPCs & Enemies', icon: '👹' },
      { id: 'loot', label: 'Loot Tables', icon: '💰' },
    ],
  },
  {
    label: 'World',
    items: [
      { id: 'maps', label: 'Map Objects', icon: '📍' },
    ],
  },
  {
    label: 'Interface',
    items: [
      { id: 'ui', label: 'UI Layout', icon: '🎨' },
    ],
  },
  {
    label: 'Tools',
    items: [
      { id: 'balance', label: 'Balance Dashboard', icon: '📊' },
      { id: 'validation', label: 'Validation', icon: '✅' },
    ],
  },
  {
    label: 'Server',
    items: [
      { id: 'admin', label: 'Live Dashboard', icon: '🖥' },
    ],
  },
];

export const Sidebar: React.FC = () => {
  const activeSection = useEditorStore(s => s.activeSection);
  const setActiveSection = useEditorStore(s => s.setActiveSection);

  return (
    <div className="sidebar">
      <div className="sidebar-header">VALHALLA</div>
      <nav className="sidebar-nav">
        {sections.map(section => (
          <React.Fragment key={section.label}>
            <div className="sidebar-section-label">{section.label}</div>
            {section.items.map(item => (
              <div
                key={item.id}
                className={`sidebar-item ${activeSection === item.id ? 'active' : ''}`}
                onClick={() => setActiveSection(item.id)}
              >
                <span className="sidebar-icon">{item.icon}</span>
                <span>{item.label}</span>
              </div>
            ))}
          </React.Fragment>
        ))}
      </nav>
    </div>
  );
};
