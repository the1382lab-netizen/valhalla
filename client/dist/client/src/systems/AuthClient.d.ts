/**
 * Client-side authentication and character management.
 * Communicates with the server REST API for login/register/characters.
 * Stores JWT token in sessionStorage.
 */
export interface CharacterSummary {
    id: number;
    name: string;
    classId: string;
    level: number;
}
export interface AuthResponse {
    token: string;
    userId: number;
    username: string;
    characters: CharacterSummary[];
}
export declare class AuthClient {
    private static get baseUrl();
    /**
     * Register a new account.
     * Stores token on success.
     */
    static register(username: string, password: string): Promise<AuthResponse>;
    /**
     * Log in to an existing account.
     * Stores token on success.
     */
    static login(username: string, password: string): Promise<AuthResponse>;
    /**
     * Fetch the current user's characters.
     */
    static getCharacters(): Promise<CharacterSummary[]>;
    /**
     * Fetch the current user's characters and username together.
     * Used when returning to character select from an active game session.
     */
    static getCharactersWithUsername(): Promise<{
        characters: CharacterSummary[];
        username: string;
    }>;
    /**
     * Create a new character.
     * Returns the created character summary.
     */
    static createCharacter(name: string, classId: string): Promise<CharacterSummary>;
    /**
     * Delete a character by ID.
     */
    static deleteCharacter(characterId: number): Promise<void>;
    /** Get stored JWT token. */
    static getToken(): string | null;
    /** Clear stored token (logout). */
    static clearToken(): void;
    /** Check if user is logged in (has a token). */
    static isLoggedIn(): boolean;
}
//# sourceMappingURL=AuthClient.d.ts.map