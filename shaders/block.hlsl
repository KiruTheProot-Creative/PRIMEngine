// block.hlsl : world blocks (camera-relative positions), shadows, sky and the final picture.
// Milestone 4 look (towards "Complementary"): linear-light HDR, sunlight with soft shadows, sky light + corner
// shading, an atmospheric sky with a sun disc and glow, fog that takes the sky's colour, filmic tonemapping.

cbuffer Frame : register(b0)
{
    row_major float4x4 gViewProj;   // camera at the origin (positions are relative to the camera)
    row_major float4x4 gShadowVP;   // camera-relative position -> shadow map
    float4 gSunDir;                 // xyz: towards the sun (or the moon at night), w: shadows on (1) / off (0)
    float4 gSunColor;               // rgb: direct light
    float4 gAmbient;                // rgb: light from the sky
    float4 gFogRange;               // x start, y end (blocks), z exposure
    float4 gSkyTop;                 // rgb
    float4 gSkyHorizon;             // rgb
    float4 gSunGlow;                // rgb: colour of the glow around the sun, w: sun disc brightness
    float4 gCamF, gCamR, gCamU;     // camera basis (sky)
    float4 gProj;                   // x tan(fov/2), y aspect
    float4 gShadowInfo;             // x 1/size, y range (blocks), z normal offset, w filter radius (texels)
    float4 gCamPos;                 // camera position (x and z wrapped every 14400 blocks, so the numbers stay small)
    float4 gVolume;                 // x cloud cover (0 = no clouds), y ground fog density, z time (s), w cloud shadows on
    float4 gDepth;                  // x near, y far plane, z/w picture size in pixels
    float4 gExtra;                  // x night (0..1, stars), y bloom amount, z wind (sway), w lamp brightness
    row_major float4x4 gSkyVP;      // camera-relative position -> "what is above" map (seen from straight above)
    float4 gWeather;                // x rain, y snow, z storm clouds (dark), w wet surfaces
    float4 gWeather2;               // x snow cover, y lightning flash, z wind, w "what is above" map ready (1)
    float4 gEnv;                    // x air (1) / vacuum (0), y heat shimmer, z planet in the sky, w haze (dust everywhere)
    float4 gEnvPart;                // things floating in the air: x kind (2 dust, 3 embers/spores, 4 ice sparkles), y amount
    float4 gEnvPartColor;           // rgb
    float4 gRefl;                   // reflections: x on (1) / off (0), y ray steps, z how far rays go (blocks)
    float4 gClipPlane;              // geometry with dot(rel, xyz) + w < 0 is cut away (only while drawing a mirror's view)
    float4 gMirror;                 // the mirror drawn this frame: plane (xyz normal, w); gRefl.w = 1 when there is one
    float4 gAOSet;                  // ambient occlusion: x on (1) / off (0), y radius (blocks), z strength, w samples
    float4 gShadow2;                // shadow edges: x 0 sharp / 1 smooth / 2 soft like sunlight, y softness
    float4 gSurf;                   // x generated normals (0 off), y coated textures (0 off), z texture materials on (1)
    row_major float4x4 gCascVP[4];  // the four sun shadow maps (cascades): camera-relative position -> that map, nearest first
    float4 gCascR;                  // how far each reaches (blocks)
    float4 gCascTexel;              // one shadow pixel in blocks, per cascade
    float4 gPrecip;                 // falling things: x their own clock (s, steady), yz how far the wind has carried them so far (blocks), w -
    row_major float4x4 gOccVP;      // camera-relative position -> the "roofs" map (opaque things seen from straight above)
    float4 gOcc;                    // x true darkness on (sky light only where the sky is), y how open the sky is around the camera (0..1)
    float4 gLookFog;                // the look's air: x fog thickness, y height (world Y) it thins above, z over how many blocks (0: everywhere), w lights in the air
    float4 gLookFogCol;             // rgb its colour (linear), w how much it glows by itself
};
float lookFogAt(float y) { return gLookFog.x * (gLookFog.z > 0.01 ? exp(-max(y - gLookFog.y, 0) / gLookFog.z) : 1); }
cbuffer ChunkCB : register(b1)
{
    float4 gOffset;                 // where the mesh's (0,0,0) is, relative to the camera (a chunk's origin)
    float4 gAxisX, gAxisY, gAxisZ;  // the mesh's axes: objects are turned and sized; 1,0,0 / 0,1,0 / 0,0,1 for the world's chunks
};
// lights placed in the world (lamps, strips, glowing emitters, shots ...): hundreds of them, sorted into "clusters" -
// little boxes of the view (a grid across the screen, sliced by distance) - so each pixel only looks at the lights near it
struct LightData
{
    float4 pos;   // xyz relative to the camera, w reach (blocks)
    float4 col;   // rgb colour x brightness, w kind: 0 point, 1 spot, 2 strip; +8: only for objects (blocks have that light already)
    float4 dir;   // spotlights: xyz where it points, w cos(cone)
    float4 ext;   // strips: xyz half the tube (from its middle to one end), w its shadow slot (-1: none)
};
struct ShadowVP { float4 r0, r1, r2, r3; };
SamplerComparisonState gShadowSamp : register(s1);
StructuredBuffer<LightData> gLightBuf : register(t19);
StructuredBuffer<uint2> gClusters : register(t20);      // per cluster: where its list starts, how many
StructuredBuffer<uint> gClusterIdx : register(t21);     // the lists (light numbers)
Texture2DArray<float> gLightShadow : register(t22);     // shadows of a few lights: 6 slices each (a cube; spotlights use the first)
StructuredBuffer<ShadowVP> gLightShadowVP : register(t23);
cbuffer ClusterCB : register(b11)
{
    float4 gCl0;   // x 1 = use the clusters, 0 = just go through a list; y, z grid across / down; w distance slices
    float4 gCl1;   // x list start, y list length; z nearest slice (blocks), w log(farthest / nearest)
};
uint clusterOf(float3 rel)
{
    float z = dot(rel, gCamF.xyz), zz = max(z, 1e-3);
    float2 ndc = float2(dot(rel, gCamR.xyz) / (zz * gProj.x * gProj.y), dot(rel, gCamU.xyz) / (zz * gProj.x));
    float2 uv = saturate(float2(ndc.x, -ndc.y) * 0.5 + 0.5);
    uint gx = (uint)gCl0.y, gy = (uint)gCl0.z, gz = (uint)gCl0.w;
    uint cx = min((uint)(uv.x * gx), gx - 1), cy = min((uint)(uv.y * gy), gy - 1);
    uint cz = (uint)clamp(log(max(z, gCl1.z) / gCl1.z) / gCl1.w * gz, 0, gz - 1);
    return (cz * gy + cy) * gx + cx;
}
float lightShadow(LightData L, float3 p, int type)
{
    int slot = (int)L.ext.w; if (slot < 0) return 1;
    int face = 0;
    if (type != 1) {   // a cube: which side
        float3 v = p - L.pos.xyz, a = abs(v);
        face = a.x >= a.y && a.x >= a.z ? (v.x > 0 ? 0 : 1) : a.y >= a.z ? (v.y > 0 ? 2 : 3) : (v.z > 0 ? 4 : 5);
    }
    int sl = slot * 6 + face; ShadowVP m = gLightShadowVP[sl];
    float4 s = mul(float4(p, 1), float4x4(m.r0, m.r1, m.r2, m.r3));
    if (s.w <= 1e-4) return 1;
    float3 q = s.xyz / s.w; float2 uv = float2(q.x, -q.y) * 0.5 + 0.5;
    if (any(uv < 0) || any(uv > 1) || q.z > 1) return 1;
    const float t = 1.0 / 512; float z = q.z - 0.00025, r = 0;
    r += gLightShadow.SampleCmpLevelZero(gShadowSamp, float3(uv + float2(-0.5, -0.5) * t, sl), z);
    r += gLightShadow.SampleCmpLevelZero(gShadowSamp, float3(uv + float2(0.5, -0.5) * t, sl), z);
    r += gLightShadow.SampleCmpLevelZero(gShadowSamp, float3(uv + float2(-0.5, 0.5) * t, sl), z);
    r += gLightShadow.SampleCmpLevelZero(gShadowSamp, float3(uv + float2(0.5, 0.5) * t, sl), z);
    return r * 0.25;
}
void evalLight(LightData L, float3 rel, float3 n, float3 v, float pw, bool object, inout float3 dif, inout float3 spec)
{
    int kind = (int)round(L.col.w); bool objOnly = kind >= 8; int type = kind & 7;
    if (objOnly && !object) return;
    float3 lp = L.pos.xyz;
    if (type == 2) { float3 a = lp - L.ext.xyz, ab = L.ext.xyz * 2; lp = a + ab * saturate(dot(rel - a, ab) / max(dot(ab, ab), 1e-4)); }   // a tube: its nearest point
    float3 Lv = lp - rel; float d = length(Lv); float range = L.pos.w;
    if (d >= range) return;
    Lv /= max(d, 1e-4);
    float att = 1 - d / range; att *= att;
    if (type == 1) { float c = dot(-Lv, L.dir.xyz), c0 = L.dir.w; att *= smoothstep(c0, c0 + (1 - c0) * 0.35, c); }
    if (att <= 0) return;
    if (L.ext.w >= 0) att *= lightShadow(L, rel + n * (0.03 + d * 0.012), type);
    float3 c = L.col.rgb * att;
    dif += c * saturate(dot(n, Lv) * 0.85 + 0.15);   // a little light wraps round edges, softer look
    float3 h = normalize(Lv + v);
    spec += c * pow(saturate(dot(n, h)), pw) * (pw + 8) / 25 * saturate(dot(n, Lv));
}
float3 dynLightsSpec(float3 rel, float3 n, float3 v, float pw, bool object, out float3 spec)
{
    float3 dif = 0; spec = 0;
    if (gCl0.x > 0.5) {
        uint2 cl = gClusters[clusterOf(rel)];
        [loop] for (uint k = 0; k < cl.y; k++) evalLight(gLightBuf[gClusterIdx[cl.x + k]], rel, n, v, pw, object, dif, spec);
    } else {
        uint s0 = (uint)gCl1.x, e = s0 + (uint)gCl1.y;
        [loop] for (uint k = s0; k < e; k++) evalLight(gLightBuf[k], rel, n, v, pw, object, dif, spec);
    }
    return dif;
}
float3 dynLights(float3 rel, float3 n) { float3 sp; return dynLightsSpec(rel, n, -normalize(rel), 16, true, sp); }
float3 meshToRel(float3 p) { return gOffset.xyz + gAxisX.xyz * p.x + gAxisY.xyz * p.y + gAxisZ.xyz * p.z; }

Texture2DArray gTex : register(t0);
Texture2DArray<float> gShadow : register(t1);   // one slice per cascade
Texture2D<float> gSceneDepth : register(t2);
Texture3D<float2> gNoise : register(t3);       // tiling cloud noise: x soft billows, y puffy cells
SamplerState gSamp : register(s0);
Texture2D gMeshTex : register(t9);              // an imported model's own picture (its vertices use layer 16383)
// materials per texture layer: a = shine, metal, glow, glow only above this brightness; b = bumps, coat, emission map brightness (0: none)
struct TexMat { float4 a; float4 b; };
StructuredBuffer<TexMat> gMats : register(t10);
Texture2DArray gEmitTex : register(t14);        // emission maps, one per texture layer (black: nothing glows)
Texture2D gMeshEmit : register(t15);            // the emission map of the model being drawn
cbuffer MeshMatCB : register(b4) { float4 gMM0; float4 gMM1; };   // the material of the model being drawn (same numbers as a texture's)
float4 albedoAt(float3 uvl) { return uvl.z > 16000 ? gMeshTex.Sample(gSamp, uvl.xy) : gTex.Sample(gSamp, uvl); }
SamplerState gLinear : register(s2);
Texture2D<float> gSkyMap : register(t4);

// is this point under the open sky? (rain falls on it, snow settles on it)
float skyExposed(float3 rel)
{
    if (gWeather2.w < 0.5) return 1;
    float4 s = mul(float4(rel, 1), gSkyVP);
    float2 uv = float2(s.x, -s.y) * 0.5 + 0.5;
    if (any(uv < 0) || any(uv > 1)) return 1;
    return gSkyMap.SampleCmpLevelZero(gShadowSamp, uv, s.z - 0.003);
}
// true darkness: how much sky light reaches a spot. The "roofs" map (opaque things seen from above, around you) says whether
// anything is overhead; a few spots around it too, so light fades in near openings and cave mouths instead of a hard edge
Texture2D<float> gSkyOcc : register(t18);
float skyOpenAt(float3 rel)
{
    float4 s = mul(float4(rel, 1), gOccVP);
    float2 uv = float2(s.x, -s.y) * 0.5 + 0.5;
    if (any(uv < 0) || any(uv > 1)) return 1;
    return gSkyOcc.SampleCmpLevelZero(gShadowSamp, uv, s.z - 0.0015);
}
float skyVis(float3 rel, float3 n)
{
    if (gOcc.x < 0.5) return 1;
    float3 p = rel + n * 0.55 + float3(0, 0.05, 0);
    float s = skyOpenAt(p) * 2, w = 2;
    [unroll] for (int k = 0; k < 8; k++)
    {
        float a = k * 0.785398 + 0.39; float3 d = float3(cos(a), 0, sin(a));
        s += skyOpenAt(p + d * 1.6) + skyOpenAt(p + d * 4.0) * 0.5; w += 1.5;
    }
    return saturate(s / w * 1.6);
}
// the height (camera-relative) of the first thing under the open sky at rel.xz: where rain lands (known: inside the map)
float groundBelow(float3 rel, out bool known)
{
    known = false;
    if (gWeather2.w < 0.5) return -1e5;
    float4 s = mul(float4(rel, 1), gSkyVP);
    float2 uv = float2(s.x, -s.y) * 0.5 + 0.5;
    if (any(uv < 0.002) || any(uv > 0.998)) return -1e5;
    float d = gSkyMap.Load(int3((int2)(uv * 512), 0));
    if (d >= 0.9999) return -1e5;                                                   // nothing under it (the void)
    known = true;
    return (d - rel.x * gSkyVP[0][2] - rel.z * gSkyVP[2][2] - gSkyVP[3][2]) / gSkyVP[1][2];   // depth back to a height (the map looks straight down)
}
// rain on a surface: little rings spreading from where drops land (slope to bend the normal), wp: world position
float2 rainRipples(float2 wp, float t)
{
    float2 slope = 0;
    [unroll] for (int layer = 0; layer < 2; layer++)
    {
        float2 q = wp * (layer == 0 ? 1.6 : 2.3) + layer * 17.3;
        float2 cell = floor(q);
        [unroll] for (int oy = -1; oy <= 1; oy++) [unroll] for (int ox = -1; ox <= 1; ox++)
        {
            float2 c = cell + float2(ox, oy);
            float3 h = frac(sin(float3(dot(c, float2(127.1, 311.7)), dot(c, float2(269.5, 183.3)), dot(c, float2(419.2, 371.9)))) * 43758.5453);
            float ph = frac(t * (0.9 + h.z * 0.6) + h.x * 7);                       // each cell: a new drop every second or so
            float2 dv = q - (c + 0.15 + h.xy * 0.7); float d = length(dv);
            float r = ph * 0.75, w = d - r;                                          // the ring grows ...
            float ring = sin(w * 38) * exp(-w * w * 160) * (1 - ph) * (1 - ph);       // ... and fades
            slope += (d > 1e-4 ? dv / d : 0) * ring;
        }
    }
    return slope * 0.22;
}

