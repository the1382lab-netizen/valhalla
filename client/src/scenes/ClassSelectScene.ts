import Phaser from 'phaser';
import { CLASS_TEMPLATES, CLASS_COLORS, ClassId, ALL_CLASS_IDS } from '@valhalla/shared';

/**
 * Simple class selection screen.
 * Shows 6 class buttons — click one to join the game as that class.
 * Intentionally barebones; polish comes in Phase 7.
 */
export class ClassSelectScene extends Phaser.Scene {
  constructor() {
    super({ key: 'ClassSelectScene' });
  }

  create(): void {
    const { width, height } = this.cameras.main;

    // Title
    this.add.text(width / 2, 60, 'VALHALLA', {
      fontSize: '48px',
      color: '#ffcc00',
      stroke: '#000000',
      strokeThickness: 4,
      fontStyle: 'bold',
    }).setOrigin(0.5);

    this.add.text(width / 2, 110, 'Choose Your Class', {
      fontSize: '20px',
      color: '#cccccc',
      stroke: '#000000',
      strokeThickness: 2,
    }).setOrigin(0.5);

    // Class buttons — 3 columns x 2 rows
    const cols = 3;
    const cardW = 200;
    const cardH = 160;
    const gapX = 24;
    const gapY = 24;
    const totalW = cols * cardW + (cols - 1) * gapX;
    const startX = (width - totalW) / 2;
    const startY = 160;

    ALL_CLASS_IDS.forEach((classId, i) => {
      const template = CLASS_TEMPLATES[classId];
      const col = i % cols;
      const row = Math.floor(i / cols);
      const x = startX + col * (cardW + gapX);
      const y = startY + row * (cardH + gapY);

      this.createClassCard(x, y, cardW, cardH, classId, template);
    });

    // Footer
    this.add.text(width / 2, height - 30, 'Click a class to begin', {
      fontSize: '14px',
      color: '#888888',
    }).setOrigin(0.5);
  }

  private createClassCard(
    x: number,
    y: number,
    w: number,
    h: number,
    classId: ClassId,
    template: typeof CLASS_TEMPLATES[ClassId],
  ): void {
    const color = CLASS_COLORS[classId];

    // Card background
    const bg = this.add.rectangle(x + w / 2, y + h / 2, w, h, 0x1a1a2e);
    bg.setStrokeStyle(2, color, 0.8);
    bg.setInteractive({ useHandCursor: true });

    // Hover effect
    bg.on('pointerover', () => {
      bg.setFillStyle(0x2a2a4e);
      bg.setStrokeStyle(3, color, 1);
    });
    bg.on('pointerout', () => {
      bg.setFillStyle(0x1a1a2e);
      bg.setStrokeStyle(2, color, 0.8);
    });

    // Click — start game with this class
    bg.on('pointerdown', () => {
      this.scene.start('GameScene', { classId });
    });

    // Class icon (colored circle)
    const iconSize = 24;
    const gfx = this.add.graphics();
    gfx.fillStyle(color, 1);
    gfx.fillCircle(x + w / 2, y + 30, iconSize / 2);
    gfx.lineStyle(2, 0xffffff, 0.3);
    gfx.strokeCircle(x + w / 2, y + 30, iconSize / 2);

    // Class name
    const hexColor = '#' + color.toString(16).padStart(6, '0');
    this.add.text(x + w / 2, y + 52, template.name, {
      fontSize: '18px',
      color: hexColor,
      stroke: '#000000',
      strokeThickness: 2,
      fontStyle: 'bold',
    }).setOrigin(0.5, 0);

    // Description
    this.add.text(x + w / 2, y + 76, template.description, {
      fontSize: '11px',
      color: '#aaaaaa',
      wordWrap: { width: w - 20 },
      align: 'center',
    }).setOrigin(0.5, 0);

    // Key stats preview
    const stats = template.baseStats;
    const armor = template.allowedArmor;
    const speed = template.baseSpeed;
    const mana = template.canUseMana ? `MP: ${stats.mana}` : 'No Mana';

    this.add.text(x + w / 2, y + h - 28, `HP: ${stats.hp}  ${mana}  Spd: ${speed}  ${armor}`, {
      fontSize: '9px',
      color: '#777777',
    }).setOrigin(0.5, 0);
  }
}
