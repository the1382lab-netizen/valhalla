"""B-06 step 1.9f: place Eldmoor's NPC Spawn Points from the approved layout.

Reads Docs/Zones/Eldmoor/eldmoor_layout.json (camps, roamers, the keep's
rooms, the outpost) and Tools/eldmoor/population_looks.json (which NPC Type
look each template wears, written by Tools/eldmoor/gen_population.py), and
places one AValhallaNPCSpawner per spawn in L_GrasslandsV2_Gameplay:

* camps C2-C10: the design's archetypes in a ring round the camp fire, plus
  the camp's goblins (Kevin, 2026-09-24); the named slots get their rare
  template and chance. C1's bandits are Kevin's own spawn points and are left
  alone; only C1's goblin is added;
* roamers R1-R8: a route on the spawn point (PingPong, or Loop for R5); the
  second of a pair follows the first;
* Ashvane Keep A1-A7 at the design's tiles, with the bailey patrol pair;
* Harrow's Rest's ten friendly NPCs, the two wall guards on their routes.

Every actor it places carries the tag `eldmoor_pop`; a re-run deletes those
and places them again, and never touches any other actor. Save the level
afterwards (the script does not).

    import valhalla_tools.place_eldmoor_population as p; p.run()
"""

import json
import math
import os

import unreal

from valhalla_tools import make_eldmoor_npc_types as types

TAG = "eldmoor_pop"
LEVEL = "L_GrasslandsV2_Gameplay"
ZONE_X = 80000.0
ZONE_Y = 0.0
REPO = os.path.abspath(os.path.join(unreal.Paths.project_dir(), ".."))
LAYOUT = os.path.join(REPO, "Docs", "Zones", "Eldmoor", "eldmoor_layout.json")
LOOKS = os.path.join(REPO, "Tools", "eldmoor", "population_looks.json")

# The design's archetype names -> the templates gen_population.py wrote.
DESIGN = {
    "renegade_footman@C10": "renegade_forager",
    "renegade_archer@C10": "renegade_forager_archer",
    "renegade_sergeant@C10": "renegade_forager_sergeant",
}
# Named slots: placeholder template -> (rare template, chance, respawn minutes)
RARE = {
    "poacher_captain": ("named_merrik_sallow", 0.25, 20),
    "barrow_hierophant": ("named_selwen_marr", 0.25, 20),
    "brigand_chief": ("named_grimald_hask", 0.30, 25),
    "renegade_lieutenant": ("named_ordric_vane", 0.20, 28),
}
ROAMERS = {
    "R1": ("wayside_footpad", "PING_PONG"), "R2": ("bandit_scout", "PING_PONG"),
    "R3": ("outlaw_courier", "PING_PONG"), "R4": ("poacher_tracker", "PING_PONG"),
    "R5": ("cultist_procession", "LOOP"), "R6": ("brigand_runner", "PING_PONG"),
    "R7": ("renegade_patrol", "PING_PONG"), "R8": ("highland_scavenger", "PING_PONG"),
}
KEEP_NAMED = {
    "Kell Draven": "named_kell_draven", "Mael Corvane": "named_mael_corvane",
    "Brann Coll": "named_brann_coll", "Warden Hesk": "named_warden_hesk",
    "renegade lieutenant": "renegade_lieutenant",
}


def _world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


# Floor heights (world Z, cm) for the keep, where two walkable floors overlap:
# the great hall on the +4 m plateau and the undercroft 3 m below it (A7).
KEEP_FLOOR_Z = 420.0
UNDERCROFT_FLOOR_Z = 110.0


