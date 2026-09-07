// Geekatplay TerraForge — which way the ground faces, and what it can see.
//
// Three things every reference application has and we did not. Vue calls it
// Orientation, Gaea calls it Aspect, World Machine calls it Select
// Orientation, Terragen reaches it through the surface normal: the compass
// direction a slope faces. It is the difference between the north side of a
// mountain and the south side, and no combination of altitude and steepness
// can express it — which is why the snow line in our scenes has always run
// straight round a peak instead of sagging on the sunny side.
//
// The other two are the long-range ones. SelectCavities already finds local
// hollows, but it looks a few pixels out; whether a valley floor sees the sky
// at all is decided by a ridge a thousand pixels away. That is a horizon
// problem, and it is the same horizon whether the question is "how much sky"
// or "how much sun".
//
// The horizon is computed exactly, in linear time, by the convex-hull sweep
// (Stewart 1998) rather than by marching rays. Ray marching at 1024² with 16
// directions and 32 samples is half a billion samples and still misses the
// ridge between them; the sweep is 16 passes over the grid and is not an
// approximation at all. Performance is the reason it is worth the fifty lines.
#include "gpx/node_graph.hpp"
#include "gpx/horizon.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/parallel.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace gpx {

namespace {

constexpr float PI = 3.14159265358979323846f;

void add_sky_controls(Node &n) {
  add_int(n.attrs, "directions", "Directions", 16, 4, 64, "Horizon")
      .tooltip = "How many compass directions the horizon is measured in.\n"
                 "Each one is a single linear pass over the terrain, so the\n"
                 "cost is flatly proportional to this: 8 is enough for a\n"
                 "soft ambient look, 32 for hard terrain shadows that have\n"
                 "to line up with the sun.";
  add_float(n.attrs, "relief", "Vertical scale", 0.25f, 0.01f, 4.f, "Horizon")
      .tooltip = "The terrain's height range as a fraction of the tile's\n"
                 "width - 0.25 means a kilometre of ground rising 250 m.\n"
                 "This is what turns the heightmap into real angles, so\n"
                 "match it to the scene's own vertical scale or the\n"
                 "shadows will be too long or too short.";
}

} // namespace

REGISTER_NODE(
    SelectAspect, "Mask",
    "Selects ground by the compass direction it faces - the shaded north "
    "sides that hold snow, the south sides that bake dry",
    [](Node &n) {
      setup_selector(n);
      add_float(n.attrs, "facing", "Facing", 0.f, 0.f, 360.f, "Selection")
          .tooltip = "The compass direction the selected slopes look\n"
                     "towards, in degrees: 0 north, 90 east, 180 south,\n"
                     "270 west. This is the control nothing else in the\n"
                     "mask set can stand in for - altitude and steepness\n"
                     "cannot tell one side of a peak from the other.";
      add_float(n.attrs, "spread", "Spread", 60.f, 5.f, 180.f, "Selection")
          .tooltip = "How wide an arc counts as facing that way, in\n"
                     "degrees either side. Narrow picks one face of a\n"
                     "ridge; 180 selects everything and is only useful\n"
                     "with the flatness cut below.";
      add_float(n.attrs, "min_slope", "Ignore flatter than", 0.05f, 0.f, 1.f,
                "Selection")
          .tooltip = "Flat ground faces nowhere in particular, and asking\n"
                     "it which way it points returns noise. Ground gentler\n"
                     "than this is dropped from the selection, which is\n"
                     "what keeps a valley floor from speckling.";
      add_float(n.attrs, "scale", "Feature scale", 0.02f, 0.002f, 0.2f,
                "Selection")
          .tooltip = "How large a feature has to be to count as facing\n"
                     "somewhere. Small scales read every pebble and give a\n"
                     "speckled mask; large ones read whole hillsides, which\n"
                     "is what you want when the question is which side of\n"
                     "the mountain this is.";
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &m = n.out_hmap("mask");
      const float facing = n.attrs.get_f("facing", 0.f) * PI / 180.f;
      const float spread = n.attrs.get_f("spread", 60.f) * PI / 180.f;
      const float soft = n.attrs.get_f("smoothing", 0.1f);
      const float min_slope = n.attrs.get_f("min_slope", 0.05f);
      float mn, mx;
      in->minmax(mn, mx);
      const float amp = (mx - mn) > 1e-12f ? mx - mn : 1.f;
      const int w = in->w;
      const float wx = std::sin(facing), wy = -std::cos(facing);
      // A Sobel with its taps spread `r` apart rather than one. A one-pixel
      // gradient on a fractal reports the pebble under your foot, and the
      // mask comes out as speckle; spreading the taps asks the question at
      // the scale of the hillside, which is the scale the question is about.
      const int r = std::max(1, (int)std::lround(
                                    n.attrs.get_f("scale", 0.02f) * w));
      const float k = w / (amp * 8.f * r);
      parallel_rows(in->h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < w; ++x) {
            const float dx =
                (in->atc(x + r, y - r) + 2.f * in->atc(x + r, y) +
                 in->atc(x + r, y + r)) -
                (in->atc(x - r, y - r) + 2.f * in->atc(x - r, y) +
                 in->atc(x - r, y + r));
            const float dy =
                (in->atc(x - r, y + r) + 2.f * in->atc(x, y + r) +
                 in->atc(x + r, y + r)) -
                (in->atc(x - r, y - r) + 2.f * in->atc(x, y - r) +
                 in->atc(x + r, y - r));
            // downhill is the way water runs, and the way a slope faces
            const float gx = -dx * k, gy = -dy * k;
            const float steep = std::sqrt(gx * gx + gy * gy);
            if (steep < min_slope) {
              m.at(x, y) = 0.f;
              continue;
            }
            // cos of the angle between the downhill direction and the one
            // asked for; 1 is dead on, -1 is the opposite face
            const float align = (gx * wx + gy * wy) / steep;
            const float ang = std::acos(std::clamp(align, -1.f, 1.f));
            m.at(x, y) = std::clamp((spread - ang) / (soft * PI) + 0.5f, 0.f,
                                    1.f);
          }
      });
      // Deliberately not finish_mask: it renormalises to 0..1, and a mask
      // that selected nothing would come back selecting everything. The
      // invert is still honoured.
      if (n.attrs.get_b("invert"))
        for (auto &v : m.v) v = 1.f - v;
    })

