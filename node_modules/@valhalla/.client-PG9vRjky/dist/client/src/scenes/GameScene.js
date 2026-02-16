import Phaser from 'phaser';
import { NetworkClient, } from '../systems/NetworkClient.js';
import { InputManager } from '../systems/InputManager.js';
import { EntityRenderer } from '../systems/EntityRenderer.js';
import { FogOfWar } from '../systems/FogOfWar.js';
import { TILE_SIZE, PLAYER_SPEED, PLAYER_COLLISION_RADIUS, PLAYER_MAX_HP, MAP_WIDTH_PX, MAP_HEIGHT_PX, RESPAWN_TIME_MS, normalise, } from '@valhalla/shared';
/**
 * Main gameplay scene.
 * Handles tile map rendering, local player with client-side prediction,
 * remote player interpolation, projectile rendering, HP, death/respawn,
 * fog of war, and visibility-based entity filtering.
 */
export class GameScene extends Phaser.Scene {
    network;
    inputManager;
    entityRenderer;
    fogOfWar;
    // Local player
    playerSprite;
    aimLine;
    localX = 0;
    localY = 0;
    // Local player HP
    localHp = PLAYER_MAX_HP;
    localMaxHp = PLAYER_MAX_HP;
    localAlive = true;
    hpBarGfx;
    // Death overlay
    deathOverlay;
    deathText;
    respawnTimer = 0;
    // Client-side prediction
    pendingInputs = [];
    lastServerSeq = 0;
    // Collision (received from server)
    collisionGrid = [];
    collisionMapW = 0;
    collisionMapH = 0;
    // Connection state
    connected = false;
    statusText;
    constructor() {
        super({ key: 'GameScene' });
    }
    create() {
        this.inputManager = new InputManager(this);
        this.entityRenderer = new EntityRenderer(this);
        this.fogOfWar = new FogOfWar(this);
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
        // Death overlay (hidden initially)
        this.deathOverlay = this.add.rectangle(this.cameras.main.width / 2, this.cameras.main.height / 2, this.cameras.main.width, this.cameras.main.height, 0x000000, 0.7);
        this.deathOverlay.setScrollFactor(0);
        this.deathOverlay.setDepth(200);
        this.deathOverlay.setVisible(false);
        this.deathText = this.add.text(this.cameras.main.width / 2, this.cameras.main.height / 2, 'YOU DIED\nRespawning...', {
            fontSize: '32px',
            color: '#ff4444',
            stroke: '#000000',
            strokeThickness: 4,
            align: 'center',
        });
        this.deathText.setOrigin(0.5, 0.5);
        this.deathText.setScrollFactor(0);
        this.deathText.setDepth(201);
        this.deathText.setVisible(false);
        // Local HP bar (HUD)
        this.hpBarGfx = this.add.graphics();
        this.hpBarGfx.setScrollFactor(0);
        this.hpBarGfx.setDepth(100);
        // Setup network callbacks
        this.setupNetworkCallbacks();
        // Connect
        this.network.connect().catch((err) => {
            this.statusText.setText('Connection failed — is the server running?');
            console.error(err);
        });
        // Set world bounds
        this.cameras.main.setBounds(0, 0, MAP_WIDTH_PX, MAP_HEIGHT_PX);
    }
    setupNetworkCallbacks() {
        this.network.onCollisionGrid = (data) => {
            this.collisionGrid = data.grid;
            this.collisionMapW = data.width;
            this.collisionMapH = data.height;
            this.buildTileMap(data);
        };
        // Visibility updates from server
        this.network.onVisibility = (data) => {
            this.fogOfWar.updateVisibility(data);
            // Show/hide remote entities based on visibility
            this.entityRenderer.applyVisibility(new Set(data.visiblePlayers), new Set(data.visibleProjectiles));
        };
        this.network.onPlayerAdd = (player, sessionId) => {
            if (sessionId === this.network.sessionId) {
                // This is us — set up local player
                this.localX = player.x;
                this.localY = player.y;
                this.localHp = player.hp;
                this.localMaxHp = player.maxHp;
                this.localAlive = player.alive;
                this.createLocalPlayer();
                this.connected = true;
                this.statusText.setText('WASD move | Mouse aim | Left-click shoot | Space melee');
                this.time.delayedCall(4000, () => this.statusText.setVisible(false));
            }
            else {
                // Remote player — start hidden until visibility confirms they're visible
                this.entityRenderer.addRemotePlayer(sessionId, player.x, player.y);
            }
        };
        this.network.onPlayerChange = (player, sessionId) => {
            if (sessionId === this.network.sessionId) {
                // Update local HP state from server
                this.localHp = player.hp;
                this.localMaxHp = player.maxHp;
                const wasAlive = this.localAlive;
                this.localAlive = player.alive;
                // Handle death transition
                if (wasAlive && !this.localAlive) {
                    this.showDeathScreen();
                }
                else if (!wasAlive && this.localAlive) {
                    this.hideDeathScreen();
                }
                // Server reconciliation — reapply unacknowledged inputs
                this.reconcile(player.x, player.y, player.inputSeq);
            }
            else {
                this.entityRenderer.updateRemotePlayerTarget(sessionId, player.x, player.y, player.aimAngle);
                this.entityRenderer.updateRemotePlayerHp(sessionId, player.hp, player.maxHp, player.alive);
            }
        };
        this.network.onPlayerRemove = (sessionId) => {
            this.entityRenderer.removeRemotePlayer(sessionId);
        };
        // Projectile callbacks
        this.network.onProjectileAdd = (proj, id) => {
            this.entityRenderer.addProjectile(id, proj.x, proj.y);
        };
        this.network.onProjectileRemove = (id) => {
            this.entityRenderer.removeProjectile(id);
        };
        this.network.onProjectileChange = (proj, id) => {
            this.entityRenderer.updateProjectileTarget(id, proj.x, proj.y);
        };
        // Combat events
        this.network.onPlayerHit = (data) => {
            if (data.targetId === this.network.sessionId) {
                // We got hit
                this.entityRenderer.showDamageFlash(this.localX, this.localY, data.damage);
                this.cameras.main.shake(100, 0.005);
            }
            else {
                // Show damage on remote player if visible
                const pos = this.entityRenderer.getRemotePlayerPosition(data.targetId);
                if (pos) {
                    this.entityRenderer.showDamageFlash(pos.x, pos.y, data.damage);
                }
            }
        };
        this.network.onPlayerDied = (data) => {
            if (data.targetId !== this.network.sessionId) {
                console.log(`[Combat] Player ${data.targetId.slice(0, 6)} was killed by ${data.killerId.slice(0, 6)}`);
            }
        };
        this.network.onPlayerRespawned = (data) => {
            if (data.playerId === this.network.sessionId) {
                console.log('[Combat] You respawned!');
            }
        };
        this.network.onMeleeAttack = (data) => {
            if (data.attackerId === this.network.sessionId) {
                this.entityRenderer.showMeleeSlash(this.localX, this.localY, data.angle);
            }
            else {
                const pos = this.entityRenderer.getRemotePlayerPosition(data.attackerId);
                if (pos) {
                    this.entityRenderer.showMeleeSlash(pos.x, pos.y, data.angle);
                }
            }
        };
    }
    createLocalPlayer() {
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
    buildTileMap(data) {
        const { grid, width, height, tileSize } = data;
        for (let y = 0; y < height; y++) {
            for (let x = 0; x < width; x++) {
                const isWall = grid[y * width + x] === 1;
                const tile = this.add.sprite(x * tileSize + tileSize / 2, y * tileSize + tileSize / 2, isWall ? 'tile_wall' : 'tile_ground');
                tile.setDepth(isWall ? 1 : 0);
            }
        }
    }
    // ── Death / Respawn UI ──────────────────────────────────────
    showDeathScreen() {
        this.deathOverlay.setVisible(true);
        this.deathText.setVisible(true);
        this.respawnTimer = RESPAWN_TIME_MS;
        if (this.playerSprite) {
            this.playerSprite.setVisible(false);
        }
        if (this.aimLine) {
            this.aimLine.clear();
        }
    }
    hideDeathScreen() {
        this.deathOverlay.setVisible(false);
        this.deathText.setVisible(false);
        if (this.playerSprite) {
            this.playerSprite.setVisible(true);
        }
    }
    // ── HUD HP Bar ──────────────────────────────────────────────
    drawLocalHpBar() {
        this.hpBarGfx.clear();
        const barWidth = 200;
        const barHeight = 16;
        const barX = 16;
        const barY = this.cameras.main.height - 40;
        // Background
        this.hpBarGfx.fillStyle(0x000000, 0.7);
        this.hpBarGfx.fillRect(barX - 2, barY - 2, barWidth + 4, barHeight + 4);
        // HP fill
        const hpRatio = Math.max(0, this.localHp / this.localMaxHp);
        const fillColor = hpRatio > 0.5 ? 0x44ff44 : hpRatio > 0.25 ? 0xffaa00 : 0xff4444;
        this.hpBarGfx.fillStyle(fillColor, 1);
        this.hpBarGfx.fillRect(barX, barY, barWidth * hpRatio, barHeight);
        // Border
        this.hpBarGfx.lineStyle(1, 0xffffff, 0.5);
        this.hpBarGfx.strokeRect(barX - 2, barY - 2, barWidth + 4, barHeight + 4);
    }
    update(_time, delta) {
        if (!this.connected || !this.playerSprite)
            return;
        const dt = delta / 1000;
        // ── Update death timer ───────────────────────────────────
        if (!this.localAlive && this.respawnTimer > 0) {
            this.respawnTimer -= delta;
            const secs = Math.max(0, Math.ceil(this.respawnTimer / 1000));
            this.deathText.setText(`YOU DIED\nRespawning in ${secs}...`);
        }
        // ── Sample input ────────────────────────────────────────
        const input = this.inputManager.getInput(this.localX, this.localY);
        // ── Client-side prediction (only if alive) ──────────────
        if (this.localAlive) {
            this.applyInputLocally(input, dt);
        }
        // ── Send to server (always, so server knows aim angle) ──
        this.network.sendInput(input);
        // ── Store for reconciliation ────────────────────────────
        this.pendingInputs.push({ input, dt });
        // ── Update local sprite ─────────────────────────────────
        if (this.localAlive) {
            this.playerSprite.x = this.localX;
            this.playerSprite.y = this.localY;
            this.playerSprite.rotation = input.aimAngle;
            // ── Draw aim line ─────────────────────────────────────
            this.drawAimLine(input.aimAngle);
        }
        // ── Draw local HP bar HUD ───────────────────────────────
        this.drawLocalHpBar();
        // ── Interpolate remote players + projectiles ────────────
        this.entityRenderer.update();
        // ── Render fog of war ───────────────────────────────────
        this.fogOfWar.render();
    }
    /**
     * Apply input locally for client-side prediction.
     * Mirrors the server's movement logic exactly.
     */
    applyInputLocally(input, dt) {
        let mx = 0;
        let my = 0;
        if (input.up)
            my -= 1;
        if (input.down)
            my += 1;
        if (input.left)
            mx -= 1;
        if (input.right)
            mx += 1;
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
    resolveCollisionLocal(x, y, dx, dy) {
        const radius = PLAYER_COLLISION_RADIUS;
        const isBlocked = (cx, cy) => {
            const minTX = Math.floor((cx - radius) / TILE_SIZE);
            const maxTX = Math.floor((cx + radius) / TILE_SIZE);
            const minTY = Math.floor((cy - radius) / TILE_SIZE);
            const maxTY = Math.floor((cy + radius) / TILE_SIZE);
            for (let ty = minTY; ty <= maxTY; ty++) {
                for (let tx = minTX; tx <= maxTX; tx++) {
                    if (tx < 0 || tx >= this.collisionMapW || ty < 0 || ty >= this.collisionMapH)
                        return true;
                    if (this.collisionGrid[ty * this.collisionMapW + tx] !== 1)
                        continue;
                    const tileLeft = tx * TILE_SIZE;
                    const tileTop = ty * TILE_SIZE;
                    const closestX = Math.max(tileLeft, Math.min(cx, tileLeft + TILE_SIZE));
                    const closestY = Math.max(tileTop, Math.min(cy, tileTop + TILE_SIZE));
                    const ddx = cx - closestX;
                    const ddy = cy - closestY;
                    if (ddx * ddx + ddy * ddy < radius * radius)
                        return true;
                }
            }
            return false;
        };
        // Try full movement
        let newX = x + dx;
        let newY = y + dy;
        if (!isBlocked(newX, newY))
            return { x: newX, y: newY };
        // Slide along axes
        if (!isBlocked(x + dx, y))
            return { x: x + dx, y };
        if (!isBlocked(x, y + dy))
            return { x, y: y + dy };
        return { x, y };
    }
    /**
     * Server reconciliation: when we get authoritative state, reapply
     * any inputs the server hasn't processed yet.
     */
    reconcile(serverX, serverY, serverSeq) {
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
    drawAimLine(angle) {
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
//# sourceMappingURL=GameScene.js.map