// ---------------------------------------------------------------- clouds (a layer high above the world)
static const float kCloudBottom = 190, kCloudTop = 290;
float3 wind() { return float3(gVolume.z * 3.0, 0, gVolume.z * 1.2); }
float cloudDensity(float3 p, bool detail)   // p: world position
{
    float hf = saturate((p.y - kCloudBottom) / (kCloudTop - kCloudBottom));
    float2 n = gNoise.SampleLevel(gLinear, (p + wind()) / 1600.0, 0);
    float base = n.x * 0.6 + n.y * 0.4;
    float profile = saturate(hf * 6) * saturate((1 - hf) * 2.5);                     // flat bottoms, thinning towards the top
    float cover = gVolume.x;
    float d = saturate((base - (1 - cover) - hf * 0.12) / max(cover, 0.05) * 1.8) * profile;
    if (detail && d > 0)
    {
        float e = gNoise.SampleLevel(gLinear, (p + wind() * 1.6) / 180.0, 0).y;
        d = saturate(d - (1 - e) * 0.5 * (1 - d));                                 // eroded, wispy edges
    }
    return d * 0.06 * (1 + gWeather.z * 1.5);                                     // extinction per block (storm clouds are thicker)
}
float phaseHG(float c, float g) { float g2 = g * g; return (1 - g2) / (12.566 * pow(max(1 + g2 - 2 * g * c, 1e-4), 1.5)); }
// how much sunlight gets through the clouds to a point below them (cloud shadows on the ground)
float cloudShadow(float3 rel)
{
    if (gVolume.x <= 0 || gVolume.w < 0.5 || gSunDir.y < 0.05) return 1;
    float3 p = gCamPos.xyz + rel;
    float t = (kCloudBottom + 35 - p.y) / gSunDir.y;
    return exp(-cloudDensity(p + gSunDir.xyz * t, false) * 90);
}

struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; float layer : TEXCOORD1; uint lit : TEXCOORD2; uint normal : TEXCOORD3; };
struct VSOut { float4 pos : SV_Position; float3 uvl : TEXCOORD0; float ao : TEXCOORD1; float3 rel : TEXCOORD2; float3 n : TEXCOORD3; float4 lamp : TEXCOORD4; float2 mat : TEXCOORD5; float kind : TEXCOORD6; float clip : SV_ClipDistance0; };
// what the reflection pass needs per pixel: direction of the surface (rgb) and how shiny it is (a)
// color: the lit picture; surface: direction + shininess (reflections); indirect: the part of the light that
// comes from the sky and lamps (not the sun), which ambient occlusion takes away in corners
struct PSOut { float4 color : SV_Target0; float4 surface : SV_Target1; float4 indirect : SV_Target2; };

float3 unpackNormal(uint n) { return normalize(float3(n & 255, (n >> 8) & 255, (n >> 16) & 255) / 255.0 * 2 - 1); }

// leaves and plants sway: position relative to the camera in, moved position out (layer carries the sway kind)
float3 sway(float3 rel, float layer, float v, out float realLayer)
{
    float kind = floor(layer / 16384);
    realLayer = layer - kind * 16384;
    if (kind < 0.5 || kind > 2.5 || gExtra.z <= 0) return rel;   // 3 water, 4 lava: they do not sway
    float3 w = rel + gCamPos.xyz;
    float t = gVolume.z;
    float amount = kind > 1.5 ? (1 - v) : 1;                              // plants: only the top moves
    float3 d = float3(sin(t * 1.9 + w.x * 0.6 + w.z * 0.3) + 0.4 * sin(t * 3.7 + w.z * 1.3),
                      0.35 * sin(t * 2.3 + w.x * 0.9 + w.y),
                      sin(t * 1.6 + w.z * 0.6 - w.x * 0.2) + 0.4 * sin(t * 4.1 + w.x * 1.1));
    return rel + d * (kind > 1.5 ? 0.09 : 0.035) * gExtra.z * amount;
}

VSOut VS_Block(VSIn i)
{
    VSOut o;
    float layer;
    float3 p = sway(meshToRel(i.pos), i.layer, i.uv.y, layer);
    o.pos = mul(float4(p, 1), gViewProj);
    o.uvl = float3(i.uv, layer);
    o.ao = (i.lit & 255) / 255.0;
    o.rel = p;
    float3 nl = unpackNormal(i.normal);
    o.n = normalize(gAxisX.xyz * nl.x + gAxisY.xyz * nl.y + gAxisZ.xyz * nl.z);
    o.lamp = float4(float3((i.lit >> 8) & 15, (i.lit >> 12) & 15, (i.lit >> 16) & 15) / 15.0, (i.lit >> 20) & 1);
    o.mat = float2(((i.lit >> 21) & 15) / 15.0, (i.lit >> 25) & 1);
    o.clip = dot(p, gClipPlane.xyz) + gClipPlane.w;
    o.kind = floor(i.layer / 16384);
    return o;
}

// ---------------------------------------------------------------- sky
float3 skyColor(float3 dir)
{
    float h = dir.y;
    float3 c = lerp(gSkyHorizon.rgb, gSkyTop.rgb, pow(saturate(h), 0.45));
    if (h < 0) c = lerp(gSkyHorizon.rgb, gSkyHorizon.rgb * 0.55, saturate(-h * 3));
    float mu = saturate(dot(dir, gSunDir.xyz));
    float horizon = 1 - saturate(abs(h) * 2.5);
    c += gSunGlow.rgb * (pow(mu, 6) * 0.6 + pow(mu, 48) * 1.5) * (0.5 + 0.5 * horizon);   // warm glow around the sun
    return c;
}

// ---------------------------------------------------------------- soft shadows (16 samples spread on a disc)
static const float2 kDisc[16] = {
    float2(-0.94, -0.40), float2(0.95, -0.21), float2(-0.09, -0.93), float2(0.34, 0.29),
    float2(-0.92, 0.46), float2(-0.82, -0.15), float2(-0.38, 0.28), float2(-0.26, -0.83),
    float2(0.54, -0.81), float2(0.45, 0.89), float2(-0.38, -0.32), float2(0.81, 0.59),
    float2(0.02, 0.69), float2(0.05, -0.20), float2(-0.62, 0.79), float2(0.62, 0.05) };

// the first cascade that holds this point (nearest = sharpest), and how close to its edge it is (0..1: blend into the next)
int cascadeFor(float3 rel, out float edge)
{
    edge = 1;
    [unroll] for (int c = 0; c < 4; c++)
    {
        float4 s = mul(float4(rel, 1), gCascVP[c]);
        float m = max(abs(s.x), abs(s.y));
        if (m < 0.94 && s.z < 1) { edge = saturate((m - 0.72) / 0.22); return c; }
    }
    return 4;
}
// filtered samples from one cascade; rB: how soft the edge is, in blocks (0: as sharp as that map is)
float shadowPCF(int c, float3 rel, float3 n, float ndl, float rB, float2 screen)
{
    float tb = gCascTexel[c];
    float3 p = rel + n * tb * 1.5 * (1.5 - ndl);                       // push off the surface by about a shadow pixel: no "shadow acne"
    float4 s = mul(float4(p, 1), gCascVP[c]);
    float2 uv = float2(s.x, -s.y) * 0.5 + 0.5;
    if (any(uv < 0) || any(uv > 1) || s.z > 1) return 1;
    float bias = s.z - (0.02 + tb * 3) / 599;
    float radius = max(0.5, rB / tb), texel = gShadowInfo.x;             // in this map's pixels
    float step = radius * 2 / 5, sum = 0;                                // no random jitter: each map keeps its blur small enough to stay smooth
    [unroll] for (int gy = 0; gy < 6; gy++) [unroll] for (int gx = 0; gx < 6; gx++)
        sum += gShadow.SampleCmpLevelZero(gShadowSamp, float3(uv + (float2(gx, gy) - 2.5) * step * texel, c), bias);
    return sum / 36;
}
float shadowAt(float3 rel, float3 n, float ndl, float2 screen)
{
    if (gSunDir.w < 0.5) return 1;
    float edge; int c = cascadeFor(rel, edge);
    if (c > 3) return 1;
    float rB = gShadow2.x < 0.5 ? 0 : 0.0715 * gShadow2.y;              // the soft edge in blocks (the same in every cascade)
    if (gShadow2.x > 1.5)
    {   // like sunlight: look for what casts the shadow; the farther below it, the softer the edge
        float tb = gCascTexel[c];
        float3 p = rel + n * tb * 1.5 * (1.5 - ndl);
        float4 s = mul(float4(p, 1), gCascVP[c]);
        float2 uv = float2(s.x, -s.y) * 0.5 + 0.5;
        float texel = gShadowInfo.x, bias = s.z - (0.02 + tb * 3) / 599, sp = max(1.0, 0.165 / tb);
        int size = int(1 / texel); int2 at = int2(uv / texel); float blk = 0, cnt = 0;
        [unroll] for (int by = 0; by < 4; by++) [unroll] for (int bx = 0; bx < 4; bx++)
        {
            float d = gShadow.Load(int4(clamp(at + int2(round((bx - 1.5) * sp), round((by - 1.5) * sp)), 0, size - 1), c, 0));
            if (d < bias) { blk += d; cnt += 1; }
        }
        if (cnt < 0.5) return 1;                                         // nothing above: lit
        float dz = (s.z - blk / cnt) * 599;                              // blocks from what casts it to here
        rB = clamp(0.033 + dz * 0.03 * gShadow2.y, 0.033, 0.77);
    }
    int cs = c;                                                          // a very soft edge: a coarser map, so its samples stay close together
    [loop] while (cs < 3 && rB / gCascTexel[cs] > 4) cs++;
    float sh = shadowPCF(cs, rel, n, ndl, rB, screen);
    if (edge > 0 && cs == c && c < 3) sh = lerp(sh, shadowPCF(c + 1, rel, n, ndl, rB, screen), edge);   // where two maps meet: a smooth blend
    float fade = saturate(length(rel.xz) / gCascR.w * 1.25 - 0.25);      // fade out where the last map ends
    return lerp(sh, 1, fade * fade);
}

