# Adds animation clips (idle, walk, run, fall, land, look) to a Blockbench humanoid FBX whose groups are its bones:
# Lower_torso, L_thigh, L_lower-leg, L_foot, R_..., Main_Torso, Shoulder_R/L, R_upper-arm, R_lower arm, L_upper-arm, ArmCanon, Head.
# Usage: python3 fbx_animate_humanoid.py in.fbx out.fbx
# Model faces -Z, Y up. Rotation about X: positive swings a hanging limb forward (toes / hands toward -Z),
# and tips an upright part (torso, head) backward.
import math, re, sys

SRC, DST = sys.argv[1], sys.argv[2]
text = open(SRC, encoding="utf-8").read()

# rest translations of the groups
rest = {}
for m in re.finditer(r'Model: (\d+), "Model::([^"]*)", "Null" \{(.*?)\n\t\}', text, re.S):
    p = re.search(r'P: "Lcl Translation",[^\n]*?,\s*([-\d.e]+),\s*([-\d.e]+),\s*([-\d.e]+)', m.group(3))
    rest[m.group(2)] = (int(m.group(1)), tuple(float(x) for x in p.groups()))

A, B = 56.25, 71.875            # thigh, shin length (file units)
LEG = A + B
ROOT = rest["Lower_torso"][1]

def ease(x): return 0.5 - 0.5 * math.cos(math.pi * max(0.0, min(1.0, x)))
def lerp(a, b, f): return a + (b - a) * f

def leg_ik(fwd, up):
    """hip -> ankle target (fwd: toward the front, up: up). Returns thigh and knee X turns (degrees), knee bends back."""
    D = math.hypot(fwd, up); D = min(D, LEG - 0.01); D = max(D, 20)
    k = math.acos(max(-1, min(1, (A * A + B * B - D * D) / (2 * A * B))))
    al = math.acos(max(-1, min(1, (A * A + D * D - B * B) / (2 * A * D))))
    phi = math.atan2(fwd, -up)
    t1 = phi + al; t2 = -(math.pi - k)
    return math.degrees(t1), math.degrees(t2)

def legs(pose, hip_dy, left, right, hip_dz=0.0):
    """left / right: (fwd, lift, toe pitch) of each ankle relative to its rest spot; hip_dy: how far the hips sank."""
    for side, (f, u, pitch) in (("L", left), ("R", right)):
        t1, t2 = leg_ik(f - hip_dz, -LEG + u - hip_dy)
        pose[side + "_thigh"] = (t1, 0, 0)
        pose[side + "_lower-leg"] = (t2, 0, 0)
        pose[side + "_foot"] = (-(t1 + t2) + pitch, 0, 0)

def base():
    return {n: (0.0, 0.0, 0.0) for n in rest}

def root(pose, dx=0, dy=0, dz=0):
    pose["@T"] = (ROOT[0] + dx, ROOT[1] + dy, ROOT[2] + dz)

# ---- arms: (shoulder X swing, shoulder out, elbow bend); R = the free hand, L = the arm cannon
def arms(pose, r_swing, r_out, r_elbow, l_swing, l_out, l_elbow):
    pose["R_upper-arm"] = (r_swing, 0, -r_out)
    pose["R_lower arm"] = (r_elbow, 0, 0)
    pose["L_upper-arm"] = (l_swing, 0, l_out)
    pose["ArmCanon"] = (l_elbow, 0, 0)

def idle(t, L=4.0):
    p = base(); s = math.sin(2 * math.pi * t / L)
    dy = -4 + 1.2 * s
    root(p, dy=dy)
    legs(p, dy, (2, 0, 0), (-2, 0, 0))
    p["Main_Torso"] = (-1.0 * s, 0, 0)
    p["Head"] = (1.5 * s, 0, 0)
    arms(p, 3 + 1.5 * s, 6, 10 + 2 * s, 12 + 1.0 * s, 4, 22)
    return p

