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
uniform int u_sp_spike_points;
uniform float u_sp_spike_angle, u_sp_spike_chroma, u_sp_star_sat, u_sp_star_glow;
// the magnitude law's exponent (how few bright stars), and the clusters: how
// many, and how wide one is, degrees
uniform float u_sp_mag_slope, u_sp_clusters, u_sp_cluster_size;

// The diffraction spikes across a bright star. They belong to the camera: a
// telescope's vanes cut the same cross into every star in the frame, so the
// arms share one angle (they used to turn by each star's own hash) and come
// in the vanes' number - 4, 6 or 8 points. Each colour reaches a little
// further along them than the last, as a real lens spreads them, and all of
// it is bounded by `reach` so it ends inside the cells searched.
float pix_k = 1.0e-3; // the pixel the field is drawn at (set by sp_stars)
vec3 sp_spike(vec3 d, vec3 s, float ang, float reach){
  if (ang >= reach) return vec3(0.0);
  vec3 upv = abs(s.y) < 0.95 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
  vec3 t1 = normalize(cross(upv, s));
  vec3 t2 = cross(s, t1);
  vec2 v = vec2(dot(d, t1), dot(d, t2));
  // thin, but never thinner than the pixel can hold, or a spike breaks up
  // into dashes as the view turns
  float w = max(reach * 0.010, pix_k * 0.55);
  int lines = clamp(u_sp_spike_points / 2, 2, 4);
  float a = 0.0;
  for (int i = 0; i < 4; ++i){
    if (i >= lines) break;
    float th = u_sp_spike_angle + 3.14159265 * float(i) / float(lines);
    float perp = dot(v, vec2(-sin(th), cos(th)));
    a += exp(-(perp * perp) / (w * w));
  }
  // bright near the star and fading along the arm faster than the glow; the
  // red end of the light reaches furthest
  vec3 len = vec3(1.0, 1.0 - 0.06 * u_sp_spike_chroma, 1.0 - 0.12 * u_sp_spike_chroma);
  vec3 t = clamp(vec3(ang) / (reach * len), 0.0, 1.0);
  vec3 f = vec3(sp_falloff(t.r), sp_falloff(t.g), sp_falloff(t.b));
  return a * f * (1.0 - t * 0.6);
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
        float mag = pow(m, u_sp_mag_slope > 0.0 ? u_sp_mag_slope : 9.0);
        // A bright star is mostly brighter, not mostly bigger: growing the
        // core with the magnitude as fast as the brightness turned a sky
        // into a field of equal discs.
        float core_r = pix * (0.55 + u_sp_star_size * (0.32 + 0.95 * mag));
        float halo_r = rich > 0
            ? min(core_r * (4.0 + 14.0 * mag) * max(u_sp_star_halo, 0.0), cellang * 0.85)
            : 0.0;
        // The bright few carry long spikes: in a photograph they are what
        // says which stars are the bright ones at all, a handful of crosses
        // over a field of points.
        float spike_r = (rich > 1 && mag > 0.06 && u_sp_star_spikes > 0.0)
            ? min(max(halo_r * 4.5, pix * 30.0 * mag), cellang * 0.94) : 0.0;
        // the bloom a long exposure spreads through the lens round the
        // brightest: wide and faint, and inside the cells like everything else
        float glow_r = (rich > 1 && u_sp_star_glow > 0.0) ? cellang * 0.94 : 0.0;
        float reach = max(core_r * 3.0, max(max(halo_r, spike_r), glow_r));
        if (ang > reach) continue;
        // most of the stars a camera records are white to blue; the orange
        // and red ones are fewer and fainter
        float t = 0.58 + (sp_hash(cc + 5.0, seed + 23.0) - 0.5) * u_sp_star_temp;
        vec3 v = vec3(exp(-(ang * ang) / (core_r * core_r)));
        if (halo_r > core_r)
          v += sp_falloff(ang / halo_r) * mag * u_sp_star_halo * (rich > 1 ? 0.12 : 0.06);
        if (spike_r > 0.0)
          v += sp_spike(d, s, ang, spike_r) * mag * u_sp_star_spikes * 0.9;
        if (glow_r > 0.0) {
          float g = sp_falloff(ang / glow_r);
          v += g * g * mag * u_sp_star_glow * 0.25;
        }
        vec3 sc = sp_star_col(t);
        sc = mix(vec3(dot(sc, vec3(0.2126, 0.7152, 0.0722))), sc, u_sp_star_sat);
        col += max(sc, vec3(0.0)) * v * (0.015 + 1.5 * mag) * bright;
      }
  return col;
}

