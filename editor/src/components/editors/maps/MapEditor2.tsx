/**
 * Overlay 2.0 map editor — Valhalla 2.0 (Unreal).
 *
 * The world lives in Unreal: each zone is a UE level with a top-down orthographic
 * capture at maps/thumbs/<zone>.png plus maps/thumbs/<zone>.json metadata. This
 * editor uses that capture as the canvas background and edits
 * maps/overlays-2.0/<zone>.json, whose coordinates are **zone-local centimetres**.
 *
 * Pixel ↔ cm:  cm = imagePixel / pixelsPerCm   (image-right = +X, image-down = +Y,
 * no origin term — originX/originY only place the zone in UE world space).
 */
import React, { useState, useEffect, useRef, useCallback, useMemo } from 'react';
import { useEditorStore } from '../../../store/editorStore';

// ─── Types ───────────────────────────────────────────────────────────────────

export type Spawn2Type = 'player_spawn' | 'enemy_spawn' | 'npc_spawn' | 'portal' | 'zone_entry';

export interface Spawn2 {
  id: string;
  type: Spawn2Type;
  x: number;          // zone-local cm
  y: number;          // zone-local cm
  templateId?: string;
  count?: number;
  radius?: number;    // cm
  targetZone?: string;
  targetEntry?: string;
  fromZone?: string;
  label?: string;
}

export interface ThumbMeta {
  originX: number;
  originY: number;
  sizeX: number;       // cm
  sizeY: number;       // cm
  pixelsPerCm: number;
  zoneId?: string;
  resolution?: number;
}

type Tool = 'select' | 'npc' | 'enemy' | 'player' | 'portal' | 'entry';

const TOOL_TYPE: Record<Exclude<Tool, 'select'>, Spawn2Type> = {
  npc: 'npc_spawn',
  enemy: 'enemy_spawn',
  player: 'player_spawn',
  portal: 'portal',
  entry: 'zone_entry',
};

const TOOL_ICONS: Record<Tool, string> = {
  select: '🖱', npc: '👤', enemy: '💀', player: '🎮', portal: '🌀', entry: '🚪',
};

const TOOL_LABELS: Record<Tool, string> = {
  select: 'Select / Move',
  npc: 'Place NPC',
  enemy: 'Place Enemy',
  player: 'Player Spawn',
  portal: 'Portal (Exit)',
  entry: 'Zone Entry',
};

const SPAWN_COLORS: Record<Spawn2Type, string> = {
  npc_spawn: '#ffdd44',
  enemy_spawn: '#ff4444',
  player_spawn: '#44ff88',
  portal: '#dd44ff',
  zone_entry: '#44ddff',
};

/** The UE server matches portals/entries to placed actors within this distance. */
const ACTOR_MATCH_CM = 128;
const DEFAULT_ENEMY_COUNT = 3;
const DEFAULT_ENEMY_RADIUS = 320;

// ─── Component ───────────────────────────────────────────────────────────────

interface Props {
  selectedZoneId: string | null;
  setSelectedZoneId: (id: string | null) => void;
  /** Rendered in the toolbar — lets the parent own the 1.0 / 2.0 switch. */
  modeToggle?: React.ReactNode;
}

