"""Skeleton + pose container with FK, built from an AnimPose (unreal) once, then pure Python."""
import json, os
from qm import *

class Pose:
    def __init__(self, names, parents, local):
        self.names = names; self.parents = parents
        self.idx = {n: i for i, n in enumerate(names)}
        self.local = [ (tuple(t), tuple(q)) for t, q in local ]   # (translation, quat)
        self.world = None
    def copy(self):
        p = Pose(self.names, self.parents, self.local); return p
    def fk(self):
        W = [None]*len(self.names)
        for i, (t, q) in enumerate(self.local):
            pi = self.parents[i]
            if pi < 0: W[i] = (t, q)
            else:
                pt, pq = W[pi]
                W[i] = (v_add(pt, q_rot(pq, t)), q_mul(pq, q))
        self.world = W; return W
    def wpos(self, b): return self.world[self.idx[b]][0]
    def wrot(self, b): return self.world[self.idx[b]][1]
    def set_world_rot(self, b, q):
        i = self.idx[b]; pi = self.parents[i]
        pq = self.world[pi][1] if pi >= 0 else (0, 0, 0, 1)
        t, _ = self.local[i]
        self.local[i] = (t, q_norm(q_mul(q_conj(pq), q)))
        self.fk()
    def rotate_world(self, b, dq):
        self.set_world_rot(b, q_mul(dq, self.wrot(b)))
    def set_local_rot(self, b, q):
        i = self.idx[b]; t, _ = self.local[i]; self.local[i] = (t, q_norm(q)); self.fk()
    def lrot(self, b): return self.local[self.idx[b]][1]
    def set_local_pos(self, b, p):
        i = self.idx[b]; _, q = self.local[i]; self.local[i] = (tuple(p), q); self.fk()
    def aim(self, bone, child, target_dir):
        cur = v_sub(self.wpos(child), self.wpos(bone))
        self.rotate_world(bone, q_between(cur, target_dir))
    def two_bone_ik(self, a, b, c, target, pole):
        """a=upperarm, b=lowerarm, c=hand. Place c at target; elbow bends toward pole (a world point)."""
        pa = self.wpos(a); pb = self.wpos(b); pc = self.wpos(c)
        l1 = v_len(v_sub(pb, pa)); l2 = v_len(v_sub(pc, pb))
        d = v_sub(target, pa); dist = min(v_len(d), (l1 + l2)*0.999); dn = v_norm(d)
        # elbow position
        x = (dist*dist + l1*l1 - l2*l2)/(2*dist)
        h = math.sqrt(max(0.0, l1*l1 - x*x))
        pv = v_sub(pole, pa); pv = v_sub(pv, v_mul(dn, v_dot(pv, dn))); pv = v_norm(pv)
        elbow = v_add(v_add(pa, v_mul(dn, x)), v_mul(pv, h))
        self.aim(a, b, v_sub(elbow, pa))
        self.aim(b, c, v_sub(v_add(pa, v_mul(dn, dist)), self.wpos(b)))

def to_json(pose, path):
    json.dump({"names": pose.names, "parents": pose.parents, "local": pose.local}, open(path, "w"))
def from_json(path):
    d = json.load(open(path)); return Pose(d["names"], d["parents"], d["local"])
