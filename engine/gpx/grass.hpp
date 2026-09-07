// Geekatplay TerraForge - a sward of grass, as a function of position.
//
// The same argument as the stones (gpx/stones.hpp): a heightmap cannot hold
// a blade of grass, because on a 5 km tile a texel is metres across and a
// tuft is centimetres. So grass is not stored either - it is evaluated,
// per vertex while the tessellator subdivides and per pixel for the shading
// normal, and has no resolution of its own.
//
// It is deliberately built on the stones' lattice, because the hard parts
// are the same: a cell holds a thing, the thing has a size and a place and a
// shape of its own, the things collect in patches, and the whole must be
// mirrored into GLSL and agree with it to the last few ulps. What differs is
// everything about the thing itself:
//
//   - a tuft is pointed, not domed. A stone's profile is a shell; grass
//     comes to a point, and the exponent that does it is the single control
//     that decides whether a field reads as grass or as gravel;
//   - blades. An angular ripple around the tuft, and how many blades show is
//     a blend across three harmonics rather than a loop - a loop over a
//     blade count would put a variable-length inner loop inside an inner
//     loop inside a per-pixel evaluation;
//   - the wind is one direction for the whole field. A stone leans whichever
//     way it fell and its neighbour leans another; grass all leans the same
//     way, and that single fact is the strongest cue that a field is grass.
//     Taller blades bend further, because they do;
//   - bare ground. Grass is not a carpet: it thins to nothing in patches,
//     at a scale far larger than a tuft.
//
// This is the CPU truth. `gpxf_grass` in engine/field_glsl.cpp is a line for
// line mirror of it, and the CPU/GPU agreement check holds the two together
// (studio/field_gpu_check.cpp).
#pragma once
#include "gpx/planet_math.hpp"
#include "gpx/stones.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gpx::grass {

// The hash mixers and the drift field are the stones', deliberately: they
// are general, they are already mirrored into the GLSL prelude as
// gpxf_remix / gpxf_remix2 / gpxf_cluster_at, and a second copy would be a
// second thing to keep in step for no gain.
using stones::cluster_at;
using stones::remix;
using stones::remix2;

// Every length is in graph units: the terrain tile is 1 across. The node
// converts from metres.
struct Params {
  float cell = 3e-5f;      // one tuft's worth of ground
  float density = 0.85f;   // 0..1 coverage target; 1 closes the sward
  float height = 1.6f;     // blade height, as a multiple of the tuft radius
  float sharp = 0.6f;      // 0 a dome, 1 a point - grass comes to a point
  float blade = 0.7f;      // how strongly the individual blades show
  float fineness = 0.5f;   // 0 few broad blades .. 1 many fine ones
  float wind = 0.35f;      // how far the blades lean
  float wind_x = 1.f;      // the wind's direction, as a unit vector, because
  float wind_z = 0.f;      // an angle in here would cost a sine per blade
  float bend = 0.5f;       // how much more the tall blades lean than the short
  float spread = 0.55f;    // tuft size variation
  float variation = 0.6f;  // shape variation between tufts
  float height_var = 0.5f; // spread of heights about the average
  // How the sward is split between the big clumps and the fine grass, as a
  // geometric weight across the octaves. The node computes it from a -1..1
  // dial so the shader never runs a pow.
  float size_step = 1.f;
  float cluster = 0.3f;    // -1 evenly spaced .. 0 scattered .. 1 in patches
  float cluster_cells = 10.f;
  float bare = 0.2f;       // how much ground is bare of grass altogether
  float bare_cells = 60.f; // how far across a bare patch is, in cells
  uint32_t seed = 0;
};

inline constexpr int MAX_OCTAVES = 5;

