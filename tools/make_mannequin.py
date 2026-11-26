# A plain test figure for screenshots and tests: grey boxes on the same bones (groups) as a Blockbench humanoid suit
# (Lower_torso, L_thigh, ... ArmCanon, Head), so tools/fbx_animate_humanoid.py can animate it.
# Writes an ASCII FBX 7.3 like Blockbench's exporter: Y up, the figure faces -Z, centimetres.
import sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "Mannequin.fbx"

# bone: (parent, rest translation from the parent)
BONES = [
    ("Lower_torso", None, (0, 168.75, 6.25)),
    ("L_thigh", "Lower_torso", (19.375, -21.875, 0.625)),
    ("L_lower-leg", "L_thigh", (-0.469, -56.25, -0.625)),
    ("L_foot", "L_lower-leg", (-0.156, -71.875, 0)),
    ("R_thigh", "Lower_torso", (-19.375, -21.875, 0.625)),
    ("R_lower-leg", "R_thigh", (0.469, -56.25, -0.625)),
    ("R_foot", "R_lower-leg", (0.156, -71.875, 0)),
    ("Main_Torso", "Lower_torso", (0, 31.25, 0)),
    ("Shoulder_R", "Main_Torso", (-33.75, 25, 3.125)),
    ("R_upper-arm", "Shoulder_R", (-27.5, -9.375, 0)),
    ("R_lower arm", "R_upper-arm", (0.625, -43.75, 0)),
    ("R_Hand", "R_lower arm", (-0.625, -46.875, -0.625)),
    ("Shoulder_L", "Main_Torso", (37.5, 21.875, 3.125)),
    ("L_upper-arm", "Shoulder_L", (24.375, -6.25, -0.625)),
    ("ArmCanon", "L_upper-arm", (0, -43.75, 0)),
    ("Head", "Main_Torso", (0.625, 46.25, 0)),
]
GREY, DARK, LIGHT, VISOR = (0.62, 0.65, 0.70), (0.30, 0.32, 0.36), (0.80, 0.82, 0.85), (0.25, 0.85, 0.75)
# boxes in their bone's space: (bone, (x0, y0, z0), (x1, y1, z1), colour)
BOXES = [
    ("Lower_torso", (-30, -32, -16), (30, 6, 16), DARK),
    ("L_thigh", (-12, -54, -12), (12, 0, 12), GREY), ("R_thigh", (-12, -54, -12), (12, 0, 12), GREY),
    ("L_lower-leg", (-11, -70, -11), (11, 0, 11), LIGHT), ("R_lower-leg", (-11, -70, -11), (11, 0, 11), LIGHT),
    ("L_foot", (-12, -18, -30), (12, 2, 12), DARK), ("R_foot", (-12, -18, -30), (12, 2, 12), DARK),
    ("Main_Torso", (-30, 0, -18), (30, 50, 18), GREY), ("Main_Torso", (-24, 8, 18), (24, 44, 30), DARK),
    ("Shoulder_R", (-18, -12, -18), (14, 14, 18), LIGHT), ("Shoulder_L", (-14, -12, -18), (18, 14, 18), LIGHT),
    ("R_upper-arm", (-9, -44, -9), (9, 0, 9), GREY), ("R_lower arm", (-9, -46, -9), (9, 0, 9), LIGHT), ("R_Hand", (-8, -18, -8), (8, 0, 8), DARK),
    ("L_upper-arm", (-10, -44, -10), (10, 0, 10), GREY), ("ArmCanon", (-15, -80, -15), (15, 0, 15), DARK),
    ("Head", (-20, -4, -21), (20, 38, 20), LIGHT), ("Head", (-15, 12, -25), (15, 26, -20), VISOR),
]

ids = {}
nid = [100000]
def new_id():
    nid[0] += 1; return nid[0]
for b, _, _ in BONES: ids[b] = new_id()

objs, conns = [], []
def model(i, name, kind, t):
    return (f'\tModel: {i}, "Model::{name}", "{kind}" {{\n\t\tVersion: 232\n\t\tProperties70:  {{\n'
            f'\t\t\tP: "RotationOrder", "enum", "", "",0\n'
            f'\t\t\tP: "Lcl Translation", "Lcl Translation", "", "A",{t[0]},{t[1]},{t[2]}\n'
            f'\t\t\tP: "Lcl Rotation", "Lcl Rotation", "", "A",0,0,0\n'
            f'\t\t\tP: "Lcl Scaling", "Lcl Scaling", "", "A",1,1,1\n\t\t}}\n\t\tShading: T\n\t\tCulling: "CullingOff"\n\t}}\n')
