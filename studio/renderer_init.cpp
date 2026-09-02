// Geekatplay TerraForge - renderer resources: GL programs, grids, preview
// meshes, terrain upload and material maps; init and shutdown. Split from
// renderer.cpp for the 500-line module rule.
#include "renderer_internal.hpp"
#include "app.hpp"
#include "console.hpp"
#include "cloud_noise.hpp"
#include "planet_renderer.hpp"
#include "scene.hpp"
#include "gpu_timer.hpp"
#include "terrain_cull.hpp"
#include "gpx/camera_math.hpp"
#include "gpx/field_glsl.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "stb_image_write.h"
#include "renderer_shaders.hpp"

namespace studio {


void make_sphere() {
  std::vector<float> v;
  const int RINGS = 12, SECT = 18;
  auto pt = [&](int r, int s, float *o) {
    float phi = float(r) / RINGS * 3.14159265f;
    float th = float(s) / SECT * 6.2831853f;
    o[0] = std::sin(phi) * std::cos(th);
    o[1] = std::cos(phi);
    o[2] = std::sin(phi) * std::sin(th);
  };
  for (int r = 0; r < RINGS; ++r)
    for (int s = 0; s < SECT; ++s) {
      float a[3], b[3], c[3], d[3];
      pt(r, s, a); pt(r + 1, s, b); pt(r + 1, s + 1, c); pt(r, s + 1, d);
      const float *tri[6] = {a, b, c, a, c, d};
      for (int i = 0; i < 6; ++i)
        v.insert(v.end(), {tri[i][0], tri[i][1], tri[i][2], tri[i][0], tri[i][1],
                           tri[i][2]});
    }
  sphere_verts = (int)(v.size() / 6);
  glGenVertexArrays(1, &vao_sphere);
  glBindVertexArray(vao_sphere);
  glGenBuffers(1, &vbo_sphere);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_sphere);
  glBufferData(GL_ARRAY_BUFFER, v.size() * 4, v.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void *)12);
  glBindVertexArray(0);
}


void upload_prev_mesh(int slot, const std::vector<float> &v) {
  prev_verts[slot] = (int)(v.size() / 8);
  glGenVertexArrays(1, &prev_vao[slot]);
  glBindVertexArray(prev_vao[slot]);
  glGenBuffers(1, &prev_vbo[slot]);
  glBindBuffer(GL_ARRAY_BUFFER, prev_vbo[slot]);
  glBufferData(GL_ARRAY_BUFFER, v.size() * 4, v.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void *)12);
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void *)24);
  glBindVertexArray(0);
}


void make_preview_shapes() {
  // sphere with proper spherical UVs
  {
    std::vector<float> v;
    const int RINGS = 24, SECT = 36;
    auto emit = [&](int r, int s) {
      float phi = float(r) / RINGS * 3.14159265f;
      float th = float(s) / SECT * 6.2831853f;
      float x = std::sin(phi) * std::cos(th);
      float y = std::cos(phi);
      float z = std::sin(phi) * std::sin(th);
      v.insert(v.end(), {x, y, z, x, y, z, float(s) / SECT * 3.f,
                         float(r) / RINGS * 1.5f});
    };
    for (int r = 0; r < RINGS; ++r)
      for (int s = 0; s < SECT; ++s) {
        emit(r, s); emit(r + 1, s); emit(r + 1, s + 1);
        emit(r, s); emit(r + 1, s + 1); emit(r, s + 1);
      }
    upload_prev_mesh(0, v);
  }
  // cube with planar per-face UVs
  {
    std::vector<float> v;
    const float N[6][3] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0},
                           {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    for (int f = 0; f < 6; ++f) {
      const float *n = N[f];
      float t[3] = {n[1], n[2], n[0]}; // any perpendicular
      float b[3] = {n[1] * t[2] - n[2] * t[1], n[2] * t[0] - n[0] * t[2],
                    n[0] * t[1] - n[1] * t[0]};
      const float S = 0.72f;
      auto corner = [&](float u, float w, float *o) {
        for (int k = 0; k < 3; ++k)
          o[k] = (n[k] + t[k] * u + b[k] * w) * S;
      };
      float c[4][3];
      corner(-1, -1, c[0]); corner(1, -1, c[1]);
      corner(1, 1, c[2]); corner(-1, 1, c[3]);
      const int idx[6] = {0, 1, 2, 0, 2, 3};
      const float uv[4][2] = {{0, 0}, {1.2f, 0}, {1.2f, 1.2f}, {0, 1.2f}};
      for (int i : idx)
        v.insert(v.end(), {c[i][0], c[i][1], c[i][2], n[0], n[1], n[2],
                           uv[i][0], uv[i][1]});
    }
    upload_prev_mesh(1, v);
  }
  // flat: a full-frame quad facing the camera
  {
    std::vector<float> v;
    const float Q[4][2] = {{-1.05f, -1.05f}, {1.05f, -1.05f},
                           {1.05f, 1.05f}, {-1.05f, 1.05f}};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int i : idx)
      v.insert(v.end(), {Q[i][0], Q[i][1], 0.f, 0.f, 0.f, 1.f,
                         Q[i][0] * 0.5f + 0.5f, Q[i][1] * 0.5f + 0.5f});
    upload_prev_mesh(2, v);
  }
}


