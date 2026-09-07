#include "gpx/field_glsl.hpp"
#include "field_glsl_internal.hpp"
#include "gpx/node_graph.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace gpx {

// ----------------------------------------------------------------- prelude
// A line-for-line mirror of gpx::planet::pl_hash / pl_vnoise / pl_fbm. These
// two implementations must agree, which is what the CPU-vs-GPU test checks;
// keeping them adjacent in the codebase is deliberate.
static const char *PRELUDE = R"GLSL(
float gpxf_hash(vec3 ip, uint seed){
  uvec3 q = uvec3(ivec3(ip));
  uint h = q.x*374761393u + q.y*668265263u + q.z*2147483647u + seed*3266489917u;
  h = (h ^ (h>>13u)) * 1274126177u;
  h ^= h>>16u;
  return float(h & 0xffffffu) / 16777215.0;
}
float gpxf_vnoise(vec3 p, uint seed){
  vec3 i = floor(p), f = fract(p);
  f = f*f*(3.0-2.0*f);
  float c000=gpxf_hash(i,seed),             c100=gpxf_hash(i+vec3(1,0,0),seed);
  float c010=gpxf_hash(i+vec3(0,1,0),seed), c110=gpxf_hash(i+vec3(1,1,0),seed);
  float c001=gpxf_hash(i+vec3(0,0,1),seed), c101=gpxf_hash(i+vec3(1,0,1),seed);
  float c011=gpxf_hash(i+vec3(0,1,1),seed), c111=gpxf_hash(i+vec3(1,1,1),seed);
  float x00=mix(c000,c100,f.x), x10=mix(c010,c110,f.x);
  float x01=mix(c001,c101,f.x), x11=mix(c011,c111,f.x);
  return mix(mix(x00,x10,f.y), mix(x01,x11,f.y), f.z);
}
float gpxf_fbm(vec3 p, uint seed, int octaves, int type){
  float sum=0.0, amp=1.0, norm=0.0;
  vec3 q = p;
  for (int i = 0; i < 12; ++i){
    if (i >= octaves) break;
    float n = gpxf_vnoise(q, seed + uint(i)*101u);
    if (type == 1) n = 1.0 - abs(n*2.0-1.0);
    else if (type == 2) n = abs(n*2.0-1.0);
    sum += n * amp;
    norm += amp;
    amp *= 0.5;
    q *= 2.03;
  }
  float v = norm > 0.0 ? sum/norm : 0.0;
  if (type == 1) v = v*v;
  return v - 0.5;
}
// Cellular (Worley) noise, mirroring gpx::planet::pl_cell line for line.
// Returns (f1, f2, id): nearest distance, second nearest, and a stable random
// value for the nearest cell.
uint gpxf_hash_bits(vec3 ip, uint seed){
  uvec3 q = uvec3(ivec3(ip));
  uint h = q.x*374761393u + q.y*668265263u + q.z*2147483647u + seed*3266489917u;
  h = (h ^ (h>>13u)) * 1274126177u;
  h ^= h>>16u;
  return h;
}
vec3 gpxf_cell(vec3 p, uint seed, float jitter, int metric){
  vec3 i = floor(p), f = p - i;
  float f1 = 1e9, f2 = 1e9, id = 0.0;
  for (int dz = -1; dz <= 1; ++dz)
  for (int dy = -1; dy <= 1; ++dy)
  for (int dx = -1; dx <= 1; ++dx){
    vec3 d3 = vec3(float(dx), float(dy), float(dz));
    uint h = gpxf_hash_bits(i + d3, seed);
    vec3 o = vec3(float(h & 0x3ffu), float((h>>10u) & 0x3ffu),
                  float((h>>20u) & 0x3ffu)) * (1.0/1023.0);
    vec3 q = d3 + vec3(0.5) + (o - vec3(0.5)) * jitter - f;
    float d;
    if (metric == 1) d = abs(q.x) + abs(q.y) + abs(q.z);
    else if (metric == 2) d = max(abs(q.x), max(abs(q.y), abs(q.z)));
    else d = sqrt(dot(q, q));
    if (d < f1){ f2 = f1; f1 = d; id = float(h & 0xffffffu) * (1.0/16777215.0); }
    else if (d < f2) f2 = d;
  }
  return vec3(f1, f2, id);
}
// A field of stones, mirroring gpx::stones::field (gpx/stones.hpp) line for
// line. Returns (height, coverage, per-stone shade). A heightmap cannot hold a stone; this
// can, because it is a function and has no resolution.
uint gpxf_remix(uint h){ h *= 2654435761u; h ^= h >> 15u; return h; }
uint gpxf_remix2(uint h){ h *= 0x85ebca6bu; h ^= h >> 13u; return h; }
// value and analytic gradient of the drift field
vec3 gpxf_cluster_at(float gx, float gz, uint seed){
  float fx = floor(gx), fz = floor(gz);
  float ux = gx - fx, uz = gz - fz;
  float ax = ux * ux * (3.0 - 2.0 * ux), az = uz * uz * (3.0 - 2.0 * uz);
  float dax = 6.0 * ux * (1.0 - ux), daz = 6.0 * uz * (1.0 - uz);
  float n00 = float(gpxf_hash_bits(vec3(fx,       5.0, fz      ), seed) & 0xffffu) * (1.0/65535.0);
  float n10 = float(gpxf_hash_bits(vec3(fx + 1.0, 5.0, fz      ), seed) & 0xffffu) * (1.0/65535.0);
  float n01 = float(gpxf_hash_bits(vec3(fx,       5.0, fz + 1.0), seed) & 0xffffu) * (1.0/65535.0);
  float n11 = float(gpxf_hash_bits(vec3(fx + 1.0, 5.0, fz + 1.0), seed) & 0xffffu) * (1.0/65535.0);
  float a = n00 + (n10 - n00) * ax;
  float b = n01 + (n11 - n01) * ax;
  return vec3(a + (b - a) * az,
              ((n10 - n00) * (1.0 - az) + (n11 - n01) * az) * dax,
              (b - a) * daz);
}
vec3 gpxf_stones(vec2 xz, float cell, float density, float tallness,
                 float flatten, float bury, float tilt, float spread,
                 float elongation, float rough, float facet, float bumpy,
                 float variation, float height_var, float size_step,
                 float cluster, float cluster_cells,
                 uint seed, int oct){
  float total = 0.0, cover = 0.0, tone = 0.0;
  float cs = max(cell, 1e-9);
  float fill = clamp(density, 0.0, 1.0);
  float chance = min(fill * (4.0/3.0), 1.0);
  float packt = clamp((fill - 0.75) * 4.0, 0.0, 1.0);
  float pack = 1.0 + 0.5 * packt;
  float ow = 1.0;
  for (int o = 0; o < 6; ++o){
    if (o >= oct) break;
    uint oseed = seed + uint(o) * 7919u;
    float inv = 1.0 / cs;
    float fx = xz.x * inv, fz = xz.y * inv;
    float ix = floor(fx), iz = floor(fz);
    for (int dz = -1; dz <= 1; ++dz)
    for (int dx = -1; dx <= 1; ++dx){
      float cxi = ix + float(dx), czi = iz + float(dz);
      uint h = gpxf_hash_bits(vec3(cxi, 0.0, czi), oseed);
      float local_density = chance * ow;
      float cgx = 0.0, cgz = 0.0;
      if (cluster > 0.0){
        float inv_cc = 1.0 / max(cluster_cells, 1.0);
        vec3 cn = gpxf_cluster_at(cxi * inv_cc, czi * inv_cc, oseed ^ 0x5bd1u);
        cgx = cn.y; cgz = cn.z;
        local_density *= 1.0 - cluster + cluster * cn.x * 2.0;
      }
      float exist = float(h & 0xfffu) * (1.0/4095.0);
      if (exist > local_density) continue;
      uint h2 = gpxf_hash_bits(vec3(cxi, 1.0, czi), oseed);
      float ox = float((h >> 12u) & 0x3ffu) * (1.0/1023.0);
      float oz = float((h >> 22u) & 0x3ffu) * (1.0/1023.0);
      // heaped together against the middle of the drift, or, below zero,
      // pushed apart by taking the jitter out
      if (cluster > 0.0){
        float tx = 0.5 + 0.5 * clamp(cgx * 2.0, -1.0, 1.0);
        float tz = 0.5 + 0.5 * clamp(cgz * 2.0, -1.0, 1.0);
        ox += (tx - ox) * cluster * 0.75;
        oz += (tz - oz) * cluster * 0.75;
      } else if (cluster < 0.0){
        float k = 1.0 + cluster * 0.85;
        ox = 0.5 + (ox - 0.5) * k;
        oz = 0.5 + (oz - 0.5) * k;
      }
      float t = float(h2 & 0x3ffu) * (1.0/1023.0);
      float sz = 1.0 - spread + spread * t * t * t;
      sz += (1.0 - sz) * 0.5 * packt;
      float rad = 0.5 * sz * pack;
      uint h3 = gpxf_hash_bits(vec3(cxi, 2.0, czi), oseed);
      float ddx = fx - (cxi + ox), ddz = fz - (czi + oz);
      // no angles: a turn is a hashed unit vector, and the harmonics that
      // roughen the outline are the multiple-angle identities on it
      float vx = float(h3 & 0x3ffu) * (2.0/1023.0) - 1.0;
      float vz = float((h3 >> 10u) & 0x3ffu) * (2.0/1023.0) - 1.0;
      float vl = sqrt(vx*vx + vz*vz);
      float inv_vl = vl > 1e-6 ? 1.0/vl : 1.0;
      float cr = vx * inv_vl, sr = vz * inv_vl;
      float rx = ddx * cr + ddz * sr, rz = -ddx * sr + ddz * cr;
      float aspect = 1.0 + elongation * 2.0 * float((h3 >> 20u) & 0x3ffu) * (1.0/1023.0);
      float ex = rx / aspect, ez = rz;
      float rr = sqrt(ex*ex + ez*ez);
      float inv_rr = rr > 1e-9 ? 1.0/rr : 0.0;
      float c1 = ex * inv_rr, s1 = ez * inv_rr;
      float c2 = c1*c1 - s1*s1, s2 = 2.0*c1*s1;
      float c3 = c1*c2 - s1*s2, s3 = s1*c2 + c1*s2;
      float c5 = c3*c2 - s3*s2, s5 = s3*c2 + c3*s2;
      float q1 = float((h3 >> 30u) & 0x3u) * (2.0/3.0) - 1.0;
      float q2 = float((h2 >> 30u) & 0x3u) * (2.0/3.0) - 1.0;
      float wob = 1.0 + rough * (0.13 * (s3 * (1.0 - abs(q1)) + c3 * q1) +
                                 0.07 * (s5 * (1.0 - abs(q2)) + c5 * q2));
      rr /= max(wob, 0.2);
      float r2 = (rr*rr) / (rad*rad);
      if (r2 >= 1.0) continue;
      float base = 1.0 - r2;
      float e = 0.35 + 0.5 * float((h2 >> 10u) & 0xffu) * (1.0/255.0);
      float prof = pow(base, e);
      uint h4 = gpxf_remix(h2), h5 = gpxf_remix(h3), h6 = gpxf_remix2(h4);
      // how far this stone departs from the field's average shape
      float vA = float(h5 & 0xffu) * (1.0/255.0);
      float vB = float(h6 & 0xffu) * (1.0/255.0);
      float s_flat  = min(flatten * (1.0 - variation + variation * 2.0 * vA), 1.0);
      float s_facet = min(facet   * (1.0 - variation + variation * 2.0 * vB), 1.0);
      float s_bumpy = min(bumpy   * (1.0 - variation + variation * 2.0 * (1.0 - vA)), 1.0);
      prof = min(prof / max(1.0 - s_flat * 0.85, 0.15), 1.0);
      if (facet > 0.0){
        float ux = ex * inv_rr, uz = ez * inv_rr;
        float r01 = sqrt(max(r2, 0.0));
        float cut = 1.0;
        for (int k = 0; k < 2; ++k){
          uint hk = k == 0 ? h4 : h5;
          float nx = float((hk >> 8u) & 0xffu) * (2.0/255.0) - 1.0;
          float nz = float((hk >> 16u) & 0xffu) * (2.0/255.0) - 1.0;
          float nl = sqrt(nx*nx + nz*nz);
          float inv_nl = nl > 1e-6 ? 1.0/nl : 1.0;
          float off = 0.35 + 0.5 * float((hk >> 24u) & 0xffu) * (1.0/255.0);
          cut = min(cut, off - r01 * (ux * nx + uz * nz) * inv_nl);
        }
        prof *= 1.0 - s_facet * (1.0 - clamp(cut * 2.0, 0.0, 1.0));
      }
      if (bumpy > 0.0)
        prof *= 1.0 + s_bumpy * 0.22 * (s3 * (2.0 * base - 1.0) + c5 * (1.0 - base));
      float lx = float((h2 >> 18u) & 0x3fu) * (2.0/63.0) - 1.0;
      float lz = float((h2 >> 24u) & 0x3fu) * (2.0/63.0) - 1.0;
      float ll = sqrt(lx*lx + lz*lz);
      float inv_ll = ll > 1e-6 ? 1.0/ll : 1.0;
      float lean = (ex * lx * inv_ll + ez * lz * inv_ll) / rad;
      prof += tilt * lean * base * 0.5;
      float hv = float(h4 & 0x3fu) * (1.0/63.0);
      float tall = 1.0 + height_var * (hv - 0.5) * 1.6;
      float H = rad * cs * tallness * tall;
      float hs = H * prof - bury * H;
      if (hs <= 0.0) continue;
      if (hs > total){
        total = hs;
        tone = float((h6 >> 8u) & 0xffffu) * (1.0/65535.0);
      }
      cover = max(cover, min(base * 3.0, 1.0));
    }
    cs *= 0.5;
    ow *= size_step;
  }
  return vec3(total, cover, tone);
}
// A sward of grass, mirroring gpx::grass::field (gpx/grass.hpp) line for
// line. Returns (height, coverage, per-tuft shade). Built on the stones'
// lattice and reusing gpxf_remix / gpxf_remix2 / gpxf_cluster_at above,
// because the placement problem is the same one; what differs is the tuft.
vec3 gpxf_grass(vec2 xz, float cell, float density, float height,
                float sharp, float blade, float fineness, float wind,
                float wind_x, float wind_z, float bend, float spread,
                float variation, float height_var, float size_step,
                float cluster, float cluster_cells,
                float bare, float bare_cells, uint seed, int oct){
  float total = 0.0, cover = 0.0, tone = 0.0;
  float cs = max(cell, 1e-9);
  float fill = clamp(density, 0.0, 1.0);
  float chance = min(fill * (4.0/3.0), 1.0);
  float packt = clamp((fill - 0.75) * 4.0, 0.0, 1.0);
  float pack = 1.0 + 0.5 * packt;
  float fn = clamp(fineness, 0.0, 1.0) * 2.0;
  float w3 = max(0.0, 1.0 - fn);
  float w5 = max(0.0, 1.0 - abs(fn - 1.0));
  float w7 = max(0.0, fn - 1.0);
  float wl = sqrt(wind_x * wind_x + wind_z * wind_z);
  float inv_wl = wl > 1e-6 ? 1.0 / wl : 1.0;
  float wx = wind_x * inv_wl, wz = wind_z * inv_wl;
  float ow = 1.0;
  for (int o = 0; o < 5; ++o){
    if (o >= oct) break;
    uint oseed = seed + uint(o) * 7919u;
    float inv = 1.0 / cs;
    float fx = xz.x * inv, fz = xz.y * inv;
    float ix = floor(fx), iz = floor(fz);
    for (int dz = -1; dz <= 1; ++dz)
    for (int dx = -1; dx <= 1; ++dx){
      float cxi = ix + float(dx), czi = iz + float(dz);
      uint h = gpxf_hash_bits(vec3(cxi, 0.0, czi), oseed);
      float local_density = chance * ow;
      float cgx = 0.0, cgz = 0.0;
      if (cluster > 0.0){
        float inv_cc = 1.0 / max(cluster_cells, 1.0);
        vec3 cn = gpxf_cluster_at(cxi * inv_cc, czi * inv_cc, oseed ^ 0x5bd1u);
        cgx = cn.y; cgz = cn.z;
        local_density *= 1.0 - cluster + cluster * cn.x * 2.0;
      }
      if (bare > 0.0){
        float inv_bc = 1.0 / max(bare_cells, 1.0);
        vec3 bn = gpxf_cluster_at(cxi * inv_bc, czi * inv_bc, oseed ^ 0x1a7du);
        float open = clamp((bn.x - 0.35) * 3.0, 0.0, 1.0);
        local_density *= 1.0 - bare + bare * open;
      }
      float exist = float(h & 0xfffu) * (1.0/4095.0);
      if (exist > local_density) continue;
      uint h2 = gpxf_hash_bits(vec3(cxi, 1.0, czi), oseed);
      float ox = float((h >> 12u) & 0x3ffu) * (1.0/1023.0);
      float oz = float((h >> 22u) & 0x3ffu) * (1.0/1023.0);
      if (cluster > 0.0){
        float tx = 0.5 + 0.5 * clamp(cgx * 2.0, -1.0, 1.0);
        float tz = 0.5 + 0.5 * clamp(cgz * 2.0, -1.0, 1.0);
        ox += (tx - ox) * cluster * 0.75;
        oz += (tz - oz) * cluster * 0.75;
      } else if (cluster < 0.0){
        float k = 1.0 + cluster * 0.85;
        ox = 0.5 + (ox - 0.5) * k;
        oz = 0.5 + (oz - 0.5) * k;
      }
      float t = float(h2 & 0x3ffu) * (1.0/1023.0);
      float sz = 1.0 - spread + spread * t * t * t;
      sz += (1.0 - sz) * 0.5 * packt;
      float rad = 0.5 * sz * pack;
      uint h3 = gpxf_hash_bits(vec3(cxi, 2.0, czi), oseed);
      float ddx = fx - (cxi + ox), ddz = fz - (czi + oz);
      float rr = sqrt(ddx*ddx + ddz*ddz);
      float r2 = (rr*rr) / (rad*rad);
      if (r2 >= 1.0) continue;
      float base = 1.0 - r2;
      float inv_rr = rr > 1e-9 ? 1.0/rr : 0.0;
      float c1 = ddx * inv_rr, s1 = ddz * inv_rr;
      float c2 = c1*c1 - s1*s1, s2 = 2.0*c1*s1;
      float c3 = c1*c2 - s1*s2, s3 = s1*c2 + c1*s2;
      float c5 = c3*c2 - s3*s2, s5 = s3*c2 + c3*s2;
      float c7 = c5*c2 - s5*s2, s7 = s5*c2 + c5*s2;
      uint h4 = gpxf_remix(h2), h5 = gpxf_remix(h3), h6 = gpxf_remix2(h4);
      float vA = float(h5 & 0xffu) * (1.0/255.0);
      float vB = float(h6 & 0xffu) * (1.0/255.0);
      float s_sharp = clamp(sharp * (1.0 - variation + variation * 2.0 * vA), 0.0, 1.0);
      float s_blade = min(blade * (1.0 - variation + variation * 2.0 * vB), 1.0);
      float e = 0.5 + s_sharp * 3.0;
      float prof = pow(base, e);
      if (blade > 0.0){
        float q1 = float((h3 >> 30u) & 0x3u) * (2.0/3.0) - 1.0;
        float q2 = float((h2 >> 30u) & 0x3u) * (2.0/3.0) - 1.0;
        float aq = 1.0 - abs(q1);
        float ripple = w3 * (s3 * aq + c3 * q1) +
                       w5 * (s5 * aq + c5 * q1) +
                       w7 * (s7 * (1.0 - abs(q2)) + c7 * q2);
        prof *= 1.0 + s_blade * 0.55 * ripple * base * (1.0 - base) * 4.0;
      }
      float hv = float(h4 & 0x3fu) * (1.0/63.0);
      float tall = 1.0 + height_var * (hv - 0.5) * 2.2;
      if (wind > 0.0){
        float along = (ddx * wx + ddz * wz) / rad;
        float lean = wind * (1.0 - bend + bend * tall);
        prof += lean * along * base * 0.5;
      }
      float H = rad * cs * height * tall;
      float hs = H * prof;
      if (hs <= 0.0) continue;
      if (hs > total){
        total = hs;
        tone = float((h6 >> 8u) & 0xffffu) * (1.0/65535.0);
      }
      cover = max(cover, min(base * 3.0, 1.0));
    }
    cs *= 0.5;
    ow *= size_step;
  }
  return vec3(total, cover, tone);
}
// guarded division: the CPU side returns 0 rather than NaN, so must this
float gpxf_div(float a, float b){ return abs(b) > 1e-9 ? a/b : 0.0; }
// RGB <-> HSV, mirroring gpx::rgb_to_hsv / hsv_to_rgb (gpx/color_math.hpp)
vec3 gpxf_rgb2hsv(vec3 c){
  float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b));
  float d = mx - mn, h = 0.0;
  if (d > 1e-9) {
    if (mx == c.r) h = mod((c.g - c.b) / d, 6.0);
    else if (mx == c.g) h = (c.b - c.r) / d + 2.0;
    else h = (c.r - c.g) / d + 4.0;
    h /= 6.0; if (h < 0.0) h += 1.0;
  }
  return vec3(h, mx > 1e-9 ? d / mx : 0.0, mx);
}
vec3 gpxf_hsv2rgb(vec3 c){
  float h = fract(c.x) * 6.0, s = clamp(c.y, 0.0, 1.0), v = c.z;
  int i = int(floor(h)); float f = h - float(i);
  float p = v * (1.0 - s), q = v * (1.0 - s * f), t = v * (1.0 - s * (1.0 - f));
  if (i == 0) return vec3(v, t, p); if (i == 1) return vec3(q, v, p);
  if (i == 2) return vec3(p, v, t); if (i == 3) return vec3(p, q, v);
  if (i == 4) return vec3(t, p, v); return vec3(v, p, q);
}
// Soft membership of a band. Deliberately not smoothstep: smoothstep(e,e,x) is
// undefined when the edges coincide, which is exactly what zero fuzziness asks
// for. Mirrors band() in nodes_field_material.cpp.
float gpxf_band(float x, float lo, float hi, float fuzz){
  if (fuzz <= 1e-6) return (x >= lo && x <= hi) ? 1.0 : 0.0;
  float a = clamp((x - (lo - fuzz)) / (2.0 * fuzz), 0.0, 1.0);
  float b = clamp(((hi + fuzz) - x) / (2.0 * fuzz), 0.0, 1.0);
  a = a*a*(3.0-2.0*a);
  b = b*b*(3.0-2.0*b);
  return min(a, b);
}
)GLSL";

