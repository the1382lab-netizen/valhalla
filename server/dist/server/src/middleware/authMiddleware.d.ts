/**
 * Express middleware that validates JWT tokens on protected routes.
 * Attaches userId and username to the request object.
 */
import type { Request, Response, NextFunction } from 'express';
declare global {
    namespace Express {
        interface Request {
            userId?: number;
            username?: string;
        }
    }
}
export declare function authMiddleware(req: Request, res: Response, next: NextFunction): void;
//# sourceMappingURL=authMiddleware.d.ts.map