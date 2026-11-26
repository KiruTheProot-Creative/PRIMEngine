// RTX reflections: the graphics card's ray tracing cores find what a shiny surface mirrors (DXR 1.1 ray queries; built with dxc,
// shader model 6.5). One thread per pixel of the reflections' (smaller) picture; PS_Reflect then blends what it found with the
// voxel reflections (which take over farther out) and the screen's own (objects and enemies nearer than what the rays found).
// The rest of the engine's shader code comes along: the same light, fog and materials as everything else.
#include "block.hlsl"

RaytracingAccelerationStructure gScene : register(t0, space1);   // the world's chunks near you, this frame
struct RtInst { uint4 g0; uint4 g1; };                           // per instance, per triangle list (opaque, cut-out): vertex buffer, byte offset, index buffer, byte offset
StructuredBuffer<RtInst> gRtInst : register(t1, space1);
RWTexture2D<float4> gRtOut : register(u0, space1);               // rgb what it found, a how far (1e5: nothing, < 0: not traced here)
ByteAddressBuffer gGeo[256] : register(t0, space2);              // the buffers the meshes are in

struct RtHit { float2 uv; float layer; uint lit; uint nrm; };
// a triangle's surface where a ray met it: its vertices are the meshes' own (float3 position, uv, layer, lit, normal: 32 bytes)
RtHit rtFetch(uint inst, uint geom, uint prim, float2 bary)
{
    RtInst ri = gRtInst[inst]; uint4 gg = geom == 0 ? ri.g0 : ri.g1;
    uint3 ix = gGeo[NonUniformResourceIndex(gg.z)].Load3(gg.w + prim * 12);
    uint4 a0 = gGeo[NonUniformResourceIndex(gg.x)].Load4(gg.y + ix.x * 32), a1 = gGeo[NonUniformResourceIndex(gg.x)].Load4(gg.y + ix.x * 32 + 16);
    uint4 b0 = gGeo[NonUniformResourceIndex(gg.x)].Load4(gg.y + ix.y * 32), b1 = gGeo[NonUniformResourceIndex(gg.x)].Load4(gg.y + ix.y * 32 + 16);
    uint4 c0 = gGeo[NonUniformResourceIndex(gg.x)].Load4(gg.y + ix.z * 32), c1 = gGeo[NonUniformResourceIndex(gg.x)].Load4(gg.y + ix.z * 32 + 16);
    float3 w = float3(1 - bary.x - bary.y, bary.x, bary.y);
    RtHit h;
    h.uv = float2(asfloat(a0.w), asfloat(a1.x)) * w.x + float2(asfloat(b0.w), asfloat(b1.x)) * w.y + float2(asfloat(c0.w), asfloat(c1.x)) * w.z;
    float layer = asfloat(a1.y); h.layer = layer - floor(layer / 16384) * 16384;   // (the high part says water / plants)
    h.lit = a1.z; h.nrm = a1.w;
    return h;
}
// its colour: lit as the voxel reflections light what they find (sun and its shadow, sky, lamps), plus the block's own lamp light and glow
float3 rtShade(float3 hp, float3 hn, RtHit h, float t, float2 pixel)
{
    uint layer = (uint)h.layer;
    float3 albedo = gVoxTex.SampleLevel(gSamp, float3(h.uv, layer), clamp(log2(max(t, 1) * 0.35), 0, 4)).rgb;
    TexMat m = gMats[layer];
    float ao = (h.lit & 255) / 255.0;
    float3 lampC = float3((h.lit >> 8) & 15, (h.lit >> 12) & 15, (h.lit >> 16) & 15) / 15.0;
    float lamp = max(lampC.r, max(lampC.g, lampC.b)); float3 hue = lamp > 0 ? lampC / lamp : 0;
    float sv; float3 light = reflLight(hp, hn, pixel, sv) * lerp(0.6, 1, ao);
    light += hue * (pow(lamp, 3.2) * 0.9) * gExtra.w * lerp(0.5, 1, ao);
    float3 c = albedo * light;
    float3 gTint = glowTint(m.b.w);
    if (gSurf.z > 0.5 && m.a.z > 0) c += albedo * gTint * m.a.z * smoothstep(m.a.w, m.a.w + 0.08, sqrt(dot(albedo, float3(0.2126, 0.7152, 0.0722)))) * 3.0;
    if (m.b.z > 0) c += gEmitTex.SampleLevel(gSamp, float3(h.uv, layer), 0).rgb * gTint * m.b.z * 2.5;
    if ((h.lit >> 20) & 1) c = (dot(albedo, float3(0.2126, 0.7152, 0.0722)) * hue * 0.85 + albedo * hue * 0.3) * 3.2 * gExtra.w;   // a lamp block glows
    return reflFog(c, hp, sv);
}