def cycle(t, P, S, H, duty, bob, lean, arm_r, arm_l, elbow_r, elbow_l, sink):
    p = base(); ph = (t / P) % 1.0
    def foot(phase):
        if phase < duty:
            f = lerp(S / 2, -S / 2, phase / duty); u = 0
            pitch = -18 * ease((phase / duty - 0.75) / 0.25)            # heel lifts at the end
        else:
            s = (phase - duty) / (1 - duty)
            f = lerp(-S / 2, S / 2, ease(s)); u = H * math.sin(math.pi * s)
            pitch = lerp(-18, 12, ease(s * 1.4))                          # toes up for the heel strike
        return f, u, pitch
    lf, rf = foot(ph), foot((ph + 0.5) % 1.0)
    dy = -sink + bob * math.cos(4 * math.pi * (ph - duty / 2))
    root(p, dy=dy)
    p["Lower_torso"] = (0, 5 * math.sin(2 * math.pi * ph), 2 * math.cos(2 * math.pi * ph))
    legs(p, dy, lf, rf)
    sw = math.sin(2 * math.pi * ph)                    # + : left leg forward
    p["Main_Torso"] = (-lean, -7 * math.sin(2 * math.pi * ph), -1.5 * math.cos(2 * math.pi * ph))
    p["Head"] = (lean * 0.6, 3 * sw, 0)
    arms(p, arm_r * sw, 7, elbow_r + 6 * max(0, sw), -arm_l * sw + 8, 5, elbow_l)
    return p

def walk(t): return cycle(t, 1.1, 76, 16, 0.6, 2.5, 3, 20, 7, 14, 24, 7)
def run(t):  return cycle(t, 0.72, 128, 34, 0.38, 5, 12, 38, 14, 55, 50, 14)

def fall(t, L=0.8):
    p = base(); s = math.sin(2 * math.pi * t / L)
    root(p, dy=0)
    p["L_thigh"] = (38 + 4 * s, 0, 4); p["L_lower-leg"] = (-62 - 4 * s, 0, 0); p["L_foot"] = (10, 0, 0)
    p["R_thigh"] = (12 - 4 * s, 0, -4); p["R_lower-leg"] = (-34 + 4 * s, 0, 0); p["R_foot"] = (6, 0, 0)
    p["Main_Torso"] = (-6, 0, 0); p["Head"] = (8, 0, 0)
    arms(p, 25 + 5 * s, 28, 20, 30 - 4 * s, 22, 30)
    return p

def land(t):
    # 0: touching down, 0.12: deepest, held to 0.3, back up to idle by 1.1
    d = 48
    if t < 0.12: k = ease(t / 0.12)
    elif t < 0.3: k = 1
    else: k = 1 - ease((t - 0.3) / 0.8)
    i = idle(0)
    p = base()
    dy = lerp(-4, -d, k)
    root(p, dy=dy, dz=lerp(0, -6, k))
    legs(p, dy, (lerp(2, 10, k), 0, 0), (lerp(-2, -8, k), 0, 0), hip_dz=lerp(0, -6, k))
    p["Main_Torso"] = (lerp(i["Main_Torso"][0], -22, k), 0, 0)
    p["Head"] = (lerp(i["Head"][0], 16, k), 0, 0)
    arms(p, lerp(3, 18, k), lerp(6, 24, k), lerp(10, 26, k), lerp(12, 38, k), lerp(4, 10, k), lerp(22, 40, k))
    return p

def look(t, L=4.5):
    # head (and a bit of the chest) turns one way, then the other, then back
    keys = [(0, 0), (0.5, 0), (1.3, 38), (2.1, 38), (2.9, -32), (3.6, -32), (4.3, 0), (4.5, 0)]
    for (a, va), (b, vb) in zip(keys, keys[1:]):
        if a <= t <= b: yaw = lerp(va, vb, ease((t - a) / (b - a))); break
    else: yaw = 0
    p = idle(t)
    p["Head"] = (p["Head"][0] - 4 * abs(yaw) / 38, yaw * 0.75, 0)
    p["Main_Torso"] = (p["Main_Torso"][0], yaw * 0.25, 0)
    return p

