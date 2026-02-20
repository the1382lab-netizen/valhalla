import React, { useState, useEffect, useRef, useCallback, useMemo } from 'react';
import { useEditorStore } from '../../../store/editorStore';

// ─── Types ───────────────────────────────────────────────────────────────────

type SpawnType = 'npc_spawn' | 'enemy_spawn' | 'player_spawn' | 'portal' | 'zone_entry';

interface SpawnPoint {
  id: string;
  type: SpawnType;
  x: number;       // world pixels
  y: number;
  label: string;   // display name (NPC name, "Player Start", etc.)
  templateId: string; // id of NPC/enemy template, or target zone for portals
  width?: number;
  height?: number;
  fromZone?: string; // for zone_entry: the source zone that uses this as its arrival point
}

interface MapData {
  width: number;
  height: number;
  tilewidth: number;
  tileheight: number;
  orientation?: string;
  layers: Array<{
    name: string;
    data?: number[];
    width?: number;
    height?: number;
    type: string;
  }>;
}

type PlaceTool = 'select' | 'npc' | 'enemy' | 'player' | 'portal' | 'entry';

type ResizeHandle = 'nw' | 'n' | 'ne' | 'e' | 'se' | 's' | 'sw' | 'w';

interface ResizeOp {
  spawnId: string;
  handle: ResizeHandle;
  startMx: number;
  startMy: number;
  startX: number;
  startY: number;
  startW: number;
  startH: number;
}

const HANDLE_RADIUS = 6; // canvas px, hit-test radius for resize handles

// ─── Helpers ─────────────────────────────────────────────────────────────────

const SPAWN_COLORS: Record<SpawnType, string> = {
  npc_spawn:    '#ffdd44',
  enemy_spawn:  '#ff4444',
  player_spawn: '#44ff88',
  portal:       '#dd44ff',
  zone_entry:   '#44ddff',
};

const TOOL_ICONS: Record<PlaceTool, string> = {
  select:  '🖱',
  npc:     '👤',
  enemy:   '💀',
  player:  '🎮',
  portal:  '🌀',
  entry:   '🚪',
};

const TOOL_LABELS: Record<PlaceTool, string> = {
  select:  'Select / Move',
  npc:     'Place NPC',
  enemy:   'Place Enemy',
  player:  'Player Spawn',
  portal:  'Portal (Exit)',
  entry:   'Zone Entry',
};

// ─── Component ───────────────────────────────────────────────────────────────

