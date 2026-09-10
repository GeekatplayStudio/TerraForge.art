// Geekatplay TerraForge — deep space, part 2 of 4: the star field.
//
// Three grids of different fineness, so the sky has a few hundred bright
// stars among a quarter of a million faint ones. Each star is a core a
// pixel or so across (a point-sampled star flickers as the camera turns), a
// halo around it, and - on the brightest - the four diffraction spikes a
// telescope's vanes cut across the image.
//
// The rule the whole file is built on: every falloff drawn from a cell grid
// must reach zero inside the cells the grid searches. The shader looks at
// the 3x3 cells around a pixel, so a glow wider than one cell is cut off at
// that boundary - and a cut-off glow is a square. Squares around every star
// were what this file looked like before; `sp_falloff` and the `cellang`
// bound below are the fix. Spliced after shaders_space.cpp.
#include "renderer_shaders.hpp"

namespace studio {

const char *const SPACE_STARS_FN = R"GLSL(
uniform int u_sp_stars, u_sp_star_seed;
uniform float u_sp_star_density, u_sp_star_bright, u_sp_star_size, u_sp_star_temp;
uniform float u_sp_star_spikes, u_sp_star_halo, u_sp_star_clump;

// The diffraction spikes across a bright star: four arms in the star's own
// tangent frame, turned by its own hash so they do not all lie the same
// way, and bounded by `reach` so they end inside the cells searched.
float sp_spike(vec3 d, vec3 s, float ang, float reach, float rot){
  if (ang >= reach) return 0.0;
  vec3 upv = abs(s.y) < 0.95 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
  vec3 t1 = normalize(cross(upv, s));
  vec3 t2 = cross(s, t1);
  vec2 v = vec2(dot(d, t1), dot(d, t2));
  float cr = cos(rot), sr = sin(rot);
  vec2 q = vec2(v.x * cr - v.y * sr, v.x * sr + v.y * cr);
  float w = max(reach * 0.016, 1.0e-7);
  float a = exp(-(q.y * q.y) / (w * w)) + exp(-(q.x * q.x) / (w * w));
  return a * sp_falloff(ang / reach);
}

// One grid: `n` cells across a cube face, two slots a cell so no lattice
// shows through, `prob` the chance a slot holds a star. `rich` is how much
// of a star is drawn: 0 the core alone (the faint many), 1 with its halo,
// 2 with spikes as well (the bright few).
vec3 sp_star_grid(vec3 d, float pix, float n, float prob, float bright,
                  float seed, float clump, int rich){
  vec2 uv;
  int f = sp_face(d, uv);
  vec2 cell = floor(uv * n);
  // The smallest a cell of this grid ever gets, in radians. The cube's
  // faces are gnomonic, so a cell at a corner subtends two thirds of one
  // at the middle; bounding every glow by the smaller keeps it inside the
  // 3x3 window wherever on the face the pixel lands.
  float cellang = 2.0 / (3.0 * n);
  vec3 col = vec3(0.0);
  for (int j = -1; j <= 1; ++j)
    for (int i = -1; i <= 1; ++i)
      for (int k = 0; k < 2; ++k){
        vec2 c = cell + vec2(float(i), float(j));
        // A cell past the edge of this face belongs to the next one.
        // Hashing it as though it were still on this face gives the two
        // sides of a cube edge different stars - and a cube's edge is a
        // great circle, which a perspective view draws as a dead straight
        // line. That is the rectangle that used to cut across the milky
        // band. Resolving the cell to the face it truly lies on makes both
        // sides agree on the same star; only the cells at an edge pay for
        // the extra work, which is a handful of pixels in a sky.
        int f2 = f;
        if (c.x < 0.0 || c.y < 0.0 || c.x >= n || c.y >= n){
          vec2 uv2;
          f2 = sp_face(sp_face_dir(f, (c + 0.5) / n), uv2);
          c = floor(uv2 * n);
        }
        vec3 cc = vec3(c, float(f2) * 7.0 + float(k) * 41.0);
        vec3 h = sp_hash3(cc, seed);
        // The ceiling matters as much as the chance. Clumping multiplies
        // the odds a slot holds a star, and a multiplier over one filled
        // every slot in the crowded parts - a dozen bright stars landing on
        // top of each other, their halos stacking into a white ball that
        // read as a bug rather than as a cluster.
        if (h.x > min(prob * clump, 0.80)) continue;
        vec3 s = sp_face_dir(f2, (c + h.yz) / n);
        float ang = length(cross(d, s));
        // magnitudes follow a steep law: a very few are bright, and the
        // rest run down to nothing, which is what a real sky looks like
        float m = sp_hash(cc + 3.0, seed + 11.0);
        float mag = pow(m, 9.0);
        // A bright star is mostly brighter, not mostly bigger: growing the
        // core with the magnitude as fast as the brightness turned a sky
        // into a field of equal discs.
        float core_r = pix * (0.55 + u_sp_star_size * (0.32 + 0.95 * mag));
        float halo_r = rich > 0
            ? min(core_r * (4.0 + 14.0 * mag) * max(u_sp_star_halo, 0.0), cellang * 0.85)
            : 0.0;
        float spike_r = (rich > 1 && mag > 0.10 && u_sp_star_spikes > 0.0)
            ? min(halo_r * 2.8, cellang * 0.94) : 0.0;
        float reach = max(core_r * 3.0, max(halo_r, spike_r));
        if (ang > reach) continue;
        float t = 0.5 + (sp_hash(cc + 5.0, seed + 23.0) - 0.5) * u_sp_star_temp;
        float v = exp(-(ang * ang) / (core_r * core_r));
        if (halo_r > core_r)
          v += sp_falloff(ang / halo_r) * mag * u_sp_star_halo * 0.060;
        if (spike_r > 0.0)
          v += sp_spike(d, s, ang, spike_r, h.y * 3.14159) * mag * u_sp_star_spikes * 0.45;
        col += sp_star_col(t) * v * (0.015 + 1.5 * mag) * bright;
      }
  return col;
}

vec3 sp_stars(vec3 d, float pix){
  if (u_sp_stars == 0) return vec3(0.0);
  float seed = float(u_sp_star_seed);
  float dens = clamp(u_sp_star_density, 0.0, 1.0);
  float b = max(u_sp_star_bright, 0.0);
  // Stars are not sprinkled evenly. They gather into associations and leave
  // the sky between them nearly empty; one coarse fractal decides how
  // crowded each part of it is.
  float clump = mix(1.0, 0.25 + 1.5 * sp_fbm(d * 2.6, seed + 61.0, 3),
                    clamp(u_sp_star_clump, 0.0, 1.0));
  // the bright few, the many, and the crowd that is only a grain of light
  vec3 col = sp_star_grid(d, pix,   9.0, 0.05 + 0.12 * dens, b,         seed,         clump, 2);
  col += sp_star_grid(d, pix,  42.0, 0.10 + 0.26 * dens, b * 0.80, seed + 101.0, clump, 1);
  col += sp_star_grid(d, pix, 190.0, 0.10 + 0.45 * dens, b * 0.42, seed + 211.0, clump, 0);
  return col;
}
)GLSL";

} // namespace studio