[numthreads(8, 8, 1)]
void CS_RtReflect(uint3 id : SV_DispatchThreadID)
{
    uint w, h; gRtOut.GetDimensions(w, h); if (id.x >= w || id.y >= h) return;
    float rs = gMinLight.w > 0.01 ? gMinLight.w : 1;
    float2 pos = float2(id.xy) + 0.5;
    int2 px = int2(min(pos / rs, gDepth.zw - 1));
    float2 ndc = pos / float2(w, h) * float2(2, -2) + float2(-1, 1);
    float4 g = gSurface.Load(int3(px, 0)); float shine = g.a;
    float minShine = gWet2.z > 1.5 ? 0.06 : 0.4;                       // low: polished and wet surfaces; high: anything shiny
    float zb = gSceneDepth.Load(int3(px, 0));
    if (gRefl.x < 0.5 || shine < max(minShine, 0.02) || zb >= 1) { gRtOut[id.xy] = float4(0, 0, 0, -1); return; }
    float3 n = normalize(g.xyz * 2 - 1);
    float3 dir = normalize(gCamF.xyz + gCamR.xyz * (ndc.x * gProj.x * gProj.y) + gCamU.xyz * (ndc.y * gProj.x));
    float dist = linDepth(zb) / max(dot(dir, gCamF.xyz), 1e-3);
    if (dist > gWet2.w) { gRtOut[id.xy] = float4(0, 0, 0, -1); return; }   // (farther: the voxel reflections)
    float3 P = dir * dist, r = reflect(dir, n);
    sReach = gRefl.z * lerp(0.3, 1, shine * shine);
    float3 ng = reflFlatNormal(px, P, n, dir, dist);
    float3 rv = r; float rn = dot(rv, ng); if (rn < 0.03) rv = normalize(rv + ng * (0.03 - rn));   // never into the surface
    float tMax = max(reflReach() * 1.5, 48);

    RayDesc ray; ray.Origin = P + ng * 0.03; ray.Direction = rv; ray.TMin = 0.005; ray.TMax = tMax;
    RayQuery<RAY_FLAG_NONE> q;
    q.TraceRayInline(gScene, RAY_FLAG_NONE, 0xFF, ray);
    while (q.Proceed())   // cut-out triangles (glass, leaves): only where their texture is solid
    {
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            RtHit c = rtFetch(q.CandidateInstanceIndex(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics());
            if (gVoxTex.SampleLevel(gSamp, float3(c.uv, (uint)c.layer), 0).a >= 0.5) q.CommitNonOpaqueTriangleHit();
        }
    }
    bool hit = q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    float tHit = hit ? q.CommittedRayT() : 1e5;
    {   // objects: their voxel copies, until the real models are in the scene too
        float ot; float3 on, oa;
        if (objectTrace(ray.Origin, rv, min(tHit, tMax), ot, on, oa))
        {
            float3 hp = ray.Origin + rv * ot; float sv;
            gRtOut[id.xy] = float4(reflFog(oa * reflLight(hp, on, pos, sv), hp, sv), ot); return;
        }
    }
    if (!hit) { gRtOut[id.xy] = float4(0, 0, 0, 1e5); return; }
    RtHit hh = rtFetch(q.CommittedInstanceIndex(), q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics());
    float3 hp = ray.Origin + rv * tHit;
    float3 hn = unpackNormal(hh.nrm); if (dot(hn, rv) > 0) hn = -hn;   // (seen from behind)
    gRtOut[id.xy] = float4(rtShade(hp, hn, hh, tHit, pos), tHit);
}
