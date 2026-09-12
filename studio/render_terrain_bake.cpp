// Geekatplay TerraForge - baking the viewport's world for the offline
// renderers. See render_terrain_bake.hpp for why this exists.
#include "render_terrain_bake.hpp"
#include "app.hpp"
#include "obj_text.hpp"
#include "planet_place.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "terrain_relief.hpp"
#include "terrain_tiles.hpp"
#include "terrain_xform.hpp"
#include "water_surface.hpp"
#include "world_shape.hpp"
#include "gpx/heightmap.hpp"
#include "gpx/material_params.hpp"
#include "gpx/parallel.hpp"
#include "gpx/planet_math.hpp"
#include "gpx/planet_palette.hpp"
#include "stb_image_write.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace studio {
namespace fs = std::filesystem;

const gpx::Heightmap *app_placed_terrain(); // app_services.cpp
float renderer_ground_base();               // renderer_scene.cpp

namespace {

// The surface, as the viewport's vertex stage builds it: the placed tile
// inside its own square, the planet's relief outside, blended over the same
// narrow ring the shader blends over (join_h in planet_shaders.cpp). World
// units throughout.
struct Ground {
  const gpx::Heightmap *tile = nullptr;
  std::vector<gpx::planet::Layer> layers;
  TerrainXform tx;
  float hscale = 1.f, amp = 1.f, base = 0.f;
  float radius = 0.f;
  gpx::planet::Shape shape;
  // The grit the viewport lays over all of it (terrain_relief.hpp), at as
  // many octaves as the tile's grid can carry. Left out, the rendered ground
  // was the heightmap alone: metres off the viewport's either way, and a
  // plant stood on the viewport's ground floated or sank in the render.
  ReliefDials grit;
  float grit_octaves = 0.f;

  float relief(float u, float v) const {
    if (layers.empty()) return base;
    const float d[3] = {u, 0.37f, v};
    return gpx::planet::heightf(d, layers.data(), (int)layers.size(), 9.f) *
               amp + base;
  }
  float wet(float u, float v) const {
    if (layers.empty()) return 0.f;
    const float d[3] = {u, 0.37f, v};
    float w = 0.f;
    gpx::planet::heightf(d, layers.data(), (int)layers.size(), 9.f, &w);
    return w;
  }
  // height at a point of the flat world, tile units in, world units out
  float at(float x, float z) const {
    return shape_at(x, z) + relief_at(x, z, grit, grit_octaves);
  }
  float shape_at(float x, float z) const {
    const float proc = relief(x, z);
    if (!tile || tile->empty()) return proc;
    float tu = x, tv = z;
    if (tx.on && !terrain_xform_unapply_xz(tx, x, z, tu, tv)) return proc;
    const float cu = std::clamp(tu, 0.f, 1.f), cv = std::clamp(tv, 0.f, 1.f);
    const float du = tu - cu, dv = tv - cv;
    const float dout = std::sqrt(du * du + dv * dv);
    if (dout > 0.06f) return proc;
    const float s = gpx::planet::pl_smoothstep(0.f, 0.06f, dout);
    // with the tile's own height and altitude, as every viewport pass draws
    // it (tile_world_y, u_txi_y): dropped, the rendered ground sat tens of
    // metres off the viewport's and its shores and colour bands moved
    const float th = tile->sample(cu, cv) * hscale * (tx.on ? tx.scl[1] : 1.f) +
                     (tx.on ? tx.pos[1] : 0.f);
    return th + (proc - th) * s;
  }
  void place(float x, float z, float out[3]) const {
    const float h = at(x, z);
    if (radius > 0.f)
      gpx::planet::sphere_place(x, z, h, radius, shape, out);
    else { out[0] = x; out[1] = h; out[2] = z; }
  }
};

// The grid the surround is drawn on: vertex density spent near the tile,
// reaching `extent` tiles at the rim. The same cubic concentration as the
// shader's, so the two sample the relief at the same places.
inline float concentrate(float p, float extent) {
  const float q = p * p * p * p;
  return p * (0.5f + (extent - 0.5f) * q);
}

// `keep`, when given, says per grid cell ((side-1)^2, row by row) whether its
// two faces are written.
void write_obj(const fs::path &path, const std::vector<float> &pos,
               const std::vector<float> &nrm, const std::vector<float> &uv,
               int side, const std::vector<uint8_t> *keep = nullptr) {
  // Six fixed decimals on half a million vertices is over a hundred
  // megabytes of text for one frame. Five significant digits places a vertex
  // to a twenty-thousandth of a tile - a quarter of a metre on the default
  // world, far finer than the grid carrying it - at a quarter of the cost to
  // write and to parse.
  ObjText f(path.string(), 5);
  f.text("# Geekatplay TerraForge render mesh\n");
  const size_t n = pos.size() / 3;
  for (size_t i = 0; i < n; ++i) f.line3("v", pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]);
  for (size_t i = 0; i < n; ++i) f.line3("vn", nrm[i * 3], nrm[i * 3 + 1], nrm[i * 3 + 2]);
  for (size_t i = 0; i < n; ++i) f.text("vt ").num(uv[i * 2]).ch(' ').num(uv[i * 2 + 1]).ch('\n');
  auto corner = [&f](int k) { f.ch(' ').num(k).ch('/').num(k).ch('/').num(k); };
  for (int y = 0; y < side - 1; ++y)
    for (int x = 0; x < side - 1; ++x) {
      if (keep && !(*keep)[(size_t)y * (side - 1) + x]) continue;
      const int a = y * side + x + 1, b = a + side, c = a + 1, d = b + 1;
      f.ch('f');
      corner(a);
      corner(b);
      corner(c);
      f.text("\nf");
      corner(c);
      corner(b);
      corner(d);
      f.ch('\n');
    }
}

