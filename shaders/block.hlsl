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
    float4 gOcc;                    // x true darkness on (sky light only where the sky is), y how open the sky is around the camera (0..1), z the light shafts' strength (god rays)
    float4 gLookFog;                // the look's air: x fog thickness, y height (world Y) it thins above, z over how many blocks (0: everywhere), w lights in the air
    float4 gLookFogCol;             // rgb its colour (linear), w how much it glows by itself
    float4 gGI;                     // bounce light: x strength (0: off), y how far it reaches (blocks)
    float4 gSkyVolO;                // the sky light around you (96 x 64 x 96 blocks): xyz its corner relative to the camera, w 1 = ready
    float4 gVoxO;                   // the blocks around you for reflection rays (192 x 96 x 192): xyz its corner relative to the camera, w most steps
    float4 gVoxM;                   // xyz where that corner sits in the (wrapping) 3D texture, w 1 = ready
    float4 gSplash[8];              // splashes on water: xyz where (relative to the camera), w how long ago (s; < 0: none)
    float4 gPlanar;                 // x 1: the mirror view is a shiny floor / water (every shiny pixel on that plane uses it), 0: only mirror blocks; y objects for reflection rays
    float4 gMinLight;               // rgb: the least light in closed spaces (true darkness); w: the reflection rays' resolution (1 full .. 0.25)
    float4 gAA;                     // anti-aliasing: x how far along an edge it looks (pixels), y how strong; z a frame number (0..63: noise that changes each frame)
    row_major float4x4 gHistVP;     // the frame before: camera-relative (then) -> its screen (bounce light's memory)
    float4 gHistCam;                // xyz how far the camera moved since (now - then); w 1 = the memory is usable
    float4 gProbeO;                 // light probes: xyz the grid's corner (camera-relative), w blocks between two probes
    float4 gProbeB;                 // xyz the grid's corner in probe steps (world)
    float4 gProbeM;                 // xyz where that corner sits in the (wrapping) atlas, w this frame's turn
    float4 gProbeP;                 // x 1 on, y rays per side, z frames for a full round, w how far a ray looks (blocks)
    row_major float4x4 gWaterVP;    // camera-relative position -> the water map (the water's surfaces seen from straight above)
    float4 gWater;                  // x caustics (0 off, 1 low, 2 high), y reflected caustics, z the water map is ready, w light shafts under water
    float4 gWater2;                 // x shore foam and soft edges, y drops on the visor (0..1: how wet), z caustics brightness, w rain drops on the visor
};
// pixelated shadows (gAA.w: 16 or 32 a block): the spot a shadow is looked up at snapped to a grid on the surface, like the textures' pixels
float3 pixSnap(float3 rel, float3 n)
{
    if (gAA.w < 0.5) return rel;
    float N = gAA.w; float3 w = gCamPos.xyz + rel;
    float3 s = (floor((w - n * (0.25 / N)) * N) + 0.5) / N;   // the pixel it is on (from just inside the surface)
    s += n * dot(w - s, n);                                    // back on the surface's plane: snapped only along it
    return s - gCamPos.xyz;
}
float3 probeLight(float3 rel, float3 n, bool leakCheck, out float wsum);   // (light probes: further down, next to the blocks' copy they trace through)
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
static float sReach = 0;   // how far this pixel's reflection rays go (0: the setting's full reach); rough surfaces don't need far ones (blurred away)
float reflReach() { return sReach > 0 ? sReach : gRefl.z; }
float lightShadow(LightData L, float3 p, int type, bool whole = false)   // whole: the map's own answer (not faded out far away)
{
    if (L.ext.w < 0) return 1;
    int slot = (int)floor(L.ext.w); float keep = whole ? 1 : saturate((frac(L.ext.w) - 0.05) / 0.9);   // (the fraction: how much of the shadow, fading out far away)
    int face = 0;
    if (type != 1) {   // a cube: which side
        float3 v = p - (type == 3 ? L.dir.xyz : L.pos.xyz), a = abs(v);   // (a glowing box: its shadow view sits at dir)
        face = a.x >= a.y && a.x >= a.z ? (v.x > 0 ? 0 : 1) : a.y >= a.z ? (v.y > 0 ? 2 : 3) : (v.z > 0 ? 4 : 5);
    }
    int sl = slot * 6 + face; ShadowVP m = gLightShadowVP[sl];
    float4 s = mul(float4(p, 1), float4x4(m.r0, m.r1, m.r2, m.r3));
    if (s.w <= 1e-4) return 1;
    float3 q = s.xyz / s.w; float2 uv = float2(q.x, -q.y) * 0.5 + 0.5;
    if (any(uv < 0) || any(uv > 1) || q.z > 1) return 1;
    float mw, mh, mn; gLightShadow.GetDimensions(mw, mh, mn);
    const float t = 1.0 / mw; float z = q.z - 0.00025, r = 0;
    if (gCl0.x < 0.5 || dot(p - L.pos.xyz, p - L.pos.xyz) > 100) return lerp(1, gLightShadow.SampleCmpLevelZero(gShadowSamp, float3(uv, sl), z), keep);   // far from the light (or the helmet's pass): one look (still smoothed 2 x 2 by the hardware)
    r += gLightShadow.SampleCmpLevelZero(gShadowSamp, float3(uv + float2(-0.5, -0.5) * t, sl), z);
    r += gLightShadow.SampleCmpLevelZero(gShadowSamp, float3(uv + float2(0.5, -0.5) * t, sl), z);
    r += gLightShadow.SampleCmpLevelZero(gShadowSamp, float3(uv + float2(-0.5, 0.5) * t, sl), z);
    r += gLightShadow.SampleCmpLevelZero(gShadowSamp, float3(uv + float2(0.5, 0.5) * t, sl), z);
    return lerp(1, r * 0.25, keep);
}
// a colour that can't break the picture: NaN -> 0 (min / max return the other value), infinity and huge values capped
float3 hdrSafe(float3 c, float cap = 4096) { return min(max(c, 0), cap); }
float voxShadow(float3 P, float3 lp, bool face, bool far = false);
bool voxLineBlocked(float3 a, float3 b);   // (below, with the light probes)   // (below: a lamp block's shadow, through the blocks; face: the light is a point just outside a glowing face)
// ---- the cooked world (Cook > Test-cook): a reflection picture per room, the lamp blocks' light baked into the air, handed over from real time at a distance
cbuffer CookCB : register(b13)
{
    float4 gCook;        // x reflection pictures (how many), y baked light on, z / w: real time until z blocks away, cooked from w on
    float4 gCookLitO;    // xyz the baked light's corner (camera-relative), w its handing over: real lamps until x blocks ...
    float4 gCookLitS;    // xyz its size (blocks), w ... baked from w on
    float4 gFogG;   // the fog grid (smooth fog): x, y, z its size; w 1 on
    float4 gFogR;   // x how far it reaches (blocks), y this frame's spot in the cell (depth, 0..1), z how much it keeps of what it knew (0: starts fresh), w lights glow this far
    float4 gFogJ;   // xy this frame's spot in the cell (across)
    float4 gLavaM;  // the lava glow map: xy its corner (x, z, camera-relative), z its size (blocks), w how strong (0: none)
    float4 gLavaH;  // x the height its heights count from (camera-relative)
    float4 gAmbV;   // ambient lights: xyz the corner of the blocks around you that know their room's light (camera-relative)
    float4 gAmbS;   // xyz how many blocks that is, w 1 on
    float4 gProbePos[64];   // each room's picture: where it was taken (camera-relative), w its mip count
    float4 gProbeLo[64];    // the room's box (camera-relative), for looking up what a ray hits
    float4 gProbeHi[64];
};
TextureCubeArray gCookProbes : register(t46);
Texture3D gCookLit : register(t47);   // rgb: the baked lamp light in each block of air
Texture3D gCookDir : register(t48);   // xyz: where it mostly comes from
float cookLitFar(float3 rel)   // 0 near (the lamps' own light), 1 far (baked)
{
    if (gCook.y < 0.5) return 0;
    float3 w = rel - gCookLitO.xyz; if (any(w < 0) || any(w >= gCookLitS.xyz)) return 0;
    return saturate((length(rel) - gCookLitO.w) / max(gCookLitS.w - gCookLitO.w, 1));
}
static float3 sFlatN = 0;   // the block's own face direction (before generated bumps), set while a block is lit (0: not known)
void evalLight(LightData L, float3 rel, float3 n, float3 v, float pw, bool object, inout float3 dif, inout float3 spec)
{
    int kind = (int)round(L.col.w); bool cooked = kind >= 32; kind -= cooked ? 32 : 0; bool voxSh = kind >= 16; kind -= voxSh ? 16 : 0; bool objOnly = kind >= 8; int type = kind & 7;
    if (objOnly && !object) return;
    float keepNear = cooked && !object ? 1 - cookLitFar(rel) : 1;   // a lamp block in the cooked world: far away its baked light takes over
    if (keepNear <= 0) return;
    float3 lp = L.pos.xyz;
    if (type == 2) { float3 a = lp - L.ext.xyz, ab = L.ext.xyz * 2; lp = a + ab * saturate(dot(rel - a, ab) / max(dot(ab, ab), 1e-4)); }   // a tube: its nearest point
    if (type == 3) lp = clamp(rel, lp - L.ext.xyz, lp + L.ext.xyz);   // a glowing box (lamp blocks): the nearest point of its surface - its sides shine, not its middle
    float3 Lv = lp - rel; float d = length(Lv); float range = L.pos.w;
    if (d >= range) return;
    Lv /= max(d, 1e-4);
    float att = 1 - d / range; att *= att;
    if (type == 1) { float c = dot(-Lv, L.dir.xyz), c0 = L.dir.w; att *= smoothstep(c0, c0 + (1 - c0) * 0.35, c); }
    if (att <= 0.002) return;   // (too faint to see: no shadow lookups for it)
    float3 nf = any(sFlatN != 0) ? sFlatN : n;   // shadows ask with the flat face: bumps near an edge would push the test round the corner (a thin line of light)
    if (any(sFlatN != 0) && dot(nf, Lv) < 0.02) return;   // behind the face itself: none (no light wrapping round a wall's edge)
    if (L.ext.w >= 0)   // (a little off the surface: no shadow speckles where the light grazes it)
    {   // its shadow map; toward the end of the shadow distance it hands over to the shadow through the blocks (not to no shadow: no light through walls far away)
        float keep = saturate((frac(L.ext.w) - 0.05) / 0.9);
        float sm = lightShadow(L, pixSnap(rel, nf) + nf * (0.05 + d * 0.02), type, true);
        float fb = (voxSh && keep < 0.999 && gCl0.x > 0.5) ? voxShadow(pixSnap(rel, nf) + nf * 0.04, lp, type == 3, true) : 1;
        att *= lerp(fb, sm, keep);
    }
    if (voxSh && att > 0 && gCl0.x > 0.5)   // (the helmet / arm cannon and other small passes: their shadow maps are enough)
    {
        if (L.ext.w < 0) att *= voxShadow(pixSnap(rel, nf) + nf * 0.04, lp, type == 3, true);   // no shadow map (too far, too many, or not drawn yet): its shadow from the blocks between, all the way
        else if (type == 3 && (((uint)gVoxM.w) & 5u) == 5u)   // a glowing box with a shadow map: only its first blocks (what the map starts too late to see) - no light along the inside of walls, or out of a side against a block
            att *= voxLineBlocked(lp, lp + (rel - lp) * (min(d - 0.1, 3.0) / max(d, 1e-3))) ? 0 : 1;
    }
    float3 c = L.col.rgb * att * keepNear;
    dif += c * saturate(dot(n, Lv) * 0.85 + 0.15);   // a little light wraps round edges, softer look
    float3 hv = Lv + v; float3 h = hv * rsqrt(max(dot(hv, hv), 1e-8));   // (looking straight into the light: no zero-length normalize - a NaN pixel)
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
struct TexMat { float4 a; float4 b; float4 c; };   // c.x: roughness (blurry reflections)
StructuredBuffer<TexMat> gMats : register(t10);
float3 glowTint(float w)   // b.w: a glowing face's colour, 0xRRGGBB as a number (0: its own colours)
{
    if (w < 0.5) return float3(1, 1, 1);
    uint c = (uint)round(w);
    return float3((c >> 16) & 255, (c >> 8) & 255, c & 255) / 255.0;
}
Texture2DArray gEmitTex : register(t14);        // emission maps, one per texture layer (black: nothing glows)
Texture2D gMeshEmit : register(t15);            // the emission map of the model being drawn
cbuffer MeshMatCB : register(b4)   // the material of the model being drawn (same numbers as a texture's), and its fake shine
{
    float4 gMM0; float4 gMM1;
    float4 gFS0;          // fake shine: x 0 off / 1 picture / 2 glint lights, y strength, z 1 fixed to the world / 0 to the camera, w a mask picture
    float4 gFSTint;       // rgb tint (linear), w which picture (0..3 built in, 4 your own)
    float4 gGlintDir[3];  // xyz where each glint light shines from, w sharpness (0..1)
    float4 gGlintCol[3];  // rgb colour x brightness, w on
};
Texture2D gShineTex : register(t27);   // your own shine picture (black: nothing, bright: a highlight), looked up by the surface's direction
Texture2D gShineMask : register(t28);  // where it shines, laid out like the model's own picture
float4 albedoAt(float3 uvl) { return uvl.z > 16000 ? gMeshTex.Sample(gSamp, uvl.xy) : gTex.Sample(gSamp, uvl); }
SamplerState gLinear : register(s2);
// ---- lava lights what is around it: a top-down map of the lava near you (how much of each column is lava, and how high), blurred by
// its smaller copies. A surface gets light from the lava below and beside it, more facing it, less the higher above it it is
Texture2D gLavaMap : register(t51);
// ---- ambient lights (objects): each block of air near you knows which ambient light's room it is in (up to two, where they blend)
struct AmbGpu { float4 c[6]; float4 fog; };   // c: the light from +x (east), -x (west), +y (up), -y (down), +z (south), -z (north); c[0].w how much it takes the sky's place; fog: rgb, a thickness
StructuredBuffer<AmbGpu> gAmbObj : register(t53);
Texture3D<uint4> gAmbVol : register(t54);       // r, b: which ambient lights (+1, 0 none), g, a: how much (0..255)
float3 ambCube(uint k, float3 n)
{
    float3 n2 = n * n;
    return n2.x * (n.x > 0 ? gAmbObj[k].c[0].rgb : gAmbObj[k].c[1].rgb) + n2.y * (n.y > 0 ? gAmbObj[k].c[2].rgb : gAmbObj[k].c[3].rgb) + n2.z * (n.z > 0 ? gAmbObj[k].c[4].rgb : gAmbObj[k].c[5].rgb);
}
float4 ambAt(float3 rel, float3 n)   // rgb: its room's light on a face looking along n (times how much it is in that room); a: how much of the sky's light it takes away
{
    if (gAmbS.w < 0.5) return 0;
    int3 c = int3(floor(rel - gAmbV.xyz)); if (any(c < 0) || any(c >= int3(gAmbS.xyz))) return 0;
    uint4 v = gAmbVol.Load(int4(c, 0));
    float wa = v.r > 0 ? v.g / 255.0 : 0, wb = v.b > 0 ? v.a / 255.0 : 0, tot = wa + wb; if (tot < 0.004) return 0;
    float3 col = 0; float mx = 0;
    if (wa > 0) { col += ambCube(v.r - 1, n) * wa; mx += gAmbObj[v.r - 1].c[0].w * wa; }
    if (wb > 0) { col += ambCube(v.b - 1, n) * wb; mx += gAmbObj[v.b - 1].c[0].w * wb; }
    float k = saturate(tot);
    return float4(col / tot * k, mx / tot * k);
}
float4 ambFogAt(float3 rel)   // its room's fog: rgb colour, a thickness
{
    if (gAmbS.w < 0.5) return 0;
    int3 c = int3(floor(rel - gAmbV.xyz)); if (any(c < 0) || any(c >= int3(gAmbS.xyz))) return 0;
    uint4 v = gAmbVol.Load(int4(c, 0));
    float wa = v.r > 0 ? v.g / 255.0 : 0, wb = v.b > 0 ? v.a / 255.0 : 0, tot = wa + wb; if (tot < 0.004) return 0;
    float4 f = 0;
    if (wa > 0) f += gAmbObj[v.r - 1].fog * wa;
    if (wb > 0) f += gAmbObj[v.b - 1].fog * wb;
    return float4(f.rgb / tot, f.a / tot * saturate(tot));
}
static const float3 kLavaLight = float3(1.0, 0.36, 0.07);
float lavaAt(float3 rel, out float above)   // how much lava is near (its blurred copies), and how high above its surface rel is
{
    above = 99; if (gLavaM.w <= 0) return 0;
    float2 uv = (rel.xz - gLavaM.xy) / gLavaM.z; if (any(uv < 0) || any(uv > 1)) return 0;
    float4 s = gLavaMap.SampleLevel(gLinear, uv, 3); if (s.r < 0.002) return 0;
    above = rel.y - gLavaH.x - s.g / s.r; return s.r;
}
float3 lavaGlow(float3 rel, float3 n)   // (n: the face's own direction)
{
    if (gLavaM.w <= 0) return 0;
    float2 uv = (rel.xz - gLavaM.xy) / gLavaM.z; if (any(uv < 0) || any(uv > 1)) return 0;
    float yr = rel.y - gLavaH.x, g = 0, band = 0, hFar = 0;
    [unroll] for (int k = 0; k < 6; k++)
    {
        float4 s = gLavaMap.SampleLevel(gLinear, uv, k); if (s.r < 0.002) continue;
        float dh = yr - s.g / s.r, R = exp2(k);
        g += s.r * exp(-max(dh, 0) / (1.2 * R + 1)) * saturate(dh + 0.6) / (k + 1);
        if (k <= 1) band += s.r * exp(-abs(dh) * 2.5) * 0.5;   // right at the lava: a hot band on what touches it
        if (k == 3) hFar = dh;
    }
    float face = saturate(0.45 - 0.55 * n.y);   // ceilings and sides facing it, not tops
    float3 c = kLavaLight * gLavaM.w * (g * face * 1.3 + band * 2.2);
    if (max(c.r, 0) > 0.01 && hFar > 1 && (((uint)gVoxM.w) & 1u) != 0)   // no glow through a floor: straight down to the lava, is there a block?
        if (voxLineBlocked(rel + n * 0.08, rel + n * 0.08 - float3(0, hFar - 0.3, 0))) c *= 0.06;
    return c;
}
float3 lavaAir(float3 rel)   // (the fog's glow over lava)
{
    if (gLavaM.w <= 0) return 0;
    float2 uv = (rel.xz - gLavaM.xy) / gLavaM.z; if (any(uv < 0) || any(uv > 1)) return 0;
    float yr = rel.y - gLavaH.x, g = 0;
    [unroll] for (int k = 2; k < 6; k++)
    {
        float4 s = gLavaMap.SampleLevel(gLinear, uv, k); if (s.r < 0.002) continue;
        float dh = yr - s.g / s.r; g += s.r * exp(-max(dh, 0) / (1.5 * exp2(k) + 2)) * saturate(dh + 1);
    }
    return kLavaLight * gLavaM.w * g * 0.5;
}
float3 cookLight(float3 rel, float3 nf, float3 n)
{
    float k = cookLitFar(rel); if (k <= 0) return 0;
    float3 uvw = (rel + nf * 0.5 - gCookLitO.xyz) / gCookLitS.xyz;
    float3 c = gCookLit.SampleLevel(gLinear, uvw, 0).rgb, d = gCookDir.SampleLevel(gLinear, uvw, 0).xyz;
    float l = length(d); float nd = l > 0.05 ? saturate(dot(n, d / l) * 0.85 + 0.15) : 0.5;
    return hdrSafe(c * nd * k);
}
bool cookProbe(float3 P, float3 r, float rough, out float3 col)   // the room's reflection picture, P in it: what the ray r sees (box-projected onto the room's walls)
{
    col = 0; int n = (int)gCook.x; if (n <= 0) return false;
    int best = -1; float bv = 1e30;
    [loop] for (int k = 0; k < n; k++)
    {
        float3 lo = gProbeLo[k].xyz - 1.01, hi = gProbeHi[k].xyz + 1.01;
        if (any(P < lo) || any(P > hi)) continue;
        float3 e = hi - lo; float v = e.x * e.y * e.z; if (v < bv) { bv = v; best = k; }
    }
    if (best < 0) return false;
    float3 lo = gProbeLo[best].xyz, hi = gProbeHi[best].xyz + 1;
    float3 rr = abs(r) > 1e-5 ? r : 1e-5;
    float3 t1 = (hi - P) / rr, t0 = (lo - P) / rr; float3 tf = max(t0, t1);
    float t = max(min(tf.x, min(tf.y, tf.z)), 0);
    float3 dir = P + r * t - gProbePos[best].xyz;
    col = gCookProbes.SampleLevel(gLinear, float4(dir, best), rough * (gProbePos[best].w - 1)).rgb;
    return true;
}
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
// near you: sky light worked out block by block (like Minecraft's: around corners and into cave mouths, never through
// walls), 0..15 in a 3D texture; farther out only the roofs map (is anything straight above?)
Texture3D<float> gSkyVol : register(t26);
SamplerState gSkyVolSamp : register(s4);
float skyAt(float3 p)
{
    if (gSkyVolO.w > 0.5)
    {
        float3 q = (p - gSkyVolO.xyz) / float3(96, 64, 96);
        if (all(q > 0.02) && all(q < 0.98))
        {
            float l = gSkyVol.SampleLevel(gSkyVolSamp, q, 0) * 15;
            return saturate((pow(0.8, 15 - l) - 0.035) / 0.965);     // each block farther from the sky: a bit darker, 0 at level 0
        }
    }
    return skyOpenAt(p);
}
float skyVis(float3 rel, float3 n)
{
    if (gOcc.x < 0.5) return 1;
    return skyAt(rel + n * 0.5);
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
struct VSOut { float4 pos : SV_Position; float3 uvl : TEXCOORD0; float ao : TEXCOORD1; float3 rel : TEXCOORD2; float3 n : TEXCOORD3; float4 lamp : TEXCOORD4; float3 mat : TEXCOORD5; float kind : TEXCOORD6; float clip : SV_ClipDistance0; };
// what the reflection pass needs per pixel: direction of the surface (rgb) and how shiny it is (a)
// color: the lit picture; surface: direction + shininess (reflections); indirect: the part of the light that
// comes from the sky and lamps (not the sun), which ambient occlusion takes away in corners
struct PSOut { float4 color : SV_Target0; float4 surface : SV_Target1; float4 indirect : SV_Target2; float4 albedo : SV_Target3; };   // albedo: for bounce light

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
    o.mat = float3(((i.lit >> 21) & 15) / 15.0, (i.lit >> 25) & 1, ((i.lit >> 30) & 1) ? ((i.lit >> 26) & 15) / 15.0 : -1);   // z: the block's own roughness (-1: its textures')
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
    // no random jitter: each map keeps its blur small enough to stay smooth. The nearest map 6 x 6 samples; farther, where a map pixel is
    // already big on screen, fewer (4 x 4, then 3 x 3): the same look for about half the work
    int N = c == 0 ? 6 : c == 1 ? 4 : 3;
    float step = radius * 2 / (N - 1), sum = 0, h = (N - 1) * 0.5;
    [loop] for (int gy = 0; gy < N; gy++) [loop] for (int gx = 0; gx < N; gx++)
        sum += gShadow.SampleCmpLevelZero(gShadowSamp, float3(uv + (float2(gx, gy) - h) * step * texel, c), bias);
    return sum / (N * N);
}
float shadowAt(float3 rel, float3 n, float ndl, float2 screen)
{
    if (gSunDir.w < 0.5) return 1;
    rel = pixSnap(rel, n);
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

// fake shine: highlights that are painted on rather than worked out from lights. The surface's direction - in the world
// (so they slide as you turn, like Metroid Prime's arm cannon) or in the camera's view (a "matcap") - picks a spot in a
// shine picture (built in or your own), or lights up under up to three glint lights that only make highlights
float shinePreset(int p, float2 q)   // q: -1..1 across the "ball" (x right, y up)
{
    if (p == 0) return exp(-pow((q.x + 0.38 - q.y * 0.25) * 7, 2)) * smoothstep(-0.3, 0.55, q.y) + exp(-dot(q - float2(0.45, 0.5), q - float2(0.45, 0.5)) * 90) * 0.8;   // a long streak and a small dot
    if (p == 1) return exp(-dot(q - float2(-0.35, 0.45), q - float2(-0.35, 0.45)) * 70);                                                                           // one sharp dot
    if (p == 2) return exp(-pow((q.y - 0.55) * 5, 2)) * 0.75 * saturate(1.2 - abs(q.x));                                                                           // a soft band across the top
    return smoothstep(0.72, 1.0, length(q)) * 0.8;                                                                                                                 // the edges (rim)
}
float3 fakeShine(VSOut i, float3 n)
{
    float3 v = -normalize(i.rel), add = 0;
    float3 nc = float3(dot(n, gCamR.xyz), dot(n, gCamU.xyz), -dot(n, gCamF.xyz));   // the direction in the camera's view (z: toward you)
    if (gFS0.x < 1.5)
    {
        float2 q = gFS0.z > 0.5 ? float2(n.x, n.y) : nc.xy;                            // fixed to the world: its own directions (like Prime) / to the camera
        int p = (int)round(gFSTint.w);
        add = p >= 4 ? gShineTex.SampleLevel(gLinear, q * float2(0.5, -0.5) + 0.5, 0).rgb : shinePreset(p, q).xxx;
    }
    else
    {
        [unroll] for (int k = 0; k < 3; k++)
        {
            if (gGlintCol[k].w < 0.5) continue;
            float3 d = gGlintDir[k].xyz;
            float3 L = normalize(gFS0.z > 0.5 ? d : gCamR.xyz * d.x + gCamU.xyz * d.y - gCamF.xyz * d.z);
            float pw = lerp(6, 900, gGlintDir[k].w * gGlintDir[k].w);
            float3 h = normalize(L + v);
            add += gGlintCol[k].rgb * pow(saturate(dot(n, h)), pw) * (pw + 8) / 60;
        }
    }
    float mask = gFS0.w > 0.5 && i.uvl.z >= 16000 ? dot(gShineMask.Sample(gSamp, i.uvl.xy).rgb, float3(0.333, 0.333, 0.334)) : 1;
    return add * gFSTint.rgb * gFS0.y * mask;
}
// ---------------------------------------------------------------- water light: caustics (the waves gather sunlight into moving bright lines)
Texture2D<float> gWaterMap : register(t43);   // the water's surfaces seen from straight above (depth: how high the top water is at each spot)
float waterTop(float3 rel, out bool has)       // the top water surface's height over this spot (camera-relative y)
{
    has = false;
    if (gWater.z < 0.5) return 0;
    float4 c = mul(float4(rel, 1), gWaterVP); float2 uv = float2(c.x, -c.y) * 0.5 + 0.5;
    if (any(uv <= 0) || any(uv >= 1)) return 0;
    float d = gWaterMap.Load(int3(uv * 512, 0));
    if (d >= 0.99999) return 0;
    has = true; return 299 - d * 699;          // (seen from 300 above the camera, near plane 1, far 700)
}
float2 cauHash(float2 p) { float3 q = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973)); q += dot(q, q.yzx + 33.33); return frac((q.xx + q.yz) * q.zy); }
float causticCells(float2 p, float t)          // 0 on the lines between moving cells, more inside them
{
    p += 0.32 * float2(sin(p.y * 1.3 + t * 0.7), sin(p.x * 1.1 - t * 0.6));   // wobbly, like light through waves
    float2 i = floor(p), f = frac(p); float d1 = 8, d2 = 8;
    [unroll] for (int y = -1; y <= 1; y++) [unroll] for (int x = -1; x <= 1; x++)
    {
        float2 g = float2(x, y), h = cauHash(i + g);
        float2 o = 0.5 + 0.4 * sin(t * (0.55 + h * 0.45) + h * 6.2832);
        float d = length(g + o - f);
        if (d < d1) { d2 = d1; d1 = d; } else if (d < d2) d2 = d;
    }
    return d2 - d1;
}
float causticAt(float2 p, float t, bool high)  // bright lines 0..~1.6
{
    float a = causticCells(p, t), l = pow(saturate(1 - a * 3.2), 4);
    if (high) { float b = causticCells(p * 1.37 + float2(5.3, 2.1), t * 1.21); l = l * 0.75 + pow(saturate(1 - b * 3.2), 4) * 0.55 + l * pow(saturate(1 - b * 2.5), 2) * 0.8; }
    return l * 1.6;
}
float3 refractedSun()                          // the sun's light bent down by the water's surface (steeper, Snell)
{
    float2 h = gSunDir.xz / 1.33; float y = sqrt(saturate(1 - dot(h, h)));
    return float3(h.x, y, h.y);
}
// under the water: how the sun's light here is spread (rgb: 1 = unchanged; brighter lines, darker between);
// above it: the light the waves throw onto walls and undersides near the water (added). pixel: the screen spot
void waterLight(float3 rel, float3 nFlat, float3 n, float2 pixel, out float3 sunMul, out float3 bounced, out float3 ambMul)
{
    sunMul = 1; bounced = 0; ambMul = 1;   // ambMul: the sky's and the lamps' light under the water (caustics in every pool, even out of the sun - like Prime)
    if (gWater.x < 0.5) return;
    float3 q = pixSnap(rel, nFlat);
    bool has; float top = waterTop(q + nFlat * 0.45, has);
    if (!has) return;
    float depth = top - q.y, t = gVolume.z;
    float3 wp = gCamPos.xyz + q;
    float farK = saturate(1 - length(rel) / 96);
    if (depth > 0.01)
    {
        float3 L = gSunDir.y > 0.05 ? refractedSun() : float3(0, 1, 0);
        float2 p = (wp.xz + L.xz / max(L.y, 0.3) * depth) * 0.85;   // where this light came in through the surface
        bool high = gWater.x > 1.5;
        float3 cc;
        if (high) { float2 e = float2(0.045, 0.03) * (1 + depth * 0.15); cc = float3(causticAt(p + e, t, true), causticAt(p, t, true), causticAt(p - e, t, true)); }
        else cc = causticAt(p, t, false).xxx;
        float k = saturate(depth * 3) * exp(-depth * 0.09) * farK * gWater2.z;   // soft right at the surface, fading deeper down
        sunMul = max(lerp(1, 0.55 + cc, k), 0.15);
        ambMul = max(lerp(1, 0.68 + cc * 0.8, k * 0.9), 0.25);
        return;
    }
    if (depth > -4 && gWater.y > 0.5 && gSunDir.y > -0.05)
    {
        float facing = saturate(-n.y) + 0.55 * saturate(1 - abs(nFlat.y));    // undersides most, walls some, tops none
        if (facing <= 0.01) return;
        float3 at = float3(q.x + nFlat.x * 0.5, top, q.z + nFlat.z * 0.5);  // the patch of water below that throws the light
        float lit = shadowAt(at, float3(0, 1, 0), saturate(gSunDir.y), pixel) * cloudShadow(at);
        float2 p = wp.xz * 0.7 + wp.y * 0.35;
        float c = causticAt(p, t * 1.35, false);
        float k = pow(saturate(1 + depth / 4), 2) * facing * lit * saturate(gSunDir.y * 4) * farK * gWater2.z;
        bounced = gSunColor.rgb * c * k * 0.22;
    }
}

