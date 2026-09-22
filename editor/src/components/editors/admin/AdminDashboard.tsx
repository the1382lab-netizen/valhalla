/**
 * AdminDashboard — Live server monitoring & control panel.
 *
 * Shows a visual overview of each zone with real-time player/NPC/loot bag
 * positions, and provides right-click context menus for admin actions
 * (spawn NPC, drop item, teleport player, kick, kill/respawn NPC).
 */
import React, { useEffect, useRef, useState, useCallback } from 'react';
import { useEditorStore } from '../../../store/editorStore';
import { ADMIN_RESULT_EVENT, AdminResult, postAdmin } from './adminApi';
import {
  ActionDialog, ActionDialogSpec, AdminPlayer, BansDialog, InspectDialog, PlayersPanel, buildPlayerMenu,
} from './AdminPlayerTools';

// ── Types ───────────────────────────────────────────────────

interface PlayerInfo {
  sessionId: string;
  name: string;
  classId: string;
  level: number;
  x: number;
  y: number;
  hp: number;
  maxHp: number;
  mana: number;
  maxMana: number;
  alive: boolean;
  zoneId?: string;
  godMode?: boolean;
  frozen?: boolean;
  mutedSeconds?: number;
}

/** An NPC Spawn Point placed in the Unreal level (2.0). */
interface SpawnPointInfo {
  id: string;
  label: string;
  npcClass: string;
  templateId: string;
  x: number;
  y: number;
  respawnSeconds: number;
  respawnOverride: boolean;
  respawnIn: number;
  npcId: string;
  npcAlive: boolean;
}

interface NPCInfo {
  id: string;
  templateId: string;
  name: string;
  npcType: string;
  level: number;
  x: number;
  y: number;
  hp: number;
  maxHp: number;
  alive: boolean;
}

interface LootBagInfo {
  id: string;
  x: number;
  y: number;
  itemCount: number;
  items: { itemId: string; quantity: number }[];
}

interface OverlaySpawnPoint {
  id: string;
  type: 'player_spawn' | 'enemy_spawn' | 'npc_spawn' | 'portal' | 'zone_entry';
  x: number;
  y: number;
  label?: string;
  templateId?: string;
  width?: number;
  height?: number;
  fromZone?: string;
  targetZone?: string;
  targetEntry?: string;
  count?: number;
  radius?: number;
}

interface OverlayData {
  spawnPoints: OverlaySpawnPoint[];
}

/** maps/thumbs/<zone>.json — the UE top-down capture metadata. */
interface ThumbMeta {
  originX: number;
  originY: number;
  sizeX: number;        // cm
  sizeY: number;        // cm
  pixelsPerCm: number;
}

interface ZoneData {
  players: PlayerInfo[];
  npcs: NPCInfo[];
  lootBags: LootBagInfo[];
  spawnPoints?: SpawnPointInfo[];
}

interface ServerState {
  online: boolean;
  zones: Record<string, ZoneData>;
  totalPlayers: number;
  totalNpcs: number;
  totalLootBags: number;
}

interface ContextMenu {
  x: number;
  y: number;
  worldX: number;
  worldY: number;
  items: ContextMenuItem[];
}

interface ContextMenuItem {
  label: string;
  action: () => void;
  danger?: boolean;
}

// ── Constants ───────────────────────────────────────────────

const POLL_INTERVAL = 1500;
const CANVAS_BG = '#0a0a18';
const GRID_COLOR = 'rgba(60, 60, 100, 0.25)';
const GRID_SIZE = 64;
/** Grid spacing in cm for Valhalla 2.0 zones (5 m). */
const GRID_SIZE_CM = 500;
/** Portal / zone-entry box drawn in cm — the UE actor match tolerance. */
const ACTOR_MATCH_CM = 128;

// Entity colors
const PLAYER_COLOR = '#44cc44';
const PLAYER_DEAD_COLOR = '#666666';
const NPC_ENEMY_COLOR = '#ff4444';
const NPC_FRIENDLY_COLOR = '#44aaff';
const NPC_DEAD_COLOR = '#553333';
const LOOT_COLOR = '#ffcc00';
const PORTAL_COLOR = 'rgba(160, 80, 255, 0.35)';
const PORTAL_STROKE = 'rgba(160, 80, 255, 0.75)';
const ZONE_ENTRY_FILL = 'rgba(50, 180, 255, 0.2)';
const ZONE_ENTRY_STROKE = 'rgba(80, 180, 255, 0.75)';
const SPAWN_PLAYER_COLOR = '#00e5ff';
const SPAWN_ENEMY_COLOR = '#ff9800';

// ── Helpers ─────────────────────────────────────────────────

async function fetchAdminState(): Promise<ServerState> {
  const res = await fetch('/api/admin/state');
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  return res.json();
}

// ── Component ───────────────────────────────────────────────

