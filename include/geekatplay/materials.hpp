#pragma once

#include <vector>

namespace Geekatplay {

struct PBRSplatmapResult {
    std::vector<float> rockMask;
    std::vector<float> snowMask;
    std::vector<float> soilMask;
    std::vector<float> gravelMask;
    std::vector<float> splatmapRGBA; // 4 channels per pixel
    std::vector<float> normalMapRGB; // 3 channels per pixel
};

class MaterialSolver {
public:
    // Computes slope angles [0, 1]
    static std::vector<float> ComputeSlopeMap(const std::vector<float>& inH, int res);

    // Computes Laplacian surface curvature
    static std::vector<float> ComputeCurvatureMap(const std::vector<float>& inH, int res);

    // Computes tangent-space normal map RGB [0, 1]
    static std::vector<float> ComputeNormalMap(const std::vector<float>& inH, int res, float strength = 8.0f);

    // Generates complete multi-channel PBR biome splatmap
    static PBRSplatmapResult GeneratePBRSplatmap(
        const std::vector<float>& inH,
        int res,
        const std::vector<float>& flowMap = {},
        float snowLine = 0.72f,
        float cliffSlopeThreshold = 0.45f
    );
};

} // namespace Geekatplay