float3 lightBlock(VSOut i, float3 albedo, bool front, out float3 n, out float shine, out float3 indirect)
{
    n = front ? i.n : -i.n;
    float3 nFlat = n;                                                                             // the face itself, before the generated bumps
    TexMat m = (TexMat)0; m.b = float4(1, 1, 0, 0);
    if (i.uvl.z < 16000) m = gMats[(uint)round(i.uvl.z)]; else { m.a = gMM0; m.b = gMM1; }
    if ((gSurf.x > 0 || gSurf.y > 0) && dot(i.rel, i.rel) < 4096) surfaceDetail(i, n, albedo, m);   // (past 64 blocks the bumps are smaller than a pixel: skipped)
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
    float3 ambientNow = gAmbient.rgb * sky * skyV;
    if (gProbeP.x > 0.5)   // light probes: the light really arriving here (bounced, lamps, glowing things - not through walls)
    {
        float pw; float3 pl = probeLight(i.rel + nFlat * 0.05, nFlat, true, pw);
        ambientNow = lerp(ambientNow, pl, saturate(pw * 3));
    }
    float3 sunC = gSunColor.rgb, bounced = 0, ambMul = 1;
    if (gWater.x > 0.5) { float3 sm; waterLight(i.rel, nFlat, n, i.pos.xy, sm, bounced, ambMul); sunC *= sm; }   // caustics under water, their reflection above
    {   // (a slab's or slope's own face: the block of air in front of it may be its own block - then the next one out)
        float4 amb = ambAt(i.rel + nFlat * 0.5, n); if (amb.a <= 0 && dot(amb.rgb, 1) <= 0) amb = ambAt(i.rel + nFlat * 1.3, n);
        ambientNow = ambientNow * (1 - amb.a) + amb.rgb;
    }   // an ambient light's room: its own light from each direction
    float3 light = sunC * ndl * sh + ambientNow * i.ao * ambMul + bounced;
    light += lavaGlow(i.rel + nFlat * 0.02, nFlat) * lerp(0.5, 1, i.ao) * ambMul;   // lava nearby lights it
    light = max(light, gMinLight.rgb * sky * i.ao * (1 - skyV));                                   // closed spaces: never darker than the world's minimum
    float lamp = max(i.lamp.r, max(i.lamp.g, i.lamp.b));
    float3 hue = lamp > 0 ? i.lamp.rgb / lamp : 0;                                                   // the colour of the lamps nearby
    light += hue * (pow(lamp, 3.2) * 0.9) * gExtra.w * lerp(0.5, 1, i.ao) * ambMul;
    float metal = saturate(m.a.y) * saturate(max(saturate(m.a.x), i.mat.x) * 4);   // (metal only shows when it is shiny at all: matt metal is just its colour)
    float dShine = max(max(saturate(m.a.x), i.mat.x), wet * 0.9);                                  // how shiny for the lights' highlights (wet streets!)
    float3 dSpec;
    sFlatN = nFlat;
    float3 dyn = dynLightsSpec(i.rel, n, -normalize(i.rel), lerp(10, 380, dShine * dShine), gOffset.w < 0.5, dSpec) * lerp(0.6, 1, i.ao) * ambMul;
    sFlatN = 0;   // placed lights, strips, glowing emitters, shots
    if (gCook.y > 0.5 && gOffset.w > 0.5) dyn += cookLight(i.rel, nFlat, n) * lerp(0.6, 1, i.ao) * ambMul;   // the cooked world: the lamps' baked light, far away
    float3 c = albedo * (light + dyn) * (1 - 0.75 * metal);                                       // metal: little plain colour ...
    c += dSpec * lerp(float3(0.04, 0.04, 0.04) + 0.5 * dShine, albedo * 1.4, metal) * max(dShine, 0.12);   // the lights' highlights
    if (gFS0.x > 0.5 && gOffset.w < 0.5) c += fakeShine(i, n);                                    // fake shine (models): painted-on glints, like Prime's arm cannon
    indirect = albedo * (light - sunC * ndl * sh) * (1 - 0.75 * metal);
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
    float3 gTint = glowTint(i.uvl.z < 16000 ? m.b.w : 0);                                        // a glowing face's colour (white: its own)
    if (m.a.z > 0)                                                                                // glowing pixels (the bright ones, if set)
    {
        float lum = sqrt(lumOf(albedo));
        float e = m.a.z * smoothstep(m.a.w, m.a.w + 0.08, lum);
        c += albedo * gTint * e * 3.0;
    }
    if (m.b.z > 0)                                                                                // its emission map: what glows, in its own colours
    {
        float3 em = i.uvl.z < 16000 ? gEmitTex.Sample(gSamp, i.uvl).rgb : gMeshEmit.Sample(gSamp, i.uvl.xy).rgb;
        c += em * gTint * m.b.z * 2.5;
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

// lava: bright and molten, darker crust drifting on it in cells, in the blocks' pixel grid; falls run down; a hot rim where it meets blocks
float3 lavaColor(float3 rel, float3 n, float rim)
{
    float3 wp = gCamPos.xyz + rel; float t = gVolume.z;
    bool side = abs(n.y) < 0.5;
    float2 p = side ? float2(abs(n.x) > 0.5 ? wp.z : wp.x, wp.y + t * 1.3) : wp.xz;   // (lava falls: running down)
    p = (floor(p * 16) + 0.5) / 16;                                                   // the blocks' pixel grid: sharp, like the textures
    float cells = causticCells(p * 0.8 + float2(t * 0.04, t * 0.025), t * 0.3);       // 0 on the lines between cells
    float n1 = gNoise.SampleLevel(gLinear, float3(p * 0.09 + t * 0.004, t * 0.006), 0).x;
    float crust = smoothstep(0.5, 0.8, n1 + (side ? -0.3 : 0)) * smoothstep(0.1, 0.4, cells);   // dark patches, cell-shaped
    float hotLine = 1 - smoothstep(0.0, 0.18, cells);                                 // the lines between cells: hottest
    float3 hot = lerp(float3(1.0, 0.30, 0.035), float3(1.0, 0.66, 0.2), saturate(n1 * 1.1 + hotLine * 0.7));
    float3 c = lerp(hot * 3.4, float3(0.32, 0.055, 0.012), crust * 0.8);
    c += float3(1.0, 0.6, 0.18) * saturate(rim) * 3.5;                                // where it meets blocks
    return side ? c * 1.2 : c;
}

PSOut PS_Opaque(VSOut i, bool front : SV_IsFrontFace)
{
    float4 t = albedoAt(i.uvl);
    PSOut o; float3 n; float sh;
    o.indirect = 0;
    o.albedo = float4(t.rgb, i.uvl.z < 16000 ? gMats[(uint)round(i.uvl.z)].c.x : 0);
    if (i.mat.z >= 0) o.albedo.a = i.mat.z;   // (the block's own roughness)   // a: how rough (blurry reflections)
    if (i.kind > 3.5) { o.color = float4(lavaColor(i.rel, i.n, 1 - i.ao), 1); o.surface = float4(i.n * 0.5 + 0.5, 0); o.albedo = 0; return o; }
    float3 ind;
    o.color = float4(hdrSafe(lightBlock(i, t.rgb, front, n, sh, ind)), 1);
    o.surface = float4(n * 0.5 + 0.5, i.mat.y > 0.5 ? 1 : min(sh, 0.98));   // 1 = mirror
    o.indirect = float4(ind, 0);
    return o;
}

PSOut PS_Cutout(VSOut i, bool front : SV_IsFrontFace)
{
    float4 t = albedoAt(i.uvl);
    clip(t.a - 0.5);
    PSOut o; float3 n; float sh; float3 ind;
    o.albedo = float4(t.rgb, i.uvl.z < 16000 ? gMats[(uint)round(i.uvl.z)].c.x : 0);
    if (i.mat.z >= 0) o.albedo.a = i.mat.z;   // (the block's own roughness)
    o.color = float4(hdrSafe(lightBlock(i, t.rgb, front, n, sh, ind)), 1);
    o.surface = float4(n * 0.5 + 0.5, i.mat.y > 0.5 ? 1 : min(sh, 0.98));
    o.indirect = float4(ind, 0);
    return o;
}

// the see-through preview of the block you are about to place
float4 PS_Ghost(VSOut i) : SV_Target
{
    float4 t = albedoAt(i.uvl);
    clip(t.a - 0.5);
    float3 c = t.rgb * (gAmbient.rgb + gSunColor.rgb * 0.3) * 1.2;
    if (gOffset.w > 1.5) return float4(lerp(c, float3(1.0, 0.12, 0.08), 0.65), 0.55);   // build modes: blocks about to be broken
    return float4(c, 0.5);
}

// ---------------------------------------------------------------- shadow map
struct ShOut { float4 pos : SV_Position; float3 uvl : TEXCOORD0; };
ShOut VS_Shadow(VSIn i)
{
    float layer; float3 p = sway(meshToRel(i.pos), i.layer, i.uv.y, layer);
    ShOut o; o.pos = mul(float4(p, 1), gShadowVP); o.uvl = float3(i.uv, layer); return o;
}
void PS_ShadowCutout(ShOut i) { clip(albedoAt(i.uvl).a - 0.5); }
ShOut VS_WaterMap(VSIn i)   // the water map: only the water's tops, seen from straight above
{
    ShOut o; o.pos = mul(float4(meshToRel(i.pos), 1), gWaterVP); o.uvl = float3(i.uv, 0);
    if (abs(floor(i.layer / 16384) - 3) > 0.5 || unpackNormal(i.normal).y < 0.5) o.pos = float4(-5, -5, 0.5, 1);   // (anything else: a point off the map)
    return o;
}
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
Texture2D gAlbedo : register(t25);   // the surfaces' own colours (bounce light); a: how rough (blurry reflections)
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
    [loop] for (int k = 0; k < steps && travelled < reflReach(); k++)
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
Texture2D gAOMap : register(t8);
float3 viewDir(float2 ndc) { return normalize(gCamF.xyz + gCamR.xyz * (ndc.x * gProj.x * gProj.y) + gCamU.xyz * (ndc.y * gProj.x)); }
// ---------------------------------------------------------------- smaller pictures (a half, a third or a quarter of the size: the
// soft things - ambient occlusion, bounce light, fog) brought back to full size without blurring over edges
float lowLz(int2 q, float2 ratio)   // the distance a smaller picture's pixel q stands for (the full-size pixel it was worked out at)
{
    int2 p = min(int2((float2(q) + 0.5) * ratio), int2(gDepth.zw) - 1);
    float z = gSceneDepth.Load(int3(p, 0)); return z >= 1 ? 1e5 : linDepth(z);
}
float4 upLow(Texture2D t, int2 px, float lz)   // the four smaller pixels around, weighted by how near and how alike in distance
{
    float2 ls; t.GetDimensions(ls.x, ls.y); float2 ratio = gDepth.zw / ls;
    float2 f = (float2(px) + 0.5) / ratio - 0.5; int2 q0 = int2(floor(f)); float2 fr = f - floor(f);
    int2 hi = int2(ls) - 1;
    float4 s = 0, best = 0; float w = 0, bestD = 1e9;
    [unroll] for (int k = 0; k < 4; k++)
    {
        int2 o = int2(k & 1, k >> 1); int2 q = clamp(q0 + o, int2(0, 0), hi);
        float bw = (o.x ? fr.x : 1 - fr.x) * (o.y ? fr.y : 1 - fr.y) + 1e-3;
        float dz = abs(lowLz(q, ratio) - lz);
        float4 v = t.Load(int3(q, 0));
        if (dz < bestD) { bestD = dz; best = v; }
        float ww = bw * exp(-dz / (lz * 0.04 + 0.08));
        s += v * ww; w += ww;
    }
    return w > 1e-3 ? s / w : best;   // (nothing alike around: the closest in distance)
}

float4 PS_AO(SkyOut i) : SV_Target
{
    int2 px = min(int2((i.ndc * float2(0.5, -0.5) + 0.5) * gDepth.zw), int2(gDepth.zw) - 1);   // (whatever the smaller picture's size)
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
float4 PS_AOBlur(SkyOut i) : SV_Target   // the smaller picture: a careful 5 x 5 blur (only across similar distances)
{
    float2 ls; gAOMap.GetDimensions(ls.x, ls.y); float2 ratio = gDepth.zw / ls;
    int2 q0 = int2(i.pos.xy), hi = int2(ls) - 1;
    float lin = lowLz(q0, ratio); if (lin > 9e4) return 1;
    float sum = 0, wsum = 0;
    [unroll] for (int y = -2; y <= 2; y++)
    [unroll] for (int x = -2; x <= 2; x++)
    {
        int2 q = clamp(q0 + int2(x, y), int2(0, 0), hi);
        float w = saturate(1 - abs(lowLz(q, ratio) - lin) / (lin * 0.04 + 0.08)) * (1.0 / (1 + x * x + y * y)) + 1e-4;
        sum += gAOMap.Load(int3(q, 0)).r * w; wsum += w;
    }
    return sum / wsum;
}
float4 PS_AOApply(SkyOut i) : SV_Target
{
    int2 px = int2(i.pos.xy);
    float zb = gSceneDepth.Load(int3(px, 0));
    if (zb >= 1) return 0;
    float lin = linDepth(zb);
    float ao = saturate(upLow(gAOMap, px, lin).r);
    ao = pow(ao, 2.2 * gAOSet.z);
    float3 ind = gIndirect.Load(int3(px, 0)).rgb;
    return float4(-ind * (1 - ao), 0);   // added on top of the picture: takes the hidden sky light away
}

// ---------------------------------------------------------------- voxel ray tracing: reflection rays step through the blocks around you
// block by block (a copy of them on the graphics card), so they find what is off screen and behind you too - any graphics card
Texture3D<uint> gVox : register(t30);              // palette number per block (0: nothing), wrapping (only changed chunks are sent)
struct VoxType { uint4 faces; uint4 info; };      // faces: texture layer per face, two per number (16 bits each); info: x 1 solid / 2 cut-out, y lamp level, z lamp colour
StructuredBuffer<VoxType> gVoxPal : register(t31);
Texture2DArray gVoxTex : register(t29);            // the block textures (the same array as t0 in the world pass)
static const int3 VOXN = int3(192, 96, 192);
uint voxAt(int3 q) { uint3 s = uint3(q + int3(gVoxM.xyz)) % uint3(VOXN); return gVox.Load(int4(int3(s), 0)); }
// objects (props, doors, platforms, pickups ...): a small voxel copy of each model (32 x 32 x 32, its colours), placed where it is
Texture3D gObjBricks : register(t32);              // 8 x 8 copies side by side (256 x 256 x 32)
struct ObjVox { float4 r0, r1, r2; float4 info; }; // camera-relative position -> 0..1 inside the copy (three rows); info.x which copy
StructuredBuffer<ObjVox> gObjVox : register(t33);
float3 reflLight(float3 hp, float3 hn, float2 pixel, out float sv)   // light at a point a reflection ray found
{
    float ndl = saturate(dot(hn, gSunDir.xyz));
    sv = skyVis(hp, hn);
    float sh = ndl > 0 ? shadowAt(hp, hn, ndl, pixel) : 0;
    float farOut = gSunDir.w < 0.5 ? 1 : saturate((length(hp) - gCascR.w * 0.92) / max(gCascR.w * 0.08, 1)); sh *= lerp(1, sv, farOut);
    float3 light = gSunColor.rgb * ndl * sh + gAmbient.rgb * (0.55 + 0.45 * hn.y) * sv;
    light = max(light, gMinLight.rgb * (0.55 + 0.45 * hn.y) * (1 - sv));
    float3 dif = 0, sp = 0; uint nl = (uint)gCl1.y;                 // the nearest lights (glowing blocks around you too)
    [loop] for (uint k = 0; k < nl; k++) evalLight(gLightBuf[k], hp, hn, -normalize(hp), 16, true, dif, sp);
    return light + dif;
}
float3 reflFog(float3 c, float3 hp, float sv)
{
    float d = length(hp), f = saturate((d - gFogRange.x) / max(gFogRange.y - gFogRange.x, 1));
    return lerp(c, skyColor(normalize(hp)) * max(sv, gOcc.y), f * f);
}
float3 voxShade(float3 hp, float3 hn, uint layer, uint id, float2 uv, float t, float2 pixel)
{
    VoxType vt = gVoxPal[id];
    float4 tex = gVoxTex.SampleLevel(gSamp, float3(uv, layer), clamp(log2(max(t, 1) * 0.35), 0, 4));
    float3 albedo = tex.rgb; TexMat m = gMats[layer];
    float sv; float3 c = albedo * reflLight(hp, hn, pixel, sv);
    if (vt.info.y > 0)   // a lamp block glows in its colour
    {
        float3 lc = float3((vt.info.z >> 16) & 255, (vt.info.z >> 8) & 255, vt.info.z & 255) / 255.0;
        c = (dot(albedo, float3(0.2126, 0.7152, 0.0722)) * lc * 0.85 + albedo * lc * 0.3) * 3.2 * gExtra.w;
    }
    float3 gTint = glowTint(m.b.w);
    if (gSurf.z > 0.5 && m.a.z > 0) c += albedo * gTint * m.a.z * smoothstep(m.a.w, m.a.w + 0.08, sqrt(dot(albedo, float3(0.2126, 0.7152, 0.0722)))) * 3.0;
    if (m.b.z > 0) c += gEmitTex.SampleLevel(gSamp, float3(uv, layer), 0).rgb * gTint * m.b.z * 2.5;
    return reflFog(c, hp, sv);
}
// the nearest object a ray meets before maxT: its colour and the side it hit
bool objectTrace(float3 P, float3 r, float maxT, out float tHit, out float3 nHit, out float3 albedo)
{
    tHit = maxT; nHit = 0; albedo = 0; bool found = false;
    uint count = (((uint)gVoxM.w) & 3u) == 3u ? (uint)gPlanar.y : 0;
    [loop] for (uint k = 0; k < count; k++)
    {
        ObjVox ov = gObjVox[k];
        float3 o = float3(dot(ov.r0.xyz, P) + ov.r0.w, dot(ov.r1.xyz, P) + ov.r1.w, dot(ov.r2.xyz, P) + ov.r2.w);
        float3 d = float3(dot(ov.r0.xyz, r), dot(ov.r1.xyz, r), dot(ov.r2.xyz, r));
        float3 inv = 1 / (abs(d) > 1e-8 ? d : 1e-8);
        float3 t0 = (0 - o) * inv, t1 = (1 - o) * inv, tn = min(t0, t1), tf = max(t0, t1);
        float te = max(max(tn.x, tn.y), max(tn.z, 0.02)), tx = min(min(tf.x, tf.y), tf.z);
        if (tx <= te || te >= tHit) continue;
        // step through its 32 x 32 x 32 copy
        float3 g = (o + d * (te + 1e-4)) * 32, gd = d * 32;
        int3 cell = clamp(int3(floor(g)), 0, 31), stp = int3(sign(gd));
        float3 ginv = 1 / (abs(gd) > 1e-8 ? gd : 1e-8);
        float3 tMax = ((float3(cell) + float3(stp > 0)) - g) * ginv + te, tDelta = abs(ginv);
        float t = te; int axis = 0; { float3 a = abs(tn - te); axis = a.x < 1e-4 ? 0 : a.y < 1e-4 ? 1 : 2; }
        int2 base = int2((int)ov.info.x % 8, (int)ov.info.x / 8) * 32;
        [loop] for (int s = 0; s < 96; s++)
        {
            if (any(cell < 0) || any(cell > 31) || t >= tHit) break;
            float4 v = gObjBricks.Load(int4(base.x + cell.x, base.y + cell.y, cell.z, 0));
            if (v.a > 0.5)
            {
                float3 nl = axis == 0 ? float3(-stp.x, 0, 0) : axis == 1 ? float3(0, -stp.y, 0) : float3(0, 0, -stp.z);
                tHit = t; albedo = v.rgb; found = true;
                nHit = normalize(ov.r0.xyz * nl.x + ov.r1.xyz * nl.y + ov.r2.xyz * nl.z);
                break;
            }
            if (tMax.x < tMax.y && tMax.x < tMax.z) { t = tMax.x; tMax.x += tDelta.x; cell.x += stp.x; axis = 0; }
            else if (tMax.y < tMax.z) { t = tMax.y; tMax.y += tDelta.y; cell.y += stp.y; axis = 1; }
            else { t = tMax.z; tMax.z += tDelta.z; cell.z += stp.z; axis = 2; }
        }
    }
    return found;
}
// a lamp block's shadow: from the point toward the lamp, block by block through the copy of the world; any solid block in between
// shades it (the lamp's own block does not). The point is already snapped to the texture pixels when shadows are pixelated.
float voxShadow(float3 P, float3 lp, bool face, bool far)
{
    if ((((uint)gVoxM.w) & 1u) == 0 || (!far && (((uint)gVoxM.w) & 4u) == 0)) return 1;   // (no blocks on the card; near: ray traced lamp shadows off. Far lamps without a map always use it)
    float3 o = P - gVoxO.xyz, e = lp - gVoxO.xyz, r = e - o; float len = length(r);
    if (face) { int3 gc = int3(floor(e)); if (all(gc >= 0) && all(gc < VOXN)) { uint gi = voxAt(gc); if (gi != 0 && gVoxPal[gi].info.x == 1) return 0; } }   // that side of the lamp is against a block: it doesn't shine
    if (len < 0.6) return 1;
    r /= len;
    int3 cell = int3(floor(o)), goal = int3(floor(e)), stp = int3(sign(r));
    float3 inv = 1 / max(abs(r), 1e-6) * sign(r + 1e-9);
    float3 tMax = ((float3(cell) + float3(stp > 0)) - o) * inv, tDelta = abs(inv);
    float stopAt = len - 0.05;   // (a lamp's light sits in front of its open faces, so every block on the way counts: no light leaking through walls)
    [loop] for (int k = 0; k < 64; k++)
    {
        if (any(cell < 0) || any(cell >= VOXN) || all(cell == goal)) return 1;
        uint id = voxAt(cell);
        if (id != 0)
        {
            uint kind = gVoxPal[id].info.x;
            if (kind == 1 && k == 0) return 0;          // the point itself is inside a solid block (a corner): no light leaks through the edge
            if (k > 0 && (kind == 1 || kind == 3)) return 0;   // (see-through blocks let it through)
        }
        float t = min(tMax.x, min(tMax.y, tMax.z)); if (t > stopAt) return 1;
        {   // through an edge or a corner (two sides crossed at almost the same moment): the blocks on both sides of it count, so no light slips between two blocks that only touch at an edge
            const float tie = 0.02;
            #define VSOLID(c) (any((c) != goal) && voxAt(c) != 0 && gVoxPal[voxAt(c)].info.x == 1)
            if (abs(tMax.x - t) < tie && abs(tMax.y - t) < tie) { if (VSOLID(cell + int3(stp.x, 0, 0)) || VSOLID(cell + int3(0, stp.y, 0))) return 0; }
            if (abs(tMax.x - t) < tie && abs(tMax.z - t) < tie) { if (VSOLID(cell + int3(stp.x, 0, 0)) || VSOLID(cell + int3(0, 0, stp.z))) return 0; }
            if (abs(tMax.y - t) < tie && abs(tMax.z - t) < tie) { if (VSOLID(cell + int3(0, stp.y, 0)) || VSOLID(cell + int3(0, 0, stp.z))) return 0; }
            #undef VSOLID
        }
        if (tMax.x < tMax.y && tMax.x < tMax.z) { tMax.x += tDelta.x; cell.x += stp.x; }
        else if (tMax.y < tMax.z) { tMax.y += tDelta.y; cell.y += stp.y; }
        else { tMax.z += tDelta.z; cell.z += stp.z; }
    }
    return 1;
}
bool voxelTrace(float3 P, float3 n, float3 r, float2 pixel, out float3 col)
{
    col = 0;
    if ((((uint)gVoxM.w) & 3u) != 3u) return false;   // (ray traced reflections off: only the screen, else the sky)
    float3 o = P + n * 0.02 - gVoxO.xyz;                                 // into the volume's own blocks
    int3 cell = int3(floor(o)), stp = int3(sign(r));
    if (all(cell >= 0) && all(cell < VOXN)) { uint s0 = voxAt(cell); if (s0 != 0 && gVoxPal[s0].info.x == 1) return true; }   // starting inside a block (the seam of an inside corner): nothing to see - not what lies behind the wall
    float3 inv = 1 / max(abs(r), 1e-6) * sign(r + 1e-9);
    float3 tMax = ((float3(cell) + float3(stp > 0)) - o) * inv, tDelta = abs(inv);
    float t = 0; int axis = 1; int steps = (int)gVoxO.w;
    bool found = false; uint hitId = 0, hitLayer = 0; float2 hitUV = 0; float3 hitN = 0, hitP = 0; float hitT = reflReach() * 1.5;
    [loop] for (int k = 0; k < steps; k++)
    {
        if (any(cell < 0) || any(cell >= VOXN) || t > reflReach() * 1.5) break;
        uint id = k > 0 ? voxAt(cell) : 0;                              // (the first block: where the ray starts)
        if (id != 0)
        {
            float3 hpv = o + r * t;
            float3 hn = axis == 0 ? float3(-stp.x, 0, 0) : axis == 1 ? float3(0, -stp.y, 0) : float3(0, 0, -stp.z);
            int face = axis * 2 + ((axis == 0 ? hn.x : axis == 1 ? hn.y : hn.z) > 0 ? 0 : 1);
            float3 f = frac(hpv);
            float2 uv = axis == 0 ? float2(hn.x > 0 ? 1 - f.z : f.z, 1 - f.y) : axis == 1 ? float2(f.x, hn.y > 0 ? f.z : 1 - f.z) : float2(hn.z > 0 ? f.x : 1 - f.x, 1 - f.y);
            VoxType vt = gVoxPal[id];
            uint pair = face < 2 ? vt.faces.x : face < 4 ? vt.faces.y : vt.faces.z;
            uint layer = (face & 1) ? (pair >> 16) : (pair & 0xFFFF);
            bool see = vt.info.x == 2 && gVoxTex.SampleLevel(gSamp, float3(uv, layer), 0).a < 0.5;   // a see-through pixel (glass, leaves): the ray goes on
            if (vt.info.x == 3 && t < 1.75) see = true;   // slopes, slabs, stairs right next to where the ray starts: (as whole blocks) they would reflect themselves
            if (!see) { found = true; hitId = id; hitLayer = layer; hitUV = uv; hitN = hn; hitP = hpv + gVoxO.xyz; hitT = t; break; }
        }
        if (tMax.x < tMax.y && tMax.x < tMax.z) { t = tMax.x; tMax.x += tDelta.x; cell.x += stp.x; axis = 0; }
        else if (tMax.y < tMax.z) { t = tMax.y; tMax.y += tDelta.y; cell.y += stp.y; axis = 1; }
        else { t = tMax.z; tMax.z += tDelta.z; cell.z += stp.z; axis = 2; }
    }
    float3 Ps = P + n * 0.02; float ot; float3 on, oa;
    if (objectTrace(Ps, r, hitT, ot, on, oa))   // an object before the block: its voxel copy
    {
        float3 hp = Ps + r * ot; float sv;
        col = reflFog(oa * reflLight(hp, on, pixel, sv), hp, sv);
        return true;
    }
    if (!found) return false;
    col = voxShade(hitP, hitN, hitLayer, hitId, hitUV, length(hitP - P), pixel);
    return true;
}
// ---------------------------------------------------------------- light probes: bounce light everywhere, found by rays through the blocks
// A grid of 32 x 16 x 32 probes around you (wrapping, like the blocks' copy). Each knows the light coming from its six sides (rays through the
// blocks: the sun, the sky, lamps and glowing things, and light that bounced). Surfaces read the probes around them.
// The atlas: x = probe.x * 6 + side (+x -x +y -y +z -z), y = probe.y + probe.z * 16; rgb the light, a the probe's world key (-1: inside a block)
static const int3 PRN = int3(32, 16, 32);
Texture2D<float4> gProbes : register(t39);
float probeKey(int3 k) { uint3 wc = uint3(int3(gProbeB.xyz) + k); return (float)((wc.x & 255u) | ((wc.y & 255u) << 8) | ((wc.z & 255u) << 16)); }
int3 probeSlot(int3 k) { return (k + int3(gProbeM.xyz)) % PRN; }
// the light arriving at a surface (rel, facing n) from the probes around it; w: how sure (0: no probe could say)
bool voxLineBlocked(float3 a, float3 b)   // is there a solid block on the straight line a -> b (camera-relative)? every block it passes, edges and corners too
{
    float3 o = a - gVoxO.xyz, e = b - gVoxO.xyz, r = e - o; float len = length(r); if (len < 1e-3) return false;
    r /= len;
    int3 cell = int3(floor(o)), goal = int3(floor(e)), stp = int3(sign(r));
    float3 inv = 1 / max(abs(r), 1e-6) * sign(r + 1e-9);
    float3 tMax = ((float3(cell) + float3(stp > 0)) - o) * inv, tDelta = abs(inv);
    [loop] for (int k = 0; k < 24; k++)
    {
        if (any(cell < 0) || any(cell >= VOXN)) return false;
        uint id = voxAt(cell); if (id != 0 && gVoxPal[id].info.x == 1) return true;
        if (all(cell == goal)) return false;
        float t = min(tMax.x, min(tMax.y, tMax.z)); if (t > len) return false;
        const float tie = 0.02;
        #define LSOLID(c) (all((c) >= 0) && all((c) < VOXN) && voxAt(c) != 0 && gVoxPal[voxAt(c)].info.x == 1)
        if (abs(tMax.x - t) < tie && abs(tMax.y - t) < tie && (LSOLID(cell + int3(stp.x, 0, 0)) || LSOLID(cell + int3(0, stp.y, 0)))) return true;
        if (abs(tMax.x - t) < tie && abs(tMax.z - t) < tie && (LSOLID(cell + int3(stp.x, 0, 0)) || LSOLID(cell + int3(0, 0, stp.z)))) return true;
        if (abs(tMax.y - t) < tie && abs(tMax.z - t) < tie && (LSOLID(cell + int3(0, stp.y, 0)) || LSOLID(cell + int3(0, 0, stp.z)))) return true;
        #undef LSOLID
        if (tMax.x < tMax.y && tMax.x < tMax.z) { tMax.x += tDelta.x; cell.x += stp.x; }
        else if (tMax.y < tMax.z) { tMax.y += tDelta.y; cell.y += stp.y; }
        else { tMax.z += tDelta.z; cell.z += stp.z; }
    }
    return false;
}
float3 probeLight(float3 rel, float3 n, bool leakCheck, out float wsum)
{
    wsum = 0; float3 acc = 0;
    if (gProbeP.x < 0.5) return 0;
    float S = gProbeO.w; float3 g = (rel - gProbeO.xyz) / S - 0.5; int3 b = int3(floor(g)); float3 f = g - float3(b);
    int3 sideOf = int3(n.x >= 0 ? 0 : 1, n.y >= 0 ? 2 : 3, n.z >= 0 ? 4 : 5);
    [unroll] for (int c = 0; c < 8; c++)
    {
        int3 o = int3(c & 1, (c >> 1) & 1, c >> 2), k = b + o;
        if (any(k < 0) || any(k >= PRN)) continue;
        int3 sl = probeSlot(k); int2 base = int2(sl.x * 6, sl.y + sl.z * 16); float key = probeKey(k);
        float4 fx = gProbes.Load(int3(base.x + sideOf.x, base.y, 0));
        if (abs(fx.a - key) > 0.5) continue;
        float4 fy = gProbes.Load(int3(base.x + sideOf.y, base.y, 0)), fz = gProbes.Load(int3(base.x + sideOf.z, base.y, 0));
        float3 L = n.x * n.x * fx.rgb + n.y * n.y * fy.rgb + n.z * n.z * fz.rgb;
        float3 pp = gProbeO.xyz + (float3(k) + 0.5) * S, to = pp - rel; float dist = max(length(to), 1e-3);
        float3 tri = lerp(1 - f, f, float3(o));
        float wt = tri.x * tri.y * tri.z;
        float face = (dot(n, to / dist) + 1) * 0.5; wt *= face * face + 0.05;   // probes behind the surface count little
        if (leakCheck && gVoxM.w > 0.5 && voxLineBlocked(rel + n * 0.03, pp)) continue;   // a block between the surface and the probe: it can't see this spot (no light through walls or the seams of corners)
        acc += L * wt; wsum += wt;
    }
    return wsum > 1e-5 ? acc / wsum : 0;
}
// one probe ray: the light coming back along it (what it hits, lit - or the sky if it gets out)
float3 probeRay(float3 P, float3 r)
{
    float3 o = P - gVoxO.xyz;
    int3 cell = int3(floor(o)), stp = int3(sign(r));
    float3 inv = 1 / max(abs(r), 1e-6) * sign(r + 1e-9);
    float3 tMax = ((float3(cell) + float3(stp > 0)) - o) * inv, tDelta = abs(inv);
    float t = 0; int axis = 1; float range = gProbeP.w;
    bool found = false; uint hitId = 0, hitLayer = 0; float2 hitUV = 0; float3 hitN = 0;
    [loop] for (int k = 0; k < 96; k++)
    {
        if (any(cell < 0) || any(cell >= VOXN) || t > range) break;
        uint id = voxAt(cell);
        if (id != 0)
        {
            float3 hpv = o + r * t;
            float3 hn = axis == 0 ? float3(-stp.x, 0, 0) : axis == 1 ? float3(0, -stp.y, 0) : float3(0, 0, -stp.z);
            int face = axis * 2 + ((axis == 0 ? hn.x : axis == 1 ? hn.y : hn.z) > 0 ? 0 : 1);
            float3 fr = frac(hpv);
            float2 uv = axis == 0 ? float2(hn.x > 0 ? 1 - fr.z : fr.z, 1 - fr.y) : axis == 1 ? float2(fr.x, hn.y > 0 ? fr.z : 1 - fr.z) : float2(hn.z > 0 ? fr.x : 1 - fr.x, 1 - fr.y);
            VoxType vt = gVoxPal[id];
            uint pair = face < 2 ? vt.faces.x : face < 4 ? vt.faces.y : vt.faces.z;
            uint layer = (face & 1) ? (pair >> 16) : (pair & 0xFFFF);
            if (!(vt.info.x == 2 && gVoxTex.SampleLevel(gSamp, float3(uv, layer), 2).a < 0.5)) { found = true; hitId = id; hitLayer = layer; hitUV = uv; hitN = hn; break; }
        }
        if (tMax.x < tMax.y && tMax.x < tMax.z) { t = tMax.x; tMax.x += tDelta.x; cell.x += stp.x; axis = 0; }
        else if (tMax.y < tMax.z) { t = tMax.y; tMax.y += tDelta.y; cell.y += stp.y; axis = 1; }
        else { t = tMax.z; tMax.z += tDelta.z; cell.z += stp.z; axis = 2; }
    }
    float tHit = found ? t : min(t, range); float ot; float3 on, oa;
    if (objectTrace(P, r, tHit, ot, on, oa))   // an object first (a door, a prop): lit like the blocks
    {
        float3 hp = P + r * ot; float sv; float pw;
        return oa * (reflLight(hp, on, float2(0, 0), sv) + probeLight(hp + on * 0.05, on, false, pw) * 0.6);
    }
    if (!found)   // out in the open (or out of reach): the sky, if this place is under it
    {
        float3 end = P + r * tHit;
        return gAmbient.rgb * (0.55 + 0.45 * r.y) * skyAt(end + float3(0, 0.5, 0)) * 1.15;
    }
    float3 hp = P + r * t;
    VoxType vt = gVoxPal[hitId];
    float3 albedo = gVoxTex.SampleLevel(gSamp, float3(hitUV, hitLayer), 3).rgb; TexMat m = gMats[hitLayer];
    float sv; float pw; float3 c = albedo * (reflLight(hp, hitN, float2(0, 0), sv) + probeLight(hp + hitN * 0.05, hitN, false, pw) * 0.6);   // (the probes' own light: more bounces, over time)
    if (vt.info.y > 0) { float3 lc = float3((vt.info.z >> 16) & 255, (vt.info.z >> 8) & 255, vt.info.z & 255) / 255.0; c = (dot(albedo, float3(0.2126, 0.7152, 0.0722)) * lc * 0.85 + albedo * lc * 0.3) * 3.2; }
    float3 gT = glowTint(m.b.w);
    if (gSurf.z > 0.5 && m.a.z > 0) c += albedo * gT * m.a.z * smoothstep(m.a.w, m.a.w + 0.08, sqrt(dot(albedo, float3(0.2126, 0.7152, 0.0722)))) * 3.0;
    if (m.b.z > 0) c += gEmitTex.SampleLevel(gSamp, float3(hitUV, hitLayer), 2).rgb * gT * m.b.z * 2.5;
    return c;
}
// the probes' update (the whole atlas each frame; only every few frames' turn traces, the rest keep what they know)
Texture2D<float4> gProbeOld : register(t38);
float4 PS_ProbeUpdate(SkyOut i) : SV_Target
{
    int2 tx = int2(i.pos.xy); int side = tx.x % 6; int3 slot = int3(tx.x / 6, tx.y % 16, tx.y / 16);
    int3 k = ((slot - int3(gProbeM.xyz)) % PRN + PRN) % PRN;
    float key = probeKey(k);
    float4 old = gProbeOld.Load(int3(tx, 0));
    bool same = abs(old.a - key) < 0.5;
    uint id = (uint)(slot.x + slot.y * 32 + slot.z * 512);
    bool due = ((id * 7u + (uint)gProbeM.w) % max((uint)gProbeP.z, 1u)) == 0;
    if (same && !due) return old;
    if (gVoxM.w < 0.5) return float4(0, 0, 0, -1);
    float3 P = gProbeO.xyz + (float3(k) + 0.5) * gProbeO.w;
    int3 vc = int3(floor(P - gVoxO.xyz));
    if (any(vc < 0) || any(vc >= VOXN) || voxAt(vc) != 0) return float4(0, 0, 0, -1);   // inside a block (or outside the blocks' copy): it can't see
    float3 fn = side == 0 ? float3(1, 0, 0) : side == 1 ? float3(-1, 0, 0) : side == 2 ? float3(0, 1, 0) : side == 3 ? float3(0, -1, 0) : side == 4 ? float3(0, 0, 1) : float3(0, 0, -1);
    float3 ta = abs(fn.y) > 0.5 ? float3(1, 0, 0) : float3(0, 1, 0), tb = cross(fn, ta); ta = cross(tb, fn);
    int N = max((int)gProbeP.y, 1); float rnd = frac(gProbeM.w * 0.618034 + id * 0.1234567 + side * 0.37);
    float3 sum = 0;
    [loop] for (int j = 0; j < N; j++)   // cosine-weighted around this side (as light falls on a surface facing it)
    {
        float u1 = frac((j + 0.5) / N + rnd), u2 = frac(j * 0.618034 + rnd * 7.31);
        float rr = sqrt(u1), ph = u2 * 6.2831853;
        float3 d = normalize(ta * (rr * cos(ph)) + tb * (rr * sin(ph)) + fn * sqrt(max(1 - u1, 0)));
        sum += probeRay(P, d);
    }
    float3 irr = min(sum / N, 32);
    return float4(same ? lerp(old.rgb, irr, 0.3) : irr, key);
}

// gMinLight.w < 1: drawn smaller (only the reflection: rgb what it adds, a how much of the picture it covers), then PS_ReflComp lays it over
float4 PS_Reflect(SkyOut i) : SV_Target
{
    float rs = gMinLight.w > 0.01 ? gMinLight.w : 1; bool small = true;   // (always into its own picture: PS_ReflComp lays it over, blurred as rough as each surface is)
    int2 px = int2(min(i.pos.xy / rs, gDepth.zw - 1));
    float3 scene = gHdr.Load(int3(px, 0)).rgb;
    float4 g = gSurface.Load(int3(px, 0));
    float shine = g.a;
    if (shine < 0.02 || gRefl.x < 0.5) return small ? float4(0, 0, 0, 0) : float4(scene, 1);
    float3 n = normalize(g.xyz * 2 - 1);
    float3 dir = normalize(gCamF.xyz + gCamR.xyz * (i.ndc.x * gProj.x * gProj.y) + gCamU.xyz * (i.ndc.y * gProj.x));
    float zb = gSceneDepth.Load(int3(px, 0));
    float dist = linDepth(zb) / max(dot(dir, gCamF.xyz), 1e-3);
    float3 P = dir * dist;                                   // the surface, relative to the camera
    float3 r = reflect(dir, n);
    bool mirror = shine > 0.999;
    sReach = gRefl.z * lerp(0.3, 1, shine * shine);   // (rough: blurred anyway, so near things are what shows - the rays stop sooner)

    float2 huv = 0; float travelled = 0;
    bool rtOn = (((uint)gVoxM.w) & 3u) == 3u;   // ray traced reflections: the blocks first; the screen only where they find nothing (models, enemies...)
    bool hit = rtOn ? false : traceScreen(P, n, r, dist, i.pos.xy, huv, travelled);

    // where the ray finds nothing on screen: the sky (only if this spot is under the open sky, else a dim guess)
    float open = skyExposed(P + n * 0.3);
    if (gSkyVolO.w > 0.5) open = max(open, saturate(skyAt(P + n * 0.5) * 1.3));                       // block by block: a slope or slab is not a roof over itself
    else if (abs(n.y) < 0.9) open = max(open, skyExposed(P + n * 0.3 + float3(0, 1.05, 0)));          // (no sky light volume: look just above it)
    float3 fallback = (skyColor(r) * lerp(0.15, 1, open * saturate(r.y * 4 + 0.6)) + gAmbient.rgb * 0.15 * (1 - open)) * skyVis(P, n);
    float edge = 0;
    if (hit) { float2 e = min(huv, 1 - huv); edge = saturate(min(e.x, e.y) * 12) * saturate(1 - travelled / reflReach()); }   // fade near the picture's edges
    float3 cooked; bool inRoom = cookProbe(P + n * 0.05, r, saturate(1 - shine), cooked);   // the cooked world: this room's reflection picture
    float farK = inRoom ? saturate((dist - gCook.z) / max(gCook.w - gCook.z, 1)) : 0;          // far away it takes over from the rays
    if (inRoom) fallback = cooked;
    if (edge < 0.99 && farK < 0.999)   // off screen / behind you: the blocks themselves
    {
        // the surface's real (flat) direction from the depth next to it: bumps and slopes must not start the ray inside the block
        int2 pr = min(px + int2(1, 0), int2(gDepth.zw) - 1), pd = min(px + int2(0, 1), int2(gDepth.zw) - 1);
        float2 nr = (float2(pr) + 0.5) / gDepth.zw * float2(2, -2) + float2(-1, 1), nd = (float2(pd) + 0.5) / gDepth.zw * float2(2, -2) + float2(-1, 1);
        float3 dr = normalize(gCamF.xyz + gCamR.xyz * (nr.x * gProj.x * gProj.y) + gCamU.xyz * (nr.y * gProj.x)), dd = normalize(gCamF.xyz + gCamR.xyz * (nd.x * gProj.x * gProj.y) + gCamU.xyz * (nd.y * gProj.x));
        float3 Pr = dr * linDepth(gSceneDepth.Load(int3(pr, 0))) / max(dot(dr, gCamF.xyz), 1e-3), Pd = dd * linDepth(gSceneDepth.Load(int3(pd, 0))) / max(dot(dd, gCamF.xyz), 1e-3);
        float3 ng = cross(Pd - P, Pr - P); ng = dot(ng, ng) > 1e-12 ? normalize(ng) : n;
        if (dot(ng, -dir) < 0) ng = -ng;
        if (dot(ng, n) < 0.3 || length(Pr - P) > dist * 0.05 || length(Pd - P) > dist * 0.05) ng = n;   // (an edge next to it: trust the surface's own)
        float3 rv = r; float rn = dot(rv, ng); if (rn < 0.03) rv = normalize(rv + ng * (0.03 - rn));   // never into the surface
        float3 vc; if (voxelTrace(P + ng * 0.03, ng, rv, i.pos.xy, vc)) { fallback = lerp(vc, fallback, farK); rtOn = false; hit = false; }
        else if (rtOn) { hit = traceScreen(P, n, r, dist, i.pos.xy, huv, travelled); if (hit) { float2 e = min(huv, 1 - huv); edge = saturate(min(e.x, e.y) * 12) * saturate(1 - travelled / reflReach()); } }
    }
    float3 refl = fallback;
    float2 suv = (float2(px) + 0.5) / gDepth.zw;
    if ((mirror || gPlanar.x > 0.5) && gRefl.w > 0.5 && abs(dot(P, gMirror.xyz) + gMirror.w) < 0.08 && dot(n, gMirror.xyz) > 0.9)
    {   // a mirror, or the shiny floor drawn as its own reflected view: everything, even animated things and what is behind you
        float2 wob = mirror ? 0 : (n - gMirror.xyz).xz * float2(0.06, -0.06);   // bumps and ripples bend it a little
        refl = gMirrorView.SampleLevel(gClamp, saturate(suv + wob), 0).rgb;
        hit = false;
    }
    else if (hit) refl = lerp(fallback, gHdr.SampleLevel(gClamp, huv, 0).rgb, edge);
    float NdotV = saturate(dot(n, -dir));
    float f0 = mirror ? 0.9 : 0.02 + 0.6 * shine;
    float F = f0 + (1 - f0) * pow(1 - NdotV, 5);
    float amount = mirror ? 1 : saturate(shine * 1.2);
    if (small) return float4(refl * F * amount, F * amount * 0.5);
    return float4(scene * (1 - F * amount * 0.5) + refl * F * amount, 1);
}
// the smaller reflections laid over the full picture (only on shiny pixels)
Texture2D gReflLow : register(t34);
float4 PS_ReflComp(SkyOut i) : SV_Target
{
    int2 px = int2(i.pos.xy);
    float3 scene = gHdr.Load(int3(px, 0)).rgb;
    if (gSurface.Load(int3(px, 0)).a < 0.02 || gRefl.x < 0.5) return float4(scene, 1);
    float2 uv = (float2(px) + 0.5) / gDepth.zw;
    float4 r = gReflLow.SampleLevel(gClamp, uv, 0);
    float rough = saturate(gAlbedo.Load(int3(px, 0)).a);
    if (rough > 0.02 && r.a > 0.001)   // rough: the reflection blurred (a disc of samples, only from shiny spots), as blurry as the surface is rough
    {
        float rad = rough * rough * 0.05 + rough * 0.012; float3 s = r.rgb; float wa = r.a;
        [unroll] for (int k = 0; k < 12; k++)
        {
            float ang = k * 2.39996 + frac(52.9829189 * frac(dot(i.pos.xy, float2(0.06711056, 0.00583715)))) * 6.2832, dk = sqrt((k + 0.5) / 12);
            float4 q = gReflLow.SampleLevel(gClamp, uv + float2(cos(ang), sin(ang) * gDepth.z / gDepth.w) * rad * dk, 0);
            s += q.rgb; wa += q.a;
        }
        float3 refl = s / max(wa * 2, 1e-4);   // (rgb is reflection x amount, a half the amount: the reflection itself, averaged)
        float a = r.a * (1 - rough * 0.35);    // rough surfaces show their reflection a little less
        return float4(hdrSafe(scene * (1 - a) + refl * a * 2), 1);
    }
    return float4(hdrSafe(scene * (1 - r.a) + r.rgb), 1);
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
        [unroll] for (int k = 0; k < 8; k++)   // shots and splashes: rings spreading from where they hit
        {
            float4 sp = gSplash[k]; if (sp.w < 0 || sp.w > 2.5 || abs(i.rel.y - sp.y) > 0.8) continue;
            float2 d = i.rel.xz - sp.xz; float r = length(d), front0 = sp.w * 2.6;
            float ring = sin((r - front0) * 9) * exp(-pow((r - front0) * 1.6, 2)) * (1 - sp.w / 2.5) * 0.55;
            slope += d / max(r, 1e-3) * ring;
        }
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
    float3 vc; bool vhit = front && gRefl.x > 0.5 && voxelTrace(i.rel, n, r, i.pos.xy, vc);   // the blocks around (ray traced reflections on), off screen too
    if (vhit) refl = vc;
    bool ssr = front && gRefl.x > 0.5 && !vhit && traceScreen(i.rel, n, r, dist, i.pos.xy, huv, travelled);   // the screen only where the blocks found nothing
    float wedge = 0; if (ssr) { float2 e = min(huv, 1 - huv); wedge = saturate(min(e.x, e.y) * 12) * saturate(1 - travelled / gRefl.z); }
    if (ssr) refl = lerp(refl, gHdr.SampleLevel(gClamp, huv, 0).rgb, wedge);
    if (front && gRefl.w > 0.5 && gPlanar.x > 0.5 && abs(dot(i.rel, gMirror.xyz) + gMirror.w) < 0.2 && gMirror.y > 0.9)   // this water is the reflected floor: everything mirrored
    {
        float2 suvw = (i.pos.xy) / gDepth.zw + (n.xz - gMirror.xz) * float2(0.05, -0.05);
        refl = gMirrorView.SampleLevel(gClamp, saturate(suvw), 0).rgb;
    }
    float NdotV = saturate(abs(dot(n, v)));
    float F = front ? 0.02 + 0.98 * pow(1 - NdotV, 5) : 0.08;
    float3 c = lerp(refr, refl, F);
    if (front) c += gSunColor.rgb * pow(saturate(dot(r, gSunDir.xyz)), 600) * 12 * shadowAt(i.rel, float3(0, 1, 0), 1, i.pos.xy);   // sun glint
    if (front && gWater2.x > 0.5 && i.n.y > 0.5)   // the shore: foam where it gets shallow, lapping in and out; the very edge fades into what is under it
    {
        float2 fp = wp.xz; if (gAA.w > 0.5) fp = (floor(fp * gAA.w) + 0.5) / gAA.w;   // (pixelated shadows: the foam in the same pixel grid)
        float shore = saturate(1 - thick / 0.9);
        float lap = 0.5 + 0.5 * sin(thick * 7 - t * 1.6 + causticCells(fp * 0.5, t * 0.3) * 5);
        float bub = causticCells(fp * 2.3, t * 0.5);                                   // little cells: the foam's bubbles
        float fm = saturate(shore * shore * (0.5 + 0.65 * lap) - (0.32 - bub) * 1.3);
        fm = smoothstep(0.12, 0.5, fm) * saturate(1 - dist / 80);
        float3 foamLit = gSunColor.rgb * saturate(gSunDir.y) * shadowAt(i.rel, float3(0, 1, 0), saturate(gSunDir.y), i.pos.xy) + gAmbient.rgb * wsv;
        c = lerp(c, foamLit * 0.8, fm * 0.85);
        c = lerp(behind, c, saturate(thick * 6));
    }
    float f = saturate((dist - gFogRange.x) / max(gFogRange.y - gFogRange.x, 1));
    return float4(hdrSafe(lerp(c, skyColor(dir), f * f)), 1);
}

// ---------------------------------------------------------------- volumetrics: clouds and ground fog over the picture
float ign(float2 px) { return frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715)))); }   // even per-pixel noise: no banding

