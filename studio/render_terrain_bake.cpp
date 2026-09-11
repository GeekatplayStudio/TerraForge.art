// Geekatplay TerraForge - baking the viewport's world for the offline
// renderers. See render_terrain_bake.hpp for why this exists.
#include "render_terrain_bake.hpp"
#include "app.hpp"
#include "planet_place.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "terrain_tiles.hpp"
#include "terrain_xform.hpp"
#include "world_shape.hpp"
#include "gpx/heightmap.hpp"
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
    const float proc = relief(x, z);
    if (!tile || tile->empty()) return proc;
    float tu = x, tv = z;
    if (tx.on && !terrain_xform_unapply_xz(tx, x, z, tu, tv)) return proc;
    const float cu = std::clamp(tu, 0.f, 1.f), cv = std::clamp(tv, 0.f, 1.f);
    const float du = tu - cu, dv = tv - cv;
    const float dout = std::sqrt(du * du + dv * dv);
    if (dout > 0.06f) return proc;
    const float s = gpx::planet::pl_smoothstep(0.f, 0.06f, dout);
    const float th = tile->sample(cu, cv) * hscale;
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

void write_obj(const fs::path &path, const std::vector<float> &pos,
               const std::vector<float> &nrm, const std::vector<float> &uv,
               int side) {
  std::ofstream f(path);
  f << "# Geekatplay TerraForge render mesh\n";
  // Six fixed decimals on half a million vertices is over a hundred
  // megabytes of text for one frame. Five significant digits places a vertex
  // to a twenty-thousandth of a tile - a quarter of a metre on the default
  // world, far finer than the grid carrying it - at a quarter of the cost to
  // write and to parse.
  f.precision(5);
  const size_t n = pos.size() / 3;
  for (size_t i = 0; i < n; ++i)
    f << "v " << pos[i * 3] << ' ' << pos[i * 3 + 1] << ' ' << pos[i * 3 + 2] << '\n';
  for (size_t i = 0; i < n; ++i)
    f << "vn " << nrm[i * 3] << ' ' << nrm[i * 3 + 1] << ' ' << nrm[i * 3 + 2] << '\n';
  for (size_t i = 0; i < n; ++i)
    f << "vt " << uv[i * 2] << ' ' << uv[i * 2 + 1] << '\n';
  for (int y = 0; y < side - 1; ++y)
    for (int x = 0; x < side - 1; ++x) {
      const int a = y * side + x + 1, b = a + side, c = a + 1, d = b + 1;
      f << "f " << a << '/' << a << '/' << a << ' ' << b << '/' << b << '/' << b
        << ' ' << c << '/' << c << '/' << c << '\n';
      f << "f " << c << '/' << c << '/' << c << ' ' << b << '/' << b << '/' << b
        << ' ' << d << '/' << d << '/' << d << '\n';
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

// The ground's colour, by the same palette the viewport paints with.
template <class Map>
void bake_albedo(const Ground &g, int side, float water_level, float lat,
                 Map map, std::vector<uint8_t> &rgba) {
  rgba.assign((size_t)side * side * 4, 255);
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
        // the texture is sampled as sRGB by the engines; the palette is linear
        uint8_t *px = &rgba[(((size_t)(side - 1 - y) * side) + x) * 4];
        for (int k = 0; k < 3; ++k)
          px[k] = (uint8_t)std::lround(
              std::clamp(std::pow(std::clamp(c[k], 0.f, 1.f), 1.f / 2.2f), 0.f, 1.f) * 255.f);
        px[3] = 255;
      }
  });
}

} // namespace

BakedTerrain render_bake_terrain(App &a, const std::string &dirs, int tile_side,
                                 int surround_side, int albedo_side) {
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
    std::vector<uint8_t> rgba;
    bake_albedo(g, albedo_side, water, lat, map, rgba);
    stbi_write_png((dir / "albedo.png").string().c_str(), albedo_side,
                   albedo_side, 4, rgba.data(), albedo_side * 4);
    out.tile_obj = (dir / "terrain.obj").string();
    out.tile_albedo = (dir / "albedo.png").string();
  }
  // and the ground beyond it, out to the horizon
  {
    const float extent = 30.5f;
    auto map = [&](float a, float b, float &x, float &z) {
      x = 0.5f + concentrate(a * 2.f - 1.f, extent);
      z = 0.5f + concentrate(b * 2.f - 1.f, extent);
    };
    std::vector<float> pos, nrm, uv;
    build_grid(g, surround_side, map, pos, nrm, uv);
    write_obj(dir / "surround.obj", pos, nrm, uv, surround_side);
    std::vector<uint8_t> rgba;
    bake_albedo(g, albedo_side, water, lat, map, rgba);
    stbi_write_png((dir / "surround_albedo.png").string().c_str(), albedo_side,
                   albedo_side, 4, rgba.data(), albedo_side * 4);
    out.surround_obj = (dir / "surround.obj").string();
    out.surround_albedo = (dir / "surround_albedo.png").string();
    out.extent = extent;
  }
  // The sea, on the same curve. A flat rectangle is fine on a flat world and
  // wrong on a round one: the land falls away with the curve and the water
  // does not, so a plane laid across it rises through the ground and shows
  // as a bright band along the horizon. It is a grid at one altitude, placed
  // the same way the ground is.
  if (rs.show_water) {
    Ground w = g;
    w.tile = nullptr;
    w.layers.clear();
    w.base = water;
    const float extent = out.extent;
    auto map = [&](float a, float b, float &x, float &z) {
      x = 0.5f + concentrate(a * 2.f - 1.f, extent);
      z = 0.5f + concentrate(b * 2.f - 1.f, extent);
    };
    std::vector<float> pos, nrm, uv;
    build_grid(w, 96, map, pos, nrm, uv);
    write_obj(dir / "water.obj", pos, nrm, uv, 96);
    out.water_obj = (dir / "water.obj").string();
  }
  out.ok = true;
  return out;
}

} // namespace studio