CLIPS = [("idle", idle, 4.0), ("walk", walk, 1.1), ("run", run, 0.72), ("fall", fall, 0.8), ("land", land, 1.1), ("look", look, 4.5)]
FPS = 30
TICK = 46186158000

objs, conns = [], []
nid = [900000000]
def new_id(): nid[0] += 1; return nid[0]
def arr(name, vals, ind):
    return f'{ind}{name}: *{len(vals)} {{\n{ind}\ta: ' + ",".join(vals) + f'\n{ind}}}\n'

for cname, fn, length in CLIPS:
    n = int(round(length * FPS))
    times = [length * i / n for i in range(n + 1)]
    poses = [fn(t) for t in times]
    sid, lid = new_id(), new_id()
    objs.append(f'\tAnimationStack: {sid}, "AnimStack::{cname}", "" {{\n\t\tProperties70:  {{\n'
                f'\t\t\tP: "LocalStop", "KTime", "Time", "",{int(length * TICK)}\n'
                f'\t\t\tP: "ReferenceStop", "KTime", "Time", "",{int(length * TICK)}\n\t\t}}\n\t}}\n')
    objs.append(f'\tAnimationLayer: {lid}, "AnimLayer::{cname}", "" {{\n\t}}\n')
    conns.append(f'\t;AnimLayer::{cname}, AnimStack::{cname}\n\tC: "OO",{lid},{sid}\n')
    kt = [str(int(t * TICK)) for t in times]
    for node in rest:
        for prop, key in (("Lcl Rotation", None), ("Lcl Translation", "@T")):
            if prop == "Lcl Translation" and node != "Lower_torso": continue
            vals = [(p[key] if key else p[node]) for p in poses]
            cid = new_id()
            v0 = vals[0]
            objs.append(f'\tAnimationCurveNode: {cid}, "AnimCurveNode::{"T" if key else "R"}", "" {{\n\t\tProperties70:  {{\n'
                        f'\t\t\tP: "d|X", "Number", "", "A",{v0[0]}\n\t\t\tP: "d|Y", "Number", "", "A",{v0[1]}\n\t\t\tP: "d|Z", "Number", "", "A",{v0[2]}\n\t\t}}\n\t}}\n')
            conns.append(f'\tC: "OO",{cid},{lid}\n\tC: "OP",{cid},{rest[node][0]}, "{prop}"\n')
            for ax in range(3):
                vid = new_id()
                kv = [f"{v[ax]:.4f}" for v in vals]
                objs.append(f'\tAnimationCurve: {vid}, "AnimCurve::", "" {{\n\t\tDefault: 0\n\t\tKeyVer: 4008\n'
                            + arr("KeyTime", kt, "\t\t") + arr("KeyValueFloat", kv, "\t\t")
                            + f'\t\tKeyAttrFlags: *1 {{\n\t\t\ta: 24836\n\t\t}}\n\t\tKeyAttrDataFloat: *4 {{\n\t\t\ta: 0,0,255790911,0\n\t\t}}\n'
                            + f'\t\tKeyAttrRefCount: *1 {{\n\t\t\ta: {len(kt)}\n\t\t}}\n\t}}\n')
                conns.append(f'\tC: "OP",{vid},{cid}, "d|{"XYZ"[ax]}"\n')

# Objects block ends right before the connections comment; Connections block is the last "}" of its section
i = text.index("; Object connections")
j = text.rindex("}", 0, i)
text = text[:j] + "".join(objs) + text[j:]
k = text.index("Connections:")
e = text.index("\n}", k)
text = text[:e] + "\n" + "".join(conns).rstrip("\n") + text[e:]
open(DST, "w", encoding="utf-8").write(text)
print("clips:", ", ".join(c[0] for c in CLIPS))