bool renderer_init() {
  std::string fs_terrain = inject_sky(FS_TERRAIN_SRC);
  std::string fs_sky = inject_sky(FS_SKY_SRC);
  std::string fs_water = inject_sky(FS_WATER);
  {
    // builds prog_terrain and, if the driver takes it, prog_terrain_tess
    std::string terr;
    rebuild_terrain_program(terr);
  }
  prog_water = link_prog(VS_WATER, fs_water.c_str());
  prog_sky = link_prog(VS_SKY, fs_sky.c_str());
  prog_lines = link_prog(VS_LINES, FS_LINES);
  prog_bg = link_prog(VS_BG, FS_BG);
  prog_mesh = link_prog(VS_MESH, FS_MESH);
  prog_gizmo = link_prog(VS_GIZMO, FS_GIZMO);
  prog_matprev = link_prog(VS_MATPREV, FS_MATPREV);
  make_preview_shapes();
  planet_renderer_init();

  // terrain grid
  std::vector<float> verts;
  verts.reserve((size_t)grid_n * grid_n * 2);
  for (int y = 0; y < grid_n; ++y)
    for (int x = 0; x < grid_n; ++x) {
      verts.push_back(x / float(grid_n - 1));
      verts.push_back(y / float(grid_n - 1));
    }
  std::vector<unsigned> idx;
  idx.reserve((size_t)(grid_n - 1) * (grid_n - 1) * 6);
  for (int y = 0; y < grid_n - 1; ++y)
    for (int x = 0; x < grid_n - 1; ++x) {
      unsigned i = y * grid_n + x;
      idx.insert(idx.end(), {i, i + (unsigned)grid_n, i + 1, i + 1,
                             i + (unsigned)grid_n, i + (unsigned)grid_n + 1});
    }
  index_count = (int)idx.size();
  glGenVertexArrays(1, &vao_grid);
  glBindVertexArray(vao_grid);
  glGenBuffers(1, &vbo_grid);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_grid);
  glBufferData(GL_ARRAY_BUFFER, verts.size() * 4, verts.data(), GL_STATIC_DRAW);
  glGenBuffers(1, &ebo_grid);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_grid);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * 4, idx.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, nullptr);
  glBindVertexArray(0);

  // The coarse patch grid the tessellator subdivides. Four indices per quad,
  // corners counter-clockwise starting at the origin, which is the order the
  // evaluation shader's bilinear interpolation assumes.
  {
    std::vector<float> pv;
    pv.reserve((size_t)patch_n * patch_n * 2);
    for (int y = 0; y < patch_n; ++y)
      for (int x = 0; x < patch_n; ++x) {
        pv.push_back(x / float(patch_n - 1));
        pv.push_back(y / float(patch_n - 1));
      }
    std::vector<unsigned> pi;
    pi.reserve((size_t)(patch_n - 1) * (patch_n - 1) * 4);
    for (int y = 0; y < patch_n - 1; ++y)
      for (int x = 0; x < patch_n - 1; ++x) {
        unsigned i = y * patch_n + x;
        pi.insert(pi.end(), {i, i + 1, i + 1 + (unsigned)patch_n,
                             i + (unsigned)patch_n});
      }
    patch_index_count = (int)pi.size();
    glGenVertexArrays(1, &vao_patch);
    glBindVertexArray(vao_patch);
    glGenBuffers(1, &vbo_patch);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_patch);
    glBufferData(GL_ARRAY_BUFFER, pv.size() * 4, pv.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &ibo_patch);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_patch);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, pi.size() * 4, pi.data(),
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, nullptr);
    glBindVertexArray(0);

    // One texel per patch, holding that patch's height range. NEAREST and
    // CLAMP: a filtered bound would be an average of two patches, which is not
    // a bound at all.
    glGenTextures(1, &tex_patch_bounds);
    glBindTexture(GL_TEXTURE_2D, tex_patch_bounds);
    std::vector<float> flat((size_t)(patch_n - 1) * (patch_n - 1) * 2, 0.f);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, patch_n - 1, patch_n - 1, 0, GL_RG,
                 GL_FLOAT, flat.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  glGenVertexArrays(1, &vao_quad);

  // ground grid lines
  {
    std::vector<float> lv;
    const int DIV = 20;
    const float y = 0.0005f;
    for (int i = 0; i <= DIV; ++i) {
      float t = i / float(DIV);
      lv.insert(lv.end(), {t, y, 0.f, t, y, 1.f});
      lv.insert(lv.end(), {0.f, y, t, 1.f, y, t});
    }
    line_vert_count = (int)lv.size() / 3;
    glGenVertexArrays(1, &vao_lines);
    glBindVertexArray(vao_lines);
    glGenBuffers(1, &vbo_lines);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_lines);
    glBufferData(GL_ARRAY_BUFFER, lv.size() * 4, lv.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, nullptr);
    glBindVertexArray(0);
  }
  // dynamic outline buffer
  glGenVertexArrays(1, &vao_dyn);
  glBindVertexArray(vao_dyn);
  glGenBuffers(1, &vbo_dyn);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_dyn);
  glBufferData(GL_ARRAY_BUFFER, 256 * 3 * 4, nullptr, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, nullptr);
  glBindVertexArray(0);
  make_sphere();

  auto mktex = [](GLuint &t, bool mips) {
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  };
  mktex(tex_height, false);
  mktex(tex_albedo, true);
  mktex(tex_normal, true);
  mktex(tex_rough, true);
  mktex(tex_disp, false);

  cloud_noise_build(tex_cloud_shape, tex_cloud_detail);

  // shadow map
  glGenTextures(1, &shadow_tex);
  glBindTexture(GL_TEXTURE_2D, shadow_tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SHADOW_RES, SHADOW_RES, 0,
               GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
  float border[4] = {1, 1, 1, 1};
  glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
  glGenFramebuffers(1, &shadow_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                         shadow_tex, 0);
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return true;
}