export const AdminDashboard: React.FC = () => {
  const [state, setState] = useState<ServerState | null>(null);
  const [selectedZone, setSelectedZone] = useState<string | null>(null);
  const [contextMenu, setContextMenu] = useState<ContextMenu | null>(null);
  const [hoveredEntity, setHoveredEntity] = useState<string | null>(null);
  const [tooltip, setTooltip] = useState<{ x: number; y: number; text: string } | null>(null);
  const [spawnDialog, setSpawnDialog] = useState<{ type: 'npc' | 'item'; worldX: number; worldY: number } | null>(null);
  const [teleportDialog, setTeleportDialog] = useState<{ sessionId: string; name: string } | null>(null);
  /** Admin API the editor server proxies to (VALHALLA_ADMIN_URL). */
  const [adminUrl, setAdminUrl] = useState<string>('');
  const [reloadStatus, setReloadStatus] = useState<string | null>(null);
  /** Last admin action result, shown as a toast for a few seconds. */
  const [notice, setNotice] = useState<AdminResult | null>(null);
  /** The open generic admin dialog (give item, set HP, mute, kick, …). */
  const [actionDialog, setActionDialog] = useState<ActionDialogSpec | null>(null);
  /** The player whose inspect window is open. */
  const [inspectPlayer, setInspectPlayer] = useState<AdminPlayer | null>(null);
  const [bansOpen, setBansOpen] = useState(false);
  /** The player highlighted in the side list. */
  const [selectedPlayerId, setSelectedPlayerId] = useState<string | null>(null);
  /** Forces a re-render (and thus a fresh draw closure) when a capture finishes loading. */
  const [thumbTick, setThumbTick] = useState(0);

  const canvasRef = useRef<HTMLCanvasElement>(null);
  const containerRef = useRef<HTMLDivElement>(null);
  const panRef = useRef({ x: 0, y: 0 });
  const zoomRef = useRef(0.5);
  const dragRef = useRef<{
    button: number;      // which mouse button started the drag (-1 = none)
    moved: boolean;      // has moved past threshold → is a real drag, not a click
    startX: number;
    startY: number;
    startPanX: number;
    startPanY: number;
  }>({ button: -1, moved: false, startX: 0, startY: 0, startPanX: 0, startPanY: 0 });

  /** Live cursor position in world space — updated every mousemove, read by draw loop. */
  const cursorWorldRef = useRef({ x: 0, y: 0 });

  /** Cached overlay data per zoneId — fetched once on first visit, never evicted. */
  const overlayCache = useRef<Record<string, OverlayData>>({});
  /** Capture metadata per zoneId; `null` means "checked, this zone has no capture". */
  const thumbMetaCache = useRef<Record<string, ThumbMeta | null>>({});
  const thumbImageCache = useRef<Record<string, HTMLImageElement>>({});

  // Access editor store for NPC templates and items
  const npcTemplates = useEditorStore(s => s.npcTemplates.data);
  const items = useEditorStore(s => s.items.data);
  const zones = useEditorStore(s => s.zones.data);

  // ── Polling ─────────────────────────────────────────────

  useEffect(() => {
    let mounted = true;
    const poll = async () => {
      try {
        const data = await fetchAdminState();
        if (mounted) {
          setState(data);
          // Auto-select first zone if none selected
          if (!selectedZone && Object.keys(data.zones).length > 0) {
            setSelectedZone(Object.keys(data.zones)[0]);
          }
        }
      } catch {
        // Server unreachable
        if (mounted) setState(prev => prev ? { ...prev, online: false } : null);
      }
    };

    poll();
    const interval = setInterval(poll, POLL_INTERVAL);
    return () => { mounted = false; clearInterval(interval); };
  }, [selectedZone]);

  // ── Admin action results → toast ────────────────────────

  useEffect(() => {
    let timer: ReturnType<typeof setTimeout> | undefined;
    const onResult = (e: Event) => {
      setNotice((e as CustomEvent<AdminResult>).detail);
      if (timer) clearTimeout(timer);
      timer = setTimeout(() => setNotice(null), 6000);
    };
    window.addEventListener(ADMIN_RESULT_EVENT, onResult);
    return () => { window.removeEventListener(ADMIN_RESULT_EVENT, onResult); if (timer) clearTimeout(timer); };
  }, []);

  // ── Editor server config (admin URL shown in the status bar) ────────────

  useEffect(() => {
    fetch('/api/config')
      .then(r => r.json())
      .then(c => setAdminUrl(c.adminUrl || ''))
      .catch(() => {});
  }, []);

  /** Fit the whole zone capture into the canvas. */
  const fitToCapture = useCallback((meta: ThumbMeta) => {
    const container = containerRef.current;
    if (!container) return;
    const cw = container.clientWidth, ch = container.clientHeight;
    if (!cw || !ch || !meta.sizeX || !meta.sizeY) return;
    const z = Math.min(cw / meta.sizeX, ch / meta.sizeY) * 0.94;
    zoomRef.current = z;
    panRef.current = { x: (cw - meta.sizeX * z) / 2, y: (ch - meta.sizeY * z) / 2 };
  }, []);

  // ── Zone capture + overlay loading ──────────────────────
  // A zone with a capture is a Valhalla 2.0 zone: world space is zone-local cm
  // and its overlay comes from maps/overlays-2.0. Zones without one keep the
  // 1.0 behaviour (world pixels, maps/overlays).

  useEffect(() => {
    if (!selectedZone) return;
    const zone = selectedZone;
    let cancelled = false;

    (async () => {
      let meta = thumbMetaCache.current[zone];
      if (meta === undefined) {
        try {
          const res = await fetch(`/api/thumbs/${zone}.json`);
          meta = res.ok ? await res.json() : null;
        } catch {
          meta = null;
        }
        if (cancelled) return;
        thumbMetaCache.current[zone] = meta ?? null;
        if (meta) {
          const img = new Image();
          img.onload = () => setThumbTick(t => t + 1);
          img.src = `/api/thumbs/${zone}.png`;
          thumbImageCache.current[zone] = img;
        }
      }
      if (meta) fitToCapture(meta);

      if (overlayCache.current[zone]) return;
      try {
        const url = meta ? `/api/overlays2/${zone}` : `/api/overlays/${zone}-overlay.json`;
        const res = await fetch(url);
        if (!res.ok) return;
        const data: OverlayData | null = await res.json();
        if (data && !cancelled) overlayCache.current[zone] = data;
      } catch { /* no overlay yet */ }
    })();

    return () => { cancelled = true; };
  }, [selectedZone, fitToCapture]);

  // ── World ↔ Screen coordinate conversion ────────────────

  const worldToScreen = useCallback((wx: number, wy: number) => {
    const zoom = zoomRef.current;
    return {
      x: wx * zoom + panRef.current.x,
      y: wy * zoom + panRef.current.y,
    };
  }, []);

  const screenToWorld = useCallback((sx: number, sy: number) => {
    const zoom = zoomRef.current;
    return {
      x: (sx - panRef.current.x) / zoom,
      y: (sy - panRef.current.y) / zoom,
    };
  }, []);

  // ── Canvas Rendering ────────────────────────────────────

  const draw = useCallback(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const w = canvas.width;
    const h = canvas.height;
    const zoom = zoomRef.current;
    const pan = panRef.current;

    // Clear
    ctx.fillStyle = CANVAS_BG;
    ctx.fillRect(0, 0, w, h);

    // ── Zone capture background (Valhalla 2.0) ──
    // With a capture, world space is zone-local cm: 1 world unit = 1 cm, so the
    // capture spans sizeX × sizeY world units from the zone origin (0,0).
    const meta = selectedZone ? thumbMetaCache.current[selectedZone] : null;
    const thumbImg = selectedZone ? thumbImageCache.current[selectedZone] : undefined;
    const inCm = !!meta;

    if (meta) {
      const iw = meta.sizeX * zoom;
      const ih = meta.sizeY * zoom;
      if (thumbImg && thumbImg.complete && thumbImg.naturalWidth > 0) {
        ctx.drawImage(thumbImg, pan.x, pan.y, iw, ih);
      }
      ctx.strokeStyle = 'rgba(255,255,255,0.25)';
      ctx.lineWidth = 1;
      ctx.strokeRect(pan.x, pan.y, iw, ih);
    }

    if (!state || !selectedZone || !state.zones[selectedZone]) {
      // Draw "no data" message
      const offline = state?.online === false || state === null;
      ctx.fillStyle = offline ? '#ff8888' : '#666';
      ctx.font = '16px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText(
        offline ? 'Valhalla 2.0 admin API offline' : 'No zone data available',
        w / 2, h / 2 - 8,
      );
      if (offline) {
        ctx.fillStyle = '#997';
        ctx.font = '12px monospace';
        ctx.fillText(`${adminUrl || 'admin API'}/api/admin/state`, w / 2, h / 2 + 14);
      }
      return;
    }

    const zoneData = state.zones[selectedZone];

    // ── Grid ──
    ctx.strokeStyle = GRID_COLOR;
    ctx.lineWidth = 1;

    const gridStep = (inCm ? GRID_SIZE_CM : GRID_SIZE) * zoom;
    const startX = pan.x % gridStep;
    const startY = pan.y % gridStep;

    if (gridStep > 6) {
      ctx.beginPath();
      for (let gx = startX; gx < w; gx += gridStep) {
        ctx.moveTo(gx, 0);
        ctx.lineTo(gx, h);
      }
      for (let gy = startY; gy < h; gy += gridStep) {
        ctx.moveTo(0, gy);
        ctx.lineTo(w, gy);
      }
      ctx.stroke();
    }

    // ── Origin crosshair ──
    const origin = worldToScreen(0, 0);
    ctx.strokeStyle = 'rgba(100, 100, 140, 0.5)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(origin.x, 0);
    ctx.lineTo(origin.x, h);
    ctx.moveTo(0, origin.y);
    ctx.lineTo(w, origin.y);
    ctx.stroke();

    // ── Overlay objects (portals, spawn points) ──
    // Rendered before entities so they appear beneath players/NPCs.
    const overlay = overlayCache.current[selectedZone];
    if (overlay?.spawnPoints) {
      for (const sp of overlay.spawnPoints) {
        const s = worldToScreen(sp.x, sp.y);

        if (sp.type === 'portal' || sp.type === 'zone_entry') {
          const isPortal = sp.type === 'portal';
          // Overlay 2.0 points are a position in cm — drawn as the UE actor-match
          // box; overlay 1.0 rectangles keep their own width/height in pixels.
          const sw = (inCm ? ACTOR_MATCH_CM : (sp.width ?? (isPortal ? 128 : 64))) * zoom;
          const sh = (inCm ? ACTOR_MATCH_CM : (sp.height ?? (isPortal ? 128 : 64))) * zoom;
          const bx = inCm ? s.x - sw / 2 : s.x;
          const by = inCm ? s.y - sh / 2 : s.y;
          ctx.fillStyle = isPortal ? PORTAL_COLOR : ZONE_ENTRY_FILL;
          ctx.fillRect(bx, by, sw, sh);
          ctx.strokeStyle = isPortal ? PORTAL_STROKE : ZONE_ENTRY_STROKE;
          ctx.lineWidth = 1.5;
          ctx.setLineDash([5, 3]);
          ctx.strokeRect(bx, by, sw, sh);
          ctx.setLineDash([]);
          if (zoom > 0.15 && sp.label) {
            ctx.fillStyle = isPortal ? 'rgba(180, 100, 255, 0.9)' : 'rgba(80, 200, 255, 0.9)';
            ctx.font = `${isPortal ? 'bold ' : ''}${Math.max(9, 11 * zoom)}px sans-serif`;
            ctx.textAlign = 'center';
            ctx.fillText(`${isPortal ? '⬦' : '↓'} ${sp.label}`, bx + sw / 2, by + sh / 2 + 4);
          }

        } else if (sp.type === 'player_spawn' || sp.type === 'enemy_spawn' || sp.type === 'npc_spawn') {
          const color = sp.type === 'player_spawn' ? SPAWN_PLAYER_COLOR : SPAWN_ENEMY_COLOR;
          const size = Math.max(5, 7 * zoom);
          // Overlay 2.0 enemy spawns carry a wander radius in cm
          if (sp.type === 'enemy_spawn' && sp.radius) {
            ctx.beginPath();
            ctx.arc(s.x, s.y, sp.radius * zoom, 0, Math.PI * 2);
            ctx.strokeStyle = color;
            ctx.globalAlpha = 0.35;
            ctx.setLineDash([3, 3]);
            ctx.stroke();
            ctx.setLineDash([]);
            ctx.globalAlpha = 1;
          }
          ctx.beginPath();
          ctx.moveTo(s.x, s.y - size);        // top
          ctx.lineTo(s.x + size, s.y);         // right
          ctx.lineTo(s.x, s.y + size);         // bottom
          ctx.lineTo(s.x - size, s.y);         // left
          ctx.closePath();
          ctx.fillStyle = color;
          ctx.globalAlpha = 0.7;
          ctx.fill();
          ctx.globalAlpha = 1;
          ctx.strokeStyle = color;
          ctx.lineWidth = 1;
          ctx.stroke();
          if (zoom > 0.3 && sp.label) {
            ctx.fillStyle = color;
            ctx.font = `${Math.max(8, 10 * zoom)}px sans-serif`;
            ctx.textAlign = 'center';
            ctx.globalAlpha = 0.85;
            ctx.fillText(sp.label, s.x, s.y + size + 12);
            ctx.globalAlpha = 1;
          }
        }
      }
    }

    // ── NPC Spawn Points (placed in Unreal) ──
    // A hollow orange diamond; a countdown under it while its NPC is down.
    for (const sp of zoneData.spawnPoints ?? []) {
      const s = worldToScreen(sp.x, sp.y);
      const r = Math.max(5, 8 * zoom);
      const isHovered = hoveredEntity === `spawn_${sp.id}`;
      ctx.beginPath();
      ctx.moveTo(s.x, s.y - r);
      ctx.lineTo(s.x + r, s.y);
      ctx.lineTo(s.x, s.y + r);
      ctx.lineTo(s.x - r, s.y);
      ctx.closePath();
      ctx.strokeStyle = SPAWN_ENEMY_COLOR;
      ctx.lineWidth = isHovered ? 2.5 : 1.5;
      ctx.globalAlpha = sp.npcAlive ? 0.55 : 1;
      ctx.stroke();
      ctx.globalAlpha = 1;
      if (!sp.npcAlive && sp.respawnIn > 0 && zoom > 0.15) {
        ctx.fillStyle = SPAWN_ENEMY_COLOR;
        ctx.font = `${Math.max(9, 10 * zoom)}px monospace`;
        ctx.textAlign = 'center';
        ctx.fillText(`${Math.ceil(sp.respawnIn)}s`, s.x, s.y + r + 11);
      }
    }

    // ── Loot bags ──
    for (const bag of zoneData.lootBags) {
      const s = worldToScreen(bag.x, bag.y);
      const size = 5 * zoom + 3;
      ctx.fillStyle = LOOT_COLOR;
      ctx.globalAlpha = 0.8;
      ctx.fillRect(s.x - size / 2, s.y - size / 2, size, size);
      ctx.globalAlpha = 1;
    }

    // ── NPCs ──
    for (const npc of zoneData.npcs) {
      const s = worldToScreen(npc.x, npc.y);
      const radius = npc.alive ? Math.max(5, 8 * zoom) : Math.max(3, 5 * zoom);
      const isHovered = hoveredEntity === `npc_${npc.id}`;

      // Circle
      ctx.beginPath();
      ctx.arc(s.x, s.y, radius, 0, Math.PI * 2);
      if (!npc.alive) {
        ctx.fillStyle = NPC_DEAD_COLOR;
        ctx.globalAlpha = 0.5;
      } else {
        ctx.fillStyle = npc.npcType === 'enemy' ? NPC_ENEMY_COLOR : NPC_FRIENDLY_COLOR;
        ctx.globalAlpha = isHovered ? 1 : 0.85;
      }
      ctx.fill();
      ctx.globalAlpha = 1;

      // HP bar (alive only)
      if (npc.alive && npc.hp < npc.maxHp) {
        const barW = radius * 2.5;
        const barH = 3;
        const barX = s.x - barW / 2;
        const barY = s.y - radius - 6;
        ctx.fillStyle = '#333';
        ctx.fillRect(barX, barY, barW, barH);
        ctx.fillStyle = '#ff3333';
        ctx.fillRect(barX, barY, barW * (npc.hp / npc.maxHp), barH);
      }

      // Label
      if (zoom > 0.3) {
        ctx.fillStyle = npc.alive ? '#ddd' : '#777';
        ctx.font = `${Math.max(9, 11 * zoom)}px sans-serif`;
        ctx.textAlign = 'center';
        ctx.fillText(npc.name, s.x, s.y + radius + 12);
      }
    }

    // ── Players ──
    for (const player of zoneData.players) {
      const s = worldToScreen(player.x, player.y);
      const radius = Math.max(6, 10 * zoom);
      const isHovered = hoveredEntity === `player_${player.sessionId}`;

      // Outer glow
      if (isHovered) {
        ctx.beginPath();
        ctx.arc(s.x, s.y, radius + 3, 0, Math.PI * 2);
        ctx.fillStyle = 'rgba(68, 204, 68, 0.3)';
        ctx.fill();
      }

      // Circle
      ctx.beginPath();
      ctx.arc(s.x, s.y, radius, 0, Math.PI * 2);
      ctx.fillStyle = player.alive ? PLAYER_COLOR : PLAYER_DEAD_COLOR;
      ctx.fill();

      // White border
      ctx.strokeStyle = '#fff';
      ctx.lineWidth = 1.5;
      ctx.stroke();

      // HP bar
      if (player.alive && player.hp < player.maxHp) {
        const barW = radius * 3;
        const barH = 3;
        const barX = s.x - barW / 2;
        const barY = s.y - radius - 8;
        ctx.fillStyle = '#333';
        ctx.fillRect(barX, barY, barW, barH);
        ctx.fillStyle = '#44cc44';
        ctx.fillRect(barX, barY, barW * (player.hp / player.maxHp), barH);
      }

      // Name label
      ctx.fillStyle = '#fff';
      ctx.font = `bold ${Math.max(10, 12 * zoom)}px sans-serif`;
      ctx.textAlign = 'center';
      ctx.fillText(player.name, s.x, s.y + radius + 14);

      // Level + class
      if (zoom > 0.35) {
        ctx.fillStyle = '#aaa';
        ctx.font = `${Math.max(8, 10 * zoom)}px sans-serif`;
        ctx.fillText(`Lv.${player.level} ${player.classId}`, s.x, s.y + radius + 26);
      }
    }

    // ── Legend ──
    ctx.fillStyle = '#888';
    ctx.font = '11px sans-serif';
    ctx.textAlign = 'left';
    const lx = 12;
    let ly = h - 124; // 7 legend items × ~16px each
    const drawLegendDot = (color: string, label: string) => {
      ctx.fillStyle = color;
      ctx.beginPath();
      ctx.arc(lx, ly, 4, 0, Math.PI * 2);
      ctx.fill();
      ctx.fillStyle = '#888';
      ctx.fillText(label, lx + 10, ly + 4);
      ly += 16;
    };
    drawLegendDot(PLAYER_COLOR, 'Player');
    drawLegendDot(NPC_ENEMY_COLOR, 'Enemy NPC');
    drawLegendDot(LOOT_COLOR, 'Loot Bag');
    drawLegendDot(SPAWN_PLAYER_COLOR, 'Player Spawn');
    drawLegendDot(SPAWN_ENEMY_COLOR, 'NPC Spawn Point');
    drawLegendDot('rgba(160, 80, 255, 0.8)', 'Portal');
    drawLegendDot('rgba(80, 200, 255, 0.8)', 'Zone Entry');

    // ── Bottom-right status: cursor world coords + zoom ──
    const cw = cursorWorldRef.current;
    ctx.fillStyle = '#555';
    ctx.font = '10px monospace';
    ctx.textAlign = 'right';
    ctx.fillText(
      `(${Math.round(cw.x)}, ${Math.round(cw.y)})${inCm ? ' cm' : ' px'}   Zoom: ${(zoom * 100).toFixed(0)}%`,
      w - 12, h - 12,
    );
  }, [state, selectedZone, hoveredEntity, worldToScreen, adminUrl, thumbTick]);

  // ── Animation loop ──────────────────────────────────────

  useEffect(() => {
    let raf: number;
    const loop = () => {
      draw();
      raf = requestAnimationFrame(loop);
    };
    raf = requestAnimationFrame(loop);
    return () => cancelAnimationFrame(raf);
  }, [draw]);

  // ── Canvas resize ───────────────────────────────────────

  useEffect(() => {
    const container = containerRef.current;
    const canvas = canvasRef.current;
    if (!container || !canvas) return;

    const observer = new ResizeObserver(() => {
      canvas.width = container.clientWidth;
      canvas.height = container.clientHeight;
    });
    observer.observe(container);
    canvas.width = container.clientWidth;
    canvas.height = container.clientHeight;
    return () => observer.disconnect();
  }, []);

  // ── Mouse interactions ──────────────────────────────────

  const handleWheel = useCallback((e: React.WheelEvent) => {
    e.preventDefault();
    const canvas = canvasRef.current;
    if (!canvas) return;

    const rect = canvas.getBoundingClientRect();
    const mx = e.clientX - rect.left;
    const my = e.clientY - rect.top;

    const oldZoom = zoomRef.current;
    const zoomFactor = e.deltaY < 0 ? 1.12 : 1 / 1.12;
    const newZoom = Math.max(0.05, Math.min(5, oldZoom * zoomFactor));

    // Zoom toward mouse position
    panRef.current.x = mx - (mx - panRef.current.x) * (newZoom / oldZoom);
    panRef.current.y = my - (my - panRef.current.y) * (newZoom / oldZoom);
    zoomRef.current = newZoom;
  }, []);

  const handleMouseDown = useCallback((e: React.MouseEvent) => {
    // Left (0) or middle (1) click both initiate drag capture.
    // Actual panning activates after the cursor moves past a 3px threshold,
    // so a plain click still registers as a click.
    if (e.button === 0 || e.button === 1) {
      dragRef.current = {
        button: e.button,
        moved: false,
        startX: e.clientX,
        startY: e.clientY,
        startPanX: panRef.current.x,
        startPanY: panRef.current.y,
      };
      if (e.button === 1) e.preventDefault(); // suppress middle-click scroll
    }
  }, []);

  const handleMouseMove = useCallback((e: React.MouseEvent) => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    const rect = canvas.getBoundingClientRect();
    const mx = e.clientX - rect.left;
    const my = e.clientY - rect.top;

    // Always update the live cursor world-space coords (read by draw loop)
    const worldPos = screenToWorld(mx, my);
    cursorWorldRef.current = worldPos;

    // ── Drag / pan logic ──
    const drag = dragRef.current;
    if (drag.button !== -1) {
      const dx = e.clientX - drag.startX;
      const dy = e.clientY - drag.startY;
      // Activate drag once past a 3px threshold
      if (!drag.moved && dx * dx + dy * dy > 9) {
        drag.moved = true;
        if (containerRef.current) containerRef.current.style.cursor = 'grabbing';
      }
      if (drag.moved) {
        panRef.current.x = drag.startPanX + dx;
        panRef.current.y = drag.startPanY + dy;
        return; // skip hover detection while panning
      }
    }

    // ── Hover detection ──
    if (!state || !selectedZone || !state.zones[selectedZone]) return;
    const world = worldPos;
    const zoneData = state.zones[selectedZone];
    const hitRadius = 15 / zoomRef.current;

    let found: string | null = null;
    let ttText = '';

    // Check players first (higher priority)
    for (const p of zoneData.players) {
      const dx = world.x - p.x;
      const dy = world.y - p.y;
      if (dx * dx + dy * dy < hitRadius * hitRadius) {
        found = `player_${p.sessionId}`;
        ttText = `${p.name} (Lv.${p.level} ${p.classId})\nHP: ${p.hp}/${p.maxHp} | Mana: ${p.mana}/${p.maxMana}\nPos: ${p.x}, ${p.y}`;
        break;
      }
    }

    if (!found) {
      for (const n of zoneData.npcs) {
        const dx = world.x - n.x;
        const dy = world.y - n.y;
        if (dx * dx + dy * dy < hitRadius * hitRadius) {
          found = `npc_${n.id}`;
          ttText = `${n.name} (Lv.${n.level} ${n.npcType})\nHP: ${n.hp}/${n.maxHp} | ${n.alive ? 'Alive' : 'Dead'}\nPos: ${n.x}, ${n.y}`;
          break;
        }
      }
    }

    if (!found) {
      for (const b of zoneData.lootBags) {
        const dx = world.x - b.x;
        const dy = world.y - b.y;
        if (dx * dx + dy * dy < hitRadius * hitRadius) {
          found = `bag_${b.id}`;
          ttText = `Loot Bag (${b.itemCount} item${b.itemCount !== 1 ? 's' : ''})\nPos: ${b.x}, ${b.y}`;
          break;
        }
      }
    }

    if (!found) {
      for (const sp of zoneData.spawnPoints ?? []) {
        const dx = world.x - sp.x;
        const dy = world.y - sp.y;
        if (dx * dx + dy * dy < hitRadius * hitRadius) {
          found = `spawn_${sp.id}`;
          const timer = `${Math.round(sp.respawnSeconds)}s ${sp.respawnOverride ? '(spawn point override)' : '(template respawnMs)'}`;
          const status = sp.npcAlive ? 'NPC up' : (sp.respawnIn > 0 ? `respawning in ${Math.ceil(sp.respawnIn)}s` : 'no NPC');
          ttText = `NPC Spawn Point: ${sp.label}\n${sp.npcClass} · ${sp.templateId}\nRespawn: ${timer}\n${status}\nPos: ${sp.x}, ${sp.y}`;
          break;
        }
      }
    }

    // Check overlay spawn points / portals
    if (!found) {
      const overlay = overlayCache.current[selectedZone];
      if (overlay?.spawnPoints) {
        for (const sp of overlay.spawnPoints) {
          const isCm = !!thumbMetaCache.current[selectedZone];
          const unit = isCm ? 'cm' : 'px';
          if (sp.type === 'portal' || sp.type === 'zone_entry') {
            const bw = isCm ? ACTOR_MATCH_CM : (sp.width ?? 128);
            const bh = isCm ? ACTOR_MATCH_CM : (sp.height ?? 64);
            const left = isCm ? sp.x - bw / 2 : sp.x;
            const top = isCm ? sp.y - bh / 2 : sp.y;
            if (world.x >= left && world.x <= left + bw && world.y >= top && world.y <= top + bh) {
              found = `overlay_${sp.id}`;
              const typeLabel = sp.type === 'portal' ? 'Portal' : 'Zone Entry';
              const link = sp.type === 'portal'
                ? (sp.targetZone ? `\n→ ${sp.targetZone}${sp.targetEntry ? ` / ${sp.targetEntry}` : ''}` : '')
                : (sp.fromZone ? `\n← ${sp.fromZone}` : '');
              ttText = `${typeLabel}: ${sp.label ?? sp.id}${link}\nPos: ${sp.x}, ${sp.y} ${unit}`;
              break;
            }
          } else {
            const dx = world.x - sp.x;
            const dy = world.y - sp.y;
            if (dx * dx + dy * dy < hitRadius * hitRadius) {
              found = `overlay_${sp.id}`;
              const typeLabel = sp.type === 'player_spawn' ? 'Player Spawn'
                : sp.type === 'npc_spawn' ? 'NPC Spawn' : 'Enemy Spawn';
              const pack = sp.count ? `\n${sp.count}× within ${sp.radius ?? 0} cm` : '';
              ttText = `${typeLabel}${sp.label ? ': ' + sp.label : ''}${pack}\nPos: ${sp.x}, ${sp.y} ${unit}`;
              break;
            }
          }
        }
      }
    }

    setHoveredEntity(found);
    if (found) {
      setTooltip({ x: e.clientX, y: e.clientY, text: ttText });
    } else {
      setTooltip(null);
    }
  }, [state, selectedZone, screenToWorld]);

  const handleMouseUp = useCallback((_e: React.MouseEvent) => {
    // Reset drag state — context menu is dismissed by its backdrop div, not here
    dragRef.current.button = -1;
    dragRef.current.moved = false;
    if (containerRef.current) containerRef.current.style.cursor = '';
  }, []);

  // ── Player admin menu (shared by the map and the player list) ──────

  const playerMenuFor = useCallback((p: AdminPlayer): ContextMenuItem[] => {
    const players: AdminPlayer[] = state?.zones
      ? Object.entries(state.zones).flatMap(([zoneId, z]) => z.players.map(q => ({ ...q, zoneId: q.zoneId ?? zoneId })))
      : [];
    return buildPlayerMenu(p, {
      allPlayers: players,
      items: items || {},
      openDialog: spec => setActionDialog(spec),
      inspect: player => setInspectPlayer(player),
      teleport: player => setTeleportDialog({ sessionId: player.sessionId, name: player.name }),
    });
  }, [state, items]);

  const openPlayerMenuAt = useCallback((e: React.MouseEvent, p: AdminPlayer) => {
    setContextMenu({ x: e.clientX, y: e.clientY, worldX: Math.round(p.x), worldY: Math.round(p.y), items: playerMenuFor(p) });
  }, [playerMenuFor]);

  // ── Right-click context menu ────────────────────────────

  const handleContextMenu = useCallback((e: React.MouseEvent) => {
    e.preventDefault();
    if (!state || !selectedZone || !state.zones[selectedZone]) return;

    const canvas = canvasRef.current;
    if (!canvas) return;
    const rect = canvas.getBoundingClientRect();
    const mx = e.clientX - rect.left;
    const my = e.clientY - rect.top;
    const world = screenToWorld(mx, my);
    const zoneData = state.zones[selectedZone];
    const hitRadius = 15 / zoomRef.current;

    const menuItems: ContextMenuItem[] = [];

    // Check if clicking on a player: the full admin menu.
    for (const p of zoneData.players) {
      const dx = world.x - p.x;
      const dy = world.y - p.y;
      if (dx * dx + dy * dy < hitRadius * hitRadius) {
        menuItems.push(...playerMenuFor({ ...p, zoneId: p.zoneId ?? selectedZone }));
        break;
      }
    }

    // Check if clicking on an NPC Spawn Point
    for (const sp of zoneData.spawnPoints ?? []) {
      const dx = world.x - sp.x;
      const dy = world.y - sp.y;
      if (dx * dx + dy * dy < hitRadius * hitRadius) {
        if (!sp.npcAlive) {
          menuItems.push({
            label: `Respawn ${sp.label} now`,
            action: () => { postAdmin('spawn-point-action', { spawnPointId: sp.id, action: 'respawn-now' }, { label: `respawn ${sp.label}` }); },
          });
        }
        menuItems.push({
          label: `Set ${sp.label} respawn time (this session)…`,
          action: () => setActionDialog({
            title: `Respawn time for ${sp.label}`,
            description: 'Applies until the server restarts. To keep it, set Respawn Time Override on the spawn point in Unreal (or respawnMs on the NPC template in the NPC editor). 0 = use the template.',
            submitLabel: 'Apply',
            fields: [{ key: 'seconds', label: 'Seconds', type: 'number', default: sp.respawnOverride ? sp.respawnSeconds : 0, min: 0 }],
            onSubmit: v => postAdmin('spawn-point-action', { spawnPointId: sp.id, action: 'set-respawn-seconds', seconds: Number(v.seconds) || 0 }, { label: `respawn time ${sp.label}` }),
          }),
        });
        break;
      }
    }

    // Check if clicking on an NPC
    for (const n of zoneData.npcs) {
      const dx = world.x - n.x;
      const dy = world.y - n.y;
      if (dx * dx + dy * dy < hitRadius * hitRadius) {
        if (n.alive) {
          menuItems.push({
            label: `Kill ${n.name}`,
            danger: true,
            action: async () => {
              await postAdmin('kill-npc', { npcId: n.id });
              setContextMenu(null);
            },
          });
        } else {
          menuItems.push({
            label: `Respawn ${n.name}`,
            action: async () => {
              await postAdmin('respawn-npc', { npcId: n.id });
              setContextMenu(null);
            },
          });
        }
        menuItems.push({
          label: `Delete ${n.name} (no respawn)`,
          danger: true,
          action: async () => {
            await postAdmin('delete-npc', { npcId: n.id });
            setContextMenu(null);
          },
        });
        break;
      }
    }

    // Always offer ground actions
    menuItems.push({
      label: 'Spawn NPC here...',
      action: () => {
        setSpawnDialog({ type: 'npc', worldX: Math.round(world.x), worldY: Math.round(world.y) });
        setContextMenu(null);
      },
    });
    menuItems.push({
      label: 'Drop item here...',
      action: () => {
        setSpawnDialog({ type: 'item', worldX: Math.round(world.x), worldY: Math.round(world.y) });
        setContextMenu(null);
      },
    });

    setContextMenu({
      x: e.clientX,
      y: e.clientY,
      worldX: Math.round(world.x),
      worldY: Math.round(world.y),
      items: menuItems,
    });
  }, [state, selectedZone, screenToWorld, playerMenuFor]);

  // Context menu is dismissed via a backdrop div rendered behind it (see JSX below).

  // ── Zone list ─────────────────────────────────────────

  // The 2.0 server reports every zone it has (empty ones included), and it is
  // the only authority on which zones exist: zones.json still lists 1.0 zones
  // with no Unreal level, and anything spawned there is rejected. Fall back to
  // zones.json only while the server is unreachable.
  const allZoneIds = new Set<string>();
  if (state?.online && state.zones) {
    for (const zId of Object.keys(state.zones)) allZoneIds.add(zId);
  } else if (zones) {
    for (const zId of Object.keys(zones)) allZoneIds.add(zId);
  }
  const zoneList = Array.from(allZoneIds).sort();
  /** Zones with a capture are Valhalla 2.0 zones: every coordinate is zone-local cm. */
  const unitLabel = selectedZone && thumbMetaCache.current[selectedZone] ? 'cm' : 'px';

  // ── Render ──────────────────────────────────────────────

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', overflow: 'hidden' }}>
      {/* Top bar: status + zone tabs */}
      <div style={{
        display: 'flex', alignItems: 'center', gap: 12,
        padding: '8px 12px',
        background: 'var(--bg-secondary)',
        borderBottom: '1px solid var(--border-color)',
        flexShrink: 0,
      }}>
        {/* Server status */}
        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
          <div style={{
            width: 8, height: 8, borderRadius: '50%',
            background: state?.online ? 'var(--success)' : 'var(--danger)',
          }} />
          <span style={{ color: state?.online ? 'var(--text-secondary)' : 'var(--danger)', fontSize: 12 }}>
            {state?.online ? 'Server Online' : 'Server Offline'}
          </span>
          <span
            style={{ fontSize: 11, color: 'var(--text-muted)', fontFamily: 'monospace' }}
            title="Valhalla 2.0 admin API (VALHALLA_ADMIN_URL on the editor server)"
          >
            {adminUrl || '…'}
          </span>
          {!state?.online && (
            <span style={{ fontSize: 11, color: 'var(--danger)' }}>
              — no response from {adminUrl || 'the admin API'}; start the UE dedicated server build with the admin API enabled
            </span>
          )}
        </div>

        <div style={{ width: 1, height: 20, background: 'var(--border-color)' }} />

        {/* Stat counts */}
        {state?.online && (
          <div style={{ display: 'flex', gap: 12, fontSize: 11, color: 'var(--text-muted)' }}>
            <span style={{ color: PLAYER_COLOR }}>
              {state.totalPlayers} player{state.totalPlayers !== 1 ? 's' : ''}
            </span>
            <span style={{ color: NPC_ENEMY_COLOR }}>
              {state.totalNpcs} NPC{state.totalNpcs !== 1 ? 's' : ''}
            </span>
            <span style={{ color: LOOT_COLOR }}>
              {state.totalLootBags} bag{state.totalLootBags !== 1 ? 's' : ''}
            </span>
          </div>
        )}

        <div style={{ flex: 1 }} />

        {/* Reload the UE server's JSON data (items, npc templates, …) */}
        {reloadStatus && (
          <span style={{ fontSize: 11, color: 'var(--text-muted)' }}>{reloadStatus}</span>
        )}
        <button
          className="btn btn-ghost"
          style={{ padding: '3px 10px', fontSize: 12 }}
          title={`POST ${adminUrl || 'admin API'}/api/admin/reload-data`}
          onClick={async () => {
            setReloadStatus('reloading…');
            try {
              const res = await fetch('/api/admin/reload-data', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: '{}',
              });
              const body = await res.json().catch(() => ({}));
              setReloadStatus(res.ok ? '✓ data reloaded' : `✗ ${body.error || `HTTP ${res.status}`}`);
            } catch (err: any) {
              setReloadStatus(`✗ ${err.message || 'request failed'}`);
            }
            setTimeout(() => setReloadStatus(null), 6000);
          }}
        >
          ⟳ Reload data
        </button>
        <button
          className="btn btn-ghost"
          style={{ padding: '3px 10px', fontSize: 12 }}
          title="Send a system message to every player, or to one zone"
          disabled={!state?.online}
          onClick={() => setActionDialog({
            title: 'Broadcast a message',
            submitLabel: 'Send',
            fields: [
              { key: 'text', label: 'Message (shown as [Admin])', type: 'textarea' },
              { key: 'zoneId', label: 'To', type: 'select', options: [
                { value: '', label: 'Everyone' },
                ...Object.keys(state?.zones ?? {}).sort().map(z => ({ value: z, label: `Zone: ${(zones as any)?.[z]?.name || z}` })),
              ] },
            ],
            onSubmit: v => postAdmin('broadcast', { text: v.text, ...(v.zoneId ? { zoneId: v.zoneId } : {}) }),
          })}
        >
          Broadcast…
        </button>
        <button
          className="btn btn-ghost"
          style={{ padding: '3px 10px', fontSize: 12 }}
          title="List, add and lift account bans"
          disabled={!state?.online}
          onClick={() => setBansOpen(true)}
        >
          Bans…
        </button>

        {/* Zone tabs */}
        <div style={{ display: 'flex', gap: 2 }}>
          {zoneList.map(zoneId => {
            const zd = state?.zones?.[zoneId];
            const playerCount = zd?.players.length ?? 0;
            const isActive = selectedZone === zoneId;
            const displayName = (zones as any)?.[zoneId]?.name || zoneId;
            return (
              <button
                key={zoneId}
                onClick={() => setSelectedZone(zoneId)}
                style={{
                  padding: '4px 12px',
                  border: `1px solid ${isActive ? 'var(--accent)' : 'var(--border-color)'}`,
                  borderRadius: 4,
                  background: isActive ? 'var(--accent-dim)' : 'var(--bg-tertiary)',
                  color: isActive ? 'var(--text-primary)' : 'var(--text-secondary)',
                  cursor: 'pointer',
                  fontSize: 12,
                  display: 'flex', alignItems: 'center', gap: 6,
                }}
              >
                {displayName}
                {playerCount > 0 && (
                  <span style={{
                    background: PLAYER_COLOR,
                    color: '#000',
                    borderRadius: 8,
                    padding: '0 5px',
                    fontSize: 10,
                    fontWeight: 700,
                  }}>
                    {playerCount}
                  </span>
                )}
              </button>
            );
          })}
        </div>
      </div>

      {/* Canvas area + player list */}
      <div style={{ flex: 1, display: 'flex', minHeight: 0 }}>
      <div
        ref={containerRef}
        style={{ flex: 1, position: 'relative', overflow: 'hidden' }}
      >
        <canvas
          ref={canvasRef}
          onWheel={handleWheel}
          onMouseDown={handleMouseDown}
          onMouseMove={handleMouseMove}
          onMouseUp={handleMouseUp}
          onMouseLeave={(e) => { handleMouseUp(e); cursorWorldRef.current = { x: 0, y: 0 }; setHoveredEntity(null); setTooltip(null); }}
          onContextMenu={handleContextMenu}
          style={{ display: 'block', width: '100%', height: '100%' }}
        />

        {/* Admin action result */}
        {notice && (
          <div
            onClick={() => setNotice(null)}
            style={{
              position: 'absolute', top: 10, left: '50%', transform: 'translateX(-50%)',
              zIndex: 20, cursor: 'pointer',
              background: notice.ok ? 'rgba(20, 60, 30, 0.95)' : 'rgba(80, 20, 20, 0.95)',
              border: `1px solid ${notice.ok ? 'var(--success)' : 'var(--danger)'}`,
              borderRadius: 4, padding: '6px 12px', fontSize: 12,
              color: 'var(--text-primary)', maxWidth: '70%',
            }}
          >
            {notice.ok ? '✓' : '✗'} {notice.action}: {notice.message}
          </div>
        )}

        {/* Tooltip */}
        {tooltip && (
          <div style={{
            position: 'fixed',
            left: tooltip.x + 14,
            top: tooltip.y + 14,
            background: 'rgba(16, 16, 32, 0.95)',
            border: '1px solid var(--border-color)',
            borderRadius: 4,
            padding: '6px 10px',
            fontSize: 11,
            color: 'var(--text-primary)',
            whiteSpace: 'pre-line',
            pointerEvents: 'none',
            zIndex: 100,
          }}>
            {tooltip.text}
          </div>
        )}

        {/* Context menu — backdrop closes on outside click, menu itself captures clicks */}
        {contextMenu && (
          <>
            {/* Invisible full-screen backdrop: clicking it closes the menu */}
            <div
              style={{ position: 'fixed', inset: 0, zIndex: 199 }}
              onMouseDown={() => setContextMenu(null)}
              onContextMenu={(e) => { e.preventDefault(); setContextMenu(null); }}
            />
            {/* The menu floats above the backdrop */}
            <div
              style={{
                position: 'fixed',
                left: contextMenu.x,
                top: contextMenu.y,
                background: 'var(--bg-secondary)',
                border: '1px solid var(--border-color)',
                borderRadius: 6,
                padding: 4,
                minWidth: 180,
                zIndex: 200,
                boxShadow: '0 4px 16px rgba(0,0,0,0.5)',
              }}
            >
              <div style={{ padding: '4px 10px', fontSize: 10, color: 'var(--text-muted)', borderBottom: '1px solid var(--border-color)', marginBottom: 2 }}>
                ({contextMenu.worldX}, {contextMenu.worldY}) {unitLabel}
              </div>
              {contextMenu.items.map((item, i) => (
                <div
                  key={i}
                  onClick={() => { setContextMenu(null); item.action(); }}
                  style={{
                    padding: '6px 10px',
                    cursor: 'pointer',
                    fontSize: 12,
                    borderRadius: 3,
                    color: item.danger ? 'var(--danger)' : 'var(--text-primary)',
                  }}
                  onMouseEnter={(e) => (e.currentTarget.style.background = 'var(--bg-tertiary)')}
                  onMouseLeave={(e) => (e.currentTarget.style.background = 'transparent')}
                >
                  {item.label}
                </div>
              ))}
            </div>
          </>
        )}
      </div>

      {state?.online && (
        <PlayersPanel
          zones={state.zones}
          zoneNames={Object.fromEntries(Object.keys(state.zones).map(z => [z, (zones as any)?.[z]?.name || z]))}
          selectedSessionId={selectedPlayerId}
          onSelect={(zoneId, p) => {
            setSelectedPlayerId(p.sessionId);
            setSelectedZone(zoneId);
            // Centre the map on them.
            const container = containerRef.current;
            if (container) {
              panRef.current = {
                x: container.clientWidth / 2 - p.x * zoomRef.current,
                y: container.clientHeight / 2 - p.y * zoomRef.current,
              };
            }
          }}
          onMenu={openPlayerMenuAt}
        />
      )}
      </div>

      {/* Generic admin action dialog */}
      {actionDialog && <ActionDialog spec={actionDialog} onClose={() => setActionDialog(null)} />}

      {/* Player inspect window */}
      {inspectPlayer && <InspectDialog player={inspectPlayer} onClose={() => setInspectPlayer(null)} />}

      {/* Account bans */}
      {bansOpen && <BansDialog onClose={() => setBansOpen(false)} />}

      {/* Spawn NPC / Drop Item dialog */}
      {spawnDialog && (
        <SpawnDialog
          type={spawnDialog.type}
          worldX={spawnDialog.worldX}
          worldY={spawnDialog.worldY}
          zoneId={selectedZone || ''}
          units={unitLabel}
          npcTemplates={npcTemplates}
          items={items}
          onClose={() => setSpawnDialog(null)}
        />
      )}

      {/* Teleport dialog */}
      {teleportDialog && (
        <TeleportDialog
          sessionId={teleportDialog.sessionId}
          playerName={teleportDialog.name}
          zones={zones}
          onClose={() => setTeleportDialog(null)}
        />
      )}
    </div>
  );
};

