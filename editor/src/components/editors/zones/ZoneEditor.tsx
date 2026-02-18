import React, { useState, useMemo } from 'react';
import { useEditorStore } from '../../../store/editorStore';

interface ZoneConfig {
  id: string;
  name: string;
  mapFile: string;
  defaultSpawn: {
    x: number;
    y: number;
  };
}

export const ZoneEditor: React.FC = () => {
  const zones = useEditorStore(s => s.zones);
  const selectedZoneId = useEditorStore(s => s.selectedZoneId);
  const setSelectedZoneId = useEditorStore(s => s.setSelectedZoneId);
  const updateData = useEditorStore(s => s.updateData);
  const saveSection = useEditorStore(s => s.saveSection);

  const [searchQuery, setSearchQuery] = useState('');

  const zoneList = useMemo(() => {
    const ids = Object.keys(zones.data);
    return ids
      .filter(id => {
        const zone = zones.data[id];
        return (
          zone.name.toLowerCase().includes(searchQuery.toLowerCase()) ||
          id.toLowerCase().includes(searchQuery.toLowerCase())
        );
      })
      .sort();
  }, [zones.data, searchQuery]);

  const selectedZone: ZoneConfig | null = selectedZoneId && zones.data[selectedZoneId]
    ? zones.data[selectedZoneId]
    : null;

  const handleSelectZone = (id: string) => {
    setSelectedZoneId(id);
  };

  const handleNewZone = () => {
    const newId = `zone_${Date.now()}`;
    const newZone: ZoneConfig = {
      id: newId,
      name: 'New Zone',
      mapFile: 'map.tmx',
      defaultSpawn: {
        x: 0,
        y: 0,
      },
    };
    const updatedZones = { ...zones.data, [newId]: newZone };
    updateData('zones', updatedZones);
    setSelectedZoneId(newId);
  };

  const handleDeleteZone = () => {
    if (!selectedZone) return;
    const updatedZones = { ...zones.data };
    delete updatedZones[selectedZoneId!];
    updateData('zones', updatedZones);
    setSelectedZoneId(null);
  };

  const handleUpdateZone = (updates: Partial<ZoneConfig>) => {
    if (!selectedZone) return;
    const updatedZones = {
      ...zones.data,
      [selectedZoneId!]: { ...selectedZone, ...updates },
    };
    updateData('zones', updatedZones);
  };

  const handleUpdateSpawn = (key: 'x' | 'y', value: number) => {
    if (!selectedZone) return;
    const newSpawn = { ...selectedZone.defaultSpawn, [key]: value };
    handleUpdateZone({ defaultSpawn: newSpawn });
  };

  const handleSave = async () => {
    try {
      await saveSection('zones');
    } catch (err) {
      console.error('Save failed:', err);
    }
  };

  return (
    <div className="split-horizontal">
      <div className="panel">
        <div className="panel-header">Zones ({zoneList.length})</div>
        <div className="panel-body">
          <div className="search-box">
            <span className="search-icon">🔍</span>
            <input
              type="text"
              placeholder="Search zones..."
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
            />
          </div>

          <div style={{ marginBottom: 12, display: 'flex', gap: 6 }}>
            <button className="btn btn-primary" onClick={handleNewZone}>+ New</button>
            <button className="btn btn-danger" onClick={handleDeleteZone} disabled={!selectedZone}>
              Delete
            </button>
          </div>

          <table className="data-table">
            <thead>
              <tr>
                <th>Name</th>
                <th>Map File</th>
              </tr>
            </thead>
            <tbody>
              {zoneList.map(id => {
                const zone = zones.data[id];
                return (
                  <tr
                    key={id}
                    className={selectedZoneId === id ? 'selected' : ''}
                    onClick={() => handleSelectZone(id)}
                  >
                    <td>{zone.name}</td>
                    <td>{zone.mapFile}</td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
      </div>

      <div className="panel">
        <div className="panel-header">Zone Details</div>
        <div className="panel-body" style={{ overflowY: 'auto', maxHeight: 'calc(100vh - 120px)' }}>
          {selectedZone ? (
            <>
              <div className="form-group">
                <label className="form-label">Name</label>
                <input
                  className="form-input"
                  type="text"
                  value={selectedZone.name}
                  onChange={(e) => handleUpdateZone({ name: e.target.value })}
                />
              </div>

              <div className="form-group">
                <label className="form-label">Map File (read-only)</label>
                <input
                  className="form-input"
                  type="text"
                  value={selectedZone.mapFile}
                  disabled
                  style={{ backgroundColor: 'var(--bg-tertiary)', cursor: 'not-allowed' }}
                />
                <div style={{ fontSize: 12, color: 'var(--text-muted)', marginTop: 4 }}>
                  Map files are managed by the Map Editor
                </div>
              </div>

              <div className="form-group">
                <label className="form-label">Default Spawn Point</label>
                <div className="form-row">
                  <div className="form-group">
                    <label className="form-label">X</label>
                    <input
                      className="form-input"
                      type="number"
                      value={selectedZone.defaultSpawn.x}
                      onChange={(e) => handleUpdateSpawn('x', parseFloat(e.target.value) || 0)}
                    />
                  </div>
                  <div className="form-group">
                    <label className="form-label">Y</label>
                    <input
                      className="form-input"
                      type="number"
                      value={selectedZone.defaultSpawn.y}
                      onChange={(e) => handleUpdateSpawn('y', parseFloat(e.target.value) || 0)}
                    />
                  </div>
                </div>
                <div
                  style={{
                    padding: 12,
                    backgroundColor: 'var(--bg-tertiary)',
                    borderRadius: 4,
                    marginTop: 8,
                    fontSize: 12,
                  }}
                >
                  <div style={{ color: 'var(--text-muted)' }}>Coordinates:</div>
                  <div style={{ marginTop: 4, fontFamily: 'monospace' }}>
                    ({selectedZone.defaultSpawn.x}, {selectedZone.defaultSpawn.y})
                  </div>
                </div>
              </div>

              <div
                style={{
                  padding: 12,
                  backgroundColor: 'var(--bg-tertiary)',
                  borderRadius: 4,
                  marginTop: 16,
                }}
              >
                <div style={{ fontSize: 12, fontWeight: 500, marginBottom: 8 }}>Zone Info</div>
                <div style={{ fontSize: 12, color: 'var(--text-muted)', lineHeight: 1.6 }}>
                  <div>ID: <span style={{ color: 'var(--text)' }}>{selectedZone.id}</span></div>
                  <div style={{ marginTop: 4 }}>
                    Spawn Distance: <span style={{ color: 'var(--text)' }}>
                      {Math.sqrt(
                        selectedZone.defaultSpawn.x ** 2 + selectedZone.defaultSpawn.y ** 2
                      ).toFixed(1)} units
                    </span>
                  </div>
                </div>
              </div>

              <button className="btn btn-primary" onClick={handleSave} style={{ width: '100%', marginTop: 16 }}>
                💾 Save Zone
              </button>
            </>
          ) : (
            <div className="empty-state">
              <div className="empty-state-icon">🌍</div>
              <p>Select a zone or create a new one</p>
            </div>
          )}
        </div>
      </div>
    </div>
  );
};
