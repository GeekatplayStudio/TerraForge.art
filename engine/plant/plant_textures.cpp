// Geekatplay TerraForge - leaf and petal pictures made from rules.
//
// A species described in words has no picture to lend its leaves, so the
// leaves paint themselves: an outline for the shape, a mid-rib and the
// side veins that leave it at about forty-five degrees, a paler middle and
// a darker rim, a mottle of noise so no two patches of the same green look
// printed, and browning that creeps in from the edge as the leaf wilts.
// The alpha is one inside the outline and zero outside with a two-pixel
// soft edge, and the outline is traced back out of it so a cut-out leaf
// can be cut to the very shape that was drawn.
//
// Coordinates: the picture is square; the leaf stands upright and fills
// the height, its stalk at the bottom centre - the hook point (0.5, 1) is
// the petiole. `t` runs 0 at the stalk to 1 at the tip and `s` is the
// sideways distance from the mid-line, both in units of the leaf's length,
// so a shape is a half-width function W(t) plus, for the compound shapes,
// a row of leaflets that reuse the same function in their own frame. The
// signed distance to the edge is approximate (sideways, and along the axis
// past the tip) but it is what the two-pixel edge needs.
//
// Every draw comes from gpx::noise seeded by the texture's `seed` alone,
// so one seed is one picture on every machine.
#include "gpx/noise_core.hpp"
#include "gpx/plant.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace gpx {

