// post.hlsl : bloom chain + final composite. Port of the GLSL post passes in web/index.html.
// No vertex buffer needed: draw(3) with SV_VertexID makes a fullscreen triangle.

cbuffer Post : register(b2)
{
    float4 gP0;   // xy = source texel size, z = bright threshold (<0 = plain 4-tap downsample), w = boost
    float4 gP1;   // x = bloom strength, y = dither on/off, z = saturation, w = vignette
    float4 gP2;   // xy = internal resolution, zw = blur direction
};

Texture2D    gSrc    : register(t0);   // bright/blur source, or scene in composite
Texture2D    gBloomA : register(t1);
Texture2D    gBloomB : register(t2);
SamplerState gLinear : register(s1);   // linear, clamp
SamplerState gSceneS : register(s2);   // linear OR nearest, chosen by the C++ side for the final upscale

struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

PSIn VS_Full(uint id : SV_VertexID)
{
    PSIn o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.uv  = float2(p.x, 1.0 - p.y);                 // D3D: v = 0 at the top
    o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

float4 PS_Bright(PSIn i) : SV_Target
{
    float2 t = gP0.xy;
    float3 c = (gSrc.Sample(gLinear, i.uv + float2(-t.x, -t.y)).rgb + gSrc.Sample(gLinear, i.uv + float2(t.x, -t.y)).rgb +
                gSrc.Sample(gLinear, i.uv + float2(-t.x,  t.y)).rgb + gSrc.Sample(gLinear, i.uv + float2(t.x,  t.y)).rgb) * 0.25;
    float k = 1.0;
    if (gP0.z >= 0.0) { float l = max(c.r, max(c.g, c.b)); k = smoothstep(gP0.z, 1.0, l); }
    return float4(c * k * gP0.w, 1.0);
}

float4 PS_Blur(PSIn i) : SV_Target
{
    float2 d = gP0.xy * gP2.zw;
    float3 c = gSrc.Sample(gLinear, i.uv).rgb * 0.2270270270
             + (gSrc.Sample(gLinear, i.uv + d * 1.3846153846).rgb + gSrc.Sample(gLinear, i.uv - d * 1.3846153846).rgb) * 0.3162162162
             + (gSrc.Sample(gLinear, i.uv + d * 3.2307692308).rgb + gSrc.Sample(gLinear, i.uv - d * 3.2307692308).rgb) * 0.0702702703;
    return float4(c, 1.0);
}

static const float kBayer[16] = { 0, 8, 2, 10,  12, 4, 14, 6,  3, 11, 1, 9,  15, 7, 13, 5 };

float4 PS_Composite(PSIn i) : SV_Target
{
    float3 scene = gSrc.Sample(gSceneS, i.uv).rgb;
    float3 col = scene + (gBloomA.Sample(gLinear, i.uv).rgb * 0.7 + gBloomB.Sample(gLinear, i.uv).rgb * 1.0) * gP1.x;

    col = saturate(col);                                              // EFB is 8-bit: clamp before grading
    float l = dot(col, float3(0.299, 0.587, 0.114));
    col = lerp(float3(l, l, l), col, gP1.z);                          // saturation
    col = lerp(col, col * col * (3.0 - 2.0 * col), 0.3);              // gentle S-curve
    float2 q = i.uv * 2.0 - 1.0;
    col *= 1.0 - gP1.w * dot(q, q) * 0.3;                             // vignette

    if (gP1.y > 0.5)                                                  // GX-style 6-bit ordered dither (pattern at internal res)
    {
        int2  ip = int2(i.uv * gP2.xy) & 3;
        float b  = (kBayer[(ip.y << 2) + ip.x] + 0.5) / 16.0;
        col = floor(saturate(col) * 63.0 + b) / 63.0;
    }
    return float4(col, 1.0);
}

// ---------------------------------------------------------------- sky (rooms): gradient, clouds or stars, with a sun
cbuffer Sky : register(b3)
{
    float4 gSkyF, gSkyR, gSkyU;   // camera forward / right / up
    float4 gSkyP;                 // x tan(fov/2), y aspect, z type (1 gradient, 2 clouds, 3 stars), w time
    float4 gSkyTop, gSkyHorizon;
    float4 gSunDir;               // xyz direction to the sun, w = 1 when there is a sun
    float4 gSunCol;
};

float skyHash(float2 p) { return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
float skyNoise(float2 p)
{
    float2 i = floor(p), f = frac(p); f = f * f * (3 - 2 * f);
    return lerp(lerp(skyHash(i), skyHash(i + float2(1, 0)), f.x), lerp(skyHash(i + float2(0, 1)), skyHash(i + float2(1, 1)), f.x), f.y);
}
float skyFbm(float2 p) { float s = 0, a = 0.5; for (int k = 0; k < 5; k++) { s += skyNoise(p) * a; p *= 2.07; a *= 0.5; } return s; }

float4 PS_Sky(PSIn i) : SV_Target
{
    float2 ndc = float2(i.uv.x * 2 - 1, 1 - i.uv.y * 2);
    float3 dir = normalize(gSkyF.xyz + gSkyR.xyz * (ndc.x * gSkyP.x * gSkyP.y) + gSkyU.xyz * (ndc.y * gSkyP.x));
    float h = dir.y;
    float3 col = lerp(gSkyHorizon.rgb, gSkyTop.rgb, saturate(h * 1.6));
    if (h < 0) col = lerp(gSkyHorizon.rgb, gSkyHorizon.rgb * 0.55, saturate(-h * 3));
    if (gSkyP.z > 1.5 && gSkyP.z < 2.5 && h > -0.02)                    // clouds on a plane above
    {
        float2 p = dir.xz / max(h, 0.06) * 0.55 + gSkyP.w * float2(0.012, 0.006);
        float n = skyFbm(p * 1.3);
        float cloud = smoothstep(0.42, 0.72, n) * saturate(h * 5);
        float3 cc = lerp(gSkyHorizon.rgb * 1.18 + 0.04, gSkyTop.rgb * 0.8, smoothstep(0.55, 0.8, n) * 0.5);
        col = lerp(col, cc, cloud * 0.8);
    }
    if (gSkyP.z > 2.5)                                                   // stars and a faint nebula
    {
        float3 g = floor(dir * 260);
        float s = skyHash(g.xy + g.z * 17.31);
        if (s > 0.9965) col += (s - 0.9965) / 0.0035 * float3(0.9, 0.95, 1.0);
        float neb = skyFbm(dir.xy * 2.5 + dir.z * 1.7);
        col += float3(0.10, 0.05, 0.16) * smoothstep(0.5, 0.85, neb);
    }
    if (gSunDir.w > 0.5)                                                 // sun disc and its glow
    {
        float d = max(dot(dir, gSunDir.xyz), 0);
        col += gSunCol.rgb * (pow(d, 900) * 3 + pow(d, 14) * 0.22);
    }
    return float4(col, 1);
}
