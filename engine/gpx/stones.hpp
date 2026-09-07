// Geekatplay TerraForge - a field of stones, as a function of position.
//
// A heightmap cannot hold a stone. On a 5 km tile at 1024 a texel is 4.9 m,
// so a boulder is a fifth of a texel and a pebble is nothing at all; the
// raster FakeStones node is a layout generator, not a stone generator, and
// its smallest expressible stone is 14 m across. Terragen's answer, and
// ours, is to stop storing the stones and evaluate them instead: this is a
// function of a point, so it has no resolution, and the GPU calls it per
// vertex and per pixel at whatever density the camera needs.
//
// What makes a field of these read as stones rather than as bumps:
//   - a power-law size spectrum: many small, a few large, as scree is;
//   - a height per stone, not one height for the field (the raster node's
//     most visible tell - every stone the same height, only the footprint
//     varying);
//   - a lean, so the apex is off centre and the stone has a downhill side;
//   - burial, so a stone shows only its top and its outline is cut by the
//     ground rather than meeting it tangentially;
//   - a plateau rather than a scaled dome when flattened, which is what a
//     slab actually is;
//   - octaves: half the cell and half the stone each time, so boulders,
//     cobbles and gravel come out of one function and one cost.
//
// This is the CPU truth. `gpxf_stones` in engine/field_glsl.cpp is a line
// for line mirror of it, and the CPU/GPU agreement check holds the two
// together (studio/field_gpu_check.cpp).
#pragma once
#include "gpx/planet_math.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gpx::stones {

// Every length is in graph units: the terrain tile is 1 across. The node
// converts from metres, so an author sizes stones in metres and the field
// stays resolution-free.
struct Params {
  float cell = 1.2e-4f;  // cell size: one stone's worth of ground
  float density = 0.7f;  // 0..1 of cells that hold a stone
  float tallness = 0.6f; // height as a fraction of the stone's radius
  float flatten = 0.25f; // 0 boulders .. 1 slabs
  float bury = 0.25f;    // how deep a stone sits in the ground
  float tilt = 0.35f;    // how far a stone leans off vertical
  float spread = 0.7f;   // size spectrum: 0 all alike, 1 many tiny, few large
  float elongation = 0.5f; // 0 round in plan, 1 up to three times as long
  float rough = 0.45f;     // how far the outline departs from an ellipse
  uint32_t seed = 0;
};

inline constexpr int MAX_OCTAVES = 5;

