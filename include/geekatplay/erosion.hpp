#pragma once

#include <vector>

namespace Geekatplay {

struct HydraulicErosionResult {
    std::vector<float> heightfield;
    std::vector<float> flowAccumulation;
    std::vector<float> sedimentMap;
};

struct ThermalWeatheringResult {
    std::vector<float> heightfield;
    std::vector<float> talusMap;
};

class ErosionSolver {
public:
    // Physical shallow-water hydraulic fluvial erosion
    static HydraulicErosionResult SimulateHydraulic(
        const std::vector<float>& heightfield,
        int res,
        int iterations = 30,
        float rainRate = 0.012f,
        float evaporationRate = 0.025f,
        float sedimentCapacity = 4.0f
    );

    // Thermal weathering freeze-thaw angle of repose settling
    static ThermalWeatheringResult SimulateThermal(
        const std::vector<float>& heightfield,
        int res,
        float reposeAngleDeg = 34.5f,
        float talusRate = 0.45f,
        int iterations = 20
    );
};

} // namespace Geekatplay
