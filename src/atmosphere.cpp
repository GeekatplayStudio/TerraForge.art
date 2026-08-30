#include "../include/geekatplay/atmosphere.hpp"
#include "../include/geekatplay/noise.hpp"
#include <cmath>
#include <algorithm>

namespace Geekatplay {

std::array<float, 3> AtmosphereSolver::GetSunDirection(float elevationDeg, float azimuthDeg) {
    float elRad = elevationDeg * 3.1415926535f / 180.0f;
    float azRad = azimuthDeg * 3.1415926535f / 180.0f;

    float x = std::cos(elRad) * std::sin(azRad);
    float y = std::sin(elRad);
    float z = std::cos(elRad) * std::cos(azRad);

    float len = std::sqrt(x * x + y * y + z * z) + 1e-6f;
    return {x / len, y / len, z / len};
}

std::array<float, 3> AtmosphereSolver::GetSunTransmittance(const AtmosphereParameters& params) {
    auto sunDir = GetSunDirection(params.sunElevationDeg, params.sunAzimuthDeg);
    float cosZenith = std::max(sunDir[1], 0.02f);
    float opticalDepth = 1.0f / cosZenith;

    // Sea-level Rayleigh scattering coefficients (RGB: 680nm, 550nm, 440nm)
    float betaR[3] = {5.8e-6f * params.rayleighTurbidity, 13.5e-6f * params.rayleighTurbidity, 33.1e-6f * params.rayleighTurbidity};
    float betaM = 21.0e-6f * params.mieHazeDensity;

    float trR = std::exp(-(betaR[0] + betaM) * opticalDepth * 8000.0f);
    float trG = std::exp(-(betaR[1] + betaM) * opticalDepth * 8000.0f);
    float trB = std::exp(-(betaR[2] + betaM) * opticalDepth * 8000.0f);

    return {trR, trG, trB};
}

std::vector<float> AtmosphereSolver::GenerateClouds(int res, float coverage, uint32_t seed) {
    auto base = NoiseGenerator::Perlin2D(res, 3.0f, seed);
    auto detail = NoiseGenerator::Perlin2D(res, 9.0f, seed + 101);

    std::vector<float> clouds(res * res, 0.0f);
    for (int i = 0; i < res * res; ++i) {
        float comb = base[i] * 0.7f + detail[i] * 0.3f;
        float c = std::clamp((comb - (1.0f - coverage)) / (coverage + 1e-6f), 0.0f, 1.0f);
        clouds[i] = c * c * (3.0f - 2.0f * c);
    }
    return clouds;
}

} // namespace Geekatplay