// the helmet's and the arm cannon's depth copied into the world's depth without the water (so effects drawn against it stay behind them)
// hiding what is behind other things: the depth made small (160 x 90), each cell the farthest depth under it. The processor reads it a
// couple of frames later: a chunk whose nearest point is behind the farthest depth everywhere it would cover can't be seen
float PS_HiZ(SkyOut i) : SV_Target
{
    float2 s = gDepth.zw / float2(160, 90);
    int2 o = int2(i.pos.xy), a = int2(floor(o * s)), b = min(int2(ceil((o + 1) * s)), int2(gDepth.zw));
    float m = 0;
    [loop] for (int y = a.y; y < b.y; y++) [loop] for (int x = a.x; x < b.x; x++) m = max(m, gSceneDepth.Load(int3(x, y, 0)));
    return m;
}
void PS_HudDepth(SkyOut i, out float depth : SV_Depth)
{
    float z = gSceneDepth.Load(int3(i.pos.xy, 0));
    if (z > 0.0021) discard;
    depth = z;
}
// ---- the fog grid (smooth fog): a small 3D grid that follows the camera (160 x 90 cells across the screen, 64 deep, finer near you).
// Each frame every cell looks at one spot inside it (a different one each frame) and blends that with what it knew (the cell it was
// in the frame before), then each column is added up front to back. The picture just looks it up at its distance.
// (its numbers: in CookCB, b13)
Texture3D gFogGrid : register(t49);   // added up: rgb what the air adds up to this depth, a what it lets through
Texture3D gFogHist : register(t50);   // the frame before's cells: rgb light given off, a how thick
RWTexture3D<float4> gFogCellW : register(u0);
RWTexture3D<float4> gFogSumW : register(u1);
float fogT(float z) { float k = z / gFogG.z; return gFogR.x * k * k; }            // a slice's start (blocks from the camera)
float fogZ(float t) { return sqrt(saturate(t / gFogR.x)) * gFogG.z; }            // ... and back
float3 fogDir(float2 cell)   // the view ray through this spot of the grid (cell coordinates)
{
    float2 ndc = cell / gFogG.xy * float2(2, -2) + float2(-1, 1);
    return normalize(gCamF.xyz + gCamR.xyz * (ndc.x * gProj.x * gProj.y) + gCamU.xyz * (ndc.y * gProj.x));
}
float4 fogCell(float3 rel, float3 dir, float t)   // what one spot of the air gives off (rgb, per block) and how thick it is (a)
{
    float y = gCamPos.y + rel.y;
    float3 E = 0; float sigma = 0;
    if ((gVolume.y > 0 || gEnv.w > 0 || gLookFog.x > 0) && t < 192)
    {
        float dl = lookFogAt(y);
        float d = gVolume.y * exp(-max(y - 1, 0) / 7) + gVolume.y * 0.06 + gEnv.w + dl;
        float open = gOcc.x > 0.5 ? skyAt(rel) : 1;
        float sun = open;
        if (gSunDir.w > 0.5)
        {
            float4 sp = mul(float4(rel, 1), gCascVP[3]);
            float2 uv = float2(sp.x, -sp.y) * 0.5 + 0.5;
            if (all(uv > 0) && all(uv < 1) && sp.z < 1) sun = gShadow.SampleCmpLevelZero(gShadowSamp, float3(uv, 3), sp.z - 0.001);
        }
        float mu = dot(dir, gSunDir.xyz);
        float phase = lerp(phaseHG(mu, 0.55), phaseHG(mu, 0), 0.4) * 12.566;
        float3 amb = gAmbient.rgb * 0.55 * (1 - gWeather.z * 0.4);
        float3 tint = lerp(float3(1, 1, 1), gLookFogCol.rgb * 1.6, dl / max(d, 1e-4));
        E += ((gSunColor.rgb * sun * phase * 0.18 * gOcc.z + amb * open) * tint) * d + gLookFogCol.rgb * gLookFogCol.w * dl;
        sigma += d;
    }
    E += lavaAir(rel) * (0.02 + sigma * 2);   // hot air over lava glows
    { float4 af = ambFogAt(rel); if (af.a > 0.001) { float d2 = af.a * 0.05; E += af.rgb * d2; sigma += d2; } }   // an ambient light's room: its own fog
    if (gLookFog.w > 0.001 && gCl0.x > 0.5 && t < gFogR.w)   // lights glowing in the air
    {
        float dens = 0.012 + gVolume.y * exp(-max(y - 1, 0) / 7) * 0.5 + gEnv.w + lookFogAt(y) * 2;
        uint2 cl = gClusters[clusterOf(rel)]; float3 Ls = 0;
        [loop] for (uint j = 0; j < min(cl.y, 12u); j++)
        {
            LightData L = gLightBuf[gClusterIdx[cl.x + j]];
            int kind = (int)round(L.col.w); if (kind >= 32) kind -= 32; if (kind >= 16) kind -= 16; if (kind >= 8) continue; int type = kind & 7;
            float3 lp = L.pos.xyz;
            if (type == 2) { float3 a = lp - L.ext.xyz, ab = L.ext.xyz * 2; lp = a + ab * saturate(dot(rel - a, ab) / max(dot(ab, ab), 1e-4)); }
            if (type == 3) lp = clamp(rel, lp - L.ext.xyz, lp + L.ext.xyz);
            float3 Lv = lp - rel; float d = length(Lv); if (d >= L.pos.w) continue;
            float att = 1 - d / L.pos.w; att *= att; att /= 1 + d * d * 0.15;
            if (type == 1) { float c = dot(-Lv / max(d, 1e-4), L.dir.xyz), c0 = L.dir.w; att *= smoothstep(c0, c0 + (1 - c0) * 0.35, c) * 2.5; }
            if (att < 0.02) continue;
            if (L.ext.w >= 0) att *= lightShadow(L, rel, type);
            Ls += L.col.rgb * att;
        }
        E += Ls * gLookFog.w * 0.8 * dens;
    }
    return float4(E, sigma);
}
void PS_FogGrid(SkyOut i)   // one column of the grid per pixel (the grid's size as the picture)
{
    uint2 xy = uint2(i.pos.xy);
    float2 cell = float2(xy) + gFogJ.xy;
    float3 dir = fogDir(cell);
    float3 S = 0; float T = 1;
    int D = (int)gFogG.z;
    [loop] for (int z = 0; z < D; z++)
    {
        float t0 = fogT(z), t1 = fogT(z + 1), t = lerp(t0, t1, gFogR.y);
        float3 rel = dir * t;
        float4 c = fogCell(rel, dir, t);
        if (gFogR.z > 0)   // what this spot was the frame before: where it was on that frame's grid
        {
            float3 relP = rel + gHistCam.xyz;
            float4 h = mul(float4(relP, 1), gHistVP);
            if (h.w > 1e-3)
            {
                float2 uv = float2(h.x, -h.y) / h.w * 0.5 + 0.5;
                float w = fogZ(length(relP)) / gFogG.z;
                if (all(uv > 0) && all(uv < 1) && w < 1)
                {
                    float4 old = gFogHist.SampleLevel(gClamp, float3(uv, w), 0);
                    c = lerp(c, old, gFogR.z);
                }
            }
        }
        c = max(c, 0);
        gFogCellW[uint3(xy, z)] = c;
        float ds = t1 - t0, a = exp(-c.a * ds);
        S += T * (c.a > 1e-5 ? c.rgb / c.a * (1 - a) : c.rgb * ds); T *= a;
        gFogSumW[uint3(xy, z)] = float4(hdrSafe(S), T);
    }
}
float4 fogGridAt(float2 ndc, float dist)   // the air between the camera and this far along the pixel's ray: rgb added, a let through
{
    float zc = fogZ(min(dist, gFogR.x));
    float2 uv = ndc * float2(0.5, -0.5) + 0.5;
    float4 g = gFogGrid.SampleLevel(gClamp, float3(uv, (zc - 0.5) / gFogG.z), 0);   // (a slice holds what is added up to its far end)
    float k = saturate(zc);
    return lerp(float4(0, 0, 0, 1), g, k);
}
float4 PS_VolumeLow(SkyOut i) : SV_Target   // a smaller picture: what the air adds (rgb) and lets through (a)
{
    int2 px = min(int2((i.ndc * float2(0.5, -0.5) + 0.5) * gDepth.zw), int2(gDepth.zw) - 1);   // (the full-size pixel this one stands for)
    float3 dir = normalize(gCamF.xyz + gCamR.xyz * (i.ndc.x * gProj.x * gProj.y) + gCamU.xyz * (i.ndc.y * gProj.x));
    float3 sAll = 0; float tAll = 1;
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
                float od = 0;   // how much cloud is between this point and the sun (far into the march, where little still shows: fewer looks)
                int taps = k < 10 ? 4 : 2;
                [loop] for (int l = 1; l <= taps; l++) od += cloudDensity(p + gSunDir.xyz * (l * l * 6.0), false) * (2 * l - 1) * 6.0;
                if (taps == 2) od *= 2.2;   // (roughly what the two far looks would have added)
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
            sAll = sAll * T + S; tAll *= T;
        }
    }

    // ---- ground fog: thick near the ground, lit by the sun where the shadow map says the sun reaches (light shafts) (the fog grid: in PS_Volume)
    if (gFogG.w < 0.5 && (gVolume.y > 0 || gEnv.w > 0 || gLookFog.x > 0))
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
            float open = gOcc.x > 0.5 ? skyAt(rel) : 1;   // true darkness: fog under a roof gets no sky light
            float sun = open;
            if (gSunDir.w > 0.5)
            {
                float4 s = mul(float4(rel, 1), gCascVP[3]);                 // the light shafts: the widest map is plenty
                float2 uv = float2(s.x, -s.y) * 0.5 + 0.5;
                if (all(uv > 0) && all(uv < 1) && s.z < 1) sun = gShadow.SampleCmpLevelZero(gShadowSamp, float3(uv, 3), s.z - 0.001);
            }
            float a = exp(-d * dt);
            float3 tint = lerp(float3(1, 1, 1), gLookFogCol.rgb * 1.6, dl / max(d, 1e-4));   // the look's fog in its own colour
            S += T * ((gSunColor.rgb * sun * phase * 0.18 * gOcc.z + amb * open) * tint + gLookFogCol.rgb * gLookFogCol.w * dl / max(d, 1e-4)) * (1 - a); T *= a;
        }
        sAll = sAll * T + S; tAll *= T;
    }
    // ---- lights in the air: every light near you glows in the fog and haze (spotlights make beams, with their shadows)
    if (gLookFog.w > 0.001 && gCl0.x > 0.5)   // (the fog grid on: its lookFog.w is 0 for this pass - a check here makes the shader compiler overflow its stack)
    {
        float tMax = min(dist, 56);
        const int NL = 16;   // (16 steps with a moving jitter: the upscaler / smoothing evens them out)
        float dt = tMax / NL, T = 1;
        float3 S = 0;
        [loop] for (int k = 0; k < NL; k++)
        {
            float t = (k + jitter) * dt;
            float3 rel = dir * t;
            float y = cam.y + rel.y;
            float dens = 0.012 + gVolume.y * exp(-max(y - 1, 0) / 7) * 0.5 + gEnv.w + lookFogAt(y) * 2;   // even clear air glows a little around lights
            uint2 cl = gClusters[clusterOf(rel)]; float3 Ls = 0;
            [loop] for (uint j = 0; j < min(cl.y, 12u); j++)
            {
                LightData L = gLightBuf[gClusterIdx[cl.x + j]];
                int kind = (int)round(L.col.w); if (kind >= 32) kind -= 32; if (kind >= 16) kind -= 16; if (kind >= 8) continue; int type = kind & 7;
                float3 lp = L.pos.xyz;
                if (type == 2) { float3 a = lp - L.ext.xyz, ab = L.ext.xyz * 2; lp = a + ab * saturate(dot(rel - a, ab) / max(dot(ab, ab), 1e-4)); }
                if (type == 3) lp = clamp(rel, lp - L.ext.xyz, lp + L.ext.xyz);   // a glowing box: its nearest point
                float3 Lv = lp - rel; float d = length(Lv); if (d >= L.pos.w) continue;
                float att = 1 - d / L.pos.w; att *= att; att /= 1 + d * d * 0.15;   // brighter right at the light
                if (type == 1) { float c = dot(-Lv / max(d, 1e-4), L.dir.xyz), c0 = L.dir.w; att *= smoothstep(c0, c0 + (1 - c0) * 0.35, c) * 2.5; }
                if (att <= 0) continue;
                if (att < 0.02) continue;   // (a faint glow: not worth its shadow lookup)
                if (L.ext.w >= 0) att *= lightShadow(L, rel, type);
                Ls += L.col.rgb * att;
            }
            float a = exp(-dens * dt);
            S += T * Ls * gLookFog.w * (1 - a) * 0.8; T *= a;
        }
        sAll += S;
    }
    // ---- under water: beams of sunlight coming down through the waves (walls and overhangs cut them off)
    if (gWater.w > 0.5 && gEnvPart.z > 0.5 && gEnvPart.z < 1.5 && gSunDir.w > 0.5 && gSunDir.y > 0.02)
    {
        float tMax = min(dist, 40);
        const int N = 16;
        float dt = tMax / N; float S = 0;
        float3 L = refractedSun(); float tm = gVolume.z;
        bool has0; float top0 = waterTop(float3(0, 0.2, 0), has0);
        [loop] for (int k = 0; k < N; k++)
        {
            float s = (k + jitter) * dt; float3 rel = dir * s;
            bool h; float top = waterTop(rel, h); if (!h) top = has0 ? top0 : rel.y + 3;
            float depth = top - rel.y; if (depth < 0) continue;
            int cs = s < gCascR.y * 0.9 ? 1 : 3;
            float4 sp = mul(float4(rel, 1), gCascVP[cs]); float2 uv = float2(sp.x, -sp.y) * 0.5 + 0.5;
            float sun = (all(uv > 0) && all(uv < 1) && sp.z < 1) ? gShadow.SampleCmpLevelZero(gShadowSamp, float3(uv, cs), sp.z - 0.002) : 1;
            float2 p = (gCamPos.xz + rel.xz + L.xz / L.y * depth) * 0.32;              // broad beams: the same light pattern, much bigger
            float beam = pow(saturate(1 - causticCells(p, tm * 0.4) * 1.7), 2.5);
            S += sun * beam * exp(-s * 0.1 - depth * 0.07) * dt;
        }
        sAll += S * gSunColor.rgb * float3(0.35, 0.85, 0.95) * phaseHG(dot(dir, L), 0.55) * 12.566 * 0.045 * gOcc.z * cloudShadow(float3(0, 0, 0));
    }
    return float4(sAll, tAll);
}