void renderer_shutdown() {
  glDeleteProgram(prog_terrain);
  glDeleteProgram(prog_water);
  glDeleteProgram(prog_sky);
  glDeleteProgram(prog_depth);
  glDeleteProgram(prog_lines);
  glDeleteProgram(prog_bg);
  glDeleteProgram(prog_mesh);
  glDeleteProgram(prog_gizmo);
}


void renderer_set_terrain(const gpx::Heightmap &h, const gpx::TextureRGBA *albedo) {
  // Upload what the graph produced, at the height it produced it.
  //
  // This used to stretch every heightmap to fill 0..1, which quietly threw
  // away absolute altitude: a terrain spanning 0.4-0.5 displayed identically
  // to one spanning 0-1, so sea level, the snow line and every altitude-keyed
  // material meant nothing. Flat ground was worse still — min equals max, so
  // the whole tile collapsed to zero and sank under the sea, which made a flat
  // planet surface impossible to show at all.
  //
  // Normalising is the graph's job and it is already exposed there, on by
  // default: TerrainOutput's "Remap to range" and every node's own output
  // block. The renderer's business is to show what it is handed.
  gpx::Heightmap norm = h;
  hm_w = norm.w;
  // the level the surrounding ground should settle to, so the two meet
  double sum = 0;
  for (float v : norm.v) sum += v;
  g_terrain_mean = norm.v.empty() ? 0.f : (float)(sum / norm.v.size());
  glBindTexture(GL_TEXTURE_2D, tex_height);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, norm.w, norm.h, 0, GL_RED, GL_FLOAT,
               norm.v.data());
  // keep a CPU copy (downsampled) for picking
  cpu_height = norm.w > 256 ? norm.resampled(256, 256) : norm;
  // Bounds for per-patch culling. Built from the full-resolution map, not the
  // picking copy: a bound taken from a downsampled height can miss a spike and
  // cull a patch that is on screen, which reads as a hole in the terrain.
  cpu_patch_bounds = patch_height_bounds(norm, patch_n - 1);
  if (tex_patch_bounds) {
    glBindTexture(GL_TEXTURE_2D, tex_patch_bounds);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, patch_n - 1, patch_n - 1, 0, GL_RG,
                 GL_FLOAT, cpu_patch_bounds.data());
  }
  has_albedo = albedo && !albedo->empty();
  if (has_albedo) {
    glBindTexture(GL_TEXTURE_2D, tex_albedo);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, albedo->w, albedo->h, 0, GL_RGBA,
                 GL_FLOAT, albedo->v.data());
    glGenerateMipmap(GL_TEXTURE_2D);
  }
}


// Uploading three textures every frame was costing far more than the whole
// rest of the frame; only re-upload when the source data actually changed.
void renderer_set_material_maps(const void *normal, const void *roughness,
                                const void *displacement, unsigned long long version) {
  static unsigned long long last_version = ~0ull;
  static const void *last_ptrs[3] = {nullptr, nullptr, nullptr};
  const void *ptrs[3] = {normal, roughness, displacement};
  if (version == last_version && ptrs[0] == last_ptrs[0] &&
      ptrs[1] == last_ptrs[1] && ptrs[2] == last_ptrs[2])
    return;
  last_version = version;
  for (int i = 0; i < 3; ++i) last_ptrs[i] = ptrs[i];

  auto up = [](GLuint tex, const gpx::TextureRGBA *t, bool mips, bool &flag) {
    flag = t && !t->empty();
    if (!flag) return;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t->w, t->h, 0, GL_RGBA, GL_FLOAT,
                 t->v.data());
    if (mips) glGenerateMipmap(GL_TEXTURE_2D);
  };
  up(tex_normal, (const gpx::TextureRGBA *)normal, true, has_normal_map);
  up(tex_rough, (const gpx::TextureRGBA *)roughness, true, has_rough_map);
  up(tex_disp, (const gpx::TextureRGBA *)displacement, false, has_disp_map);
}

} // namespace studio
