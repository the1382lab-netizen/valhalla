import React, { useState, useMemo } from 'react';
import { validateZoneAtmosphere } from '@valhalla/shared';
import { useEditorStore } from '../../../store/editorStore';

/**
 * The Zones page: zones.json. B-06 added it for the per-zone atmosphere
 * (fog, light, camera limit and sight range). Saving writes shared/data/zones.json;
 * a running game server with valhalla.DataHotReload on reloads it and every
 * client blends to the new profile within about a second.
 *
 * An empty atmosphere field means "today's global look" — the field is removed
 * from the JSON rather than written as 0.
 */

type AtmosphereNumberKey =
  | 'visionClearRadiusCm'
  | 'visionFadeWidthCm'
  | 'heightFogDensity'
  | 'heightFogStartCm'
  | 'sunIntensityScale'
  | 'skyLightIntensityScale'
  | 'cameraMaxArmCm'
  | 'netRelevancyRadiusCm'
  | 'firelightGlow'
  | 'firelightRangeCm';

type AtmosphereColorKey = 'fogColor' | 'gradeTint';

interface ZoneAtmosphere {
  notes?: string;
  visionClearRadiusCm?: number;
  visionFadeWidthCm?: number;
  fogColor?: string;
  heightFogDensity?: number;
  heightFogStartCm?: number;
  sunIntensityScale?: number;
  skyLightIntensityScale?: number;
  gradeTint?: string;
  cameraMaxArmCm?: number;
  netRelevancyRadiusCm?: number;
  firelightGlow?: number;
  firelightRangeCm?: number;
}

interface ZoneConfig {
  id: string;
  name: string;
  mapFile: string;
  defaultSpawn: { x: number; y: number };
  atmosphere?: ZoneAtmosphere;
}

const NUMBER_FIELDS: { key: AtmosphereNumberKey; label: string; step: number; hint: string }[] = [
  { key: 'visionClearRadiusCm', label: 'Vision clear radius (cm)', step: 50, hint: 'Ground distance from the player that stays clear. Empty = no vision fog.' },
  { key: 'visionFadeWidthCm', label: 'Vision fade width (cm)', step: 50, hint: 'Soft fade from clear to fully fogged.' },
  { key: 'heightFogDensity', label: 'Height fog density', step: 0.005, hint: "Horizon haze when the camera tilts up. Today's: 0.012." },
  { key: 'heightFogStartCm', label: 'Height fog start (cm)', step: 100, hint: "Distance from the camera. Today's: 1800." },
  { key: 'sunIntensityScale', label: 'Sun intensity ×', step: 0.05, hint: '1 = today, 0 = no sun.' },
  { key: 'skyLightIntensityScale', label: 'Sky light ×', step: 0.05, hint: 'Sky light and the cool fill light. 1 = today, 0 = off.' },
  { key: 'cameraMaxArmCm', label: 'Camera max zoom-out (cm)', step: 50, hint: 'Boom length. The default view is 1500; the normal limit is 2600.' },
  { key: 'netRelevancyRadiusCm', label: 'Relevancy radius (cm)', step: 50, hint: 'Server stops sending NPCs/players/loot beyond this. Caps the class vision range (1200–1800). Keep ≥ clear + fade.' },
  { key: 'firelightGlow', label: 'Firelight glow ×', step: 0.1, hint: 'Fires, braziers and lamp posts glow through the vision fog. 1 = default, 0 = off.' },
  { key: 'firelightRangeCm', label: 'Firelight range (cm)', step: 100, hint: 'Glows fade out beyond this distance. Empty = 1.5 × the fully fogged distance.' },
];

const COLOR_FIELDS: { key: AtmosphereColorKey; label: string; fallback: string; hint: string }[] = [
  { key: 'fogColor', label: 'Fog colour', fallback: '#8c978a', hint: 'Vision fog and height fog colour.' },
  { key: 'gradeTint', label: 'Grade tint', fallback: '#ffffff', hint: 'Multiplies the colour grade. White = none.' },
];

const HEX = /^#[0-9a-fA-F]{6}$/;