Texture2D gVolLow : register(t41);
float4 PS_Volume(SkyOut i) : SV_Target   // full size: the picture seen through the air (the smaller picture brought back up), heat shimmer, murky water
{
    int2 px = int2(i.pos.xy);
    float3 dir = normalize(gCamF.xyz + gCamR.xyz * (i.ndc.x * gProj.x * gProj.y) + gCamU.xyz * (i.ndc.y * gProj.x));
    if (gEnv.y > 0 || gLavaM.w > 0)   // heat shimmer: wobble far, low parts of the picture (and just over lava)
    {
        float z0 = gSceneDepth.Load(int3(px, 0));
        float d0 = z0 >= 1 ? 400 : gDepth.x * gDepth.y / (gDepth.y - z0 * (gDepth.y - gDepth.x));
        float k = gEnv.y * saturate((d0 - 12) / 50) * saturate(1 - abs(dir.y) * 6);
        float above; float cov = lavaAt(dir * (d0 / max(dot(dir, gCamF.xyz), 1e-3)), above);
        k += cov * saturate(1 - above / 5) * saturate(above + 1) * saturate(1 - d0 / 70) * 0.7;
        float2 w = float2(sin(i.pos.y * 0.21 + gVolume.z * 7 + i.pos.x * 0.04), cos(i.pos.x * 0.17 + gVolume.z * 5.3 + i.pos.y * 0.09));
        px = clamp(px + int2(round(w * k * 2.2)), int2(0, 0), int2(gDepth.zw) - 1);
    }
    float3 scene = gHdr.Load(int3(px, 0)).rgb;
    float zb = gSceneDepth.Load(int3(px, 0));
    float dist = zb >= 1 ? 1e9 : gDepth.x * gDepth.y / (gDepth.y - zb * (gDepth.y - gDepth.x)) / max(dot(dir, gCamF.xyz), 1e-3);
    float lz = zb >= 1 ? 1e5 : linDepth(zb);
    float4 air = upLow(gVolLow, px, lz);
    scene = scene * air.a + (gEnvPart.z > 0.5 ? 0 : air.rgb);
    if (gFogG.w > 0.5 && gEnvPart.z < 0.5) { float4 fg = fogGridAt(i.ndc, dist); scene = scene * fg.a + fg.rgb; }   // the fog grid (smooth fog): fog, light shafts and glowing lights, already added up
    // under water / in lava: the view gets murky (and then what the water adds on top: the light shafts)
    if (gEnvPart.z > 0.5)
    {

        float d = min(dist, 400);
        if (gEnvPart.z < 1.5)
        {
            scene *= exp(-min(d, 60) * float3(0.30, 0.06, 0.04));                                  // under water: red fades first
            scene = lerp(scene, (gAmbient.rgb * 0.7 + gSunColor.rgb * 0.12) * float3(0.05, 0.30, 0.36), 1 - exp(-d * 0.16));
        }
        else scene = lerp(scene, float3(1.6, 0.45, 0.08), 1 - exp(-d * 1.4));
        scene += air.rgb;
    }
    return float4(hdrSafe(scene), 1);
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
    return float4(hdrSafe(r, 64), 1);   // no single fireflies (and a broken pixel never glows: NaN would have become 64)
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
    float4 gLkAdapt;   // x how much the eye adapts, y seconds since last frame, z how fast, w how many stops it may brighten (dark nights: less)
    float4 gDeath;     // dying: x static in the visor, y the suit shutting down (dark, grey), z the picture switching off (0..1), w the clock
    float4 gDeathCol;  // rgb the static's colour, w black
    float4 gFlare;     // the sun's lens flare: xy where the sun is on screen (-1..1), z how strong (0: none)
    float4 gFlareCol;  // its colour
};
Texture2D gExposure : register(t16);   // 1x1: the eye's adaptation (stops), see PS_Adapt
Texture2D gLumTex : register(t17);     // log brightness of the picture, with mips (PS_LumLog)
// drops on the visor: little lenses that show the view upside down, drying off one by one (wet 0..1), and rain's own (coming and going)
void visorDrops(float2 pos, out float2 off, out float rim)
{
    off = 0; rim = 0;
    float sz = gDepth.w, t = gVolume.z;
    [unroll] for (int L = 0; L < 3; L++)
    {
        float amount = L < 2 ? gWater2.y * (L == 0 ? 0.6 : 0.85) : gWater2.w * 0.4;
        if (amount < 0.003) continue;
        float cells = L == 0 ? 6 : L == 1 ? 11 : 9;
        float2 p = pos / sz * cells, ci = floor(p), f = frac(p);
        float gen = L == 2 ? floor(t * 0.3 + cauHash(ci).x * 5) : 0;               // rain: a cell's drop comes and goes
        float2 h = cauHash(ci + L * 17.3 + gen * 3.1); float h3 = cauHash(ci.yx + 3.7 + L + gen).x;
        if (h.x > amount) continue;                                                // (fewer and fewer as they dry)
        float2 c = 0.5 + (h - 0.5) * 0.55;
        float r = (0.14 + 0.2 * h3) * (L == 1 ? 0.7 : 1) * (L < 2 ? 0.55 + 0.45 * saturate(gWater2.y * 1.5) : 0.8);
        float2 d = (f - c) / r; d.y *= 0.85;
        float k = dot(d, d);
        if (k < 1) { float z = sqrt(1 - k); off -= d * (1 - z * 0.35) * r / cells * sz * 0.9; rim = max(rim, smoothstep(0.5, 1, k)); }
    }
}
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
    float3 c = hdrSafe(gHdr.Load(int3(px, 0)).rgb);
    c = lerp(c, hdrSafe(gBloom.SampleLevel(gClamp, uv, 0).rgb) / 6, gExtra.y);              // soft glow around bright things (6 levels added up)
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
    float target = clamp(log2(0.18) - avg, -2, max(gLkAdapt.w, 0));
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
// dying: static, the suit shutting down, black (applied last, before gamma)
float3 deathFx(float3 c, int2 px, float boost)
{
    const float3 W = float3(0.2126, 0.7152, 0.0722); float t = gDeath.w;
    float a = gDeath.x;
    if (a > 0.001)
    {
        float n = postHash(float2(px) * 0.73 + frac(t * 13.7) * 517);
        c = lerp(c, gDeathCol.rgb * n * 1.4, saturate(a * 0.55));                     // noise in the static's colour
        c *= 1 - a * 0.35 * (float)(px.y & 1);                                          // scanlines
        c *= 1 - a * 0.25 * postHash(float2(floor(t * 30), 7));                         // the whole picture flickering
    }
    float sd = gDeath.y;
    if (sd > 0.001) { float l = dot(c, W); c = lerp(c, l.xxx, sd) * (1 - 0.92 * sd); }   // the colour drains away, it goes dark
    return c * (1 - gDeathCol.w) * boost;
}
float4 PS_Post(SkyOut i) : SV_Target
{
    float tvBoost = 1;
    if (gDeath.z > 0.001)   // an old TV switching off: the picture squeezes into a bright line, the line into a dot, then black
    {
        float p = gDeath.z;
        if (p >= 0.6)
        {
            float q = saturate((p - 0.6) / 0.4), w = lerp(0.7, 0.0, q * q), lh = 3 / max(gDepth.w, 1);
            float k = saturate(1 - abs(i.ndc.y) / lh) * saturate(1 - abs(i.ndc.x) / max(w, 1e-4)) * (1 - q * 0.6);
            return float4(k.xxx * (1 - gDeathCol.w * 0), 1);
        }
        float band = lerp(1, 0.004, smoothstep(0, 1, p / 0.6));
        if (abs(i.ndc.y) > band) return float4(0, 0, 0, 1);
        i.ndc.y /= band; i.pos.y = (-i.ndc.y * 0.5 + 0.5) * gDepth.w;
        tvBoost = 1 + 4 * (1 - band) * (1 - band);
    }
    if (gDeath.x > 0.01)   // static: rows of the picture torn sideways
    {
        float row = floor(i.pos.y / 7), ft = floor(gDeath.w * 24);
        if (postHash(float2(row, ft)) > 1 - 0.35 * gDeath.x) { float sh = (postHash(float2(row + 3.1, ft)) - 0.5) * 90 * gDeath.x; i.pos.x = clamp(i.pos.x + sh, 0, gDepth.z - 1); i.ndc.x = i.pos.x / gDepth.z * 2 - 1; }
    }
    int2 px = int2(i.pos.xy);
    float2 uv = i.ndc * float2(0.5, -0.5) + 0.5;
    float dz = gSceneDepth.Load(int3(px, 0));
    if (dz >= gVHud.x && dz < gVHud.y) {   // the helmet's own parts: no visor look, no grade, no eye adaptation on them (the arm cannon gets it)
        float3 h = postBase(px, uv, 1);
        float2 v0 = i.ndc; h *= 1 - 0.18 * dot(v0, v0) * 0.5;
        return float4(pow(max(deathFx(h, px, tvBoost), 0), 1 / 2.2), 1);
    }
    float dropRim = 0;
    if (gWater2.y > 0.003 || gWater2.w > 0.003) { float2 o; visorDrops(i.pos.xy, o, dropRim); px = clamp(px + int2(round(o)), int2(0, 0), int2(gDepth.zw) - 1); uv = (float2(px) + 0.5) / gDepth.zw; }
    float expo = lookExposure();
    float3 c = postBase(px, uv, expo);
    c *= 1 - dropRim * 0.35;   // a drop's dark rim
    {   // colour fringes: red and blue a little apart - the visor's (sideways) and the lens's (toward the edges)
        float2 rad = i.ndc * float2(1, -1) * gLk1.y * 0.004 * gDepth.z;
        float2 of = float2(gVMisc.x, 0) + rad;
        if (dot(of, of) > 0.25) {
            int2 o = int2(round(of)); float2 ou = of / max(gDepth.zw, 1);
            c.r = postBase(px + o, uv + ou, expo).r; c.b = postBase(px - o, uv - ou, expo).b;
        }
    }
    c = lookGrade(c);
    if (gFlare.z > 0.001)   // lens flare: a glow and a streak around the sun, rings along the line through the middle - only if nothing is in front of the sun
    {
        float2 sp = gFlare.xy, asp = float2(gDepth.z / max(gDepth.w, 1), 1);
        int2 spx = int2((sp * float2(0.5, -0.5) + 0.5) * gDepth.zw); float vis = 0;
        [unroll] for (int y = -2; y <= 2; y++) [unroll] for (int x = -2; x <= 2; x++)
        {
            int2 q = spx + int2(x, y) * 7;
            vis += (all(q >= 0) && all(q < int2(gDepth.zw))) ? (gSceneDepth.Load(int3(q, 0)) >= 0.99999 ? 1 : 0) : 1;   // (off screen: the CPU's look toward the sun, in gFlare.z, decides)
        }
        vis /= 25;
        float k = gFlare.z * vis;
        if (k > 0.001)
        {
            float2 dv = (i.ndc - sp) * asp;
            float3 add = exp(-length(dv) * 5) * 0.35 + exp(-abs(dv.y) * 70) * exp(-abs(dv.x) * 2.2) * 0.14;   // glow and a horizontal streak
            const float at[6] = { 0.45, -0.15, -0.4, -0.65, -0.95, -1.3 }, rad[6] = { 0.05, 0.13, 0.035, 0.22, 0.07, 0.32 };
            const float3 tint[6] = { float3(1, 0.8, 0.5), float3(0.5, 0.8, 1), float3(1, 0.6, 0.9), float3(0.6, 1, 0.7), float3(1, 0.9, 0.6), float3(0.5, 0.6, 1) };
            [unroll] for (int g = 0; g < 6; g++)
            {
                float d = length((i.ndc - sp * at[g]) * asp);
                float disc = smoothstep(rad[g], rad[g] * 0.7, d), rim = exp(-pow((d - rad[g]) * 60, 2));
                add += tint[g] * (disc * 0.035 + rim * 0.05);
            }
            c += add * gFlareCol.rgb * k;
        }
    }
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
    return float4(pow(max(deathFx(c, px, tvBoost), 0), 1 / 2.2), 1);
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
    int fl = (int)(i.info.w + 0.5);
    bool streak = (fl & 1) != 0;   // info.w: 1 = streak, +2 = soft (smoke) instead of glowing, +4 = a ribbon's corner (already where it goes)
    if ((fl & 4) != 0) p = i.pos;
    else if (streak)
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
    o.pos = mul(float4(p, 1), gViewProj); o.uv = i.corner; o.col = i.col; o.rel = p; o.shape = i.info.z; o.seed = i.info.y; o.soft = (fl & 2) != 0 ? 1 : 0;
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
    else if (shape == 6) a = exp(-q.x * q.x * 3.2) * 0.75 + exp(-q.x * q.x * 28) * 0.6;                     // ribbon: soft edges, a bright line down its middle
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
        float tw, th, nl; gHudTex.GetDimensions(0, tw, th, nl); float m = log2(max(tw, 1) / 1024);   // (the same glow size whatever the plane's resolution)
        float3 g = gHudTex.SampleLevel(gClamp, i.uv, 2.5 + m).rgb * 0.45 + gHudTex.SampleLevel(gClamp, i.uv, 4 + m).rgb * 0.45 + gHudTex.SampleLevel(gClamp, i.uv, 5.5 + m).rgb * 0.5;
        c.rgb += g * bloom * 1.6;                                                         // light only: the see-through parts stay see-through
    }
    c.rgb *= 1 + gPrevLight.z;                                                            // a flash (morphing)
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
// ---------------------------------------------------------------- screen transitions: the old picture and the new one put together
cbuffer TransCB : register(b12)
{
    float4 gTr;      // x how far (0..1, eased), y which kind, z direction (0 left, 1 right, 2 top, 3 bottom), w 1: going out (the old one leaves toward the direction)
    float4 gTrCol;   // rgb its colour, w a clock (s)
    float4 gTrSize;  // x width / height, y 1 / width, z 1 / height, w how far without easing
};
Texture2D gTrFrom : register(t44);
Texture2D gTrTo : register(t45);
float3 trA(float2 q) { return gTrFrom.SampleLevel(gClamp, q, 0).rgb; }
float3 trB(float2 q) { return gTrTo.SampleLevel(gClamp, q, 0).rgb; }
bool trIn(float2 q) { return all(q >= 0) && all(q <= 1); }
float trHash(float2 p) { return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
float4 PS_Trans(BlitOut i) : SV_Target
{
    float2 uv = i.uv; float p = gTr.x, pl = gTrSize.w; int k = (int)round(gTr.y), d = (int)round(gTr.z); bool outw = gTr.w > 0.5;
    float2 dv = d == 0 ? float2(-1, 0) : d == 1 ? float2(1, 0) : d == 2 ? float2(0, -1) : float2(0, 1);   // where the new one comes from (going out: where the old one goes)
    float3 col = gTrCol.rgb, c = trB(uv);
    float aspect = gTrSize.x;
    switch (k)
    {
    case 1: c = p < 0.5 ? lerp(trA(uv), col, saturate(p * 2)) : lerp(col, trB(uv), saturate(p * 2 - 1)); break;
    case 2: c = lerp(trA(uv), trB(uv), saturate(p)); break;
    case 3:   // slide: the new one over the old (going out: the old one away, the new one under it)
        if (!outw) { float2 q = uv - dv * (1 - p); c = trIn(q) ? trB(q) : trA(uv) * (1 - 0.35 * p); }
        else { float2 q = uv - dv * p; c = trIn(q) ? trA(q) : trB(uv) * (0.65 + 0.35 * p); }
        break;
    case 4: { float2 qb = uv - dv * (1 - p), qa = uv + dv * p; c = trIn(qb) ? trB(qb) : trA(qa); break; }   // push: both move
    case 5: { float2 qa = 0.5 + (uv - 0.5) / (1 + p * 1.5), qb = 0.5 + (uv - 0.5) * (1 + (1 - p) * 0.25); c = lerp(trA(qa), trB(qb), smoothstep(0.15, 0.85, pl)); break; }   // zoom in
    case 6: { float2 qa = 0.5 + (uv - 0.5) * (1 + p * 2.5), qb = 0.5 + (uv - 0.5) / (1 + (1 - p) * 0.5); float a = trIn(qa) ? 1 - smoothstep(0.3, 1, pl) : 0; c = lerp(trB(qb), trA(qa), a); break; }   // zoom out
    case 7: case 9:   // wipe (a straight line, or slanted): the new one behind a glowing edge
    {
        float s = k == 9 ? (uv.x * aspect + uv.y) / (aspect + 1) : dv.x != 0 ? (dv.x > 0 ? 1 - uv.x : uv.x) : (dv.y > 0 ? 1 - uv.y : uv.y);
        if (outw) s = 1 - s;
        float e = p * 1.1 - 0.05; c = s < e ? trB(uv) : trA(uv);
        c += col * exp(-abs(s - e) * 90) * 1.2 * (1 - pl);
        break;
    }
    case 8:   // iris: a circle opens on the new one (going out: closes on the old)
    {
        float r = length((uv - 0.5) * float2(aspect, 1)), R = (outw ? 1 - p : p) * length(float2(aspect, 1)) * 0.52;
        bool inside = r < R; c = outw ? (inside ? trA(uv) : trB(uv)) : (inside ? trB(uv) : trA(uv));
        c += col * exp(-abs(r - R) * 60) * (1 - abs(pl * 2 - 1));
        break;
    }
    case 10:   // visor scan: a bright line sweeps down; above it the new picture (with scanlines that fade), below it the old
    {
        float y0 = p * 1.15 - 0.075;
        if (uv.y < y0) { c = trB(uv) * (1 - 0.25 * (1 - pl) * step(0.5, frac(uv.y / max(gTrSize.z, 1e-5) * 0.25))); }
        else c = trA(uv);
        c += col * (exp(-abs(uv.y - y0) * 120) * 1.5 + exp(-abs(uv.y - y0) * 18) * 0.25) * (1 - pl * 0.5);
        break;
    }
    case 11:   // static: blocks of the new picture appear among torn rows of noise
    {
        float t = floor(gTrCol.w * 30), w = sin(3.14159 * pl);
        float row = floor(uv.y * 90); float tear = (trHash(float2(row, t)) - 0.5) * 0.08 * w * step(0.6, trHash(float2(row + 7, t)));
        float2 q = float2(uv.x + tear, uv.y);
        float n = trHash(floor(q * float2(48, 90)) + t * 0.37);
        c = n < pl ? trB(q) : trA(q);
        float g = trHash(uv * 731 + t); c = lerp(c, g.xxx, 0.55 * w);
        break;
    }
    case 12:   // pixelate: the old one breaks into big pixels, the new one comes out of them
    {
        float px = lerp(1, 64, sin(3.14159 * saturate(pl))), sx = px * gTrSize.y, sy = px * gTrSize.z;
        float2 q = (floor(uv / float2(sx, sy)) + 0.5) * float2(sx, sy);
        c = pl < 0.5 ? trA(q) : trB(q);
        break;
    }
    case 13:   // blur: the old one blurs into the new one, which sharpens
    {
        float r = sin(3.14159 * saturate(pl)) * 14; float3 s = 0;
        [unroll] for (int j = 0; j < 12; j++) { float a = j * 2.39996, dk = sqrt((j + 0.5) / 12); float2 o = float2(cos(a), sin(a)) * dk * r * gTrSize.yz; s += lerp(trA(uv + o), trB(uv + o), smoothstep(0.3, 0.7, pl)); }
        c = s / 12;
        break;
    }
    case 14: { float w = 1 - abs(pl * 2 - 1); c = (pl < 0.5 ? trA(uv) : trB(uv)) + col * pow(w, 1.5) * 2.2; break; }   // flash
    default: break;
    }
    return float4(c, 1);
}

// ---------------------------------------------------------------- planets: stars, a coloured haze and a planet in layers (ocean, terrain bands, ice, clouds, atmosphere, night lights, rings)
cbuffer PlanetCB : register(b3)
{
    float4 gPlA;        // x width / height, y time (s), z pixels across the planet (0: smooth), w seed
    float4 gPlPos;      // xy its middle (0..1 of the picture), z its radius (part of the height), w how far it has turned (radians)
    float4 gPlSun;      // xyz towards the sun (the view: x right, y up, z towards you), w stars
    float4 gPlAxis;     // xyz the planet's axis (the view), w -
    float4 gPlNeb;      // rgb the haze, w how much
    float4 gPlOcean;    // rgb shallow water, w sea level (0..1; < 0: no ocean)
    float4 gPlDeep;     // rgb deep water, w its shine
    float4 gPlLand;     // x continent size, y roughness, z how many bands, w -
    float4 gPlBand[8];  // rgb colour, w from (height above the sea, 0..1)
    float4 gPlIce;      // rgb, w how far down from the poles (0: none)
    float4 gPlCloud;    // rgb, w cover (0: none)
    float4 gPlCloud2;   // x puffy, y how far the clouds have turned more than the ground (radians), z their shadows, w -
    float4 gPlAtmo;     // rgb, w thickness (0: none)
    float4 gPlAtmo2;    // x glow, yzw -
    float4 gPlNight;    // rgb, w amount (0: none)
    float4 gPlRing;     // rgb, w 1: rings
    float4 gPlRing2;    // rgb, w -
    float4 gPlRing3;    // x inner, y outer radius (the planet is 1), zw -
};
float plHash(float3 p) { p = frac(p * 0.3183099 + 0.1); p *= 17.0; return frac(p.x * p.y * p.z * (p.x + p.y + p.z)); }
float plNoise(float3 x)
{
    float3 i = floor(x), f = frac(x); f = f * f * (3 - 2 * f);
    return lerp(lerp(lerp(plHash(i), plHash(i + float3(1, 0, 0)), f.x), lerp(plHash(i + float3(0, 1, 0)), plHash(i + float3(1, 1, 0)), f.x), f.y),
                lerp(lerp(plHash(i + float3(0, 0, 1)), plHash(i + float3(1, 0, 1)), f.x), lerp(plHash(i + float3(0, 1, 1)), plHash(i + float3(1, 1, 1)), f.x), f.y), f.z);
}
float plFbm(float3 p, int oct, float rough)
{
    float s = 0, a = 0.5, n = 0; float g = lerp(0.38, 0.62, rough);
    [loop] for (int k = 0; k < oct; k++) { s += plNoise(p) * a; n += a; p = p * 2.03 + 11.7; a *= g; }
    return s / n;
}
float3 plRotate(float3 v, float3 axis, float ang)   // turn v around axis (Rodrigues)
{
    float c = cos(ang), s = sin(ang); return v * c + cross(axis, v) * s + axis * dot(axis, v) * (1 - c);
}
float3 plLocal(float3 n, float spin)   // a direction in the view -> on the planet's own globe (its axis is y)
{
    float3 ax = gPlAxis.xyz, up = float3(0, 1, 0);
    float3 k = cross(ax, up); float sn = length(k), c = dot(ax, up);
    float3 v = sn > 1e-4 ? plRotate(n, k / sn, atan2(sn, c)) : n;   // tip the axis upright
    return plRotate(v, up, -spin);
}
float3 plSurface(float3 loc, float3 n, float3 L, out float spec)   // the ground's colour at a point of the globe
{
    float seed = gPlA.w; spec = 0;
    float h = plFbm(loc * gPlLand.x + seed * 1.37, 6, gPlLand.y);
    h = saturate((h - 0.5) * 1.8 + 0.5);
    float3 col;
    float sea = gPlOcean.w;
    if (sea >= 0 && h < sea)
    {
        float d = saturate((sea - h) / max(sea, 1e-3) * 2.5);
        col = lerp(gPlOcean.rgb, gPlDeep.rgb, d);
        float3 r = reflect(float3(0, 0, -1), n); spec = pow(saturate(dot(r, L)), 48) * gPlDeep.w * 2.5;
    }
    else
    {
        float above = sea >= 0 ? saturate((h - sea) / max(1 - sea, 1e-3)) : h;
        above = saturate(above + (plNoise(loc * 18 + seed) - 0.5) * 0.12 * gPlLand.y);   // ragged edges between the bands
        int nb = (int)round(gPlLand.z); col = nb > 0 ? gPlBand[0].rgb : float3(0.4, 0.5, 0.3);
        [loop] for (int b = 1; b < nb; b++) { float e = gPlBand[b].w; col = lerp(col, gPlBand[b].rgb, smoothstep(e - 0.02, e + 0.02, above)); }
        col *= 0.85 + 0.3 * plNoise(loc * 40 + seed * 3.1);   // a little texture
    }
    if (gPlIce.w > 0)
    {
        float lat = abs(loc.y), edge = 1 - gPlIce.w + (plNoise(loc * 9 + seed) - 0.5) * 0.12;
        col = lerp(col, gPlIce.rgb, smoothstep(edge - 0.015, edge + 0.015, lat)); if (lat > edge) spec *= 0.2;
    }
    return col;
}
float plClouds(float3 loc)
{
    if (gPlCloud.w <= 0) return 0;
    float3 q = plRotate(loc, float3(0, 1, 0), -gPlCloud2.y);
    float c = plFbm(q * 2.6 + gPlA.w * 3.3 + float3(0, gPlA.y * 0.004, 0), 5, 0.55);
    float t = 1 - gPlCloud.w, soft = lerp(0.03, 0.16, gPlCloud2.x);
    return smoothstep(t - soft, t + soft, c);
}
float4 PS_Planet(BlitOut i) : SV_Target
{
    float2 uv = i.uv; float aspect = gPlA.x;
    float3 col = float3(0.004, 0.006, 0.012);
    // the haze and the stars
    {
        float2 q = uv * float2(aspect, 1) * 2.2;
        float nb = plFbm(float3(q, gPlA.w * 0.1), 5, 0.6); nb = saturate((nb - 0.42) * 2.4);
        col += gPlNeb.rgb * nb * nb * gPlNeb.w * 1.6;
        float2 cell = floor(uv * float2(aspect, 1) * 420); float hs = plHash(float3(cell, 7.1));
        float lim = 1 - gPlSun.w * 0.012;
        if (hs > lim) { float tw = 0.6 + 0.4 * sin(gPlA.y * (1.5 + hs * 4) + hs * 60); col += (hs - lim) / max(1 - lim, 1e-4) * tw * float3(0.9, 0.95, 1) * 1.6; }
    }
    float R = gPlPos.z;
    float2 p = (uv - gPlPos.xy) * float2(aspect, 1) / max(R, 1e-3); p.y = -p.y;   // in planet radii, y up
    if (gPlA.z > 0.5) { float g = gPlA.z * 0.5; p = (floor(p * g) + 0.5) / g; }      // pixelated
    float r2 = dot(p, p);
    float3 L = normalize(gPlSun.xyz);
    float3 ax = gPlAxis.xyz;
    // the rings: where the view line meets their plane (behind the planet / in front)
    float4 ringC = 0; float ringZ = -10;
    if (gPlRing.w > 0.5 && abs(ax.z) > 1e-3)
    {
        float z = -(ax.x * p.x + ax.y * p.y) / ax.z; float3 x = float3(p, z); float d = length(x);
        if (d > gPlRing3.x && d < gPlRing3.y)
        {
            float t = (d - gPlRing3.x) / (gPlRing3.y - gPlRing3.x);
            float bands = 0.55 + 0.45 * sin(t * 38 + sin(t * 9) * 2) * plNoise(float3(t * 60, 1, 2));
            float a = smoothstep(0, 0.06, t) * smoothstep(1, 0.9, t) * saturate(bands + 0.2);
            float lit = 0.35 + 0.65 * saturate(abs(dot(ax, L)) + 0.25);
            float3 rc = lerp(gPlRing.rgb, gPlRing2.rgb, t) * lit;
            bool shadow = false;   // the planet's shadow on the rings
            { float b = dot(x, L), c = dot(x, x) - 1; shadow = b < 0 && b * b - c > 0; }
            if (shadow) rc *= 0.15;
            ringC = float4(rc, a * 0.85); ringZ = z;
        }
    }
    bool ringBehind = ringC.a > 0 && ringZ < 0 && r2 < 1;
    if (ringBehind) col = lerp(col, ringC.rgb, ringC.a);   // behind the planet (hidden by it below)
    // the atmosphere's glow just outside the edge
    if (gPlAtmo.w > 0 && r2 >= 1)
    {
        float rr = sqrt(r2), k = exp(-(rr - 1) / max(gPlAtmo.w, 1e-3) * 2.2);
        float side = saturate(dot(normalize(float3(p, 0)), L) * 0.7 + 0.45);
        col += gPlAtmo.rgb * k * side * gPlAtmo2.x * 0.9;
    }
    if (r2 < 1)
    {
        float3 n = float3(p, sqrt(1 - r2));
        float3 loc = plLocal(n, gPlPos.w), Lloc = plLocal(L, gPlPos.w);
        float spec; float3 ground = plSurface(loc, n, L, spec);
        float ndl = dot(n, L), day = smoothstep(-0.08, 0.25, ndl), diff = saturate(ndl) * 0.85 + 0.15 * day;
        float cl = plClouds(loc);
        float sh = gPlCloud2.z > 0 ? plClouds(normalize(loc + Lloc * 0.03)) * gPlCloud2.z : 0;   // a cloud's shadow on the ground, offset from it toward the sun
        float3 c = ground * diff * (1 - sh * 0.6) + spec * day * (1 - cl) * float3(1, 0.95, 0.85);
        if (gPlNight.w > 0)   // the dark side: lights where there is land
        {
            float sea = gPlOcean.w; float h = plFbm(loc * gPlLand.x + gPlA.w * 1.37, 6, gPlLand.y); h = saturate((h - 0.5) * 1.8 + 0.5);
            float land = sea < 0 || h > sea ? 1 : 0;
            float lights = smoothstep(0.62, 0.8, plNoise(loc * 55 + gPlA.w)) * smoothstep(0.4, 0.7, plNoise(loc * 7 + 4.1));
            c += gPlNight.rgb * lights * land * (1 - day) * gPlNight.w * 1.6 * (1 - cl * 0.7);
        }
        c = lerp(c, gPlCloud.rgb * (diff * 0.95 + 0.05), cl * 0.92);
        if (gPlAtmo.w > 0)   // the air thickens toward the edge (lit side)
        {
            float rim = pow(1 - n.z, 2.5);
            c = lerp(c, gPlAtmo.rgb * (0.25 + day), saturate(rim * gPlAtmo2.x * (0.4 + gPlAtmo.w * 3)) * (0.3 + 0.7 * day));
        }
        float edge = smoothstep(1, 0.985, sqrt(r2));   // (a smooth edge)
        col = lerp(col, c, gPlA.z > 0.5 ? 1 : edge);
    }
    if (ringC.a > 0 && !ringBehind) col = lerp(col, ringC.rgb, ringC.a);   // in front (or beside it)
    return float4(col, 1);
}

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

// ---------------------------------------------------------------- bounce light (screen-space GI): light bouncing off lit and glowing
// surfaces onto the ones near them - a green glow tints the wall beside it, a sunlit floor lights the ceiling a little
Texture2D gGITex : register(t24);
float3 relAt(int2 fp, float zb)
{
    float2 ndc = (float2(fp) + 0.5) / gDepth.zw * float2(2, -2) + float2(-1, 1);
    float3 d = normalize(gCamF.xyz + gCamR.xyz * (ndc.x * gProj.x * gProj.y) + gCamU.xyz * (ndc.y * gProj.x));
    return d * linDepth(zb) / max(dot(d, gCamF.xyz), 1e-3);
}
float4 PS_GI(SkyOut i) : SV_Target   // half size
{
    float2 uv = i.ndc * float2(0.5, -0.5) + 0.5;
    int2 fp = clamp(int2(uv * gDepth.zw), int2(0, 0), int2(gDepth.zw) - 1);
    float zb = gSceneDepth.Load(int3(fp, 0)); if (zb >= 1) return 0;
    float3 P = relAt(fp, zb); float dist = length(P);
    float3 n = normalize(gSurface.Load(int3(fp, 0)).xyz * 2 - 1);
    float rot = ign(i.pos.xy + gAA.z * float2(5.588238, 3.137)) * 6.2832;   // (a different pattern each frame: the memory averages the dots away)
    float rpx = clamp(gGI.y / max(dist, 0.5) * gDepth.w / (2 * gProj.x), 4, gDepth.w * 0.15);   // the reach, in pixels at this distance
    float3 sum = 0; const int N = 16;
    [loop] for (int k = 0; k < N; k++)
    {
        float a = rot + k * 2.39996, r = rpx * sqrt((k + 0.5) / N);
        int2 sp = fp + int2(round(float2(cos(a), sin(a)) * r));
        if (any(sp < 0) || any(sp >= int2(gDepth.zw))) continue;
        float zs = gSceneDepth.Load(int3(sp, 0)); if (zs >= 1) continue;
        float3 v = relAt(sp, zs) - P; float d = length(v); if (d < 0.05 || d > gGI.y * 2) continue; v /= d;
        float3 nS = normalize(gSurface.Load(int3(sp, 0)).xyz * 2 - 1);
        float w = saturate(dot(n, v)) * saturate(dot(nS, -v) + 0.25);   // facing each other (glowing things shine every way, a bit)
        sum += gHdr.Load(int3(sp, 0)).rgb * w / (1 + d * d / (gGI.y * gGI.y) * 4);
    }
    return float4(min(sum / N * gGI.x * 2.2, 16), 1);
}
// the memory (half size): this frame's bounce light blended into what the frames before found at the same spot (followed as the camera moves);
// a spot that was hidden before, or something else, starts again. rgb: the light, a: how far away (to check next time)
Texture2D gGINew : register(t36);
Texture2D gGIHist : register(t37);
float4 PS_GITemp(SkyOut i) : SV_Target
{
    float2 uv = i.ndc * float2(0.5, -0.5) + 0.5;
    int2 fp = clamp(int2(uv * gDepth.zw), int2(0, 0), int2(gDepth.zw) - 1);
    float zb = gSceneDepth.Load(int3(fp, 0)); if (zb >= 1) return 0;
    float lz = linDepth(zb);
    int2 hp = int2(i.pos.xy);
    float3 cur = gGINew.Load(int3(hp, 0)).rgb;
    if (gHistCam.w < 0.5) return float4(cur, lz);
    float4 c = mul(float4(relAt(fp, zb) + gHistCam.xyz, 1), gHistVP);   // where it was on the screen a frame ago
    if (c.w <= 0.05) return float4(cur, lz);
    float2 puv = c.xy / c.w * float2(0.5, -0.5) + 0.5;
    if (any(puv < 0.001) || any(puv > 0.999)) return float4(cur, lz);
    float4 h = gGIHist.SampleLevel(gClamp, puv, 0);
    if (abs(h.a - c.w) > max(c.w * 0.05, 0.12)) return float4(cur, lz);   // something else was there: start again
    // keep the memory near what is around it now (so a light that goes out fades fast, not over seconds)
    float3 lo = cur, hi = cur;
    [unroll] for (int y = -1; y <= 1; y++) [unroll] for (int x = -1; x <= 1; x++) { float3 q = gGINew.Load(int3(hp + int2(x, y), 0)).rgb; lo = min(lo, q); hi = max(hi, q); }
    float3 pad = (hi - lo) * 0.5 + 0.02;
    float3 old = clamp(h.rgb, lo - pad, hi + pad);
    return float4(lerp(old, cur, 0.1), lz);
}
float4 PS_GIApply(SkyOut i) : SV_Target   // full size: the smaller picture brought back up (only across similar distances), times the surface's colour
{
    int2 px = int2(i.pos.xy);
    float zb = gSceneDepth.Load(int3(px, 0)); if (zb >= 1) return 0;
    return float4(upLow(gGITex, px, linDepth(zb)).rgb * gAlbedo.Load(int3(px, 0)).rgb, 0);
}

// ---------------------------------------------------------------- paused / reading a scan: the world (and the arm cannon) blurred behind the menu
// t0 the sharp picture, t1 the blurred one, t2 depth; gOffset.x how much (0..1)
float4 PS_BlurComp(SkyOut i) : SV_Target
{
    int2 px = int2(i.pos.xy); float2 uv = i.ndc * float2(0.5, -0.5) + 0.5;
    float3 sharp = gHdr.Load(int3(px, 0)).rgb;
    float dz = gSceneDepth.Load(int3(px, 0));
    if (dz >= gVHud.x && dz < gVHud.y) return float4(sharp, 1);           // the helmet's own parts stay sharp
    float3 b = gBloom.SampleLevel(gClamp, uv, 0).rgb * (1 - 0.2 * gOffset.x);
    return float4(lerp(sharp, b, saturate(gOffset.x)), 1);
}

// ---------------------------------------------------------------- anti-aliasing: jagged edges in the finished picture smoothed along their direction
Texture2D gAASrc : register(t35);
float aaLuma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }
float4 PS_AA(SkyOut i) : SV_Target
{
    float2 px = 1.0 / gDepth.zw, uv = i.pos.xy * px;
    float3 c = gAASrc.SampleLevel(gClamp, uv, 0).rgb;
    float lc = aaLuma(c);
    float lnw = aaLuma(gAASrc.SampleLevel(gClamp, uv + float2(-1, -1) * px, 0).rgb), lne = aaLuma(gAASrc.SampleLevel(gClamp, uv + float2(1, -1) * px, 0).rgb);
    float lsw = aaLuma(gAASrc.SampleLevel(gClamp, uv + float2(-1, 1) * px, 0).rgb), lse = aaLuma(gAASrc.SampleLevel(gClamp, uv + float2(1, 1) * px, 0).rgb);
    float lo = min(lc, min(min(lnw, lne), min(lsw, lse))), hi = max(lc, max(max(lnw, lne), max(lsw, lse)));
    if (hi - lo < max(0.04, hi * 0.15)) return float4(c, 1);                                       // no edge here: as it is
    {   // only the edges of things (where the distance jumps) - the pixels painted inside a texture stay crisp (pixel art!)
        int2 p0 = int2(i.pos.xy), mx = int2(gDepth.zw) - 1;
        float d0 = linDepth(gSceneDepth.Load(int3(p0, 0)));
        float d1 = linDepth(gSceneDepth.Load(int3(clamp(p0 + int2(1, 1), 0, mx), 0))), d2 = linDepth(gSceneDepth.Load(int3(clamp(p0 + int2(-1, -1), 0, mx), 0)));
        float d3 = linDepth(gSceneDepth.Load(int3(clamp(p0 + int2(1, -1), 0, mx), 0))), d4 = linDepth(gSceneDepth.Load(int3(clamp(p0 + int2(-1, 1), 0, mx), 0)));
        float jump = max(max(abs(d1 - d0), abs(d2 - d0)), max(abs(d3 - d0), abs(d4 - d0)));
        if (jump < max(d0 * 0.03, 0.05)) return float4(c, 1);
    }
    // which way the edge runs: across the brightness change
    float2 d = float2((lsw + lse) - (lnw + lne), (lnw + lsw) - (lne + lse));
    float soft = max((lnw + lne + lsw + lse) * 0.03, 1.0 / 128);
    d = clamp(d / (min(abs(d.x), abs(d.y)) + soft), -gAA.x, gAA.x) * px;
    // two samples close by along it, then two further out
    float3 nearC = 0.5 * (gAASrc.SampleLevel(gClamp, uv - d / 6, 0).rgb + gAASrc.SampleLevel(gClamp, uv + d / 6, 0).rgb);
    float3 farC = nearC * 0.5 + 0.25 * (gAASrc.SampleLevel(gClamp, uv - d * 0.5, 0).rgb + gAASrc.SampleLevel(gClamp, uv + d * 0.5, 0).rgb);
    float lf = aaLuma(farC);
    float3 o = (lf < lo || lf > hi) ? nearC : farC;                                                // the far ones crossed another edge: only the near ones
    return float4(lerp(c, o, gAA.y), 1);
}

