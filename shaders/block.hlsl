// block.hlsl : world blocks (camera-relative positions) and the sky.
// Milestone 1 look: pixel textures, Minecraft-style face shading + corner shading, distance fog into the sky.

cbuffer Frame : register(b0)
{
    row_major float4x4 gViewProj;   // camera at the origin (positions are relative to the camera)
    float4 gFogColor;               // rgb, w = unused
    float4 gFogRange;               // x start, y end (blocks)
    float4 gSkyTop;                 // rgb
    float4 gSkyHorizon;             // rgb
    float4 gCamF, gCamR, gCamU;     // camera basis (sky)
    float4 gProj;                   // x tan(fov/2), y aspect
};
cbuffer ChunkCB : register(b1)
{
    float4 gOffset;                 // chunk origin minus camera position
};

Texture2DArray gTex : register(t0);
SamplerState gSamp : register(s0);

struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; float layer : TEXCOORD1; float shade : TEXCOORD2; float normal : TEXCOORD3; };
struct VSOut { float4 pos : SV_Position; float3 uvl : TEXCOORD0; float shade : TEXCOORD1; float dist : TEXCOORD2; };

VSOut VS_Block(VSIn i)
{
    VSOut o;
    float3 p = i.pos + gOffset.xyz;
    o.pos = mul(float4(p, 1), gViewProj);
    o.uvl = float3(i.uv, i.layer);
    o.shade = i.shade;
    o.dist = length(p);
    return o;
}

float3 shadeBlock(VSOut i, float3 albedo)
{
    float3 c = albedo * i.shade;
    float f = saturate((i.dist - gFogRange.x) / max(gFogRange.y - gFogRange.x, 1));
    return lerp(c, gFogColor.rgb, f * f);
}

float4 PS_Opaque(VSOut i) : SV_Target
{
    float4 t = gTex.Sample(gSamp, i.uvl);
    return float4(shadeBlock(i, t.rgb), 1);
}

float4 PS_Cutout(VSOut i) : SV_Target
{
    float4 t = gTex.Sample(gSamp, i.uvl);
    clip(t.a - 0.5);
    return float4(shadeBlock(i, t.rgb), 1);
}

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
    float h = dir.y;
    float3 c = lerp(gSkyHorizon.rgb, gSkyTop.rgb, saturate(h * 1.5));
    if (h < 0) c = lerp(gSkyHorizon.rgb, gSkyHorizon.rgb * 0.75, saturate(-h * 4));
    return float4(c, 1);
}