// One grid, from a map of grid parameter (0..1 each way) to a point of the
// flat world. Normals come from the surface itself rather than from the
// triangles, so a coarse grid still shades like the fine one beside it.
template <class Map>
void build_grid(const Ground &g, int side, Map map, std::vector<float> &pos,
                std::vector<float> &nrm, std::vector<float> &uv) {
  const size_t n = (size_t)side * side;
  pos.resize(n * 3);
  nrm.resize(n * 3);
  uv.resize(n * 2);
  gpx::parallel_rows(side, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < side; ++x) {
        const float a = x / float(side - 1), b = y / float(side - 1);
        float wx, wz;
        map(a, b, wx, wz);
        const size_t i = (size_t)y * side + x;
        g.place(wx, wz, &pos[i * 3]);
        uv[i * 2] = a;
        uv[i * 2 + 1] = 1.f - b;
        // the step in world units this grid cell spans, so the difference
        // follows the surface at the scale the mesh resolves it
        float ax, az, bx, bz;
        map(std::min(a + 1.f / (side - 1), 1.f), b, ax, az);
        map(a, std::min(b + 1.f / (side - 1), 1.f), bx, bz);
        const float e = std::max(std::max(std::fabs(ax - wx), std::fabs(bz - wz)),
                                 1e-5f) * 0.5f;
        const float hx = g.at(wx + e, wz) - g.at(wx - e, wz);
        const float hz = g.at(wx, wz + e) - g.at(wx, wz - e);
        float nx = -hx, ny = 2.f * e, nz = -hz;
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        nx /= len; ny /= len; nz /= len;
        if (g.radius > 0.f) {
          float east[3], up[3], north[3];
          gpx::planet::sphere_frame(wx, wz, g.radius, g.shape, east, up, north);
          for (int k = 0; k < 3; ++k)
            nrm[i * 3 + k] = east[k] * nx + up[k] * ny + north[k] * nz;
        } else {
          nrm[i * 3] = nx; nrm[i * 3 + 1] = ny; nrm[i * 3 + 2] = nz;
        }
      }
  });
}