// ── Spawn Dialog ──────────────────────────────────────────

const SpawnDialog: React.FC<{
  type: 'npc' | 'item';
  worldX: number;
  worldY: number;
  zoneId: string;
  /** 'cm' for Valhalla 2.0 zones (what the UE admin API expects), 'px' for 1.0. */
  units: string;
  npcTemplates: Record<string, any>;
  items: Record<string, any>;
  onClose: () => void;
}> = ({ type, worldX, worldY, zoneId, units, npcTemplates, items, onClose }) => {
  const [selectedId, setSelectedId] = useState('');
  const [quantity, setQuantity] = useState(1);
  const [filter, setFilter] = useState('');

  const list = type === 'npc'
    ? Object.entries(npcTemplates).map(([id, t]) => ({ id, name: (t as any).name || id }))
    : Object.entries(items).map(([id, t]) => ({ id, name: (t as any).name || id }));

  const filtered = filter
    ? list.filter(l => l.name.toLowerCase().includes(filter.toLowerCase()) || l.id.toLowerCase().includes(filter.toLowerCase()))
    : list;

  const handleSubmit = async () => {
    if (!selectedId) return;
    if (type === 'npc') {
      await postAdmin('spawn-npc', { templateId: selectedId, zoneId, x: worldX, y: worldY });
    } else {
      await postAdmin('drop-item', { itemId: selectedId, zoneId, x: worldX, y: worldY, quantity });
    }
    onClose();
  };

  return (
    <div style={{
      position: 'fixed', inset: 0, background: 'rgba(0,0,0,0.6)',
      display: 'flex', alignItems: 'center', justifyContent: 'center', zIndex: 300,
    }} onClick={onClose}>
      <div
        style={{
          background: 'var(--bg-secondary)', border: '1px solid var(--border-color)',
          borderRadius: 8, padding: 20, width: 400, maxHeight: 500,
          display: 'flex', flexDirection: 'column', gap: 12,
        }}
        onClick={(e) => e.stopPropagation()}
      >
        <h3 style={{ margin: 0, fontSize: 14, color: 'var(--text-primary)' }}>
          {type === 'npc' ? 'Spawn NPC' : 'Drop Item'} at ({worldX}, {worldY}) {units}
        </h3>

        <input
          className="form-input"
          placeholder={`Search ${type === 'npc' ? 'NPC templates' : 'items'}...`}
          value={filter}
          onChange={(e) => setFilter(e.target.value)}
          autoFocus
        />

        <div style={{
          flex: 1, overflow: 'auto', border: '1px solid var(--border-color)',
          borderRadius: 4, maxHeight: 250,
        }}>
          {filtered.map(item => (
            <div
              key={item.id}
              onClick={() => setSelectedId(item.id)}
              style={{
                padding: '6px 10px', cursor: 'pointer', fontSize: 12,
                background: selectedId === item.id ? 'var(--accent-dim)' : 'transparent',
                color: selectedId === item.id ? 'var(--text-primary)' : 'var(--text-secondary)',
                borderBottom: '1px solid rgba(51,51,85,0.2)',
              }}
              onMouseEnter={(e) => { if (selectedId !== item.id) e.currentTarget.style.background = 'var(--bg-tertiary)'; }}
              onMouseLeave={(e) => { if (selectedId !== item.id) e.currentTarget.style.background = 'transparent'; }}
            >
              <div style={{ fontWeight: 500 }}>{item.name}</div>
              <div style={{ fontSize: 10, color: 'var(--text-muted)' }}>{item.id}</div>
            </div>
          ))}
          {filtered.length === 0 && (
            <div style={{ padding: 16, textAlign: 'center', color: 'var(--text-muted)', fontSize: 12 }}>
              No results
            </div>
          )}
        </div>

        {type === 'item' && (
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <label style={{ fontSize: 11, color: 'var(--text-secondary)' }}>Qty:</label>
            <input
              className="form-input"
              type="number"
              min={1}
              max={99}
              value={quantity}
              onChange={(e) => setQuantity(parseInt(e.target.value) || 1)}
              style={{ width: 60 }}
            />
          </div>
        )}

        <div style={{ display: 'flex', justifyContent: 'flex-end', gap: 8 }}>
          <button className="btn btn-ghost" onClick={onClose}>Cancel</button>
          <button
            className="btn btn-primary"
            onClick={handleSubmit}
            disabled={!selectedId}
          >
            {type === 'npc' ? 'Spawn' : 'Drop'}
          </button>
        </div>
      </div>
    </div>
  );
};