float lumOf(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
float hash21(float2 p) { p = frac(p * float2(123.34, 456.21)); p += dot(p, p + 45.32); return frac(p.x * p.y); }

// generated normals and the coat (like Complementary's): from the texture's own pixels, nothing extra to paint.
// Each pixel is a little tile: its brightness is its height, so light pixels stand up and dark gaps sink in, with a
// bevel at the pixel's edges; the coat is a fine noise inside each pixel (4 x 4 specks). Both fade out far away.
void surfaceDetail(VSOut i, inout float3 n, inout float3 albedo, TexMat m)
{
    float3 dp1 = ddx(i.rel), dp2 = ddy(i.rel); float2 du1 = ddx(i.uvl.xy), du2 = ddy(i.uvl.xy);   // (before any branch)
    bool mesh = i.uvl.z > 16000;                                                                  // an imported model: its own picture
    uint W, H, L, M;
    if (mesh) gMeshTex.GetDimensions(0, W, H, M); else gTex.GetDimensions(0, W, H, L, M);
    float2 tc = i.uvl.xy * float2(W, H);
    float2 fw = max(abs(du1), abs(du2)) * float2(W, H);
    float near = saturate(1.6 - max(fw.x, fw.y) * 1.2);                                          // a pixel smaller than about a screen pixel: off (no shimmer)
    if (near <= 0) return;
    int layer = (int)round(i.uvl.z); int2 c = int2(floor(tc)); float2 f = frac(tc);   // round: the layer arrives as e.g. 2.9999 (cutting it off gave the texture next to it)
    int2 sz = int2(W, H);
    #define HT(o) sqrt(lumOf(mesh ? gMeshTex.Load(int3(((c + (o)) % sz + sz) % sz, 0)).rgb : gTex.Load(int4(((c + (o)) % sz + sz) % sz, layer, 0)).rgb))
    float h0 = HT(int2(0, 0)), hr = HT(int2(1, 0)), hl = HT(int2(-1, 0)), hd = HT(int2(0, 1)), hu = HT(int2(0, -1));
    #undef HT
    float bump = gSurf.x * m.b.x * near;
    if (bump > 0)
    {
        float e0 = smoothstep(0.5, 1.0, f.x), e1 = smoothstep(0.5, 1.0, 1 - f.x), e2 = smoothstep(0.5, 1.0, f.y), e3 = smoothstep(0.5, 1.0, 1 - f.y);
        float sx = (hr - h0) * e0 + (h0 - hl) * e1 + (hr - hl) * 0.15;                          // the edges toward the neighbours, and a little overall slope
        float sy = (hd - h0) * e2 + (h0 - hu) * e3 + (hd - hu) * 0.15;
        float3 g = normalize(n);                                                                  // the surface's own tangents from the screen derivatives
        float3 p2 = cross(dp2, g), p1 = cross(g, dp1);
        float3 T = p2 * du1.x + p1 * du2.x, B = p2 * du1.y + p1 * du2.y;
        float s = rsqrt(max(1e-12, max(dot(T, T), dot(B, B))));
        T *= s; B *= s;
        float k = bump * 3.5;                                                                     // brighter toward +u: the surface tilts away from +u
        n = normalize(g - (T * sx + B * sy) * k);
        float avg = (hr + hl + hd + hu) * 0.25;                                                   // pixels lower than their neighbours (gaps, cracks) sit in shade
        albedo *= 1 - saturate((avg - h0) * 2.5) * 0.35 * saturate(bump);
    }
    float coat = gSurf.y * m.b.y * near;
    if (coat > 0)
    {
        float speck = hash21(floor(tc * 4) + layer * 17.13) - 0.5;                               // 4 x 4 specks in each pixel
        float tile = hash21(c + layer * 3.7) - 0.5;                                               // and each pixel a touch different
        albedo *= 1 + (speck * 0.22 + tile * 0.06) * coat;
    }
}

float3 lightBlock(VSOut i, float3 albedo, bool front, out float3 n, out float shine, out float3 indirect)
{
    n = front ? i.n : -i.n;
    float3 nFlat = n;                                                                             // the face itself, before the generated bumps
    TexMat m = (TexMat)0; m.b = float4(1, 1, 0, 0);
    if (i.uvl.z < 16000) m = gMats[(uint)round(i.uvl.z)]; else { m.a = gMM0; m.b = gMM1; }
    if (gSurf.x > 0 || gSurf.y > 0) surfaceDetail(i, n, albedo, m);
    if (gSurf.z < 0.5) { m.a = 0; }                                                               // materials off: plain
    // weather on surfaces: snow settles on tops open to the sky, rain makes things darker and shiny
    float open = (gWeather.w > 0.01 || gWeather2.x > 0.01) ? skyExposed(i.rel + nFlat * 0.3) : 0;
    float snowy = gWeather2.x * open * smoothstep(0.35, 0.85, nFlat.y);
    albedo = lerp(albedo, float3(0.92, 0.95, 1.0), snowy * 0.95);
    float wet = gWeather.w * open * (1 - snowy);
    albedo *= lerp(1, 0.55, wet);
    n = normalize(lerp(n, nFlat, saturate(wet * 1.5 + snowy)));                                   // water and snow fill the little dents: smooth again
    if (gWeather.x > 0.02 && wet > 0.2 && nFlat.y > 0.9)                                          // raining on wet tops: drops land in the film of water
    {
        float2 rip = rainRipples((gCamPos.xyz + i.rel).xz, gVolume.z) * saturate(gWeather.x * 1.5) * saturate((wet - 0.2) * 2) * saturate(1 - length(i.rel) / 40);
        n = normalize(n + float3(rip.x, 0, rip.y));
    }
    float ndl = saturate(dot(n, gSunDir.xyz));
    float ndlFlat = saturate(dot(nFlat, gSunDir.xyz));                                            // shadows ask with the flat face: the bumps would push each pixel's test
    float sh = ndlFlat > 0 ? shadowAt(i.rel, nFlat, ndlFlat, i.pos.xy) * cloudShadow(i.rel) : 0; // a different way and the face would shadow itself in stripes
    float skyV = skyVis(i.rel, nFlat);                                 // true darkness: no sky light where the sky is not
    {   // past the sun's shadow maps (or with shadows off) the roofs map keeps the sun out of caves
        float farOut = gSunDir.w < 0.5 ? 1 : saturate((length(i.rel) - gCascR.w * 0.92) / max(gCascR.w * 0.08, 1));
        sh *= lerp(1, skyV, farOut);
    }
    float sky = 0.55 + 0.45 * n.y;                                     // more sky light on top than on the sides
    float3 light = gSunColor.rgb * ndl * sh + gAmbient.rgb * sky * i.ao * skyV;
    float lamp = max(i.lamp.r, max(i.lamp.g, i.lamp.b));
    float3 hue = lamp > 0 ? i.lamp.rgb / lamp : 0;                                                   // the colour of the lamps nearby
    light += hue * (pow(lamp, 3.2) * 0.9) * gExtra.w * lerp(0.5, 1, i.ao);
    float metal = saturate(m.a.y);
    float dShine = max(max(saturate(m.a.x), i.mat.x), wet * 0.9);                                  // how shiny for the lights' highlights (wet streets!)
    float3 dSpec;
    float3 dyn = dynLightsSpec(i.rel, n, -normalize(i.rel), lerp(10, 380, dShine * dShine), gOffset.w < 0.5, dSpec) * lerp(0.6, 1, i.ao);   // placed lights, strips, glowing emitters, shots
    float3 c = albedo * (light + dyn) * (1 - 0.75 * metal);                                       // metal: little plain colour ...
    c += dSpec * lerp(float3(0.04, 0.04, 0.04) + 0.5 * dShine, albedo * 1.4, metal) * max(dShine, 0.12);   // the lights' highlights
    indirect = albedo * (light - gSunColor.rgb * ndl * sh) * (1 - 0.75 * metal);
    float3 v0 = -normalize(i.rel);
    float mshine = saturate(m.a.x);
    if (mshine > 0.01 || metal > 0.01)                                                            // ... and the sun as a highlight, in its own colour for metal
    {
        float3 hv = normalize(gSunDir.xyz + v0);
        float pw = lerp(12, 500, mshine * mshine);
        float spec = pow(saturate(dot(n, hv)), pw) * (pw + 8) / 60;
        float3 specCol = lerp(float3(0.04, 0.04, 0.04) + 0.3 * mshine, albedo * 1.4, metal);
        c += gSunColor.rgb * sh * ndl * spec * specCol * max(mshine, 0.35);
    }
    if (m.a.z > 0)                                                                                // glowing pixels (the bright ones, if set)
    {
        float lum = sqrt(lumOf(albedo));
        float e = m.a.z * smoothstep(m.a.w, m.a.w + 0.08, lum);
        c += albedo * e * 3.0;
    }
    if (m.b.z > 0)                                                                                // its emission map: what glows, in its own colours
    {
        float3 em = i.uvl.z < 16000 ? gEmitTex.Sample(gSamp, i.uvl).rgb : gMeshEmit.Sample(gSamp, i.uvl.xy).rgb;
        c += em * m.b.z * 2.5;
    }
    if (i.lamp.a > 0.5)                                                                               // the lamp itself glows in its colour
    {
        c = (dot(albedo, float3(0.2126, 0.7152, 0.0722)) * hue * 0.85 + albedo * hue * 0.3) * 3.2 * gExtra.w;
        indirect = 0;                                                                                 // glowing things are never shaded
    }
    float3 v = -normalize(i.rel);
    n = normalize(lerp(nFlat, n, 0.15));                                                          // reflections see a much calmer surface (else every pixel mirrors something else: noisy streaks)
    shine = max(max(i.mat.x, mshine), wet * lerp(0.25, 0.55, saturate(n.y)));    // wet ground shines like a puddle
    if (i.mat.y > 0.5) { shine = 1; c *= 0.15; indirect *= 0.15; }    // a mirror is (almost) only its reflection
    if (gRefl.x < 0.5 && shine > 0.01)   // reflections off: just a little of the sky mirrored
    {
        float fr = 0.04 + 0.96 * pow(1 - saturate(dot(n, v)), 5);
        c += skyColor(reflect(-v, n)) * fr * shine * 0.9 * lerp(float3(1, 1, 1), albedo * 1.5, metal) * skyV;
    }
    if (metal > 0.01)   // metal mirrors its surroundings in its own colour even when not polished (the sky here; reflections add the rest)
    {
        float3 rv = reflect(-v, n);
        c += skyColor(rv) * albedo * metal * (0.35 + 0.65 * saturate(m.a.x)) * lerp(0.35, 1.0, i.ao) * (gRefl.x < 0.5 ? 1.0 : 0.6) * skyV;
    }
    float d = length(i.rel);
    float f = saturate((d - gFogRange.x) / max(gFogRange.y - gFogRange.x, 1));
    indirect *= 1 - f * f;
    return lerp(c, skyColor(normalize(i.rel)) * max(skyV, gOcc.y), f * f);   // in a cave the distance fades to dark, not to the sky
}

// lava: a dark crust with glowing cracks that slowly churn
float3 lavaColor(float3 rel)
{
    float3 wp = gCamPos.xyz + rel; float t = gVolume.z;
    float a = gNoise.SampleLevel(gLinear, float3(wp.x * 0.09 + t * 0.006, wp.y * 0.05 + t * 0.003, wp.z * 0.09), 0).x;
    float b = gNoise.SampleLevel(gLinear, float3(wp.x * 0.27 - t * 0.012, wp.z * 0.27, t * 0.008 + wp.y * 0.1), 0).y;
    float crack = smoothstep(0.45, 0.75, a * 0.65 + b * 0.5);
    float3 crust = float3(0.22, 0.035, 0.01), glow = float3(1.0, 0.36, 0.06);
    return lerp(crust, glow, crack) * (1.4 + 5.0 * crack * crack);
}

PSOut PS_Opaque(VSOut i, bool front : SV_IsFrontFace)
{
    float4 t = albedoAt(i.uvl);
    PSOut o; float3 n; float sh;
    o.indirect = 0;
    if (i.kind > 3.5) { o.color = float4(lavaColor(i.rel), 1); o.surface = float4(i.n * 0.5 + 0.5, 0); return o; }
    float3 ind;
    o.color = float4(lightBlock(i, t.rgb, front, n, sh, ind), 1);
    o.surface = float4(n * 0.5 + 0.5, i.mat.y > 0.5 ? 1 : min(sh, 0.98));   // 1 = mirror
    o.indirect = float4(ind, 0);
    return o;
}

PSOut PS_Cutout(VSOut i, bool front : SV_IsFrontFace)
{
    float4 t = albedoAt(i.uvl);
    clip(t.a - 0.5);
    PSOut o; float3 n; float sh; float3 ind;
    o.color = float4(lightBlock(i, t.rgb, front, n, sh, ind), 1);
    o.surface = float4(n * 0.5 + 0.5, i.mat.y > 0.5 ? 1 : min(sh, 0.98));
    o.indirect = float4(ind, 0);
    return o;
}

// the see-through preview of the block you are about to place
float4 PS_Ghost(VSOut i) : SV_Target
{
    float4 t = albedoAt(i.uvl);
    clip(t.a - 0.5);
    return float4(t.rgb * (gAmbient.rgb + gSunColor.rgb * 0.3) * 1.2, 0.5);
}

// ---------------------------------------------------------------- shadow map
struct ShOut { float4 pos : SV_Position; float3 uvl : TEXCOORD0; };
ShOut VS_Shadow(VSIn i)
{
    float layer; float3 p = sway(meshToRel(i.pos), i.layer, i.uv.y, layer);
    ShOut o; o.pos = mul(float4(p, 1), gShadowVP); o.uvl = float3(i.uv, layer); return o;
}
void PS_ShadowCutout(ShOut i) { clip(albedoAt(i.uvl).a - 0.5); }
ShOut VS_SkyMap(VSIn i)
{
    float layer; float3 p = sway(meshToRel(i.pos), i.layer, i.uv.y, layer);
    ShOut o; o.pos = mul(float4(p, 1), gSkyVP); o.uvl = float3(i.uv, layer); return o;
}

// ---------------------------------------------------------------- rain and snow: thousands of drops in a box that moves with you
float3 hash3u(uint n)
{
    n = (n << 13U) ^ n; n = n * (n * n * 15731U + 789221U) + 1376312589U;
    uint3 k = uint3(n, n * 16807U, n * 48271U);
    return float3(k & 0x7fffffffU) / 2147483647.0;
}
static const uint kMaxDrops = 14000;
struct RainOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float3 rel : TEXCOORD1; float fade : TEXCOORD2; float kind : TEXCOORD3; };
// kind: 0 rain, 1 snow, 2 dust, 3 embers / spores (rise and glow), 4 ice sparkles
RainOut particle(uint id, float kind, float amount, uint salt)
{
    RainOut o = (RainOut)0;
    uint drop = id / 6, c = id % 6;
    if (drop >= (uint)(amount * kMaxDrops)) { o.pos = float4(0, 0, -1, 1); return o; }
    float3 h = hash3u(drop * 3 + salt);
    float3 box = float3(36, 30, 36);
    float t = gPrecip.x;
    float2 wind = float2(1, 0.45) * gWeather2.z;
    float3 vel;
    if (kind < 0.5) vel = float3(wind.x * 4.0, -(15 + h.x * 5), wind.y * 4.0);
    else if (kind < 1.5) vel = float3(wind.x * 2.5, -(1.6 + h.x * 0.8), wind.y * 2.5);
    else if (kind < 2.5) vel = float3(wind.x * 3.0 + 0.6, -(0.15 + h.x * 0.2), wind.y * 3.0 + 0.2);
    else if (kind < 3.5) vel = float3(wind.x * 0.8, 0.5 + h.x * 0.7, wind.y * 0.8);
    else vel = float3(wind.x * 0.6, -(0.1 + h.x * 0.15), wind.y * 0.6);
    float3 p = h * box * 7 + (vel - float3(wind.x, 0, wind.y) * (kind < 2.5 ? 3.0 : kind < 3.5 ? 0.8 : 0.6)) * t
             + float3(gPrecip.y, 0, gPrecip.z) * (kind < 2.5 ? 3.0 : kind < 3.5 ? 0.8 : 0.6);   // (the wind's part: added up as it went)
    if (kind > 0.5) p.xz += float2(sin(t * 1.3 + h.y * 40), cos(t * 1.1 + h.z * 40)) * (kind > 2.5 ? 0.9 : 0.6);   // drifting
    float3 rel = (frac((p - gCamPos.xyz) / box) - 0.5) * box;
    static const float2 q[6] = { float2(0, 0), float2(1, 0), float2(1, 1), float2(0, 0), float2(1, 1), float2(0, 1) };
    float2 k = q[c];
    float dist = length(rel);
    float size = kind < 1.5 ? 0.07 : kind < 2.5 ? 0.035 : kind < 3.5 ? 0.05 : 0.03;
    float3 offs;
    if (kind < 0.5) { float3 ax = normalize(vel), side = normalize(cross(ax, rel)); offs = side * (k.x - 0.5) * 2 * (0.01 + dist * 0.0012) + ax * (k.y - 0.5) * 0.6; }
    else offs = (gCamR.xyz * (k.x - 0.5) + gCamU.xyz * (k.y - 0.5)) * (size + dist * 0.002);
    o.pos = mul(float4(rel + offs, 1), gViewProj);
    o.uv = k; o.rel = rel; o.kind = kind + frac(h.z * 7.13) * 0.4;   // the fraction: a random value per particle (sparkle timing)
    o.fade = saturate(dist - 0.6) * saturate((box.x * 0.5 - max(abs(rel.x), abs(rel.z))) / 5) * saturate((box.y * 0.5 - abs(rel.y)) / 4);
    return o;
}
// rain and snow falling from high above: each drop falls until it meets the first thing under the open sky (the "what is above"
// map), then rain makes a ring and a little crown of droplets there, snow lies for a moment; then it falls again from the top.
// far: a second, bigger layer of drops further out (no landing: the world in front hides them), so the rain fills the distance.
static const uint kNearDrops = 16000, kFarDrops = 9000;
RainOut rainDrop(uint id, uint salt, bool far)
{
    RainOut o = (RainOut)0; o.pos = float4(0, 0, -1, 1);
    bool snow = gWeather.y > gWeather.x; float amount = max(gWeather.x, gWeather.y);
    uint per = far ? 6 : 12, drop = id / per, c = id % per;
    if (drop >= (uint)(amount * (far ? kFarDrops : kNearDrops))) return o;
    float3 h = hash3u(drop * 3 + salt);
    float t = gPrecip.x;                                                           // a steady clock (the weather changing never makes them jump)
    float3 box = far ? float3(150, 100, 150) : float3(40, 56, 40);
    float low = far ? -44 : -16;                                                   // the column: from low to low + box.y around the camera
    float2 wind = float2(1, 0.45) * gWeather2.z;
    float speed = snow ? 1.6 + h.x * 0.9 : 13 + h.x * 5, push = snow ? 2.5 : 4.0;
    float3 vel = float3(wind.x * push, -speed, wind.y * push);                     // (now: for the streaks' slant and where a splash was)
    float3 p = h * box * 7 + float3(gPrecip.y * push, -speed * t, gPrecip.z * push); // how far the wind has carried them, added up as it went
    if (snow) p.xz += float2(sin(t * 1.3 + h.y * 40), cos(t * 1.1 + h.z * 40)) * 0.6;   // flakes drift
    float3 rel;
    rel.xz = (frac((p.xz - gCamPos.xz) / box.xz) - 0.5) * box.xz;
    rel.y = frac((p.y - gCamPos.y) / box.y) * box.y + low;
    static const float2 q[6] = { float2(0, 0), float2(1, 0), float2(1, 1), float2(0, 0), float2(1, 1), float2(0, 1) };
    float2 k = q[c % 6];
    float edge = saturate((box.x * 0.5 - max(abs(rel.x), abs(rel.z))) / 6) * saturate((rel.y - low) / 3) * saturate((low + box.y - rel.y) / 6);   // fades at the box's sides, appears at the top
    float rnd = frac(h.z * 7.13) * 0.4;
    bool known = false; float g = far ? -1e5 : groundBelow(rel, known);
    if (known && rel.y < g)
    {   // it has landed
        float tau = (g - rel.y) / speed, life = snow ? 2.5 : 0.38;
        if (tau > life) return o;                                                   // gone until it falls again
        float2 at = rel.xz - vel.xz * tau; bool k2; float g2 = groundBelow(float3(at.x, g, at.y), k2);
        if (!k2 || abs(g2 - g) > 0.6) { at = rel.xz; g2 = g; }                      // (it landed over an edge: keep it where it is)
        float3 base = float3(at.x, g2 + 0.025, at.y), center = base;
        float e = tau / life, fade = (1 - e) * edge;
        float dist = length(base);
        if (snow)
        {
            if (c >= 6) return o;
            float sz = 0.05 + dist * 0.0015;
            o.pos = mul(float4(base + (gCamR.xyz * (k.x - 0.5) + gCamU.xyz * (k.y - 0.5)) * sz * 2, 1), gViewProj);
            o.uv = k; o.rel = base; o.kind = 1 + rnd; o.fade = fade * saturate(dist - 0.6); return o;
        }
        if (c < 6)
        {   // the ring on the surface
            float r = 0.05 + 0.34 * sqrt(e);
            o.pos = mul(float4(base + float3((k.x - 0.5) * 2 * r, 0, (k.y - 0.5) * 2 * r), 1), gViewProj);
            o.uv = k; o.rel = base; o.kind = 5 + rnd; o.fade = fade * saturate(1 - dist / 30);
        }
        else
        {   // a little crown of droplets jumping up (only at first)
            if (e > 0.6) return o;
            float up = 0.05 + 0.14 * sin(e / 0.6 * 3.14159), sz = 0.09 + 0.08 * e;
            center = base + float3(0, up, 0);
            o.pos = mul(float4(center + (gCamR.xyz * (k.x - 0.5) + float3(0, 1, 0) * (k.y - 0.5)) * sz * 2, 1), gViewProj);
            o.uv = k; o.rel = center; o.kind = 6 + rnd; o.fade = (1 - e / 0.6) * edge * saturate(1 - dist / 22);
        }
        return o;
    }
    if (c >= 6) return o;
    float dist = length(rel);
    float3 offs;
    if (!snow) { float3 ax = normalize(vel), side = normalize(cross(ax, rel)); offs = side * (k.x - 0.5) * 2 * (0.01 + dist * 0.0012) * (far ? 1.6 : 1) + ax * (k.y - 0.5) * (far ? 1.5 : 0.6); }
    else offs = (gCamR.xyz * (k.x - 0.5) + gCamU.xyz * (k.y - 0.5)) * (0.07 + dist * 0.002) * (far ? 1.8 : 1);
    o.pos = mul(float4(rel + offs, 1), gViewProj);
    o.uv = k; o.rel = rel; o.kind = (snow ? 1 : 0) + rnd;
    o.fade = saturate(dist - 0.6) * edge * (far ? saturate((dist - 16) / 10) * 0.75 : 1);
    return o;
}
RainOut VS_Rain(uint id : SV_VertexID) { return rainDrop(id, 1, false); }
RainOut VS_RainFar(uint id : SV_VertexID) { return rainDrop(id, 13, true); }
RainOut VS_Dust(uint id : SV_VertexID) { return particle(id, gEnvPart.x, gEnvPart.y, 9); }
float4 PS_Rain(RainOut i) : SV_Target
{
    float kind = floor(i.kind), rnd = frac(i.kind) / 0.4;
    if (i.fade <= 0.001 || (kind < 1.5 && skyExposed(i.rel) < 0.5)) discard;      // no rain or snow under a roof (dust floats everywhere)
    float3 lc = gAmbient.rgb * 1.4 + gSunColor.rgb * 0.25 + gWeather2.y * float3(1.6, 1.7, 2.2);
    float2 d = i.uv * 2 - 1; float rd = saturate(1 - dot(d, d));
    if (kind > 5.5) { float2 a = i.uv * 2 - 1; float dots = 0;                                       // the crown: a few droplets
        dots += saturate(1 - length(a - float2(-0.45, -0.1)) / 0.22); dots += saturate(1 - length(a - float2(0.1, 0.35)) / 0.2); dots += saturate(1 - length(a - float2(0.5, -0.2)) / 0.18);
        return float4(lc * 1.3, saturate(dots) * 0.6 * i.fade); }
    if (kind > 4.5) { float d = length(i.uv * 2 - 1); float ring = exp(-pow((d - 0.78) / 0.12, 2)) + 0.35 * exp(-pow((d - 0.45) / 0.1, 2));   // the ring where a drop landed
        return float4(lc * 1.2, saturate(ring) * 0.42 * i.fade); }
    if (kind < 0.5) return float4(lc, (1 - abs(i.uv.x * 2 - 1)) * 0.28 * i.fade);
    if (kind < 1.5) return float4(lc * 1.2, rd * rd * 0.9 * i.fade);
    if (kind < 2.5) return float4(gEnvPartColor.rgb * lc, rd * 0.55 * i.fade);
    if (kind < 3.5) return float4(gEnvPartColor.rgb * (4 + 3 * sin(gVolume.z * 3 + rnd * 40)), rd * rd * i.fade);   // glowing embers / spores
    float tw = pow(saturate(sin(gVolume.z * (4 + rnd * 9) + rnd * 60)), 12);                                      // ice sparkles glint
    return float4(gEnvPartColor.rgb * (lc + gSunColor.rgb * 3 * tw), rd * (0.25 + 0.75 * tw) * i.fade);
}
// a lightning bolt (bright, so it blooms); gOffset.w = brightness
float4 PS_Bolt(VSOut i) : SV_Target { return float4(float3(0.75, 0.8, 1.0) * gOffset.w, 1); }

