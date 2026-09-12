// Geekatplay TerraForge - a grown species into the scene.
//
// A PlantSpecies node grows its individual when the graph evaluates
// (engine/nodes/nodes_plant.cpp keeps the mesh in plant_mesh_for). After
// every evaluation this takes that mesh and makes the scene agree: the
// object the root drives is found by driver_node, then adopted by name,
// then created - and never deleted, like every other node-driven object
// (scene_nodes_objects.cpp). The mesh arrives in metres with the foot at
// the origin; the object keeps it at unit height and wears the height as
// its scale, the way the built-in plants do, so the transform gizmo, the
// ground lock and the exporters see an ordinary mesh object.
//
// What makes it a plant to the renderer: the wind and tint streams
// (SceneObject::wind / tint) the mesh shader reads at locations 7 and 8,
// and the wind its species root set (SceneObject::plant_wind). The parts'
// pictures come from the species' materials - made from rules in memory,
// or files on disk decoded here once and kept.
#include "app.hpp"
#include "console.hpp"
#include "gpx/plant.hpp"
#include "plant_species.hpp"
#include "mesh_thumbnail.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "stb_image.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <string>

namespace studio {

namespace {
bool g_wind_preview = true;   // the Plant Editor's switch: the editor's own preview
bool g_view_animates = false; // the window being drawn right now
}
// Read by the mesh passes (renderer_meshes.cpp, renderer_shadow_meshes.cpp).
// Both have to say yes: the Plant Editor's Wind preview switch, and the
// window being drawn, which decides for itself whether plants move in it
// (RenderSettings::ViewConfig::animate_plants, off by default).
bool a_plant_wind_preview() { return g_wind_preview && g_view_animates; }
void plant_wind_preview_set(bool on) { g_wind_preview = on; }
void plant_view_animates_set(bool on) { g_view_animates = on; }

namespace {

float m_to_tile(float m, float size_m) { return m / std::max(size_m, 1.f); }

int find_driven_species(const gpx::Node &n, const std::string &name) {
  SceneState &sc = scene();
  for (int i = 0; i < (int)sc.objects.size(); ++i)
    if (sc.objects[(size_t)i].driver_node == n.id && sc.objects[(size_t)i].type == SceneObject::Mesh) return i;
  if (!name.empty())
    for (int i = 0; i < (int)sc.objects.size(); ++i)
      if (sc.objects[(size_t)i].type == SceneObject::Mesh && sc.objects[(size_t)i].name == name &&
          sc.objects[(size_t)i].driver_node == 0)
        return i;
  return -1;
}

// A picture file decoded once. The alpha map, when separate, is merged in
// as the colour's alpha so the shader cuts leaves the same way for both.
struct Picture {
  std::vector<uint8_t> rgba;
  int w = 0, h = 0;
};
const Picture &picture_of(const std::string &color_map, const std::string &alpha_map) {
  static std::map<std::string, Picture> cache;
  const std::string key = color_map + "|" + alpha_map;
  auto it = cache.find(key);
  if (it != cache.end()) return it->second;
  Picture p;
  int c = 0;
  if (!color_map.empty())
    if (unsigned char *px = stbi_load(color_map.c_str(), &p.w, &p.h, &c, 4)) {
      p.rgba.assign(px, px + (size_t)p.w * p.h * 4);
      stbi_image_free(px);
    }
  if (!alpha_map.empty() && !p.rgba.empty()) {
    int aw = 0, ah = 0, ac = 0;
    if (unsigned char *ap = stbi_load(alpha_map.c_str(), &aw, &ah, &ac, 1)) {
      for (int y = 0; y < p.h; ++y)
        for (int x = 0; x < p.w; ++x) {
          const int ax = x * aw / std::max(p.w, 1), ay = y * ah / std::max(p.h, 1);
          p.rgba[((size_t)y * p.w + x) * 4 + 3] = ap[(size_t)ay * aw + ax];
        }
      stbi_image_free(ap);
    }
  }
  return cache[key] = std::move(p);
}

// The mesh into the object: a triangle soup at unit height, its parts,
// pictures, wind weights and tints.
//
// One scene part per MATERIAL, not per grown part. A plant's mesh carries a
// part for every primitive it grew - a mature oak has thousands - and giving
// each of those its own scene part meant copying the whole leaf picture, and
// uploading its own GL texture, thousands of times over. A 24 m oak ran the
// machine out of memory doing it. The triangles are rebuilt here anyway, so
// they are gathered by the material they wear: four or five runs, four or
// five pictures, four or five draws. The per-primitive names stay on the
// plant's own mesh, which is what the exporters write.
void mesh_to_object(SceneObject &o, const gpx::PlantMesh &m) {
  const float h = std::max(m.height_m, 1e-3f);
  const float inv = 1.f / h;
  const size_t tris = m.idx.size() / 3;
  o.verts.clear();
  o.uvs.clear();
  o.wind.clear();
  o.tint.clear();
  o.verts.reserve(tris * 18);
  o.uvs.reserve(tris * 6);
  o.wind.reserve(tris * 12);
  o.tint.reserve(tris * 12);
  o.parts.clear();
  const bool have_wind = m.wind.size() == m.pos.size() / 3 * 4;
  const bool have_tint = m.tint.size() == m.pos.size() / 3 * 4;

  // the materials actually worn, in the order they first appear
  std::vector<int> order;
  for (const gpx::PlantPart &part : m.parts)
    if (std::find(order.begin(), order.end(), part.material) == order.end()) order.push_back(part.material);

  for (int mat_index : order) {
    SceneObject::Part sp;
    sp.first = (int)(o.verts.size() / 6);
    const gpx::PlantMaterial *mat = mat_index >= 0 && mat_index < (int)m.materials.size()
                                        ? &m.materials[(size_t)mat_index] : nullptr;
    sp.name = mat && !mat->name.empty() ? mat->name : "part";
    if (mat) {
      for (int k = 0; k < 3; ++k) sp.color[k] = mat->color[k];
      // how it reflects and what it lets through, from the material rather
      // than from a constant somewhere in the renderer
      sp.roughness = mat->roughness;
      sp.translucency = mat->translucency;
      if (!mat->rgba.empty() && mat->w > 0) {
        sp.rgba = mat->rgba;
        sp.normal_rgba = mat->normal_rgba;
        sp.rough_rgba = mat->rough_rgba;
        sp.w = mat->w;
        sp.h = mat->h;
      } else if (!mat->color_map.empty()) {
        const Picture &pic = picture_of(mat->color_map, mat->alpha_map);
        if (!pic.rgba.empty()) {
          sp.rgba = pic.rgba;
          sp.w = pic.w;
          sp.h = pic.h;
          // the picture carries the colour; the tint stays neutral
          sp.color[0] = sp.color[1] = sp.color[2] = 1.f;
        }
      }
    }
    for (const gpx::PlantPart &part : m.parts) {
      if (part.material != mat_index) continue;
      sp.double_sided = sp.double_sided || part.double_sided;
      for (uint32_t i = part.first_index; i + 2 < part.first_index + part.index_count && i + 2 < m.idx.size();
           i += 3) {
        for (int c = 0; c < 3; ++c) {
          const uint32_t v = m.idx[i + c];
          if (v * 3 + 2 >= m.pos.size()) continue;
          o.verts.insert(o.verts.end(), {m.pos[v * 3] * inv, m.pos[v * 3 + 1] * inv, m.pos[v * 3 + 2] * inv,
                                         m.nrm[v * 3], m.nrm[v * 3 + 1], m.nrm[v * 3 + 2]});
          o.uvs.insert(o.uvs.end(), {m.uv[v * 2], m.uv[v * 2 + 1]});
          if (have_wind)
            o.wind.insert(o.wind.end(), {m.wind[v * 4], m.wind[v * 4 + 1], m.wind[v * 4 + 2], m.wind[v * 4 + 3]});
          else
            o.wind.insert(o.wind.end(), {0.f, 0.f, 0.f, m.pos[v * 3 + 1] * inv});
          if (have_tint)
            o.tint.insert(o.tint.end(), {m.tint[v * 4], m.tint[v * 4 + 1], m.tint[v * 4 + 2], m.tint[v * 4 + 3]});
          else
            o.tint.insert(o.tint.end(), {1.f, 1.f, 1.f, 1.f});
        }
      }
    }
    sp.count = (int)(o.verts.size() / 6) - sp.first;
    if (sp.count > 0) o.parts.push_back(std::move(sp));
  }
  o.vert_count = (int)(o.verts.size() / 6);
  o.plant = true;
  o.gpu_dirty = true;
  for (SceneObject::Part &p : o.parts) p.tex = 0; // the GL textures die with the old vao
  o.lod_verts[0].clear();
  o.lod_verts[1].clear();
  o.lod_tried = false;
}

gpx::PlantWind wind_of(const gpx::AttrSet &at) {
  gpx::PlantWind w;
  const bool on = at.get_b("receive_wind", true);
  // The world's wind, and the plant's own answer to it.
  //
  // A species says how hard it is blown and how it gusts; where the wind
  // comes from and how strong it is on the day belong to the scene, not to
  // the plant - a wood where every tree leans its own way, while the clouds
  // go a third way and the waves a fourth, is the clearest sign a landscape
  // was assembled rather than seen. So the direction and the gusting come
  // from the atmosphere, and the species' own strength is what it does with
  // them. At the default wind - five metres a second - the scale is one, so
  // a plant blows exactly as it did before; raise the wind and it answers.
  const RenderSettings &rs = render_settings();
  const bool follow = at.get_b("wind_follow", true);
  const float scale = follow ? std::max(rs.wind.speed_ms, 0.f) / 5.f : 1.f;
  w.strength = on ? at.get_f("wind_strength", 0.3f) * scale : 0.f;
  const float a = (follow ? rs.wind.direction_deg : at.get_f("wind_direction", 0.f)) * 0.017453293f;
  w.dir[0] = std::cos(a);
  w.dir[1] = std::sin(a);
  w.breeze = at.get_f("seg_breeze_influence", 0.5f);
  w.breeze_speed = at.get_f("seg_breeze_speed", 1.f);
  w.breeze_randomness = at.get_f("seg_breeze_randomness", 0.5f);
  w.flutter = at.get_f("leaf_flutter_influence", 0.5f);
  w.flutter_speed = at.get_f("leaf_flutter_speed", 3.f);
  w.gust = follow ? rs.wind.gust_strength : at.get_f("gust_amplitude", 0.3f);
  w.gust_frequency = follow ? rs.wind.gust_frequency : at.get_f("gust_frequency", 0.15f);
  return w;
}

} // namespace

void apply_species(App &a, gpx::Node &n, float size_m) {
  (void)a;
  std::shared_ptr<const gpx::PlantMesh> mesh = gpx::plant_mesh_for(n.id);
  std::string name = n.attrs.get_s("object");
  if (name.empty()) name = n.attrs.get_s("name");
  if (name.empty()) name = "Plant";
  int idx = find_driven_species(n, name);
  if (idx < 0) {
    SceneObject o;
    o.type = SceneObject::Mesh;
    o.name = name;
    o.path = "species:" + std::to_string(n.id);
    o.color[0] = o.color[1] = o.color[2] = 1.f;
    o.driver_node = n.id;
    o.plant = true;
    scene().objects.push_back(std::move(o));
    idx = (int)scene().objects.size() - 1;
    a.scene_selection_serial++;
  }
  SceneObject &o = scene().objects[(size_t)idx];
  o.driver_node = n.id;
  if (!n.attrs.get_s("object").empty() || o.name.empty()) o.name = name;
  o.plant = true;
  o.plant_wind = wind_of(n.attrs);
  // the geometry, when the species grew again
  static std::map<uint64_t, const gpx::PlantMesh *> last;
  const float height_m = mesh ? std::max(mesh->height_m, 1e-3f) : 1.f;
  if (mesh && last[n.id] != mesh.get()) {
    last[n.id] = mesh.get();
    mesh_to_object(o, *mesh);
    o.path = "species:" + std::to_string(n.id);
  }
  // the transform: the node drives the object when the node changed since
  // it last did; otherwise the object's own edits are the truth and the node
  // follows (the same two-way rule as every driven object)
  std::vector<float> now(6);
  now[0] = n.attrs.get_f("x_m", 2500.f);
  now[1] = n.attrs.get_f("y_m", 0.f);
  now[2] = n.attrs.get_f("z_m", 2500.f);
  now[3] = n.attrs.get_f("heading", 0.f);
  now[4] = n.attrs.get_f("scale", 1.f) * height_m;
  now[5] = n.attrs.get_b("visible", true) ? 1.f : 0.f;
  const bool node_changed = o.driver_stamp != now;
  if (node_changed) {
    o.pos[0] = m_to_tile(now[0], size_m);
    o.pos[1] = m_to_tile(now[1], size_m);
    o.pos[2] = m_to_tile(now[2], size_m);
    o.yaw = now[3];
    o.scale = m_to_tile(now[4], size_m);
    o.visible = now[5] > 0.5f;
    o.ground_lock = n.attrs.get_b("grounded", true);
  } else {
    if (gpx::Attribute *x = n.attrs.find("x_m")) x->f = o.pos[0] * size_m;
    if (gpx::Attribute *y = n.attrs.find("y_m")) y->f = o.pos[1] * size_m;
    if (gpx::Attribute *z = n.attrs.find("z_m")) z->f = o.pos[2] * size_m;
    if (gpx::Attribute *h = n.attrs.find("heading")) h->f = o.yaw;
    if (gpx::Attribute *v = n.attrs.find("visible")) v->b = o.visible;
    now[0] = o.pos[0] * size_m;
    now[1] = o.pos[1] * size_m;
    now[2] = o.pos[2] * size_m;
    now[3] = o.yaw;
    now[5] = o.visible ? 1.f : 0.f;
  }
  o.driver_stamp = now;
  if (!previews_get(n.id) && o.vert_count > 0) previews_set_image(n.id, mesh_thumbnail(o, 112), 112, 112);
}

} // namespace studio
