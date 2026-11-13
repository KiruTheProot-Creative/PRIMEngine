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
};
cbuffer ChunkCB : register(b1)
{
    float4 gOffset;                 // where the mesh's (0,0,0) is, relative to the camera (a chunk's origin)
    float4 gAxisX, gAxisY, gAxisZ;  // the mesh's axes: objects are turned and sized; 1,0,0 / 0,1,0 / 0,0,1 for the world's chunks
};
// lights placed in the world (and glowing emitters, shots): up to 32 near the camera
cbuffer Lights : register(b3)
{
    float4 gLightCount;             // x: how many
    float4 gLightPos[32];           // xyz relative to the camera, w reach (blocks)
    float4 gLightCol[32];           // rgb colour x brightness, w 1 = spotlight
    float4 gLightDir[32];           // spotlights: xyz where it points, w cos(cone)
};
float3 dynLights(float3 rel, float3 n)
{
    float3 sum = 0;
    int count = (int)gLightCount.x;
    [loop] for (int k = 0; k < count; k++)
    {
        float3 L = gLightPos[k].xyz - rel; float d = length(L); float range = gLightPos[k].w;
        if (d >= range) continue;
        L /= max(d, 1e-4);
        float att = 1 - d / range; att *= att;
        float ndl = saturate(dot(n, L) * 0.85 + 0.15);   // a little light wraps round edges, softer look
        float cone = 1;
        if (gLightCol[k].w > 0.5) { float c = dot(-L, gLightDir[k].xyz), c0 = gLightDir[k].w; cone = smoothstep(c0, c0 + (1 - c0) * 0.35, c); }
        sum += gLightCol[k].rgb * att * ndl * cone;
    }
    return sum;
}
float3 meshToRel(float3 p) { return gOffset.xyz + gAxisX.xyz * p.x + gAxisY.xyz * p.y + gAxisZ.xyz * p.z; }

Texture2DArray gTex : register(t0);
Texture2D<float> gShadow : register(t1);
Texture2D<float> gSceneDepth : register(t2);
Texture3D<float2> gNoise : register(t3);       // tiling cloud noise: x soft billows, y puffy cells
SamplerState gSamp : register(s0);
Texture2D gMeshTex : register(t9);              // an imported model's own picture (its vertices use layer 16383)
float4 albedoAt(float3 uvl) { return uvl.z > 16000 ? gMeshTex.Sample(gSamp, uvl.xy) : gTex.Sample(gSamp, uvl); }
SamplerComparisonState gShadowSamp : register(s1);
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

float shadowAt(float3 rel, float3 n, float ndl, float2 screen)
{
    if (gSunDir.w < 0.5) return 1;
    float3 p = rel + n * gShadowInfo.z * (1.5 - ndl);                 // push off the surface: no "shadow acne"
    float4 s = mul(float4(p, 1), gShadowVP);
    float2 uv = float2(s.x, -s.y) * 0.5 + 0.5;
    if (any(uv < 0) || any(uv > 1) || s.z > 1) return 1;
    float a = frac(sin(dot(screen, float2(12.9898, 78.233))) * 43758.5453) * 6.2831;   // turn the disc per pixel: smooth edges
    float2x2 r = float2x2(cos(a), -sin(a), sin(a), cos(a));
    float sum = 0;
    [unroll] for (int k = 0; k < 16; k++)
        sum += gShadow.SampleCmpLevelZero(gShadowSamp, uv + mul(kDisc[k], r) * gShadowInfo.x * gShadowInfo.w, s.z - 0.0004);
    float sh = sum / 16;
    float edge = saturate(length(rel.xz) / gShadowInfo.y * 1.25 - 0.25);   // fade out where the shadow map ends
    return lerp(sh, 1, edge * edge);
}