namespace {

constexpr float PI = 3.14159265f;

float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
float smoothstep(float a, float b, float x) {
  float t = clamp01((x - a) / (b - a));
  return t * t * (3 - 2 * t);
}
float sawtooth(float x) { return x - std::floor(x); }

enum class Shape { Ovate, Lanceolate, Lobed, Palmate, Needle, Scale, Pinnate, Heart, Linear, Round, Elliptic, Frond, Blade, Spray };

Shape shape_of(std::string s) {
  for (char &c : s) c = (char)std::tolower((unsigned char)c);
  if (s == "lanceolate") return Shape::Lanceolate;
  if (s == "lobed") return Shape::Lobed;
  if (s == "palmate") return Shape::Palmate;
  if (s == "needle") return Shape::Needle;
  if (s == "needle spray" || s == "spray") return Shape::Spray;
  if (s == "scale") return Shape::Scale;
  if (s == "pinnate") return Shape::Pinnate;
  if (s == "heart") return Shape::Heart;
  if (s == "linear") return Shape::Linear;
  if (s == "round") return Shape::Round;
  if (s == "elliptic") return Shape::Elliptic;
  if (s == "frond") return Shape::Frond;
  if (s == "blade") return Shape::Blade;
  return Shape::Ovate;
}

// A beta-like profile, widest at p/(p+q), normalised to 1 at its peak.
float beta_profile(float t, float p, float q) {
  if (t <= 0 || t >= 1) return 0;
  float tm = p / (p + q);
  float peak = std::pow(tm, p) * std::pow(1 - tm, q);
  return std::pow(t, p) * std::pow(1 - t, q) / peak;
}

// Half-width of a simple leaf blade at t, for half-width A at the widest.
// Lobes are a cosine wave along the edge; serration a sawtooth on top.
float half_width(Shape sh, float t, float A, const PlantLeafTexture &tx) {
  float w;
  switch (sh) {
  case Shape::Lanceolate: w = A * beta_profile(t, 0.6f, 2.2f); break;
  case Shape::Elliptic: w = A * beta_profile(t, 0.8f, 0.8f); break;
  case Shape::Round: w = A * beta_profile(t, 0.5f, 0.5f); break;
  case Shape::Heart: w = A * beta_profile(t, 0.45f, 1.3f); break;
  case Shape::Scale: w = A * beta_profile(t, 1.2f, 0.45f); break;
  case Shape::Linear: w = A * std::min({1.f, 6 * t, 3 * (1 - t)}); break;
  case Shape::Needle: w = A * std::min({1.f, 10 * t, 4 * (1 - t)}); break;
  case Shape::Blade: w = A * std::min(1.f, 12 * t) * std::pow(std::max(0.f, 1 - t), 0.55f); break;
  case Shape::Lobed: {
    w = A * beta_profile(t, 0.7f, 0.9f);
    int per_side = std::max(2, (tx.lobe_count + 1) / 2);
    float depth = tx.lobes > 0 ? tx.lobes : 0.5f;
    float phase = (t - 0.1f) / 0.85f * per_side;             // lobes between t 0.1 and 0.95
    float lobe = 0.5f + 0.5f * std::cos(2 * PI * (phase - 0.5f)); // 1 on a lobe, 0 in a sinus
    lobe = std::pow(lobe, 0.6f);
    w *= 1 - 0.55f * depth * (1 - lobe);
    break;
  }
  default: w = A * beta_profile(t, 0.55f, 1.1f); break; // ovate
  }
  if (tx.serration > 0 && sh != Shape::Needle && sh != Shape::Scale) {
    float teeth = 22.f;
    w += tx.serration * A * 0.08f * (sawtooth(t * teeth) - 0.5f) * smoothstep(0.02f, 0.15f, t) * smoothstep(1.f, 0.9f, t);
  }
  return std::max(w, 0.f);
}

// Where a pixel sits on the leaf: the signed distance to the edge (units
// of length, negative inside), and its own (t, s, half-width) frame for
// the veins and the shading - a leaflet's own frame on a compound leaf.
struct Local {
  float dist = 1.f, t = 0.f, s = 0.f, hw = 1.f;
  float vein = 0.f;  // extra vein mask the shape itself supplies (palmate rays, rachis)
};

Local simple_leaf(Shape sh, float t, float s, float A, const PlantLeafTexture &tx) {
  Local L;
  L.t = t;
  L.s = s;
  L.hw = std::max(half_width(sh, clamp01(t), A, tx), 1e-4f);
  L.dist = std::max({s - L.hw, t - 1.f, -t});
  if (sh == Shape::Heart) {
    // the notch at the base: two rounded lobes either side of the stalk
    float nd = 0.14f * (1 - s / (0.7f * A));
    if (nd > 0) L.dist = std::max(L.dist, nd - t);
  }
  return L;
}

// A leaflet on a rachis: the simple ovate/lanceolate blade in a frame at
// (t0, side) leaving the rachis at `angle` from it, of length `len`.
Local leaflet(float t, float s_signed, float t0, float angle, float len, float width, bool narrow, const PlantLeafTexture &tx) {
  float dx = std::fabs(s_signed), dy = t - t0;
  float ca = std::cos(angle), sa = std::sin(angle);
  float along = (dx * ca + dy * sa) / len;
  float across = std::fabs(-dx * sa + dy * ca) / len;
  Local L = simple_leaf(narrow ? Shape::Lanceolate : Shape::Ovate, along, across, width, tx);
  L.dist *= len;
  L.t = along;
  L.s = across;
  L.hw *= len;
  L.s *= len;
  return L;
}

Local compound_leaf(bool frond, float t, float s_signed, float A, const PlantLeafTexture &tx) {
  Local best;
  best.dist = 1.f;
  const float rachis_w = frond ? 0.008f : 0.011f;
  // the rachis itself
  float rd = std::max({std::fabs(s_signed) - rachis_w, -t, t - (frond ? 0.97f : 0.86f)});
  best.dist = rd;
  best.t = t;
  best.s = std::fabs(s_signed);
  best.hw = rachis_w;
  best.vein = 1.f;
  int pairs = frond ? std::max(8, tx.lobe_count * 2) : std::max(3, tx.lobe_count);
  float first = frond ? 0.06f : 0.1f, last = frond ? 0.9f : 0.8f;
  for (int i = 0; i < pairs; ++i) {
    float f = pairs > 1 ? (float)i / (float)(pairs - 1) : 0.f;
    float t0 = first + f * (last - first);
    float taper = frond ? 0.35f + 0.65f * std::sin(PI * (0.1f + 0.8f * f)) : 1.f - 0.25f * f;
    float len = (frond ? A * 1.05f : A * 0.95f) * taper;
    float angle = frond ? (55.f - 25.f * f) * PI / 180.f : 50.f * PI / 180.f;
    float width = frond ? 0.13f : 0.24f;
    Local L = leaflet(t, s_signed, t0, angle, len, width, frond, tx);
    if (L.dist < best.dist) best = L;
  }
  // the terminal leaflet on a pinnate leaf points straight up
  if (!frond) {
    float len = A * 0.7f;
    Local L = simple_leaf(Shape::Ovate, (t - 0.82f) / len, std::fabs(s_signed) / len, 0.24f, tx);
    L.dist *= len;
    L.hw *= len;
    L.s *= len;
    if (L.dist < best.dist) best = L;
  }
  return best;
}

// A spray of needles on a shoot: what a conifer's foliage actually is, and
// what a card standing in for one has to carry.
//
// A single needle drawn across a whole picture leaves it 6% opaque, and a
// card wearing it is 94% hole. A pine built from those came out as bare
// brown wood with a green tinge at any distance, because almost nothing of
// the picture was ever there. A spray fills the card the way a real shoot
// does: needles in pairs along a shoot, angled forward, shortening toward
// the tip - around forty per cent opaque, which is what reads as foliage.
Local needle_spray(float t, float s_signed, float A, const PlantLeafTexture &tx) {
  Local best;
  best.dist = 1.f;
  // the shoot the needles grow from
  best.dist = std::max({std::fabs(s_signed) - 0.012f, -t, t - 0.95f});
  best.t = t;
  best.s = std::fabs(s_signed);
  best.hw = 0.012f;
  best.vein = 1.f;
  const int pairs = std::max(7, tx.lobe_count * 3);
  const float first = 0.04f, last = 0.93f;
  for (int i = 0; i < pairs; ++i) {
    const float f = pairs > 1 ? (float)i / (float)(pairs - 1) : 0.f;
    const float t0 = first + f * (last - first);
    // they shorten toward the tip, and the pair alternates about the shoot
    const float len = A * (1.15f - 0.45f * f);
    const float angle = (38.f + 10.f * f) * PI / 180.f;
    for (int side = 0; side < 2; ++side) {
      const float ss = side == 0 ? s_signed : -s_signed;
      Local L = leaflet(t, ss, t0 + (side ? 0.5f / (float)pairs : 0.f), angle, len, 0.055f, true, tx);
      if (L.dist < best.dist) best = L;
    }
  }
  return best;
}

// A palmate leaf in polar coordinates round a point a little above the
// stalk: pointed lobes on an ellipse, the lower ones shorter, and a ray
// vein to every lobe tip.
Local palmate_leaf(float t, float s_signed, float A, const PlantLeafTexture &tx) {
  const float cy = 0.18f;
  float dx = s_signed, dy = t - cy;
  float r = std::sqrt(dx * dx + dy * dy);
  float th = std::atan2(dx, dy); // 0 up the mid-line, +-pi down
  int n = std::max(3, tx.lobe_count | 1);
  float depth = tx.lobes > 0 ? tx.lobes : 0.55f;
  float spread = std::min(PI * 0.62f, PI * 0.16f * (float)n); // half-angle the lobes cover
  float step = 2 * spread / (float)(n - 1);
  float k = std::round(th / step);
  float lobe_angle = k * step;
  float ray = 0.f, bump = 0.f;
  if (std::fabs(lobe_angle) <= spread + 1e-4f) {
    float d = std::fabs(th - lobe_angle) / (step * 0.5f); // 0 on the lobe, 1 in the sinus
    bump = std::pow(std::max(0.f, 1 - d), 0.8f);
    ray = smoothstep(0.02f, 0.f, std::fabs(th - lobe_angle) * r);
  }
  float ell = 1.f / std::sqrt(std::pow(std::cos(th) / (1 - cy), 2.f) + std::pow(std::sin(th) / A, 2.f));
  float shorten = 0.45f + 0.55f * std::cos(std::min(std::fabs(th), spread) / spread * PI * 0.5f);
  float R = ell * shorten * (1 - depth + depth * bump);
  if (std::fabs(th) > spread) R = ell * 0.45f * std::max(0.35f, std::cos((std::fabs(th) - spread) * 1.2f));
  if (tx.serration > 0) R += tx.serration * 0.03f * (sawtooth(th * 12.f) - 0.5f);
  Local L;
  L.dist = std::max(r - R, -t);
  L.t = clamp01(r / std::max(R, 1e-4f));
  L.s = L.t;
  L.hw = 1.f;
  L.vein = ray * smoothstep(0.0f, 0.1f, r / std::max(R, 1e-4f));
  return L;
}

// Side veins leaving the mid-rib at about 45 degrees, bowing toward the
// tip, plus the mid-rib itself; 1 on a vein.
float vein_mask(const Local &L, float length_px) {
  float t = L.t, s = std::fabs(L.s);
  float rib_w = (0.006f + 0.006f * (1 - t)) * (t < 0.97f ? 1.f : 0.f);
  float rib = smoothstep(rib_w, rib_w * 0.4f, s);
  const float spacing = 0.085f;
  float k = std::round((t - s * 1.1f) / spacing);
  float t0 = k * spacing;
  float rel = t - t0;
  float sv = rel - 0.45f * rel * rel; // bows forward
  float d = std::fabs(s - sv) * 0.7f;
  float vw = 0.0035f + 0.5f / std::max(length_px, 64.f);
  float side = smoothstep(vw, vw * 0.35f, d) * (t0 > 0.05f && t0 < 0.92f ? 1.f : 0.f);
  side *= smoothstep(L.hw, L.hw * 0.85f, s); // fade before the edge
  return std::max({rib, side * 0.7f, L.vein});
}

} // namespace

