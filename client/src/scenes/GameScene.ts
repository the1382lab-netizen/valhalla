import Phaser from 'phaser';
import { NetworkClient, CollisionGridData } from '../systems/NetworkClient.js';
import { InputManager } from '../systems/InputManager.js';
import { EntityRenderer } from '../systems/EntityRenderer.js';
import {
  TILE_SIZE,
  PLAYER_SPEED,
  PLAYER_COLLISION_RADIUS,
  MAP_WIDTH_PX,
  MAP_HEIGHT_PX,
  InputPayload,
  normalise,
} from '@valhalla/shared';

interface PendingInput {
  input: InputPayload;
  dt: number;
}

/**
 * Main gameplay scene.
 * Handles tile map rendering, local player with client-side prediction,
 * remote player interpolation, and mouse aiming.
 */
export class GameScene extends Phaser.Scene {
  private network!: NetworkClient;
  private inputManager!: InputManager;
  private entityRenderer!: EntityRenderer;

  // Local player
  private playerSprite!: Phaser.GameObjects.Sprite;
  private aimLine!: Phaser.GameObjects.Graphics;
  private localX: number = 0;
  private localY: number = 0;

  // Client-side prediction
  private pendingInputs: PendingInput[] = [];
  private lastServerSeq: number = 0;

  // Collision (received from server)
  private collisionGrid: number[] = [];
  private collisionMapW: number = 0;
  private collisionMapH: number = 0;

  // Connection state
  private connected: boolean = false;
  private statusText!: Phaser.GameObjects.Text;

  constructor() {
    super({ key: 'GameScene' });
  }

  create(): void {
    this.inputManager = new InputManager(this);
    this.entityRenderer = new EntityRenderer(this);
    this.network = new NetworkClient();

    // Status text
    this.statusText = this.add.text(16, 16, 'Connecting...', {
      fontSize: '16px',
      color: '#ffcc00',
      stroke: '#000000',
      strokeThickness: 3,
    });
    this.statusText.setScrollFactor(0);
    this.statusText.setDepth(100);

    // Setup network callbacks
    this.network.onCollisionGrid = (data: CollisionGridData) => {
      this.collisionGrid = data.grid;
      this.collisionMapW = data.width;
      this.collisionMapH = data.height;
      this.buildTileMap(data);
    };

    this.network.onPlayerAdd = (player: any, sessionId: string) => {
      if (sessionId === this.network.sessionId) {
        // This is us — set up local player
        this.localX = player.x;
        this.localY = player.y;
        this.createLocalPlayer();
        this.connected = true;
        this.statusText.setText('Connected — WASD to move, mouse to aim');
        this.time.delayedCall(3000, () => this.statusText.setVisible(false));
      } else {
        // Remote player
        this.entityRenderer.addRemotePlayer(sessionId, player.x, player.y);
      }
    };

    this.network.onPlayerChange = (player: any, sessionId: string) => {
      if (sessionId === this.network.sessionId) {
        // Server reconciliation — reapply unacknowledged inputs
        this.reconcile(player.x, player.y, player.inputSeq);
      } else {
        this.entityRenderer.updateRemotePlayerTarget(
          sessionId,
          player.x,
          player.y,
          player.aimAngle,
        );
      }
    };

    this.network.onPlayerRemove = (sessionId: string) => {
      this.entityRenderer.removeRemotePlayer(sessionId);
    };

    // Connect
    this.network.connect().catch((err) => {
      this.statusText.setText('Connection failed — is the server running?');
      console.error(err);
    });

    // Set world bounds
    this.cameras.main.setBounds(0, 0, MAP_WIDTH_PX, MAP_HEIGHT_PX);
  }

  private createLocalPlayer(): void {
    this.playerSprite = this.add.sprite(this.localX, this.localY, 'player');
    this.playerSprite.setDepth(10);

    // Aim indicator line
    this.aimLine = this.add.graphics();
    this.aimLine.setDepth(9);

    // Camera follows player
    this.cameras.main.startFollow(this.playerSprite, true, 0.1, 0.1);
    this.cameras.main.setZoom(1);
  }

