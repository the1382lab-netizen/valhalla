/**
 * HTML overlay for Login / Register forms.
 * Sits on top of the Phaser canvas. Dark-themed to match the game.
 * Calls AuthClient under the hood and invokes a callback on success.
 */
import { AuthResponse } from '../systems/AuthClient.js';
export type LoginSuccessCallback = (result: AuthResponse) => void;
export declare class LoginOverlay {
    private container;
    private mode;
    private onSuccess;
    private usernameInput;
    private passwordInput;
    private confirmInput;
    private confirmRow;
    private submitBtn;
    private toggleLink;
    private errorDiv;
    private titleEl;
    constructor(onSuccess: LoginSuccessCallback);
    private buildDOM;
    private createInput;
    private wrapInput;
    private toggleMode;
    private handleSubmit;
    /** Remove the overlay from the DOM. */
    destroy(): void;
}
//# sourceMappingURL=LoginOverlay.d.ts.map