import React, { useEffect, useState } from 'react';
import { EditorLayout } from './components/layout/EditorLayout';
import { useEditorStore } from './store/editorStore';

export default function App() {
  const loadAll = useEditorStore(s => s.loadAll);
  const [loaded, setLoaded] = useState(false);

  useEffect(() => {
    loadAll().then(() => setLoaded(true));
  }, [loadAll]);

  if (!loaded) {
    return (
      <div className="loading-screen">
        <div className="loading-spinner" />
        <p>Loading game data...</p>
      </div>
    );
  }

  return <EditorLayout />;
}
