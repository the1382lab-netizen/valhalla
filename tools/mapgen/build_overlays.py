"""
Editor overlays for the new zones.

Everything that lives outside the tile grid — the player spawn, NPC and enemy
placements, and every portal — goes in the overlay rather than the Tiled
objectgroup, because `MapManager.mergeOverlay` replaces a zone's portals with
the overlay's list wholesale whenever an overlay file exists. Splitting them
across both files would mean half of them silently vanish.

Every point is checked against the generated collision grid before it is
written: a spawn inside a wall is a player who cannot move.
"""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import build_grasslands_v2 as G
import build_cave_dungeon as C

T = 64                       # ORTHO_TILE_SIZE
CENTRE = T // 2

# The three NPC templates the game actually ships with.
NPC_AGGRO = "npc_1771431708366"      # Test Enemy   — aggressive
NPC_TOUGH = "npc_1771709765831"      # Tough Guy    — patrol
NPC_CALM = "npc_1771781907844"       # New NPC      — passive


def px(tx, ty):
    """Tile -> the centre of that tile in world pixels."""
    return tx * T + CENTRE, ty * T + CENTRE


def rect(x0, y0, x1, y1):
    """Inclusive tile box -> the top-left-anchored pixel rect the editor uses."""
    return {"x": x0 * T, "y": y0 * T,
            "width": (x1 - x0 + 1) * T, "height": (y1 - y0 + 1) * T}


class Overlay:
    def __init__(self, cv, zone):
        self.cv, self.zone = cv, zone
        self.solid = cv.collision()
        self.points = []
        self.moved = []
        self.n = 0

    def _walkable(self, tx, ty, what):
        """Snap onto open ground if the hand-picked tile happens to have grown a
        tree or a boulder. The radius is deliberately small — a spawn that has
        to travel more than three tiles to find air was placed in the wrong
        place, and should be moved by hand rather than quietly relocated."""
        if not self.cv.inside(tx, ty):
            raise SystemExit(f"{self.zone}: {what} at ({tx},{ty}) is off the map")
        if not self.solid[ty * self.cv.w + tx]:
            return tx, ty
        for r in (1, 2, 3):
            for dy in range(-r, r + 1):
                for dx in range(-r, r + 1):
                    if max(abs(dx), abs(dy)) != r:
                        continue
                    nx, ny = tx + dx, ty + dy
                    if self.cv.inside(nx, ny) and not self.solid[ny * self.cv.w + nx]:
                        self.moved.append((what, (tx, ty), (nx, ny)))
                        return nx, ny
        raise SystemExit(f"{self.zone}: {what} at ({tx},{ty}) is walled in")

    def _id(self, kind):
        self.n += 1
        return f"{self.zone}_{kind}_{self.n:03d}"

    def spawn(self, kind, tx, ty, label=None, template=None):
        tx, ty = self._walkable(tx, ty, label or kind)
        x, y = px(tx, ty)
        e = {"id": self._id(kind), "type": kind, "x": x, "y": y, "properties": {}}
        if label:
            e["label"] = label
        if template:
            e["templateId"] = template
        self.points.append(e)
        return e

    def portal(self, target, box, label):
        e = {"id": self._id("portal"), "type": "portal",
             "label": label, "templateId": target, **rect(*box), "properties": {}}
        self.points.append(e)
        return e

    def entry(self, from_zone, tx, ty, label):
        tx, ty = self._walkable(tx, ty, label)
        x, y = px(tx, ty)
        self.points.append({
            "id": self._id("entry"), "type": "zone_entry", "x": x, "y": y,
            "label": label, "templateId": from_zone, "fromZone": from_zone,
            "properties": {},
        })

    def write(self, path):
        with open(path, "w") as f:
            json.dump({"version": "1.0.0", "spawnPoints": self.points}, f, indent=2)
        return path


