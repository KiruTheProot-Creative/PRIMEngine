#!/usr/bin/env python3
"""Bakes a Blockbench ASCII FBX (meshes + bone animation) into a small JSON file
that the PRIMEngine renderer reads directly. Also extracts the embedded texture.
usage: bake_fbx.py input.fbx out_dir
"""
import sys, re, json, base64, os

KTIME = 46186158000.0  # FBX ticks per second


def split_props(s):
    out, cur, q = [], "", False
    for ch in s:
        if ch == '"':
            q = not q
            cur += ch
        elif ch == "," and not q:
            out.append(cur.strip()); cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    res = []
    for p in out:
        if p.startswith('"') and p.endswith('"'):
            res.append(p[1:-1])
        else:
            try:
                res.append(float(p) if ("." in p or "e" in p.lower()) else int(p))
            except ValueError:
                res.append(p)
    return res


class Node:
    def __init__(self, name, props):
        self.name, self.props, self.kids = name, props, []

    def get(self, name):
        for k in self.kids:
            if k.name == name:
                return k
        return None

    def all(self, name):
        return [k for k in self.kids if k.name == name]


def parse(text):
    root = Node("root", [])
    stack = [root]
    for raw in text.split("\n"):
        line = raw.strip()
        if not line or line.startswith(";"):
            continue
        if line == "}":
            stack.pop(); continue
        m = re.match(r'^([^:]+):\s*(.*)$', line)
        if not m:
            continue
        name, rest = m.group(1).strip(), m.group(2)
        opens = rest.endswith("{")
        if opens:
            rest = rest[:-1].strip()
        if name == "a":  # array payload
            stack[-1].props = [float(x) for x in rest.split(",") if x.strip() != ""]
            continue
        node = Node(name, [] if name == "Content" else split_props(rest))
        if name == "Content":
            node.props = [rest.strip().strip('"')]
        stack[-1].kids.append(node)
        if opens:
            stack.append(node)
    return root