// ---------------------------------------------------------------- sky: a full-screen triangle behind everything
struct SkyOut { float4 pos : SV_Position; float2 ndc : TEXCOORD0; };
// a planet hanging in the sky (space): lit by the sun, oceans, land and clouds from the cloud noise, a thin blue rim of air
float3 planet(float3 dir, float3 c)
{
    float3 pc = normalize(float3(0.35, 0.30, 0.89));
    float R = 0.24, ca = dot(dir, pc);
    if (ca < cos(R)) return c;
    float3 right = normalize(cross(float3(0, 1, 0), pc)), up = cross(pc, right);
    float2 q = float2(dot(dir, right), dot(dir, up)) / sin(R);
    float r2 = dot(q, q); if (r2 >= 1) return c;
    float3 n = normalize(q.x * right + q.y * up - sqrt(1 - r2) * pc);
    float2 nz = gNoise.SampleLevel(gLinear, n * 0.7 + 0.31, 0);
    float land = smoothstep(0.52, 0.56, nz.x);
    float3 ground = lerp(float3(0.02, 0.06, 0.22), lerp(float3(0.10, 0.22, 0.06), float3(0.35, 0.28, 0.16), nz.y), land);
    float cloud = smoothstep(0.62, 0.8, gNoise.SampleLevel(gLinear, n * 1.3 + float3(0.1, 0.5, 0.2), 0).y);
    ground = lerp(ground, float3(0.8, 0.82, 0.85), cloud);
    float lit = saturate(dot(n, gSunDir.xyz) * 1.2 + 0.05);
    float rim = pow(1 - sqrt(1 - r2), 3);
    float3 col = ground * lit * 2.2 + float3(0.25, 0.45, 1.0) * rim * (0.3 + lit * 2);
    return lerp(c, col, smoothstep(1.0, 0.985, r2));
}

SkyOut VS_Sky(uint id : SV_VertexID)
{
    SkyOut o; float2 p = float2((id << 1) & 2, id & 2);
    o.ndc = p * 2 - 1; o.pos = float4(o.ndc, 1, 1);
    return o;
}
float4 PS_Sky(SkyOut i) : SV_Target
{
    float3 dir = normalize(gCamF.xyz + gCamR.xyz * (i.ndc.x * gProj.x * gProj.y) + gCamU.xyz * (i.ndc.y * gProj.x));
    float3 c = skyColor(dir);
    float mu = dot(dir, gSunDir.xyz);
    c += gSunGlow.w * smoothstep(0.9993, 0.9997, mu) * float3(1.0, 0.95, 0.85);   // the sun (or moon) itself
    if (gExtra.x > 0 && dir.y > 0)                                                // stars: one maybe in each little patch of sky
    {
        float3 g = dir * 90;
        float3 cell = floor(g);
        float h = frac(sin(dot(cell, float3(127.1, 311.7, 74.7))) * 43758.5453);
        if (h > 0.985)
        {
            float3 jit = frac(sin(float3(dot(cell, float3(12.9, 78.2, 37.7)), dot(cell, float3(39.3, 11.1, 83.2)), dot(cell, float3(73.1, 52.3, 9.7)))) * 4375.85);
            float d = length(g - (cell + 0.2 + jit * 0.6));
            float tw = 0.7 + 0.3 * sin(gVolume.z * (2 + h * 30) + h * 100);
            float b = (h - 0.985) / 0.015;
            c += float3(0.9 + 0.1 * jit.x, 0.9, 0.85 + 0.15 * jit.z) * pow(smoothstep(0.22, 0.0, d), 1.5) * (0.6 + 4 * b * b) * tw * gExtra.x * saturate(dir.y * 6);
        }
    }
    if (gEnv.z > 0) c = planet(dir, c);
    return float4(c, 1);
}

// ---------------------------------------------------------------- reflections (screen space): shiny pixels look for what they mirror
// in the finished picture by walking a ray through the depth buffer; where it finds nothing, they mirror the sky.
Texture2D gHdr : register(t0);
Texture2D gSurface : register(t5);
Texture2D gMirrorView : register(t6);   // the world drawn from the mirrored camera (same screen positions as the picture)
SamplerState gClamp : register(s3);
float ignR(float2 px) { return frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715)))); }
float linDepth(float zb) { return gDepth.x * gDepth.y / (gDepth.y - zb * (gDepth.y - gDepth.x)); }

// walks a ray through the picture's depth: small steps first, longer further away; true + where it hit
bool traceScreen(float3 P, float3 n, float3 r, float dist, float2 pixel, out float2 huv, out float travelled)
{
    huv = 0; travelled = 0;
    float3 pos = P + n * (0.03 + dist * 0.002);
    float stepLen = 0.12 + dist * 0.01;
    pos += r * stepLen * ignR(pixel);
    int steps = (int)gRefl.y;
    [loop] for (int k = 0; k < steps && travelled < gRefl.z; k++)
    {
        float3 prev = pos;
        pos += r * stepLen; travelled += stepLen;
        float4 c = mul(float4(pos, 1), gViewProj);
        if (c.w <= 0.05) return false;
        float2 ndc = c.xy / c.w;
        if (any(abs(ndc) > 1)) return false;
        float2 uv = ndc * float2(0.5, -0.5) + 0.5;
        float sceneZ = linDepth(gSceneDepth.Load(int3(uv * gDepth.zw, 0)));
        float behind = c.w - sceneZ;
        if (behind > 0 && behind < max(0.5, stepLen * 2.5))
        {
            float3 a = prev, b = pos;   // found it: home in between the last two steps
            [unroll] for (int s = 0; s < 5; s++)
            {
                float3 m = (a + b) * 0.5; float4 cm = mul(float4(m, 1), gViewProj);
                float2 um = cm.xy / cm.w * float2(0.5, -0.5) + 0.5;
                if (cm.w - linDepth(gSceneDepth.Load(int3(um * gDepth.zw, 0))) > 0) b = m; else a = m;
            }
            float4 cb = mul(float4(b, 1), gViewProj);
            huv = cb.xy / cb.w * float2(0.5, -0.5) + 0.5;
            return true;
        }
        stepLen *= 1.12;
    }
    return false;
}

// ---------------------------------------------------------------- ambient occlusion (screen space): works for anything
// in the picture (blocks, and later items, creatures, players). Half size: each pixel throws a few short rays into the
// half ball above its surface and counts how many end up inside something. Then a careful blur takes away only the
// sky and lamp light where it is hidden.
Texture2D gIndirect : register(t7);
Texture2D<float> gAOMap : register(t8);
float3 viewDir(float2 ndc) { return normalize(gCamF.xyz + gCamR.xyz * (ndc.x * gProj.x * gProj.y) + gCamU.xyz * (ndc.y * gProj.x)); }
float4 PS_AO(SkyOut i) : SV_Target
{
    int2 px = min(int2(i.pos.xy) * 2, int2(gDepth.zw) - 1);
    float zb = gSceneDepth.Load(int3(px, 0));
    if (zb >= 1) return 1;
    float2 ndc = ((float2(px) + 0.5) / gDepth.zw) * float2(2, -2) + float2(-1, 1);
    float3 dir = viewDir(ndc);
    float lin = linDepth(zb);
    float3 P = dir * (lin / max(dot(dir, gCamF.xyz), 1e-3));
    float3 n = gSurface.Load(int3(px, 0)).xyz * 2 - 1;
    if (dot(n, n) < 0.25) return 1;
    n = normalize(n);
    float R = gAOSet.y;
    float3 t = normalize(abs(n.y) < 0.9 ? cross(n, float3(0, 1, 0)) : cross(n, float3(1, 0, 0)));
    float3 b = cross(n, t);
    float rnd = ignR(i.pos.xy), rnd2 = frac(rnd * 7.31 + 0.37);
    float3 base = P + n * (0.02 + lin * 0.0015);
    int N = (int)gAOSet.w; float occ = 0;
    [loop] for (int k = 0; k < N; k++)
    {
        float a = rnd * 6.2831853 + k * 2.3999632;                 // golden angle spiral: even spread
        float r = sqrt((k + rnd2) / N), h = sqrt(saturate(1 - r * r));
        float len = R * lerp(0.12, 1, frac((k + 0.5) / N + rnd2 * 0.61));
        len *= len / R;                                             // more short rays (contact shadow) than long ones
        float3 S = base + (t * (cos(a) * r) + b * (sin(a) * r) + n * h) * len;
        float4 c = mul(float4(S, 1), gViewProj);
        if (c.w <= 0.05) continue;
        float2 su = c.xy / c.w * float2(0.5, -0.5) + 0.5;
        if (any(su <= 0) || any(su >= 1)) continue;
        float sz = linDepth(gSceneDepth.Load(int3(su * gDepth.zw, 0)));
        float behind = c.w - sz;
        if (behind > 0.02 + lin * 0.001)
            occ += smoothstep(0, 1, R / max(abs(lin - sz), 1e-3));   // something far in front does not count
    }
    return 1 - occ / max(N, 1);
}
float4 PS_AOApply(SkyOut i) : SV_Target
{
    int2 px = int2(i.pos.xy);
    float zb = gSceneDepth.Load(int3(px, 0));
    if (zb >= 1) return 0;
    float lin = linDepth(zb);
    int2 hsz = max(int2(gDepth.zw) / 2, 1), hp0 = px / 2;
    float sum = 0, wsum = 0;
    [unroll] for (int y = -2; y <= 2; y++)
    [unroll] for (int x = -2; x <= 2; x++)
    {
        int2 hp = clamp(hp0 + int2(x, y), int2(0, 0), hsz - 1);
        float a = gAOMap.Load(int3(hp, 0));
        float z = linDepth(gSceneDepth.Load(int3(min(hp * 2, int2(gDepth.zw) - 1), 0)));
        float w = saturate(1 - abs(z - lin) / (lin * 0.04 + 0.08)) * (1.0 / (1 + x * x + y * y)) + 1e-4;
        sum += a * w; wsum += w;
    }
    float ao = saturate(sum / wsum);
    ao = pow(ao, 2.2 * gAOSet.z);
    float3 ind = gIndirect.Load(int3(px, 0)).rgb;
    return float4(-ind * (1 - ao), 0);   // added on top of the picture: takes the hidden sky light away
}

