#pragma once

#include <vector>
#include <cmath>
#include <cstdint>

namespace Geekatplay {

class NoiseGenerator {
public:
    // 2D Perlin gradient noise normalized to [0, 1]
    static std::vector<float> Perlin2D(int res, float frequency = 4.0f, uint32_t seed = 42);

    // Sharp Alpine Ridged Multifractal synthesis
    static std::vector<float> RidgedMultifractal(
        int res,
        int octaves = 8,
        float frequency = 2.0f,
        float lacunarity = 2.15f,
        float gain = 0.5f,
        float offset = 1.0f,
        uint32_t seed = 42
    );

    // Voronoi / Worley cellular distance field
    static std::vector<float> VoronoiCellular(
        int res,
        int cellCount = 16,
        uint32_t seed = 42
    );

    // Billow puffy noise
    static std::vector<float> Billow(
        int res,
        int octaves = 6,
        float frequency = 3.0f,
        uint32_t seed = 42
    );

private:
    static float Fade(float t) {
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }
};

} // namespace Geekatplay
