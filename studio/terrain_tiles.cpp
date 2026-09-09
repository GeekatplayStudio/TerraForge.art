// Geekatplay TerraForge — more than one terrain tile (see terrain_tiles.hpp).
#include "terrain_tiles.hpp"
#include "app.hpp"
#include "renderer_internal.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "terrain_upload.hpp"
#include <glad/gl.h>
#include <algorithm>
#include <utility>

namespace studio {

namespace {

std::vector<TerrainTileGpu> g_extra;
int g_current = -1; // -1: tile 0 (the globals)

// the eight scalars the terrain pass uploads beside matp, in one place
float *mat_slot(RenderSettings &rs, int i) {
  switch (i) {
    case 0: return &rs.mat_roughness;
    case 1: return &rs.mat_metallic;
    case 2: return &rs.mat_specular;
    case 3: return &rs.mat_reflection;
    case 4: return &rs.mat_translucency;
    case 5: return &rs.mat_transparency;
    case 6: return &rs.mat_normal_strength;
    default: return &rs.mat_displacement;
  }
}

void make_tex(GLuint &t, bool mips) {
  if (t) return;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void drop_tex(GLuint &t) {
  if (t) glDeleteTextures(1, &t);
  t = 0;
}

} // namespace

std::vector<TerrainTileGpu> &terrain_tiles_extra() { return g_extra; }
int terrain_tile_count() { return 1 + (int)g_extra.size(); }
int terrain_tile_current() { return g_current; }

int terrain_tiles_bind(App &a) {
  SceneState &sc = scene();
  // the Terrain objects, in scene order; the first is tile 0
  std::vector<int> objs;
  for (int i = 0; i < (int)sc.objects.size(); ++i)
    if (sc.objects[i].type == SceneObject::Terrain) objs.push_back(i);
  auto is_output = [&](uint64_t id) {
    gpx::Node *n = id ? a.graph.find_node(id) : nullptr;
    return n && n->type == "TerrainOutput";
  };
  // tile 0's output: its driver, else the first TerrainOutput not driving
  // another tile
  std::vector<uint64_t> claimed;
  for (size_t k = 1; k < objs.size(); ++k)
    if (is_output(sc.objects[objs[k]].driver_node)) claimed.push_back(sc.objects[objs[k]].driver_node);
  // extra tiles: one entry per further Terrain object that names an output
  std::vector<TerrainTileGpu> next;
  for (size_t k = 1; k < objs.size(); ++k) {
    const SceneObject &o = sc.objects[objs[k]];
    if (!is_output(o.driver_node)) continue;
    // keep the textures of a tile that already exists for this object
    TerrainTileGpu t;
    for (TerrainTileGpu &e : g_extra)
      if (e.output_node == o.driver_node) {
        t = std::move(e);
        e = TerrainTileGpu();
        break;
      }
    t.object = objs[k];
    t.output_node = o.driver_node;
    next.push_back(std::move(t));
  }
  for (TerrainTileGpu &e : g_extra) { // whatever was not carried over
    drop_tex(e.tex_height);
    drop_tex(e.tex_albedo);
    drop_tex(e.tex_place_w);
    drop_tex(e.tex_patch_bounds);
  }
  g_extra = std::move(next);
  return terrain_tile_count();
}

int terrain_tile_for_object(int object) {
  if (object < 0) return -1;
  SceneState &sc = scene();
  for (int i = 0; i < (int)sc.objects.size(); ++i)
    if (sc.objects[i].type == SceneObject::Terrain) {
      if (i == object) return 0;
      break;
    }
  for (size_t k = 0; k < g_extra.size(); ++k)
    if (g_extra[k].object == object) return (int)k + 1;
  return -1;
}

int terrain_tile_object(int tile) {
  if (tile <= 0) {
    SceneState &sc = scene();
    for (int i = 0; i < (int)sc.objects.size(); ++i)
      if (sc.objects[i].type == SceneObject::Terrain) return i;
    return -1;
  }
  return tile - 1 < (int)g_extra.size() ? g_extra[(size_t)tile - 1].object : -1;
}

bool terrain_tile_visible(int tile) {
  const int obj = terrain_tile_object(tile);
  SceneState &sc = scene();
  if (obj < 0 || obj >= (int)sc.objects.size()) return false;
  if (tile > 0 && !g_extra[(size_t)tile - 1].ready) return false;
  return sc.object_visible(sc.objects[(size_t)obj]);
}

void terrain_tile_set_prepared(int tile, TerrainUpload &up) {
  if (tile <= 0 || tile - 1 >= (int)g_extra.size() || !up.height) return;
  TerrainTileGpu &t = g_extra[(size_t)tile - 1];
  const gpx::Heightmap &h = *up.height;
  make_tex(t.tex_height, true);
  glBindTexture(GL_TEXTURE_2D, t.tex_height);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, h.w, h.h, 0, GL_RED, GL_FLOAT, h.v.data());
  glGenerateMipmap(GL_TEXTURE_2D);
  t.hm_w = h.w;
  t.cpu_height = std::move(up.picking);
  t.cpu_patch_bounds = std::move(up.bounds);
  make_tex(t.tex_patch_bounds, false);
  glBindTexture(GL_TEXTURE_2D, t.tex_patch_bounds);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, patch_n - 1, patch_n - 1, 0, GL_RG, GL_FLOAT,
               t.cpu_patch_bounds.data());
  t.has_place_w = up.placement.placed && !up.placement.weight.empty() &&
                  up.placement.weight.w == h.w && up.placement.weight.h == h.h;
  if (t.has_place_w) {
    make_tex(t.tex_place_w, false);
    glBindTexture(GL_TEXTURE_2D, t.tex_place_w);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, h.w, h.h, 0, GL_RED, GL_FLOAT,
                 up.placement.weight.v.data());
  }
  t.has_albedo = up.albedo && !up.albedo->empty();
  if (t.has_albedo) {
    make_tex(t.tex_albedo, true);
    glBindTexture(GL_TEXTURE_2D, t.tex_albedo);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, up.albedo->w, up.albedo->h, 0, GL_RGBA, GL_FLOAT,
                 up.albedo->v.data());
    glGenerateMipmap(GL_TEXTURE_2D);
  }
  t.placement = up.placement;
  t.placement.weight = gpx::Heightmap(); // uploaded; the CPU copy is not needed
  t.mean = up.mean;
  t.ready = true;
  renderer_invalidate_views();
  ++g_shadow_revision;
}

