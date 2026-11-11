// assetmeta.h : per-asset settings the editor writes next to each model ("Arm_canon.fbx.meta").
// Plain text so it is readable and diff-friendly. Clip settings are keyed by the clip's name in the
// FBX, so re-exporting the model from Blockbench keeps renames, loop flags and speeds.
#pragma once
#include "model.h"
#include <string>
#include <vector>

namespace px {

struct ClipMeta { std::string fileName, displayName; ClipSettings s; };

struct AssetMeta {
    float importScale = 0;          // 0 = auto
    bool pixelFilter = true;        // nearest-neighbour textures (Blockbench pixel art)
    bool autoFit = true;            // scale the viewmodel to `fitLength` metres
    float fitLength = 0.85f;
    V3 viewPos{ 0.27f, -0.27f, 0.55f };   // viewmodel centre, camera space (x right, y up, z forward)
    V3 viewRotDeg{ 0, -8, 0 };
    float viewScale = 1.f;          // extra scale on top of auto fit
    float reflect = 0.25f;          // sphere-map reflectivity
    BlendMode blendMode = BlendMode::Inertialize;
    BlendCurve blendCurve = BlendCurve::Smooth;
    float blendTime = 0.2f;
    std::string defaultClip;        // played when the model loads
    std::vector<ClipMeta> clips;

    bool load(const std::string& path);
    bool save(const std::string& path) const;

    // Fills `settings` (one per clip of `m`) from the stored entries; adds entries for new clips.
    void applyTo(const Model& m, std::vector<ClipSettings>& settings, std::vector<std::string>& names);
    // Copies the editor state back into the stored entries.
    void capture(const Model& m, const std::vector<ClipSettings>& settings, const std::vector<std::string>& names);
};

const char* blendModeName(BlendMode m);
const char* blendCurveName(BlendCurve c);

} // namespace px