// ---------------------------------------------------------------- render scale: the world's picture fitted to the screen
// smaller than the screen: enlarged smoothly, then sharpened a little; bigger (supersampled): each screen pixel averages the pixels it covers
Texture2D gUpSrc : register(t40);
float4 PS_Upscale(SkyOut i) : SV_Target
{
    float2 uv = i.ndc * float2(0.5, -0.5) + 0.5; float sw, sh; gUpSrc.GetDimensions(sw, sh);
    float2 du = float2(abs(ddx(uv.x)), abs(ddy(uv.y)));                 // one screen pixel, in the picture's 0..1
    float3 c;
    if (du.x * sw > 1.01)   // supersampled: a small box over the screen pixel
    {
        c = 0.25 * (gUpSrc.SampleLevel(gClamp, uv + du * float2(-0.25, -0.25), 0).rgb + gUpSrc.SampleLevel(gClamp, uv + du * float2(0.25, -0.25), 0).rgb
                  + gUpSrc.SampleLevel(gClamp, uv + du * float2(-0.25, 0.25), 0).rgb + gUpSrc.SampleLevel(gClamp, uv + du * float2(0.25, 0.25), 0).rgb);
    }
    else
    {
        float2 t = 1 / float2(sw, sh); c = gUpSrc.SampleLevel(gClamp, uv, 0).rgb;
        float3 n = gUpSrc.SampleLevel(gClamp, uv + float2(t.x, 0), 0).rgb + gUpSrc.SampleLevel(gClamp, uv - float2(t.x, 0), 0).rgb
                 + gUpSrc.SampleLevel(gClamp, uv + float2(0, t.y), 0).rgb + gUpSrc.SampleLevel(gClamp, uv - float2(0, t.y), 0).rgb;
        if (du.x * sw < 0.99) c = saturate(c + (c - n * 0.25) * 0.45);  // enlarged: sharpened
    }
    return float4(c, 1);
}