// ── Teleport Dialog ───────────────────────────────────────

const TeleportDialog: React.FC<{
  sessionId: string;
  playerName: string;
  zones: Record<string, any>;
  onClose: () => void;
}> = ({ sessionId, playerName, zones, onClose }) => {
  const [targetZone, setTargetZone] = useState('');
  const [x, setX] = useState(0);
  const [y, setY] = useState(0);
  const [units, setUnits] = useState<'cm' | 'px'>('px');

  // Default to the target zone's first overlay 2.0 player spawn (cm); fall back
  // to the 1.0 zones.json defaultSpawn (world pixels) when the zone has none.
  useEffect(() => {
    if (!targetZone) return;
    let cancelled = false;
    (async () => {
      try {
        const res = await fetch(`/api/overlays2/${targetZone}`);
        if (res.ok) {
          const ov = await res.json();
          const spawn = (ov.spawnPoints || []).find((sp: any) => sp.type === 'player_spawn');
          if (spawn && !cancelled) {
            setX(Math.round(spawn.x));
            setY(Math.round(spawn.y));
            setUnits('cm');
            return;
          }
        }
      } catch { /* fall through to the 1.0 default */ }
      const fallback = (zones as any)?.[targetZone]?.defaultSpawn;
      if (fallback && !cancelled) {
        setX(fallback.x);
        setY(fallback.y);
        setUnits('px');
      }
    })();
    return () => { cancelled = true; };
  }, [targetZone, zones]);

  const handleSubmit = async () => {
    if (!targetZone) return;
    await postAdmin('teleport-player', { sessionId, zoneId: targetZone, x, y });
    onClose();
  };

  const zoneList = Object.entries(zones || {}).map(([id, z]) => ({
    id,
    name: (z as any).name || id,
  }));

  return (
    <div style={{
      position: 'fixed', inset: 0, background: 'rgba(0,0,0,0.6)',
      display: 'flex', alignItems: 'center', justifyContent: 'center', zIndex: 300,
    }} onClick={onClose}>
      <div
        style={{
          background: 'var(--bg-secondary)', border: '1px solid var(--border-color)',
          borderRadius: 8, padding: 20, width: 340,
          display: 'flex', flexDirection: 'column', gap: 12,
        }}
        onClick={(e) => e.stopPropagation()}
      >
        <h3 style={{ margin: 0, fontSize: 14, color: 'var(--text-primary)' }}>
          Teleport {playerName}
        </h3>

        <div className="form-group">
          <label className="form-label">Target Zone</label>
          <select
            className="form-select"
            value={targetZone}
            onChange={(e) => setTargetZone(e.target.value)}
          >
            <option value="">Select zone...</option>
            {zoneList.map(z => (
              <option key={z.id} value={z.id}>{z.name}</option>
            ))}
          </select>
        </div>

        <div className="form-row">
          <div className="form-group">
            <label className="form-label">X ({units})</label>
            <input
              className="form-input"
              type="number"
              value={x}
              onChange={(e) => setX(parseInt(e.target.value) || 0)}
            />
          </div>
          <div className="form-group">
            <label className="form-label">Y ({units})</label>
            <input
              className="form-input"
              type="number"
              value={y}
              onChange={(e) => setY(parseInt(e.target.value) || 0)}
            />
          </div>
        </div>

        <div style={{ display: 'flex', justifyContent: 'flex-end', gap: 8 }}>
          <button className="btn btn-ghost" onClick={onClose}>Cancel</button>
          <button
            className="btn btn-primary"
            onClick={handleSubmit}
            disabled={!targetZone}
          >
            Teleport
          </button>
        </div>
      </div>
    </div>
  );
};