for b, parent, t in BONES:
    objs.append(model(ids[b], b, "Null", t))
    conns.append(f'\tC: "OO",{ids[b]},{ids[parent] if parent else 0}\n')
mats = {}
for c in (GREY, DARK, LIGHT, VISOR):
    mid = new_id(); mats[c] = mid
    objs.append(f'\tMaterial: {mid}, "Material::m{mid}", "" {{\n\t\tVersion: 102\n\t\tShadingModel: "lambert"\n\t\tProperties70:  {{\n'
                f'\t\t\tP: "DiffuseColor", "Color", "", "A",{c[0]},{c[1]},{c[2]}\n\t\t}}\n\t}}\n')
# a box: 8 corners, 6 faces (counter-clockwise from outside), normals per corner
FACES = [((0, 2, 3, 1), (0, 0, -1)), ((4, 5, 7, 6), (0, 0, 1)), ((0, 1, 5, 4), (0, -1, 0)), ((2, 6, 7, 3), (0, 1, 0)), ((0, 4, 6, 2), (-1, 0, 0)), ((1, 3, 7, 5), (1, 0, 0))]
for k, (bone, lo, hi, col) in enumerate(BOXES):
    corners = [(hi[0] if i & 1 else lo[0], hi[1] if i & 2 else lo[1], hi[2] if i & 4 else lo[2]) for i in range(8)]
    pvi, nrm = [], []
    for f, n in FACES:
        pvi += [f[0], f[1], f[2], -f[3] - 1]; nrm += list(n) * 4
    gid, mid = new_id(), new_id()
    objs.append(f'\tGeometry: {gid}, "Geometry::box{k}", "Mesh" {{\n'
                f'\t\tVertices: *24 {{\n\t\t\ta: ' + ",".join(f"{v}" for c in corners for v in c) + '\n\t\t}\n'
                f'\t\tPolygonVertexIndex: *24 {{\n\t\t\ta: ' + ",".join(map(str, pvi)) + '\n\t\t}\n\t\tGeometryVersion: 124\n'
                f'\t\tLayerElementNormal: 0 {{\n\t\t\tVersion: 101\n\t\t\tName: ""\n\t\t\tMappingInformationType: "ByPolygonVertex"\n\t\t\tReferenceInformationType: "Direct"\n'
                f'\t\t\tNormals: *72 {{\n\t\t\t\ta: ' + ",".join(map(str, nrm)) + '\n\t\t\t}\n\t\t}\n'
                f'\t\tLayerElementMaterial: 0 {{\n\t\t\tVersion: 101\n\t\t\tName: ""\n\t\t\tMappingInformationType: "AllSame"\n\t\t\tReferenceInformationType: "IndexToDirect"\n'
                f'\t\t\tMaterials: *1 {{\n\t\t\t\ta: 0\n\t\t\t}}\n\t\t}}\n'
                f'\t\tLayer: 0 {{\n\t\t\tVersion: 100\n\t\t\tLayerElement:  {{\n\t\t\t\tType: "LayerElementNormal"\n\t\t\t\tTypedIndex: 0\n\t\t\t}}\n'
                f'\t\t\tLayerElement:  {{\n\t\t\t\tType: "LayerElementMaterial"\n\t\t\t\tTypedIndex: 0\n\t\t\t}}\n\t\t}}\n\t}}\n')
    objs.append(model(mid, f"box{k}", "Mesh", (0, 0, 0)))
    conns.append(f'\tC: "OO",{mid},{ids[bone]}\n\tC: "OO",{gid},{mid}\n\tC: "OO",{mats[col]},{mid}\n')

head = '''; FBX 7.3.0 project file
; A plain test figure (tools/make_mannequin.py)
; ----------------------------------------------------

FBXHeaderExtension:  {
	FBXHeaderVersion: 1003
	FBXVersion: 7300
	Creator: "PRIMEngine tools"
}
GlobalSettings:  {
	Version: 1000
	Properties70:  {
		P: "UpAxis", "int", "Integer", "",1
		P: "UpAxisSign", "int", "Integer", "",1
		P: "FrontAxis", "int", "Integer", "",2
		P: "FrontAxisSign", "int", "Integer", "",1
		P: "CoordAxis", "int", "Integer", "",0
		P: "CoordAxisSign", "int", "Integer", "",1
		P: "UnitScaleFactor", "double", "Number", "",1
	}
}

; Object properties
;------------------------------------------------------------------

Objects:  {
'''
text = head + "".join(objs) + "}\n\n; Object connections\n;------------------------------------------------------------------\n\nConnections:  {\n" + "".join(conns) + "}\n"
open(OUT, "w").write(text)
print("wrote", OUT)