float3 lightBlock(VSOut i, float3 albedo, bool front, out float3 n, out float shine, out float3 indirect)
{
    n = front ? i.n : -i.n;
    // weather on surfaces: snow settles on tops open to the sky, rain makes things darker and shiny
    float open = (gWeather.w > 0.01 || gWeather2.x > 0.01) ? skyExposed(i.rel + n * 0.3) : 0;
    float snowy = gWeather2.x * open * smoothstep(0.35, 0.85, n.y);
    albedo = lerp(albedo, float3(0.92, 0.95, 1.0), snowy * 0.95);
    float wet = gWeather.w * open * (1 - snowy);
    albedo *= lerp(1, 0.55, wet);
    float ndl = saturate(dot(n, gSunDir.xyz));
    float sh = ndl > 0 ? shadowAt(i.rel, n, ndl, i.pos.xy) * cloudShadow(i.rel) : 0;
    float sky = 0.55 + 0.45 * n.y;                                     // more sky light on top than on the sides
    float3 light = gSunColor.rgb * ndl * sh + gAmbient.rgb * sky * i.ao;
    float lamp = max(i.lamp.r, max(i.lamp.g, i.lamp.b));
    float3 hue = lamp > 0 ? i.lamp.rgb / lamp : 0;                                                   // the colour of the lamps nearby
    light += hue * (pow(lamp, 3.2) * 0.9) * gExtra.w * lerp(0.5, 1, i.ao);
    float3 dyn = dynLights(i.rel, n) * lerp(0.6, 1, i.ao);                                               // placed lights, glowing emitters, shots
    float3 c = albedo * (light + dyn);
    indirect = albedo * (light - gSunColor.rgb * ndl * sh);
    if (i.lamp.a > 0.5)                                                                               // the lamp itself glows in its colour
    {
        c = (dot(albedo, float3(0.2126, 0.7152, 0.0722)) * hue * 0.85 + albedo * hue * 0.3) * 3.2 * gExtra.w;
        indirect = 0;                                                                                 // glowing things are never shaded
    }
    float3 v = -normalize(i.rel);
    shine = max(i.mat.x, wet * lerp(0.25, 0.55, saturate(n.y)));    // wet ground shines like a puddle
    if (i.mat.y > 0.5) { shine = 1; c *= 0.15; indirect *= 0.15; }    // a mirror is (almost) only its reflection
    if (gRefl.x < 0.5 && shine > 0.01)   // reflections off: just a little of the sky mirrored
    {
        float fr = 0.04 + 0.96 * pow(1 - saturate(dot(n, v)), 5);
        c += skyColor(reflect(-v, n)) * fr * shine * 0.9;
    }
    float d = length(i.rel);
    float f = saturate((d - gFogRange.x) / max(gFogRange.y - gFogRange.x, 1));
    indirect *= 1 - f * f;
    return lerp(c, skyColor(normalize(i.rel)), f * f);
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
    float t = gVolume.z;
    float2 wind = float2(1, 0.45) * gWeather2.z;
    float3 vel;
    if (kind < 0.5) vel = float3(wind.x * 4.0, -(15 + h.x * 5), wind.y * 4.0);
    else if (kind < 1.5) vel = float3(wind.x * 2.5, -(1.6 + h.x * 0.8), wind.y * 2.5);
    else if (kind < 2.5) vel = float3(wind.x * 3.0 + 0.6, -(0.15 + h.x * 0.2), wind.y * 3.0 + 0.2);
    else if (kind < 3.5) vel = float3(wind.x * 0.8, 0.5 + h.x * 0.7, wind.y * 0.8);
    else vel = float3(wind.x * 0.6, -(0.1 + h.x * 0.15), wind.y * 0.6);
    float3 p = h * box * 7 + vel * t;
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
RainOut VS_Rain(uint id : SV_VertexID) { return particle(id, gWeather.y > gWeather.x ? 1 : 0, max(gWeather.x, gWeather.y), 1); }
RainOut VS_Dust(uint id : SV_VertexID) { return particle(id, gEnvPart.x, gEnvPart.y, 9); }
float4 PS_Rain(RainOut i) : SV_Target
{
    float kind = floor(i.kind), rnd = frac(i.kind) / 0.4;
    if (i.fade <= 0.001 || (kind < 1.5 && skyExposed(i.rel) < 0.5)) discard;      // no rain or snow under a roof (dust floats everywhere)
    float3 lc = gAmbient.rgb * 1.4 + gSunColor.rgb * 0.25 + gWeather2.y * float3(1.6, 1.7, 2.2);
    float2 d = i.uv * 2 - 1; float rd = saturate(1 - dot(d, d));
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
    float3 fallback = skyColor(r) * lerp(0.15, 1, open * saturate(r.y * 4 + 0.6)) + gAmbient.rgb * 0.15 * (1 - open);
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
    float3 refl = skyColor(r) * lerp(0.25, 1, open);
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
    if (gVolume.y > 0 || gEnv.w > 0)
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
            float d = gVolume.y * exp(-max(y - 1, 0) / 7) + gVolume.y * 0.06 + gEnv.w;  // a low layer, a thin haze everywhere, dust of the place
            float sun = 1;
            if (gSunDir.w > 0.5)
            {
                float4 s = mul(float4(rel, 1), gShadowVP);
                float2 uv = float2(s.x, -s.y) * 0.5 + 0.5;
                if (all(uv > 0) && all(uv < 1) && s.z < 1) sun = gShadow.SampleCmpLevelZero(gShadowSamp, uv, s.z - 0.001);
            }
            float a = exp(-d * dt);
            S += T * (gSunColor.rgb * sun * phase * 0.18 + amb) * (1 - a); T *= a;
        }
        scene = scene * T + S;
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
float4 PS_Post(SkyOut i) : SV_Target
{
    int2 px = int2(i.pos.xy);
    float3 c = gHdr.Load(int3(px, 0)).rgb;
    float2 uv = i.ndc * float2(0.5, -0.5) + 0.5;
    c = lerp(c, gBloom.SampleLevel(gClamp, uv, 0).rgb / 6, gExtra.y);              // soft glow around bright things (6 levels added up)
    c *= gFogRange.z;
    c = tonemap(c);
    float l = dot(c, float3(0.2126, 0.7152, 0.0722));
    c = max(lerp(l.xxx, c, 1.02), 0);
    float2 v = i.ndc; c *= 1 - 0.18 * dot(v, v) * 0.5;                 // gentle vignette
    return float4(pow(c, 1 / 2.2), 1);
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
float4 PS_Hud(HudOut i) : SV_Target
{
    return gHudTex.Sample(gClamp, i.uv) * gPrevLight.x;
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
