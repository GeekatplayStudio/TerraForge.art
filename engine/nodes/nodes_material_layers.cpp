// Geekatplay TerraForge — fractal colour and the layered material stack.
//
// Two nodes, both taken from the Vue reference manual and adapted to a raster
// pipeline. FractalColor is Vue's procedural colour production (p711): a
// fractal gives a number, a filter reshapes it, a colour map turns it into
// colour *and alpha*. That second output is the point — one pattern decides
// both what a layer looks like and where it exists.
//
// MaterialLayer is a layer of the stack (p761-764). It chains: each instance
// takes what is below it and returns the result including itself, so the graph
// reads bottom-up while the editor shows it top-down, as image editors do.
// Presence is kept strictly separate from the channel data: opacity, a mask,
// and independent altitude / slope / orientation constraints, multiplied. The
// manual never states how Vue combines those three, so multiplying is a choice
// and not a reading; it matches FieldDistribution, which already multiplies,
// and it gives the "steep AND high" intersection a user expects.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/color_math.hpp"
#include "gpx/fractal_core.hpp"
#include "gpx/parallel.hpp"
#include <algorithm>
#include <cmath>

namespace gpx {

namespace {

constexpr float PI_F = 3.14159265358979f;

// Soft membership of a band with a fade of `fuzz` on each side. The same
// function as FieldDistribution's, deliberately: a layer keyed on slope must
// pick the same pixels whether it is asked in the field domain or this one.
// Not GLSL smoothstep, because smoothstep(e, e, x) is undefined and a zero
// fuzziness asks for exactly that.
float band(float x, float lo, float hi, float fuzz) {
  if (fuzz <= 1e-6f) return (x >= lo && x <= hi) ? 1.f : 0.f;
  float a = std::clamp((x - (lo - fuzz)) / (2.f * fuzz), 0.f, 1.f);
  float b = std::clamp(((hi + fuzz) - x) / (2.f * fuzz), 0.f, 1.f);
  a = a * a * (3.f - 2.f * a);
  b = b * b * (3.f - 2.f * b);
  return std::min(a, b);
}

// Angular distance in degrees, shortest way round the circle.
float arc(float a, float b) {
  float d = std::fmod(std::fabs(a - b), 360.f);
  return d > 180.f ? 360.f - d : d;
}

// Bias and gain: the two-parameter reshaping pair. bias moves the midpoint,
// gain pushes values towards or away from it. Together they cover most of what
// a filter curve gets used for, without a curve editor.
float bias_gain(float v, float bias, float gain) {
  v = std::clamp(v, 0.f, 1.f);
  if (bias != 0.5f) {
    float b = std::clamp(bias, 0.01f, 0.99f);
    v = v / ((1.f / b - 2.f) * (1.f - v) + 1.f);
  }
  if (gain != 0.5f) {
    float g = std::clamp(gain, 0.01f, 0.99f);
    v = v < 0.5f ? v / ((1.f / g - 2.f) * (1.f - 2.f * v) + 1.f)
                 : ((1.f / g - 2.f) * (1.f - 2.f * v) - v) /
                       ((1.f / g - 2.f) * (1.f - 2.f * v) - 1.f);
  }
  return std::clamp(v, 0.f, 1.f);
}

// Bilinear fetch in 0..1 texture space, wrapping. Layers place their own maps,
// so a layer tiled four times has to read outside 0..1 and come back round.
void tex_wrap(const TextureRGBA &t, float u, float v, float *out) {
  u -= std::floor(u);
  v -= std::floor(v);
  float fx = u * t.w - 0.5f, fy = v * t.h - 0.5f;
  int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
  float ax = fx - x0, ay = fy - y0;
  auto wrap = [](int i, int n) { return ((i % n) + n) % n; };
  int xa = wrap(x0, t.w), xb = wrap(x0 + 1, t.w);
  int ya = wrap(y0, t.h), yb = wrap(y0 + 1, t.h);
  const float *p00 = t.px(xa, ya), *p10 = t.px(xb, ya);
  const float *p01 = t.px(xa, yb), *p11 = t.px(xb, yb);
  for (int c = 0; c < 4; ++c)
    out[c] = (p00[c] * (1 - ax) + p10[c] * ax) * (1 - ay) +
             (p01[c] * (1 - ax) + p11[c] * ax) * ay;
}

} // namespace

// ------------------------------------------------------- fractal -> colour
REGISTER_NODE(
    FractalColor, "Material",
    "A fractal through a colour map: RGBA texture out, and the same pattern as a mask",
    [](Node &n) {
      n.add_in("warp", DataType::Heightmap, true);
      n.add_in("mask", DataType::Heightmap, true);
      n.add_out("texture", DataType::Texture);
      n.add_out("mask", DataType::Heightmap);
      add_seed(n.attrs, "seed", "Seed", 0, "Fractal");
      add_choice(n.attrs, "base", "Base noise",
                 {"Perlin", "Value", "Cellular", "Cell edges", "Grainy"}, 0,
                 "Fractal");
      add_float(n.attrs, "wavelength", "Wavelength", 0.25f, 0.005f, 2.f, "Fractal")
          .tooltip = "Size of the largest feature, in tile widths.";
      add_int(n.attrs, "octaves", "Iterations", 8, 1, 16, "Fractal");
      add_float(n.attrs, "roughness", "Roughness", 1.f, 0.f, 2.f, "Fractal")
          .tooltip = "1 keeps the same detail at every scale. Lower is\n"
                     "smoother, higher is grittier.";
      add_float(n.attrs, "gain", "Gain", 1.f, 0.2f, 6.f, "Fractal");
      add_float(n.attrs, "distortion", "Distortion", 0.f, 0.f, 1.f, "Fractal")
          .tooltip = "Smears the sampling position with a low-frequency noise,\n"
                     "which breaks up the lattice the noise sits on.";
      add_choice(n.attrs, "landscape", "Shape",
                 {"Plain", "Ridges", "Billows", "Ridge mix", "Billow/ridge mix"},
                 0, "Fractal");
      add_float(n.attrs, "warp_amount", "Warp by input", 0.3f, 0.f, 2.f, "Fractal")
          .tooltip = "How far the warp input displaces the sample position.";
      add_float(n.attrs, "bias", "Bias", 0.5f, 0.01f, 0.99f, "Filter")
          .tooltip = "Moves the midpoint of the pattern: below 0.5 the colour\n"
                     "map's left end takes more of the surface.";
      add_float(n.attrs, "contrast", "Gain", 0.5f, 0.01f, 0.99f, "Filter")
          .tooltip = "Pushes values away from the middle. High values give\n"
                     "hard-edged patches rather than a smooth wash.";
      add_gradient(n.attrs, "gradient", "Colour map",
                   {{0.0f, 0.20f, 0.17f, 0.14f, 1},
                    {0.35f, 0.38f, 0.33f, 0.26f, 1},
                    {0.65f, 0.52f, 0.47f, 0.38f, 1},
                    {1.0f, 0.72f, 0.69f, 0.62f, 1}},
                   "Colour")
          .tooltip = "Colour and alpha both come from here. A stop's alpha\n"
                     "becomes the texture's alpha, so one fractal can decide\n"
                     "a layer's look and its presence at the same time.";
    },
    [](Node &n) {
      TextureRGBA &tex = n.out_tex("texture");
      Heightmap &msk = n.out_hmap("mask");
      const Heightmap *warp = n.in_hmap("warp");
      const Heightmap *gate = n.in_hmap("mask");
      if (warp && warp->empty()) warp = nullptr;
      if (gate && gate->empty()) gate = nullptr;

      fractal::Params P;
      P.base = n.attrs.get_choice("base");
      P.wavelength = n.attrs.get_f("wavelength", 0.25f);
      P.octaves = n.attrs.get_i("octaves", 8);
      P.roughness = n.attrs.get_f("roughness", 1.f);
      P.gain = n.attrs.get_f("gain", 1.f);
      P.distortion = n.attrs.get_f("distortion", 0.f);
      P.landscape = n.attrs.get_choice("landscape");
      const uint32_t seed = n.attrs.get_seed("seed");
      const float wamt = n.attrs.get_f("warp_amount", 0.3f);
      const float bias = n.attrs.get_f("bias", 0.5f);
      const float gain = n.attrs.get_f("contrast", 0.5f);
      const Attribute *ga = n.attrs.find("gradient");
      static const std::vector<GradientStop> none;
      const std::vector<GradientStop> &stops = ga ? ga->stops : none;

      parallel_rows(tex.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < tex.w; ++x) {
            float u = (x + 0.5f) / tex.w, v = (y + 0.5f) / tex.h;
            float sx = u, sy = v;
            if (warp) {
              // one field, two offsets: cheaper than a second fractal and
              // enough to stop the pattern reading as a grid
              float d = warp->sample(u, v) * wamt * P.wavelength;
              sx += d;
              sy += d * 0.7f;
            }
            float t = fractal::eval(sx, sy, seed, P) * 0.5f + 0.5f;
            t = bias_gain(t, bias, gain);
            float rgba[4];
            eval_gradient(stops, t, rgba);
            float a = rgba[3];
            if (gate) a *= std::clamp(gate->sample(u, v), 0.f, 1.f);
            float *p = tex.px(x, y);
            p[0] = rgba[0]; p[1] = rgba[1]; p[2] = rgba[2]; p[3] = a;
            msk.at(x, y) = a;
          }
      });
    })

