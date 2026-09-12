// Geekatplay TerraForge - the sea baked for the offline renderers.
//
// The viewport's sea is a clipmap of rings about the eye, displaced by the
// waves (renderer_water.cpp); an offline engine gets a mesh instead, so this
// builds the same surface from the same CPU truth (gpx/water_waves.hpp):
// rings about the camera's point over the world, spaced ever wider with
// distance, each carrying the waves its spacing can hold - four cells a
// wave, the viewport's own rule - and a normal from every wave that spacing
// resolves. What the mesh cannot carry goes into the roughness, as the
// viewport's shading does with what a pixel cannot.
#include "render_water_bake.hpp"
#include "obj_text.hpp"
#include "render_settings.hpp"
#include "water_surface.hpp"
#include "world_shape.hpp"
#include "gpx/planet_math.hpp"
#include "gpx/water_waves.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <vector>

namespace studio {

BakedWater render_bake_water(const RenderSettings &rs, const float eye[3],
                             const std::string &path, float extent) {
  BakedWater out;
  const float size_m = std::max(rs.terrain_size_m, 1e-3f);
  const float R = rs.planet_radius;
  const gpx::planet::Shape S = world_shape(rs, SIDE_WORLD);
  const float level = rs.water_level * rs.height_scale;
  const WaterWaves &ww = water_waves(rs);

  double eu = 0.0, ev = 0.0;
  water_eye_param(eye, R, S, eu, ev);
  float lim_x = 0.f, lim_z = 0.f;
  water_param_limits(R, S, lim_x, lim_z);
  const double wx = water_origin_snap(eu), wz = water_origin_snap(ev);
  float ph[gpx::water::MAX_WAVES] = {};
  gpx::water::rebase(ww.w, ww.n, wx * size_m, wz * size_m, 0.0, ph);

  float alt = eye[1];
  if (R > 0.f && !gpx::planet::shape_is_flat(S)) alt = gpx::planet::world_alt(eye, R, S);
  const double above = std::fabs(double(alt) - level);
  // from a few centimetres under the camera out to the ground's rim
  const double r0 = std::max(above * 0.004, 0.03 / size_m);
  const double r1 = std::max(double(extent), r0 * 4.0);
  const int NR = 180, NA = 256;
  const bool displace = rs.water_displaced;

  std::vector<float> pos((size_t)NR * (NA + 1) * 3), nrm(pos.size()), uv((size_t)NR * (NA + 1) * 2);
  for (int j = 0; j < NR; ++j) {
    const double r = j == 0 ? 0.0 : r0 * std::pow(r1 / r0, (j - 1) / double(NR - 2));
    const double rn = r0 * std::pow(r1 / r0, j / double(NR - 2));
    const double cell_m = std::max(rn - r, r * 6.283185307 / NA) * size_m;
    for (int i = 0; i <= NA; ++i) {
      const double a = i * 6.283185307179586 / NA;
      double x = eu + r * std::cos(a), z = ev + r * std::sin(a);
      x = std::clamp(x, 0.5 - double(lim_x), 0.5 + double(lim_x));
      z = std::clamp(z, 0.5 - double(lim_z), 0.5 + double(lim_z));
      gpx::water::Sample s;
      if (displace)
        gpx::water::evaluate(ww.w, ph, ww.n, float((x - wx) * size_m), float((z - wz) * size_m),
                             float(4.0 * cell_m), s);
      float nf[3];
      gpx::water::normal(s, nf);
      const size_t k = (size_t)j * (NA + 1) + i;
      gpx::planet::sphere_place(float(x + s.disp[0] / size_m), float(z + s.disp[2] / size_m),
                                level + s.disp[1] / size_m, R, S, &pos[k * 3]);
      if (R > 0.f) {
        float east[3], up[3], north[3];
        gpx::planet::sphere_frame(float(x), float(z), R, S, east, up, north);
        for (int c = 0; c < 3; ++c)
          nrm[k * 3 + c] = east[c] * nf[0] + up[c] * nf[1] + north[c] * nf[2];
      } else {
        for (int c = 0; c < 3; ++c) nrm[k * 3 + c] = nf[c];
      }
      uv[k * 2] = float(i) / NA;
      uv[k * 2 + 1] = float(j) / (NR - 1);
    }
  }

  // eight digits: a wave a few centimetres high must survive the text
  ObjText f(path, 8);
  if (!f.ok()) return out;
  f.text("# Geekatplay TerraForge sea\n");
  const size_t n = pos.size() / 3;
  for (size_t i = 0; i < n; ++i) f.line3("v", pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]);
  for (size_t i = 0; i < n; ++i) f.line3("vn", nrm[i * 3], nrm[i * 3 + 1], nrm[i * 3 + 2]);
  for (size_t i = 0; i < n; ++i) f.text("vt ").num(uv[i * 2]).ch(' ').num(uv[i * 2 + 1]).ch('\n');
  // a runs outward to b and round to c: (c - a) x (b - a) is up, so the
  // faces look at the sky, as the terrain's do
  auto corner = [&f](size_t k) { f.ch(' ').num((long long)k).ch('/').num((long long)k).ch('/').num((long long)k); };
  for (int j = 0; j + 1 < NR; ++j)
    for (int i = 0; i < NA; ++i) {
      const size_t a = (size_t)j * (NA + 1) + i + 1, b = a + NA + 1, c = a + 1, d = b + 1;
      f.ch('f');
      corner(a);
      corner(c);
      corner(b);
      f.text("\nf");
      corner(c);
      corner(d);
      corner(b);
      f.ch('\n');
    }
  // The roughness the engines' BSDF gets: the slopes of every wave the mesh
  // is too coarse to carry a stone's throw from the camera, where most of
  // what a shot shows of the sea is.
  {
    const double cell_m = std::max(r0 * size_m, 50.0 * 6.283185307 / NA);
    gpx::water::Sample s;
    gpx::water::evaluate(ww.w, ph, ww.n, 0.f, 0.f, float(4.0 * cell_m), s);
    out.roughness = std::sqrt(0.0004f + 2.f * s.lost);
  }
  out.ok = true;
  out.obj = path;
  return out;
}

} // namespace studio
