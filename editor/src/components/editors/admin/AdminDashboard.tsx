/**
 * AdminDashboard — Live server monitoring & control panel.
 *
 * Shows a visual overview of each zone with real-time player/NPC/loot bag
 * positions, and provides right-click context menus for admin actions
 * (spawn NPC, drop item, teleport player, kick, kill/respawn NPC).
 */
import React, { useEffect, useRef, useState, useCallback } from 'react';
import { useEditorStore } from '../../../store/editorStore';

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
  type: 'player_spawn' | 'enemy_spawn' | 'portal' | 'zone_entry';
  x: number;
  y: number;
  label?: string;
  templateId?: string;
  width?: number;
  height?: number;
  fromZone?: string;
}

interface OverlayData {
  spawnPoints: OverlaySpawnPoint[];
}

interface ZoneData {
  players: PlayerInfo[];
  npcs: NPCInfo[];
  lootBags: LootBagInfo[];
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

async function postAdmin(action: string, body: any): Promise<any> {
  const res = await fetch(`/api/admin/${action}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
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

  // ── Overlay loading ─────────────────────────────────────

  useEffect(() => {
    if (!selectedZone) return;
    if (overlayCache.current[selectedZone]) return; // already loaded
    fetch(`/api/overlays/${selectedZone}-overlay.json`)
      .then(r => r.ok ? r.json() : null)
      .then((data: OverlayData | null) => {
        if (data) overlayCache.current[selectedZone] = data;
      })
      .catch(() => {}); // silently fail if file not found
  }, [selectedZone]);

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

    if (!state || !selectedZone || !state.zones[selectedZone]) {
      // Draw "no data" message
      ctx.fillStyle = '#666';
      ctx.font = '16px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText(
        state?.online === false ? 'Game server offline' : 'No zone data available',
        w / 2, h / 2,
      );
      return;
    }

    const zoneData = state.zones[selectedZone];

    // ── Grid ──
    ctx.strokeStyle = GRID_COLOR;
    ctx.lineWidth = 1;

    const gridStep = GRID_SIZE * zoom;
    const startX = pan.x % gridStep;
    const startY = pan.y % gridStep;

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

        if (sp.type === 'portal') {
          const sw = (sp.width ?? 128) * zoom;
          const sh = (sp.height ?? 128) * zoom;
          ctx.fillStyle = PORTAL_COLOR;
          ctx.fillRect(s.x, s.y, sw, sh);
          ctx.strokeStyle = PORTAL_STROKE;
          ctx.lineWidth = 1.5;
          ctx.setLineDash([5, 3]);
          ctx.strokeRect(s.x, s.y, sw, sh);
          ctx.setLineDash([]);
          if (zoom > 0.15 && sp.label) {
            ctx.fillStyle = 'rgba(180, 100, 255, 0.9)';
            ctx.font = `bold ${Math.max(9, 11 * zoom)}px sans-serif`;
            ctx.textAlign = 'center';
            ctx.fillText(`⬦ ${sp.label}`, s.x + sw / 2, s.y + sh / 2 + 4);
          }

        } else if (sp.type === 'zone_entry') {
          const sw = (sp.width ?? 64) * zoom;
          const sh = (sp.height ?? 64) * zoom;
          ctx.fillStyle = ZONE_ENTRY_FILL;
          ctx.fillRect(s.x, s.y, sw, sh);
          ctx.strokeStyle = ZONE_ENTRY_STROKE;
          ctx.lineWidth = 1.5;
          ctx.setLineDash([5, 3]);
          ctx.strokeRect(s.x, s.y, sw, sh);
          ctx.setLineDash([]);
          if (zoom > 0.2 && sp.label) {
            ctx.fillStyle = 'rgba(80, 200, 255, 0.9)';
            ctx.font = `${Math.max(8, 10 * zoom)}px sans-serif`;
            ctx.textAlign = 'center';
            ctx.fillText(`↓ ${sp.label}`, s.x + sw / 2, s.y + sh / 2 + 4);
          }

        } else if (sp.type === 'player_spawn' || sp.type === 'enemy_spawn') {
          const color = sp.type === 'player_spawn' ? SPAWN_PLAYER_COLOR : SPAWN_ENEMY_COLOR;
          const size = Math.max(5, 7 * zoom);
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
    drawLegendDot(SPAWN_ENEMY_COLOR, 'Enemy Spawn');
    drawLegendDot('rgba(160, 80, 255, 0.8)', 'Portal');
    drawLegendDot('rgba(80, 200, 255, 0.8)', 'Zone Entry');

    // ── Bottom-right status: cursor world coords + zoom ──
    const cw = cursorWorldRef.current;
    ctx.fillStyle = '#555';
    ctx.font = '10px monospace';
    ctx.textAlign = 'right';
    ctx.fillText(
      `(${Math.round(cw.x)}, ${Math.round(cw.y)})   Zoom: ${(zoom * 100).toFixed(0)}%`,
      w - 12, h - 12,
    );
  }, [state, selectedZone, hoveredEntity, worldToScreen]);

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

    // Check overlay spawn points / portals
    if (!found) {
      const overlay = overlayCache.current[selectedZone];
      if (overlay?.spawnPoints) {
        for (const sp of overlay.spawnPoints) {
          if (sp.type === 'portal' || sp.type === 'zone_entry') {
            const w = sp.width ?? 128;
            const h = sp.height ?? 64;
            if (world.x >= sp.x && world.x <= sp.x + w && world.y >= sp.y && world.y <= sp.y + h) {
              found = `overlay_${sp.id}`;
              const typeLabel = sp.type === 'portal' ? 'Portal' : 'Zone Entry';
              ttText = `${typeLabel}: ${sp.label ?? sp.templateId ?? '?'}\nPos: ${sp.x}, ${sp.y}  Size: ${w}×${h}`;
              break;
            }
          } else {
            const dx = world.x - sp.x;
            const dy = world.y - sp.y;
            if (dx * dx + dy * dy < hitRadius * hitRadius) {
              found = `overlay_${sp.id}`;
              const typeLabel = sp.type === 'player_spawn' ? 'Player Spawn' : 'Enemy Spawn';
              ttText = `${typeLabel}${sp.label ? ': ' + sp.label : ''}\nPos: ${sp.x}, ${sp.y}`;
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

    // Check if clicking on a player
    for (const p of zoneData.players) {
      const dx = world.x - p.x;
      const dy = world.y - p.y;
      if (dx * dx + dy * dy < hitRadius * hitRadius) {
        menuItems.push({
          label: `Teleport ${p.name}...`,
          action: () => setTeleportDialog({ sessionId: p.sessionId, name: p.name }),
        });
        menuItems.push({
          label: `Kick ${p.name}`,
          danger: true,
          action: async () => {
            await postAdmin('kick-player', { sessionId: p.sessionId });
            setContextMenu(null);
          },
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
  }, [state, selectedZone, screenToWorld]);

  // Context menu is dismissed via a backdrop div rendered behind it (see JSX below).

  // ── Zone list ─────────────────────────────────────────

  const allZoneIds = new Set<string>();
  if (state?.zones) {
    for (const zId of Object.keys(state.zones)) allZoneIds.add(zId);
  }
  // Also include zones from editor data even if empty on server
  if (zones) {
    for (const zId of Object.keys(zones)) allZoneIds.add(zId);
  }
  const zoneList = Array.from(allZoneIds).sort();

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
          <span style={{ color: 'var(--text-secondary)', fontSize: 12 }}>
            {state?.online ? 'Server Online' : 'Server Offline'}
          </span>
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

      {/* Canvas area */}
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
                ({contextMenu.worldX}, {contextMenu.worldY})
              </div>
              {contextMenu.items.map((item, i) => (
                <div
                  key={i}
                  onClick={() => item.action()}
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

      {/* Spawn NPC / Drop Item dialog */}
      {spawnDialog && (
        <SpawnDialog
          type={spawnDialog.type}
          worldX={spawnDialog.worldX}
          worldY={spawnDialog.worldY}
          zoneId={selectedZone || ''}
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
  npcTemplates: Record<string, any>;
  items: Record<string, any>;
  onClose: () => void;
}> = ({ type, worldX, worldY, zoneId, npcTemplates, items, onClose }) => {
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
          {type === 'npc' ? 'Spawn NPC' : 'Drop Item'} at ({worldX}, {worldY})
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

  // Set defaults from zone spawn
  useEffect(() => {
    if (targetZone && (zones as any)?.[targetZone]?.defaultSpawn) {
      const spawn = (zones as any)[targetZone].defaultSpawn;
      setX(spawn.x);
      setY(spawn.y);
    }
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
            <label className="form-label">X</label>
            <input
              className="form-input"
              type="number"
              value={x}
              onChange={(e) => setX(parseInt(e.target.value) || 0)}
            />
          </div>
          <div className="form-group">
            <label className="form-label">Y</label>
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
