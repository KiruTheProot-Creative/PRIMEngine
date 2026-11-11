// world.hlsl : GX/TEV-flavoured material shader (Shader Model 5, D3D11).
// 1:1 port of the GLSL in web/index.html. Matrices are DirectXMath row-major, row-vector (mul(v, M)).
//
//   colour = albedo * (baked vertex light + per-vertex dynamic lights)      // "TEV stage 0": texture * rasterised colour
//          + sphere-map environment * mask                                   // "TEV stage 1": indirect/env add
//          + albedo * alpha(glow mask)                                       // emissive
//   then linear depth haze. Everything is done in GAMMA space on purpose (like Flipper/Hollywood).

cbuffer Frame : register(b0)
{
    row_major float4x4 gViewProj;
    row_major float4x4 gView;
    float4 gCam;          // xyz camera pos, w time
    float4 gLightPos[4];  // xyz position, w radius
    float4 gLightCol[4];  // rgb colour * intensity
    float4 gFogColor;     // rgb, w = density 0..1
    float4 gFogRange;     // x start, y end
};

cbuffer Object : register(b1)
{
    row_major float4x4 gWorld;
    float4 gTint;
    float4 gParams;       // x env amount, y glow amount, z dynamic-light flag, w alpha cut-out (0 = off)
};

Texture2D    gAlbedo : register(t0);
Texture2D    gEnv    : register(t1);
SamplerState gWrap   : register(s0);   // linear + mips, wrap
SamplerState gClamp  : register(s1);   // linear, clamp

struct VSIn
{
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float2 uv  : TEXCOORD0;
    float4 col : COLOR0;   // baked light
};

struct VSOut
{
    float4 pos   : SV_Position;
    float2 uv    : TEXCOORD0;
    float3 light : TEXCOORD1;
    float2 env   : TEXCOORD2;
    float3 n     : TEXCOORD3;
    float3 wp    : TEXCOORD4;
    float  depth : TEXCOORD5;
};

VSOut VS_World(VSIn i)
{
    VSOut o;
    float4 wp = mul(float4(i.pos, 1.0), gWorld);
    float3 n  = normalize(mul(i.nrm, (float3x3)gWorld));
    o.pos = mul(wp, gViewProj);

    float3 lit = i.col.rgb;                                   // baked light (vertex colour)
    [unroll] for (int k = 0; k < 4; k++)                      // GX-style per-vertex lights
    {
        float3 d    = gLightPos[k].xyz - wp.xyz;
        float  dist = length(d);
        float  att  = saturate(1.0 - dist / gLightPos[k].w);
        att *= att;
        float ndl = max(dot(n, d / max(dist, 1e-4)), 0.0);
        lit += gLightCol[k].rgb * (ndl * att * gParams.z);
    }

    float3 nv = normalize(mul(n, (float3x3)gView));           // sphere-map texgen
    o.light = lit;
    o.env   = float2(nv.x * 0.5 + 0.5, 0.5 - nv.y * 0.5);
    o.uv    = i.uv;
    o.n     = n;
    o.wp    = wp.xyz;
    o.depth = o.pos.w;
    return o;
}

float4 PS_World(VSOut i) : SV_Target
{
    float4 t   = gAlbedo.Sample(gWrap, i.uv);
    if (gParams.w > 0.0 && t.a < gParams.w) discard;                         // cut-out pixels (model textures)
    float3 col = t.rgb * gTint.rgb * i.light;

    float3 N   = normalize(i.n);
    float3 V   = normalize(gCam.xyz - i.wp);
    float  ndv = saturate(dot(N, V));
    float  mask = saturate(dot(t.rgb, float3(0.3, 0.59, 0.11)) * 1.8 + 0.15);

    col += gEnv.Sample(gClamp, i.env).rgb * gParams.x * mask;                 // sphere-map metal
    col += pow(1.0 - ndv, 3.0) * gParams.x * float3(0.10, 0.42, 0.55);        // visor-ish rim
    float pulse = 0.9 + 0.1 * sin(gCam.w * 2.2 + i.wp.x * 0.6 + i.wp.z * 0.45);
    col += t.rgb * (t.a * gParams.y * 1.05 * pulse);                          // glow mask in alpha

    float f = saturate((i.depth - gFogRange.x) / (gFogRange.y - gFogRange.x));
    f = f * f * (3.0 - 2.0 * f) * gFogColor.w;
    return float4(lerp(col, gFogColor.rgb, f), 1.0);
}

// ---------------------------------------------------------------- particles (additive sprites)
struct PVSOut
{
    float4 pos   : SV_Position;
    float2 uv    : TEXCOORD0;
    float4 col   : TEXCOORD1;
    float  depth : TEXCOORD2;
};

PVSOut VS_Particle(VSIn i)
{
    PVSOut o;
    o.pos   = mul(float4(i.pos, 1.0), gViewProj);
    o.uv    = i.uv;
    o.col   = i.col;
    o.depth = o.pos.w;
    return o;
}

float4 PS_Particle(PVSOut i) : SV_Target
{
    float s = gAlbedo.Sample(gClamp, i.uv).r;
    float f = saturate((i.depth - gFogRange.x) / (gFogRange.y - gFogRange.x));
    f = f * f * (3.0 - 2.0 * f) * gFogColor.w;
    return float4(i.col.rgb * (s * i.col.a * (1.0 - f)), 1.0);   // blend state: ONE, ONE
}
