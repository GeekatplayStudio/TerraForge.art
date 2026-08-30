#include "../include/geekatplay/ecosystem.hpp"
#include <random>
#include <cmath>
#include <algorithm>

namespace Geekatplay {

std::vector<EcoInstance> EcoSystemSolver::Populate(
    const std::vector<float>& inH, int res, const std::vector<EcoSpecies>& rules, uint32_t seed, int maxInstances
) {
    std::vector<EcoInstance> instances;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
    std::uniform_real_distribution<float> distAngle(0.0f, 360.0f);

    for (const auto& rule : rules) {
        for (int y = 1; y < res - 1; ++y) {
            for (int x = 1; x < res - 1; ++x) {
                if (static_cast<int>(instances.size()) >= maxInstances) break;

                float h = inH[y * res + x];
                if (h < rule.minAltitude || h > rule.maxAltitude) continue;

                float dx = (inH[y * res + (x + 1)] - inH[y * res + (x - 1)]) * 0.5f;
                float dy = (inH[(y + 1) * res + x] - inH[(y - 1) * res + x]) * 0.5f;
                float slopeDeg = std::atan(std::sqrt(dx * dx + dy * dy)) * 180.0f / 3.1415926535f;

                if (slopeDeg > rule.maxSlopeDeg) continue;

                if (dist01(rng) <= rule.density) {
                    EcoInstance inst;
                    float jx = (dist01(rng) - 0.5f) * 0.8f;
                    float jy = (dist01(rng) - 0.5f) * 0.8f;

                    inst.x = (static_cast<float>(x) + jx) / res;
                    inst.y = h;
                    inst.z = (static_cast<float>(y) + jy) / res;

                    inst.yaw = distAngle(rng);
                    inst.pitch = (dist01(rng) - 0.5f) * 4.0f;
                    inst.roll = (dist01(rng) - 0.5f) * 4.0f;

                    inst.scale = rule.scaleMin + dist01(rng) * (rule.scaleMax - rule.scaleMin);
                    inst.speciesId = rule.id;

                    instances.push_back(inst);
                }
            }
        }
    }

    return instances;
}

} // namespace Geekatplay
