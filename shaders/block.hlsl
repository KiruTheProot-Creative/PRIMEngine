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
    float4 gDepth;                  // x near, y far plane
};
cbuffer ChunkCB : register(b1)
{
    float4 gOffset;                 // chunk origin minus camera position
};

Texture2DArray gTex : register(t0);
Texture2D<float> gShadow : register(t1);
Texture2D<float> gSceneDepth : register(t2);
Texture3D<float2> gNoise : register(t3);       // tiling cloud noise: x soft billows, y puffy cells
SamplerState gSamp : register(s0);
SamplerComparisonState gShadowSamp : register(s1);
SamplerState gLinear : register(s2);

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
        d = saturate(d - (1 - e) * 0.35 * (1 - d));                                // eroded, wispy edges
    }
    return d * 0.045;                                                              // extinction per block
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

struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; float layer : TEXCOORD1; float shade : TEXCOORD2; uint normal : TEXCOORD3; };
struct VSOut { float4 pos : SV_Position; float3 uvl : TEXCOORD0; float ao : TEXCOORD1; float3 rel : TEXCOORD2; float3 n : TEXCOORD3; };

float3 unpackNormal(uint n) { return normalize(float3(n & 255, (n >> 8) & 255, (n >> 16) & 255) / 255.0 * 2 - 1); }

VSOut VS_Block(VSIn i)
{
    VSOut o;
    float3 p = i.pos + gOffset.xyz;
    o.pos = mul(float4(p, 1), gViewProj);
    o.uvl = float3(i.uv, i.layer);
    o.ao = i.shade;
    o.rel = p;
    o.n = unpackNormal(i.normal);
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

float3 lightBlock(VSOut i, float3 albedo, bool front)
{
    float3 n = front ? i.n : -i.n;
    float ndl = saturate(dot(n, gSunDir.xyz));
    float sh = ndl > 0 ? shadowAt(i.rel, n, ndl, i.pos.xy) * cloudShadow(i.rel) : 0;
    float sky = 0.55 + 0.45 * n.y;                                     // more sky light on top than on the sides
    float3 light = gSunColor.rgb * ndl * sh + gAmbient.rgb * sky * i.ao;
    float3 c = albedo * light;
    float d = length(i.rel);
    float f = saturate((d - gFogRange.x) / max(gFogRange.y - gFogRange.x, 1));
    return lerp(c, skyColor(normalize(i.rel)), f * f);
}

float4 PS_Opaque(VSOut i, bool front : SV_IsFrontFace) : SV_Target
{
    float4 t = gTex.Sample(gSamp, i.uvl);
    return float4(lightBlock(i, t.rgb, front), 1);
}

float4 PS_Cutout(VSOut i, bool front : SV_IsFrontFace) : SV_Target
{
    float4 t = gTex.Sample(gSamp, i.uvl);
    clip(t.a - 0.5);
    return float4(lightBlock(i, t.rgb, front), 1);
}

// the see-through preview of the block you are about to place
float4 PS_Ghost(VSOut i) : SV_Target
{
    float4 t = gTex.Sample(gSamp, i.uvl);
    clip(t.a - 0.5);
    return float4(t.rgb * (gAmbient.rgb + gSunColor.rgb * 0.3) * 1.2, 0.5);
}

// ---------------------------------------------------------------- shadow map
struct ShOut { float4 pos : SV_Position; float3 uvl : TEXCOORD0; };
ShOut VS_Shadow(VSIn i)
{
    ShOut o; o.pos = mul(float4(i.pos + gOffset.xyz, 1), gShadowVP); o.uvl = float3(i.uv, i.layer); return o;
}
void PS_ShadowCutout(ShOut i) { clip(gTex.Sample(gSamp, i.uvl).a - 0.5); }

// ---------------------------------------------------------------- sky: a full-screen triangle behind everything
struct SkyOut { float4 pos : SV_Position; float2 ndc : TEXCOORD0; };
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
    return float4(c, 1);
}

// ---------------------------------------------------------------- volumetrics: clouds and ground fog over the picture
Texture2D gHdr : register(t0);
float ign(float2 px) { return frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715)))); }   // even per-pixel noise: no banding

float4 PS_Volume(SkyOut i) : SV_Target
{
    int2 px = int2(i.pos.xy);
    float3 scene = gHdr.Load(int3(px, 0)).rgb;
    float3 dir = normalize(gCamF.xyz + gCamR.xyz * (i.ndc.x * gProj.x * gProj.y) + gCamU.xyz * (i.ndc.y * gProj.x));
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
            float3 amb = lerp(gSkyHorizon.rgb, gSkyTop.rgb, 0.6) * 0.9;
            [loop] for (int k = 0; k < N && T > 0.02; k++)
            {
                float3 p = cam + dir * (t0 + (k + jitter) * dt);
                float d = cloudDensity(p, true);
                if (d <= 0) continue;
                float od = 0;   // how much cloud is between this point and the sun
                [unroll] for (int l = 1; l <= 4; l++) od += cloudDensity(p + gSunDir.xyz * (l * l * 6.0), false) * (2 * l - 1) * 6.0;
                float hf = saturate((p.y - kCloudBottom) / (kCloudTop - kCloudBottom));
                float3 light = gSunColor.rgb * (exp(-od) + 0.4 * exp(-od * 0.25)) * phase * 0.5 + amb * (0.45 + 0.55 * hf);
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
    if (gVolume.y > 0)
    {
        float tMax = min(dist, 192);
        const int N = 20;
        float dt = tMax / N, T = 1;
        float3 S = 0;
        float phase = lerp(phaseHG(mu, 0.55), phaseHG(mu, 0), 0.4) * 12.566;
        float3 amb = gAmbient.rgb * 0.9;
        [loop] for (int k = 0; k < N; k++)
        {
            float t = (k + jitter) * dt;
            float3 rel = dir * t;
            float y = cam.y + rel.y;
            float d = gVolume.y * exp(-max(y - 1, 0) / 7) + gVolume.y * 0.06;          // a low layer plus a thin haze everywhere
            float sun = 1;
            if (gSunDir.w > 0.5)
            {
                float4 s = mul(float4(rel, 1), gShadowVP);
                float2 uv = float2(s.x, -s.y) * 0.5 + 0.5;
                if (all(uv > 0) && all(uv < 1) && s.z < 1) sun = gShadow.SampleCmpLevelZero(gShadowSamp, uv, s.z - 0.001);
            }
            float a = exp(-d * dt);
            S += T * (gSunColor.rgb * sun * phase * 0.35 + amb) * (1 - a); T *= a;
        }
        scene = scene * T + S;
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
float4 PS_Post(SkyOut i) : SV_Target
{
    int2 px = int2(i.pos.xy);
    float3 c = gHdr.Load(int3(px, 0)).rgb * gFogRange.z;
    c = tonemap(c);
    float l = dot(c, float3(0.2126, 0.7152, 0.0722));
    c = max(lerp(l.xxx, c, 1.02), 0);
    float2 v = i.ndc; c *= 1 - 0.18 * dot(v, v) * 0.5;                 // gentle vignette
    return float4(pow(c, 1 / 2.2), 1);
}
