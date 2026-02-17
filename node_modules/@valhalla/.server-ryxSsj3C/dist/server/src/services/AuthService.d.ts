/**
 * Authentication service: registration, login, and JWT verification.
 * Uses raw sql.js queries (no ORM).
 */
export interface JwtPayload {
    userId: number;
    username: string;
}
export interface AuthResult {
    token: string;
    userId: number;
    username: string;
}
/**
 * Register a new user account.
 * @throws Error if username is taken or input is invalid.
 */
export declare function register(username: string, password: string): Promise<AuthResult>;
/**
 * Log in an existing user.
 * @throws Error if credentials are invalid.
 */
export declare function login(username: string, password: string): Promise<AuthResult>;
/**
 * Verify and decode a JWT token.
 * @throws Error if token is invalid or expired.
 */
export declare function verifyToken(token: string): JwtPayload;
//# sourceMappingURL=AuthService.d.ts.map