const char *field_glsl_prelude() { return PRELUDE; }

std::string field_glsl_strip_prelude(const std::string &code) {
  const size_t n = std::strlen(PRELUDE);
  if (code.compare(0, n, PRELUDE) == 0) return code.substr(n);
  return code; // already stripped, or not one of ours
}

// ----------------------------------------------------------------- emitters

// the transpiler internals live in glslgen (field_glsl_internal.hpp); the
// compiler half uses them as its own
using namespace glslgen;


bool field_glsl_supports(const std::string &node_type) {
  install_emitters();
  return emitters().count(node_type) > 0;
}

// -------------------------------------------------------------- compilation
static bool emit_node(const Node &n, const std::string &out_port, EmitCtx &ctx,
                      std::set<uint64_t> &visiting);

// Reading a vec4 as a scalar, exactly as FieldValue::number() does on the CPU.
// The permissive conversions are the point — a user may wire a colour into a
// number slot — so the two implementations have to agree on what that means,
// or a graph will look different on the GPU for no visible reason.
static std::string as_number(const std::string &v, FieldType t) {
  switch (t) {
    case FieldType::Color:
      return "dot(" + v + ".rgb, vec3(0.299, 0.587, 0.114))"; // luminance
    case FieldType::Vector:
      return "length(" + v + ".xyz)";
    default:
      return v + ".x";
  }
}
// The same for FieldValue::as_vector(): a vector or colour passes through, and
// anything scalar broadcasts to all three components rather than leaving two
// of them zero.
static std::string as_vec3(const std::string &v, FieldType t) {
  if (t == FieldType::Vector || t == FieldType::Color) return v + ".xyz";
  return "vec3(" + v + ".x)";
}
// And FieldValue::as_color(): a colour keeps its alpha, anything else becomes
// grey at full opacity. Forcing alpha to 1 for a real colour would quietly
// throw away transparency.
static std::string as_vec4(const std::string &v, FieldType t) {
  if (t == FieldType::Color) return v;
  return "vec4(vec3(" + as_number(v, t) + "), 1.0)";
}

