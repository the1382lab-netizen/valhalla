import React, { useState, useEffect } from 'react';
import { useEditorStore } from '../../../store/editorStore';
import { Overlay2MapEditor } from './MapEditor2';

/**
 * The Maps page: the Valhalla 2.0 overlay editor over each zone's Unreal
 * top-down capture (maps/thumbs/<zone>.png, overlay in maps/overlays-2.0).
 *
 * The 1.0 Tiled/isometric editor that used to live here was retired with the
 * 1.0 client (git tag archive/1.0-final).
 */
export const MapEditor: React.FC = () => {
  const zones = useEditorStore(s => s.zones.data);
  const [selectedZoneId, setSelectedZoneId] = useState<string | null>(null);
  const [thumbZones, setThumbZones] = useState<string[] | null>(null);

  useEffect(() => {
    fetch('/api/thumbs')
      .then(r => r.ok ? r.json() : { zones: [] })
      .then(d => setThumbZones(Array.isArray(d.zones) ? d.zones : []))
      .catch(() => setThumbZones([]));
  }, []);

  useEffect(() => {
    if (!selectedZoneId) {
      const firstId = Object.keys(zones || {})[0];
      if (firstId) setSelectedZoneId(firstId);
    }
  }, [zones]);

  const hasThumb = !!(selectedZoneId && thumbZones?.includes(selectedZoneId));
  const captureNote = !hasThumb && thumbZones !== null ? (
    <span style={{ fontSize: 10, color: 'var(--text-muted)' }} title="maps/thumbs/<zone>.png is missing">
      no capture
    </span>
  ) : null;

  if (thumbZones === null) {
    return (
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', height: '100%', color: 'var(--text-muted)' }}>
        Loading zone captures…
      </div>
    );
  }

  return <Overlay2MapEditor selectedZoneId={selectedZoneId} setSelectedZoneId={setSelectedZoneId} modeToggle={captureNote} />;
};
