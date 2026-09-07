// Geekatplay TerraForge - the card a scattered mesh becomes at distance.
//
// Baked once per mesh on the CPU (studio/mesh_thumbnail.cpp's rasteriser,
// straight on, no plate, the model's own cut-outs kept), so a distant
// forest costs four vertices a tree instead of a few thousand. What the
// square covers in model units is recorded with it, so the card stands
// exactly where the mesh stood.
#pragma once

namespace studio {
struct SceneObject;

// Fills o.card_rgba / card_px / card_size / card_centre. False when the
// mesh has nothing to draw. Pure: no GL.
bool billboard_build(SceneObject &o);
} // namespace studio