export const MapEditor: React.FC = () => {
  const zones = useEditorStore(s => s.zones.data);
  const npcTemplates = useEditorStore(s => s.npcTemplates.data);
  const updateData = useEditorStore(s => s.updateData);
  const saveSection = useEditorStore(s => s.saveSection);

  // Zone / map state
  const [selectedZoneId, setSelectedZoneId] = useState<string | null>(null);
  const [mapData, setMapData] = useState<MapData | null>(null);
  const [loadError, setLoadError] = useState<string | null>(null);
  const [isDirty, setIsDirty] = useState(false);
  const [saveStatus, setSaveStatus] = useState<'idle' | 'saving' | 'saved' | 'error'>('idle');

  // Available map files from /maps directory
  const [availableMapFiles, setAvailableMapFiles] = useState<string[]>([]);
  const [mapFileSaveStatus, setMapFileSaveStatus] = useState<'idle' | 'saving' | 'saved' | 'error'>('idle');

  // Spawn points
  const [spawnPoints, setSpawnPoints] = useState<SpawnPoint[]>([]);
  const [selectedId, setSelectedId] = useState<string | null>(null);

  // Tool state
  const [activeTool, setActiveTool] = useState<PlaceTool>('select');
  const [selectedNpcId, setSelectedNpcId] = useState<string>('');
  const [selectedEnemyId, setSelectedEnemyId] = useState<string>('');
  const [portalTargetZone, setPortalTargetZone] = useState<string>('');
  const [entryFromZone, setEntryFromZone] = useState<string>('');

  // Canvas / pan / zoom
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const containerRef = useRef<HTMLDivElement>(null);
  const [zoom, setZoom] = useState(1);
  const [pan, setPan] = useState({ x: 0, y: 0 });
  const panRef = useRef({ x: 0, y: 0 });
  const [isPanning, setIsPanning] = useState(false);
  const [isDraggingSpawn, setIsDraggingSpawn] = useState(false);
  const panStart = useRef({ mx: 0, my: 0, px: 0, py: 0 });
  const rafIdRef = useRef<number>(0);
  const [isResizing, setIsResizing] = useState(false);
  const resizeOpRef = useRef<ResizeOp | null>(null);

  // Derived lists
  const zoneList = useMemo(() => Object.values(zones || {}) as any[], [zones]);
  const npcList = useMemo(() => Object.values(npcTemplates || {}) as any[], [npcTemplates]);
  // Enemies = npc templates with type 'enemy'
  const enemyList = useMemo(
    () => npcList.filter((n: any) => n.type === 'enemy' || !n.type),
    [npcList]
  );
  const npcOnlyList = useMemo(
    () => npcList.filter((n: any) => n.type !== 'enemy'),
    [npcList]
  );

  const selectedSpawn = spawnPoints.find(s => s.id === selectedId) || null;

  // ── Fetch available map files ────────────────────────────────────────────────

  useEffect(() => {
    (async () => {
      try {
        const res = await fetch('/api/maps');
        if (res.ok) {
          const data = await res.json();
          // Server returns { maps: string[] }
          const files: string[] = Array.isArray(data) ? data : (data.maps || []);
          setAvailableMapFiles(files.filter((f: string) => f.endsWith('.json')).sort());
        }
      } catch {
        // silently ignore — dropdown will just be empty
      }
    })();
  }, []);

  // ── Handle map file assignment for the selected zone ─────────────────────────

  const handleMapFileChange = async (newMapFile: string) => {
    if (!selectedZoneId || !newMapFile) return;
    const zone = (zones as any)[selectedZoneId];
    if (!zone) return;

    // Update the zone's mapFile in the store and persist it
    const updatedZones = { ...zones, [selectedZoneId]: { ...zone, mapFile: newMapFile } };
    updateData('zones', updatedZones);

    setMapFileSaveStatus('saving');
    try {
      await saveSection('zones');
      setMapFileSaveStatus('saved');
      setTimeout(() => setMapFileSaveStatus('idle'), 2000);
    } catch {
      setMapFileSaveStatus('error');
    }

    // Reload the map with the newly selected file
    setMapData(null);
    setSpawnPoints([]);
    setSelectedId(null);
    setLoadError(null);
    try {
      const mapRes = await fetch(`/api/maps/${newMapFile}`);
      if (!mapRes.ok) throw new Error(`Map file not found: ${newMapFile}`);
      setMapData(await mapRes.json());
      try {
        const ovRes = await fetch(`/api/overlays/${selectedZoneId}-overlay.json`);
        if (ovRes.ok) {
          const overlay = await ovRes.json();
          setSpawnPoints(overlay.spawnPoints || []);
        }
      } catch { /* no overlay yet */ }
    } catch (err: any) {
      setLoadError(err.message || 'Failed to load map');
    }
  };

  // ── Zone / map loading ──────────────────────────────────────────────────────

  useEffect(() => {
    if (zoneList.length > 0 && !selectedZoneId) {
      const firstId = Object.keys(zones || {})[0];
      setSelectedZoneId(firstId);
    }
  }, [zones]);

  // Reset portal target zone whenever the edited zone changes.
  // Without this, a stale portalTargetZone value from a previous zone could be
  // used when placing portals, creating self-referencing portals or portals
  // that target the wrong zone.
  useEffect(() => {
    setPortalTargetZone('');
    setEntryFromZone('');
  }, [selectedZoneId]);

  useEffect(() => {
    if (!selectedZoneId) return;
    setMapData(null);
    setSpawnPoints([]);
    setSelectedId(null);
    setLoadError(null);
    setIsDirty(false);

    const zone = (zones as any)?.[selectedZoneId];
    if (!zone?.mapFile) {
      setLoadError('Zone has no mapFile set. Edit the Zone in the Zones editor first.');
      return;
    }

    (async () => {
      try {
        const mapRes = await fetch(`/api/maps/${zone.mapFile}`);
        if (!mapRes.ok) throw new Error(`Map file not found: ${zone.mapFile}`);
        const map = await mapRes.json();
        setMapData(map);

        try {
          const ovRes = await fetch(`/api/overlays/${selectedZoneId}-overlay.json`);
          if (ovRes.ok) {
            const overlay = await ovRes.json();
            setSpawnPoints(overlay.spawnPoints || []);
          }
        } catch { /* no overlay yet, start fresh */ }
      } catch (err: any) {
        setLoadError(err.message || 'Failed to load map');
      }
    })();
  }, [selectedZoneId]);

  // ── Canvas rendering (imperative — no state-driven feedback loops) ────────

  const renderCanvas = useCallback(() => {
    const canvas = canvasRef.current;
    const container = containerRef.current;
    if (!canvas || !container || !mapData) return;

    // Read container size directly from the DOM — no React state involved
    const cw = container.clientWidth;
    const ch = container.clientHeight;
    if (cw === 0 || ch === 0) return;

    // Only resize the pixel buffer when the container actually changed size
    // (setting canvas.width/height clears the canvas, so avoid doing it needlessly)
    if (canvas.width !== cw || canvas.height !== ch) {
      canvas.width = cw;
      canvas.height = ch;
    }

    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const isIso = mapData.orientation === 'isometric';
    // For ISO, use a uniform ortho tile size for the grid (tileheight = 64)
    const orthoTs = isIso ? mapData.tileheight : mapData.tilewidth;
    const tw = orthoTs * zoom;
    const th = orthoTs * zoom;

    ctx.fillStyle = '#111118';
    ctx.fillRect(0, 0, cw, ch);

    // Tile grid
    const collisionLayer = mapData.layers.find(l => l.name.toLowerCase().includes('collision'));
    const collisionData = collisionLayer?.data || [];

    for (let ty = 0; ty < mapData.height; ty++) {
      for (let tx = 0; tx < mapData.width; tx++) {
        const idx = ty * mapData.width + tx;
        const isCollision = collisionData[idx] > 0;

        let tileColor: string;
        if (isCollision) {
          tileColor = '#2a2a35';
        } else if (selectedZoneId === 'desert') {
          tileColor = '#c09060';
        } else if (selectedZoneId?.includes('cave') || selectedZoneId?.includes('dungeon')) {
          tileColor = '#404050';
        } else {
          tileColor = '#507850';
        }

        if (isIso) {
          // Isometric diamond rendering
          // Ortho center → ISO screen center
          const orthoCx = tx * orthoTs + orthoTs / 2;
          const orthoCy = ty * orthoTs + orthoTs / 2;
          const isoX = (orthoCx - orthoCy) * zoom + pan.x;
          const isoY = ((orthoCx + orthoCy) / 2) * zoom + pan.y;
          const halfW = orthoTs * zoom; // diamond half-width
          const halfH = (orthoTs / 2) * zoom; // diamond half-height

          // Cull off-screen
          if (isoX + halfW < 0 || isoX - halfW > cw || isoY + halfH < 0 || isoY - halfH > ch) continue;

          ctx.fillStyle = tileColor;
          ctx.beginPath();
          ctx.moveTo(isoX, isoY - halfH);
          ctx.lineTo(isoX + halfW, isoY);
          ctx.lineTo(isoX, isoY + halfH);
          ctx.lineTo(isoX - halfW, isoY);
          ctx.closePath();
          ctx.fill();

          ctx.strokeStyle = 'rgba(255,255,255,0.05)';
          ctx.lineWidth = 0.5;
          ctx.stroke();
        } else {
          // Orthogonal square rendering
          const px = tx * tw + pan.x;
          const py = ty * th + pan.y;
          if (px + tw < 0 || py + th < 0 || px > cw || py > ch) continue;

          ctx.fillStyle = tileColor;
          ctx.fillRect(px, py, tw - 0.5, th - 0.5);

          ctx.strokeStyle = 'rgba(255,255,255,0.05)';
          ctx.lineWidth = 0.5;
          ctx.strokeRect(px, py, tw, th);
        }
      }
    }

    // Spawn points
    spawnPoints.forEach(sp => {
      // Spawn point coordinates are in ortho world space
      let px: number, py: number;
      if (isIso) {
        px = (sp.x - sp.y) * zoom + pan.x;
        py = ((sp.x + sp.y) / 2) * zoom + pan.y;
      } else {
        px = sp.x * zoom + pan.x;
        py = sp.y * zoom + pan.y;
      }
      const isSelected = selectedId === sp.id;
      const color = SPAWN_COLORS[sp.type];
      const size = isSelected ? 14 : 11;

      ctx.save();
      ctx.globalAlpha = isSelected ? 1 : 0.85;

      if (isSelected) {
        ctx.shadowColor = color;
        ctx.shadowBlur = 12;
      }

      ctx.fillStyle = color;
      ctx.strokeStyle = isSelected ? '#ffffff' : color;
      ctx.lineWidth = isSelected ? 2.5 : 1.5;

      if (sp.type === 'player_spawn') {
        ctx.beginPath();
        ctx.moveTo(px, py - size);
        ctx.lineTo(px + size, py + size);
        ctx.lineTo(px - size, py + size);
        ctx.closePath();
        ctx.fill();
        ctx.stroke();
      } else if (sp.type === 'portal') {
        const pw = (sp.width || 64) * zoom;
        const ph = (sp.height || 64) * zoom;
        ctx.globalAlpha = isSelected ? 0.5 : 0.35;
        ctx.fillRect(px, py, pw, ph);
        ctx.globalAlpha = 1;
        ctx.strokeRect(px, py, pw, ph);
      } else if (sp.type === 'zone_entry') {
        // Render as a dashed rectangle like portal but cyan, with a small arrow inside
        const pw = (sp.width || 64) * zoom;
        const ph = (sp.height || 64) * zoom;
        ctx.globalAlpha = isSelected ? 0.5 : 0.3;
        ctx.fillRect(px, py, pw, ph);
        ctx.globalAlpha = 1;
        ctx.setLineDash([4, 3]);
        ctx.strokeRect(px, py, pw, ph);
        ctx.setLineDash([]);
        // Entry arrow hint (pointing down-right inside the rect)
        ctx.strokeStyle = 'rgba(255,255,255,0.85)';
        ctx.lineWidth = 1.5;
        const mx2 = px + pw / 2, my2 = py + ph / 2;
        const as2 = Math.min(pw, ph) * 0.25;
        ctx.beginPath();
        ctx.moveTo(mx2 - as2, my2 - as2);
        ctx.lineTo(mx2 + as2, my2 + as2);
        ctx.moveTo(mx2 + as2, my2 + as2);
        ctx.lineTo(mx2 - as2 * 0.3, my2 + as2);
        ctx.moveTo(mx2 + as2, my2 + as2);
        ctx.lineTo(mx2 + as2, my2 - as2 * 0.3);
        ctx.stroke();
      } else {
        ctx.beginPath();
        ctx.arc(px, py, size, 0, Math.PI * 2);
        ctx.fill();
        ctx.stroke();
      }

      ctx.shadowBlur = 0;

      ctx.globalAlpha = 1;
      ctx.fillStyle = '#ffffff';
      ctx.font = `bold ${isSelected ? 12 : 10}px sans-serif`;
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      const labelText = sp.label ?? '';
      const label = labelText.length > 10 ? labelText.substring(0, 9) + '…' : labelText;
      ctx.fillText(label, px, py + size + 10);

      ctx.restore();
    });

    // ── Resize handles for selected portal / zone_entry ──────────────────────
    const selSp = spawnPoints.find(s => s.id === selectedId);
    if (selSp && (selSp.type === 'portal' || selSp.type === 'zone_entry')) {
      let bx: number, by: number;
      if (isIso) {
        bx = (selSp.x - selSp.y) * zoom + pan.x;
        by = ((selSp.x + selSp.y) / 2) * zoom + pan.y;
      } else {
        bx = selSp.x * zoom + pan.x;
        by = selSp.y * zoom + pan.y;
      }
      const bw = (selSp.width || 64) * zoom;
      const bh = (selSp.height || 64) * zoom;
      const handlePositions: [ResizeHandle, number, number][] = [
        ['nw', bx,          by         ],
        ['n',  bx + bw / 2, by         ],
        ['ne', bx + bw,     by         ],
        ['e',  bx + bw,     by + bh / 2],
        ['se', bx + bw,     by + bh    ],
        ['s',  bx + bw / 2, by + bh    ],
        ['sw', bx,          by + bh    ],
        ['w',  bx,          by + bh / 2],
      ];
      const HS = HANDLE_RADIUS;
      handlePositions.forEach(([, hx, hy]) => {
        ctx.fillStyle = '#ffffff';
        ctx.strokeStyle = '#333333';
        ctx.lineWidth = 1;
        ctx.fillRect(hx - HS, hy - HS, HS * 2, HS * 2);
        ctx.strokeRect(hx - HS, hy - HS, HS * 2, HS * 2);
      });
    }

    // Crosshair at origin
    ctx.strokeStyle = 'rgba(255,255,255,0.2)';
    ctx.lineWidth = 1;
    ctx.setLineDash([4, 4]);
    ctx.beginPath();
    ctx.moveTo(pan.x, 0); ctx.lineTo(pan.x, ch);
    ctx.moveTo(0, pan.y); ctx.lineTo(cw, pan.y);
    ctx.stroke();
    ctx.setLineDash([]);
  }, [mapData, spawnPoints, selectedId, zoom, pan, selectedZoneId]);

  // Schedule a render whenever dependencies change
  useEffect(() => {
    cancelAnimationFrame(rafIdRef.current);
    rafIdRef.current = requestAnimationFrame(renderCanvas);
    return () => cancelAnimationFrame(rafIdRef.current);
  }, [renderCanvas]);

  // Also re-render when the container resizes (no React state involved)
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

  // ── Canvas interactions ─────────────────────────────────────────────────────

  const worldPos = useCallback((e: React.MouseEvent) => {
    const rect = canvasRef.current!.getBoundingClientRect();
    const cx = e.clientX - rect.left;
    const cy = e.clientY - rect.top;

    // Convert canvas pixel to world pixel
    const isIso = mapData?.orientation === 'isometric';
    if (isIso) {
      // Canvas → ISO screen coords → ortho world coords
      const isoX = (cx - pan.x) / zoom;
      const isoY = (cy - pan.y) / zoom;
      // Inverse of orthoToIso: orthoX = isoX/2 + isoY, orthoY = -isoX/2 + isoY
      const orthoX = isoX / 2 + isoY;
      const orthoY = -isoX / 2 + isoY;
      return { wx: Math.round(orthoX), wy: Math.round(orthoY), cx, cy };
    }
    return {
      wx: Math.round((cx - pan.x) / zoom),
      wy: Math.round((cy - pan.y) / zoom),
      cx,
      cy,
    };
  }, [pan, zoom, mapData]);

  /** Returns the handle the mouse is over for the currently selected portal/zone_entry, or null. */
  const findHandleAt = useCallback((cx: number, cy: number): ResizeHandle | null => {
    const selSp = spawnPoints.find(s => s.id === selectedId);
    if (!selSp || (selSp.type !== 'portal' && selSp.type !== 'zone_entry')) return null;
    const isIso = mapData?.orientation === 'isometric';
    let bx: number, by: number;
    if (isIso) {
      bx = (selSp.x - selSp.y) * zoom + pan.x;
      by = ((selSp.x + selSp.y) / 2) * zoom + pan.y;
    } else {
      bx = selSp.x * zoom + pan.x;
      by = selSp.y * zoom + pan.y;
    }
    const bw = (selSp.width || 64) * zoom;
    const bh = (selSp.height || 64) * zoom;
    const candidates: [ResizeHandle, number, number][] = [
      ['nw', bx,          by         ],
      ['n',  bx + bw / 2, by         ],
      ['ne', bx + bw,     by         ],
      ['e',  bx + bw,     by + bh / 2],
      ['se', bx + bw,     by + bh    ],
      ['s',  bx + bw / 2, by + bh    ],
      ['sw', bx,          by + bh    ],
      ['w',  bx,          by + bh / 2],
    ];
    for (const [handle, hx, hy] of candidates) {
      if (Math.abs(cx - hx) <= HANDLE_RADIUS + 2 && Math.abs(cy - hy) <= HANDLE_RADIUS + 2) {
        return handle;
      }
    }
    return null;
  }, [spawnPoints, selectedId, zoom, pan, mapData]);

  const findSpawnAt = useCallback((cx: number, cy: number): string | null => {
    let best: string | null = null;
    let bestDist = Infinity;
    const isIso = mapData?.orientation === 'isometric';
    for (const sp of spawnPoints) {
      let px: number, py: number;
      if (isIso) {
        px = (sp.x - sp.y) * zoom + pan.x;
        py = ((sp.x + sp.y) / 2) * zoom + pan.y;
      } else {
        px = sp.x * zoom + pan.x;
        py = sp.y * zoom + pan.y;
      }
      // For rectangular objects, check if click is inside the rect
      if (sp.type === 'portal' || sp.type === 'zone_entry') {
        const pw = (sp.width || 64) * zoom;
        const ph = (sp.height || 64) * zoom;
        if (cx >= px && cx <= px + pw && cy >= py && cy <= py + ph) {
          // Use negative dist so nearest center wins on overlap
          const distToCenter = Math.hypot(cx - (px + pw / 2), cy - (py + ph / 2));
          if (distToCenter < bestDist) {
            bestDist = distToCenter;
            best = sp.id;
          }
        }
        continue;
      }
      const dist = Math.hypot(cx - px, cy - py);
      if (dist < 20 && dist < bestDist) {
        bestDist = dist;
        best = sp.id;
      }
    }
    return best;
  }, [spawnPoints, zoom, pan, mapData]);

  const handleMouseDown = useCallback((e: React.MouseEvent) => {
    if (!mapData) return;
    e.preventDefault();

    // Middle or right mouse = pan
    if (e.button === 1 || e.button === 2) {
      setIsPanning(true);
      panStart.current = { mx: e.clientX, my: e.clientY, px: pan.x, py: pan.y };
      return;
    }

    const { wx, wy, cx, cy } = worldPos(e);

    if (activeTool === 'select') {
      // Check for resize handle first (only when a rect spawn is selected)
      const handle = findHandleAt(cx, cy);
      if (handle) {
        const selSp = spawnPoints.find(s => s.id === selectedId)!;
        resizeOpRef.current = {
          spawnId: selSp.id,
          handle,
          startMx: e.clientX,
          startMy: e.clientY,
          startX: selSp.x,
          startY: selSp.y,
          startW: selSp.width || 64,
          startH: selSp.height || 64,
        };
        setIsResizing(true);
        return;
      }
      const hit = findSpawnAt(cx, cy);
      setSelectedId(hit);
      if (hit) setIsDraggingSpawn(true);
      return;
    }

    // Placement tools
    const id = `spawn_${Date.now()}`;
    let newSpawn: SpawnPoint;

    if (activeTool === 'npc') {
      const npc = npcList.find((n: any) => n.id === selectedNpcId) as any;
      newSpawn = {
        id, type: 'npc_spawn', x: wx, y: wy,
        label: npc?.name || selectedNpcId || 'NPC',
        templateId: selectedNpcId,
      };
    } else if (activeTool === 'enemy') {
      const enemy = npcList.find((n: any) => n.id === selectedEnemyId) as any;
      newSpawn = {
        id, type: 'enemy_spawn', x: wx, y: wy,
        label: enemy?.name || selectedEnemyId || 'Enemy',
        templateId: selectedEnemyId,
      };
    } else if (activeTool === 'player') {
      newSpawn = { id, type: 'player_spawn', x: wx, y: wy, label: 'Player', templateId: '' };
    } else if (activeTool === 'portal') {
      // portal — require a target zone to be selected first
      if (!portalTargetZone) {
        // Silently ignore the click; the UI hint in the panel already tells the user to pick a zone
        return;
      }
      const targetZoneName = (zones as any)[portalTargetZone]?.name || portalTargetZone;
      newSpawn = { id, type: 'portal', x: wx, y: wy, label: targetZoneName, templateId: portalTargetZone, width: 64, height: 64 };
    } else {
      // entry — place a zone entry point (arrival position for players coming from another zone)
      if (!entryFromZone) {
        // Silently ignore; UI hint tells user to pick a source zone
        return;
      }
      const sourceZoneName = (zones as any)[entryFromZone]?.name || entryFromZone;
      newSpawn = {
        id,
        type: 'zone_entry',
        x: wx,
        y: wy,
        label: `From ${sourceZoneName}`,
        templateId: entryFromZone,
        fromZone: entryFromZone,
        width: 128,
        height: 64,
      };
    }

    setSpawnPoints(prev => [...prev, newSpawn]);
    setSelectedId(id);
    setIsDirty(true);
  }, [mapData, activeTool, selectedNpcId, selectedEnemyId, portalTargetZone, entryFromZone, npcList, findSpawnAt, findHandleAt, spawnPoints, selectedId, pan, zoom, worldPos]);

  const handleMouseMove = useCallback((e: React.MouseEvent) => {
    if (isPanning) {
      const nx = panStart.current.px + (e.clientX - panStart.current.mx);
      const ny = panStart.current.py + (e.clientY - panStart.current.my);
      setPan({ x: nx, y: ny });
      panRef.current = { x: nx, y: ny };
    } else if (isResizing && resizeOpRef.current) {
      const op = resizeOpRef.current;
      // Delta in world pixels
      const dxPx = (e.clientX - op.startMx) / zoom;
      const dyPx = (e.clientY - op.startMy) / zoom;
      const MIN = 16;
      let nx = op.startX, ny = op.startY, nw = op.startW, nh = op.startH;
      switch (op.handle) {
        case 'se': nw = Math.max(MIN, op.startW + dxPx); nh = Math.max(MIN, op.startH + dyPx); break;
        case 's':  nh = Math.max(MIN, op.startH + dyPx); break;
        case 'e':  nw = Math.max(MIN, op.startW + dxPx); break;
        case 'sw': { const ww = Math.max(MIN, op.startW - dxPx); nx = op.startX + (op.startW - ww); nw = ww; nh = Math.max(MIN, op.startH + dyPx); break; }
        case 'w':  { const ww = Math.max(MIN, op.startW - dxPx); nx = op.startX + (op.startW - ww); nw = ww; break; }
        case 'ne': { nw = Math.max(MIN, op.startW + dxPx); const hh = Math.max(MIN, op.startH - dyPx); ny = op.startY + (op.startH - hh); nh = hh; break; }
        case 'n':  { const hh = Math.max(MIN, op.startH - dyPx); ny = op.startY + (op.startH - hh); nh = hh; break; }
        case 'nw': { const ww = Math.max(MIN, op.startW - dxPx); nx = op.startX + (op.startW - ww); nw = ww; const hh = Math.max(MIN, op.startH - dyPx); ny = op.startY + (op.startH - hh); nh = hh; break; }
      }
      setSpawnPoints(prev =>
        prev.map(s => s.id === op.spawnId
          ? { ...s, x: Math.round(nx), y: Math.round(ny), width: Math.round(nw), height: Math.round(nh) }
          : s)
      );
      setIsDirty(true);
    } else if (isDraggingSpawn && selectedId) {
      const { wx, wy } = worldPos(e);
      setSpawnPoints(prev =>
        prev.map(s => s.id === selectedId ? { ...s, x: wx, y: wy } : s)
      );
      setIsDirty(true);
    }
  }, [isPanning, isResizing, isDraggingSpawn, selectedId, zoom, worldPos]);

  const handleMouseUp = useCallback(() => {
    setIsPanning(false);
    setIsDraggingSpawn(false);
    if (isResizing) {
      setIsResizing(false);
      resizeOpRef.current = null;
    }
  }, [isResizing]);

  const handleWheel = useCallback((e: React.WheelEvent) => {
    e.preventDefault();
    const factor = e.deltaY < 0 ? 1.1 : 0.9;
    setZoom(z => Math.max(0.25, Math.min(6, z * factor)));
  }, []);

  // ── Save ────────────────────────────────────────────────────────────────────

  const handleSave = async () => {
    if (!selectedZoneId) return;
    setSaveStatus('saving');
    try {
      await fetch(`/api/overlays/${selectedZoneId}-overlay.json`, {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ version: '1.0.0', spawnPoints }),
      });
      setSaveStatus('saved');
      setIsDirty(false);
      setTimeout(() => setSaveStatus('idle'), 2000);
    } catch {
      setSaveStatus('error');
    }
  };

  const deleteSelected = () => {
    if (!selectedId) return;
    setSpawnPoints(prev => prev.filter(s => s.id !== selectedId));
    setSelectedId(null);
    setIsDirty(true);
  };

  const updateSpawn = (updates: Partial<SpawnPoint>) => {
    setSpawnPoints(prev =>
      prev.map(s => s.id === selectedId ? { ...s, ...updates } : s)
    );
    setIsDirty(true);
  };

  // ── Cursor style ────────────────────────────────────────────────────────────

  const portalReadyToPlace = activeTool === 'portal' && !!portalTargetZone;
  const entryReadyToPlace  = activeTool === 'entry'  && !!entryFromZone;
  const cursorStyle = isPanning || isDraggingSpawn || isResizing
    ? 'grabbing'
    : activeTool === 'select'
      ? 'default'
      : (activeTool === 'portal' && !portalReadyToPlace) || (activeTool === 'entry' && !entryReadyToPlace)
        ? 'not-allowed'
        : 'crosshair';

  // ── Render ──────────────────────────────────────────────────────────────────

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', width: '100%', overflow: 'hidden' }}>

      {/* ── Top bar ── */}
      <div style={{
        display: 'flex', alignItems: 'center', gap: 10,
        padding: '6px 12px',
        background: 'var(--bg-secondary)',
        borderBottom: '1px solid var(--border-color)',
        flexShrink: 0,
      }}>
        <span style={{ fontWeight: 600, color: 'var(--text-secondary)', fontSize: 12 }}>ZONE:</span>
        <select
          className="form-select"
          value={selectedZoneId || ''}
          onChange={(e) => { setSelectedZoneId(e.target.value || null); }}
          style={{ width: 160 }}
        >
          <option value="">— select —</option>
          {zoneList.map((z: any) => (
            <option key={z.id} value={z.id}>{z.name || z.id}</option>
          ))}
        </select>

        <div style={{ width: 1, height: 22, background: 'var(--border-color)', margin: '0 4px' }} />

        <span style={{ fontWeight: 600, color: 'var(--text-secondary)', fontSize: 12 }}>MAP FILE:</span>
        <select
          className="form-select"
          value={selectedZoneId ? ((zones as any)[selectedZoneId]?.mapFile || '') : ''}
          onChange={(e) => handleMapFileChange(e.target.value)}
          disabled={!selectedZoneId}
          style={{ width: 180 }}
          title="Choose which map JSON file this zone uses"
        >
          <option value="">— select map —</option>
          {availableMapFiles.map(f => (
            <option key={f} value={f}>{f}</option>
          ))}
        </select>
        {mapFileSaveStatus === 'saving' && (
          <span style={{ fontSize: 11, color: 'var(--text-muted)' }}>saving…</span>
        )}
        {mapFileSaveStatus === 'saved' && (
          <span style={{ fontSize: 11, color: 'var(--success, #44cc88)' }}>✓ saved</span>
        )}
        {mapFileSaveStatus === 'error' && (
          <span style={{ fontSize: 11, color: 'var(--danger)' }}>save failed</span>
        )}

        <div style={{ width: 1, height: 22, background: 'var(--border-color)', margin: '0 4px' }} />

        <span style={{ fontWeight: 600, color: 'var(--text-secondary)', fontSize: 12 }}>ZOOM:</span>
        <input type="range" min="0.25" max="6" step="0.05" value={zoom}
          onChange={(e) => setZoom(parseFloat(e.target.value))}
          style={{ width: 90 }} />
        <span style={{ fontSize: 11, color: 'var(--text-muted)', width: 36 }}>{(zoom * 100).toFixed(0)}%</span>

        <button className="btn btn-ghost" onClick={() => { setZoom(1); setPan({ x: 0, y: 0 }); }}
          title="Reset view" style={{ padding: '2px 8px', fontSize: 12 }}>
          ⊡ Reset
        </button>

        <div style={{ flex: 1 }} />

        {isDirty && <span style={{ color: 'var(--warning)', fontSize: 11 }}>• Unsaved</span>}
        <button
          className={`btn ${saveStatus === 'saving' ? 'btn-ghost' : saveStatus === 'saved' ? 'btn-success' : 'btn-primary'}`}
          onClick={handleSave}
          disabled={!selectedZoneId || saveStatus === 'saving'}
          style={{ padding: '4px 14px' }}
        >
          {saveStatus === 'saving' ? '…' : saveStatus === 'saved' ? '✓ Saved' : '💾 Save Map'}
        </button>
      </div>

      {/* ── Body row ── */}
      <div style={{ display: 'flex', flex: 1, overflow: 'hidden', gap: 0 }}>

        {/* ── LEFT PANEL: Tool palette + object list ── */}
        <div style={{
          width: 220, flexShrink: 0,
          background: 'var(--bg-secondary)',
          borderRight: '1px solid var(--border-color)',
          display: 'flex', flexDirection: 'column',
          overflow: 'hidden',
        }}>
          {/* Tool palette */}
          <div style={{ padding: '10px 10px 6px', borderBottom: '1px solid var(--border-color)' }}>
            <div style={{ fontSize: 10, textTransform: 'uppercase', letterSpacing: 1, color: 'var(--text-muted)', marginBottom: 6 }}>
              Place Tool
            </div>
            <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
              {(['select', 'npc', 'enemy', 'player', 'portal', 'entry'] as PlaceTool[]).map(tool => (
                <button
                  key={tool}
                  onClick={() => setActiveTool(tool)}
                  style={{
                    display: 'flex', alignItems: 'center', gap: 8,
                    padding: '6px 10px',
                    background: activeTool === tool ? 'var(--accent-dim)' : 'transparent',
                    border: activeTool === tool ? '1px solid var(--accent)' : '1px solid transparent',
                    borderRadius: 4,
                    color: activeTool === tool ? 'var(--text-primary)' : 'var(--text-secondary)',
                    cursor: 'pointer', fontSize: 12, textAlign: 'left',
                  }}
                >
                  <span style={{ fontSize: 14 }}>{TOOL_ICONS[tool]}</span>
                  {TOOL_LABELS[tool]}
                </button>
              ))}
            </div>
          </div>

          {/* Entity picker (shown when NPC/Enemy/Portal tool is active) */}
          {activeTool === 'npc' && (
            <div style={{ padding: '8px 10px', borderBottom: '1px solid var(--border-color)' }}>
              <div style={{ fontSize: 10, textTransform: 'uppercase', letterSpacing: 1, color: 'var(--text-muted)', marginBottom: 4 }}>
                NPC Template
              </div>
              {npcOnlyList.length === 0 ? (
                <div style={{ fontSize: 11, color: 'var(--text-muted)', padding: '4px 0' }}>
                  No NPCs yet. Create them in the NPCs editor.
                </div>
              ) : (
                <select className="form-select" value={selectedNpcId}
                  onChange={e => setSelectedNpcId(e.target.value)} style={{ width: '100%' }}>
                  <option value="">— pick NPC —</option>
                  {npcOnlyList.map((n: any) => (
                    <option key={n.id} value={n.id}>{n.name || n.id}</option>
                  ))}
                </select>
              )}
              <div style={{ fontSize: 10, color: 'var(--text-muted)', marginTop: 4 }}>
                Then click on the map to place
              </div>
            </div>
          )}

          {activeTool === 'enemy' && (
            <div style={{ padding: '8px 10px', borderBottom: '1px solid var(--border-color)' }}>
              <div style={{ fontSize: 10, textTransform: 'uppercase', letterSpacing: 1, color: 'var(--text-muted)', marginBottom: 4 }}>
                Enemy Template
              </div>
              {npcList.length === 0 ? (
                <div style={{ fontSize: 11, color: 'var(--text-muted)', padding: '4px 0' }}>
                  No enemies yet. Create them in the NPCs editor.
                </div>
              ) : (
                <select className="form-select" value={selectedEnemyId}
                  onChange={e => setSelectedEnemyId(e.target.value)} style={{ width: '100%' }}>
                  <option value="">— pick enemy —</option>
                  {npcList.map((n: any) => (
                    <option key={n.id} value={n.id}>{n.name || n.id}</option>
                  ))}
                </select>
              )}
              <div style={{ fontSize: 10, color: 'var(--text-muted)', marginTop: 4 }}>
                Then click on the map to place
              </div>
            </div>
          )}

          {activeTool === 'portal' && (
            <div style={{ padding: '8px 10px', borderBottom: '1px solid var(--border-color)' }}>
              <div style={{ fontSize: 10, textTransform: 'uppercase', letterSpacing: 1, color: 'var(--text-muted)', marginBottom: 4 }}>
                Destination Zone
              </div>
              <select className="form-select" value={portalTargetZone}
                onChange={e => setPortalTargetZone(e.target.value)} style={{ width: '100%' }}>
                <option value="">— pick zone —</option>
                {zoneList.filter((z: any) => z.id !== selectedZoneId).map((z: any) => (
                  <option key={z.id} value={z.id}>{z.name || z.id}</option>
                ))}
              </select>
              {portalTargetZone
                ? <div style={{ fontSize: 10, color: 'var(--text-muted)', marginTop: 4 }}>
                    Click on the map to place the portal
                  </div>
                : <div style={{ fontSize: 10, color: 'var(--warning, #ffaa44)', marginTop: 4 }}>
                    ⚠ Select a destination zone before placing
                  </div>
              }
            </div>
          )}

          {activeTool === 'entry' && (
            <div style={{ padding: '8px 10px', borderBottom: '1px solid var(--border-color)' }}>
              <div style={{ fontSize: 10, textTransform: 'uppercase', letterSpacing: 1, color: 'var(--text-muted)', marginBottom: 4 }}>
                Arriving From Zone
              </div>
              <select className="form-select" value={entryFromZone}
                onChange={e => setEntryFromZone(e.target.value)} style={{ width: '100%' }}>
                <option value="">— pick source zone —</option>
                {zoneList.filter((z: any) => z.id !== selectedZoneId).map((z: any) => (
                  <option key={z.id} value={z.id}>{z.name || z.id}</option>
                ))}
              </select>
              {entryFromZone
                ? <div style={{ fontSize: 10, color: 'var(--text-muted)', marginTop: 4 }}>
                    Click on the map to place the entry point
                  </div>
                : <div style={{ fontSize: 10, color: 'var(--warning, #ffaa44)', marginTop: 4 }}>
                    ⚠ Select a source zone before placing
                  </div>
              }
              <div style={{ fontSize: 10, color: 'var(--text-muted)', marginTop: 6, lineHeight: 1.4, borderTop: '1px solid var(--border-color)', paddingTop: 6 }}>
                Players arriving from the selected zone will spawn at this point.
              </div>
            </div>
          )}

          {/* Placed objects list */}
          <div style={{ fontSize: 10, textTransform: 'uppercase', letterSpacing: 1, color: 'var(--text-muted)', padding: '8px 10px 4px' }}>
            Objects ({spawnPoints.length})
          </div>
          <div style={{ flex: 1, overflowY: 'auto', padding: '0 6px 6px' }}>
            {spawnPoints.length === 0 ? (
              <div style={{ fontSize: 11, color: 'var(--text-muted)', padding: '8px 4px' }}>
                No objects placed yet.
              </div>
            ) : (
              spawnPoints.map(sp => (
                <div
                  key={sp.id}
                  onClick={() => {
                    const isAlreadySelected = sp.id === selectedId;
                    setSelectedId(isAlreadySelected ? null : sp.id);
                    // Pan to center this object in the canvas viewport
                    if (!isAlreadySelected && containerRef.current) {
                      const cw = containerRef.current.clientWidth;
                      const ch = containerRef.current.clientHeight;
                      const isIsoMap = mapData?.orientation === 'isometric';
                      if (isIsoMap) {
                        const isoX = (sp.x - sp.y) * zoom;
                        const isoY = ((sp.x + sp.y) / 2) * zoom;
                        setPan({ x: cw / 2 - isoX, y: ch / 2 - isoY });
                      } else {
                        setPan({ x: cw / 2 - sp.x * zoom, y: ch / 2 - sp.y * zoom });
                      }
                    }
                  }}
                  style={{
                    display: 'flex', alignItems: 'center', gap: 6,
                    padding: '5px 8px', marginBottom: 2,
                    borderRadius: 4, cursor: 'pointer',
                    background: selectedId === sp.id ? 'var(--accent-dim)' : 'transparent',
                    border: selectedId === sp.id ? '1px solid var(--accent)' : '1px solid transparent',
                  }}
                >
                  <span style={{
                    width: 8, height: 8, borderRadius: '50%', flexShrink: 0,
                    background: SPAWN_COLORS[sp.type],
                  }} />
                  <span style={{ fontSize: 11, color: 'var(--text-primary)', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                    {sp.label}
                  </span>
                  <span style={{ fontSize: 10, color: 'var(--text-muted)', flexShrink: 0, marginLeft: 'auto' }}>
                    {sp.x},{sp.y}
                  </span>
                </div>
              ))
            )}
          </div>

          {/* Legend */}
          <div style={{ padding: '8px 10px', borderTop: '1px solid var(--border-color)', fontSize: 10, color: 'var(--text-muted)' }}>
            <div style={{ marginBottom: 3, fontWeight: 600, textTransform: 'uppercase', letterSpacing: 1 }}>Legend</div>
            {Object.entries(SPAWN_COLORS).map(([type, color]) => (
              <div key={type} style={{ display: 'flex', alignItems: 'center', gap: 5, marginBottom: 2 }}>
                <span style={{ width: 8, height: 8, borderRadius: '50%', background: color, display: 'inline-block', flexShrink: 0 }} />
                <span>{type.replace('_', ' ')}</span>
              </div>
            ))}
            <div style={{ marginTop: 5 }}>
              <div><kbd style={{ fontSize: 9, background: '#333', padding: '1px 4px', borderRadius: 2 }}>Scroll</kbd> zoom</div>
              <div><kbd style={{ fontSize: 9, background: '#333', padding: '1px 4px', borderRadius: 2 }}>RMB/MMB</kbd> pan</div>
              <div><kbd style={{ fontSize: 9, background: '#333', padding: '1px 4px', borderRadius: 2 }}>Del</kbd> delete selected</div>
            </div>
          </div>
        </div>

        {/* ── CENTER: Canvas ── */}
        <div
          ref={containerRef}
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
              <div>Select a zone to load its map</div>
            </div>
          ) : loadError ? (
            <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', height: '100%', color: 'var(--danger)', textAlign: 'center', padding: 32 }}>
              <div style={{ fontSize: 32, marginBottom: 12 }}>⚠</div>
              <div>{loadError}</div>
            </div>
          ) : !mapData ? (
            <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', height: '100%', color: 'var(--text-muted)' }}>
              Loading map…
            </div>
          ) : (
            <canvas ref={canvasRef} style={{ display: 'block', position: 'absolute', top: 0, left: 0 }} />
          )}

          {/* Hint badge */}
          {mapData && activeTool !== 'select' && (
            <div style={{
              position: 'absolute', bottom: 12, left: '50%', transform: 'translateX(-50%)',
              background: 'rgba(0,0,0,0.7)', border: '1px solid var(--border-color)',
              borderRadius: 6, padding: '4px 12px', fontSize: 11, color: 'var(--text-secondary)',
              pointerEvents: 'none',
            }}>
              {TOOL_ICONS[activeTool]} {TOOL_LABELS[activeTool]} — click on map to place
            </div>
          )}
        </div>

        {/* ── RIGHT PANEL: Properties ── */}
        <div style={{
          width: 230, flexShrink: 0,
          background: 'var(--bg-secondary)',
          borderLeft: '1px solid var(--border-color)',
          display: 'flex', flexDirection: 'column',
          overflow: 'hidden',
        }}>
          <div style={{ padding: '10px 14px', borderBottom: '1px solid var(--border-color)', fontWeight: 600, fontSize: 13 }}>
            Properties
          </div>
          <div style={{ flex: 1, overflowY: 'auto', padding: '12px 14px' }}>
            {!selectedSpawn ? (
              <div style={{ fontSize: 12, color: 'var(--text-muted)', textAlign: 'center', paddingTop: 40 }}>
                <div style={{ fontSize: 28, marginBottom: 8 }}>👆</div>
                Click an object on the map or in the list to select it
              </div>
            ) : (
              <>
                {/* Type badge */}
                <div style={{ display: 'flex', alignItems: 'center', gap: 8, marginBottom: 14 }}>
                  <span style={{
                    display: 'inline-block', width: 10, height: 10, borderRadius: '50%',
                    background: SPAWN_COLORS[selectedSpawn.type], flexShrink: 0,
                  }} />
                  <span style={{ fontSize: 11, fontWeight: 600, textTransform: 'uppercase', letterSpacing: 0.5, color: 'var(--text-secondary)' }}>
                    {selectedSpawn.type.replace(/_/g, ' ')}
                  </span>
                </div>

                <div className="form-group">
                  <label className="form-label">Label</label>
                  <input className="form-input" type="text" value={selectedSpawn.label}
                    onChange={(e) => updateSpawn({ label: e.target.value })} />
                </div>

                {/* Type selector */}
                <div className="form-group">
                  <label className="form-label">Type</label>
                  <select className="form-select" value={selectedSpawn.type}
                    onChange={(e) => updateSpawn({ type: e.target.value as SpawnType })}>
                    <option value="npc_spawn">NPC Spawn</option>
                    <option value="enemy_spawn">Enemy Spawn</option>
                    <option value="player_spawn">Player Spawn</option>
                    <option value="portal">Portal (Exit)</option>
                    <option value="zone_entry">Zone Entry</option>
                  </select>
                </div>

                {/* NPC template picker */}
                {selectedSpawn.type === 'npc_spawn' && (
                  <div className="form-group">
                    <label className="form-label">NPC Template</label>
                    <select className="form-select" value={selectedSpawn.templateId}
                      onChange={(e) => {
                        const npc = npcList.find((n: any) => n.id === e.target.value) as any;
                        updateSpawn({ templateId: e.target.value, label: npc?.name || e.target.value });
                      }}>
                      <option value="">— none —</option>
                      {npcList.map((n: any) => (
                        <option key={n.id} value={n.id}>{n.name || n.id}</option>
                      ))}
                    </select>
                  </div>
                )}

                {/* Enemy template picker */}
                {selectedSpawn.type === 'enemy_spawn' && (
                  <div className="form-group">
                    <label className="form-label">Enemy Template</label>
                    <select className="form-select" value={selectedSpawn.templateId}
                      onChange={(e) => {
                        const enemy = npcList.find((n: any) => n.id === e.target.value) as any;
                        updateSpawn({ templateId: e.target.value, label: enemy?.name || e.target.value });
                      }}>
                      <option value="">— none —</option>
                      {npcList.map((n: any) => (
                        <option key={n.id} value={n.id}>{n.name || n.id}</option>
                      ))}
                    </select>
                  </div>
                )}

                {/* Portal target zone */}
                {selectedSpawn.type === 'portal' && (
                  <div className="form-group">
                    <label className="form-label">Destination Zone</label>
                    <select className="form-select" value={selectedSpawn.templateId}
                      onChange={(e) => {
                        const z = zoneList.find((zn: any) => zn.id === e.target.value) as any;
                        updateSpawn({ templateId: e.target.value, label: z?.name || e.target.value });
                      }}>
                      <option value="">— none —</option>
                      {zoneList.filter((z: any) => z.id !== selectedZoneId).map((z: any) => (
                        <option key={z.id} value={z.id}>{z.name || z.id}</option>
                      ))}
                    </select>
                  </div>
                )}

                {/* Zone entry: source zone picker */}
                {selectedSpawn.type === 'zone_entry' && (
                  <div className="form-group">
                    <label className="form-label">Arriving From Zone</label>
                    <select className="form-select" value={selectedSpawn.fromZone || selectedSpawn.templateId || ''}
                      onChange={(e) => {
                        const z = zoneList.find((zn: any) => zn.id === e.target.value) as any;
                        updateSpawn({
                          templateId: e.target.value,
                          fromZone: e.target.value,
                          label: `From ${z?.name || e.target.value}`,
                        });
                      }}>
                      <option value="">— none —</option>
                      {zoneList.filter((z: any) => z.id !== selectedZoneId).map((z: any) => (
                        <option key={z.id} value={z.id}>{z.name || z.id}</option>
                      ))}
                    </select>
                    <div style={{ fontSize: 10, color: 'var(--text-muted)', marginTop: 4, lineHeight: 1.4 }}>
                      Players arriving from this zone will spawn at this entry point.
                    </div>
                  </div>
                )}

                {/* Position */}
                <div style={{ display: 'flex', gap: 8 }}>
                  <div className="form-group" style={{ flex: 1 }}>
                    <label className="form-label">X</label>
                    <input className="form-input" type="number" value={selectedSpawn.x}
                      onChange={(e) => updateSpawn({ x: parseInt(e.target.value, 10) || 0 })} />
                  </div>
                  <div className="form-group" style={{ flex: 1 }}>
                    <label className="form-label">Y</label>
                    <input className="form-input" type="number" value={selectedSpawn.y}
                      onChange={(e) => updateSpawn({ y: parseInt(e.target.value, 10) || 0 })} />
                  </div>
                </div>

                {/* Portal / zone_entry dimensions */}
                {(selectedSpawn.type === 'portal' || selectedSpawn.type === 'zone_entry') && (
                  <div style={{ display: 'flex', gap: 8 }}>
                    <div className="form-group" style={{ flex: 1 }}>
                      <label className="form-label">W</label>
                      <input className="form-input" type="number" value={selectedSpawn.width || 64}
                        onChange={(e) => updateSpawn({ width: parseInt(e.target.value, 10) || 64 })} />
                    </div>
                    <div className="form-group" style={{ flex: 1 }}>
                      <label className="form-label">H</label>
                      <input className="form-input" type="number" value={selectedSpawn.height || 64}
                        onChange={(e) => updateSpawn({ height: parseInt(e.target.value, 10) || 64 })} />
                    </div>
                  </div>
                )}

                {/* ID (read-only) */}
                <div className="form-group">
                  <label className="form-label">ID</label>
                  <input className="form-input" type="text" value={selectedSpawn.id} disabled
                    style={{ opacity: 0.5, fontSize: 10 }} />
                </div>

                <button className="btn btn-danger"
                  onClick={deleteSelected}
                  style={{ width: '100%', marginTop: 8 }}>
                  🗑 Delete Object
                </button>
              </>
            )}
          </div>
        </div>
      </div>

      {/* ── Status bar ── */}
      <div style={{
        flexShrink: 0,
        height: 24,
        display: 'flex',
        alignItems: 'center',
        gap: 18,
        padding: '0 14px',
        background: 'var(--bg-tertiary, #0d0d14)',
        borderTop: '1px solid var(--border-color)',
        fontSize: 11,
        color: 'var(--text-muted)',
        fontVariantNumeric: 'tabular-nums',
      }}>
        {mapData ? (
          <>
            <span title="Map size in tiles">
              <span style={{ color: 'var(--text-secondary)', fontWeight: 600 }}>Tiles </span>
              {mapData.width} × {mapData.height}
            </span>
            <span style={{ width: 1, height: 12, background: 'var(--border-color)' }} />
            <span title="Map size in world pixels">
              <span style={{ color: 'var(--text-secondary)', fontWeight: 600 }}>Pixels </span>
              {(mapData.width * mapData.tilewidth).toLocaleString()} × {(mapData.height * mapData.tileheight).toLocaleString()}
            </span>
            <span style={{ width: 1, height: 12, background: 'var(--border-color)' }} />
            <span title="Tile size">
              <span style={{ color: 'var(--text-secondary)', fontWeight: 600 }}>Tile </span>
              {mapData.tilewidth} × {mapData.tileheight} px
            </span>
            <span style={{ width: 1, height: 12, background: 'var(--border-color)' }} />
            <span title="Map orientation" style={{ textTransform: 'capitalize' }}>
              {mapData.orientation ?? 'orthogonal'}
            </span>
            <span style={{ width: 1, height: 12, background: 'var(--border-color)' }} />
            <span title="Number of placed objects">
              {spawnPoints.length} object{spawnPoints.length !== 1 ? 's' : ''}
            </span>
          </>
        ) : (
          <span>No map loaded</span>
        )}
      </div>
    </div>
  );
};
