#pragma once

#include <vector>
#include <string>
#include <cstdint>

namespace Geekatplay {

struct EcoSpecies {
    int id;
    std::string name;
    float minAltitude;
    float maxAltitude;
    float maxSlopeDeg;
    float density;
    float scaleMin;
    float scaleMax;
};

struct EcoInstance {
    float x, y, z;
    float yaw, pitch, roll;
    float scale;
    int speciesId;
};

class EcoSystemSolver {
public:
    // Distributes vegetation instances based on bioclimatic rules
    static std::vector<EcoInstance> Populate(
        const std::vector<float>& inH,
        int res,
        const std::vector<EcoSpecies>& rules,
        uint32_t seed = 42,
        int maxInstances = 50000
    );
};

} // namespace Geekatplay