bool plant_texture_leaf(const PlantLeafTexture &tx, int w, int h, std::vector<uint8_t> &rgba,
                        std::vector<float> &cutout_uv, std::vector<uint8_t> *normal_rgba,
                        std::vector<uint8_t> *rough_rgba) {
  rgba.clear();
  cutout_uv.clear();
  // The leaf's own surface, kept as it is drawn so the normal and the
  // roughness can be read off it afterwards: a midrib standing proud, the
  // blade dished either side, the secondaries ridged across it, and a fine
  // grain over the whole. Height is what turns a printed card into a leaf.
  std::vector<float> height, gloss;
  if (normal_rgba) height.assign((size_t)w * h, 0.f);
  if (rough_rgba) gloss.assign((size_t)w * h, 0.f);
  if (w < 4 || h < 4) return false;
  const Shape sh = shape_of(tx.shape);
  float aspect = tx.aspect > 0.02f ? tx.aspect : 0.55f;
  if (sh == Shape::Needle) aspect = std::min(aspect, 0.12f);
  if (sh == Shape::Linear) aspect = std::min(aspect, 0.2f);
  if (sh == Shape::Blade) aspect = std::min(aspect, 0.25f);
  if (sh == Shape::Round) aspect = std::max(aspect, 0.92f); // a round leaf is round
  const float A = aspect * 0.5f;
  const bool compound = sh == Shape::Pinnate || sh == Shape::Frond;
  noise::FbmParams fp;
  fp.octaves = 5;
  const float brown[3] = {0.48f, 0.32f, 0.12f};
  rgba.resize((size_t)w * h * 4);
  for (int y = 0; y < h; ++y) {
    float v = ((float)y + 0.5f) / (float)h;
    float t = 1.f - v;
    for (int x = 0; x < w; ++x) {
      float u = ((float)x + 0.5f) / (float)w;
      float s_signed = (u - 0.5f) * (float)w / (float)h;
      Local L;
      if (sh == Shape::Spray) L = needle_spray(t, s_signed, A, tx);
      else if (compound) L = compound_leaf(sh == Shape::Frond, t, s_signed, A, tx);
      else if (sh == Shape::Palmate) L = palmate_leaf(t, s_signed, A, tx);
      else L = simple_leaf(sh, t, std::fabs(s_signed), A, tx);
      float dist_px = L.dist * (float)h;
      float alpha = clamp01(0.5f - dist_px * 0.5f);
      // the stalk: a short dark stem at the very base so the hook point
      // sits on leaf
      float stem = smoothstep(0.012f, 0.006f, std::fabs(s_signed)) * (t < 0.06f ? 1.f : 0.f);
      alpha = std::max(alpha, stem);
      float edge = clamp01(std::fabs(L.s) / L.hw);     // 0 mid-line .. 1 rim
      if (sh == Shape::Palmate) edge = L.t;
      float n1 = noise::fbm(u * 9.f, v * 9.f, tx.seed, fp);
      float n2 = noise::fbm(u * 31.f + 7.f, v * 31.f, tx.seed ^ 0x51u, fp);
      float shade = 1.14f - 0.28f * edge * edge + tx.mottle * (0.12f * n1 + 0.05f * n2);
      float c[3];
      for (int k = 0; k < 3; ++k) c[k] = tx.color[k] * shade;
      float vm = vein_mask(L, (float)h) * tx.vein_strength;
      for (int k = 0; k < 3; ++k) c[k] = c[k] + (tx.vein_color[k] - c[k]) * vm;
      if (stem > 0) for (int k = 0; k < 3; ++k) c[k] = c[k] + (brown[k] * 0.7f - c[k]) * stem;
      if (tx.wilt > 0) {
        // browning creeps from the rim and the tip, ragged by noise, and
        // the whole leaf loses its saturation
        float reach = tx.wilt * 1.3f;
        float from_rim = std::max(edge, sh == Shape::Palmate ? 0.f : L.t * 0.8f);
        float wm = smoothstep(1 - reach, 1 - reach + 0.35f, from_rim + 0.25f * n1 * tx.wilt);
        float lum = 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2];
        for (int k = 0; k < 3; ++k) {
          c[k] = c[k] + (lum - c[k]) * tx.wilt * 0.35f;
          c[k] = c[k] + (brown[k] * (0.8f + 0.3f * n2) - c[k]) * wm;
        }
      }
      uint8_t *px = &rgba[((size_t)y * w + x) * 4];
      for (int k = 0; k < 3; ++k) px[k] = (uint8_t)std::lround(clamp01(c[k]) * 255.f);
      px[3] = (uint8_t)std::lround(alpha * 255.f);
      const size_t at = (size_t)y * w + x;
      if (!height.empty()) {
        // the midrib and the secondaries stand proud, the blade dishes away
        // toward the rim, and a fine grain runs over all of it
        float z = 0.5f + 0.30f * vm + 0.12f * (1.f - edge * edge) + 0.05f * n2;
        z *= clamp01(alpha * 1.4f); // the rim falls away to nothing
        height[at] = z;
      }
      if (!gloss.empty()) {
        // A leaf's cuticle is waxy and glossy; the veins are dull, the rim is
        // drier, and a wilting leaf loses its shine altogether. A uniformly
        // matte leaf is the other half of why a canopy can read as fabric.
        float r = 0.30f + 0.22f * vm + 0.12f * edge + 0.06f * n1 + tx.wilt * 0.35f;
        gloss[at] = clamp01(r);
      }
    }
  }
  // The normal from the height by central difference, the same way the bark's
  // is made, so the two light alike.
  if (normal_rgba && !height.empty()) {
    normal_rgba->assign((size_t)w * h * 4, 0);
    const float strength = 2.6f * (float)h / 512.f;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const int xm = (x + w - 1) % w, xp = (x + 1) % w;
        const int ym = (y + h - 1) % h, yp = (y + 1) % h;
        const float dx = (height[(size_t)y * w + xp] - height[(size_t)y * w + xm]) * strength;
        const float dy = (height[(size_t)yp * w + x] - height[(size_t)ym * w + x]) * strength;
        float nx = -dx, ny = -dy, nz = 1.f;
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        nx /= len;
        ny /= len;
        nz /= len;
        uint8_t *np = &(*normal_rgba)[((size_t)y * w + x) * 4];
        np[0] = (uint8_t)std::lround((nx * 0.5f + 0.5f) * 255.f);
        np[1] = (uint8_t)std::lround((ny * 0.5f + 0.5f) * 255.f);
        np[2] = (uint8_t)std::lround((nz * 0.5f + 0.5f) * 255.f);
        np[3] = 255;
      }
  }
  if (rough_rgba && !gloss.empty()) {
    rough_rgba->assign((size_t)w * h * 4, 0);
    for (size_t i = 0; i < gloss.size(); ++i) {
      const uint8_t r = (uint8_t)std::lround(clamp01(gloss[i]) * 255.f);
      uint8_t *rp = &(*rough_rgba)[i * 4];
      rp[0] = rp[1] = rp[2] = r;
      rp[3] = 255;
    }
  }
  plant_trace_cutout(rgba.data(), w, h, compound ? 48 : 28, cutout_uv);
  return true;
}