// Height (graph units), coverage (0..1) and a per-tuft shade (0..1, the tuft
// the point actually stands on) at a point on the ground. `oct` is the
// octave budget the caller can afford - the field domain's LOD.
inline void field(const Params &p, float x, float z, int oct, float &height,
                  float &mask, float &shade) {
  float total = 0.f, cover = 0.f, tone = 0.f;
  float cs = std::max(p.cell, 1e-9f);
  const int octaves = std::clamp(oct, 1, MAX_OCTAVES);
  // Density is a coverage target, exactly as the stones' is: past three
  // quarters every cell holds a tuft and the tufts grow into one another,
  // so 1 is a closed sward with no ground showing through it.
  const float fill = std::clamp(p.density, 0.f, 1.f);
  const float chance = std::min(fill * (4.f / 3.f), 1.f);
  const float packt = std::clamp((fill - 0.75f) * 4.f, 0.f, 1.f);
  const float pack = 1.f + 0.5f * packt;
  // How many blades show, as a blend across three harmonics. A blade count
  // would want a loop of that length in the innermost loop of a per-pixel
  // evaluation; three fixed harmonics and two weights buy the same range for
  // a few multiplies, and the multiple-angle identities give them free from
  // the direction vector we already have.
  const float fn = std::clamp(p.fineness, 0.f, 1.f) * 2.f;
  const float w3 = std::max(0.f, 1.f - fn);
  const float w5 = std::max(0.f, 1.f - std::fabs(fn - 1.f));
  const float w7 = std::max(0.f, fn - 1.f);
  // the wind as a unit vector, normalised once rather than per tuft
  const float wl = std::sqrt(p.wind_x * p.wind_x + p.wind_z * p.wind_z);
  const float inv_wl = wl > 1e-6f ? 1.f / wl : 1.f;
  const float wx = p.wind_x * inv_wl, wz = p.wind_z * inv_wl;

  float ow = 1.f; // this octave's share of the sward
  for (int o = 0; o < octaves; ++o) {
    const uint32_t oseed = p.seed + (uint32_t)o * 7919u;
    const float inv = 1.f / cs;
    const float fx = x * inv, fz = z * inv;
    const int ix = (int)std::floor(fx), iz = (int)std::floor(fz);
    for (int dz = -1; dz <= 1; ++dz)
      for (int dx = -1; dx <= 1; ++dx) {
        const int cxi = ix + dx, czi = iz + dz;
        const uint32_t h = planet::pl_hash_bits(cxi, 0, czi, oseed);
        float local_density = chance * ow;
        float cgx = 0.f, cgz = 0.f;
        if (p.cluster > 0.f) {
          const float inv_cc = 1.f / std::max(p.cluster_cells, 1.f);
          float cn = 0.f;
          cluster_at((float)cxi * inv_cc, (float)czi * inv_cc, oseed ^ 0x5bd1u,
                     cn, cgx, cgz);
          local_density *= 1.f - p.cluster + p.cluster * cn * 2.f;
        }
        // Bare ground, at a scale far larger than a tuft. Sharpened so it
        // actually reaches zero: a field that only ever thins to four
        // fifths reads as an even carpet, which is the thing grass never
        // is. Trodden ground, drought and shade all look like this.
        if (p.bare > 0.f) {
          const float inv_bc = 1.f / std::max(p.bare_cells, 1.f);
          float bn = 0.f, bgx = 0.f, bgz = 0.f;
          cluster_at((float)cxi * inv_bc, (float)czi * inv_bc, oseed ^ 0x1a7du,
                     bn, bgx, bgz);
          const float open = std::clamp((bn - 0.35f) * 3.f, 0.f, 1.f);
          local_density *= 1.f - p.bare + p.bare * open;
        }
        const float exist = (float)(h & 0xfffu) * (1.f / 4095.f);
        if (exist > local_density) continue;
        const uint32_t h2 = planet::pl_hash_bits(cxi, 1, czi, oseed);
        float ox = (float)((h >> 12) & 0x3ffu) * (1.f / 1023.f);
        float oz = (float)((h >> 22) & 0x3ffu) * (1.f / 1023.f);
        // gathered into patches, or pushed apart - the stones' move, and
        // for the same reason: thinning a field says how many, not where
        if (p.cluster > 0.f) {
          const float tx = 0.5f + 0.5f * std::clamp(cgx * 2.f, -1.f, 1.f);
          const float tz = 0.5f + 0.5f * std::clamp(cgz * 2.f, -1.f, 1.f);
          ox += (tx - ox) * p.cluster * 0.75f;
          oz += (tz - oz) * p.cluster * 0.75f;
        } else if (p.cluster < 0.f) {
          const float k = 1.f + p.cluster * 0.85f;
          ox = 0.5f + (ox - 0.5f) * k;
          oz = 0.5f + (oz - 0.5f) * k;
        }
        const float t = (float)(h2 & 0x3ffu) * (1.f / 1023.f);
        float sz = 1.f - p.spread + p.spread * t * t * t;
        sz += (1.f - sz) * 0.5f * packt;
        const float rad = 0.5f * sz * pack;
        const uint32_t h3 = planet::pl_hash_bits(cxi, 2, czi, oseed);
        const float ddx = fx - ((float)cxi + ox), ddz = fz - ((float)czi + oz);
        float rr = std::sqrt(ddx * ddx + ddz * ddz);
        const float r2 = (rr * rr) / (rad * rad);
        if (r2 >= 1.f) continue;
        const float base = 1.f - r2;
        // the direction out from the tuft's middle, as a unit vector - the
        // cosine and sine of an angle we never compute
        const float inv_rr = rr > 1e-9f ? 1.f / rr : 0.f;
        const float c1 = ddx * inv_rr, s1 = ddz * inv_rr;
        const float c2 = c1 * c1 - s1 * s1, s2 = 2.f * c1 * s1;
        const float c3 = c1 * c2 - s1 * s2, s3 = s1 * c2 + c1 * s2;
        const float c5 = c3 * c2 - s3 * s2, s5 = s3 * c2 + c3 * s2;
        const float c7 = c5 * c2 - s5 * s2, s7 = s5 * c2 + c5 * s2;
        const uint32_t h4 = remix(h2), h5 = remix(h3), h6 = remix2(h4);
        // how far this tuft departs from the field's average shape
        const float vA = (float)(h5 & 0xffu) * (1.f / 255.f);
        const float vB = (float)(h6 & 0xffu) * (1.f / 255.f);
        const float vary = p.variation;
        const float s_sharp =
            std::clamp(p.sharp * (1.f - vary + vary * 2.f * vA), 0.f, 1.f);
        const float s_blade =
            std::min(p.blade * (1.f - vary + vary * 2.f * vB), 1.f);
        // A tuft comes to a point. This is the exponent that decides whether
        // the field reads as grass or as gravel: a stone's profile is a
        // shell, and no amount of blade detail rescues a field of shells.
        const float e = 0.5f + s_sharp * 3.f;
        float prof = std::pow(base, e);
        // Blades: an angular ripple, strongest partway out where the blades
        // splay and gone at the tuft's own middle and at its rim.
        if (p.blade > 0.f) {
          const float q1 = (float)((h3 >> 30) & 0x3u) * (2.f / 3.f) - 1.f;
          const float q2 = (float)((h2 >> 30) & 0x3u) * (2.f / 3.f) - 1.f;
          const float aq = 1.f - std::fabs(q1);
          const float ripple = w3 * (s3 * aq + c3 * q1) +
                               w5 * (s5 * aq + c5 * q1) +
                               w7 * (s7 * (1.f - std::fabs(q2)) + c7 * q2);
          prof *= 1.f + s_blade * 0.55f * ripple * base * (1.f - base) * 4.f;
        }
        // this tuft's own height
        const float hv = (float)(h4 & 0x3fu) * (1.f / 63.f);
        // The mean is 1 whatever the spread, so widening it does not
        // quietly raise or lower the whole sward. 0.5 is the old fixed
        // 0.45..1.55.
        const float tall = 1.f + p.height_var * (hv - 0.5f) * 2.2f;
        // The wind, which is one direction for the whole field - that is
        // the difference between grass and a field of little stones, each
        // leaning whichever way it happened to fall. Taller blades bend
        // further, because they do.
        if (p.wind > 0.f) {
          const float along = (ddx * wx + ddz * wz) / rad;
          const float lean = p.wind * (1.f - p.bend + p.bend * tall);
          prof += lean * along * base * 0.5f;
        }
        const float H = rad * cs * p.height * tall;
        const float hs = H * prof;
        if (hs <= 0.f) continue;
        if (hs > total) {
          total = hs;
          // Whose tuft this is. Grass varies enormously in colour from one
          // tuft to the next - dry to green within a stride - and a single
          // colour over a whole sward is what gives a procedural one away.
          tone = (float)((h6 >> 8) & 0xffffu) * (1.f / 65535.f);
        }
        cover = std::max(cover, std::min(base * 3.f, 1.f));
      }
    cs *= 0.5f;
    ow *= p.size_step;
  }
  height = total;
  mask = cover;
  shade = tone;
}

} // namespace gpx::grass
