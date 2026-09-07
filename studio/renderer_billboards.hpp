// Geekatplay TerraForge - the billboard rung of the scattered-copy ladder.
// See renderer_billboards.cpp; the cards themselves are baked in
// studio/billboard.cpp and uploaded by instances_upload.
#pragma once
#include <vector>

namespace studio {
struct SceneObject;
struct InstanceRun;
struct RenderSettings;

// What the card pass needs from the frame it is drawn in. Everything is
// borrowed; nothing is kept.
struct BillboardPass {
  const float *mvp = nullptr;
  const float *model = nullptr;
  const float *view_eye = nullptr;
  const float *sun = nullptr;
  const float *sun_color = nullptr;
  const float *grade = nullptr;
  float hscale = 1.f, exposure = 1.f, saturation = 1.f, time_acc = 0.f;
  int aov = 0, object_id = 0;
  bool atmosphere = true;
  RenderSettings *RS = nullptr;
};

void instances_draw_billboards(SceneObject &o, const std::vector<InstanceRun> &runs,
                               const BillboardPass &bp);
} // namespace studio
