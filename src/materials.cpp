#include "../include/geekatplay/materials.hpp"
#include <cmath>
#include <algorithm>

namespace Geekatplay {

std::vector<float> MaterialSolver::ComputeSlopeMap(const std::vector<float>& inH, int res) {
    std::vector<float> slope(res * res, 0.0f);
    for (int y = 1; y < res - 1; ++y) {
        for (int x = 1; x < res - 1; ++x) {
            float dx = (inH[y * res + (x + 1)] - inH[y * res + (x - 1)]) * 0.5f;
            float dy = (inH[(y + 1) * res + x] - inH[(y - 1) * res + x]) * 0.5f;
            float s = std::atan(std::sqrt(dx * dx + dy * dy)) / (3.1415926535f * 0.5f);
            slope[y * res + x] = std::clamp(s, 0.0f, 1.0f);
        }
    }
    return slope;
}

std::vector<float> MaterialSolver::ComputeCurvatureMap(const std::vector<float>& inH, int res) {
    std::vector<float> curv(res * res, 0.5f);
    for (int y = 1; y < res - 1; ++y) {
        for (int x = 1; x < res - 1; ++x) {
            float h = inH[y * res + x];
            float lap = inH[(y - 1) * res + x] + inH[(y + 1) * res + x] + inH[y * res + (x - 1)] + inH[y * res + (x + 1)] - 4.0f * h;
            curv[y * res + x] = std::clamp(lap * 10.0f + 0.5f, 0.0f, 1.0f);
        }
    }
    return curv;
}

std::vector<float> MaterialSolver::ComputeNormalMap(const std::vector<float>& inH, int res, float strength) {
    std::vector<float> normalRGB(res * res * 3, 0.0f);
    for (int y = 1; y < res - 1; ++y) {
        for (int x = 1; x < res - 1; ++x) {
            float dx = (inH[y * res + (x + 1)] - inH[y * res + (x - 1)]) * 0.5f;
            float dy = (inH[(y + 1) * res + x] - inH[(y - 1) * res + x]) * 0.5f;

            float nx = -dx * strength;
            float ny = -dy * strength;
            float nz = 1.0f;

            float len = std::sqrt(nx * nx + ny * ny + nz * nz) + 1e-6f;
            nx /= len; ny /= len; nz /= len;

            int idx = (y * res + x) * 3;
            normalRGB[idx + 0] = (nx + 1.0f) * 0.5f;
            normalRGB[idx + 1] = (ny + 1.0f) * 0.5f;
            normalRGB[idx + 2] = (nz + 1.0f) * 0.5f;
        }
    }
    return normalRGB;
}

PBRSplatmapResult MaterialSolver::GeneratePBRSplatmap(
    const std::vector<float>& inH, int res, const std::vector<float>& flowMap, float snowLine, float cliffSlopeThreshold
) {
    PBRSplatmapResult result;
    result.rockMask.assign(res * res, 0.0f);
    result.snowMask.assign(res * res, 0.0f);
    result.soilMask.assign(res * res, 0.0f);
    result.gravelMask.assign(res * res, 0.0f);
    result.splatmapRGBA.assign(res * res * 4, 0.0f);

    auto slope = ComputeSlopeMap(inH, res);
    result.normalMapRGB = ComputeNormalMap(inH, res);

    for (int i = 0; i < res * res; ++i) {
        float h = inH[i];
        float s = slope[i];

        // 1. Rock Mask
        float rock = std::clamp((s - cliffSlopeThreshold * 0.7f) / (cliffSlopeThreshold * 0.6f + 1e-6f), 0.0f, 1.0f);

        // 2. Snow Mask
        float snow = std::clamp((h - snowLine) / 0.18f, 0.0f, 1.0f) * (1.0f - rock * 0.6f);

        float flowVal = (i < static_cast<int>(flowMap.size())) ? flowMap[i] : 0.0f;

        // 3. Soil / Vegetation (Enhanced by flow wetness)
        float soil = (1.0f - rock) * (1.0f - snow) * std::clamp(1.0f - h * 1.2f + flowVal * 0.2f, 0.0f, 1.0f);

        // 4. Gravel / Scree
        float gravel = std::clamp(1.0f - (rock + snow + soil), 0.0f, 1.0f);

        result.rockMask[i] = rock;
        result.snowMask[i] = snow;
        result.soilMask[i] = soil;
        result.gravelMask[i] = gravel;

        int rgbaIdx = i * 4;
        result.splatmapRGBA[rgbaIdx + 0] = rock;
        result.splatmapRGBA[rgbaIdx + 1] = gravel;
        result.splatmapRGBA[rgbaIdx + 2] = soil;
        result.splatmapRGBA[rgbaIdx + 3] = snow;
    }

    return result;
}

} // namespace Geekatplay