// The tile's own colour, where it has one: the texture the viewport's tile
// is painted with (sRGB), the material's weights for it, and the placement's
// blend weight toward the palette (planet_place.hpp).
struct TileColour {
  const gpx::TextureRGBA *tex = nullptr;
  const gpx::MaterialParams *params = nullptr;
  const gpx::Heightmap *weight = nullptr;
};

// bilinear, clamped, uv in 0..1 with row 0 at v = 0 (the heightmap's order)
void sample_rgb(const gpx::TextureRGBA &t, float u, float v, float out[3]) {
  const float fx = std::clamp(u, 0.f, 1.f) * float(t.w - 1);
  const float fy = std::clamp(v, 0.f, 1.f) * float(t.h - 1);
  const int x0 = (int)fx, y0 = (int)fy;
  const int x1 = std::min(x0 + 1, t.w - 1), y1 = std::min(y0 + 1, t.h - 1);
  const float ax = fx - float(x0), ay = fy - float(y0);
  for (int k = 0; k < 3; ++k) {
    const float top = t.px(x0, y0)[k] * (1.f - ax) + t.px(x1, y0)[k] * ax;
    const float bot = t.px(x0, y1)[k] * (1.f - ax) + t.px(x1, y1)[k] * ax;
    out[k] = top * (1.f - ay) + bot * ay;
  }
}

// mat_albedo (renderer_matparams.cpp), on the CPU: tint and gain, saturation,
// and the colour blend
void material_albedo(const gpx::MaterialParams &p, float a[3]) {
  for (int k = 0; k < 3; ++k) a[k] *= p.tint[k] * p.gain;
  const float l = a[0] * 0.299f + a[1] * 0.587f + a[2] * 0.114f;
  for (int k = 0; k < 3; ++k) a[k] = std::max(l + (a[k] - l) * p.saturation, 0.f);
  const float amount = p.color_blend ? p.blend_amount : 0.f;
  if (amount > 0.f)
    for (int k = 0; k < 3; ++k) {
      const float product = a[k] * (1.f + (p.blend_color[k] - 1.f) * amount);
      a[k] = product + (p.blend_color[k] - product) * p.blend_mask * amount;
    }
}

// The ground's colour, by the same palette the viewport paints with - and on
// the tile, its material over the palette by the placement's weight, as the
// terrain shader mixes them.
template <class Map>
void bake_albedo(const Ground &g, int side, float water_level, float lat,
                 Map map, std::vector<uint8_t> &rgba, const TileColour *tile = nullptr) {
  rgba.assign((size_t)side * side * 4, 255);
  const bool has_tex = tile && tile->tex && tile->tex->w > 0 && tile->tex->h > 0;
  const float span = std::max(g.hscale - water_level, 0.02f);
  gpx::parallel_rows(side, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < side; ++x) {
        const float a = (x + 0.5f) / float(side), b = (y + 0.5f) / float(side);
        float wx, wz;
        map(a, b, wx, wz);
        const float h = g.at(wx, wz);
        const float e = 1.f / float(side) * 2.f;
        const float hx = g.at(wx + e, wz) - g.at(wx - e, wz);
        const float hz = g.at(wx, wz + e) - g.at(wx, wz - e);
        const float ny = 2.f * e / std::sqrt(hx * hx + hz * hz + 4.f * e * e);
        float c[3];
        gpx::planet::palette((h - water_level) / span, 1.f - ny, lat,
                             g.wet(wx, wz), 0.62f,
                             gpx::planet::palette_var(wx, wz), c);
        if (has_tex) {
          float m[3];
          sample_rgb(*tile->tex, a, b, m);
          for (int k = 0; k < 3; ++k) m[k] = std::pow(std::clamp(m[k], 0.f, 1.f), 2.2f);
          if (tile->params) material_albedo(*tile->params, m);
          const float w = tile->weight && !tile->weight->empty()
                              ? std::clamp(tile->weight->sample(a, b), 0.f, 1.f)
                              : 1.f;
          for (int k = 0; k < 3; ++k) c[k] += (m[k] - c[k]) * w;
        }
        // The texture is sampled as sRGB by the engines; the palette is
        // linear. Row y holds grid row y: the mesh's v is 1 - b and the
        // engines read v up from the image's bottom, so the two meet. Written
        // upside down as well, the palette came out mirrored north to south
        // against the ground it coloured.
        uint8_t *px = &rgba[(((size_t)y * side) + x) * 4];
        for (int k = 0; k < 3; ++k)
          px[k] = (uint8_t)std::lround(
              std::clamp(std::pow(std::clamp(c[k], 0.f, 1.f), 1.f / 2.2f), 0.f, 1.f) * 255.f);
        px[3] = 255;
      }
  });
}

} // namespace

