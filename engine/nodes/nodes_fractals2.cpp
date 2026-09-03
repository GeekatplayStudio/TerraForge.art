// Geekatplay TerraForge — Vue's Terrain Fractal 2 and Rocky Mountains
// fractals (Reference Manual p876-880), on the shared fractal engine.
//
// TerrainFractal2: rocks emerging from sedimentary soil. Low harmonics decide
// regions of rock density (Overall aspect), a soil layer of adjustable
// thickness hides rocks where it is thick (Ground aspect), and an optional
// stratification follows the relief and shows mostly on rough ground.
//
// RockyMountains: irregular ridge networks added iteration by iteration, as
// separate mountain blocks or as basins between ridges, each iteration
// stretched along its own direction, with optional rock overlays and an
// eroded variant. Both report rough areas on the second output.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/fractal_core.hpp"
#include "gpx/parallel.hpp"

namespace gpx {

namespace {

void setup_base(Node &n) {
  n.add_in("envelope", DataType::Heightmap, true);
  n.add_out("output");
  n.add_out("rough_areas");
  add_seed(n.attrs, "seed", "Seed", 0, "Base");
  add_float(n.attrs, "wavelength", "Wavelength", 0.35f, 0.01f, 4.f, "Base");
  add_int(n.attrs, "octaves", "Iterations", 8, 1, 16, "Base");
  add_float(n.attrs, "scale_ratio", "Scale ratio", 0.5f, 0.1f, 0.9f, "Base");
  add_float(n.attrs, "roughness", "Roughness", 1.f, 0.f, 2.f, "Base");
  add_float(n.attrs, "gain", "Gain", 1.f, 0.2f, 10.f, "Base");
  add_float(n.attrs, "distortion", "Distortion", 0.f, 0.f, 1.f, "Base");
}

fractal::Params read_base(const Node &n) {
  fractal::Params P;
  P.wavelength = n.attrs.get_f("wavelength", 0.35f);
  P.octaves = n.attrs.get_i("octaves", 8);
  P.scale_ratio = n.attrs.get_f("scale_ratio", 0.5f);
  P.roughness = n.attrs.get_f("roughness", 1.f);
  P.gain = n.attrs.get_f("gain", 1.f);
  P.distortion = n.attrs.get_f("distortion", 0.f);
  return P;
}

// A low-frequency 0..1 field: the regions (rock density, soil thickness).
inline float region(float x, float y, uint32_t seed, float scale, float smooth,
                    float contrast) {
  fractal::Params R;
  R.wavelength = scale;
  R.octaves = 3;
  R.roughness = 0.8f;
  float v = fractal::eval(x, y, seed, R) * 0.5f + 0.5f;
  // contrast around the middle, then softened
  v = 0.5f + (v - 0.5f) * contrast;
  float e = 0.15f + smooth * 0.35f;
  return std::clamp((v - 0.5f + e) / (2.f * e), 0.f, 1.f);
}

} // namespace

REGISTER_NODE(
    TerrainFractal2, "Primitive",
    "Vue's Terrain Fractal 2: rocks emerging from sedimentary soil, with regions of rock density, soil thickness, buoyancy and relief-following strata",
    [](Node &n) {
      setup_base(n);
      add_float(n.attrs, "turbulence", "Turbulence", 0.3f, 0.f, 1.f, "Overall aspect")
          .tooltip = "Overall distortion of the terrain by its first harmonics.";
      add_float(n.attrs, "turb_damping", "Turbulence damping", 0.5f, 0.f, 1.f,
                "Overall aspect")
          .tooltip = "How much the first octaves' turbulence carries into\n"
                     "the finer ones.";
      add_float(n.attrs, "ls_smooth", "Large scale smoothness", 0.5f, 0.f, 1.f,
                "Overall aspect")
          .tooltip = "Softness of the transition from low to high rock\n"
                     "density regions.";
      add_float(n.attrs, "ls_contrast", "Large scale contrast", 1.f, 0.f, 3.f,
                "Overall aspect")
          .tooltip = "Range over which the rock population can vary.";
      add_float(n.attrs, "buoyancy", "Buoyancy", 0.2f, -1.f, 1.f, "Overall aspect")
          .tooltip = "+: low average altitude with rocks rising above it.\n"
                     "-: features dig below a high average. 0: around zero.";
      add_float(n.attrs, "bump_surge", "Bump surge", 0.5f, 0.f, 2.f, "Ground aspect")
          .tooltip = "How much the rocks spring out of the ground.";
      add_float(n.attrs, "rock_abundance", "Rock abundance", 0.5f, 0.f, 1.f,
                "Ground aspect");
      add_float(n.attrs, "soil_thickness", "Soil thickness", 0.4f, 0.f, 1.f,
                "Ground aspect")
          .tooltip = "Thin: more rocks show and smooth areas keep some\n"
                     "roughness. Thick: rocks buried, smooth areas smooth.";
      add_float(n.attrs, "rock_dispersion", "Rock dispersion", 0.3f, 0.f, 1.f,
                "Ground aspect")
          .tooltip = "Scattered over the landscape rather than gathered.";
      add_float(n.attrs, "strata_strength", "Processing strength", 0.f, 0.f, 1.f,
                "Strata processing");
      add_float(n.attrs, "strata_spacing", "Layer spacing", 0.08f, 0.01f, 0.5f,
                "Strata processing");
      add_float(n.attrs, "strata_offset", "Offset", 0.f, -0.5f, 0.5f,
                "Strata processing");
      setup_post(n);
    },
    [](Node &n) {
      Heightmap &out = n.out_hmap("output");
      Heightmap &rough = n.out_hmap("rough_areas");
      const Heightmap *env = n.in_hmap("envelope");
      const uint32_t seed = n.attrs.get_seed("seed");
      fractal::Params base = read_base(n);
      base.landscape = fractal::RIDGE_MIX;
      base.blend = 0.4f;
      fractal::Params rocks = base;
      rocks.landscape = fractal::RIDGES;
      rocks.wavelength = base.wavelength * 0.25f;
      rocks.octaves = std::max(3, base.octaves - 2);
      rocks.roughness = std::min(2.f, base.roughness * 1.3f);
      const float turb = n.attrs.get_f("turbulence", 0.3f);
      const float tdamp = n.attrs.get_f("turb_damping", 0.5f);
      const float ls_smooth = n.attrs.get_f("ls_smooth", 0.5f);
      const float ls_contrast = n.attrs.get_f("ls_contrast", 1.f);
      const float buoy = n.attrs.get_f("buoyancy", 0.2f);
      const float surge = n.attrs.get_f("bump_surge", 0.5f);
      const float abundance = n.attrs.get_f("rock_abundance", 0.5f);
      const float soil = n.attrs.get_f("soil_thickness", 0.4f);
      const float disp = n.attrs.get_f("rock_dispersion", 0.3f);
      const float s_str = n.attrs.get_f("strata_strength", 0.f);
      const float s_sp = n.attrs.get_f("strata_spacing", 0.08f);
      const float s_off = n.attrs.get_f("strata_offset", 0.f);
      const int w = out.w, h = out.h;
      parallel_rows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < w; ++x) {
            float u = x / float(w), v = y / float(h);
            // turbulence: the first harmonics push the coordinates about,
            // damped so the fine detail is not dragged along with them
            float tx = noise::perlin(u / base.wavelength + 3.1f, v / base.wavelength + 7.7f,
                                     seed ^ 0x1b873593u);
            float ty = noise::perlin(u / base.wavelength - 9.2f, v / base.wavelength + 1.4f,
                                     seed ^ 0xcc9e2d51u);
            float ub = u + tx * turb * 0.3f, vb = v + ty * turb * 0.3f;
            float uf = u + tx * turb * 0.3f * (1.f - tdamp);
            float vf = v + ty * turb * 0.3f * (1.f - tdamp);
            float rb = 0.f, rr = 0.f;
            float ground = fractal::eval(ub, vb, seed, base, &rb);
            // where the rocks live: regions of density, less where soil is thick
            float density = region(u, v, seed ^ 0x85ebca6bu, base.wavelength * 2.5f,
                                   ls_smooth, ls_contrast);
            float soil_here = region(u, v, seed ^ 0xe6546b64u,
                                     base.wavelength * (1.5f + disp * 2.f), 0.6f, 1.f);
            float cover = std::clamp(soil - (1.f - soil_here) * 0.6f, 0.f, 1.f);
            float rock_w = abundance * density * (1.f - cover);
            rock_w = rock_w * (1.f - disp) + abundance * disp * (1.f - cover) * 0.6f;
            float rock = fractal::eval(uf, vf, seed ^ 0x9e3779b9u, rocks, &rr);
            rock = std::max(rock, 0.f); // rocks only rise
            float a = ground * (1.f - 0.4f * rock_w) + rock * surge * rock_w;
            // buoyancy: shift the mean so features rise above or dig below
            a += buoy * (rock_w * 0.5f - 0.25f);
            // strata that follow the relief, showing on rough ground only
            if (s_str > 0.f) {
              float layer = (a + s_off) / std::max(s_sp, 1e-3f);
              float stepped = std::floor(layer) * s_sp - s_off;
              float t = layer - std::floor(layer);
              float edge = t * t * (3.f - 2.f * t);
              float strat = stepped + edge * s_sp;
              float vis = std::clamp(rb * 1.5f + rock_w, 0.f, 1.f);
              a = a * (1.f - s_str * vis) + strat * s_str * vis;
            }
            if (env && !env->empty()) a *= std::clamp(env->atc(x, y), 0.f, 1.f);
            out.at(x, y) = a;
            rough.at(x, y) = std::clamp(rb * (1.f - rock_w) + rr * rock_w + rock_w * 0.5f,
                                        0.f, 1.f);
          }
      });
      apply_post(n, out);
    })