// And FieldValue::as_texcoord(): texture coordinates pass through, a vector
// is read on the ground plane, a colour gives (r, g), a number fills both.
static std::string as_vec2(const std::string &v, FieldType t) {
  switch (t) {
    case FieldType::TexCoord: return v + ".xy";
    case FieldType::Vector: return v + ".xz";
    case FieldType::Color: return v + ".xy";
    default: return "vec2(" + v + ".x)";
  }
}

// Which value type feeds an input, or Number when nothing does. Converter
// nodes branch on this at compile time exactly as their CPU half branches on
// FieldValue::type, so the two halves take the same path for the same graph.
static FieldType upstream_type(const Node &n, const char *port) {
  const Port *src_port = n.graph ? n.graph->upstream(n, port) : nullptr;
  return src_port ? src_port->field_type : FieldType::Number;
}

// Resolve one input port to a GLSL expression. A leading '#' asks for the vec3
// form (vector inputs), '@' for the vec4 form (colour inputs), '%' for the
// vec2 form (texture coordinates) and '!' for the raw vec4 with no conversion
// at all — the layout FieldValue::v has, for converters that pick lanes by
// hand; otherwise the scalar form. The prefix picks which FieldValue
// conversion to mirror.
static std::string resolve_input(const Node &n, EmitCtx &ctx,
                                 std::set<uint64_t> &visiting, const char *port,
                                 const char *fallback) {
  bool want_vec3 = port[0] == '#';
  bool want_vec4 = port[0] == '@';
  bool want_vec2 = port[0] == '%';
  bool want_raw = port[0] == '!';
  std::string pname =
      (want_vec3 || want_vec4 || want_vec2 || want_raw) ? port + 1 : port;
  // Which output feeds us matters: a node may produce several, and they are
  // different values, not different views of one.
  const Port *src_port = n.graph ? n.graph->upstream(n, pname) : nullptr;
  const Node *src = n.graph ? n.graph->upstream_node(n, pname) : nullptr;
  if (!src || !src_port) return fallback;
  if (!emit_node(*src, src_port->name, ctx, visiting)) return fallback;
  auto it = ctx.var_of.find(ctx.key(src->id, src_port->name));
  if (it == ctx.var_of.end()) return fallback;
  if (want_raw) return it->second;
  if (want_vec3) return as_vec3(it->second, src_port->field_type);
  if (want_vec4) return as_vec4(it->second, src_port->field_type);
  if (want_vec2) return as_vec2(it->second, src_port->field_type);
  return as_number(it->second, src_port->field_type);
}