def mat_mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def main(src, outdir):
    os.makedirs(outdir, exist_ok=True)
    tree = parse(open(src, encoding="utf-8").read())
    objs = tree.get("Objects")
    conns = [(k.props[1], k.props[2], k.props[0], (k.props[3] if len(k.props) > 3 else None))
             for k in tree.get("Connections").all("C")]

    models, geoms, curvenodes, curves, stacks, layers = {}, {}, {}, {}, {}, {}
    for o in objs.kids:
        oid = o.props[0] if o.props else None
        nm = o.props[1].split("::")[-1] if len(o.props) > 1 else ""
        if o.name == "Model": models[oid] = (nm, o)
        elif o.name == "Geometry": geoms[oid] = o
        elif o.name == "AnimationCurveNode": curvenodes[oid] = o
        elif o.name == "AnimationCurve": curves[oid] = o
        elif o.name == "AnimationStack": stacks[oid] = (nm, o)
        elif o.name == "AnimationLayer": layers[oid] = (nm, o)

    # ---- texture
    for o in objs.kids:
        if o.name == "Video":
            c = o.get("Content")
            if c and c.props and c.props[0]:
                png = base64.b64decode(c.props[0])
                open(os.path.join(outdir, "canon.png"), "wb").write(png)
                print("texture bytes", len(png))
                break

    parent = {}
    for a, b, _, prop in conns:
        if a in models and (b in models or b == 0):
            parent[a] = b

    def lcl(o, key, default):
        pr = o.get("Properties70")
        if pr:
            for p in pr.all("P"):
                if p.props and p.props[0] == key:
                    return p.props[-3:]
        return default

    order = []
    def visit(mid):
        order.append(mid)
        for c in [m for m in models if parent.get(m) == mid]:
            visit(c)
    for r in [m for m in models if parent.get(m) == 0]:
        visit(r)
    index = {mid: i for i, mid in enumerate(order)}

    geom_of = {}
    for a, b, _, _ in conns:
        if a in geoms and b in models:
            geom_of[b] = a

    def build_mesh(g):
        verts = g.get("Vertices").props
        pvi = [int(x) for x in g.get("PolygonVertexIndex").props]
        nrm = g.get("LayerElementNormal").get("Normals").props
        uvn = g.get("LayerElementUV")
        uv = uvn.get("UV").props
        uvi = uvn.get("UVIndex").props if uvn.get("UVIndex") else None
        pos, nor, uvs = [], [], []
        poly, pcount, corner = [], 0, 0
        polys = []
        cur = []
        for i, v in enumerate(pvi):
            end = v < 0
            vi = (-v - 1) if end else v
            cur.append((vi, i))
            if end:
                polys.append(cur); cur = []
        for pi, poly in enumerate(polys):
            n = nrm[pi * 3: pi * 3 + 3]
            for t in range(1, len(poly) - 1):
                for (vi, ci) in (poly[0], poly[t], poly[t + 1]):
                    pos += [round(verts[vi * 3 + j], 4) for j in range(3)]
                    nor += list(n)
                    ui = int(uvi[ci]) if uvi else ci
                    uvs += [round(uv[ui * 2], 5), round(1.0 - uv[ui * 2 + 1], 5)]
        return {"p": pos, "n": nor, "u": uvs}

    nodes = []
    for mid in order:
        nm, o = models[mid]
        node = {
            "name": nm,
            "parent": index[parent[mid]] if parent.get(mid) in index else -1,
            "t": lcl(o, "Lcl Translation", [0, 0, 0]),
            "r": lcl(o, "Lcl Rotation", [0, 0, 0]),
            "s": lcl(o, "Lcl Scaling", [1, 1, 1]),
        }
        if mid in geom_of:
            node["mesh"] = build_mesh(geoms[geom_of[mid]])
        nodes.append(node)

    # ---- animation
    curve_by = {}
    for a, b, _, prop in conns:
        if a in curves and b in curvenodes and prop:
            curve_by[(b, prop)] = a
    target = {}
    for a, b, _, prop in conns:
        if a in curvenodes and b in models:
            target[a] = (b, prop)
    layer_nodes = {}
    for a, b, _, _ in conns:
        if a in curvenodes and b in layers:
            layer_nodes.setdefault(b, []).append(a)
    stack_layer = {}
    for a, b, _, _ in conns:
        if a in layers and b in stacks:
            stack_layer[b] = a

    anims = {}
    for sid, (sname, so) in stacks.items():
        dur = 0.0
        pr = so.get("Properties70")
        if pr:
            for p in pr.all("P"):
                if p.props[0] == "LocalStop":
                    dur = p.props[-1] / KTIME
        tracks = []
        for cn in layer_nodes.get(stack_layer.get(sid), []):
            mid, prop = target.get(cn, (None, None))
            if mid not in index: continue
            key = {"Lcl Translation": 0, "Lcl Rotation": 1, "Lcl Scaling": 2}.get(prop)
            if key is None: continue
            ch = []
            for ax in ("d|X", "d|Y", "d|Z"):
                cid = curve_by.get((cn, ax))
                if cid is None:
                    ch.append(None); continue
                c = curves[cid]
                ts = [round(t / KTIME, 5) for t in c.get("KeyTime").props]
                vs = [round(v, 4) for v in c.get("KeyValueFloat").props]
                ch.append({"t": ts, "v": vs})
                dur = max(dur, ts[-1])
            tracks.append({"n": index[mid], "p": key, "c": ch})
        anims[sname] = {"dur": round(dur, 4), "tracks": tracks}

    out = {"nodes": nodes, "anims": anims}
    json.dump(out, open(os.path.join(outdir, "arm.json"), "w"), separators=(",", ":"))
    print("nodes", len(nodes), "meshes", sum(1 for n in nodes if "mesh" in n))
    for k, v in anims.items():
        print(" anim", repr(k), v["dur"], "s", len(v["tracks"]), "tracks")
    print("size", os.path.getsize(os.path.join(outdir, "arm.json")))


main(sys.argv[1], sys.argv[2])
