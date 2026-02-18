import Phaser from 'phaser';
/**
 * First scene in the game flow.
 * Shows the Valhalla title with a dark background, and an HTML overlay
 * for login/register forms. Once authenticated, transitions to either
 * CharacterSelectScene (if user has characters) or ClassSelectScene.
 */
export declare class LoginScene extends Phaser.Scene {
    private overlay;
    constructor();
    create(): void;
    private handleAuthSuccess;
    shutdown(): void;
}
//# sourceMappingURL=LoginScene.d.ts.map