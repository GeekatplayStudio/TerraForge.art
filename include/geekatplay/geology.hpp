#pragma once

#include <vector>

namespace Geekatplay {

class GeologySolver {
public:
    // Multi-layer rock strata with differential hardness resistance
    static std::vector<float> GeologicalStrata(
        const std::vector<float>& inH,
        int res,
        int layers = 12,
        float hardnessVariance = 0.6f,
        float foldingAngleDeg = 15.0f
    );

    // Tectonic fault uplift and displacement
    static std::vector<float> TectonicFault(
        const std::vector<float>& inH,
        int res,
        float faultAngleDeg = 45.0f,
        float displacement = 0.25f,
        float faultPosition = 0.5f
    );

    // Terraced plateau stepped cliffs
    static std::vector<float> TerracePlateau(
        const std::vector<float>& inH,
        int res,
        int steps = 6,
        float smoothness = 0.15f
    );
};

} // namespace Geekatplay
