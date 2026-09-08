// Geekatplay TerraForge — the base materials, as the graphs that make them.
//
// Each entry says what the substance does with light, in the terms the
// material already has: how rough, how metallic, how transparent and at what
// index, how much light passes through it, whether it glows, whether it has
// relief. A flat colour or a fractal feeds the base colour. Nothing here is a
// picture - every one of these is parameters, which is what makes them a
// base rather than a finished thing.
#include "material_presets.hpp"
#include "app.hpp"
#include "console.hpp"
#include "undo.hpp"
#include "gpx/node_graph.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>

namespace studio {

namespace {

struct Preset {
  MaterialPresetInfo info;
  float rgb[3];
  // surface: -1 leaves the MaterialOutput's default
  float roughness, metallic, specular, reflection;
  float transparency, ior, translucency, luminous;
  float displacement; // > 0 wires a Noise into the displacement input
  int fractal;        // 1: FractalColor with the gradient below instead of a flat colour
  float g0[3], g1[3]; // the fractal's two colours, dark to light
  float volume = 0.f; // > 0: a medium, not a surface (vol_density)
};

// name, group, blurb | colour | rough metal spec refl | transp ior transl lum | disp | fractal g0 g1
const Preset PRESETS[] = {
    {{"Basic Color", "Basic", "A plain matte surface in one colour - the starting point for anything."},
     {0.60f, 0.60f, 0.60f}, 0.85f, 0.f, 0.35f, 0.10f, 0.f, 1.5f, 0.f, 0.f, 0.f, 0, {}, {}},
    {{"Mirror", "Basic", "Perfect reflection: a polished metal with no colour of its own."},
     {0.95f, 0.95f, 0.95f}, 0.02f, 1.f, 1.f, 1.f, 0.f, 1.5f, 0.f, 0.f, 0.f, 0, {}, {}},
    {{"Wax", "Basic", "Soft and slightly translucent, the light getting a little way in."},
     {0.92f, 0.86f, 0.72f}, 0.45f, 0.f, 0.30f, 0.15f, 0.f, 1.45f, 0.45f, 0.f, 0.f, 0, {}, {}},
    {{"Slime", "Basic", "Wet, green, glossy and a little see-through."},
     {0.30f, 0.70f, 0.20f}, 0.12f, 0.f, 0.55f, 0.35f, 0.25f, 1.38f, 0.35f, 0.f, 0.f, 0, {}, {}},

    {{"Metal", "Metal", "A generic brushed metal - steel-grey, mostly reflective."},
     {0.72f, 0.72f, 0.74f}, 0.35f, 1.f, 0.90f, 0.70f, 0.f, 1.5f, 0.f, 0.f, 0.f, 0, {}, {}},
    {{"Copper", "Metal", "Polished copper, warm and highly reflective."},
     {0.95f, 0.64f, 0.54f}, 0.22f, 1.f, 0.95f, 0.80f, 0.f, 1.5f, 0.f, 0.f, 0.f, 0, {}, {}},
    {{"Gold", "Metal", "Gold: yellow in the reflection, not just in the colour."},
     {1.00f, 0.78f, 0.34f}, 0.20f, 1.f, 0.95f, 0.85f, 0.f, 1.5f, 0.f, 0.f, 0.f, 0, {}, {}},
    {{"Iron", "Metal", "Rough dark iron with a dull sheen."},
     {0.56f, 0.57f, 0.58f}, 0.62f, 1.f, 0.70f, 0.35f, 0.f, 1.5f, 0.f, 0.f, 0.f, 0, {}, {}},

    {{"Glass", "Glass & liquid", "Clear glass: transparent, refracting at 1.5, a sharp reflection."},
     {0.96f, 0.98f, 1.00f}, 0.02f, 0.f, 0.90f, 0.80f, 0.95f, 1.52f, 0.f, 0.f, 0.f, 0, {}, {}},
    {{"Water", "Glass & liquid", "Still water: clear, refracting at 1.33, reflective at a low angle."},
     {0.80f, 0.90f, 0.95f}, 0.04f, 0.f, 0.70f, 0.60f, 0.90f, 1.33f, 0.f, 0.f, 0.f, 0, {}, {}},
    {{"Ice", "Glass & liquid", "Ice: translucent rather than clear, cold-blue, with a hard shine."},
     {0.85f, 0.93f, 0.98f}, 0.10f, 0.f, 0.70f, 0.55f, 0.45f, 1.31f, 0.60f, 0.f, 0.f, 0, {}, {}},
    {{"Smoke", "Volume", "A true volume: light is extinguished and scattered through the object, not at its surface."},
     {0.55f, 0.55f, 0.57f}, 1.00f, 0.f, 0.05f, 0.f, 0.f, 1.00f, 0.f, 0.f, 0.f, 0, {}, {}, 2.5f},
    {{"Cloud", "Volume", "A bright, dense medium that scatters nearly everything - a cloud you can put a box around."},
     {0.95f, 0.95f, 0.97f}, 1.00f, 0.f, 0.05f, 0.f, 0.f, 1.00f, 0.f, 0.f, 0.f, 0, {}, {}, 6.0f},
    {{"Dust", "Volume", "Thin, warm and forward-scattering: the haze in a shaft of light."},
     {0.80f, 0.72f, 0.58f}, 1.00f, 0.f, 0.05f, 0.f, 0.f, 1.00f, 0.f, 0.f, 0.f, 0, {}, {}, 0.8f},
    {{"Glow", "Glass & liquid", "A surface that lights itself - warm white, no reflection."},
     {1.00f, 0.92f, 0.78f}, 0.90f, 0.f, 0.10f, 0.f, 0.f, 1.5f, 0.f, 2.0f, 0.f, 0, {}, {}},

    {{"Ground", "Ground", "Bare earth, matte and brown, with a little mottling."},
     {0.36f, 0.28f, 0.20f}, 0.95f, 0.f, 0.20f, 0.02f, 0.f, 1.5f, 0.f, 0.f, 0.f, 1,
     {0.26f, 0.20f, 0.14f}, {0.48f, 0.40f, 0.30f}},
    {{"Sand", "Ground", "Pale dry sand, matte, faintly grainy."},
     {0.80f, 0.72f, 0.54f}, 0.95f, 0.f, 0.20f, 0.03f, 0.f, 1.5f, 0.f, 0.f, 0.f, 1,
     {0.70f, 0.62f, 0.46f}, {0.90f, 0.84f, 0.66f}},
    {{"Rock", "Ground", "Grey rock with veining, rough and dull."},
     {0.46f, 0.45f, 0.43f}, 0.90f, 0.f, 0.30f, 0.06f, 0.f, 1.5f, 0.f, 0.f, 0.f, 1,
     {0.30f, 0.30f, 0.30f}, {0.62f, 0.60f, 0.57f}},
    {{"Snow", "Ground", "Fresh snow: bright, soft, light getting just under the surface."},
     {0.96f, 0.97f, 0.99f}, 0.75f, 0.f, 0.25f, 0.10f, 0.f, 1.31f, 0.30f, 0.f, 0.f, 0, {}, {}},
    {{"Swamp", "Ground", "Dark wet mud with a green film and a low shine."},
     {0.18f, 0.22f, 0.12f}, 0.35f, 0.f, 0.45f, 0.25f, 0.f, 1.4f, 0.10f, 0.f, 0.f, 1,
     {0.10f, 0.14f, 0.08f}, {0.30f, 0.36f, 0.18f}},
    {{"Displacement rock", "Relief", "Rock whose surface is actually raised by a fractal, not just shaded."},
     {0.44f, 0.42f, 0.40f}, 0.92f, 0.f, 0.30f, 0.05f, 0.f, 1.5f, 0.f, 0.f, 0.06f, 1,
     {0.28f, 0.27f, 0.26f}, {0.60f, 0.58f, 0.55f}},
    {{"Displacement ground", "Relief", "Earth with real lumps and hollows raised from the surface."},
     {0.38f, 0.30f, 0.22f}, 0.95f, 0.f, 0.20f, 0.02f, 0.f, 1.5f, 0.f, 0.f, 0.03f, 1,
     {0.26f, 0.20f, 0.14f}, {0.50f, 0.42f, 0.32f}},
};

void set_f(gpx::Node *n, const char *key, float v) {
  if (v < 0.f) return;
  if (gpx::Attribute *at = n->attrs.find(key)) at->f = v;
}

} // namespace

const std::vector<MaterialPresetInfo> &material_presets() {
  static std::vector<MaterialPresetInfo> v;
  if (v.empty())
    for (const Preset &p : PRESETS) v.push_back(p.info);
  return v;
}

uint64_t material_preset_create(App &a, const std::string &name, std::string &err) {
  const Preset *p = nullptr;
  for (const Preset &q : PRESETS)
    if (name == q.info.name) p = &q;
  if (!p) {
    err = "no base material named '" + name + "'";
    return 0;
  }
  undo_push(a, std::string("Base material: ") + name);
  std::unique_lock<App::GraphMutex> lk(a.graph_mtx, std::defer_lock);
  if (!lk.try_lock_for(std::chrono::milliseconds(500))) {
    err = "the graph is busy";
    return 0;
  }
  gpx::Graph &g = a.graph;
  // below everything already there, so nothing overlaps
  float x = 200.f, y = 500.f;
  for (auto &n : g.nodes) y = std::max(y, n->pos_y + 320.f);

  gpx::Node *mat = g.add_node("MaterialOutput", x + 260.f, y);
  if (!mat) {
    err = "MaterialOutput is not registered";
    return 0;
  }
  if (gpx::Attribute *nm = mat->attrs.find("name")) nm->s = p->info.name;

  // the colour source
  gpx::Node *src = nullptr;
  if (p->fractal) {
    src = g.add_node("FractalColor", x, y);
    if (src) {
      if (gpx::Attribute *gr = src->attrs.find("gradient")) {
        gr->stops = {{0.f, p->g0[0], p->g0[1], p->g0[2], 1.f},
                     {1.f, p->g1[0], p->g1[1], p->g1[2], 1.f}};
      }
      if (gpx::Attribute *wl = src->attrs.find("wavelength")) wl->f = 0.12f;
      if (gpx::Attribute *oc = src->attrs.find("octaves")) oc->i = 6;
    }
  } else {
    src = g.add_node("FlatColor", x, y);
    if (src) {
      set_f(src, "r", p->rgb[0]);
      set_f(src, "g", p->rgb[1]);
      set_f(src, "b", p->rgb[2]);
    }
  }
  if (src) g.add_link(src->id, "texture", mat->id, "base color");

  // what the substance does with light
  set_f(mat, "roughness", p->roughness);
  set_f(mat, "metallic", p->metallic);
  set_f(mat, "specular", p->specular);
  set_f(mat, "reflection", p->reflection);
  set_f(mat, "transparency", p->transparency);
  set_f(mat, "ior", p->ior);
  set_f(mat, "translucency", p->translucency);
  set_f(mat, "luminous", p->luminous);
  if (p->volume > 0.f) {
    set_f(mat, "vol_density", p->volume);
    set_f(mat, "vol_albedo", p->info.name[0] == 'C' ? 0.98f : (p->info.name[0] == 'D' ? 0.7f : 0.55f));
    set_f(mat, "vol_anisotropy", p->info.name[0] == 'D' ? 0.7f : 0.3f);
    if (gpx::Attribute *ab = mat->attrs.find("vol_absorb"))
      for (int k = 0; k < 3; ++k) ab->col[k] = p->rgb[k];
  }
  if (p->transparency > 0.5f) {
    // glass and water mirror at a low angle; a matte surface does not
    set_f(mat, "reflect_with_angle", 0.4f);
    if (gpx::Attribute *ts = mat->attrs.find("thin_surface")) ts->b = false;
  }

  // real relief: a fractal into the displacement input, at the amplitude
  if (p->displacement > 0.f) {
    gpx::Node *nz = g.add_node("Noise", x, y + 220.f);
    if (nz) {
      if (gpx::Attribute *oc = nz->attrs.find("octaves")) oc->i = 7;
      g.add_link(nz->id, nz->first_out(gpx::DataType::Heightmap)->name, mat->id,
                 "displacement");
    }
    set_f(mat, "displacement", p->displacement);
  }

  a.last_material = mat->id;
  a.graph_layout_serial++;
  g.mark_dirty(mat->id);
  a.request_eval();
  log_info("materials", std::string("base material '") + p->info.name + "' created");
  return mat->id;
}

} // namespace studio
