/**
 * Client-side authentication and character management.
 * Communicates with the server REST API for login/register/characters.
 * Stores JWT token in sessionStorage.
 */
import { SERVER_URL } from '@valhalla/shared';
const TOKEN_KEY = 'valhalla_token';
export class AuthClient {
    static get baseUrl() {
        return SERVER_URL;
    }
    /**
     * Register a new account.
     * Stores token on success.
     */
    static async register(username, password) {
        const res = await fetch(`${this.baseUrl}/api/auth/register`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ username, password }),
        });
        const data = await res.json();
        if (!res.ok) {
            throw new Error(data.error || 'Registration failed.');
        }
        sessionStorage.setItem(TOKEN_KEY, data.token);
        return data;
    }
    /**
     * Log in to an existing account.
     * Stores token on success.
     */
    static async login(username, password) {
        const res = await fetch(`${this.baseUrl}/api/auth/login`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ username, password }),
        });
        const data = await res.json();
        if (!res.ok) {
            throw new Error(data.error || 'Login failed.');
        }
        sessionStorage.setItem(TOKEN_KEY, data.token);
        return data;
    }
    /**
     * Fetch the current user's characters.
     */
    static async getCharacters() {
        const token = this.getToken();
        if (!token)
            throw new Error('Not logged in.');
        const res = await fetch(`${this.baseUrl}/api/characters`, {
            headers: { Authorization: `Bearer ${token}` },
        });
        const data = await res.json();
        if (!res.ok) {
            throw new Error(data.error || 'Failed to fetch characters.');
        }
        return data.characters;
    }
    /**
     * Create a new character.
     * Returns the created character summary.
     */
    static async createCharacter(name, classId) {
        const token = this.getToken();
        if (!token)
            throw new Error('Not logged in.');
        const res = await fetch(`${this.baseUrl}/api/characters`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                Authorization: `Bearer ${token}`,
            },
            body: JSON.stringify({ name, classId }),
        });
        const data = await res.json();
        if (!res.ok) {
            throw new Error(data.error || 'Failed to create character.');
        }
        return data.character;
    }
    /**
     * Delete a character by ID.
     */
    static async deleteCharacter(characterId) {
        const token = this.getToken();
        if (!token)
            throw new Error('Not logged in.');
        const res = await fetch(`${this.baseUrl}/api/characters/${characterId}`, {
            method: 'DELETE',
            headers: { Authorization: `Bearer ${token}` },
        });
        if (!res.ok) {
            const data = await res.json();
            throw new Error(data.error || 'Failed to delete character.');
        }
    }
    /** Get stored JWT token. */
    static getToken() {
        return sessionStorage.getItem(TOKEN_KEY);
    }
    /** Clear stored token (logout). */
    static clearToken() {
        sessionStorage.removeItem(TOKEN_KEY);
    }
    /** Check if user is logged in (has a token). */
    static isLoggedIn() {
        return !!sessionStorage.getItem(TOKEN_KEY);
    }
}
//# sourceMappingURL=AuthClient.js.map