  /**
   * Build the visual tilemap from the collision grid data.
   */
  private buildTileMap(data: CollisionGridData): void {
    const { grid, width, height, tileSize } = data;

    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) {
        const isWall = grid[y * width + x] === 1;
        const tile = this.add.sprite(
          x * tileSize + tileSize / 2,
          y * tileSize + tileSize / 2,
          isWall ? 'tile_wall' : 'tile_ground',
        );
        tile.setDepth(isWall ? 1 : 0);
      }
    }
  }

  update(_time: number, delta: number): void {
    if (!this.connected || !this.playerSprite) return;

    const dt = delta / 1000;

    // ── Sample input ────────────────────────────────────────
    const input = this.inputManager.getInput(this.localX, this.localY);

    // ── Client-side prediction ──────────────────────────────
    this.applyInputLocally(input, dt);

    // ── Send to server ──────────────────────────────────────
    this.network.sendInput(input);

    // ── Store for reconciliation ────────────────────────────
    this.pendingInputs.push({ input, dt });

    // ── Update local sprite ─────────────────────────────────
    this.playerSprite.x = this.localX;
    this.playerSprite.y = this.localY;
    this.playerSprite.rotation = input.aimAngle;

    // ── Draw aim line ───────────────────────────────────────
    this.drawAimLine(input.aimAngle);

    // ── Interpolate remote players ──────────────────────────
    this.entityRenderer.update();
  }

  /**
   * Apply input locally for client-side prediction.
   * Mirrors the server's movement logic exactly.
   */
  private applyInputLocally(input: InputPayload, dt: number): void {
    let mx = 0;
    let my = 0;
    if (input.up) my -= 1;
    if (input.down) my += 1;
    if (input.left) mx -= 1;
    if (input.right) mx += 1;

    const dir = normalise(mx, my);
    const dx = dir.x * PLAYER_SPEED * dt;
    const dy = dir.y * PLAYER_SPEED * dt;

    if (dx !== 0 || dy !== 0) {
      const resolved = this.resolveCollisionLocal(this.localX, this.localY, dx, dy);
      this.localX = resolved.x;
      this.localY = resolved.y;
    }
  }

  /**
   * Client-side collision resolution (mirrors server logic).
   */
  private resolveCollisionLocal(
    x: number,
    y: number,
    dx: number,
    dy: number,
  ): { x: number; y: number } {
    const radius = PLAYER_COLLISION_RADIUS;

    const isBlocked = (cx: number, cy: number): boolean => {
      const minTX = Math.floor((cx - radius) / TILE_SIZE);
      const maxTX = Math.floor((cx + radius) / TILE_SIZE);
      const minTY = Math.floor((cy - radius) / TILE_SIZE);
      const maxTY = Math.floor((cy + radius) / TILE_SIZE);

      for (let ty = minTY; ty <= maxTY; ty++) {
        for (let tx = minTX; tx <= maxTX; tx++) {
          if (tx < 0 || tx >= this.collisionMapW || ty < 0 || ty >= this.collisionMapH) return true;
          if (this.collisionGrid[ty * this.collisionMapW + tx] !== 1) continue;

          const tileLeft = tx * TILE_SIZE;
          const tileTop = ty * TILE_SIZE;
          const closestX = Math.max(tileLeft, Math.min(cx, tileLeft + TILE_SIZE));
          const closestY = Math.max(tileTop, Math.min(cy, tileTop + TILE_SIZE));
          const ddx = cx - closestX;
          const ddy = cy - closestY;
          if (ddx * ddx + ddy * ddy < radius * radius) return true;
        }
      }
      return false;
    };

    // Try full movement
    let newX = x + dx;
    let newY = y + dy;
    if (!isBlocked(newX, newY)) return { x: newX, y: newY };

    // Slide along axes
    if (!isBlocked(x + dx, y)) return { x: x + dx, y };
    if (!isBlocked(x, y + dy)) return { x, y: y + dy };

    return { x, y };
  }

  /**
   * Server reconciliation: when we get authoritative state, reapply
   * any inputs the server hasn't processed yet.
   */
  private reconcile(serverX: number, serverY: number, serverSeq: number): void {
    // Drop all inputs that have been acknowledged by the server
    this.pendingInputs = this.pendingInputs.filter((p) => p.input.seq > serverSeq);

    // Start from authoritative position
    this.localX = serverX;
    this.localY = serverY;

    // Re-apply unacknowledged inputs
    for (const pending of this.pendingInputs) {
      this.applyInputLocally(pending.input, pending.dt);
    }
  }

  /**
   * Draw a short aim line from the player in the aim direction.
   */
  private drawAimLine(angle: number): void {
    this.aimLine.clear();
    this.aimLine.lineStyle(2, 0xff4444, 0.7);

    const len = 40;
    const startX = this.localX + Math.cos(angle) * 20;
    const startY = this.localY + Math.sin(angle) * 20;
    const endX = this.localX + Math.cos(angle) * (20 + len);
    const endY = this.localY + Math.sin(angle) * (20 + len);

    this.aimLine.beginPath();
    this.aimLine.moveTo(startX, startY);
    this.aimLine.lineTo(endX, endY);
    this.aimLine.strokePath();
  }
}
