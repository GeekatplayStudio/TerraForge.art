// Geekatplay TerraForge - a CPU-rasterised thumbnail of a scene mesh, for
// the ImportObject node's card. RGBA, size x size, no GL needed.
#pragma once
#include <cstdint>
#include <vector>

namespace studio {
struct SceneObject;
std::vector<uint8_t> mesh_thumbnail(const SceneObject &o, int size);
} // namespace studio