# ---------------------------------------------------------------- grasslands v2
def grasslands_v2(cv):
    o = Overlay(cv, "grasslands_v2")
    o.spawn("player_spawn", *G.SPAWN, label="Eldmoor market")

    # --- portals -----------------------------------------------------------
    o.portal("cave_dungeon", (99, 88, 101, 90), "Greyfell Cave")
    o.entry("cave_dungeon", 100, 94, "From Greyfell Cave")

    o.portal("desert", G.DESERT_PORTAL, "Scorched Desert")
    o.entry("desert", 116, 62, "From Scorched Desert")

    o.portal("grasslands", G.OLD_ROAD_PORTAL, "Grasslands (old road)")
    o.entry("grasslands", 34, 7, "From the old road")

    # --- townsfolk ---------------------------------------------------------
    for label, tx, ty in (
        ("Innkeeper, Gilded Stag", 20, 19),
        ("Innkeeper, Wandering Boar", 48, 31),
        ("Shopkeeper", 19, 32),
        ("Market trader", 31, 22),
        ("Market trader", 37, 28),
        ("Town guard", 34, 43),
        ("Town guard", 52, 26),
        ("Farmer", 91, 22),
    ):
        o.spawn("npc_spawn", tx, ty, label=label, template=NPC_CALM)

    # --- things that want to kill you --------------------------------------
    for label, tx, ty, tpl in (
        ("Wolf",         30, 62, NPC_AGGRO),
        ("Wolf",         18, 72, NPC_AGGRO),
        ("Bandit",       26, 88, NPC_TOUGH),
        ("Bandit",       19, 95, NPC_AGGRO),
        ("Boar",         60, 60, NPC_AGGRO),
        ("Boar",         74, 70, NPC_AGGRO),
        ("Hill raider",  92, 80, NPC_TOUGH),
        ("Hill raider", 110, 96, NPC_AGGRO),
        ("Hill raider", 104, 110, NPC_AGGRO),
        ("Crow",         86, 34, NPC_AGGRO),
        ("Crow",        108, 28, NPC_AGGRO),
        ("River lurker", 70, 84, NPC_TOUGH),
    ):
        o.spawn("enemy_spawn", tx, ty, label=label, template=tpl)
    return o


# ---------------------------------------------------------------- cave dungeon
def cave_dungeon(cv):
    o = Overlay(cv, "cave_dungeon")
    o.spawn("player_spawn", *C.SPAWN, label="Cave entrance")

    o.portal("grasslands_v2", (31, 61, 33, 61), "Grasslands")
    o.entry("grasslands_v2", 32, 56, "From the Greyfell hills")

    for label, tx, ty, tpl in (
        ("Cave bat",      32, 46, NPC_AGGRO),
        ("Cave bat",      28, 42, NPC_AGGRO),
        ("Cave bat",      36, 47, NPC_AGGRO),
        ("Warren spider", 10, 40, NPC_AGGRO),
        ("Warren spider", 16, 46, NPC_AGGRO),
        ("Warren spider",  8, 45, NPC_TOUGH),
        ("Deep miner",    48, 42, NPC_TOUGH),
        ("Deep miner",    55, 47, NPC_AGGRO),
        ("Gallery ghoul", 27, 26, NPC_AGGRO),
        ("Gallery ghoul", 38, 31, NPC_AGGRO),
        ("Cistern crawler", 9, 29, NPC_TOUGH),
        ("Collapsed shade", 47, 22, NPC_AGGRO),
        ("Collapsed shade", 55, 28, NPC_AGGRO),
        ("Drowned knight", 24, 6, NPC_TOUGH),
        ("Drowned knight", 40, 14, NPC_TOUGH),
        ("Drowned knight", 32, 5, NPC_AGGRO),
    ):
        o.spawn("enemy_spawn", tx, ty, label=label, template=tpl)
    return o


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    gv = G.build()
    cd = C.build()
    o1, o2 = grasslands_v2(gv), cave_dungeon(cd)
    p1 = o1.write(os.path.join(out, "grasslands_v2-overlay.json"))
    p2 = o2.write(os.path.join(out, "cave_dungeon-overlay.json"))
    for o, p in ((o1, p1), (o2, p2)):
        n = len(json.load(open(p))["spawnPoints"])
        print(f"{p}: {n} entries")
        for what, was, now in o.moved:
            print(f"    snapped {what}: {was} -> {now}")