float4 PS_Reflect(SkyOut i) : SV_Target
{
    int2 px = int2(i.pos.xy);
    float3 scene = gHdr.Load(int3(px, 0)).rgb;
    float4 g = gSurface.Load(int3(px, 0));
    float shine = g.a;
    if (shine < 0.02 || gRefl.x < 0.5) return float4(scene, 1);
    float3 n = normalize(g.xyz * 2 - 1);
    float3 dir = normalize(gCamF.xyz + gCamR.xyz * (i.ndc.x * gProj.x * gProj.y) + gCamU.xyz * (i.ndc.y * gProj.x));
    float zb = gSceneDepth.Load(int3(px, 0));
    float dist = linDepth(zb) / max(dot(dir, gCamF.xyz), 1e-3);
    float3 P = dir * dist;                                   // the surface, relative to the camera
    float3 r = reflect(dir, n);
    bool mirror = shine > 0.999;

    float2 huv = 0; float travelled = 0;
    bool hit = traceScreen(P, n, r, dist, i.pos.xy, huv, travelled);

    // where the ray finds nothing on screen: the sky (only if this spot is under the open sky, else a dim guess)
    float open = skyExposed(P + n * 0.3);
    float3 fallback = (skyColor(r) * lerp(0.15, 1, open * saturate(r.y * 4 + 0.6)) + gAmbient.rgb * 0.15 * (1 - open)) * skyVis(P, n);
    float3 refl = fallback;
    float2 suv = (float2(px) + 0.5) / gDepth.zw;
    if (mirror && gRefl.w > 0.5 && abs(dot(P, gMirror.xyz) + gMirror.w) < 0.08 && dot(n, gMirror.xyz) > 0.9)
    {
        refl = gMirrorView.SampleLevel(gClamp, suv, 0).rgb;     // a true mirror: everything, even what is behind you
        hit = false;
    }
    else if (hit)
    {
        float2 e = min(huv, 1 - huv);
        float edge = saturate(min(e.x, e.y) * 12) * saturate(1 - travelled / gRefl.z);   // fade near the picture's edges
        refl = lerp(fallback, gHdr.SampleLevel(gClamp, huv, 0).rgb, edge);
    }
    float NdotV = saturate(dot(n, -dir));
    float f0 = mirror ? 0.9 : 0.02 + 0.6 * shine;
    float F = f0 + (1 - f0) * pow(1 - NdotV, 5);
    float amount = mirror ? 1 : saturate(shine * 1.2);
    return float4(scene * (1 - F * amount * 0.5) + refl * F * amount, 1);
}

// ---------------------------------------------------------------- water: waves, what is under it (bent, and bluer the deeper), reflections
float4 PS_Water(VSOut i, bool front : SV_IsFrontFace) : SV_Target
{
    float3 wp = gCamPos.xyz + i.rel; float t = gVolume.z;
    float3 n = front ? i.n : -i.n;
    if (abs(i.n.y) > 0.5)   // waves on top: two layers of moving noise plus a slow swell
    {
        float2 q1 = gNoise.SampleLevel(gLinear, float3(wp.x * 0.06 + t * 0.025, wp.z * 0.06, t * 0.015), 0).xy - 0.5;
        float2 q2 = gNoise.SampleLevel(gLinear, float3(wp.x * 0.21 - t * 0.04, wp.z * 0.21 + t * 0.03, 0.37 + t * 0.02), 0).xy - 0.5;
        float2 slope = q1 * 0.30 + q2 * 0.16 + float2(sin(wp.x * 0.8 + wp.z * 0.35 + t * 1.5), cos(wp.z * 0.9 - wp.x * 0.3 + t * 1.2)) * 0.035;
        if (gWeather.x > 0.02 && n.y > 0 && front) slope += rainRipples(wp.xz, t) * 1.6 * saturate(gWeather.x * 1.5) * skyExposed(i.rel + float3(0, 0.3, 0)) * saturate(1 - length(i.rel) / 50);   // rain: rings where drops land
        n = normalize(float3(slope.x, n.y > 0 ? 1 : -1, slope.y));
    }
    float dist = length(i.rel); float3 dir = i.rel / max(dist, 1e-4), v = -dir;
    float2 suv = i.pos.xy / gDepth.zw;
    float waterZ = dot(i.rel, gCamF.xyz);
    // what is under the surface, bent by the waves
    float2 ruv = suv + n.xz * 0.035 * saturate(1 - dist / 90);
    float sceneZ = linDepth(gSceneDepth.Load(int3(ruv * gDepth.zw, 0)));
    if (sceneZ < waterZ) { ruv = suv; sceneZ = linDepth(gSceneDepth.Load(int3(suv * gDepth.zw, 0))); }
    float thick = front ? max(sceneZ - waterZ, 0) / max(dot(dir, gCamF.xyz), 0.2) : 0.4;
    float3 behind = gHdr.SampleLevel(gClamp, ruv, 0).rgb;
    float3 absorb = exp(-thick * float3(0.40, 0.10, 0.065));                        // red goes first: deep water is blue-green
    float3 deep = (gAmbient.rgb * 0.5 + gSunColor.rgb * 0.08) * float3(0.04, 0.22, 0.28);
    float3 refr = behind * absorb + deep * (1 - absorb);
    // what it mirrors
    float3 r = reflect(dir, n);
    if (front && r.y < 0.02) r.y = 0.02;
    float open = skyExposed(i.rel + float3(0, 0.3, 0));
    float wsv = skyVis(i.rel, float3(0, 1, 0)); deep *= wsv; refr = behind * absorb + deep * (1 - absorb);
    float3 refl = skyColor(r) * lerp(0.25, 1, open) * wsv;
    float2 huv; float travelled;
    if (front && gRefl.x > 0.5 && traceScreen(i.rel, n, r, dist, i.pos.xy, huv, travelled))
    {
        float2 e = min(huv, 1 - huv);
        refl = lerp(refl, gHdr.SampleLevel(gClamp, huv, 0).rgb, saturate(min(e.x, e.y) * 12) * saturate(1 - travelled / gRefl.z));
    }
    float NdotV = saturate(abs(dot(n, v)));
    float F = front ? 0.02 + 0.98 * pow(1 - NdotV, 5) : 0.08;
    float3 c = lerp(refr, refl, F);
    if (front) c += gSunColor.rgb * pow(saturate(dot(r, gSunDir.xyz)), 600) * 12 * shadowAt(i.rel, float3(0, 1, 0), 1, i.pos.xy);   // sun glint
    float f = saturate((dist - gFogRange.x) / max(gFogRange.y - gFogRange.x, 1));
    return float4(lerp(c, skyColor(dir), f * f), 1);
}

// ---------------------------------------------------------------- volumetrics: clouds and ground fog over the picture
float ign(float2 px) { return frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715)))); }   // even per-pixel noise: no banding

float4 PS_Volume(SkyOut i) : SV_Target
{
    int2 px = int2(i.pos.xy);
    float3 dir = normalize(gCamF.xyz + gCamR.xyz * (i.ndc.x * gProj.x * gProj.y) + gCamU.xyz * (i.ndc.y * gProj.x));
    if (gEnv.y > 0)   // heat shimmer: wobble far, low parts of the picture
    {
        float z0 = gSceneDepth.Load(int3(px, 0));
        float d0 = z0 >= 1 ? 400 : gDepth.x * gDepth.y / (gDepth.y - z0 * (gDepth.y - gDepth.x));
        float k = gEnv.y * saturate((d0 - 12) / 50) * saturate(1 - abs(dir.y) * 6);
        float2 w = float2(sin(i.pos.y * 0.21 + gVolume.z * 7 + i.pos.x * 0.04), cos(i.pos.x * 0.17 + gVolume.z * 5.3 + i.pos.y * 0.09));
        px = clamp(px + int2(round(w * k * 2.2)), int2(0, 0), int2(gDepth.zw) - 1);
    }
    float3 scene = gHdr.Load(int3(px, 0)).rgb;
    float zb = gSceneDepth.Load(int3(px, 0));
    float dist = zb >= 1 ? 1e9 : gDepth.x * gDepth.y / (gDepth.y - zb * (gDepth.y - gDepth.x)) / max(dot(dir, gCamF.xyz), 1e-3);
    float jitter = ign(i.pos.xy);
    float mu = dot(dir, gSunDir.xyz);
    float3 cam = gCamPos.xyz;

    // ---- clouds: march through the layer, light each step by looking a little towards the sun
    if (gVolume.x > 0)
    {
        float t0, t1;
        if (abs(dir.y) < 1e-4) { t0 = 1e9; t1 = 0; }
        else { float a = (kCloudBottom - cam.y) / dir.y, b = (kCloudTop - cam.y) / dir.y; t0 = max(min(a, b), 0); t1 = max(a, b); }
        if (cam.y > kCloudBottom && cam.y < kCloudTop) t0 = 0;
        t1 = min(min(t1, dist), t0 + 6000);
        if (t1 > t0)
        {
            const int N = 32;
            float dt = (t1 - t0) / N, T = 1;
            float3 S = 0;
            float phase = lerp(phaseHG(mu, -0.25), phaseHG(mu, 0.65), 0.6) * 12.566;
            float3 amb = lerp(gSkyHorizon.rgb, gSkyTop.rgb, 0.7) * 0.8;
            [loop] for (int k = 0; k < N && T > 0.02; k++)
            {
                float3 p = cam + dir * (t0 + (k + jitter) * dt);
                float d = cloudDensity(p, true);
                if (d <= 0) continue;
                float od = 0;   // how much cloud is between this point and the sun
                [unroll] for (int l = 1; l <= 4; l++) od += cloudDensity(p + gSunDir.xyz * (l * l * 6.0), false) * (2 * l - 1) * 6.0;
                od *= 1.6;
                float hf = saturate((p.y - kCloudBottom) / (kCloudTop - kCloudBottom));
                float powder = 1 - exp(-d * 60);                                       // dark-ish edges facing the sun: puffier look
                float3 light = (gSunColor.rgb * (exp(-od) + 0.25 * exp(-od * 0.25)) * phase * 0.5 * lerp(0.6, 1, powder) + amb * (0.3 + 0.5 * hf)) * (1 - gWeather.z * 0.7)
                             + gWeather2.y * float3(1.2, 1.3, 1.8) * (0.3 + hf);                // storm clouds are dark; lightning lights them up
                float a = exp(-d * dt);
                S += T * light * (1 - a); T *= a;
            }
            float far = saturate((t0 - 1500) / 4500);                                  // far clouds melt into the haze
            float3 haze = skyColor(dir);
            S = lerp(S, haze * (1 - T), far * far);
            scene = scene * T + S;
        }
    }

    // ---- ground fog: thick near the ground, lit by the sun where the shadow map says the sun reaches (light shafts)
    if (gVolume.y > 0 || gEnv.w > 0 || gLookFog.x > 0)
    {
        float tMax = min(dist, 192);
        const int N = 20;
        float dt = tMax / N, T = 1;
        float3 S = 0;
        float phase = lerp(phaseHG(mu, 0.55), phaseHG(mu, 0), 0.4) * 12.566;
        float3 amb = gAmbient.rgb * 0.55 * (1 - gWeather.z * 0.4);
        [loop] for (int k = 0; k < N; k++)
        {
            float t = (k + jitter) * dt;
            float3 rel = dir * t;
            float y = cam.y + rel.y;
            float dl = lookFogAt(y);
            float d = gVolume.y * exp(-max(y - 1, 0) / 7) + gVolume.y * 0.06 + gEnv.w + dl;  // a low layer, a thin haze everywhere, dust of the place, the look's fog
            float open = gOcc.x > 0.5 ? skyOpenAt(rel + float3(0, 0.5, 0)) : 1;   // true darkness: fog under a roof gets no sky light
            float sun = open;
            if (gSunDir.w > 0.5)
            {
                float4 s = mul(float4(rel, 1), gCascVP[3]);                 // the light shafts: the widest map is plenty
                float2 uv = float2(s.x, -s.y) * 0.5 + 0.5;
                if (all(uv > 0) && all(uv < 1) && s.z < 1) sun = gShadow.SampleCmpLevelZero(gShadowSamp, float3(uv, 3), s.z - 0.001);
            }
            float a = exp(-d * dt);
            float3 tint = lerp(float3(1, 1, 1), gLookFogCol.rgb * 1.6, dl / max(d, 1e-4));   // the look's fog in its own colour
            S += T * ((gSunColor.rgb * sun * phase * 0.18 + amb * open) * tint + gLookFogCol.rgb * gLookFogCol.w * dl / max(d, 1e-4)) * (1 - a); T *= a;
        }
        scene = scene * T + S;
    }
    // ---- lights in the air: every light near you glows in the fog and haze (spotlights make beams, with their shadows)
    if (gLookFog.w > 0.001 && gCl0.x > 0.5)
    {
        float tMax = min(dist, 64);
        const int NL = 24;
        float dt = tMax / NL, T = 1;
        float3 S = 0;
        [loop] for (int k = 0; k < NL; k++)
        {
            float t = (k + jitter) * dt;
            float3 rel = dir * t;
            float y = cam.y + rel.y;
            float dens = 0.012 + gVolume.y * exp(-max(y - 1, 0) / 7) * 0.5 + gEnv.w + lookFogAt(y) * 2;   // even clear air glows a little around lights
            uint2 cl = gClusters[clusterOf(rel)]; float3 Ls = 0;
            [loop] for (uint j = 0; j < min(cl.y, 24u); j++)
            {
                LightData L = gLightBuf[gClusterIdx[cl.x + j]];
                int kind = (int)round(L.col.w); if (kind >= 8) continue; int type = kind & 7;
                float3 lp = L.pos.xyz;
                if (type == 2) { float3 a = lp - L.ext.xyz, ab = L.ext.xyz * 2; lp = a + ab * saturate(dot(rel - a, ab) / max(dot(ab, ab), 1e-4)); }
                float3 Lv = lp - rel; float d = length(Lv); if (d >= L.pos.w) continue;
                float att = 1 - d / L.pos.w; att *= att; att /= 1 + d * d * 0.15;   // brighter right at the light
                if (type == 1) { float c = dot(-Lv / max(d, 1e-4), L.dir.xyz), c0 = L.dir.w; att *= smoothstep(c0, c0 + (1 - c0) * 0.35, c) * 2.5; }
                if (att <= 0) continue;
                if (L.ext.w >= 0) att *= lightShadow(L, rel, type);
                Ls += L.col.rgb * att;
            }
            float a = exp(-dens * dt);
            S += T * Ls * gLookFog.w * (1 - a) * 0.8; T *= a;
        }
        scene += S;
    }
    // under water / in lava: the view gets murky
    if (gEnvPart.z > 0.5)
    {
        float d = min(dist, 400);
        if (gEnvPart.z < 1.5)
        {
            scene *= exp(-min(d, 60) * float3(0.30, 0.06, 0.04));                                  // under water: red fades first
            scene = lerp(scene, (gAmbient.rgb * 0.7 + gSunColor.rgb * 0.12) * float3(0.05, 0.30, 0.36), 1 - exp(-d * 0.16));
        }
        else scene = lerp(scene, float3(1.6, 0.45, 0.08), 1 - exp(-d * 1.4));
    }
    return float4(scene, 1);
}