// -------------------------------------------------------- one material layer
REGISTER_NODE(
    MaterialLayer, "Material",
    "One layer of a material stack: its own maps, its own mask, and its own reaction to altitude, slope and orientation",
    [](Node &n) {
      n.add_in("below albedo", DataType::Texture, true);
      n.add_in("below normal", DataType::Texture, true);
      n.add_in("below rough", DataType::Texture, true);
      n.add_in("albedo", DataType::Texture, true);
      n.add_in("normal", DataType::Texture, true);
      n.add_in("roughness", DataType::Texture, true);
      n.add_in("mask", DataType::Heightmap, true);
      n.add_in("terrain", DataType::Heightmap, true);
      n.add_out("albedo", DataType::Texture);
      n.add_out("normal", DataType::Texture);
      n.add_out("roughness", DataType::Texture);
      n.add_out("presence", DataType::Heightmap);

      add_text(n.attrs, "name", "Name", "Layer", "Layer");
      add_bool(n.attrs, "enabled", "Visible", true, "Layer");
      add_float(n.attrs, "opacity", "Opacity", 1.f, 0.f, 1.f, "Layer")
          .tooltip = "Overall presence of the layer, within whatever the\n"
                     "environment constraints below already allow. It cannot\n"
                     "put the layer anywhere they exclude.";
      add_choice(n.attrs, "blend", "Blend",
                 {"Normal", "Cover", "Colour only", "Add", "Multiply"}, 0,
                 "Layer")
          .tooltip = "Normal: ordinary alpha-over.\n"
                     "Cover: colour switches without a ramp, only the normal\n"
                     "transitions, so the layer reads as sitting on top.\n"
                     "Colour only: takes colour from here, everything else\n"
                     "from below.";
      add_bool(n.attrs, "invert_mask", "Invert mask", false, "Layer");
      add_float(n.attrs, "rough_value", "Roughness", 0.8f, 0.f, 1.f, "Layer")
          .tooltip = "Used where this layer has no roughness map connected.";
      add_float(n.attrs, "normal_add", "Add to normals below", 1.f, 0.f, 1.f,
                "Layer")
          .tooltip = "1: this layer's relief adds to the layer beneath, the\n"
                     "way lichen sits on rock. 0: it replaces it, the way\n"
                     "snow flattens what it covers.";

      add_bool(n.attrs, "use_altitude", "By altitude", false, "Altitude");
      add_range(n.attrs, "altitude", "Altitude band", 0.f, 1.f, 0.f, 1.f,
                "Altitude")
          .tooltip = "As a fraction of the terrain's own height range.";
      add_float(n.attrs, "altitude_fuzz", "Fade", 0.08f, 0.f, 0.5f, "Altitude");

      add_bool(n.attrs, "use_slope", "By slope", false, "Slope");
      add_range(n.attrs, "slope", "Slope band", 0.f, 30.f, 0.f, 90.f, "Slope")
          .tooltip = "Degrees from horizontal. 0 is flat, 90 is a cliff.";
      add_float(n.attrs, "slope_fuzz", "Fade", 6.f, 0.f, 45.f, "Slope");

      add_bool(n.attrs, "use_orientation", "By orientation", false,
               "Orientation");
      add_float(n.attrs, "orientation", "Faces", 0.f, 0.f, 360.f, "Orientation")
          .tooltip = "Compass direction the surface looks towards, in degrees.\n"
                     "0 is north. North faces hold snow; south faces dry out.";
      add_float(n.attrs, "orient_width", "Arc", 60.f, 5.f, 180.f, "Orientation")
          .tooltip = "How far either side of that direction still counts.";
      add_float(n.attrs, "orient_fuzz", "Fade", 20.f, 0.f, 90.f, "Orientation");

      add_float(n.attrs, "tiles", "Tiling", 1.f, 0.05f, 64.f, "Placement")
          .tooltip = "How many times this layer's own maps repeat across the\n"
                     "terrain. Does not affect the mask or the constraints.";
      add_vec2(n.attrs, "offset", "Offset", 0.f, 0.f, -4.f, 4.f, "Placement");
      add_float(n.attrs, "rotation", "Rotation", 0.f, -180.f, 180.f, "Placement");
    },
    [](Node &n) {
      const TextureRGBA *BA = n.in_tex("below albedo");
      const TextureRGBA *BN = n.in_tex("below normal");
      const TextureRGBA *BR = n.in_tex("below rough");
      const TextureRGBA *LA = n.in_tex("albedo");
      const TextureRGBA *LN = n.in_tex("normal");
      const TextureRGBA *LR = n.in_tex("roughness");
      const Heightmap *MK = n.in_hmap("mask");
      const Heightmap *TR = n.in_hmap("terrain");
      auto live = [](const TextureRGBA *t) { return t && !t->empty() ? t : nullptr; };
      BA = live(BA); BN = live(BN); BR = live(BR);
      LA = live(LA); LN = live(LN); LR = live(LR);
      if (MK && MK->empty()) MK = nullptr;
      if (TR && TR->empty()) TR = nullptr;
      if (!BA && !LA) {
        n.error = "connect an albedo, here or below";
        return;
      }

      const bool on = n.attrs.get_b("enabled", true);
      const float opacity = n.attrs.get_f("opacity", 1.f);
      const int blend = n.attrs.get_choice("blend");
      const bool inv = n.attrs.get_b("invert_mask", false);
      const float rval = n.attrs.get_f("rough_value", 0.8f);
      const float naddw = n.attrs.get_f("normal_add", 1.f);
      const bool ua = n.attrs.get_b("use_altitude", false);
      const bool us = n.attrs.get_b("use_slope", false);
      const bool uo = n.attrs.get_b("use_orientation", false);
      float alo, ahi, slo, shi;
      n.attrs.get_range("altitude", alo, ahi);
      n.attrs.get_range("slope", slo, shi);
      const float afz = n.attrs.get_f("altitude_fuzz", 0.08f);
      const float sfz = n.attrs.get_f("slope_fuzz", 6.f);
      const float odir = n.attrs.get_f("orientation", 0.f);
      const float owid = n.attrs.get_f("orient_width", 60.f);
      const float ofz = n.attrs.get_f("orient_fuzz", 20.f);
      const float tiles = n.attrs.get_f("tiles", 1.f);
      float ox = 0, oy = 0;
      n.attrs.get_vec2("offset", ox, oy);
      const float rot = n.attrs.get_f("rotation", 0.f) * PI_F / 180.f;
      const float cs = std::cos(rot), sn = std::sin(rot);

      // altitude is read as a fraction of the terrain's own range, so a band
      // set on one terrain still means "the top third" on another
      float hlo = 0.f, hhi = 1.f;
      if (TR && ua) {
        hlo = 1e30f; hhi = -1e30f;
        for (float v : TR->v) { hlo = std::min(hlo, v); hhi = std::max(hhi, v); }
        if (hhi - hlo < 1e-6f) hhi = hlo + 1.f;
      }
      // world size per texel, so a slope in degrees means degrees
      const float cell = TR ? 1.f / std::max(TR->w, 1) : 1.f;

      TextureRGBA &oa = n.out_tex("albedo");
      TextureRGBA &onm = n.out_tex("normal");
      TextureRGBA &orh = n.out_tex("roughness");
      Heightmap &opz = n.out_hmap("presence");

      parallel_rows(oa.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < oa.w; ++x) {
            const float u = (x + 0.5f) / oa.w, v = (y + 0.5f) / oa.h;

            // ---- presence -------------------------------------------------
            float p = on ? opacity : 0.f;
            if (p > 0.f && MK) {
              float m = std::clamp(MK->sample(u, v), 0.f, 1.f);
              p *= inv ? 1.f - m : m;
            } else if (p > 0.f && inv) {
              p = 0.f; // inverting an absent mask means "nowhere"
            }
            if (p > 0.f && TR && (ua || us || uo)) {
              float dx = 0, dy = 0;
              int tx = std::min((int)(u * TR->w), TR->w - 1);
              int ty = std::min((int)(v * TR->h), TR->h - 1);
              TR->gradient_at(tx, ty, dx, dy);
              if (ua) {
                float h = (TR->sample(u, v) - hlo) / (hhi - hlo);
                p *= band(h, alo, ahi, afz);
              }
              if (p > 0.f && (us || uo)) {
                float gx = dx / cell, gy = dy / cell;
                if (us) {
                  float deg = std::atan(std::sqrt(gx * gx + gy * gy)) * 180.f / PI_F;
                  p *= band(deg, slo, shi, sfz);
                }
                if (p > 0.f && uo) {
                  // the surface looks the way it falls: down the gradient
                  float az = std::atan2(-gx, -gy) * 180.f / PI_F;
                  if (az < 0.f) az += 360.f;
                  float d = arc(az, odir);
                  // flat ground has no facing, so it never counts as oriented
                  float flat = std::sqrt(gx * gx + gy * gy);
                  p *= band(d, 0.f, owid, ofz) *
                       std::clamp(flat * 8.f, 0.f, 1.f);
                }
              }
            }
            p = std::clamp(p, 0.f, 1.f);
            opz.at(x, y) = p;

            // ---- this layer's own maps, placed --------------------------
            float lu = u - 0.5f, lv = v - 0.5f;
            float ru = lu * cs - lv * sn, rv = lu * sn + lv * cs;
            ru = ru * tiles + 0.5f + ox;
            rv = rv * tiles + 0.5f + oy;

            float ca[4] = {0.5f, 0.5f, 0.5f, 1.f};
            if (LA) tex_wrap(*LA, ru, rv, ca);
            float cn[4] = {0.5f, 0.5f, 1.f, 1.f};
            if (LN) tex_wrap(*LN, ru, rv, cn);
            float cr = rval;
            if (LR) { float t4[4]; tex_wrap(*LR, ru, rv, t4); cr = t4[0]; }

            float ba[4] = {0.f, 0.f, 0.f, 1.f};
            if (BA) tex_wrap(*BA, u, v, ba);
            else { ba[0] = ca[0]; ba[1] = ca[1]; ba[2] = ca[2]; }
            float bn[4] = {0.5f, 0.5f, 1.f, 1.f};
            if (BN) tex_wrap(*BN, u, v, bn);
            float br = rval;
            if (BR) { float t4[4]; tex_wrap(*BR, u, v, t4); br = t4[0]; }

            // ---- composite ----------------------------------------------
            float ka = p, kn = p, kr = p;
            float mix[3];
            switch (blend) {
              case 1: // Cover: colour switches, only the normal ramps
                ka = p > 0.5f ? 1.f : 0.f;
                for (int c = 0; c < 3; ++c)
                  mix[c] = ba[c] + (ca[c] - ba[c]) * ka;
                break;
              case 2: // Colour only: everything else stays as it was below
                kn = kr = 0.f;
                for (int c = 0; c < 3; ++c)
                  mix[c] = ba[c] + (ca[c] - ba[c]) * ka;
                break;
              case 3: // Add
                for (int c = 0; c < 3; ++c)
                  mix[c] = std::min(ba[c] + ca[c] * ka, 1.f);
                break;
              case 4: // Multiply
                for (int c = 0; c < 3; ++c)
                  mix[c] = ba[c] * (1.f - ka + ca[c] * ka);
                break;
              default: // Normal
                for (int c = 0; c < 3; ++c)
                  mix[c] = ba[c] + (ca[c] - ba[c]) * ka;
            }
            float *pa = oa.px(x, y);
            pa[0] = mix[0]; pa[1] = mix[1]; pa[2] = mix[2];
            pa[3] = std::max(ba[3], ka);

            // normals: add this layer's tangent-space tilt to what is below,
            // or replace it, on the naddw dial
            float lx = cn[0] * 2.f - 1.f, ly = cn[1] * 2.f - 1.f;
            float ux = bn[0] * 2.f - 1.f, uy = bn[1] * 2.f - 1.f;
            float nx = ux * (1.f - kn * (1.f - naddw)) + lx * kn;
            float ny = uy * (1.f - kn * (1.f - naddw)) + ly * kn;
            float nz = std::sqrt(std::max(1.f - nx * nx - ny * ny, 1e-4f));
            float *pn = onm.px(x, y);
            pn[0] = nx * 0.5f + 0.5f; pn[1] = ny * 0.5f + 0.5f;
            pn[2] = nz; pn[3] = 1.f;

            float r = br + (cr - br) * kr;
            float *pr = orh.px(x, y);
            pr[0] = pr[1] = pr[2] = r;
            pr[3] = 1.f;
          }
      });
    })

} // namespace gpx