// The same, but evaluated somewhere else. Everything upstream is re-emitted
// under the new point, because under a redirect it genuinely is a different
// value; the scoped cache keeps a subtree shared within one point.
static std::string resolve_input_at(const Node &n, EmitCtx &ctx,
                                    std::set<uint64_t> &visiting,
                                    const char *port, const std::string &pos,
                                    const std::string &alt,
                                    const std::string &lod,
                                    const char *fallback) {
  std::string save_pos = ctx.pos, save_alt = ctx.alt, save_lod = ctx.lod;
  ctx.pos = pos;
  ctx.alt = alt;
  ctx.lod = lod;
  std::string r = resolve_input(n, ctx, visiting, port, fallback);
  ctx.pos = save_pos;
  ctx.alt = save_alt;
  ctx.lod = save_lod;
  return r;
}

static bool emit_node(const Node &n, const std::string &out_port, EmitCtx &ctx,
                      std::set<uint64_t> &visiting) {
  const std::string k = ctx.key(n.id, out_port);
  if (ctx.var_of.count(k)) return true; // already emitted here; reuse the value
  if (visiting.count(n.id)) {
    ctx.error = "cycle through node " + n.type;
    return false;
  }
  install_emitters();
  // the port-specific emitter if there is one, otherwise the node's primary
  auto e = emitters().find(n.type + "\x1f" + out_port);
  if (e == emitters().end()) e = emitters().find(n.type);
  if (e == emitters().end()) {
    ctx.error = "node type '" + n.type + "' has no GLSL emitter";
    return false;
  }
  visiting.insert(n.id);
  InputFn in;
  in.f = [&](const char *port, const char *fallback) {
    return resolve_input(n, ctx, visiting, port, fallback);
  };
  in.f_at = [&](const char *port, const std::string &pos, const std::string &alt,
                const std::string &lod, const char *fallback) {
    return resolve_input_at(n, ctx, visiting, port, pos, alt, lod, fallback);
  };
  in.f_type = [&](const char *port) { return upstream_type(n, port); };
  in.f_connected = [&](const char *port) {
    return n.graph && n.graph->upstream(n, port) != nullptr;
  };
  std::string expr = e->second.emit(n, in, ctx);
  visiting.erase(n.id);
  if (!ctx.error.empty()) return false;
  std::string var = ctx.fresh("n");
  ctx.body << "  vec4 " << var << " = " << expr << ";\n";
  // key under the scope that was current when the emitter *started*: an
  // emitter may have moved the point while resolving its own inputs
  ctx.var_of[k] = var;
  return true;
}