def _ground(x, y, floor_z=None):
    """World location of zone-local (x, y) cm on the nav mesh (or the Landscape), feet level.

    floor_z picks one floor where two overlap (the hall above the undercroft):
    the projection only looks 1.5 m above and below it.
    """
    w = _world()
    if floor_z is not None:
        # That floor only, nudged up to 1.3 m sideways if the exact spot has no
        # nav mesh (the hall floor over the undercroft is patchy); never the
        # other floor.
        for r, n in ((0, 1), (64, 8), (128, 12)):
            for k in range(n):
                a = 2.0 * math.pi * k / n
                q = unreal.NavigationSystemV1.project_point_to_navigation(
                    w, unreal.Vector(ZONE_X + x + r * math.cos(a), ZONE_Y + y + r * math.sin(a), floor_z),
                    None, None, unreal.Vector(40, 40, 150))
                if q and (q.x != 0.0 or q.y != 0.0):
                    return q
        return None
    probe = unreal.Vector(ZONE_X + x, ZONE_Y + y, 600.0)
    q = unreal.NavigationSystemV1.project_point_to_navigation(w, probe, None, None, unreal.Vector(80, 80, 2000))
    if q and (q.x != 0.0 or q.y != 0.0):
        return q
    hit = unreal.SystemLibrary.line_trace_single(w, unreal.Vector(probe.x, probe.y, 5000), unreal.Vector(probe.x, probe.y, -5000),
                                                 unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE, True)
    if hit:
        return hit.to_tuple()[4]
    return unreal.Vector(probe.x, probe.y, 0.0)


def _template_id(design_name, camp=None):
    base = design_name.split("/")[0].strip().replace(" ", "_")
    base = base.split("(")[0].strip().rstrip("_")
    return DESIGN.get("%s@%s" % (base, camp), base)


class Placer:
    def __init__(self, looks):
        self.looks = looks
        self.count = 0
        self.problems = []
        self.cls = unreal.load_class(None, "/Script/ValhallaGame.ValhallaNPCSpawner")

    def place(self, label, template, x, y, yaw=0.0, rare=None, respawn_min=None, floor_z=None):
        look = self.looks.get(template)
        if not look:
            self.problems.append("%s: template %s has no look (not written by gen_population.py?)" % (label, template))
            return None
        loc = _ground(x, y, floor_z)
        if loc is None:
            self.problems.append("%s: no nav mesh on its floor near (%.0f, %.0f)" % (label, x, y))
            return None
        actor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(
            self.cls, unreal.Vector(loc.x, loc.y, loc.z + 100.0), unreal.Rotator(0.0, 0.0, yaw))
        actor.set_actor_label("Eld_" + label)
        actor.tags = [TAG]
        actor.set_editor_property("npc_class", types.class_for_look(look))
        actor.set_editor_property("template_id", template)
        if rare:
            rare_id, chance, minutes = rare
            actor.set_editor_property("rare_template_id", rare_id)
            actor.set_editor_property("rare_npc_class", types.class_for_look(self.looks.get(rare_id, look)))
            actor.set_editor_property("rare_chance", chance)
            respawn_min = minutes
        if respawn_min:
            actor.set_editor_property("respawn_seconds", float(respawn_min * 60))
        self.count += 1
        return actor

    def route(self, actor, points, mode, floor_z=None):
        # Patrol points are in the spawn point's local space (they turn with it),
        # so world points go through the inverse of its transform.
        xf = actor.get_actor_transform()
        rel = []
        for (x, y) in points:
            g = _ground(x, y, floor_z) or _ground(x, y)
            # Nav-mesh height plus a little: the zone check looks for nav within 1 m.
            rel.append(xf.inverse_transform_location(unreal.Vector(g.x, g.y, g.z + 40.0)))
        actor.set_editor_property("patrol_points", rel)
        actor.set_editor_property("patrol_mode", getattr(unreal.ValhallaPatrolMode, mode))


def _ring(cx, cy, n, radius, start_deg=200.0):
    for i in range(n):
        a = math.radians(start_deg + 360.0 * i / max(n, 1))
        yield cx + radius * math.cos(a), cy + radius * math.sin(a), math.degrees(math.atan2(-math.sin(a), -math.cos(a)))


