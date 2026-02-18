import Phaser from 'phaser';
import { BootScene } from './scenes/BootScene.js';
import { LoginScene } from './scenes/LoginScene.js';
import { CharacterSelectScene } from './scenes/CharacterSelectScene.js';
import { ClassSelectScene } from './scenes/ClassSelectScene.js';
import { GameScene } from './scenes/GameScene.js';
const config = {
    type: Phaser.AUTO,
    parent: 'game-container',
    width: window.innerWidth,
    height: window.innerHeight,
    backgroundColor: '#1a1a2e',
    pixelArt: true,
    scale: {
        mode: Phaser.Scale.RESIZE,
        autoCenter: Phaser.Scale.CENTER_BOTH,
    },
    scene: [BootScene, LoginScene, CharacterSelectScene, ClassSelectScene, GameScene],
};
new Phaser.Game(config);
//# sourceMappingURL=main.js.map