GlslProgram field_to_glsl(const Node &node, const std::string &out_port,
                          const std::string &fn_name) {
  GlslProgram prog;
  prog.entry = fn_name;
  install_emitters();

  EmitCtx ctx;
  std::set<uint64_t> visiting;
  // default to the node's first field output when the caller did not name one
  std::string port = out_port;
  if (port.empty()) {
    for (const Port &p : node.ports)
      if (p.dir == PortDir::Out && p.type == DataType::Field) {
        port = p.name;
        break;
      }
  }
  if (!emit_node(node, port, ctx, visiting)) {
    prog.error = ctx.error.empty() ? "could not emit graph" : ctx.error;
    return prog;
  }
  auto it = ctx.var_of.find(ctx.key(node.id, port));
  if (it == ctx.var_of.end()) {
    prog.error = "output node produced no value";
    return prog;
  }

  std::ostringstream out;
  out << PRELUDE;
  for (const std::string &s : ctx.samplers)
    out << "uniform sampler2D " << s << ";\n";
  out << "\nvec4 " << fn_name
      << "(vec3 P, vec3 N, float alt, float slope, float orient, float t, "
         "float lod){\n";
  out << ctx.body.str();
  out << "  return " << it->second << ";\n}\n";

  prog.code = out.str();
  prog.samplers = ctx.samplers;
  prog.node_count = (int)ctx.var_of.size();
  prog.ok = true;
  return prog;
}

} // namespace gpx