TileSwap::TileSwap(int tile) : tile_(tile), prev_current_(g_current) {
  if (tile <= 0 || tile - 1 >= (int)g_extra.size()) {
    g_current = -1;
    return;
  }
  TerrainTileGpu &t = g_extra[(size_t)tile - 1];
  RenderSettings &rs = render_settings();
  std::swap(tex_height, t.tex_height);
  std::swap(tex_albedo, t.tex_albedo);
  std::swap(tex_place_w, t.tex_place_w);
  std::swap(tex_patch_bounds, t.tex_patch_bounds);
  std::swap(hm_w, t.hm_w);
  std::swap(has_albedo, t.has_albedo);
  std::swap(has_place_w, t.has_place_w);
  std::swap(cpu_patch_bounds, t.cpu_patch_bounds);
  std::swap(cpu_height, t.cpu_height);
  std::swap(g_terrain_mean, t.mean);
  std::swap(rs.matp, t.matp);
  for (int i = 0; i < 8; ++i) std::swap(*mat_slot(rs, i), t.mat[i]);
  g_current = tile;
  swapped_ = true;
}

TileSwap::~TileSwap() {
  if (swapped_) {
    TerrainTileGpu &t = g_extra[(size_t)tile_ - 1];
    RenderSettings &rs = render_settings();
    std::swap(tex_height, t.tex_height);
    std::swap(tex_albedo, t.tex_albedo);
    std::swap(tex_place_w, t.tex_place_w);
    std::swap(tex_patch_bounds, t.tex_patch_bounds);
    std::swap(hm_w, t.hm_w);
    std::swap(has_albedo, t.has_albedo);
    std::swap(has_place_w, t.has_place_w);
    std::swap(cpu_patch_bounds, t.cpu_patch_bounds);
    std::swap(cpu_height, t.cpu_height);
    std::swap(g_terrain_mean, t.mean);
    std::swap(rs.matp, t.matp);
    for (int i = 0; i < 8; ++i) std::swap(*mat_slot(rs, i), t.mat[i]);
  }
  g_current = prev_current_;
}

} // namespace studio
