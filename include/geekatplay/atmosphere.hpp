#pragma once

#include <vector>
#include <array>
#include <cstdint>

namespace Geekatplay {

struct AtmosphereParameters {
    float sunElevationDeg = 28.0f;
    float sunAzimuthDeg = 135.0f;
    float rayleighTurbidity = 1.0f;
    float mieHazeDensity = 0.005f;
    float cloudCoverage = 0.45f;
};

class AtmosphereSolver {
public:
    // Calculates normalized sun direction [X, Y, Z]
    static std::array<float, 3> GetSunDirection(float elevationDeg, float azimuthDeg);

    // Calculates Rayleigh and Mie spectral scattering attenuation
    static std::array<float, 3> GetSunTransmittance(const AtmosphereParameters& params);

    // Generates 2D volumetric cloud coverage density
    static std::vector<float> GenerateClouds(int res, float coverage = 0.45f, uint32_t seed = 42);
};

} // namespace Geekatplay