// ---------------------------------------------------------------- final picture: exposure, filmic curve, gamma
float3 tonemap(float3 x)
{
    // "ACES" fitted curve (Narkowicz): soft highlights, rich mid tones
    x *= 0.6;
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}
// ---------------------------------------------------------------- bloom: a chain of ever smaller, blurrier copies
// gOffset (b1) here: xy = one pixel of the source, z = how much of this level to add
Texture2D gBloomSrc : register(t0);
float4 PS_BloomDown(SkyOut i) : SV_Target
{
    float2 uv = i.ndc * float2(0.5, -0.5) + 0.5, t = gOffset.xy;
    float3 a = gBloomSrc.SampleLevel(gClamp, uv + t * float2(-2, -2), 0).rgb, b = gBloomSrc.SampleLevel(gClamp, uv + t * float2(0, -2), 0).rgb, c = gBloomSrc.SampleLevel(gClamp, uv + t * float2(2, -2), 0).rgb;
    float3 d = gBloomSrc.SampleLevel(gClamp, uv + t * float2(-2, 0), 0).rgb, e = gBloomSrc.SampleLevel(gClamp, uv, 0).rgb, f = gBloomSrc.SampleLevel(gClamp, uv + t * float2(2, 0), 0).rgb;
    float3 g = gBloomSrc.SampleLevel(gClamp, uv + t * float2(-2, 2), 0).rgb, h = gBloomSrc.SampleLevel(gClamp, uv + t * float2(0, 2), 0).rgb, k = gBloomSrc.SampleLevel(gClamp, uv + t * float2(2, 2), 0).rgb;
    float3 j = gBloomSrc.SampleLevel(gClamp, uv + t * float2(-1, -1), 0).rgb, l = gBloomSrc.SampleLevel(gClamp, uv + t * float2(1, -1), 0).rgb;
    float3 m = gBloomSrc.SampleLevel(gClamp, uv + t * float2(-1, 1), 0).rgb, n = gBloomSrc.SampleLevel(gClamp, uv + t * float2(1, 1), 0).rgb;
    float3 r = e * 0.125 + (a + c + g + k) * 0.03125 + (b + d + f + h) * 0.0625 + (j + l + m + n) * 0.125;
    return float4(min(r, 64), 1);   // no single fireflies
}
float4 PS_BloomUp(SkyOut i) : SV_Target
{
    float2 uv = i.ndc * float2(0.5, -0.5) + 0.5, t = gOffset.xy;
    float3 r = gBloomSrc.SampleLevel(gClamp, uv, 0).rgb * 4;
    r += (gBloomSrc.SampleLevel(gClamp, uv + float2(t.x, 0), 0).rgb + gBloomSrc.SampleLevel(gClamp, uv - float2(t.x, 0), 0).rgb + gBloomSrc.SampleLevel(gClamp, uv + float2(0, t.y), 0).rgb + gBloomSrc.SampleLevel(gClamp, uv - float2(0, t.y), 0).rgb) * 2;
    r += gBloomSrc.SampleLevel(gClamp, uv + t, 0).rgb + gBloomSrc.SampleLevel(gClamp, uv - t, 0).rgb + gBloomSrc.SampleLevel(gClamp, uv + float2(t.x, -t.y), 0).rgb + gBloomSrc.SampleLevel(gClamp, uv + float2(-t.x, t.y), 0).rgb;
    return float4(r / 16 * gOffset.z, 1);
}

Texture2D gBloom : register(t1);
// the visor's look (Player editor > Visors): over the world picture only, never over the HUD
cbuffer VisorCB : register(b6)
{
    float4 gVTint;    // rgb tint, w how much
    float4 gVGrade;   // x colourfulness, y contrast, z brightness, w dark edges
    float4 gVLines;   // x scan lines, y how many, z how fast they move, w grain
    float4 gVEdge;    // rgb outline colour, w outlines
    float4 gVMisc;    // x colour fringes (pixels), y time (s), z heat colours, w switching flash
    float4 gVHud;     // the helmet's 3D parts sit in this slice of depth (x from, y to): the visor's look leaves them alone
    float4 gVOut;     // scan outlines: x on, y width (pixels), z fill inside, w pulse
};
Texture2D gScanMask : register(t11);   // scannable things, each in its marker colour (alpha: there is one)
// the colour look (Looks): tone curve, exposure, contrast and the grade after it
cbuffer LookCB : register(b10)
{
    float4 gLk0;       // x tone curve (0 Classic, 1 Film, 2 Neutral), y exposure (stops), z contrast, w colourfulness
    float4 gLk1;       // x warmth (-1 cold .. 1 warm), y lens fringes, z grain, w vignette
    float4 gLkShadow;  // rgb: colour in the dark parts (linear), w how much
    float4 gLkHigh;    // rgb: colour in the bright parts, w how much
    float4 gLkAdapt;   // x how much the eye adapts, y seconds since last frame, z how fast, w -
};
Texture2D gExposure : register(t16);   // 1x1: the eye's adaptation (stops), see PS_Adapt
Texture2D gLumTex : register(t17);     // log brightness of the picture, with mips (PS_LumLog)
// "AgX" (Troy Sobotka's film-like curve; polynomial fit by Benjamin Wrensch): very bright colours bleach toward white
// like film instead of turning into flat neon blobs
float3 agxCurve(float3 x)
{
    float3 x2 = x * x, x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}
float3 tonemapAgX(float3 v)
{
    const float3x3 M = float3x3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
                                0.0784335999999992, 0.878468636469772, 0.0784336,
                                0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const float3x3 Mi = float3x3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
                                 -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                                 -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float lo = -12.47393, hi = 4.026069;
    v = mul(max(v * 0.65, 1e-10), M);
    v = (clamp(log2(v), lo, hi) - lo) / (hi - lo);
    v = mul(agxCurve(v), Mi);
    return pow(saturate(v), 2.2);
}
float3 tonemapNeutral(float3 c)   // Khronos "PBR Neutral": colours stay as they are until they get very bright
{
    const float start = 0.76, desat = 0.15;
    float x = min(c.r, min(c.g, c.b)); float off = x < 0.08 ? x - 6.25 * x * x : 0.04; c -= off;
    float peak = max(c.r, max(c.g, c.b)); if (peak < start) return max(c, 0);
    const float d = 1 - start; float np = 1 - d * d / (peak + d - start);
    c *= np / peak; float g = 1 - 1 / (desat * (peak - np) + 1);
    return saturate(lerp(c, np.xxx, g));
}
float lookExposure() { return exp2(gLk0.y + gLkAdapt.x * gExposure.Load(int3(0, 0, 0)).r); }
float3 postBase(int2 px, float2 uv, float expo)   // exposure, glow, contrast, the tone curve (before gamma)
{
    float3 c = gHdr.Load(int3(px, 0)).rgb;
    c = lerp(c, gBloom.SampleLevel(gClamp, uv, 0).rgb / 6, gExtra.y);              // soft glow around bright things (6 levels added up)
    c *= gFogRange.z * expo;
    float l0 = dot(c, float3(0.2126, 0.7152, 0.0722));
    c *= pow(max(l0, 1e-6) / 0.18, max(gLk0.z, 0.05) - 1);                           // contrast around middle grey
    if (gLk0.x > 1.5) c = tonemapNeutral(c);
    else if (gLk0.x > 0.5) c = tonemapAgX(c);
    else { c = tonemap(c); float l = dot(c, float3(0.2126, 0.7152, 0.0722)); c = lerp(l.xxx, c, 1.02); }
    return max(c, 0);
}
float postLum(int2 px, float2 uv, float expo) { return dot(postBase(px, uv, expo), float3(0.2126, 0.7152, 0.0722)); }
float3 lookGrade(float3 c)   // warmth, colourfulness, colours in the shadows and highlights
{
    const float3 W = float3(0.2126, 0.7152, 0.0722);
    c *= max(1 + gLk1.x * float3(0.2, 0.02, -0.25), 0);
    float l = dot(c, W); c = lerp(l.xxx, c, gLk0.w);
    l = saturate(dot(max(c, 0), W));
    float sw = (1 - smoothstep(0.0, 0.45, l)) * gLkShadow.w, hw = smoothstep(0.3, 1.0, l) * gLkHigh.w;
    float3 st = min(gLkShadow.rgb / max(dot(gLkShadow.rgb, W), 0.02), 4), ht = min(gLkHigh.rgb / max(dot(gLkHigh.rgb, W), 0.02), 4);
    c = lerp(c, c * st, sw) + gLkShadow.rgb * sw * 0.025;    // (a little lift too, so pure black takes the colour)
    c = lerp(c, c * ht, hw);
    return max(c, 0);
}
// auto exposure: 1) the picture's log brightness (weighted to the middle) into a small texture, mips average it
float4 PS_LumLog(SkyOut i) : SV_Target
{
    float2 uv = i.ndc * float2(0.5, -0.5) + 0.5;
    float3 c = gHdr.SampleLevel(gClamp, uv, 0).rgb * gFogRange.z;
    float l = clamp(log2(max(dot(c, float3(0.2126, 0.7152, 0.0722)), 1e-4)), -12, 8);
    float w = 1 - 0.75 * saturate(dot(i.ndc, i.ndc) * 0.5);
    return float4(l * w, w, 0, 1);
}
// 2) the eye moves toward "middle grey on average", a bit each frame (1x1 texture, ping-pong); clamped to +-2 stops
float4 PS_Adapt(SkyOut i) : SV_Target
{
    float2 a = gLumTex.SampleLevel(gClamp, float2(0.5, 0.5), 12).rg;
    float avg = a.x / max(a.y, 1e-4);
    float target = clamp(log2(0.18) - avg, -2, 2);
    float prev = gExposure.Load(int3(0, 0, 0)).r; if (!(prev == prev) || abs(prev) > 4) prev = 0;
    return float4(prev + (target - prev) * (1 - exp(-gLkAdapt.y * gLkAdapt.z)), 0, 0, 1);
}
float3 heatPalette(float t)   // cold (dark blue) .. hot (white)
{
    t = saturate(t);
    float3 a = float3(0.02, 0.0, 0.18), b = float3(0.35, 0.0, 0.6), c = float3(0.95, 0.1, 0.3), d = float3(1.0, 0.62, 0.0), e = float3(1.0, 1.0, 0.85);
    if (t < 0.25) return lerp(a, b, t / 0.25);
    if (t < 0.5) return lerp(b, c, (t - 0.25) / 0.25);
    if (t < 0.75) return lerp(c, d, (t - 0.5) / 0.25);
    return lerp(d, e, (t - 0.75) / 0.25);
}
float postHash(float2 p) { float3 p3 = frac(float3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return frac((p3.x + p3.y) * p3.z); }
float4 PS_Post(SkyOut i) : SV_Target
{
    int2 px = int2(i.pos.xy);
    float2 uv = i.ndc * float2(0.5, -0.5) + 0.5;
    float dz = gSceneDepth.Load(int3(px, 0));
    if (dz >= gVHud.x && dz < gVHud.y) {   // the helmet's own parts: no visor look, no grade, no eye adaptation on them (the arm cannon gets it)
        float3 h = postBase(px, uv, 1);
        float2 v0 = i.ndc; h *= 1 - 0.18 * dot(v0, v0) * 0.5;
        return float4(pow(max(h, 0), 1 / 2.2), 1);
    }
    float expo = lookExposure();
    float3 c = postBase(px, uv, expo);
    {   // colour fringes: red and blue a little apart - the visor's (sideways) and the lens's (toward the edges)
        float2 rad = i.ndc * float2(1, -1) * gLk1.y * 0.004 * gDepth.z;
        float2 of = float2(gVMisc.x, 0) + rad;
        if (dot(of, of) > 0.25) {
            int2 o = int2(round(of)); float2 ou = of / max(gDepth.zw, 1);
            c.r = postBase(px + o, uv + ou, expo).r; c.b = postBase(px - o, uv - ou, expo).b;
        }
    }
    c = lookGrade(c);
    const float3 W = float3(0.2126, 0.7152, 0.0722);
    if (gVEdge.w > 0.001) {   // outlines where brightness jumps
        float e = abs(postLum(px + int2(1, 0), uv, expo) - postLum(px - int2(1, 0), uv, expo)) + abs(postLum(px + int2(0, 1), uv, expo) - postLum(px - int2(0, 1), uv, expo));
        c += gVEdge.rgb * saturate(e * 3) * gVEdge.w;
    }
    float l = dot(c, W);
    c = lerp(c, heatPalette(pow(saturate(l), 0.8)), gVMisc.z);
    l = dot(c, W); c = lerp(l.xxx, c, gVGrade.x);
    c = (c - 0.5) * gVGrade.y + 0.5; c *= gVGrade.z;
    l = dot(max(c, 0), W); c = lerp(c, l * gVTint.rgb * 1.4, gVTint.w);
    c *= 1 - gVLines.x * (0.5 + 0.5 * sin(uv.y * gVLines.y * 6.2832 + gVMisc.y * gVLines.z));
    c += (postHash(float2(px) + frac(gVMisc.y * 7.13) * 917) - 0.5) * gVLines.w;
    if (gVOut.x > 0.5) {   // the Scan Visor: an outline (and a faint fill) around everything scannable, in its marker's colour
        float4 m = gScanMask.Load(int3(px, 0)); float4 edge = 0; int r = (int)gVOut.y;
        [loop] for (int y = -r; y <= r; y += max(1, r / 2)) [loop] for (int x = -r; x <= r; x += max(1, r / 2)) { float4 n = gScanMask.Load(int3(px + int2(x, y), 0)); if (n.a > edge.a) edge = n; }
        float pulse = 0.75 + 0.25 * sin(gVMisc.y * 4 + gVOut.w);
        if (m.a < 0.5 && edge.a > 0.5) c = lerp(c, edge.rgb, 0.85 * pulse);   // just outside it: the outline
        else if (m.a > 0.5) c = lerp(c, m.rgb, gVOut.z * pulse);             // on it: a faint tint
    }
    {   // film grain (stronger in the dark parts, like film)
        float g = postHash(float2(px) * 1.37 + frac(gVMisc.y * 3.71) * 613) + postHash(float2(px) * 0.71 + frac(gVMisc.y * 5.3) * 271) - 1;
        c += g * gLk1.z * 0.07 * (1.1 - saturate(dot(c, W)));
    }
    float2 v = i.ndc; c *= 1 - gLk1.w * dot(v, v) * 0.5;               // the look's vignette
    c *= 1 - gVGrade.w * dot(v, v) * 0.5;                               // ... and the visor's own
    c = lerp(c, float3(1, 1, 1), saturate(gVMisc.w) * 0.6);             // switching visors: a flash
    return float4(pow(max(c, 0), 1 / 2.2), 1);
}

// ---------------------------------------------------------------- the 3D viewer: a model alone, with simple light
cbuffer Preview : register(b2)
{
    row_major float4x4 gPrevVP;     // camera at the origin
    float4 gPrevLight;              // xyz: towards the light
};
struct PrevOut { float4 pos : SV_Position; float3 uvl : TEXCOORD0; float3 n : NORMAL; float ao : TEXCOORD1; float glow : TEXCOORD2; };
PrevOut VS_Preview(VSIn i)
{
    PrevOut o;
    float kind = floor(i.layer / 16384);
    o.uvl = float3(i.uv, i.layer - kind * 16384);
    o.pos = mul(float4(meshToRel(i.pos), 1), gPrevVP);
    float3 nl = unpackNormal(i.normal);
    o.n = normalize(gAxisX.xyz * nl.x + gAxisY.xyz * nl.y + gAxisZ.xyz * nl.z);
    o.ao = (i.lit & 255) / 255.0;
    o.glow = (i.lit >> 20) & 1;
    return o;
}
float4 PS_Preview(PrevOut i, bool front : SV_IsFrontFace) : SV_Target
{
    float4 t = albedoAt(i.uvl);
    clip(t.a - 0.5);
    float3 n = normalize(front ? i.n : -i.n);
    float d = saturate(dot(n, normalize(gPrevLight.xyz)));
    float3 c = t.rgb * (0.45 + 0.3 * (0.5 + 0.5 * n.y) + 0.9 * d) * lerp(0.6, 1, i.ao);
    if (i.glow > 0.5) c = t.rgb * 1.3;
    if (gPrevLight.w > 0.5)                                             // (the HUD's 3D parts) emission maps glow here too
    {
        float3 em = 0;
        if (i.uvl.z > 16000) { if (gMM1.z > 0) em = gMeshEmit.Sample(gSamp, i.uvl.xy).rgb * gMM1.z; }
        else { TexMat m = gMats[(uint)round(i.uvl.z)]; em = gEmitTex.Sample(gSamp, i.uvl).rgb * m.b.z; }
        c += em * 2.5;
    }
    return float4(pow(saturate(c), 1 / 2.2), 1);
}

// ---------------------------------------------------------------- door forcefields and the test beam (added on top, glowing)
// ChunkCB here: gOffset.xyz corner (relative to the camera), w 0 field / 1 beam; gAxisX.xyz across (w: hit u), gAxisY.xyz up (w: hit v);
// gAxisZ.rgb colour (linear), w how much of the field is there (1 full, 0 gone)
struct FieldOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float3 rel : TEXCOORD1; };
FieldOut VS_Field(uint id : SV_VertexID)
{
    static const float2 c[6] = { float2(0, 0), float2(1, 0), float2(1, 1), float2(0, 0), float2(1, 1), float2(0, 1) };
    FieldOut o; o.uv = c[id];
    o.rel = gOffset.xyz + gAxisX.xyz * o.uv.x + gAxisY.xyz * o.uv.y;
    o.pos = mul(float4(o.rel, 1), gViewProj);
    return o;
}
float hexEdge(float2 p)   // 0 in the middle of a hexagon, 1 on its edge
{
    const float2 s = float2(1, 1.7320508);
    float2 a = fmod(p, s) - s * 0.5, b = fmod(p - s * 0.5, s) - s * 0.5;
    a = abs(a); b = abs(b);
    float2 g = dot(a, a) < dot(b, b) ? a : b;
    return saturate(max(dot(g, normalize(s)), g.x) * 2);
}
float4 PS_Field(FieldOut i) : SV_Target
{
    float t = gVolume.z;
    if (gOffset.w > 0.5)   // the beam: a bright core with a soft glow around it
    {
        float a = 1 - abs(i.uv.y * 2 - 1);
        float core = pow(a, 6) * 3 + pow(a, 1.5) * 0.6;
        return float4(gAxisZ.rgb * core * smoothstep(0, 0.25, i.uv.x), 0);
    }
    float w = length(gAxisX.xyz), h = length(gAxisY.xyz);
    float2 p = float2(i.uv.x * w, i.uv.y * h);                         // blocks across the opening
    float hex = hexEdge(p * 3.2 + float2(0, t * 0.15));
    float scan = 0.5 + 0.5 * sin(p.y * 9 - t * 4);
    float pulse = 0.85 + 0.15 * sin(t * 2.3);
    float2 e = min(i.uv, 1 - i.uv) * float2(w, h);
    float rim = 1 - smoothstep(0, 0.12, min(e.x, e.y));                // brighter at the frame
    float n = gNoise.SampleLevel(gLinear, float3(p * 0.6, t * 0.05), 0).x;
    // dissolving: a hole grows from where the shot hit, with a bright burning edge
    float2 hit = float2(gAxisX.w * w, gAxisY.w * h);
    float reach = (1 - gAxisZ.w) * (max(w, h) * 1.4 + 0.5);
    float d = length(p - hit) + (n - 0.5) * 0.6;
    float keep = smoothstep(reach - 0.05, reach + 0.05, d);
    float burn = gAxisZ.w < 0.999 ? (1 - smoothstep(0, 0.18, abs(d - reach))) * keep : 0;
    float glow = (0.10 + 0.55 * pow(hex, 6) + 0.12 * scan + 0.5 * rim) * pulse * keep;
    float fade = gAxisZ.w < 0.999 && gAxisZ.w > 0 && reach < 0.06 ? gAxisZ.w : 1;   // coming back: fades in
    return float4(gAxisZ.rgb * (glow * 1.6 * fade + burn * 4), 0);
}

