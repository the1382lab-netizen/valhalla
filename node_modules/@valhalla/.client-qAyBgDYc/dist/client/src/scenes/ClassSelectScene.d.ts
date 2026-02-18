import Phaser from 'phaser';
interface SceneData {
    token?: string;
    isNewAccount?: boolean;
}
/**
 * Class selection + character naming screen.
 * After picking a class, prompts for a character name, creates the character
 * in the database, then transitions to GameScene.
 */
export declare class ClassSelectScene extends Phaser.Scene {
    private token;
    private isNewAccount;
    private nameOverlay;
    constructor();
    init(data: SceneData): void;
    create(): void;
    private createClassCard;
    /**
     * Show an HTML overlay to input the character name.
     */
    private showNameInput;
    shutdown(): void;
}
export {};
//# sourceMappingURL=ClassSelectScene.d.ts.map