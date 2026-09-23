import React from 'react';
import { useEditorStore } from '../../store/editorStore';
import { useUndoRedoStore } from '../../store/undoRedo';

export const StatusBar: React.FC = () => {
  const items = useEditorStore(s => s.items);
  const skills = useEditorStore(s => s.skills);
  const classes = useEditorStore(s => s.classes);
  const zones = useEditorStore(s => s.zones);
  const npcTemplates = useEditorStore(s => s.npcTemplates);
  const lootTables = useEditorStore(s => s.lootTables);
  const uiConfig = useEditorStore(s => s.uiConfig);

  const undoStackDepth = useUndoRedoStore(s => s.getUndoCount());

  // Check if any section is dirty
  const anyDirty = items.isDirty || skills.isDirty || classes.isDirty ||
    zones.isDirty || npcTemplates.isDirty || lootTables.isDirty || uiConfig.isDirty;

  // Count items in each section
  const itemCount = Object.keys(items.data).length;
  const skillCount = Object.keys(skills.data.skills).length;
  const classCount = Object.keys(classes.data.classes).length;
  const npcCount = Object.keys(npcTemplates.data).length;
  const lootTableCount = Object.keys(lootTables.data).length;

  // Find the most recent lastSaved timestamp across all sections
  const timestamps = [
    items.lastSaved,
    skills.lastSaved,
    classes.lastSaved,
    zones.lastSaved,
    npcTemplates.lastSaved,
    lootTables.lastSaved,
    uiConfig.lastSaved,
  ].filter((ts): ts is number => ts !== null);

  const lastSavedTime = timestamps.length > 0 ? Math.max(...timestamps) : null;

  const formatTime = (timestamp: number) => {
    const date = new Date(timestamp);
    return date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
  };

  return (
    <div className="statusbar">
      <span>
        <span className={`statusbar-dot ${anyDirty ? 'dirty' : 'clean'}`} />
        {anyDirty ? 'Unsaved changes' : 'All saved'}
      </span>
      <span>{itemCount} items</span>
      <span>{skillCount} skills</span>
      <span>{classCount} classes</span>
      <span>{npcCount} NPCs</span>
      <span>{lootTableCount} loot tables</span>
      {lastSavedTime && (
        <span title={new Date(lastSavedTime).toISOString()}>
          Last saved: {formatTime(lastSavedTime)}
        </span>
      )}
      <span>Undo stack: {undoStackDepth}</span>
    </div>
  );
};