// ---------------------------------------------------------------- PrimeScaler: the picture rebuilt at the screen's size from several frames.
// Every frame the camera moves a fraction of a pixel (always a different spot), so the frames' pixels land in slightly different places:
// together they hold more detail than one frame. Each screen pixel keeps what it learned (rgb) and how many frames that is (a), follows
// what it shows as the camera moves, and never keeps a colour that is not around it now (no smearing, no ghosts).
// gOffset: xy this frame's shift (world picture pixels), z 1 = start again, w sharpening; gAxisX.zw: the world picture's size;
// gAxisY: x where the helmet's depth ends (it moves with you), y how many frames at most
Texture2D gPrimeHist : register(t42);
Texture2D<float> gPrimePrevZ : register(t52);   // the frame before's depth (no ghosts: where it doesn't match, the past is forgotten)
float3 toYCoCg(float3 c) { return float3(dot(c, float3(0.25, 0.5, 0.25)), dot(c, float3(0.5, 0, -0.5)), dot(c, float3(-0.25, 0.5, -0.25))); }
float3 fromYCoCg(float3 c) { return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }
float4 primeHistAt(float2 uv, float2 size)   // the frames before at uv, sharp (Catmull-Rom from five reads)
{
    float2 p = uv * size, c = floor(p - 0.5) + 0.5, f = p - c;
    float2 w0 = f * (-0.5 + f * (1 - 0.5 * f)), w1 = 1 + f * f * (-2.5 + 1.5 * f), w2 = f * (0.5 + f * (2 - 1.5 * f)), w3 = f * f * (-0.5 + 0.5 * f);
    float2 w12 = w1 + w2, o12 = w2 / w12;
    float2 t0 = (c - 1) / size, t3 = (c + 2) / size, t12 = (c + o12) / size;
    float a = w12.x * w0.y, b = w0.x * w12.y, m = w12.x * w12.y, d = w3.x * w12.y, e = w12.x * w3.y;
    float4 r = gPrimeHist.SampleLevel(gClamp, float2(t12.x, t0.y), 0) * a + gPrimeHist.SampleLevel(gClamp, float2(t0.x, t12.y), 0) * b
             + gPrimeHist.SampleLevel(gClamp, t12, 0) * m + gPrimeHist.SampleLevel(gClamp, float2(t3.x, t12.y), 0) * d
             + gPrimeHist.SampleLevel(gClamp, float2(t12.x, t3.y), 0) * e;
    return r / (a + b + m + d + e);
}
float4 PS_PrimeScale(SkyOut i) : SV_Target
{
    float2 D; gPrimeHist.GetDimensions(D.x, D.y);
    float2 S = gAxisX.zw, j = gOffset.xy;
    float2 uv = i.pos.xy / D;                     // this screen pixel's centre, 0..1
    float2 x = uv * S;                            // ... in the world picture's pixels
    int2 q = int2(floor(x + j)), hi = int2(S) - 1; // the nearest of this frame's pixels (each was taken at its centre minus the shift)
    float3 sum = 0, m1 = 0, m2 = 0, cMin = 1e9, cMax = -1e9; float wsum = 0, zClose = 1;
    [unroll] for (int y = -1; y <= 1; y++)
    [unroll] for (int xx = -1; xx <= 1; xx++)
    {
        int2 qq = clamp(q + int2(xx, y), int2(0, 0), hi);
        float3 c = toYCoCg(gUpSrc.Load(int3(qq, 0)).rgb);
        float2 d = x - (float2(qq) + 0.5 - j);
        float w = exp(-2.2 * dot(d, d));
        sum += c * w; wsum += w; m1 += c; m2 += c * c; cMin = min(cMin, c); cMax = max(cMax, c);
        zClose = min(zClose, gSceneDepth.Load(int3(qq, 0)).r);   // (the nearest thing around: edges follow the thing in front)
    }
    float3 cur = sum / max(wsum, 1e-4);
    float2 dn = (x - (float2(q) + 0.5 - j)) * (D / S);            // how far this frame's nearest pixel landed from this pixel's centre (screen pixels)
    float conf = exp(-1.6 * dot(dn, dn));                         // near: this frame tells a lot about this pixel
    float3 mean = m1 / 9, sd = sqrt(max(m2 / 9 - mean * mean, 0));
    // where this was on the screen a frame ago
    float2 puv = uv; float wasAt = -1;   // (wasAt: how far that point was from the camera then)
    if (zClose >= gAxisY.x)   // (the helmet and the arm cannon move with you: they stay put)
    {
        float2 ndc = uv * float2(2, -2) + float2(-1, 1);
        float3 dir = normalize(gCamF.xyz + gCamR.xyz * (ndc.x * gProj.x * gProj.y) + gCamU.xyz * (ndc.y * gProj.x));
        float3 P = zClose >= 1 ? dir * 1e4 : dir * (linDepth(zClose) / max(dot(dir, gCamF.xyz), 1e-3));
        float4 c = mul(float4(P + gHistCam.xyz, 1), gHistVP);
        puv = c.w > 1e-3 ? c.xy / c.w * float2(0.5, -0.5) + 0.5 : float2(-1, -1);
        wasAt = zClose >= 1 ? -1 : c.w;
    }
    bool ok = gOffset.z < 0.5 && all(puv > 0) && all(puv < 1);
    bool moverHere = false;   // something moved there (an enemy, a door, a shot): the frame before showed something else at that spot
    if (ok && gAxisY.z > 0.5)
    {   // (the nearest depth round it, then and now - the same 3 x 3 both times, so still edges always match and never jitter)
        int2 pq = clamp(int2(puv * S), int2(0, 0), hi); float zp = 1;
        [unroll] for (int yy = -1; yy <= 1; yy++) [unroll] for (int xq = -1; xq <= 1; xq++) zp = min(zp, gPrimePrevZ.Load(int3(clamp(pq + int2(xq, yy), int2(0, 0), hi), 0)));
        bool skyNow = wasAt < 0, skyThen = zp >= 1;
        if (skyNow != skyThen) moverHere = true;
        else if (!skyNow) { float lt = linDepth(zp); moverHere = abs(lt - wasAt) > wasAt * 0.08 + 0.35; }
    }
    if (!ok) return float4(hdrSafe(fromYCoCg(cur)), conf);                 // nothing to remember here: this frame alone
    float4 h = primeHistAt(puv, D);
    float3 hc = clamp(toYCoCg(max(h.rgb, 0)), max(cMin, mean - sd * 1.25), min(cMax, mean + sd * 1.25));   // only colours that are around it now
    float moved = length((uv - puv) * D);
    float n = min(h.a, lerp(gAxisY.y, 3, saturate(moved / 48)));  // moving fast: lean on the past less (it is blurrier there)
    if (moverHere) { n = min(n, 1); hc = clamp(toYCoCg(max(h.rgb, 0)), max(cMin, mean - sd * 0.6), min(cMax, mean + sd * 0.6)); }   // (only a little of the past, and only close to what is here now: no trail, no flicker)
    float a = max(conf / (n + conf), 1 / (gAxisY.y + 1));
    return float4(hdrSafe(fromYCoCg(lerp(hc, cur, a))), n + conf);
}
float4 PS_PrimeSharpen(SkyOut i) : SV_Target   // PrimeScaler's picture onto the screen, sharpened (never past its neighbours: no halos)
{
    float2 D; gPrimeHist.GetDimensions(D.x, D.y); int2 p = int2(i.pos.xy), hi = int2(D) - 1;
    float3 c = gPrimeHist.Load(int3(p, 0)).rgb;
    float3 n = gPrimeHist.Load(int3(clamp(p + int2(0, -1), int2(0, 0), hi), 0)).rgb, s = gPrimeHist.Load(int3(clamp(p + int2(0, 1), int2(0, 0), hi), 0)).rgb;
    float3 e = gPrimeHist.Load(int3(clamp(p + int2(1, 0), int2(0, 0), hi), 0)).rgb, w = gPrimeHist.Load(int3(clamp(p + int2(-1, 0), int2(0, 0), hi), 0)).rgb;
    float3 mn = min(c, min(min(n, s), min(e, w))), mx = max(c, max(max(n, s), max(e, w)));
    float3 r = c + (c - (n + s + e + w) * 0.25) * gOffset.w * 1.6;
    return float4(saturate(clamp(r, mn, mx)), 1);
}

