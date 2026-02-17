import Phaser from 'phaser';
import { CLASS_TEMPLATES, CLASS_COLORS, ALL_CLASS_IDS } from '@valhalla/shared';
import { AuthClient } from '../systems/AuthClient.js';
/**
 * Class selection + character naming screen.
 * After picking a class, prompts for a character name, creates the character
 * in the database, then transitions to GameScene.
 */
export class ClassSelectScene extends Phaser.Scene {
    token = '';
    isNewAccount = false;
    nameOverlay = null;
    constructor() {
        super({ key: 'ClassSelectScene' });
    }
    init(data) {
        this.token = data?.token || AuthClient.getToken() || '';
        this.isNewAccount = data?.isNewAccount ?? false;
    }
    create() {
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
        // Back button (if coming from character select, not new account)
        if (!this.isNewAccount) {
            const backBtn = this.add.text(20, height - 30, '< Back', {
                fontSize: '14px',
                color: '#888888',
            }).setInteractive({ useHandCursor: true });
            backBtn.on('pointerover', () => backBtn.setColor('#cccccc'));
            backBtn.on('pointerout', () => backBtn.setColor('#888888'));
            backBtn.on('pointerdown', async () => {
                try {
                    const chars = await AuthClient.getCharacters();
                    if (chars.length > 0) {
                        this.scene.start('CharacterSelectScene', {
                            characters: chars,
                            token: this.token,
                            username: '',
                        });
                    }
                }
                catch {
                    // If fetch fails, just stay here
                }
            });
        }
        // Footer
        this.add.text(width / 2, height - 30, 'Click a class to create your character', {
            fontSize: '14px',
            color: '#888888',
        }).setOrigin(0.5);
    }
    createClassCard(x, y, w, h, classId, template) {
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
        // Click — show name input overlay
        bg.on('pointerdown', () => {
            this.showNameInput(classId);
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
    /**
     * Show an HTML overlay to input the character name.
     */
    showNameInput(classId) {
        // Remove existing overlay if any
        if (this.nameOverlay) {
            this.nameOverlay.remove();
        }
        const overlay = document.createElement('div');
        this.nameOverlay = overlay;
        Object.assign(overlay.style, {
            position: 'fixed',
            top: '0',
            left: '0',
            width: '100vw',
            height: '100vh',
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            zIndex: '1000',
            backgroundColor: 'rgba(0,0,0,0.5)',
            fontFamily: 'Arial, Helvetica, sans-serif',
        });
        const card = document.createElement('div');
        Object.assign(card.style, {
            background: '#1a1a2e',
            border: '2px solid #555588',
            borderRadius: '12px',
            padding: '24px 32px',
            minWidth: '300px',
            textAlign: 'center',
        });
        const title = document.createElement('h2');
        const tmpl = CLASS_TEMPLATES[classId];
        title.textContent = `Name Your ${tmpl.name}`;
        Object.assign(title.style, {
            color: '#ffcc00',
            fontSize: '20px',
            margin: '0 0 16px 0',
        });
        card.appendChild(title);
        const errorDiv = document.createElement('div');
        Object.assign(errorDiv.style, {
            color: '#ff4444',
            fontSize: '13px',
            minHeight: '18px',
            marginBottom: '8px',
        });
        card.appendChild(errorDiv);
        const input = document.createElement('input');
        input.type = 'text';
        input.placeholder = 'Character name';
        input.maxLength = 20;
        Object.assign(input.style, {
            width: '100%',
            padding: '10px 12px',
            fontSize: '14px',
            backgroundColor: '#0d0d1a',
            color: '#ffffff',
            border: '1px solid #444477',
            borderRadius: '6px',
            outline: 'none',
            boxSizing: 'border-box',
            marginBottom: '12px',
        });
        card.appendChild(input);
        const btnRow = document.createElement('div');
        btnRow.style.display = 'flex';
        btnRow.style.gap = '8px';
        const cancelBtn = document.createElement('button');
        cancelBtn.textContent = 'Cancel';
        Object.assign(cancelBtn.style, {
            flex: '1',
            padding: '10px',
            fontSize: '14px',
            backgroundColor: '#333',
            color: '#ccc',
            border: '1px solid #555',
            borderRadius: '6px',
            cursor: 'pointer',
        });
        cancelBtn.addEventListener('click', () => overlay.remove());
        const createBtn = document.createElement('button');
        createBtn.textContent = 'Create';
        Object.assign(createBtn.style, {
            flex: '1',
            padding: '10px',
            fontSize: '14px',
            fontWeight: 'bold',
            backgroundColor: '#ffcc00',
            color: '#1a1a2e',
            border: 'none',
            borderRadius: '6px',
            cursor: 'pointer',
        });
        const doCreate = async () => {
            const name = input.value.trim();
            if (!name) {
                errorDiv.textContent = 'Please enter a name.';
                return;
            }
            createBtn.disabled = true;
            createBtn.textContent = 'Creating...';
            errorDiv.textContent = '';
            try {
                const char = await AuthClient.createCharacter(name, classId);
                overlay.remove();
                this.nameOverlay = null;
                // Transition to game with the new character
                this.scene.start('GameScene', {
                    characterId: char.id,
                    token: this.token,
                });
            }
            catch (err) {
                errorDiv.textContent = err.message || 'Failed to create character.';
                createBtn.disabled = false;
                createBtn.textContent = 'Create';
            }
        };
        createBtn.addEventListener('click', doCreate);
        input.addEventListener('keydown', (e) => {
            if (e.key === 'Enter')
                doCreate();
            if (e.key === 'Escape')
                overlay.remove();
        });
        btnRow.appendChild(cancelBtn);
        btnRow.appendChild(createBtn);
        card.appendChild(btnRow);
        overlay.appendChild(card);
        document.body.appendChild(overlay);
        setTimeout(() => input.focus(), 100);
    }
    shutdown() {
        if (this.nameOverlay) {
            this.nameOverlay.remove();
            this.nameOverlay = null;
        }
    }
}
//# sourceMappingURL=ClassSelectScene.js.map