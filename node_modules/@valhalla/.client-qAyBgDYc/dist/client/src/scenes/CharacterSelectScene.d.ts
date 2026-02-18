import Phaser from 'phaser';
import { type CharacterSummary } from '../systems/AuthClient.js';
interface SceneData {
    characters: CharacterSummary[];
    token: string;
    username: string;
}
/**
 * Character selection screen.
 * Shows existing characters as clickable cards. Allows playing, deleting,
 * or creating a new character.
 */
export declare class CharacterSelectScene extends Phaser.Scene {
    private characters;
    private token;
    private username;
    constructor();
    init(data: SceneData): void;
    create(): void;
    private createCharacterCard;
    private createNewCharacterButton;
}
export {};
//# sourceMappingURL=CharacterSelectScene.d.ts.map