bool plant_texture_petal(const float color[3], const float base[3], uint32_t seed, int w, int h,
                         std::vector<uint8_t> &rgba, std::vector<float> &cutout_uv) {
  rgba.clear();
  cutout_uv.clear();
  if (w < 4 || h < 4) return false;
  PlantLeafTexture tx;
  tx.seed = seed;
  const float A = 0.36f;
  noise::FbmParams fp;
  fp.octaves = 4;
  rgba.resize((size_t)w * h * 4);
  for (int y = 0; y < h; ++y) {
    float v = ((float)y + 0.5f) / (float)h, t = 1.f - v;
    for (int x = 0; x < w; ++x) {
      float u = ((float)x + 0.5f) / (float)w;
      float s = std::fabs(u - 0.5f);
      // an obovate petal: widest past the middle, rounded at the tip, with
      // a slightly wavy rim
      float ripple = 0.012f * std::sin(t * 40.f + noise::perlin(u * 3.f, v * 3.f, seed) * 4.f);
      float hw = std::max(A * beta_profile(clamp01(t), 1.3f, 0.8f) + ripple * smoothstep(0.3f, 0.7f, t), 1e-4f);
      float dist = std::max({s - hw, t - 1.f, -t});
      float alpha = clamp01(0.5f - dist * (float)h * 0.5f);
      float edge = clamp01(s / hw);
      // colour runs from the base tone at the stalk to the petal colour at
      // the tip; the rim goes paler and thinner as if lit through
      float g = smoothstep(0.f, 0.75f, t);
      float n = noise::fbm(u * 6.f, v * 6.f, seed, fp);
      float c[3];
      for (int k = 0; k < 3; ++k) {
        c[k] = base[k] + (color[k] - base[k]) * g;
        c[k] *= 1.f + 0.05f * n;
        c[k] += (1.f - c[k]) * 0.35f * std::pow(edge, 3.f);
      }
      // faint veins fanning out from the base
      float phi = std::atan2(s, std::max(t, 1e-3f));
      float r = std::sqrt(s * s + t * t);
      float vk = std::round(phi / 0.11f) * 0.11f;
      float vd = std::fabs(phi - vk) * r;
      float vein = smoothstep(0.004f, 0.0015f, vd) * smoothstep(0.05f, 0.2f, r) * (1 - g * 0.5f);
      for (int k = 0; k < 3; ++k) c[k] = c[k] + (base[k] * 0.9f - c[k]) * vein * 0.25f;
      uint8_t *px = &rgba[((size_t)y * w + x) * 4];
      for (int k = 0; k < 3; ++k) px[k] = (uint8_t)std::lround(clamp01(c[k]) * 255.f);
      px[3] = (uint8_t)std::lround(alpha * (0.8f + 0.2f * (1 - std::pow(edge, 6.f))) * 255.f);
    }
  }
  plant_trace_cutout(rgba.data(), w, h, 24, cutout_uv);
  return true;
}

} // namespace gpx
