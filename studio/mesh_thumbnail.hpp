// Geekatplay TerraForge - a CPU-rasterised picture of a scene mesh, for the
// ImportObject node's card and for the billboards a scattered mesh becomes
// at distance. RGBA, size x size, no GL needed, so it can be made on
// whatever thread loaded the mesh.
#pragma once
#include <cstdint>
#include <vector>

namespace studio {
struct SceneObject;

struct MeshRasterOptions {
  // where the camera stands: a three-quarter view from above for a
  // thumbnail, straight on for a card that will be seen from the ground
  float yaw_deg = 35.f, pitch_deg = -25.f;
  bool plate = true;        // a dark backing, so a thumbnail reads as a picture
  bool alpha_cutout = true; // a texel the model's own picture calls transparent
                            // is not drawn - without this a leaf card is a slab
};

// The mesh, rasterised. With `plate` off, everything the mesh did not cover
// is left fully transparent.
std::vector<uint8_t> mesh_raster(const SceneObject &o, int size,
                                 const MeshRasterOptions &opt);

// The node card: the three-quarter view on its plate.
std::vector<uint8_t> mesh_thumbnail(const SceneObject &o, int size);
} // namespace studio