// Height (graph units) and coverage (0..1) at a point on the ground.
// `oct` is the octave budget the caller can afford - the field-domain LOD.
inline void field(const Params &p, float x, float z, int oct, float &height,
                  float &mask) {
  float total = 0.f, cover = 0.f;
  float cs = std::max(p.cell, 1e-9f);
  const int octaves = std::clamp(oct, 1, MAX_OCTAVES);
  for (int o = 0; o < octaves; ++o) {
    const uint32_t oseed = p.seed + (uint32_t)o * 7919u;
    const float inv = 1.f / cs;
    const float fx = x * inv, fz = z * inv;
    const int ix = (int)std::floor(fx), iz = (int)std::floor(fz);
    for (int dz = -1; dz <= 1; ++dz)
      for (int dx = -1; dx <= 1; ++dx) {
        const int cxi = ix + dx, czi = iz + dz;
        const uint32_t h = planet::pl_hash_bits(cxi, 0, czi, oseed);
        // does this cell hold a stone at all
        const float exist = (float)(h & 0xfffu) * (1.f / 4095.f);
        if (exist > p.density) continue;
        const uint32_t h2 = planet::pl_hash_bits(cxi, 1, czi, oseed);
        // its centre, anywhere in the cell - a stone on the border is as
        // likely as one in the middle, or the lattice stays visible
        const float ox = (float)((h >> 12) & 0x3ffu) * (1.f / 1023.f);
        const float oz = (float)((h >> 22) & 0x3ffu) * (1.f / 1023.f);
        // power-law size: t^3 gives many small and a few large, the
        // spectrum a scree slope actually has
        const float t = (float)(h2 & 0x3ffu) * (1.f / 1023.f);
        const float sz = 1.f - p.spread + p.spread * t * t * t;
        const float rad = 0.5f * sz;
        const uint32_t h3 = planet::pl_hash_bits(cxi, 2, czi, oseed);
        const float ddx = fx - ((float)cxi + ox), ddz = fz - ((float)czi + oz);
        // A stone lies the way it fell: turned, and longer one way than the
        // other. Circles in plan are the tell of a procedural field.
        //
        // No angles anywhere in here. This is the innermost loop of the
        // innermost loop - nine cells an octave, five octaves, and the
        // fragment stage calls the whole thing four times over for the
        // shading normal - so a sine costs more than the stone is worth.
        // A turn is a hashed unit vector, and the harmonics that roughen
        // the outline come from the multiple-angle identities on that same
        // vector, which are multiplies.
        const float vx = (float)(h3 & 0x3ffu) * (2.f / 1023.f) - 1.f;
        const float vz = (float)((h3 >> 10) & 0x3ffu) * (2.f / 1023.f) - 1.f;
        const float vl = std::sqrt(vx * vx + vz * vz);
        const float inv_vl = vl > 1e-6f ? 1.f / vl : 1.f;
        const float cr = vx * inv_vl, sr = vz * inv_vl;
        const float rx = ddx * cr + ddz * sr, rz = -ddx * sr + ddz * cr;
        const float aspect =
            1.f + p.elongation * 2.f * (float)((h3 >> 20) & 0x3ffu) * (1.f / 1023.f);
        const float ex = rx / aspect, ez = rz;
        float rr = std::sqrt(ex * ex + ez * ez);
        // the direction as a unit vector, which is the cosine and sine of
        // the angle we never computed
        const float inv_rr = rr > 1e-9f ? 1.f / rr : 0.f;
        const float c1 = ex * inv_rr, s1 = ez * inv_rr;
        const float c2 = c1 * c1 - s1 * s1, s2 = 2.f * c1 * s1;
        const float c3 = c1 * c2 - s1 * s2, s3 = s1 * c2 + c1 * s2;
        const float c5 = c3 * c2 - s3 * s2, s5 = s3 * c2 + c3 * s2;
        // each harmonic's phase is a hashed unit vector too
        const float q1 = (float)((h3 >> 30) & 0x3u) * (2.f / 3.f) - 1.f;
        const float q2 = (float)((h2 >> 24) & 0x3u) * (2.f / 3.f) - 1.f;
        const float wob = 1.f + p.rough * (0.13f * (s3 * (1.f - std::fabs(q1)) + c3 * q1) +
                                           0.07f * (s5 * (1.f - std::fabs(q2)) + c5 * q2));
        rr /= std::max(wob, 0.2f);
        const float r2 = (rr * rr) / (rad * rad);
        if (r2 >= 1.f) continue;
        const float base = 1.f - r2;
        // the profile: a rounder or a squarer stone, per stone
        const float e = 0.35f + 0.5f * (float)((h2 >> 10) & 0xffu) * (1.f / 255.f);
        float prof = std::pow(base, e);
        // flattening raises the top into a plateau and keeps the footprint,
        // which is what a slab is; scaling the dome only makes it smaller
        prof = std::min(prof / std::max(1.f - p.flatten * 0.85f, 0.15f), 1.f);
        // the lean: the apex moves off centre, so the stone has a high side
        // the lean's direction: another hashed vector, no angle
        const float lx = (float)((h2 >> 18) & 0x3fu) * (2.f / 63.f) - 1.f;
        const float lz = (float)((h2 >> 24) & 0x3fu) * (2.f / 63.f) - 1.f;
        const float ll = std::sqrt(lx * lx + lz * lz);
        const float inv_ll = ll > 1e-6f ? 1.f / ll : 1.f;
        const float lean = (ex * lx * inv_ll + ez * lz * inv_ll) / rad;
        prof += p.tilt * lean * base * 0.5f;
        // this stone's own height
        const float hv = (float)((h2 >> 26) & 0x3fu) * (1.f / 63.f);
        const float H = rad * cs * p.tallness * (0.6f + 0.8f * hv);
        const float hs = H * prof - p.bury * H;
        if (hs <= 0.f) continue;
        // stones meet in a crease rather than blending into one another
        total = std::max(total, hs);
        cover = std::max(cover, std::min(base * 3.f, 1.f));
      }
    cs *= 0.5f;
  }
  height = total;
  mask = cover;
}

} // namespace gpx::stones
