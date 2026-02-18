import React, { useRef, useEffect, useCallback } from 'react';
import { useEditorStore } from '../../store/editorStore';
import { useUndoRedoStore } from '../../store/undoRedo';

export const Toolbar: React.FC = () => {
  const saveAll = useEditorStore(s => s.saveAll);
  const importAll = useEditorStore(s => s.importAll);
  const items = useEditorStore(s => s.items);
  const skills = useEditorStore(s => s.skills);
  const classes = useEditorStore(s => s.classes);
  const zones = useEditorStore(s => s.zones);
  const npcTemplates = useEditorStore(s => s.npcTemplates);
  const lootTables = useEditorStore(s => s.lootTables);
  const uiConfig = useEditorStore(s => s.uiConfig);

  const undo = useUndoRedoStore(s => s.undo);
  const redo = useUndoRedoStore(s => s.redo);
  const undoStack = useUndoRedoStore(s => s.undoStack);
  const redoStack = useUndoRedoStore(s => s.redoStack);

  const fileInputRef = useRef<HTMLInputElement>(null);

  const anyDirty = items.isDirty || skills.isDirty || classes.isDirty ||
    zones.isDirty || npcTemplates.isDirty || lootTables.isDirty || uiConfig.isDirty;

  const handleSave = useCallback(async () => {
    try {
      await saveAll();
    } catch (err) {
      console.error('Save failed:', err);
    }
  }, [saveAll]);

  const handleImport = () => {
    fileInputRef.current?.click();
  };

  const handleFileSelect = (event: React.ChangeEvent<HTMLInputElement>) => {
    const file = event.target.files?.[0];
    if (!file) return;

    const reader = new FileReader();
    reader.onload = (e) => {
      try {
        const data = JSON.parse(e.target?.result as string);
        importAll(data);
        console.log('Data imported successfully');
      } catch (err) {
        console.error('Failed to parse JSON:', err);
        alert('Failed to import: Invalid JSON file');
      }
    };
    reader.readAsText(file);

    if (fileInputRef.current) {
      fileInputRef.current.value = '';
    }
  };

  const handleExport = () => {
    const exportData = {
      items: items.data,
      skills: skills.data,
      classes: classes.data,
      zones: zones.data,
      npcTemplates: npcTemplates.data,
      lootTables: lootTables.data,
      uiConfig: uiConfig.data,
    };

    const json = JSON.stringify(exportData, null, 2);
    const blob = new Blob([json], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = `valhalla-content-export-${new Date().toISOString().split('T')[0]}.json`;
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    URL.revokeObjectURL(url);
  };

  // Global keyboard listeners for Ctrl+Z, Ctrl+Shift+Z, Ctrl+S
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if ((e.ctrlKey || e.metaKey) && e.key === 'z' && !e.shiftKey) {
        e.preventDefault();
        undo();
      }
      if ((e.ctrlKey || e.metaKey) && e.key === 'z' && e.shiftKey) {
        e.preventDefault();
        redo();
      }
      if ((e.ctrlKey || e.metaKey) && e.key === 's') {
        e.preventDefault();
        handleSave();
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [undo, redo, handleSave]);

  return (
    <div className="toolbar">
      <button className={`toolbar-btn ${anyDirty ? 'primary' : ''}`} onClick={handleSave} title="Save All (Ctrl+S)">
        💾 Save All
      </button>

      <div className="toolbar-divider" />

      <button
        className={`toolbar-btn ${undoStack.length > 0 ? '' : 'disabled'}`}
        onClick={undo}
        disabled={undoStack.length === 0}
        title="Undo (Ctrl+Z)"
      >
        ↶ Undo
      </button>
      <button
        className={`toolbar-btn ${redoStack.length > 0 ? '' : 'disabled'}`}
        onClick={redo}
        disabled={redoStack.length === 0}
        title="Redo (Ctrl+Shift+Z)"
      >
        ↷ Redo
      </button>

      <div className="toolbar-divider" />

      <button className="toolbar-btn" onClick={handleImport} title="Import JSON bundle">
        📥 Import
      </button>
      <button className="toolbar-btn" onClick={handleExport} title="Export all data as JSON">
        📤 Export
      </button>

      <input
        ref={fileInputRef}
        type="file"
        accept=".json"
        onChange={handleFileSelect}
        style={{ display: 'none' }}
      />

      {anyDirty && (
        <span style={{ color: 'var(--warning)', fontSize: 11, marginLeft: 8 }}>
          • Unsaved changes
        </span>
      )}
    </div>
  );
};