export const Overlay2MapEditor: React.FC<Props> = ({ selectedZoneId, setSelectedZoneId, modeToggle }) => {
  const zones = useEditorStore(s => s.zones.data);
  const npcTemplates = useEditorStore(s => s.npcTemplates.data);

  const [meta, setMeta] = useState<ThumbMeta | null>(null);
  const [image, setImage] = useState<HTMLImageElement | null>(null);
  const [spawnPoints, setSpawnPoints] = useState<Spawn2[]>([]);
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [loadError, setLoadError] = useState<string | null>(null);
  const [isDirty, setIsDirty] = useState(false);
  const [saveStatus, setSaveStatus] = useState<'idle' | 'saving' | 'saved' | 'error'>('idle');
  const [saveErrors, setSaveErrors] = useState<string[]>([]);
  const [reloadStatus, setReloadStatus] = useState<string | null>(null);
  const [adminUrl, setAdminUrl] = useState<string>('');

  const [activeTool, setActiveTool] = useState<Tool>('select');
  const [toolTemplateId, setToolTemplateId] = useState<string>('');
  const [toolTargetZone, setToolTargetZone] = useState<string>('');
  const [toolFromZone, setToolFromZone] = useState<string>('');

  // Entry ids of other zones, for the portal targetEntry dropdown
  const [entryIdsByZone, setEntryIdsByZone] = useState<Record<string, string[]>>({});

  const canvasRef = useRef<HTMLCanvasElement>(null);
  const containerRef = useRef<HTMLDivElement>(null);
  const [zoom, setZoom] = useState(0.25);          // screen px per image px
  const [pan, setPan] = useState({ x: 0, y: 0 });
  const [isPanning, setIsPanning] = useState(false);
  const [isDragging, setIsDragging] = useState(false);
  const panStart = useRef({ mx: 0, my: 0, px: 0, py: 0 });
  const rafIdRef = useRef<number>(0);
  const [cursorCm, setCursorCm] = useState<{ x: number; y: number } | null>(null);

  const zoneList = useMemo(() => Object.values(zones || {}) as any[], [zones]);
  const npcList = useMemo(() => Object.values(npcTemplates || {}) as any[], [npcTemplates]);
  const npcOnlyList = useMemo(() => npcList.filter((n: any) => n.type && n.type !== 'enemy'), [npcList]);
  const selected = spawnPoints.find(s => s.id === selectedId) || null;

  const ppcm = meta?.pixelsPerCm ?? 0.5;

  useEffect(() => {
    fetch('/api/config').then(r => r.json()).then(c => setAdminUrl(c.adminUrl || '')).catch(() => {});
  }, []);

  // ── Load thumb metadata + image + overlay for the selected zone ────────────

  useEffect(() => {
    if (!selectedZoneId) return;
    let cancelled = false;
    setMeta(null); setImage(null); setSpawnPoints([]); setSelectedId(null);
    setLoadError(null); setIsDirty(false); setSaveErrors([]); setReloadStatus(null);

    (async () => {
      try {
        const metaRes = await fetch(`/api/thumbs/${selectedZoneId}.json`);
        if (!metaRes.ok) {
          throw new Error(
            `No top-down capture for "${selectedZoneId}". Generate one in Unreal with ` +
            `ValhallaLevelTools.capture_zone_topdown, or switch to 1.0 mode.`
          );
        }
        const m: ThumbMeta = await metaRes.json();
        if (cancelled) return;
        setMeta(m);

        const img = new Image();
        img.onload = () => { if (!cancelled) setImage(img); };
        img.onerror = () => { if (!cancelled) setLoadError(`Failed to load /api/thumbs/${selectedZoneId}.png`); };
        img.src = `/api/thumbs/${selectedZoneId}.png`;

        const ovRes = await fetch(`/api/overlays2/${selectedZoneId}`);
        if (ovRes.ok) {
          const ov = await ovRes.json();
          if (!cancelled) setSpawnPoints(Array.isArray(ov.spawnPoints) ? ov.spawnPoints : []);
        }
      } catch (err: any) {
        if (!cancelled) setLoadError(err.message || 'Failed to load zone');
      }
    })();

    return () => { cancelled = true; };
  }, [selectedZoneId]);

  // Fit the capture into the viewport whenever a new one loads
  useEffect(() => {
    if (!meta || !containerRef.current) return;
    const cw = containerRef.current.clientWidth;
    const ch = containerRef.current.clientHeight;
    const imgW = meta.sizeX * meta.pixelsPerCm;
    const imgH = meta.sizeY * meta.pixelsPerCm;
    if (!imgW || !imgH || !cw || !ch) return;
    const z = Math.min(cw / imgW, ch / imgH) * 0.96;
    setZoom(z);
    setPan({ x: (cw - imgW * z) / 2, y: (ch - imgH * z) / 2 });
  }, [meta]);

  // Entry ids of every zone that has an overlay — used by the portal target dropdown
  useEffect(() => {
    const ids = Object.keys(zones || {});
    ids.forEach(zid => {
      if (entryIdsByZone[zid]) return;
      fetch(`/api/overlays2/${zid}`)
        .then(r => r.ok ? r.json() : null)
        .then(ov => {
          if (!ov) return;
          const entries = (ov.spawnPoints || [])
            .filter((sp: Spawn2) => sp.type === 'zone_entry')
            .map((sp: Spawn2) => sp.id);
          setEntryIdsByZone(prev => ({ ...prev, [zid]: entries }));
        })
        .catch(() => {});
    });
  }, [zones]);

  // ── Coordinate conversion: zone-local cm ↔ canvas px ──────────────────────

  const cmToScreen = useCallback((x: number, y: number) => ({
    x: x * ppcm * zoom + pan.x,
    y: y * ppcm * zoom + pan.y,
  }), [ppcm, zoom, pan]);

  const screenToCm = useCallback((sx: number, sy: number) => ({
    x: (sx - pan.x) / (ppcm * zoom),
    y: (sy - pan.y) / (ppcm * zoom),
  }), [ppcm, zoom, pan]);

  // ── Rendering ─────────────────────────────────────────────────────────────

  const renderCanvas = useCallback(() => {
    const canvas = canvasRef.current;
    const container = containerRef.current;
    if (!canvas || !container || !meta) return;

    const cw = container.clientWidth;
    const ch = container.clientHeight;
    if (!cw || !ch) return;
    if (canvas.width !== cw || canvas.height !== ch) { canvas.width = cw; canvas.height = ch; }

    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    ctx.fillStyle = '#111118';
    ctx.fillRect(0, 0, cw, ch);

    const imgW = meta.sizeX * ppcm * zoom;
    const imgH = meta.sizeY * ppcm * zoom;

    // Background: the UE top-down capture
    if (image) {
      ctx.imageSmoothingEnabled = true;
      ctx.drawImage(image, pan.x, pan.y, imgW, imgH);
    } else {
      ctx.fillStyle = '#1b1b26';
      ctx.fillRect(pan.x, pan.y, imgW, imgH);
      ctx.fillStyle = '#555';
      ctx.font = '14px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText('loading capture…', pan.x + imgW / 2, pan.y + imgH / 2);
    }

    // Zone bounds
    ctx.strokeStyle = 'rgba(255,255,255,0.35)';
    ctx.lineWidth = 1;
    ctx.strokeRect(pan.x, pan.y, imgW, imgH);

    // 1 m grid once we are zoomed in enough to make it useful
    const stepCm = 500;
    const stepPx = stepCm * ppcm * zoom;
    if (stepPx > 24) {
      ctx.strokeStyle = 'rgba(255,255,255,0.08)';
      ctx.beginPath();
      for (let x = 0; x <= meta.sizeX; x += stepCm) {
        const s = pan.x + x * ppcm * zoom;
        ctx.moveTo(s, pan.y); ctx.lineTo(s, pan.y + imgH);
      }
      for (let y = 0; y <= meta.sizeY; y += stepCm) {
        const s = pan.y + y * ppcm * zoom;
        ctx.moveTo(pan.x, s); ctx.lineTo(pan.x + imgW, s);
      }
      ctx.stroke();
    }

    // Spawn points
    for (const sp of spawnPoints) {
      const s = cmToScreen(sp.x, sp.y);
      const isSel = sp.id === selectedId;
      const color = SPAWN_COLORS[sp.type] || '#ffffff';
      const size = isSel ? 9 : 7;

      ctx.save();
      if (isSel) { ctx.shadowColor = color; ctx.shadowBlur = 12; }
      ctx.fillStyle = color;
      ctx.strokeStyle = isSel ? '#ffffff' : color;
      ctx.lineWidth = isSel ? 2.5 : 1.5;

      if (sp.type === 'portal' || sp.type === 'zone_entry') {
        // Drawn as the actor-match box so the level author can see the tolerance
        const half = (ACTOR_MATCH_CM / 2) * ppcm * zoom;
        ctx.globalAlpha = isSel ? 0.45 : 0.28;
        ctx.fillRect(s.x - half, s.y - half, half * 2, half * 2);
        ctx.globalAlpha = 1;
        if (sp.type === 'zone_entry') ctx.setLineDash([4, 3]);
        ctx.strokeRect(s.x - half, s.y - half, half * 2, half * 2);
        ctx.setLineDash([]);
      } else if (sp.type === 'player_spawn') {
        ctx.beginPath();
        ctx.moveTo(s.x, s.y - size);
        ctx.lineTo(s.x + size, s.y + size);
        ctx.lineTo(s.x - size, s.y + size);
        ctx.closePath();
        ctx.fill(); ctx.stroke();
      } else {
        if (sp.type === 'enemy_spawn' && sp.radius) {
          const r = sp.radius * ppcm * zoom;
          ctx.globalAlpha = 0.18;
          ctx.beginPath(); ctx.arc(s.x, s.y, r, 0, Math.PI * 2); ctx.fill();
          ctx.globalAlpha = 0.6;
          ctx.setLineDash([3, 3]);
          ctx.beginPath(); ctx.arc(s.x, s.y, r, 0, Math.PI * 2); ctx.stroke();
          ctx.setLineDash([]);
          ctx.globalAlpha = 1;
        }
        ctx.beginPath(); ctx.arc(s.x, s.y, size, 0, Math.PI * 2);
        ctx.fill(); ctx.stroke();
      }

      ctx.shadowBlur = 0;
      ctx.globalAlpha = 1;
      ctx.fillStyle = '#ffffff';
      ctx.font = `${isSel ? 'bold ' : ''}${isSel ? 12 : 10}px sans-serif`;
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      const text = sp.label || sp.id;
      const shown = text.length > 18 ? text.slice(0, 17) + '…' : text;
      const countSuffix = sp.type === 'enemy_spawn' && sp.count ? ` ×${sp.count}` : '';
      ctx.fillText(shown + countSuffix, s.x, s.y + size + 11);
      ctx.restore();
    }
  }, [meta, image, spawnPoints, selectedId, zoom, pan, ppcm, cmToScreen]);

  useEffect(() => {
    cancelAnimationFrame(rafIdRef.current);
    rafIdRef.current = requestAnimationFrame(renderCanvas);
    return () => cancelAnimationFrame(rafIdRef.current);
  }, [renderCanvas]);

  useEffect(() => {
    const container = containerRef.current;
    if (!container) return;
    const ro = new ResizeObserver(() => {
      cancelAnimationFrame(rafIdRef.current);
      rafIdRef.current = requestAnimationFrame(renderCanvas);
    });
    ro.observe(container);
    return () => ro.disconnect();
  }, [renderCanvas]);

  // ── Interaction ───────────────────────────────────────────────────────────

  const canvasPos = (e: React.MouseEvent) => {
    const rect = (canvasRef.current ?? containerRef.current)!.getBoundingClientRect();
    return { cx: e.clientX - rect.left, cy: e.clientY - rect.top };
  };

  const findSpawnAt = useCallback((cx: number, cy: number): string | null => {
    let best: string | null = null;
    let bestDist = Infinity;
    const boxHalf = (ACTOR_MATCH_CM / 2) * ppcm * zoom;
    for (const sp of spawnPoints) {
      const s = cmToScreen(sp.x, sp.y);
      const d = Math.hypot(cx - s.x, cy - s.y);
      const hit = (sp.type === 'portal' || sp.type === 'zone_entry')
        ? (Math.abs(cx - s.x) <= Math.max(boxHalf, 8) && Math.abs(cy - s.y) <= Math.max(boxHalf, 8))
        : d < 14;
      if (hit && d < bestDist) { bestDist = d; best = sp.id; }
    }
    return best;
  }, [spawnPoints, cmToScreen, ppcm, zoom]);

  const makeId = (type: Spawn2Type) => {
    const base = `${type}_${selectedZoneId || 'zone'}`;
    let n = 0;
    let id = `${base}_${n}`;
    const taken = new Set(spawnPoints.map(s => s.id));
    while (taken.has(id)) { n += 1; id = `${base}_${n}`; }
    return id;
  };

  const handleMouseDown = useCallback((e: React.MouseEvent) => {
    if (!meta) return;
    e.preventDefault();

    if (e.button === 1 || e.button === 2) {
      setIsPanning(true);
      panStart.current = { mx: e.clientX, my: e.clientY, px: pan.x, py: pan.y };
      return;
    }

    const { cx, cy } = canvasPos(e);

    if (activeTool === 'select') {
      const hit = findSpawnAt(cx, cy);
      setSelectedId(hit);
      if (hit) setIsDragging(true);
      return;
    }

    const cm = screenToCm(cx, cy);
    const type = TOOL_TYPE[activeTool];
    const x = Math.round(cm.x);
    const y = Math.round(cm.y);

    let sp: Spawn2;
    if (type === 'npc_spawn' || type === 'enemy_spawn') {
      if (!toolTemplateId) return; // the panel already tells the user to pick a template
      const tpl = npcList.find((n: any) => n.id === toolTemplateId) as any;
      sp = {
        id: makeId(type), type, x, y,
        templateId: toolTemplateId,
        label: tpl?.name || toolTemplateId,
        ...(type === 'enemy_spawn' ? { count: DEFAULT_ENEMY_COUNT, radius: DEFAULT_ENEMY_RADIUS } : {}),
      };
    } else if (type === 'player_spawn') {
      sp = { id: makeId(type), type, x, y, label: `${(zones as any)?.[selectedZoneId!]?.name || selectedZoneId} spawn` };
    } else if (type === 'portal') {
      if (!toolTargetZone) return;
      const tz = (zones as any)?.[toolTargetZone];
      const entries = entryIdsByZone[toolTargetZone] || [];
      sp = {
        id: makeId(type), type, x, y,
        targetZone: toolTargetZone,
        targetEntry: entries[0],
        label: tz?.name || toolTargetZone,
      };
    } else {
      if (!toolFromZone) return;
      const fz = (zones as any)?.[toolFromZone];
      sp = { id: makeId(type), type, x, y, fromZone: toolFromZone, label: `From ${fz?.name || toolFromZone}` };
    }

    setSpawnPoints(prev => [...prev, sp]);
    setSelectedId(sp.id);
    setIsDirty(true);
  }, [meta, activeTool, pan, screenToCm, findSpawnAt, toolTemplateId, toolTargetZone, toolFromZone,
      npcList, zones, selectedZoneId, entryIdsByZone, spawnPoints]);

  const handleMouseMove = useCallback((e: React.MouseEvent) => {
    const { cx, cy } = canvasPos(e);
    if (meta) {
      const cm = screenToCm(cx, cy);
      setCursorCm({ x: Math.round(cm.x), y: Math.round(cm.y) });
    }
    if (isPanning) {
      setPan({
        x: panStart.current.px + (e.clientX - panStart.current.mx),
        y: panStart.current.py + (e.clientY - panStart.current.my),
      });
    } else if (isDragging && selectedId) {
      const cm = screenToCm(cx, cy);
      setSpawnPoints(prev => prev.map(s =>
        s.id === selectedId ? { ...s, x: Math.round(cm.x), y: Math.round(cm.y) } : s));
      setIsDirty(true);
    }
  }, [isPanning, isDragging, selectedId, screenToCm, meta]);

  const handleMouseUp = useCallback(() => { setIsPanning(false); setIsDragging(false); }, []);

  const handleWheel = useCallback((e: React.WheelEvent) => {
    e.preventDefault();
    const { cx, cy } = canvasPos(e);
    const factor = e.deltaY < 0 ? 1.12 : 1 / 1.12;
    setZoom(prev => {
      const next = Math.max(0.02, Math.min(8, prev * factor));
      setPan(p => ({
        x: cx - (cx - p.x) * (next / prev),
        y: cy - (cy - p.y) * (next / prev),
      }));
      return next;
    });
  }, []);

  // ── Mutations ─────────────────────────────────────────────────────────────

  const updateSelected = (updates: Partial<Spawn2>) => {
    if (!selectedId) return;
    setSpawnPoints(prev => prev.map(s => s.id === selectedId ? { ...s, ...updates } : s));
    if (updates.id && updates.id !== selectedId) setSelectedId(updates.id);
    setIsDirty(true);
  };

  const deleteSelected = () => {
    if (!selectedId) return;
    setSpawnPoints(prev => prev.filter(s => s.id !== selectedId));
    setSelectedId(null);
    setIsDirty(true);
  };

  const handleSave = async () => {
    if (!selectedZoneId) return;
    setSaveStatus('saving'); setSaveErrors([]);
    try {
      const res = await fetch(`/api/overlays2/${selectedZoneId}`, {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ version: '2.0', units: 'cm', zoneId: selectedZoneId, spawnPoints }),
      });
      const body = await res.json().catch(() => ({}));
      if (!res.ok) {
        setSaveStatus('error');
        setSaveErrors(body.errors || [body.error || `HTTP ${res.status}`]);
        return;
      }
      setSaveStatus('saved');
      setIsDirty(false);
      setTimeout(() => setSaveStatus('idle'), 2000);
    } catch (err: any) {
      setSaveStatus('error');
      setSaveErrors([err.message || 'Request failed']);
    }
  };

  const handleReloadInGame = async () => {
    setReloadStatus('reloading…');
    try {
      const res = await fetch('/api/admin/reload-overlays', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ zoneId: selectedZoneId }),
      });
      const body = await res.json().catch(() => ({}));
      setReloadStatus(res.ok
        ? `✓ reloaded in game${body.counts?.zones ? ` (${body.counts.zones} zones)` : ''}`
        : `✗ ${body.error || `HTTP ${res.status}`}`);
    } catch (err: any) {
      setReloadStatus(`✗ ${err.message || 'request failed'}`);
    }
    setTimeout(() => setReloadStatus(null), 6000);
  };

  // ── Render ────────────────────────────────────────────────────────────────

  const placementBlocked =
    (activeTool === 'npc' || activeTool === 'enemy') ? !toolTemplateId
    : activeTool === 'portal' ? !toolTargetZone
    : activeTool === 'entry' ? !toolFromZone
    : false;

  const cursorStyle = isPanning || isDragging ? 'grabbing'
    : activeTool === 'select' ? 'default'
    : placementBlocked ? 'not-allowed' : 'crosshair';

  const targetEntryOptions = selected?.targetZone ? (entryIdsByZone[selected.targetZone] || []) : [];

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', width: '100%', overflow: 'hidden' }}>

      {/* ── Top bar ── */}
      <div style={{
        display: 'flex', alignItems: 'center', gap: 10,
        padding: '6px 12px', background: 'var(--bg-secondary)',
        borderBottom: '1px solid var(--border-color)', flexShrink: 0,
      }}>
        <span style={{ fontWeight: 600, color: 'var(--text-secondary)', fontSize: 12 }}>ZONE:</span>
        <select className="form-select" value={selectedZoneId || ''}
          onChange={(e) => setSelectedZoneId(e.target.value || null)} style={{ width: 160 }}>
          <option value="">— select —</option>
          {zoneList.map((z: any) => <option key={z.id} value={z.id}>{z.name || z.id}</option>)}
        </select>

        {modeToggle}

        <div style={{ width: 1, height: 22, background: 'var(--border-color)', margin: '0 4px' }} />

        <span style={{ fontWeight: 600, color: 'var(--text-secondary)', fontSize: 12 }}>ZOOM:</span>
        <input type="range" min="0.02" max="4" step="0.01" value={zoom}
          onChange={(e) => setZoom(parseFloat(e.target.value))} style={{ width: 90 }} />
        <span style={{ fontSize: 11, color: 'var(--text-muted)', width: 44 }}>{(zoom * 100).toFixed(0)}%</span>
        <button className="btn btn-ghost" style={{ padding: '2px 8px', fontSize: 12 }}
          title="Fit the zone capture"
          onClick={() => {
            if (!meta || !containerRef.current) return;
            const cw = containerRef.current.clientWidth, ch = containerRef.current.clientHeight;
            const imgW = meta.sizeX * meta.pixelsPerCm, imgH = meta.sizeY * meta.pixelsPerCm;
            const z = Math.min(cw / imgW, ch / imgH) * 0.96;
            setZoom(z);
            setPan({ x: (cw - imgW * z) / 2, y: (ch - imgH * z) / 2 });
          }}>
          ⊡ Fit
        </button>

        <div style={{ flex: 1 }} />

        {reloadStatus && <span style={{ fontSize: 11, color: 'var(--text-muted)' }}>{reloadStatus}</span>}
        <button className="btn btn-ghost" onClick={handleReloadInGame} disabled={!selectedZoneId}
          title={`POST ${adminUrl || 'the admin API'}/api/admin/reload-overlays`}
          style={{ padding: '4px 12px' }}>
          ⟳ Reload in game
        </button>
        {isDirty && <span style={{ color: 'var(--warning)', fontSize: 11 }}>• Unsaved</span>}
        <button
          className={`btn ${saveStatus === 'saving' ? 'btn-ghost' : saveStatus === 'saved' ? 'btn-success' : 'btn-primary'}`}
          onClick={handleSave} disabled={!selectedZoneId || saveStatus === 'saving' || !meta}
          style={{ padding: '4px 14px' }}>
          {saveStatus === 'saving' ? '…' : saveStatus === 'saved' ? '✓ Saved' : '💾 Save Overlay'}
        </button>
      </div>

      {/* ── Save errors from the server-side schema check ── */}
      {saveErrors.length > 0 && (
        <div style={{
          flexShrink: 0, padding: '6px 12px', fontSize: 11, lineHeight: 1.5,
          background: 'rgba(255,68,68,0.12)', borderBottom: '1px solid var(--danger)', color: 'var(--danger)',
        }}>
          <strong>Save rejected:</strong>
          {saveErrors.map((e, i) => <div key={i}>• {e}</div>)}
        </div>
      )}

      {/* ── Body ── */}
      <div style={{ display: 'flex', flex: 1, overflow: 'hidden' }}>

        {/* LEFT: tools + object list */}
        <div style={{
          width: 220, flexShrink: 0, background: 'var(--bg-secondary)',
          borderRight: '1px solid var(--border-color)',
          display: 'flex', flexDirection: 'column', overflow: 'hidden',
        }}>
          <div style={{ padding: '10px 10px 6px', borderBottom: '1px solid var(--border-color)' }}>
            <div style={{ fontSize: 10, textTransform: 'uppercase', letterSpacing: 1, color: 'var(--text-muted)', marginBottom: 6 }}>
              Place Tool
            </div>
            <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
              {(['select', 'player', 'portal', 'entry'] as Tool[]).map(tool => (
                <button key={tool} onClick={() => setActiveTool(tool)} style={{
                  display: 'flex', alignItems: 'center', gap: 8, padding: '6px 10px',
                  background: activeTool === tool ? 'var(--accent-dim)' : 'transparent',
                  border: activeTool === tool ? '1px solid var(--accent)' : '1px solid transparent',
                  borderRadius: 4,
                  color: activeTool === tool ? 'var(--text-primary)' : 'var(--text-secondary)',
                  cursor: 'pointer', fontSize: 12, textAlign: 'left',
                }}>
                  <span style={{ fontSize: 14 }}>{TOOL_ICONS[tool]}</span>{TOOL_LABELS[tool]}
                </button>
              ))}
            </div>
          </div>

          <div style={{ padding: '8px 10px', borderBottom: '1px solid var(--border-color)', fontSize: 11, color: 'var(--text-muted)' }}>
            NPCs are placed in Unreal as <b>NPC Spawn Points</b> (in <code>L_&lt;Zone&gt;_Gameplay</code>),
            one NPC each, with their own respawn timer. The Live Dashboard shows them.
          </div>

          {(activeTool === 'npc' || activeTool === 'enemy') && (
            <div style={{ padding: '8px 10px', borderBottom: '1px solid var(--border-color)' }}>
              <div style={{ fontSize: 10, textTransform: 'uppercase', letterSpacing: 1, color: 'var(--text-muted)', marginBottom: 4 }}>
                {activeTool === 'npc' ? 'NPC Template' : 'Enemy Template'}
              </div>
              <select className="form-select" value={toolTemplateId}
                onChange={e => setToolTemplateId(e.target.value)} style={{ width: '100%' }}>
                <option value="">— pick template —</option>
                {(activeTool === 'npc' && npcOnlyList.length > 0 ? npcOnlyList : npcList).map((n: any) => (
                  <option key={n.id} value={n.id}>{n.name || n.id}</option>
                ))}
              </select>
              <div style={{ fontSize: 10, color: toolTemplateId ? 'var(--text-muted)' : 'var(--warning, #ffaa44)', marginTop: 4 }}>
                {toolTemplateId ? 'Click the capture to place' : '⚠ Pick a template before placing'}
              </div>
            </div>
          )}

          {activeTool === 'portal' && (
            <div style={{ padding: '8px 10px', borderBottom: '1px solid var(--border-color)' }}>
              <div style={{ fontSize: 10, textTransform: 'uppercase', letterSpacing: 1, color: 'var(--text-muted)', marginBottom: 4 }}>
                Destination Zone
              </div>
              <select className="form-select" value={toolTargetZone}
                onChange={e => setToolTargetZone(e.target.value)} style={{ width: '100%' }}>
                <option value="">— pick zone —</option>
                {zoneList.filter((z: any) => z.id !== selectedZoneId).map((z: any) => (
                  <option key={z.id} value={z.id}>{z.name || z.id}</option>
                ))}
              </select>
              <div style={{ fontSize: 10, color: toolTargetZone ? 'var(--text-muted)' : 'var(--warning, #ffaa44)', marginTop: 4 }}>
                {toolTargetZone ? 'Click the capture to place' : '⚠ Pick a destination zone first'}
              </div>
            </div>
          )}

          {activeTool === 'entry' && (
            <div style={{ padding: '8px 10px', borderBottom: '1px solid var(--border-color)' }}>
              <div style={{ fontSize: 10, textTransform: 'uppercase', letterSpacing: 1, color: 'var(--text-muted)', marginBottom: 4 }}>
                Arriving From Zone
              </div>
              <select className="form-select" value={toolFromZone}
                onChange={e => setToolFromZone(e.target.value)} style={{ width: '100%' }}>
                <option value="">— pick source zone —</option>
                {zoneList.filter((z: any) => z.id !== selectedZoneId).map((z: any) => (
                  <option key={z.id} value={z.id}>{z.name || z.id}</option>
                ))}
              </select>
              <div style={{ fontSize: 10, color: toolFromZone ? 'var(--text-muted)' : 'var(--warning, #ffaa44)', marginTop: 4 }}>
                {toolFromZone ? 'Click the capture to place' : '⚠ Pick a source zone first'}
              </div>
            </div>
          )}

          <div style={{ fontSize: 10, textTransform: 'uppercase', letterSpacing: 1, color: 'var(--text-muted)', padding: '8px 10px 4px' }}>
            Spawn Points ({spawnPoints.length})
          </div>
          <div style={{ flex: 1, overflowY: 'auto', padding: '0 6px 6px' }}>
            {spawnPoints.length === 0 ? (
              <div style={{ fontSize: 11, color: 'var(--text-muted)', padding: '8px 4px' }}>Nothing placed yet.</div>
            ) : spawnPoints.map(sp => (
              <div key={sp.id}
                onClick={() => {
                  const already = sp.id === selectedId;
                  setSelectedId(already ? null : sp.id);
                  if (!already && containerRef.current) {
                    const cw = containerRef.current.clientWidth, ch = containerRef.current.clientHeight;
                    setPan({ x: cw / 2 - sp.x * ppcm * zoom, y: ch / 2 - sp.y * ppcm * zoom });
                  }
                }}
                style={{
                  display: 'flex', alignItems: 'center', gap: 6, padding: '5px 8px', marginBottom: 2,
                  borderRadius: 4, cursor: 'pointer',
                  background: selectedId === sp.id ? 'var(--accent-dim)' : 'transparent',
                  border: selectedId === sp.id ? '1px solid var(--accent)' : '1px solid transparent',
                }}>
                <span style={{ width: 8, height: 8, borderRadius: '50%', flexShrink: 0, background: SPAWN_COLORS[sp.type] }} />
                <span style={{ fontSize: 11, color: 'var(--text-primary)', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                  {sp.label || sp.id}
                </span>
                <span style={{ fontSize: 10, color: 'var(--text-muted)', flexShrink: 0, marginLeft: 'auto' }}>
                  {Math.round(sp.x)},{Math.round(sp.y)}
                </span>
              </div>
            ))}
          </div>

          <div style={{ padding: '8px 10px', borderTop: '1px solid var(--border-color)', fontSize: 10, color: 'var(--text-muted)' }}>
            <div style={{ marginBottom: 3, fontWeight: 600, textTransform: 'uppercase', letterSpacing: 1 }}>Legend</div>
            {Object.entries(SPAWN_COLORS).map(([type, color]) => (
              <div key={type} style={{ display: 'flex', alignItems: 'center', gap: 5, marginBottom: 2 }}>
                <span style={{ width: 8, height: 8, borderRadius: '50%', background: color, display: 'inline-block', flexShrink: 0 }} />
                <span>{type.replace(/_/g, ' ')}</span>
              </div>
            ))}
            <div style={{ marginTop: 5 }}>
              <div><kbd style={{ fontSize: 9, background: '#333', padding: '1px 4px', borderRadius: 2 }}>Scroll</kbd> zoom</div>
              <div><kbd style={{ fontSize: 9, background: '#333', padding: '1px 4px', borderRadius: 2 }}>RMB/MMB</kbd> pan</div>
            </div>
          </div>
        </div>

        {/* CENTER: capture canvas */}
        <div ref={containerRef}
          style={{ flex: 1, overflow: 'hidden', position: 'relative', background: '#111118', cursor: cursorStyle }}
          onMouseDown={handleMouseDown}
          onMouseMove={handleMouseMove}
          onMouseUp={handleMouseUp}
          onMouseLeave={handleMouseUp}
          onWheel={handleWheel}
          onContextMenu={(e) => e.preventDefault()}
        >
          {!selectedZoneId ? (
            <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', height: '100%', color: 'var(--text-muted)' }}>
              <div style={{ fontSize: 48, marginBottom: 12 }}>🗺</div>
              <div>Select a zone to load its Unreal capture</div>
            </div>
          ) : loadError ? (
            <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', height: '100%', color: 'var(--danger)', textAlign: 'center', padding: 32 }}>
              <div style={{ fontSize: 32, marginBottom: 12 }}>⚠</div>
              <div style={{ maxWidth: 460, lineHeight: 1.5 }}>{loadError}</div>
            </div>
          ) : !meta ? (
            <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', height: '100%', color: 'var(--text-muted)' }}>
              Loading capture…
            </div>
          ) : (
            <canvas ref={canvasRef} style={{ display: 'block', position: 'absolute', top: 0, left: 0 }} />
          )}

          {meta && activeTool !== 'select' && (
            <div style={{
              position: 'absolute', bottom: 12, left: '50%', transform: 'translateX(-50%)',
              background: 'rgba(0,0,0,0.7)', border: '1px solid var(--border-color)',
              borderRadius: 6, padding: '4px 12px', fontSize: 11, color: 'var(--text-secondary)',
              pointerEvents: 'none',
            }}>
              {TOOL_ICONS[activeTool]} {TOOL_LABELS[activeTool]} — click the capture to place (cm)
            </div>
          )}
        </div>

        {/* RIGHT: properties */}
        <div style={{
          width: 240, flexShrink: 0, background: 'var(--bg-secondary)',
          borderLeft: '1px solid var(--border-color)',
          display: 'flex', flexDirection: 'column', overflow: 'hidden',
        }}>
          <div style={{ padding: '10px 14px', borderBottom: '1px solid var(--border-color)', fontWeight: 600, fontSize: 13 }}>
            Properties
          </div>
          <div style={{ flex: 1, overflowY: 'auto', padding: '12px 14px' }}>
            {!selected ? (
              <div style={{ fontSize: 12, color: 'var(--text-muted)', textAlign: 'center', paddingTop: 40 }}>
                <div style={{ fontSize: 28, marginBottom: 8 }}>👆</div>
                Select a spawn point on the capture or in the list
              </div>
            ) : (
              <>
                <div style={{ display: 'flex', alignItems: 'center', gap: 8, marginBottom: 14 }}>
                  <span style={{ display: 'inline-block', width: 10, height: 10, borderRadius: '50%', background: SPAWN_COLORS[selected.type], flexShrink: 0 }} />
                  <span style={{ fontSize: 11, fontWeight: 600, textTransform: 'uppercase', letterSpacing: 0.5, color: 'var(--text-secondary)' }}>
                    {selected.type.replace(/_/g, ' ')}
                  </span>
                </div>

                <div className="form-group">
                  <label className="form-label">ID</label>
                  <input className="form-input" type="text" value={selected.id}
                    onChange={(e) => updateSelected({ id: e.target.value })} />
                  <div style={{ fontSize: 10, color: 'var(--text-muted)', marginTop: 3 }}>
                    Portals in other zones point at entry ids.
                  </div>
                </div>

                <div className="form-group">
                  <label className="form-label">Label</label>
                  <input className="form-input" type="text" value={selected.label || ''}
                    onChange={(e) => updateSelected({ label: e.target.value || undefined })} />
                </div>

                <div className="form-group">
                  <label className="form-label">Type</label>
                  <select className="form-select" value={selected.type}
                    onChange={(e) => updateSelected({ type: e.target.value as Spawn2Type })}>
                    <option value="npc_spawn">NPC Spawn</option>
                    <option value="enemy_spawn">Enemy Spawn</option>
                    <option value="player_spawn">Player Spawn</option>
                    <option value="portal">Portal (Exit)</option>
                    <option value="zone_entry">Zone Entry</option>
                  </select>
                </div>

                {(selected.type === 'npc_spawn' || selected.type === 'enemy_spawn') && (
                  <>
                    <div className="form-group">
                      <label className="form-label">Template</label>
                      <select className="form-select" value={selected.templateId || ''}
                        onChange={(e) => {
                          const tpl = npcList.find((n: any) => n.id === e.target.value) as any;
                          updateSelected({ templateId: e.target.value || undefined, label: tpl?.name || selected.label });
                        }}>
                        <option value="">— none —</option>
                        {npcList.map((n: any) => <option key={n.id} value={n.id}>{n.name || n.id}</option>)}
                      </select>
                    </div>
                    {selected.type === 'enemy_spawn' && (
                      <div style={{ display: 'flex', gap: 8 }}>
                        <div className="form-group" style={{ flex: 1 }}>
                          <label className="form-label">Count</label>
                          <input className="form-input" type="number" min={1} value={selected.count ?? DEFAULT_ENEMY_COUNT}
                            onChange={(e) => updateSelected({ count: Math.max(1, parseInt(e.target.value, 10) || 1) })} />
                        </div>
                        <div className="form-group" style={{ flex: 1 }}>
                          <label className="form-label">Radius (cm)</label>
                          <input className="form-input" type="number" min={0} step={10} value={selected.radius ?? DEFAULT_ENEMY_RADIUS}
                            onChange={(e) => updateSelected({ radius: Math.max(0, parseInt(e.target.value, 10) || 0) })} />
                        </div>
                      </div>
                    )}
                  </>
                )}

                {selected.type === 'portal' && (
                  <>
                    <div className="form-group">
                      <label className="form-label">Target Zone</label>
                      <select className="form-select" value={selected.targetZone || ''}
                        onChange={(e) => {
                          const z = (zones as any)?.[e.target.value];
                          updateSelected({
                            targetZone: e.target.value || undefined,
                            targetEntry: (entryIdsByZone[e.target.value] || [])[0],
                            label: z?.name || selected.label,
                          });
                        }}>
                        <option value="">— none —</option>
                        {zoneList.filter((z: any) => z.id !== selectedZoneId).map((z: any) => (
                          <option key={z.id} value={z.id}>{z.name || z.id}</option>
                        ))}
                      </select>
                    </div>
                    <div className="form-group">
                      <label className="form-label">Target Entry</label>
                      <input className="form-input" type="text" list="overlay2-entry-ids"
                        value={selected.targetEntry || ''}
                        placeholder={targetEntryOptions[0] || 'entry id in the target zone'}
                        onChange={(e) => updateSelected({ targetEntry: e.target.value || undefined })} />
                      <datalist id="overlay2-entry-ids">
                        {targetEntryOptions.map(id => <option key={id} value={id} />)}
                      </datalist>
                      <div style={{ fontSize: 10, color: 'var(--text-muted)', marginTop: 3 }}>
                        {selected.targetZone
                          ? `${targetEntryOptions.length} zone_entry point(s) in ${selected.targetZone}`
                          : 'Pick a target zone to list its entries'}
                      </div>
                    </div>
                  </>
                )}

                {selected.type === 'zone_entry' && (
                  <div className="form-group">
                    <label className="form-label">From Zone</label>
                    <select className="form-select" value={selected.fromZone || ''}
                      onChange={(e) => {
                        const z = (zones as any)?.[e.target.value];
                        updateSelected({
                          fromZone: e.target.value || undefined,
                          label: `From ${z?.name || e.target.value}`,
                        });
                      }}>
                      <option value="">— none —</option>
                      {zoneList.filter((z: any) => z.id !== selectedZoneId).map((z: any) => (
                        <option key={z.id} value={z.id}>{z.name || z.id}</option>
                      ))}
                    </select>
                    <div style={{ fontSize: 10, color: 'var(--text-muted)', marginTop: 4, lineHeight: 1.4 }}>
                      Players arriving from this zone appear here.
                    </div>
                  </div>
                )}

                <div style={{ display: 'flex', gap: 8 }}>
                  <div className="form-group" style={{ flex: 1 }}>
                    <label className="form-label">X (cm)</label>
                    <input className="form-input" type="number" value={selected.x}
                      onChange={(e) => updateSelected({ x: parseFloat(e.target.value) || 0 })} />
                  </div>
                  <div className="form-group" style={{ flex: 1 }}>
                    <label className="form-label">Y (cm)</label>
                    <input className="form-input" type="number" value={selected.y}
                      onChange={(e) => updateSelected({ y: parseFloat(e.target.value) || 0 })} />
                  </div>
                </div>

                {(selected.type === 'portal' || selected.type === 'zone_entry') && (
                  <div style={{ fontSize: 10, color: 'var(--text-muted)', lineHeight: 1.4, marginBottom: 8 }}>
                    Unreal validates this against the actor placed in the level — move it here and
                    the level author has to move the actor to within {ACTOR_MATCH_CM} cm.
                  </div>
                )}

                <button className="btn btn-danger" onClick={deleteSelected} style={{ width: '100%', marginTop: 8 }}>
                  🗑 Delete Spawn Point
                </button>
              </>
            )}
          </div>
        </div>
      </div>

      {/* ── Status bar ── */}
      <div style={{
        flexShrink: 0, minHeight: 24, display: 'flex', alignItems: 'center', gap: 16,
        padding: '0 14px', background: 'var(--bg-tertiary, #0d0d14)',
        borderTop: '1px solid var(--border-color)', fontSize: 11,
        color: 'var(--text-muted)', fontVariantNumeric: 'tabular-nums',
      }}>
        <span style={{ color: 'var(--accent, #8888ff)', fontWeight: 600 }}>OVERLAY 2.0</span>
        {meta ? (
          <>
            <span style={{ width: 1, height: 12, background: 'var(--border-color)' }} />
            <span title="Zone size in centimetres">
              <span style={{ color: 'var(--text-secondary)', fontWeight: 600 }}>Zone </span>
              {meta.sizeX.toLocaleString()} × {meta.sizeY.toLocaleString()} cm
            </span>
            <span style={{ width: 1, height: 12, background: 'var(--border-color)' }} />
            <span title="Capture scale">{meta.pixelsPerCm} px/cm · origin {meta.originX}, {meta.originY}</span>
            <span style={{ width: 1, height: 12, background: 'var(--border-color)' }} />
            <span>{cursorCm ? `${cursorCm.x}, ${cursorCm.y} cm` : '—'}</span>
            <span style={{ width: 1, height: 12, background: 'var(--border-color)' }} />
            <span>{spawnPoints.length} spawn point{spawnPoints.length !== 1 ? 's' : ''}</span>
          </>
        ) : (
          <span>No capture loaded</span>
        )}
        <span style={{ flex: 1 }} />
        <span style={{ color: 'var(--warning, #ffaa44)' }}>
          validated in UE: portals and entries must match placed actors ({ACTOR_MATCH_CM} cm)
        </span>
      </div>
    </div>
  );
};