// ---------------------------------------------------------------- motion blur: the picture smeared along how the camera moved it since the frame before
// gOffset: x how long (a share of the frame's movement), y where the helmet's depth ends (it moves with you: never smeared), z the longest smear (0..1 of the picture)
float4 PS_MotionBlur(SkyOut i) : SV_Target
{
    int2 px = int2(i.pos.xy);
    float3 c0 = gHdr.Load(int3(px, 0)).rgb;
    float z = gSceneDepth.Load(int3(px, 0));
    if (z < gOffset.y) return float4(c0, 1);
    float2 uv = (float2(px) + 0.5) / gDepth.zw;
    float3 dir = normalize(gCamF.xyz + gCamR.xyz * (i.ndc.x * gProj.x * gProj.y) + gCamU.xyz * (i.ndc.y * gProj.x));
    float3 P = z >= 1 ? dir * 1e4 : dir * (linDepth(z) / max(dot(dir, gCamF.xyz), 1e-3));
    float4 c = mul(float4(P + gHistCam.xyz, 1), gHistVP);
    if (c.w <= 1e-3) return float4(c0, 1);
    float2 v = (uv - (c.xy / c.w * float2(0.5, -0.5) + 0.5)) * gOffset.x;
    float L = length(v); if (L > gOffset.z) v *= gOffset.z / L;
    if (length(v * gDepth.zw) < 0.75) return float4(c0, 1);   // (less than a pixel: nothing to smear)
    const int N = 12; float j = ign(i.pos.xy + gAA.z * float2(3.1, 1.7));
    float3 sum = c0; float w = 1;
    [loop] for (int k = 0; k < N; k++)
    {
        float2 su = uv + v * ((k + j) / N - 0.5);
        if (any(su < 0) || any(su > 1)) continue;
        int2 sp = min(int2(su * gDepth.zw), int2(gDepth.zw) - 1);
        if (gSceneDepth.Load(int3(sp, 0)) < gOffset.y) continue;   // (the helmet's parts do not smear into the world)
        sum += gHdr.Load(int3(sp, 0)).rgb; w += 1;
    }
    return float4(sum / w, 1);
}
