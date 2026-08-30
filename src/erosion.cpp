#include "../include/geekatplay/erosion.hpp"
#include <cmath>
#include <algorithm>

namespace Geekatplay {

HydraulicErosionResult ErosionSolver::SimulateHydraulic(
    const std::vector<float>& inH, int res, int iterations, float rainRate, float evaporationRate, float sedimentCapacity
) {
    HydraulicErosionResult result;
    result.heightfield = inH;
    result.flowAccumulation.assign(res * res, 0.0f);
    result.sedimentMap.assign(res * res, 0.0f);

    std::vector<float> water(res * res, 0.0f);

    for (int iter = 0; iter < iterations; ++iter) {
        // 1. Rain
        for (int i = 0; i < res * res; ++i) {
            water[i] += rainRate;
        }

        // 2. Flow transfer
        for (int y = 1; y < res - 1; ++y) {
            for (int x = 1; x < res - 1; ++x) {
                int idx = y * res + x;
                float currentH = result.heightfield[idx] + water[idx];

                float hUp = result.heightfield[(y - 1) * res + x] + water[(y - 1) * res + x];
                float hDown = result.heightfield[(y + 1) * res + x] + water[(y + 1) * res + x];
                float hLeft = result.heightfield[y * res + (x - 1)] + water[y * res + (x - 1)];
                float hRight = result.heightfield[y * res + (x + 1)] + water[y * res + (x + 1)];

                float sUp = std::max(0.0f, currentH - hUp);
                float sDown = std::max(0.0f, currentH - hDown);
                float sLeft = std::max(0.0f, currentH - hLeft);
                float sRight = std::max(0.0f, currentH - hRight);

                float totalSlope = sUp + sDown + sLeft + sRight + 1e-6f;
                float flow = water[idx];
                result.flowAccumulation[idx] += flow;

                // Erode sediment based on velocity and slope
                float capacity = std::max(0.01f, totalSlope) * flow * sedimentCapacity;
                float diff = capacity - result.sedimentMap[idx];

                if (diff > 0.0f) {
                    float erode = diff * 0.3f;
                    result.heightfield[idx] -= std::min(erode, result.heightfield[idx] * 0.05f);
                    result.sedimentMap[idx] += erode;
                } else {
                    float deposit = -diff * 0.3f;
                    result.heightfield[idx] += deposit;
                    result.sedimentMap[idx] -= deposit;
                }

                // Evaporate
                water[idx] = std::max(0.0f, water[idx] * (1.0f - evaporationRate) - 0.001f);
            }
        }
    }

    // Normalize flow
    float maxFlow = *std::max_element(result.flowAccumulation.begin(), result.flowAccumulation.end()) + 1e-6f;
    for (auto& f : result.flowAccumulation) {
        f /= maxFlow;
    }

    for (auto& h : result.heightfield) {
        h = std::clamp(h, 0.0f, 1.0f);
    }
    return result;
}

ThermalWeatheringResult ErosionSolver::SimulateThermal(
    const std::vector<float>& inH, int res, float reposeAngleDeg, float talusRate, int iterations
) {
    ThermalWeatheringResult result;
    result.heightfield = inH;
    result.talusMap.assign(res * res, 0.0f);

    float critThreshold = std::tan(reposeAngleDeg * 3.1415926535f / 180.0f) / static_cast<float>(res);

    for (int iter = 0; iter < iterations; ++iter) {
        for (int y = 1; y < res - 1; ++y) {
            for (int x = 1; x < res - 1; ++x) {
                int idx = y * res + x;
                float h = result.heightfield[idx];

                float dUp = std::max(0.0f, h - result.heightfield[(y - 1) * res + x] - critThreshold);
                float dDown = std::max(0.0f, h - result.heightfield[(y + 1) * res + x] - critThreshold);
                float dLeft = std::max(0.0f, h - result.heightfield[y * res + (x - 1)] - critThreshold);
                float dRight = std::max(0.0f, h - result.heightfield[y * res + (x + 1)] - critThreshold);

                float totalExcess = dUp + dDown + dLeft + dRight;
                float slippage = (totalExcess * 0.25f) * talusRate;

                result.heightfield[idx] -= slippage;
                result.talusMap[idx] += slippage;

                result.heightfield[(y - 1) * res + x] += dUp * 0.25f * talusRate;
                result.heightfield[(y + 1) * res + x] += dDown * 0.25f * talusRate;
                result.heightfield[y * res + (x - 1)] += dLeft * 0.25f * talusRate;
                result.heightfield[y * res + (x + 1)] += dRight * 0.25f * talusRate;
            }
        }
    }

    float maxTalus = *std::max_element(result.talusMap.begin(), result.talusMap.end()) + 1e-6f;
    for (auto& t : result.talusMap) {
        t /= maxTalus;
    }

    for (auto& h : result.heightfield) {
        h = std::clamp(h, 0.0f, 1.0f);
    }
    return result;
}

} // namespace Geekatplay