// ---------------------------------------------------------------- particles: quads facing you (sparks: stretched along their flight)
struct PIn { float3 pos : POSITION; float2 corner : TEXCOORD0; float4 col : COLOR; float4 info : TEXCOORD1; float3 vel : TEXCOORD2; };   // info: size, turn, shape, streak
struct POut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR; float3 rel : TEXCOORD1; float shape : TEXCOORD2; float seed : TEXCOORD3; float soft : TEXCOORD4; };
POut VS_Particle(PIn i)
{
    POut o;
    float size = i.info.x;
    float3 view = normalize(i.pos), p;
    bool streak = fmod(i.info.w, 2) > 0.5;   // info.w: 1 = streak, +2 = soft (smoke) instead of glowing
    if (streak)
    {
        float3 v = i.vel - dot(i.vel, view) * view; float sp = length(v);
        float3 ax = sp > 1e-4 ? v / sp : gCamU.xyz;
        float3 side = normalize(cross(view, ax));
        float len = size + sp * 0.045;
        p = i.pos + ax * (i.corner.y * len * 0.5) + side * (i.corner.x * size * 0.25);
    }
    else
    {
        float c = cos(i.info.y), s = sin(i.info.y);
        float2 k = float2(c * i.corner.x - s * i.corner.y, s * i.corner.x + c * i.corner.y);
        p = i.pos + (gCamR.xyz * k.x + gCamU.xyz * k.y) * size * 0.5;
    }
    o.pos = mul(float4(p, 1), gViewProj); o.uv = i.corner; o.col = i.col; o.rel = p; o.shape = i.info.z; o.seed = i.info.y; o.soft = i.info.w >= 1.5 ? 1 : 0;
    return o;
}
float4 PS_Particle(POut i) : SV_Target
{
    float2 q = i.uv; float r = length(q), a;
    int shape = (int)(i.shape + 0.5);
    if (shape == 1) a = exp(-q.x * q.x * 6) * saturate(1 - q.y * q.y);                                  // spark
    else if (shape == 2)                                                                                // smoke puff
    {
        float n = gNoise.SampleLevel(gLinear, float3(q * 0.35 + i.seed * 0.11, i.seed * 0.05), 0).x;
        a = saturate(1 - r) * saturate(1 - r) * (0.45 + 0.9 * n);
    }
    else if (shape == 3) a = max(abs(q.x), abs(q.y)) < 0.75 ? 1 : 0;                                   // pixel
    else if (shape == 4) a = exp(-pow((r - 0.7) * 7, 2));                                                // ring
    else if (shape == 5) a = exp(-r * r * 9) + exp(-abs(q.x) * 14) * saturate(1 - abs(q.y)) + exp(-abs(q.y) * 14) * saturate(1 - abs(q.x));   // flare
    else a = exp(-r * r * 4.5) * saturate(1 - r);                                                        // soft dot
    // soft where it meets the world
    float zb = gSceneDepth.Load(int3(i.pos.xy, 0));
    float scene = linDepth(zb), here = dot(i.rel, gCamF.xyz);
    a *= saturate((scene - here) * 3);
    if (i.soft < 0.5) return float4(i.col.rgb * a, 0);                                                  // glowing: added on top
    float3 lit = i.col.rgb * (gAmbient.rgb * 1.3 + gSunColor.rgb * 0.35 + dynLights(i.rel, -normalize(i.rel)) * 0.5);   // soft (smoke): lit by the sky, the sun and lights
    return float4(lit, saturate(a * i.col.a));
}

// ---------------------------------------------------------------- the HUD (Player editor): bent planes in camera space, their picture premultiplied
// gPrevVP: camera space -> screen; gPrevLight.x: how much of it shows (opacity)
Texture2D gHudTex : register(t0);
struct HudOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
HudOut VS_Hud(float3 p : POSITION, float2 uv : TEXCOORD0)
{
    HudOut o; o.pos = mul(float4(p, 1), gPrevVP); o.uv = uv; return o;
}
// gPrevLight.y: the glow around what is drawn (the plane's smaller copies, blurred, added on top: a bloom)
float4 PS_Hud(HudOut i) : SV_Target
{
    float4 c = gHudTex.Sample(gClamp, i.uv);
    float bloom = gPrevLight.y;
    if (bloom > 0)
    {
        float3 g = gHudTex.SampleLevel(gClamp, i.uv, 2.5).rgb * 0.45 + gHudTex.SampleLevel(gClamp, i.uv, 4).rgb * 0.45 + gHudTex.SampleLevel(gClamp, i.uv, 5.5).rgb * 0.5;
        c.rgb += g * bloom * 1.6;                                                         // light only: the see-through parts stay see-through
    }
    return c * gPrevLight.x;
}
// a picture copied over the whole target (the camera preview's background)
struct BlitOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
BlitOut VS_Blit(uint id : SV_VertexID)
{
    BlitOut o; float2 t = float2((id << 1) & 2, id & 2); o.uv = t; o.pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1); return o;
}
float4 PS_Blit(BlitOut i) : SV_Target { return float4(gHudTex.Sample(gClamp, i.uv).rgb, 1); }

// ---------------------------------------------------------------- a door's forcefield on the door's own parts (a shell just outside them)
// ChunkCB: gOffset.xyz where it was hit (relative to the camera), w how big the door is; gAxisZ.rgb colour, w how much of the field is there
struct FMOut { float4 pos : SV_Position; float3 rel : TEXCOORD0; float3 n : NORMAL; };
FMOut VS_FieldMesh(float3 p : POSITION, float3 n : NORMAL)
{
    FMOut o; o.rel = p; o.n = n; o.pos = mul(float4(p, 1), gViewProj); return o;
}
float4 PS_FieldMesh(FMOut i) : SV_Target
{
    float t = gVolume.z; float3 n = normalize(i.n); float3 w = i.rel + gCamPos.xyz; float3 a = abs(n);
    float2 uv = a.x > a.y && a.x > a.z ? w.zy : (a.y > a.z ? w.xz : w.xy);           // the hexagons lie on the face
    float hex = hexEdge(uv * 3.2 + float2(0, t * 0.15));
    float scan = 0.5 + 0.5 * sin(w.y * 9 - t * 4);
    float pulse = 0.85 + 0.15 * sin(t * 2.3);
    float rim = pow(1 - abs(dot(n, normalize(-i.rel))), 3);                             // brighter where you see it edge-on
    float nz = gNoise.SampleLevel(gLinear, float3(uv * 0.6, t * 0.05), 0).x;
    float reach = (1 - gAxisZ.w) * (gOffset.w * 1.4 + 0.5);
    float d = length(i.rel - gOffset.xyz) + (nz - 0.5) * 0.6;
    float keep = smoothstep(reach - 0.05, reach + 0.05, d);
    float burn = gAxisZ.w < 0.999 ? (1 - smoothstep(0, 0.18, abs(d - reach))) * keep : 0;
    float glow = (0.08 + 0.5 * pow(hex, 6) + 0.1 * scan + 0.6 * rim) * pulse * keep;
    float fade = gAxisZ.w < 0.999 && gAxisZ.w > 0 && reach < 0.06 ? gAxisZ.w : 1;
    return float4(gAxisZ.rgb * (glow * 1.6 * fade + burn * 4), 0);
}

// ---------------------------------------------------------------- impact spheres: a glowing shell of moving plasma (VS_FieldMesh places it)
// ChunkCB: gOffset.xyz its middle (relative to the camera), w how far it has grown (0..1); gAxisX.x radius now, y a seed;
// gAxisZ.rgb colour, w how much is left (fades)
float4 PS_ImpactSphere(FMOut i) : SV_Target
{
    float3 n = normalize(i.n), v = normalize(-i.rel);
    float facing = abs(dot(n, v)), t = gVolume.z, grown = gOffset.w;
    float3 d = normalize(i.rel - gOffset.xyz);                                            // where on the ball (moves with it, not with the world)
    float seed = gAxisX.y;
    // two layers of noise flowing over the ball in different directions: a moving, boiling texture
    float a = gNoise.SampleLevel(gLinear, d * 0.9 + float3(seed, t * 0.55, seed * 0.7), 0).x;
    float b = gNoise.SampleLevel(gLinear, d * 2.3 + float3(-t * 0.8, seed * 0.3, t * 0.35), 0).y;
    float plasma = saturate(a * 1.3 - 0.25) * (0.55 + 0.9 * b);
    float veins = pow(saturate(1 - abs(a - 0.5) * 6), 3);                                // bright thin lines where the noise crosses the middle
    float rim = pow(1 - facing, 2.2);                                                     // bright where you see it edge-on: a bubble
    float core = pow(facing, 2.5) * (1 - grown) * (1 - grown) * 0.5;                      // at first a hot middle
    float glow = rim * 1.3 + 0.03 + plasma * plasma * (0.35 + rim * 0.8) + veins * 0.7 + core;
    float3 col = gAxisZ.rgb * glow + float3(1, 0.95, 0.85) * (core * 0.25 + veins * rim * 0.25);   // a little white where it is hottest
    return float4(col * gAxisZ.w * 0.55, 0);                                              // (each side of the shell adds its light)
}