REGISTER_NODE(
    RockyMountains, "Primitive",
    "Vue's Rocky Mountains fractal: irregular ridge networks added per iteration, as separate mountains or basins between ridges, stretched, with optional rocks and an eroded variant",
    [](Node &n) {
      setup_base(n);
      add_bool(n.attrs, "separate", "Separate mountains", true, "Overall aspect")
          .tooltip = "On: independent mountain blocks side by side.\n"
                     "Off: basins separated by irregular ridges.";
      add_float(n.attrs, "scale_factor", "Scale factor", 0.55f, 0.3f, 0.9f,
                "Overall aspect")
          .tooltip = "How much smaller each new iteration's features are.";
      add_float(n.attrs, "flat_level", "Flat level (per iteration)", 0.3f, 0.f, 1.f,
                "Overall aspect")
          .tooltip = "Balance of smooth areas against ridged ones per iteration.";
      add_float(n.attrs, "ground_level", "Ground level", 0.f, -1.f, 1.f,
                "Overall aspect")
          .tooltip = "Sinks the fractal into the ground.";
      add_int(n.attrs, "quality", "Subdivision quality", 1, 0, 2, "Overall aspect")
          .tooltip = "Higher hides the approximation's discontinuities,\n"
                     "at a cost.";
      add_float(n.attrs, "stretch_factor", "Stretch factor", 0.5f, 0.f, 1.f,
                "Stretch and distortion")
          .tooltip = "Each iteration is stretched along its own direction,\n"
                     "the way real ridge networks run.";
      add_choice(n.attrs, "rocks", "Optional rocks", {"None", "Correlated", "Everywhere"},
                 0, "Rocks");
      add_int(n.attrs, "rock_iteration", "Rock correlation", 2, 0, 8, "Rocks")
          .tooltip = "Rocks follow the ridges seen at this iteration.";
      add_float(n.attrs, "rock_roughness", "Rock roughness", 1.f, 0.f, 2.f, "Rocks");
      add_float(n.attrs, "rock_height", "Rock height", 0.3f, 0.f, 1.f, "Rocks");
      add_bool(n.attrs, "eroded", "Eroded", false, "Rocks")
          .tooltip = "The Eroded Rocky Mountains variant: gullied flanks.";
      add_float(n.attrs, "rough_ref", "Rough areas: ref. feature size", 0.f, 0.f, 1.f,
                "Output");
      setup_post(n);
    },
    [](Node &n) {
      Heightmap &out = n.out_hmap("output");
      Heightmap &rough = n.out_hmap("rough_areas");
      const Heightmap *env = n.in_hmap("envelope");
      const uint32_t seed = n.attrs.get_seed("seed");
      fractal::Params base = read_base(n);
      const bool separate = n.attrs.get_b("separate", true);
      const float sf = n.attrs.get_f("scale_factor", 0.55f);
      const float flat = n.attrs.get_f("flat_level", 0.3f);
      const float ground = n.attrs.get_f("ground_level", 0.f);
      const int quality = n.attrs.get_i("quality", 1);
      const float stretch = n.attrs.get_f("stretch_factor", 0.5f);
      const int rocks = n.attrs.get_choice("rocks");
      const int rock_it = n.attrs.get_i("rock_iteration", 2);
      const float rock_rough = n.attrs.get_f("rock_roughness", 1.f);
      const float rock_h = n.attrs.get_f("rock_height", 0.3f);
      const bool eroded = n.attrs.get_b("eroded");
      const float rref = n.attrs.get_f("rough_ref", 0.f);
      const int w = out.w, h = out.h;
      const float jitter = quality == 0 ? 0.6f : quality == 1 ? 0.85f : 1.f;
      parallel_rows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < w; ++x) {
            float u = x / float(w), v = y / float(h);
            if (base.distortion > 0.f) {
              float ds = 1.f / base.wavelength;
              u += noise::perlin(u * ds + 5.f, v * ds + 2.f, seed ^ 0x27d4eb2fu) * base.distortion * 0.3f;
              v += noise::perlin(u * ds - 7.f, v * ds + 9.f, seed ^ 0x165667b1u) * base.distortion * 0.3f;
            }
            float a = 0.f, amp = 1.f, wl = base.wavelength, rgh = 0.f, rnorm = 0.f;
            float ridge_ref = 0.f;
            for (int i = 0; i < base.octaves; ++i) {
              // this iteration's own stretch direction
              float ang = (float)i * 2.39996323f;
              float c = std::cos(ang), s = std::sin(ang);
              float px = u * c - v * s, py = u * s + v * c;
              float sx = 1.f + stretch * 2.f, sy = 1.f;
              if (!separate && !eroded) sx = 1.f;
              float f1, f2;
              noise::worley(px / (wl * sx) + (float)i * 11.f, py / (wl * sy) - (float)i * 3.f,
                            seed + (uint32_t)i * 7919u, f1, f2, jitter);
              // blocks: a summit per cell; basins: the walls between cells
              float feat = separate ? std::clamp(1.f - f1 * 1.2f, 0.f, 1.f)
                                    : std::clamp((f2 - f1) * 1.5f, 0.f, 1.f);
              // flat level: below it the iteration adds nothing
              feat = std::max(feat - flat * 0.5f, 0.f) / std::max(1.f - flat * 0.5f, 1e-3f);
              feat = feat * feat * (3.f - 2.f * feat);
              if (i == rock_it) ridge_ref = feat;
              a += feat * amp;
              if (rref <= 0.f || wl < rref) {
                rgh += feat * amp;
                rnorm += amp;
              }
              amp *= sf * (0.6f + 0.4f * base.roughness);
              wl *= sf;
            }
            // rocks overlaid where the chosen iteration is ridged
            if (rocks) {
              fractal::Params rp;
              rp.wavelength = base.wavelength * 0.08f;
              rp.octaves = 4;
              rp.landscape = fractal::RIDGES;
              rp.roughness = rock_rough;
              float r = std::max(fractal::eval(u, v, seed ^ 0xdeadbeefu, rp), 0.f);
              float wgt = rocks == 2 ? 1.f : ridge_ref;
              a += r * rock_h * wgt;
              rgh += r * wgt * 0.5f;
              rnorm += 0.5f;
            }
            if (eroded) {
              // gullies down the flanks: a fine ridged noise subtracted
              // where the surface is neither summit nor floor
              fractal::Params ep;
              ep.wavelength = base.wavelength * 0.12f;
              ep.octaves = 5;
              ep.landscape = fractal::RIDGES;
              float g = std::max(fractal::eval(u, v, seed ^ 0x51ed270bu, ep), 0.f);
              float flank = std::clamp(a * 2.f, 0.f, 1.f) * (1.f - std::clamp(a - 0.5f, 0.f, 1.f) * 2.f);
              a -= g * 0.25f * flank;
            }
            a = a * 2.f - 1.f - ground;
            if (base.gain != 1.f)
              a = std::copysign(std::pow(std::fabs(std::clamp(a, -1.f, 1.f)), 1.f / base.gain), a);
            if (env && !env->empty()) a *= std::clamp(env->atc(x, y), 0.f, 1.f);
            out.at(x, y) = a;
            rough.at(x, y) = rnorm > 0.f ? std::clamp(rgh / rnorm, 0.f, 1.f) : 0.f;
          }
      });
      apply_post(n, out);
    })

} // namespace gpx
