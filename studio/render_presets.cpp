// Geekatplay TerraForge - named render presets and the batch queue
// (render_presets.hpp).
#include "render_presets.hpp"
#include "app.hpp"
#include "scene.hpp"
#include <algorithm>

namespace studio {

RenderPreset *render_preset_find(const std::string &name) {
  for (RenderPreset &p : scene().render_presets)
    if (p.name == name) return &p;
  return nullptr;
}

int render_preset_upsert(const std::string &name, const RenderAssign &from) {
  SceneState &sc = scene();
  for (size_t i = 0; i < sc.render_presets.size(); ++i)
    if (sc.render_presets[i].name == name) {
      sc.render_presets[i].assign = from;
      sc.render_presets[i].assign.preset = name;
      return (int)i;
    }
  RenderPreset p;
  p.name = name;
  p.assign = from;
  p.assign.preset = name;
  sc.render_presets.push_back(p);
  return (int)sc.render_presets.size() - 1;
}

bool render_preset_apply(const std::string &name, RenderAssign &to) {
  const RenderPreset *p = render_preset_find(name);
  if (!p) return false;
  const std::string keep_output = to.output;
  to = p->assign;
  if (to.output.empty()) to.output = keep_output;
  to.preset = name;
  return true;
}

bool render_preset_delete(const std::string &name) {
  SceneState &sc = scene();
  for (size_t i = 0; i < sc.render_presets.size(); ++i)
    if (sc.render_presets[i].name == name) {
      sc.render_presets.erase(sc.render_presets.begin() + (long)i);
      return true;
    }
  return false;
}

std::string render_preset_free_name() {
  for (int n = 1; n < 1000; ++n) {
    const std::string name = "Preset " + std::to_string(n);
    if (!render_preset_find(name)) return name;
  }
  return "Preset";
}

int render_batch_queue(App &a, const std::vector<int> &cameras, const std::string &preset) {
  SceneState &sc = scene();
  int n = 0;
  for (int c : cameras) {
    if (c < 0 || c >= (int)sc.objects.size() || sc.objects[(size_t)c].type != SceneObject::Camera)
      continue;
    RenderAssign &r = sc.objects[(size_t)c].cam.render;
    if (!preset.empty()) render_preset_apply(preset, r);
    // every camera its own file: the default name becomes the camera's
    if (r.output.empty() || r.output == "render.png") {
      std::string safe = sc.objects[(size_t)c].name;
      for (char &ch : safe)
        if (!(isalnum((unsigned char)ch) || ch == '-' || ch == '_')) ch = '_';
      r.output = "render_" + safe + ".png";
    }
    if (std::find(a.render_queue.begin(), a.render_queue.end(), c) == a.render_queue.end())
      a.render_queue.push_back(c);
    ++n;
  }
  return n;
}

} // namespace studio
