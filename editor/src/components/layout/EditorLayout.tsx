import React from 'react';
import { Sidebar } from './Sidebar';
import { Toolbar } from './Toolbar';
import { StatusBar } from './StatusBar';
import { SearchBar } from '../shared/SearchBar';
import { useEditorStore } from '../../store/editorStore';
import { ItemEditor } from '../editors/items/ItemEditor';
import { SkillEditor } from '../editors/skills/SkillEditor';
import { ClassEditor } from '../editors/classes/ClassEditor';
import { NPCEditor } from '../editors/npcs/NPCEditor';
import { LootTableEditor } from '../editors/loot/LootTableEditor';
import { UILayoutEditor } from '../editors/ui/UILayoutEditor';
import { ZoneEditor } from '../editors/zones/ZoneEditor';
import { BalanceDashboard } from '../editors/balance/BalanceDashboard';
import { ValidationPanel } from '../editors/validation/ValidationPanel';
import { AdminDashboard } from '../editors/admin/AdminDashboard';

const editorComponents: Record<string, React.FC> = {
  items: ItemEditor,
  skills: SkillEditor,
  classes: ClassEditor,
  npcs: NPCEditor,
  loot: LootTableEditor,
  ui: UILayoutEditor,
  zones: ZoneEditor,
  balance: BalanceDashboard,
  validation: ValidationPanel,
  admin: AdminDashboard,
};

// Sections that manage their own layout — no outer padding needed
const FULL_BLEED_SECTIONS = new Set(['balance', 'validation', 'admin']);

export const EditorLayout: React.FC = () => {
  const activeSection = useEditorStore(s => s.activeSection);
  const EditorComponent = editorComponents[activeSection];
  const isFullBleed = FULL_BLEED_SECTIONS.has(activeSection);

  return (
    <div className="editor-root">
      <Sidebar />
      <Toolbar />
      <div className={`main-content${isFullBleed ? ' no-pad' : ''}`}>
        {EditorComponent ? <EditorComponent /> : (
          <div className="empty-state">
            <div className="empty-state-icon">🎮</div>
            <p>Select an editor from the sidebar</p>
          </div>
        )}
      </div>
      <StatusBar />
      <SearchBar />
    </div>
  );
};