// Star clusters. A coarse grid of cells, a few holding a cluster: stars born
// together, crowded at a core and thinning to a tidal edge by a King profile.
// An open cluster is a loose handful of young blue-white stars; a globular
// (a third of them) a tight old yellow ball of thousands, most of them too
// faint to tell apart - that part is a glow, the brighter members are drawn
// one by one on a fine grid in the cluster's own tangent plane while a pixel
// can still separate them. Everything ends inside the 3x3 cells searched, at
// both scales, the rule every glow drawn from a grid in this file obeys.
float sp_king(float r, float core){
  float k0 = 1.0 / sqrt(1.0 + 1.0 / (core * core));
  float k = max(1.0 / sqrt(1.0 + (r * r) / (core * core)) - k0, 0.0) / (1.0 - k0);
  return k * k;
}
vec3 sp_clusters(vec3 d, float pix){
  if (u_sp_clusters <= 0.0) return vec3(0.0);
  const float N = 6.0;
  float seed = float(u_sp_star_seed) + 307.0;
  vec2 uv;
  int f = sp_face(d, uv);
  vec2 cell = floor(uv * N);
  float cellang = 2.0 / (3.0 * N);
  vec3 col = vec3(0.0);
  for (int j = -1; j <= 1; ++j)
    for (int i = -1; i <= 1; ++i){
      vec2 c = cell + vec2(float(i), float(j));
      int f2 = f;
      if (c.x < 0.0 || c.y < 0.0 || c.x >= N || c.y >= N){
        vec2 uv2;
        f2 = sp_face(sp_face_dir(f, (c + 0.5) / N), uv2);
        c = floor(uv2 * N);
      }
      vec3 cc = vec3(c, float(f2) * 7.0 + 3.0);
      vec3 h = sp_hash3(cc, seed);
      if (h.x > u_sp_clusters * 0.45) continue;
      vec3 s = sp_face_dir(f2, (c + 0.2 + 0.6 * h.yz) / N);
      if (dot(d, s) <= 0.0) continue;
      float rc = clamp(max(u_sp_cluster_size, 0.05) * 0.0087266 * (0.6 + 0.9 * h.y),
                       1.0e-4, cellang * 0.85);
      float ang = length(cross(d, s));
      if (ang >= rc) continue;
      bool globular = sp_hash(cc + 9.0, seed) < 0.33;
      // the core radius, as a share of the edge's: a globular's light is
      // gathered in the middle, an open cluster's is loose throughout
      float core = globular ? 0.22 : 0.5;
      float t = 0.58 + (h.z - 0.5) * u_sp_star_temp + (globular ? -0.14 : 0.12);
      vec3 tint = sp_star_col(t);
      float bright = max(u_sp_star_bright, 0.0) * (0.5 + 0.5 * h.z);
      // The members too faint to tell apart: a haze over a globular's core,
      // next to nothing in an open cluster. Drawn brighter, it read as an
      // out-of-focus disc instead of a crowd of stars.
      float r = ang / rc;
      // the ones a pixel can still separate, on a grid of cells across the
      // cluster: a member's chance of being there follows the same profile -
      // hundreds of faint points in a globular, a loose handful of bright
      // ones in an open cluster. Too small on screen to separate, the whole
      // of their light is the haze, and the cluster is the fuzzy star a
      // distant one really looks like.
      float n_m = globular ? 30.0 : 8.0;
      float m_cell = 2.0 * rc / n_m;
      bool resolved = m_cell >= pix * 2.5 && !sp_probe_pass;
      col += tint * sp_king(r, core) * bright *
             (resolved ? (globular ? 0.28 : 0.025) : (globular ? 0.9 : 0.3));
      if (!resolved) continue;
      vec3 t1 = normalize(cross(abs(s.y) < 0.95 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0), s));
      vec3 t2 = cross(s, t1);
      vec2 p = vec2(dot(d, t1), dot(d, t2)) / m_cell;
      vec2 pc = floor(p);
      float mseed = seed + cc.x * 13.0 + cc.y * 29.0 + cc.z * 3.0;
      for (int mj = -1; mj <= 1; ++mj)
        for (int mi = -1; mi <= 1; ++mi){
          vec2 q = pc + vec2(float(mi), float(mj));
          vec3 hm = sp_hash3(vec3(q, 5.0), mseed);
          vec2 at = q + 0.15 + 0.7 * hm.yz;
          float rm = length(at) * m_cell / rc;
          if (rm >= 1.0) continue;
          // the chance follows the profile's square root: the count of stars,
          // where the glow above follows their light
          if (hm.x > sqrt(sp_king(rm, core)) * (globular ? 1.0 : 0.8)) continue;
          float mag = pow(sp_hash(vec3(q, 11.0), mseed), globular ? 3.0 : 1.2);
          // a core that ends inside its own cell and the ones beside it
          float cr = min(pix * (0.55 + u_sp_star_size * (0.32 + 0.95 * mag)), m_cell * 0.3);
          float da = length(p - at) * m_cell;
          if (da > cr * 3.0) continue;
          float tm = t + (sp_hash(vec3(q, 17.0), mseed) - 0.5) * 0.35 * u_sp_star_temp;
          vec3 sc = mix(vec3(dot(sp_star_col(tm), vec3(0.2126, 0.7152, 0.0722))),
                        sp_star_col(tm), u_sp_star_sat);
          col += max(sc, vec3(0.0)) * exp(-(da * da) / (cr * cr)) *
                 (globular ? 0.2 + 1.8 * mag : 0.45 + 3.0 * mag) * bright;
        }
    }
  return col;
}

vec3 sp_stars(vec3 d, float pix){
  if (u_sp_stars == 0) return vec3(0.0);
  pix_k = pix;
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
  return col + sp_clusters(d, pix);
}
)GLSL";

} // namespace studio
