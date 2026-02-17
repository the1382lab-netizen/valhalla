import Phaser from 'phaser';
import { LoginOverlay } from '../ui/LoginOverlay.js';
import { AuthClient } from '../systems/AuthClient.js';
/**
 * First scene in the game flow.
 * Shows the Valhalla title with a dark background, and an HTML overlay
 * for login/register forms. Once authenticated, transitions to either
 * CharacterSelectScene (if user has characters) or ClassSelectScene.
 */
export class LoginScene extends Phaser.Scene {
    overlay = null;
    constructor() {
        super({ key: 'LoginScene' });
    }
    create() {
        const { width, height } = this.cameras.main;
        // Dark background
        this.cameras.main.setBackgroundColor('#0d0d1a');
        // Subtle "VALHALLA" background text (behind overlay)
        this.add.text(width / 2, height / 2 - 80, 'VALHALLA', {
            fontSize: '72px',
            color: '#1a1a2e',
            fontStyle: 'bold',
        }).setOrigin(0.5);
        // Create the HTML login overlay
        this.overlay = new LoginOverlay((result) => {
            this.handleAuthSuccess(result);
        });
    }
    handleAuthSuccess(result) {
        // Clean up overlay
        if (this.overlay) {
            this.overlay.destroy();
            this.overlay = null;
        }
        if (result.characters.length > 0) {
            // User has existing characters → character select
            this.scene.start('CharacterSelectScene', {
                characters: result.characters,
                token: AuthClient.getToken(),
                username: result.username,
            });
        }
        else {
            // New account, no characters → class select to create first character
            this.scene.start('ClassSelectScene', {
                token: AuthClient.getToken(),
                isNewAccount: true,
            });
        }
    }
    shutdown() {
        if (this.overlay) {
            this.overlay.destroy();
            this.overlay = null;
        }
    }
}
//# sourceMappingURL=LoginScene.js.map