bool baked_tile_height(const BakedTerrain &b, float x, float z, float &h) {
  const int n = b.tile_side;
  if (n < 2 || b.tile_heights.size() != (size_t)n * n || x < 0.f || x > 1.f || z < 0.f || z > 1.f)
    return false;
  const float gx = x * float(n - 1), gz = z * float(n - 1);
  const int cx = std::min((int)gx, n - 2), cz = std::min((int)gz, n - 2);
  const float fx = gx - float(cx), fz = gz - float(cz);
  auto H = [&](int i, int j) { return b.tile_heights[(size_t)j * n + i]; };
  // the two faces write_obj lays in each cell, split from (x, y+1) to (x+1, y)
  if (fx + fz <= 1.f)
    h = H(cx, cz) + (H(cx + 1, cz) - H(cx, cz)) * fx + (H(cx, cz + 1) - H(cx, cz)) * fz;
  else
    h = H(cx + 1, cz + 1) + (H(cx, cz + 1) - H(cx + 1, cz + 1)) * (1.f - fx) +
        (H(cx + 1, cz) - H(cx + 1, cz + 1)) * (1.f - fz);
  return true;
}

BakedTerrain render_bake_terrain(App &a, const std::string &dirs, int tile_side,
                                 int surround_side, int albedo_side,
                                 const gpx::TextureRGBA *material, const float *eye) {
  BakedTerrain out;
  const fs::path dir(dirs);
  const RenderSettings &rs = render_settings();
  Ground g;
  g.tile = app_placed_terrain();
  if (!g.tile || g.tile->empty()) {
    out.err = "terrain not computed yet";
    return out;
  }
  // the face tile 0 stands on: its placement reads that face's ground
  int side = 0;
  {
    const int obj = terrain_tile_object(0);
    if (obj >= 0 && obj < (int)scene().objects.size())
      side = object_side(rs, scene().objects[(size_t)obj]);
  }
  g.layers = planet_home_layers(side);
  g.hscale = rs.height_scale;
  g.amp = rs.height_scale * 1.2f;
  g.base = renderer_ground_base();
  // A camera frames a picture, and a picture of a world shows its curve: the
  // render uses the radius every camera view draws with, never the flat
  // modelling one.
  g.radius = rs.planet_radius;
  g.shape = world_shape(rs);
  g.grit = relief_dials(rs);
  g.grit_octaves = relief_octaves_for_grid(float(tile_side - 1), g.grit.scale);
  {
    const int obj = terrain_tile_object(0);
    if (obj >= 0 && obj < (int)scene().objects.size())
      g.tx = terrain_xform_of(scene().objects[(size_t)obj], rs.height_scale);
  }
  const float water = rs.show_water ? rs.water_level * rs.height_scale : 0.f;
  const float lat = std::fabs(rs.latitude) / 90.f;

  // the tile, over its own square
  {
    auto map = [&](float a, float b, float &x, float &z) {
      if (g.tx.on) {
        float p[3] = {a, 0.f, b};
        terrain_xform_apply(g.tx, p);
        x = p[0]; z = p[2];
      } else { x = a; z = b; }
    };
    std::vector<float> pos, nrm, uv;
    build_grid(g, tile_side, map, pos, nrm, uv);
    write_obj(dir / "terrain.obj", pos, nrm, uv, tile_side);
    // the flat heights, for seating what stands on the ground (baked_tile_height)
    if (!g.tx.on) {
      out.tile_side = tile_side;
      out.tile_heights.resize((size_t)tile_side * tile_side);
      gpx::parallel_rows(tile_side, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < tile_side; ++x)
            out.tile_heights[(size_t)y * tile_side + x] =
                g.at(x / float(tile_side - 1), y / float(tile_side - 1));
      });
    }
    std::vector<uint8_t> rgba;
    TileColour tc;
    tc.tex = material;
    tc.params = &rs.matp;
    const PlaceResult &placed = planet_place_last();
    if (placed.placed && !placed.weight.empty()) tc.weight = &placed.weight;
    bake_albedo(g, albedo_side, water, lat, map, rgba, &tc);
    stbi_write_png((dir / "albedo.png").string().c_str(), albedo_side,
                   albedo_side, 4, rgba.data(), albedo_side * 4);
    out.tile_obj = (dir / "terrain.obj").string();
    out.tile_albedo = (dir / "albedo.png").string();
  }
  // and the ground beyond it, out to the horizon
  {
    const float extent = 30.5f;
    // The grid gathers its vertices about the ground under the camera, as
    // the sea's rings do (render_water_bake.cpp) - about the tile it spent
    // them where the camera might not be looking from, and a tile's width
    // away its cells were hundreds of metres across: a stream's channel came
    // out as a sawtooth trench of black walls.
    double cu = 0.5, cv = 0.5;
    if (eye) water_eye_param(eye, g.radius, g.shape, cu, cv);
    auto map = [&](float a, float b, float &x, float &z) {
      x = float(cu) + concentrate(a * 2.f - 1.f, extent);
      z = float(cv) + concentrate(b * 2.f - 1.f, extent);
    };
    std::vector<float> pos, nrm, uv;
    build_grid(g, surround_side, map, pos, nrm, uv);
    // The tile's square belongs to the tile: the viewport's surround throws
    // its fragments away there (FS_INF), and the baked one has to leave its
    // faces out. Kept, they lay a hair's breadth from the tile's own surface
    // over the whole square, and the path tracer took one or the other pixel
    // by pixel - the grey and green bands across the rendered mountain.
    const int cells = surround_side - 1;
    std::vector<uint8_t> keep((size_t)cells * cells, 1);
    for (int y = 0; y < cells; ++y)
      for (int x = 0; x < cells; ++x) {
        float cx, cz;
        map((x + 0.5f) / float(cells), (y + 0.5f) / float(cells), cx, cz);
        float tu = cx, tv = cz;
        if (g.tx.on && !terrain_xform_unapply_xz(g.tx, cx, cz, tu, tv)) continue;
        if (tu > 0.f && tu < 1.f && tv > 0.f && tv < 1.f) keep[(size_t)y * cells + x] = 0;
      }
    write_obj(dir / "surround.obj", pos, nrm, uv, surround_side, &keep);
    std::vector<uint8_t> rgba;
    bake_albedo(g, albedo_side, water, lat, map, rgba);
    stbi_write_png((dir / "surround_albedo.png").string().c_str(), albedo_side,
                   albedo_side, 4, rgba.data(), albedo_side * 4);
    out.surround_obj = (dir / "surround.obj").string();
    out.surround_albedo = (dir / "surround_albedo.png").string();
    out.extent = extent;
  }
  // (the sea is baked about the camera, with its waves: render_water_bake.cpp)
  out.ok = true;
  return out;
}

} // namespace studio
