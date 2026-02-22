const VISION_RADIUS = 400;
const VISION_CONE_ANGLE = Math.PI;
/**
 * Normalise an angle to the range [-PI, PI].
 */
function normaliseAngle(a) {
    while (a > Math.PI)
        a -= 2 * Math.PI;
    while (a < -Math.PI)
        a += 2 * Math.PI;
    return a;
}
/**
 * Check whether an angle is within a cone centered on aimAngle with the given half-width.
 */
function isAngleInCone(a, aimAngle, halfCone) {
    const diff = normaliseAngle(a - aimAngle);
    return Math.abs(diff) <= halfCone;
}
/**
 * VisibilitySystem — computes 2D visibility polygons using raycasting
 * with directional cone support.
 *
 * Approach:
 * 1. Extract wall edge segments from the collision grid (done once at construction)
 * 2. For a given player position and aim direction, gather nearby wall segments
 * 3. Collect unique wall endpoints within the vision cone
 * 4. Cast rays to each endpoint (+ tiny angle offsets to peek around corners)
 * 5. Find the closest wall/boundary intersection for each ray
 * 6. Clamp results to the vision radius
 * 7. Insert the player origin at cone edges, sort by angle, return polygon
 */
export class VisibilitySystem {
    constructor(collisionGrid, mapWidth, mapHeight, tileSize) {
        this.segments = [];
        this.mapW = mapWidth;
        this.mapH = mapHeight;
        this.tileSize = tileSize;
        this.segments = this.extractWallSegments(collisionGrid);
    }
    /**
     * Extract wall edge segments from the collision grid.
     */
    extractWallSegments(grid) {
        const segs = [];
        const w = this.mapW;
        const h = this.mapH;
        const ts = this.tileSize;
        const isWall = (tx, ty) => {
            if (tx < 0 || tx >= w || ty < 0 || ty >= h)
                return false;
            return grid[ty * w + tx] === 1;
        };
        for (let ty = 0; ty < h; ty++) {
            for (let tx = 0; tx < w; tx++) {
                if (!isWall(tx, ty))
                    continue;
                const left = tx * ts;
                const top = ty * ts;
                const right = left + ts;
                const bottom = top + ts;
                if (!isWall(tx, ty - 1)) {
                    segs.push({ a: { x: left, y: top }, b: { x: right, y: top } });
                }
                if (!isWall(tx, ty + 1)) {
                    segs.push({ a: { x: left, y: bottom }, b: { x: right, y: bottom } });
                }
                if (!isWall(tx - 1, ty)) {
                    segs.push({ a: { x: left, y: top }, b: { x: left, y: bottom } });
                }
                if (!isWall(tx + 1, ty)) {
                    segs.push({ a: { x: right, y: top }, b: { x: right, y: bottom } });
                }
            }
        }
        return segs;
    }
    /**
     * Compute the visibility polygon for a player at (px, py) looking in direction aimAngle.
     *
     * @param px        Player X position
     * @param py        Player Y position
     * @param aimAngle  Direction the player is facing (radians, 0 = right)
     * @param radius    Maximum vision distance in pixels
     * @param coneAngle Total field of view angle in radians (e.g. 100° ≈ 1.745 rad)
     * @returns         Array of {x,y} forming the visibility polygon, sorted by angle.
     */
    computeVisibilityPolygon(px, py, aimAngle = 0, radius = VISION_RADIUS, coneAngle = VISION_CONE_ANGLE) {
        const radiusSq = radius * radius;
        const halfCone = coneAngle / 2;
        // Cone boundary angles
        const coneLeft = normaliseAngle(aimAngle - halfCone);
        const coneRight = normaliseAngle(aimAngle + halfCone);
        // ── 1. Gather nearby wall segments ──────────────────────
        const nearbySegments = [];
        const extRadius = radius * 1.5;
        const extRadiusSq = extRadius * extRadius;
        for (const seg of this.segments) {
            const dxa = seg.a.x - px;
            const dya = seg.a.y - py;
            const dxb = seg.b.x - px;
            const dyb = seg.b.y - py;
            if (dxa * dxa + dya * dya > extRadiusSq &&
                dxb * dxb + dyb * dyb > extRadiusSq)
                continue;
            nearbySegments.push(seg);
        }
        // ── 2. Bounding-box segments at the vision radius ───────
        //    These ensure rays always hit *something* even in open space.
        const bLeft = px - radius;
        const bRight = px + radius;
        const bTop = py - radius;
        const bBottom = py + radius;
        const boundarySegs = [
            { a: { x: bLeft, y: bTop }, b: { x: bRight, y: bTop } },
            { a: { x: bRight, y: bTop }, b: { x: bRight, y: bBottom } },
            { a: { x: bRight, y: bBottom }, b: { x: bLeft, y: bBottom } },
            { a: { x: bLeft, y: bBottom }, b: { x: bLeft, y: bTop } },
        ];
        // NOTE: We deliberately do NOT add cone-edge segments here.
        // Segments that start at the player origin would cause every ray to
        // intersect at distance 0, collapsing the polygon.  Instead the cone
        // is enforced purely by filtering which angles we cast rays toward,
        // and by inserting the player position at the cone edges afterwards.
        const allSegments = [...nearbySegments, ...boundarySegs];
        // ── 3. Collect unique endpoint angles within the cone ───
        const uniqueAngles = new Set();
        for (const seg of allSegments) {
            const angA = Math.atan2(seg.a.y - py, seg.a.x - px);
            const angB = Math.atan2(seg.b.y - py, seg.b.x - px);
            if (isAngleInCone(angA, aimAngle, halfCone))
                uniqueAngles.add(angA);
            if (isAngleInCone(angB, aimAngle, halfCone))
                uniqueAngles.add(angB);
        }
        // Always include the exact cone-edge angles
        uniqueAngles.add(coneLeft);
        uniqueAngles.add(coneRight);
        // Add intermediate angles along the outer arc so the curved boundary
        // is smooth rather than a single straight line between endpoints.
        const arcSteps = 16;
        for (let i = 0; i <= arcSteps; i++) {
            const t = i / arcSteps;
            // Interpolate from coneLeft → coneRight through the cone centre
            const arcAngle = normaliseAngle(aimAngle - halfCone + coneAngle * t);
            uniqueAngles.add(arcAngle);
        }
        // ── 4. Build ray list (each angle ± tiny epsilon) ───────
        const epsilon = 0.00001;
        const rays = [];
        for (const angle of uniqueAngles) {
            const a1 = angle - epsilon;
            const a2 = angle;
            const a3 = angle + epsilon;
            if (isAngleInCone(a1, aimAngle, halfCone + epsilon * 2))
                rays.push(a1);
            if (isAngleInCone(a2, aimAngle, halfCone + epsilon * 2))
                rays.push(a2);
            if (isAngleInCone(a3, aimAngle, halfCone + epsilon * 2))
                rays.push(a3);
        }
        // ── 5. Cast each ray — find closest wall / boundary hit ─
        const intersections = [];
        for (const angle of rays) {
            const rdx = Math.cos(angle);
            const rdy = Math.sin(angle);
            let closest = null;
            for (const seg of allSegments) {
                const hit = this.raySegmentIntersect(px, py, rdx, rdy, seg);
                if (hit === null)
                    continue;
                if (closest === null || hit.param < closest.param) {
                    closest = hit;
                }
            }
            if (closest !== null) {
                closest.angle = angle;
                // Clamp to vision radius
                const dx = closest.x - px;
                const dy = closest.y - py;
                const distSq = dx * dx + dy * dy;
                if (distSq > radiusSq) {
                    const dist = Math.sqrt(distSq);
                    closest.x = px + (dx / dist) * radius;
                    closest.y = py + (dy / dist) * radius;
                }
                intersections.push(closest);
            }
            else {
                // No wall/boundary hit — place a point at the vision radius edge
                intersections.push({
                    x: px + rdx * radius,
                    y: py + rdy * radius,
                    param: radius,
                    angle,
                });
            }
        }
        // ── 6. Close the polygon back to the player at cone edges
        //    The two origin points ensure the polygon forms a proper cone
        //    (pie-slice) shape that returns to the player position.
        intersections.push({
            x: px, y: py, param: 0,
            angle: normaliseAngle(aimAngle - halfCone - epsilon * 3),
        });
        intersections.push({
            x: px, y: py, param: 0,
            angle: normaliseAngle(aimAngle + halfCone + epsilon * 3),
        });
        // ── 7. Sort by angle relative to the aim direction ──────
        intersections.sort((a, b) => {
            const da = normaliseAngle(a.angle - aimAngle);
            const db = normaliseAngle(b.angle - aimAngle);
            return da - db;
        });
        return intersections.map((i) => ({ x: i.x, y: i.y }));
    }
    /**
     * Ray-segment intersection.
     * Returns the intersection point and distance parameter, or null.
     */
    raySegmentIntersect(rx, ry, rdx, rdy, seg) {
        const sdx = seg.b.x - seg.a.x;
        const sdy = seg.b.y - seg.a.y;
        const denom = rdx * sdy - rdy * sdx;
        if (Math.abs(denom) < 1e-10)
            return null;
        const t2 = (rdx * (seg.a.y - ry) - rdy * (seg.a.x - rx)) / denom;
        const t1 = (sdx * (seg.a.y - ry) - sdy * (seg.a.x - rx)) / denom;
        if (t1 < 0)
            return null;
        if (t2 < 0 || t2 > 1)
            return null;
        return {
            x: rx + rdx * t1,
            y: ry + rdy * t1,
            param: t1,
            angle: 0,
        };
    }
    /**
     * Check if a point is inside a visibility polygon (ray-casting algorithm).
     */
    isPointVisible(polygon, px, py) {
        let inside = false;
        const n = polygon.length;
        for (let i = 0, j = n - 1; i < n; j = i++) {
            const xi = polygon[i].x;
            const yi = polygon[i].y;
            const xj = polygon[j].x;
            const yj = polygon[j].y;
            const intersect = yi > py !== yj > py && px < ((xj - xi) * (py - yi)) / (yj - yi) + xi;
            if (intersect)
                inside = !inside;
        }
        return inside;
    }
    /**
     * Check if a circle (entity) is at least partially visible.
     */
    isCircleVisible(polygon, cx, cy, radius) {
        if (this.isPointVisible(polygon, cx, cy))
            return true;
        for (let i = 0; i < 8; i++) {
            const angle = (i / 8) * Math.PI * 2;
            const px = cx + Math.cos(angle) * radius;
            const py = cy + Math.sin(angle) * radius;
            if (this.isPointVisible(polygon, px, py))
                return true;
        }
        return false;
    }
}
//# sourceMappingURL=VisibilitySystem.js.map