// ---------------------------------------------------------------- beam shots: a glowing ball of energy (VS_FieldMesh places it)
// ChunkCB: gOffset.xyz its middle (relative to the camera), w its radius; gAxisX.xyz the way it flies, w 1 = charged;
// gAxisY.x a seed, y 1 = the soft halo around it, z its age (s); gAxisZ.rgb colour
float4 PS_BeamOrb(FMOut i) : SV_Target
{
    float3 n = normalize(i.n), v = normalize(-i.rel);
    float facing = saturate(abs(dot(n, v))), t = gVolume.z + gAxisY.x * 7.1, charged = gAxisX.w;
    float3 col = gAxisZ.rgb;
    if (gAxisY.y > 0.5) {                                                                 // the halo: soft light around the ball, strongest in the middle
        float h = pow(facing, 4);
        return float4(col * h * (0.12 + 0.12 * charged), 0);
    }
    float3 d = normalize(i.rel - gOffset.xyz), f = gAxisX.xyz;
    // energy streaming backwards over the ball (along the way it flies) and swirling around it
    float along = dot(d, f);
    float swirl = gNoise.SampleLevel(gLinear, d * (1.6 + charged) + f * (t * 2.2) + float3(0, t * 0.7, 0), 0).x;
    float fine = gNoise.SampleLevel(gLinear, d * 4.0 - f * (t * 3.5) + float3(t * 0.9, 0, gAxisY.x), 0).y;
    float bands = 0.5 + 0.5 * sin((along * 9 - t * 14) + swirl * 6);                      // rings flowing back
    float crackle = pow(saturate(1 - abs(fine - 0.5) * (5 - charged * 2)), 4) * (0.6 + charged * 1.6);   // lightning-like lines (more when charged)
    float core = pow(facing, 3);                                                          // white-hot in the middle
    float rim = pow(1 - facing, 2);                                                       // the colour at the edges
    float energy = 0.6 + 0.8 * swirl * bands;
    // a solid ball of light: saturated colour, a white-hot middle, energy and crackle moving over it (it covers what is behind)
    float3 c = col * (0.55 + energy * 0.55 + crackle * 0.9 + rim * 0.5) * 1.3 + float3(1, 1, 1) * core * (0.7 + charged * 0.5);
    float flicker = 0.92 + 0.08 * sin(t * 40 + gAxisY.x * 13);
    float cover = saturate(0.45 + 0.55 * facing);                                         // the very edge lets a little through: soft, not a hard disc
    return float4(c * flicker * cover, cover);                                            // premultiplied
}

// ---------------------------------------------------------------- the map: a hologram of what has been seen
cbuffer MapCB : register(b5)
{
    row_major float4x4 gMapVP;
    float4 gMapEye;       // xyz where the map camera is (world), w the clock now (s, the same clock as `born`)
    float4 gMapCenter;    // xyz the player (world), w when the map was opened (the build-up sweep starts there)
    float4 gMapMode;      // x 0 solid, 1 see-through edges (added on top); z 1 = the HUD minimap (see-through, fades out at w blocks)
};
struct MapIn { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; float2 size : TEXCOORD1; float born : TEXCOORD2; float4 col : COLOR; };
struct MapOut { float4 pos : SV_Position; float3 w : TEXCOORD0; float3 n : NORMAL; float2 uv : TEXCOORD1; float2 size : TEXCOORD2; float born : TEXCOORD3; float4 col : COLOR; };
MapOut VS_Map(MapIn i)
{
    MapOut o; o.pos = mul(float4(i.p, 1), gMapVP); o.w = i.p; o.n = i.n; o.uv = i.uv; o.size = i.size; o.born = i.born; o.col = i.col; return o;
}
// col.a: 0 a panel you have seen yourself, 0.25 a panel only downloaded at a Map Station (rgb its tint), 1 a door (rgb its colour), 2 you
float4 PS_Map(MapOut i, bool front : SV_IsFrontFace) : SV_Target
{
    float t = gMapEye.w; bool mini = gMapMode.z > 0.5;
    float3 n = normalize(front ? i.n : -i.n), v = normalize(gMapEye.xyz - i.w);
    float facing = saturate(abs(dot(n, v)));
    float2 e2 = min(i.uv, i.size - i.uv); float edge = min(e2.x, e2.y);                 // blocks to the panel's border
    float px = max(fwidth(edge), 1e-4);
    float outline = 1 - smoothstep(px * 0.7, px * 2.4, edge);                            // a crisp line around every panel
    float2 gd = abs(frac(i.uv + 0.5) - 0.5) / max(fwidth(i.uv), 1e-4);
    float grid = (1 - saturate(min(gd.x, gd.y) - 0.25)) * saturate(1.5 - max(fwidth(i.uv.x), fwidth(i.uv.y)) * 3);   // one line per block (fades out far away)
    float3 d = i.w - gMapCenter.xyz;
    float sweep = (t - gMapCenter.w) * 60 - length(d) * 0.8;                              // the map builds up outward from you when it opens
    float appear = saturate(sweep / 6) * saturate((t - i.born) / 0.7);                    // ... and newly seen parts fade in
    float front_ = saturate(1 - abs(sweep - 3) / 3);                                      // the bright front of that sweep
    float fade = 1;
    if (mini) { fade = (1 - smoothstep(gMapMode.w * 0.7, gMapMode.w, length(d.xz))) * (1 - smoothstep(5, 11, abs(d.y - 1))); front_ = 0; }   // a round window around you, your level
    float kind = i.col.a; float3 base = i.col.rgb;
    if (kind > 1.5) {                                                                     // you: solid, bright, pulsing
        float pulse = 0.85 + 0.15 * sin(t * 6);
        return float4(base * (0.55 + 0.7 * facing) * pulse, 1);
    }
    float3 c;
    if (kind > 0.5) {                                                                     // a door: filled with its colour, the size of the door
        float scan = 0.8 + 0.2 * sin(i.w.y * 7 - t * 3);
        c = base * (0.75 * scan + 1.6 * outline) * appear;
        if (gMapMode.x > 0.5) c *= 0.3;
    } else if (gMapMode.x > 0.5) c = base * outline * 0.16 * appear;                      // through the walls: only the edges, faint
    else {
        bool down = kind > 0.1;                                                           // downloaded, not visited: see-through, no fill, slowly breathing
        float shade = n.y > 0.5 ? 1.0 : (n.y < -0.5 ? 0.42 : 0.68);                      // floors brightest, ceilings dimmest
        float rim = pow(1 - facing, 2);
        float lines = 0.93 + 0.07 * sin(i.w.y * 6.2832 - t * 2.5);                        // slow scan lines
        c = down ? base * shade * (0.05 + 0.12 * rim + 0.1 * grid + 0.75 * outline) * (0.8 + 0.2 * sin(t * 1.7)) * appear
                 : base * shade * (0.15 + 0.25 * rim + 0.12 * grid + 1.0 * outline) * lines * appear;
        c += base * front_ * (down ? 0.3 : 0.6) + float3(0.7, 1, 1) * front_ * outline * 0.4;
    }
    if (mini) { c *= fade * 1.25; return float4(c, saturate(max(c.r, max(c.g, c.b)) * 1.4)); }   // the HUD: premultiplied, see-through where it is dark
    return float4(c, 1);
}

// ---------------------------------------------------------------- a flat colour (the Scan Visor's outline mask)
cbuffer FlatCB : register(b7) { float4 gFlat; };
float4 PS_Flat(VSOut i) : SV_Target { return gFlat; }

// ---------------------------------------------------------------- the screens' compositor (Screens editor): one pass per node (blur and glare take a few)
cbuffer CompCB : register(b8)
{
    float4 gCP0;    // the node's values 0..3
    float4 gCP1;    // x, y values 4..5, z mode, w which operation
    float4 gCC0, gCC1, gCC2;   // its colours
    float4 gCSize;  // w, h, 1 / w, 1 / h
    float4 gCMisc;  // x time (s), y pixels per 1080, zw a blur's step (pixels)
    float4 gCRect;  // a picture's place on the screen (u0, v0, u1, v1)
};
Texture2D gCompA : register(t12);
Texture2D gCompB : register(t13);
float4 compA(float2 uv) { return gCompA.SampleLevel(gClamp, uv, 0); }
float compLum(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
float3 rgb2hsv(float3 c)
{
    float4 K = float4(0, -1.0 / 3, 2.0 / 3, -1); float4 p = lerp(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
    float4 q = lerp(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r)); float d = q.x - min(q.w, q.y);
    return float3(abs(q.z + (q.w - q.y) / (6 * d + 1e-10)), d / (q.x + 1e-10), q.x);
}
float3 hsv2rgb(float3 c) { float3 p = abs(frac(c.xxx + float3(1, 2.0 / 3, 1.0 / 3)) * 6 - 3); return c.z * lerp(1, saturate(p - 1), c.y); }
float compHash(float2 p) { return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453); }
float4 PS_Comp(BlitOut i) : SV_Target
{
    int op = (int)round(gCP1.w); float2 uv = i.uv; float4 a = 0;
    if (op != 1 && op != 2 && op != 6 && op != 14 && op != 15 && op != 16) a = compA(uv);
    switch (op)
    {
    case 0: return float4(a.rgb * gCP0.x, a.a);                                                   // copy (with a fade)
    case 1: return float4(lerp(gCC0.rgb, gCC1.rgb, uv.y), 1);                                    // a gradient
    case 2: { float2 q = (uv - gCRect.xy) / max(gCRect.zw - gCRect.xy, 1e-5);                  // a picture where it sits
              if (any(q < 0) || any(q > 1)) return float4(0, 0, 0, 1);
              float4 c = gCompA.SampleLevel(gLinear, q, 0); return float4(c.rgb * c.a, 1); }
    case 3: return float4(a.rgb * (1 - gCP0.x), a.a);                                             // darker
    case 4: { float4 b = gCompB.SampleLevel(gClamp, uv, 0) * gCP0.x; return a * (1 - b.a) + b; }  // alpha over (premultiplied)
    case 5: { float4 b = gCompB.SampleLevel(gClamp, uv, 0); int m = (int)round(gCP1.z); float3 r;
              if (m == 1) r = a.rgb + b.rgb; else if (m == 2) r = a.rgb * b.rgb; else if (m == 3) r = 1 - (1 - a.rgb) * (1 - b.rgb);
              else if (m == 4) r = lerp(2 * a.rgb * b.rgb, 1 - 2 * (1 - a.rgb) * (1 - b.rgb), step(0.5, a.rgb));
              else if (m == 5) r = abs(a.rgb - b.rgb); else if (m == 6) r = max(a.rgb, b.rgb); else if (m == 7) r = min(a.rgb, b.rgb); else r = b.rgb;
              return float4(lerp(a.rgb, r, gCP0.x), max(a.a, b.a * gCP0.x)); }
    case 6: { float4 s = 0; float wsum = 0;                                                        // blur along one direction
              [unroll] for (int k = -12; k <= 12; k++) { float w = exp(-k * k / 50.0); s += compA(uv + gCMisc.zw * gCSize.zw * (k / 12.0)) * w; wsum += w; }
              return s / wsum; }
    case 7: { float l = max(a.r, max(a.g, a.b)); return float4(a.rgb * max(0, l - gCP0.x) / max(l, 1e-3), 1); }   // the bright parts (for glare)
    case 8: { float4 b = gCompB.SampleLevel(gClamp, uv, 0); return float4(a.rgb + b.rgb * gCP0.y * gCC0.rgb, a.a); }  // glare added
    case 9: { float3 c = max(a.rgb * gCP0.z * gCC2.rgb + gCP0.x * gCC0.rgb, 0); return float4(pow(c, 1 / max(gCP0.y * gCC1.rgb, 0.05)), a.a); }   // lift / gamma / gain
    case 10: { float3 h = rgb2hsv(max(a.rgb, 0)); h.x = frac(h.x + gCP0.x); h.y = saturate(h.y * gCP0.y); h.z *= gCP0.z; return float4(hsv2rgb(h), a.a); }
    case 11: { float k = gCP0.y >= 0 ? 1 / max(1 - gCP0.y, 0.02) : 1 + gCP0.y; return float4(max((a.rgb - 0.5) * k + 0.5 + gCP0.x, 0), a.a); }
    case 12: return float4(lerp(a.rgb, 1 - saturate(a.rgb), gCP0.x), a.a);
    case 13: { float2 d = (uv - 0.5) * float2(gCSize.x * gCSize.w, 1); float r = length(d) * 2;   // vignette
               float v = smoothstep(gCP0.y, gCP0.y + gCP0.z, r) * gCP0.x; return float4(lerp(a.rgb, gCC0.rgb, v), a.a); }
    case 14: { float2 d = (uv - 0.5) * gCP0.x * gCMisc.y * gCSize.zw * 2;                           // chromatic fringes
               return float4(compA(uv + d).r, compA(uv).g, compA(uv - d).b, compA(uv).a); }
    case 15: { float2 c = uv - 0.5; float r2 = dot(c, c) * 4;                                      // lens distortion
               float3 k = gCP0.x + float3(-1, 0, 1) * gCP0.y;
               return float4(compA(0.5 + c * (1 + k.r * r2)).r, compA(0.5 + c * (1 + k.g * r2)).g, compA(0.5 + c * (1 + k.b * r2)).b, compA(0.5 + c * (1 + k.g * r2)).a); }
    case 16: { float2 px = max(gCP0.x * gCMisc.y, 1) * gCSize.zw; return compA((floor(uv / px) + 0.5) * px); }   // pixelate
    case 17: { float2 cell = floor(uv * gCSize.xy / max(gCP0.y * gCMisc.y, 1)); float t = floor(gCMisc.x * 24) * gCP0.z;
               return float4(a.rgb + (compHash(cell + t * 17.13) - 0.5) * gCP0.x, a.a); }              // grain
    case 18: { float s = sin((uv.y * gCP0.y + gCMisc.x * gCP0.z * 0.05) * 6.2831853); return float4(a.rgb * (1 - gCP0.x * (0.5 + 0.5 * s)), a.a); }   // scan lines
    case 19: { float2 o = gCSize.zw; float4 n = compA(uv + float2(o.x, 0)) + compA(uv - float2(o.x, 0)) + compA(uv + float2(0, o.y)) + compA(uv - float2(0, o.y));
               return float4(max(a.rgb + (a.rgb * 4 - n.rgb) * gCP0.x, 0), a.a); }                  // sharpen
    case 20: return float4(lerp(a.rgb, lerp(gCC0.rgb, gCC1.rgb, saturate(compLum(a.rgb))), gCP0.x), a.a);   // gradient map
    }
    return a;
}

// ---- a 3D HUD part's glow behind it: its outline in one colour (small), blurred, then laid around it (not over it)
float4 PS_FlatPrev(PrevOut i) : SV_Target { return gFlat; }
cbuffer GlowCB : register(b9) { float4 gGlowDir; };   // xy: one blur step (uv), z: how bright when laid down
float4 PS_GlowBlur(BlitOut i) : SV_Target
{
    static const float w[5] = { 0.2270, 0.1945, 0.1216, 0.0540, 0.0162 };
    float4 c = gCompA.SampleLevel(gClamp, i.uv, 0) * w[0];
    [unroll] for (int k = 1; k < 5; k++) { float2 o = gGlowDir.xy * k; c += (gCompA.SampleLevel(gClamp, i.uv + o, 0) + gCompA.SampleLevel(gClamp, i.uv - o, 0)) * w[k]; }
    return c;
}
float4 PS_GlowComp(BlitOut i) : SV_Target
{
    float4 b = gCompA.SampleLevel(gClamp, i.uv, 0);
    float inside = gCompB.SampleLevel(gClamp, i.uv, 0).a;      // where the part itself is: no glow on top of it
    return float4(b.rgb * (1 - saturate(inside)) * gGlowDir.z, 0);
}