def run():
    layout = json.load(open(LAYOUT, encoding="utf-8"))
    pop = json.load(open(LOOKS, encoding="utf-8"))
    looks, camp_goblins = pop["looks"], pop["campGoblins"]

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).set_current_level_by_name(LEVEL)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    removed = 0
    for a in eas.get_all_level_actors():
        if TAG in [str(t) for t in a.tags]:
            eas.destroy_actor(a)
            removed += 1

    p = Placer(looks)

    # ── Camps ───────────────────────────────────────────────────────────
    for camp in layout["camps"]:
        cid = camp["id"]
        cx, cy = camp["centre"]
        members = []
        if cid != "C1":   # C1's bandits are Kevin's own spawn points
            for npc in camp["npcs"]:
                for _ in range(npc["count"]):
                    members.append(npc["templateId"])
        goblins = camp_goblins.get(cid, [])
        n = len(members) + len(goblins)
        spots = list(_ring(cx, cy, n, 150.0 if n <= 4 else 190.0))
        for i, design_name in enumerate(members):
            tid = _template_id(design_name, cid)
            x, y, yaw = spots[i]
            p.place("%s_%s_%d" % (cid, tid, i), tid, x, y, yaw, rare=RARE.get(tid))
        for j, gid in enumerate(goblins):
            x, y, yaw = spots[len(members) + j] if cid != "C1" else (cx + 110.0, cy - 60.0, 150.0)
            p.place("%s_%s" % (cid, gid), gid, x, y, yaw)

    # ── Roamers ─────────────────────────────────────────────────────────
    for r in layout["roamers"]:
        tid, mode = ROAMERS[r["id"]]
        path = [tuple(pt) for pt in r["path"]]
        # A path that walks back over itself (R1, R3) is a ping-pong of its unique stops.
        if len(path) >= 3 and path[-1] == path[-3]:
            path = path[:-1]
        x0, y0 = path[0]
        leader = p.place("%s_%s" % (r["id"], tid), tid, x0, y0)
        if leader:
            p.route(leader, path[1:], mode)
        for k in range(1, r["spawnCount"]):
            f = p.place("%s_%s_f%d" % (r["id"], tid, k), tid, x0 - 120.0 * k, y0)
            if f and leader:
                f.set_editor_property("follow_spawner", leader)

    # ── Ashvane Keep ────────────────────────────────────────────────────
    for area in layout["castle"]["areas"]:
        pair_leader = None
        for i, s in enumerate(area["spawns"]):
            name = s["template"]
            tid = None
            for key, mapped in KEEP_NAMED.items():
                if key in name:
                    tid = mapped
            tid = tid or _template_id(name)
            x, y = s["cm"]
            floor = UNDERCROFT_FLOOR_Z if area["id"] == "A7" else KEEP_FLOOR_Z
            a = p.place("%s_%s_%d" % (area["id"], tid, i), tid, x, y, 90.0, rare=RARE.get(tid), floor_z=floor)
            if a and "patrol, pair" in name:
                if pair_leader is None:
                    pair_leader = a
                    if area.get("patrol"):
                        p.route(a, [tuple(pt) for pt in area["patrol"][1:]], "PING_PONG", floor_z=floor)
                else:
                    a.set_editor_property("follow_spawner", pair_leader)

    # ── Harrow's Rest ───────────────────────────────────────────────────
    posts = {g["npc"]: g for g in layout["guardPosts"]}
    for npc in layout["outpost"]["npcs"]:
        tid = npc["id"]
        x, y = npc["cm"]
        a = p.place("Outpost_%s" % tid, tid, x, y, 180.0)
        post = posts.get(tid)
        if a and post and post.get("path"):
            p.route(a, [tuple(pt) for pt in post["path"]], "PING_PONG")

    return {"removed": removed, "placed": p.count, "problems": p.problems}