const hint: React.CSSProperties = { fontSize: 12, color: 'var(--text-muted)', marginTop: 4 };

export const ZoneEditor: React.FC = () => {
  const zones = useEditorStore(s => s.zones);
  const selectedZoneId = useEditorStore(s => s.selectedZoneId);
  const setSelectedZoneId = useEditorStore(s => s.setSelectedZoneId);
  const updateData = useEditorStore(s => s.updateData);
  const saveSection = useEditorStore(s => s.saveSection);

  const [saveError, setSaveError] = useState<string | null>(null);

  const zoneIds = useMemo(() => Object.keys(zones.data || {}).sort(), [zones.data]);

  const zone: ZoneConfig | null = selectedZoneId && zones.data[selectedZoneId]
    ? zones.data[selectedZoneId]
    : null;

  const updateZone = (updates: Partial<ZoneConfig>) => {
    if (!zone || !selectedZoneId) return;
    const next: ZoneConfig = { ...zone, ...updates };
    if (updates.atmosphere === undefined && 'atmosphere' in updates) {
      delete next.atmosphere;
    }
    updateData('zones', { ...zones.data, [selectedZoneId]: next });
  };

  const setAtmosphereField = (key: keyof ZoneAtmosphere, value: number | string | undefined) => {
    if (!zone) return;
    const next: ZoneAtmosphere = { ...(zone.atmosphere || {}) };
    if (value === undefined || value === '') {
      delete next[key];
    } else {
      (next as Record<string, unknown>)[key] = value;
    }
    updateZone({ atmosphere: next });
  };

  const handleSave = async () => {
    setSaveError(null);
    try {
      await saveSection('zones');
    } catch (err) {
      setSaveError(String(err));
    }
  };

  const atmosphere = zone?.atmosphere;
  const problems = validateZoneAtmosphere(atmosphere);
  const clear = atmosphere?.visionClearRadiusCm ?? 0;
  const fade = atmosphere?.visionFadeWidthCm ?? 0;

  return (
    <div className="split-horizontal">
      <div className="panel">
        <div className="panel-header">Zones ({zoneIds.length})</div>
        <div className="panel-body">
          <table className="data-table">
            <thead>
              <tr>
                <th>Name</th>
                <th>Id</th>
                <th>Atmosphere</th>
              </tr>
            </thead>
            <tbody>
              {zoneIds.map(id => {
                const z = zones.data[id];
                return (
                  <tr
                    key={id}
                    className={selectedZoneId === id ? 'selected' : ''}
                    onClick={() => setSelectedZoneId(id)}
                  >
                    <td>{z.name}</td>
                    <td>{id}</td>
                    <td>{z.atmosphere ? 'custom' : '—'}</td>
                  </tr>
                );
              })}
            </tbody>
          </table>
          <div style={hint}>
            A new zone's level comes from scaffold_zone in Unreal; its zones.json entry is added by hand (B-19).
          </div>
        </div>
      </div>

      <div className="panel">
        <div className="panel-header">Zone Details</div>
        <div className="panel-body" style={{ overflowY: 'auto', maxHeight: 'calc(100vh - 120px)' }}>
          {zone ? (
            <>
              <div className="form-row">
                <div className="form-group">
                  <label className="form-label">Name</label>
                  <input
                    className="form-input"
                    type="text"
                    value={zone.name}
                    onChange={(e) => updateZone({ name: e.target.value })}
                  />
                </div>
                <div className="form-group">
                  <label className="form-label">Id</label>
                  <input className="form-input" type="text" value={zone.id} disabled />
                </div>
              </div>

              <div className="form-row">
                <div className="form-group">
                  <label className="form-label">Default spawn X</label>
                  <input
                    className="form-input"
                    type="number"
                    value={zone.defaultSpawn?.x ?? 0}
                    onChange={(e) => updateZone({ defaultSpawn: { ...zone.defaultSpawn, x: parseFloat(e.target.value) || 0 } })}
                  />
                </div>
                <div className="form-group">
                  <label className="form-label">Default spawn Y</label>
                  <input
                    className="form-input"
                    type="number"
                    value={zone.defaultSpawn?.y ?? 0}
                    onChange={(e) => updateZone({ defaultSpawn: { ...zone.defaultSpawn, y: parseFloat(e.target.value) || 0 } })}
                  />
                </div>
              </div>

              <div className="panel-header" style={{ marginTop: 16, marginBottom: 8 }}>Atmosphere (fog, light, sight)</div>

              <div className="form-group">
                <label className="form-label">
                  <input
                    type="checkbox"
                    checked={!!atmosphere}
                    onChange={(e) => updateZone({ atmosphere: e.target.checked ? {} : undefined })}
                    style={{ marginRight: 6 }}
                  />
                  Custom atmosphere
                </label>
                <div style={hint}>
                  Off = today's global look. Any empty field below also keeps today's value.
                </div>
              </div>

              {atmosphere && (
                <>
                  <div className="form-group">
                    <label className="form-label">Notes</label>
                    <textarea
                      className="form-input"
                      rows={2}
                      value={atmosphere.notes || ''}
                      onChange={(e) => setAtmosphereField('notes', e.target.value || undefined)}
                    />
                  </div>

                  {NUMBER_FIELDS.map(f => (
                    <div className="form-group" key={f.key}>
                      <label className="form-label">{f.label}</label>
                      <input
                        className="form-input"
                        type="number"
                        min="0"
                        step={f.step}
                        placeholder="default"
                        value={atmosphere[f.key] ?? ''}
                        onChange={(e) => {
                          const raw = e.target.value;
                          const v = parseFloat(raw);
                          setAtmosphereField(f.key, raw === '' || !Number.isFinite(v) ? undefined : Math.max(0, v));
                        }}
                      />
                      <div style={hint}>{f.hint}</div>
                    </div>
                  ))}

                  {COLOR_FIELDS.map(f => {
                    const value = atmosphere[f.key];
                    return (
                      <div className="form-group" key={f.key}>
                        <label className="form-label">
                          <input
                            type="checkbox"
                            checked={value !== undefined}
                            onChange={(e) => setAtmosphereField(f.key, e.target.checked ? f.fallback : undefined)}
                            style={{ marginRight: 6 }}
                          />
                          {f.label}
                        </label>
                        {value !== undefined ? (
                          <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                            <input
                              type="color"
                              value={HEX.test(value) ? value : f.fallback}
                              onChange={(e) => setAtmosphereField(f.key, e.target.value)}
                            />
                            <input
                              className="form-input"
                              type="text"
                              value={value}
                              onChange={(e) => setAtmosphereField(f.key, e.target.value)}
                              style={{ maxWidth: 120 }}
                            />
                          </div>
                        ) : null}
                        <div style={hint}>{value === undefined ? 'Default. ' : ''}{f.hint}</div>
                      </div>
                    );
                  })}

                  <div style={{ ...hint, marginBottom: 8 }}>
                    {clear > 0
                      ? `Clear to ${(clear / 100).toFixed(1)} m, fully fogged at ${((clear + fade) / 100).toFixed(1)} m.`
                      : 'No vision fog.'}
                    {' '}The default camera shows about 7.4 m from the player to the screen corner; zoomed out to 2600 about 12.8 m.
                  </div>

                  {problems.length > 0 && (
                    <div style={{ color: 'var(--danger)', fontSize: 12, marginBottom: 8 }}>
                      {problems.map(p => <div key={p}>⚠ {p}</div>)}
                    </div>
                  )}
                </>
              )}

              <div style={{ display: 'flex', gap: 8, alignItems: 'center', marginTop: 12 }}>
                <button className="btn btn-primary" onClick={handleSave} disabled={!zones.isDirty}>
                  Save zones.json
                </button>
                {saveError && <span style={{ color: 'var(--danger)', fontSize: 12 }}>{saveError}</span>}
              </div>
            </>
          ) : (
            <div className="empty-state">
              <div className="empty-state-icon">🗺</div>
              <p>Select a zone</p>
            </div>
          )}
        </div>
      </div>
    </div>
  );
};
