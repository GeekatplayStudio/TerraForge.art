#include "../include/geekatplay/geology.hpp"
#include <cmath>
#include <random>
#include <algorithm>

namespace Geekatplay {

std::vector<float> GeologySolver::GeologicalStrata(
    const std::vector<float>& inH, int res, int layers, float hardnessVariance, float foldingAngleDeg
) {
    std::vector<float> outH = inH;
    float rad = foldingAngleDeg * 3.1415926535f / 180.0f;
    float cosA = std::cos(rad);
    float sinA = std::sin(rad);

    for (int y = 0; y < res; ++y) {
        for (int x = 0; x < res; ++x) {
            int idx = y * res + x;
            float gx = static_cast<float>(x) / res;
            float gy = static_cast<float>(y) / res;

            float dipOffset = (gx * cosA + gy * sinA) * 0.15f;
            float strataCoord = std::clamp((inH[idx] + dipOffset) * layers, 0.0f, static_cast<float>(layers - 1));

            int layerIdx = static_cast<int>(std::floor(strataCoord));
            float frac = strataCoord - layerIdx;

            // S-curve terrace profiling modulated by hardnessVariance
            float expLow = std::max(0.2f, 1.0f - hardnessVariance * 0.6f);
            float expHigh = 1.0f + hardnessVariance * 0.8f;
            float shapedFrac = (layerIdx % 2 == 0) ? std::pow(frac, expLow) : std::pow(frac, expHigh);
            float strataH = (layerIdx + shapedFrac) / layers;

            outH[idx] = std::clamp(inH[idx] * 0.7f + strataH * 0.3f, 0.0f, 1.0f);
        }
    }
    return outH;
}

std::vector<float> GeologySolver::TectonicFault(
    const std::vector<float>& inH, int res, float faultAngleDeg, float displacement, float faultPosition
) {
    std::vector<float> outH = inH;
    float rad = faultAngleDeg * 3.1415926535f / 180.0f;
    float nx = std::cos(rad);
    float ny = std::sin(rad);

    for (int y = 0; y < res; ++y) {
        for (int x = 0; x < res; ++x) {
            int idx = y * res + x;
            float gx = static_cast<float>(x) / res;
            float gy = static_cast<float>(y) / res;

            float dist = (gx * nx + gy * ny) - faultPosition;
            float disp = displacement * (1.0f / (1.0f + std::exp(-dist / 0.08f)));
            outH[idx] += disp;
        }
    }

    float minVal = *std::min_element(outH.begin(), outH.end());
    float maxVal = *std::max_element(outH.begin(), outH.end());
    if (maxVal > minVal) {
        for (auto& v : outH) {
            v = (v - minVal) / (maxVal - minVal);
        }
    }
    return outH;
}

std::vector<float> GeologySolver::TerracePlateau(
    const std::vector<float>& inH, int res, int steps, float smoothness
) {
    std::vector<float> outH(res * res, 0.0f);
    for (int i = 0; i < res * res; ++i) {
        float scaled = inH[i] * steps;
        float base = std::floor(scaled);
        float frac = scaled - base;

        float t = std::clamp((frac - (0.5f - smoothness)) / (2.0f * smoothness + 1e-6f), 0.0f, 1.0f);
        float stepped = base + t * t * (3.0f - 2.0f * t);
        outH[i] = stepped / steps;
    }
    return outH;
}

} // namespace Geekatplay
