// Geekatplay TerraForge - the instance stream on the GPU, and the LOD
// decisions both passes draw from.
//
// A scattered mesh's copies live in one VBO shared by every view, attached
// to the mesh's VAO and to its reduced copies' VAOs. Each frame the colour
// pass decides, per cell of the population, how many of its copies to draw
// and from which mesh level (scatter_lod.hpp); the shadow pass reuses the
// same decision so a copy's shadow is exactly where the copy is.
#pragma once
#include "terrain_cull.hpp"
#include <vector>

namespace studio {
struct SceneObject;
struct LodParams;

struct InstanceRun {
  int base = 0, count = 0; // copies, into the bucketed stream
  int level = 0;           // 0 the mesh itself, 1..2 its reduced copies
  float grow = 1.f;        // survivors' growth where the run is thinned
};

// Upload the stream when the rebuild moved it; build and upload the reduced
// meshes the first time a scattered mesh is seen.
void instances_upload(SceneObject &o);
// A mesh's geometry and pictures onto the GPU when `gpu_dirty`
// (renderer_mesh_upload.cpp).
void mesh_upload(SceneObject &o);

// The runs to draw this frame. `eye` in world units; `fr` may be null (the
// shadow pass draws every cell the sun sees). `height_scale` and `size_m`
// turn the cells' bounds into metres.
void instance_runs(const SceneObject &o, const float eye[3], const Frustum *fr,
                   float height_scale, float size_m, const LodParams &p,
                   std::vector<InstanceRun> &runs);

// Draw a vertex range of the mesh for every level-0 run (base instance +
// count), with u_inst_grow set per run.
void instances_draw_range(unsigned prog, const std::vector<InstanceRun> &runs, int first, int count);
// Draw the reduced levels: every run whose level is 1 or 2, from that VAO.
void instances_draw_lods(unsigned prog, SceneObject &o, const std::vector<InstanceRun> &runs);

// How many copies the colour passes drew this frame, and how many exist.
void renderer_instance_stats(int &drawn, int &total);
void instances_count(int drawn, int total);

// The frame's LOD dials from the settings and the governor.
LodParams instance_lod_params();

} // namespace studio
