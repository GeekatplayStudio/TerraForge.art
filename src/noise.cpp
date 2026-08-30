#include "../include/geekatplay/noise.hpp"
#include <random>
#include <algorithm>
#include <numbers>

namespace Geekatplay {

std::vector<float> NoiseGenerator::Perlin2D(int res, float frequency, uint32_t seed) {
    std::vector<float> heightfield(res * res, 0.0f);
    int gridSize = std::max(static_cast<int>(std::ceil(frequency)) + 2, 4);

    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(0.0f, 2.0f * 3.1415926535f);

    // Generate random unit gradient vectors
    std::vector<float> gradX(gridSize * gridSize);
    std::vector<float> gradY(gridSize * gridSize);
    for (int i = 0; i < gridSize * gridSize; ++i) {
        float angle = dist(rng);
        gradX[i] = std::cos(angle);
        gradY[i] = std::sin(angle);
    }

    for (int y = 0; y < res; ++y) {
        for (int x = 0; x < res; ++x) {
            float gx = (static_cast<float>(x) / res) * frequency;
            float gy = (static_cast<float>(y) / res) * frequency;

            int x0 = static_cast<int>(std::floor(gx)) % gridSize;
            int y0 = static_cast<int>(std::floor(gy)) % gridSize;
            int x1 = (x0 + 1) % gridSize;
            int y1 = (y0 + 1) % gridSize;

            float fx = gx - std::floor(gx);
            float fy = gy - std::floor(gy);

            float u = Fade(fx);
            float v = Fade(fy);

            // Dot products
            float d00 = gradX[y0 * gridSize + x0] * fx + gradY[y0 * gridSize + x0] * fy;
            float d10 = gradX[y0 * gridSize + x1] * (fx - 1.0f) + gradY[y0 * gridSize + x1] * fy;
            float d01 = gradX[y1 * gridSize + x0] * fx + gradY[y1 * gridSize + x0] * (fy - 1.0f);
            float d11 = gradX[y1 * gridSize + x1] * (fx - 1.0f) + gradY[y1 * gridSize + x1] * (fy - 1.0f);

            float nx0 = d00 + u * (d10 - d00);
            float nx1 = d01 + u * (d11 - d01);
            float n = nx0 + v * (nx1 - nx0);

            // Normalize to [0, 1]
            heightfield[y * res + x] = std::clamp((n + 0.707f) / 1.414f, 0.0f, 1.0f);
        }
    }
    return heightfield;
}

std::vector<float> NoiseGenerator::RidgedMultifractal(
    int res, int octaves, float frequency, float lacunarity, float gain, float offset, uint32_t seed
) {
    std::vector<float> heightfield(res * res, 0.0f);
    std::vector<float> weight(res * res, 1.0f);
    float freq = frequency;
    float amp = 1.0f;

    for (int o = 0; o < octaves; ++o) {
        auto layer = Perlin2D(res, freq, seed + o * 137);
        for (int i = 0; i < res * res; ++i) {
            float signal = offset - std::abs(layer[i] * 2.0f - 1.0f);
            signal = signal * signal * weight[i];
            heightfield[i] += signal * amp;
            weight[i] = std::clamp(signal * gain, 0.0f, 1.0f);
        }
        freq *= lacunarity;
        amp *= gain;
    }

    float minVal = *std::min_element(heightfield.begin(), heightfield.end());
    float maxVal = *std::max_element(heightfield.begin(), heightfield.end());
    if (maxVal > minVal) {
        for (auto& v : heightfield) {
            v = (v - minVal) / (maxVal - minVal);
        }
    }
    return heightfield;
}

std::vector<float> NoiseGenerator::VoronoiCellular(int res, int cellCount, uint32_t seed) {
    std::vector<float> heightfield(res * res, 0.0f);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    std::vector<float> ptsX(cellCount);
    std::vector<float> ptsY(cellCount);
    for (int i = 0; i < cellCount; ++i) {
        ptsX[i] = dist(rng);
        ptsY[i] = dist(rng);
    }

    for (int y = 0; y < res; ++y) {
        for (int x = 0; x < res; ++x) {
            float nx = static_cast<float>(x) / res;
            float ny = static_cast<float>(y) / res;
            float minDistSq = 1e9f;

            for (int i = 0; i < cellCount; ++i) {
                float dx = nx - ptsX[i];
                float dy = ny - ptsY[i];
                float d2 = dx * dx + dy * dy;
                if (d2 < minDistSq) minDistSq = d2;
            }
            heightfield[y * res + x] = std::sqrt(minDistSq);
        }
    }

    float minVal = *std::min_element(heightfield.begin(), heightfield.end());
    float maxVal = *std::max_element(heightfield.begin(), heightfield.end());
    if (maxVal > minVal) {
        for (auto& v : heightfield) {
            v = (v - minVal) / (maxVal - minVal);
        }
    }
    return heightfield;
}

std::vector<float> NoiseGenerator::Billow(int res, int octaves, float frequency, uint32_t seed) {
    std::vector<float> heightfield(res * res, 0.0f);
    float freq = frequency;
    float amp = 1.0f;
    float totalAmp = 0.0f;

    for (int o = 0; o < octaves; ++o) {
        auto layer = Perlin2D(res, freq, seed + o * 99);
        for (int i = 0; i < res * res; ++i) {
            float billow = 2.0f * std::abs(layer[i] - 0.5f);
            heightfield[i] += billow * amp;
        }
        totalAmp += amp;
        freq *= 2.0f;
        amp *= 0.5f;
    }

    for (auto& v : heightfield) {
        v /= (totalAmp > 0.0f ? totalAmp : 1.0f);
    }
    return heightfield;
}

} // namespace Geekatplay
