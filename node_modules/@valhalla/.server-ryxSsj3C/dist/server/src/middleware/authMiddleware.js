/**
 * Express middleware that validates JWT tokens on protected routes.
 * Attaches userId and username to the request object.
 */
import { verifyToken } from '../services/AuthService.js';
export function authMiddleware(req, res, next) {
    const authHeader = req.headers.authorization;
    if (!authHeader || !authHeader.startsWith('Bearer ')) {
        res.status(401).json({ error: 'Missing or invalid authorization header.' });
        return;
    }
    const token = authHeader.slice(7); // Remove 'Bearer '
    try {
        const payload = verifyToken(token);
        req.userId = payload.userId;
        req.username = payload.username;
        next();
    }
    catch {
        res.status(401).json({ error: 'Invalid or expired token.' });
    }
}
//# sourceMappingURL=authMiddleware.js.map