REGISTER_NODE(
    SkyExposure, "Analysis",
    "How much of the sky each point can see - open ridges near 1, valley "
    "floors shut in by their own walls near 0",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      add_sky_controls(n);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      const int dirs = n.attrs.get_i("directions", 16);
      const Heightmap z = in_cell_units(*in, n.attrs.get_f("relief", 0.25f));
      Heightmap horizon(in->w, in->h);
      std::fill(out.v.begin(), out.v.end(), 0.f);
      for (int d = 0; d < dirs; ++d) {
        const float a = 2.f * PI * d / dirs;
        horizon_pass(z, std::cos(a), std::sin(a), horizon);
        parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
          for (size_t i = i0; i < i1; ++i) {
            // Dozier & Frew: the visible fraction of the sky in one azimuth
            // is cos^2 of the horizon's elevation. A horizon slope of s is
            // an elevation of atan(s), and cos^2(atan(s)) is 1/(1+s^2) - so
            // the trig cancels and this is one divide per cell.
            const float s = horizon.v[i];
            out.v[i] += 1.f / (1.f + s * s);
          }
        });
      }
      const float inv = 1.f / (float)dirs;
      for (auto &v : out.v) v *= inv;
    })

REGISTER_NODE(
    SunExposure, "Analysis",
    "How much direct sun reaches each point - the ground a ridge keeps in "
    "shadow all day, which is where the snow stays",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      add_float(n.attrs, "azimuth", "Sun azimuth", 180.f, 0.f, 360.f, "Sun")
          .tooltip = "Which way the sun lies, in degrees around the\n"
                     "compass: 0 north, 90 east, 180 south, 270 west. Match\n"
                     "it to the scene's sun and the shadows agree with the\n"
                     "render.";
      add_float(n.attrs, "altitude", "Sun altitude", 30.f, 1.f, 89.f, "Sun")
          .tooltip = "How high the sun stands above the horizon, in\n"
                     "degrees. Low sun throws long shadows and picks out\n"
                     "every fold in the ground; overhead sun shadows almost\n"
                     "nothing.";
      add_float(n.attrs, "arc", "Sweep", 90.f, 0.f, 270.f, "Sun")
          .tooltip = "How far the sun travels, in degrees of azimuth, and\n"
                     "the result is the fraction of that journey each point\n"
                     "is lit for. Zero is one instant - a hard shadow map.\n"
                     "Ninety is a morning, and gives the soft edges you\n"
                     "want for deciding where snow survives rather than\n"
                     "where a shadow falls right now.";
      add_sky_controls(n);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      const float az = n.attrs.get_f("azimuth", 180.f) * PI / 180.f;
      const float alt = n.attrs.get_f("altitude", 30.f) * PI / 180.f;
      const float arc = n.attrs.get_f("arc", 90.f) * PI / 180.f;
      const int dirs = n.attrs.get_i("directions", 16);
      // Only the azimuths the sun actually crosses are swept. A one-instant
      // sun is one pass over the terrain, not sixteen.
      const int steps = arc <= 1e-6f
                            ? 1
                            : std::max(2, (int)std::lround(dirs * arc /
                                                           (2.f * PI)));
      const float sun_slope = std::tan(alt);
      const Heightmap z = in_cell_units(*in, n.attrs.get_f("relief", 0.25f));
      Heightmap horizon(in->w, in->h);
      std::fill(out.v.begin(), out.v.end(), 0.f);
      for (int s = 0; s < steps; ++s) {
        const float t = steps == 1 ? 0.5f : (float)s / (float)(steps - 1);
        const float a = az + (t - 0.5f) * arc;
        // the sweep looks along the ray; the sun is what the ray points at,
        // so this is the direction *towards* the sun
        horizon_pass(z, std::sin(a), -std::cos(a), horizon);
        parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
          for (size_t i = i0; i < i1; ++i)
            if (horizon.v[i] < sun_slope) out.v[i] += 1.f;
        });
      }
      const float inv = 1.f / (float)steps;
      for (auto &v : out.v) v *= inv;
    })

} // namespace gpx
